/*
 * Differential test of the FDOS x86 -> Thumb-2 translator (src/fdos/xlat).
 *
 * Runs on an ARM Linux host or under qemu-arm (see run.sh). Random 16-bit
 * real-mode programs with loops are executed twice:
 *   - by the reference interpreter below, which reproduces XRIP's semantics
 *     and its lazy flags (src/xrip386/cpu.c) for the instructions used;
 *   - by the same interpreter with xlat_run() called at random "burst"
 *     boundaries, exactly as the firmware adapter does.
 * Registers (all 32 bits), resolved flags and memory must match.
 *
 *   xlat_test [programs] [first-seed]
 *   xlat_test --replay xlat.log [states]   regions logged by an XLAT_LOG firmware
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "xlat.h"

#define MEM_BYTES 0x110100u

static uint8_t mem[MEM_BYTES];

/* ---------------------------------------------------------------------- */
/* Host hooks                                                             */
/* ---------------------------------------------------------------------- */

void *xlat_test_calloc(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

/* ---------------------------------------------------------------------- */
/* Reference CPU (XRIP semantics)                                         */
/* ---------------------------------------------------------------------- */

typedef struct {
    uint32_t gpr[8];
    uint32_t seg_base[4];   /* ES CS SS DS */
    uint16_t cs, ip;
    uint32_t flags_result, flags_dst, flags_src, eflags;
    uint8_t flags_op, flags_cf;
    bool halted, bad;
} ref_t;

static ref_t R;
static unsigned long interp_calls, side_calls;

enum { FLAGS_ADD = 1, FLAGS_SUB, FLAGS_LOGIC };

static const uint8_t byteoff[8] = {0, 4, 8, 12, 1, 5, 9, 13};

static uint8_t parity_even(uint8_t v)
{
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1);
}

static uint8_t getreg8(int r) { return ((uint8_t *)R.gpr)[byteoff[r]]; }
static void putreg8(int r, uint8_t v) { ((uint8_t *)R.gpr)[byteoff[r]] = v; }
static uint16_t getreg16(int r) { return (uint16_t)R.gpr[r]; }
static void putreg16(int r, uint16_t v) { R.gpr[r] = (R.gpr[r] & 0xffff0000u) | v; }

static uint8_t rd8(uint32_t a) { return mem[a]; }
static uint16_t rd16(uint32_t a) { return (uint16_t)(mem[a] | mem[a + 1] << 8); }
static void wr8(uint32_t a, uint8_t v) { mem[a] = v; }
static void wr16(uint32_t a, uint16_t v) { mem[a] = (uint8_t)v; mem[a + 1] = (uint8_t)(v >> 8); }

static void lazy_flags(uint8_t kind, uint8_t bits, uint32_t dst, uint32_t src, uint32_t result)
{
    const uint8_t shift = 32 - bits;
    R.flags_dst = dst << shift;
    R.flags_src = (kind == FLAGS_SUB ? ~src : src) << shift;
    R.flags_result = result << shift;
    R.flags_op = kind << 5 | shift;
}
static void add_flags(uint8_t bits, uint32_t dst, uint32_t src)
{
    lazy_flags(FLAGS_ADD, bits, dst, src, dst + src);
    R.flags_cf = R.flags_result < R.flags_dst;
}
static void sub_flags(uint8_t bits, uint32_t dst, uint32_t src)
{
    R.flags_cf = dst << (32 - bits) < src << (32 - bits);
    lazy_flags(FLAGS_SUB, bits, dst, src, dst - src);
}
static void logic_flags(uint8_t bits, uint32_t result)
{
    lazy_flags(FLAGS_LOGIC, bits, result, result, result);
    R.flags_cf = 0;
}
static bool get_cf(void) { return R.flags_op ? R.flags_cf : (R.eflags & 1); }
static bool get_zf(void) { return R.flags_op ? R.flags_result == 0 : (R.eflags >> 6 & 1); }
static bool get_sf(void) { return R.flags_op ? R.flags_result >> 31 : (R.eflags >> 7 & 1); }
static bool get_of(void)
{
    return R.flags_op ? ((R.flags_result ^ R.flags_dst) & (R.flags_result ^ R.flags_src)) >> 31
                      : (R.eflags >> 11 & 1);
}
static void resolve_flags(void)
{
    const uint8_t shift = R.flags_op & 31, kind = R.flags_op >> 5;
    uint32_t value = get_cf() | parity_even((uint8_t)(R.flags_result >> shift)) << 2 |
                     get_zf() << 6 | get_sf() << 7 | get_of() << 11;
    uint32_t carries = R.flags_dst ^ R.flags_src ^ R.flags_result;
    if (kind == FLAGS_SUB) carries = ~carries;
    if (kind != FLAGS_LOGIC) value |= (carries >> shift) & 0x10;
    R.eflags = (R.eflags & ~0x8d5u) | value;
    R.flags_op = 0;
}
static uint32_t *flags_now(void)
{
    if (R.flags_op) resolve_flags();
    return &R.eflags;
}
#define SETF(bit, v) (*flags_now() = (*flags_now() & ~(1u << (bit))) | ((uint32_t)!!(v) << (bit)))
#define CF() (*flags_now() & 1u)

static uint32_t inc_dec(uint32_t value, bool decrement, uint8_t bits)
{
    if (!R.flags_op) R.flags_cf = R.eflags & 1;
    const uint32_t result = decrement ? value - 1 : value + 1;
    lazy_flags(decrement ? FLAGS_SUB : FLAGS_ADD, bits, value, 1, result);
    return result;
}

static bool condition(uint8_t code)
{
    bool result;
    switch ((code & 0x0f) >> 1) {
    case 0: result = get_of(); break;
    case 1: result = get_cf(); break;
    case 2: result = get_zf(); break;
    case 3: result = get_cf() || get_zf(); break;
    case 4: result = get_sf(); break;
    case 5: result = (*flags_now() >> 2) & 1; break;
    case 6: result = get_sf() != get_of(); break;
    default: result = get_zf() || get_sf() != get_of(); break;
    }
    return result != (code & 1);
}

static void flag_szp8(uint8_t v)
{
    uint32_t *f = flags_now();
    *f = (*f & ~0xc4u) | (v == 0 ? 0x40u : 0) | (v & 0x80u) | (uint32_t)parity_even(v) << 2;
}
static void flag_szp16(uint16_t v)
{
    uint32_t *f = flags_now();
    *f = (*f & ~0xc4u) | (v == 0 ? 0x40u : 0) | ((v >> 8) & 0x80u) |
         (uint32_t)parity_even((uint8_t)v) << 2;
}
static uint8_t adc8(uint8_t v1, uint8_t v2)
{
    const uint8_t c = CF();
    const uint8_t res = (uint8_t)(v1 + v2 + c);
    uint32_t dst = (uint32_t)v1 + v2 + c;
    flag_szp8((uint8_t)dst);
    SETF(11, ((dst ^ v1) & (dst ^ v2) & 0x80) != 0);
    SETF(0, (dst & 0xff00) != 0);
    SETF(4, ((v1 ^ v2 ^ dst) & 0x10) != 0);
    return res;
}
static uint16_t adc16(uint16_t v1, uint16_t v2)
{
    const uint16_t c = CF();
    const uint16_t res = (uint16_t)(v1 + v2 + c);
    uint32_t dst = (uint32_t)v1 + v2 + c;
    flag_szp16((uint16_t)dst);
    SETF(11, (((dst ^ v1) & (dst ^ v2)) & 0x8000) != 0);
    SETF(0, (dst & 0xffff0000u) != 0);
    SETF(4, ((v1 ^ v2 ^ dst) & 0x10) != 0);
    return res;
}
static uint8_t sbb8(uint8_t v1, uint8_t v2)
{
    const uint8_t c = CF();
    uint32_t dst = (uint32_t)v1 - v2 - c;
    flag_szp8((uint8_t)dst);
    SETF(0, ((dst >> 8) & 1) != 0);
    SETF(11, ((dst ^ v1) & (v1 ^ v2) & 0x80) != 0);
    SETF(4, ((v1 ^ v2 ^ dst ^ c) & 0x10) != 0);
    return (uint8_t)dst;
}
static uint16_t sbb16(uint16_t v1, uint16_t v2)
{
    const uint8_t c = CF();
    uint32_t dst = (uint32_t)v1 - v2 - c;
    flag_szp16((uint16_t)dst);
    SETF(0, ((dst >> 16) & 1) != 0);
    SETF(11, ((dst ^ v1) & (v1 ^ v2) & 0x8000) != 0);
    SETF(4, ((v1 ^ v2 ^ dst ^ c) & 0x10) != 0);
    return (uint16_t)dst;
}

/* ModR/M state of the current instruction */
static uint8_t mode, reg, rm;
static uint32_t ea, useseg;
static bool segoverride;

static uint8_t fetch8(void) { uint8_t v = rd8(R.seg_base[1] + R.ip); R.ip++; return v; }
static uint16_t fetch16(void) { uint16_t v = rd16(R.seg_base[1] + R.ip); R.ip += 2; return v; }

static void modregrm(void)
{
    uint8_t b = fetch8();
    mode = b >> 6;
    reg = (b >> 3) & 7;
    rm = b & 7;
    uint16_t disp = 0;
    switch (mode) {
    case 0:
        if (rm == 6) disp = fetch16();
        if ((rm == 2 || rm == 3) && !segoverride) useseg = R.seg_base[2];
        break;
    case 1:
        disp = (uint16_t)(int16_t)(int8_t)fetch8();
        if ((rm == 2 || rm == 3 || rm == 6) && !segoverride) useseg = R.seg_base[2];
        break;
    case 2:
        disp = fetch16();
        if ((rm == 2 || rm == 3 || rm == 6) && !segoverride) useseg = R.seg_base[2];
        break;
    default:
        return;
    }
    uint32_t off = disp;
    switch (rm) {
    case 0: off += getreg16(3) + getreg16(6); break;
    case 1: off += getreg16(3) + getreg16(7); break;
    case 2: off += getreg16(5) + getreg16(6); break;
    case 3: off += getreg16(5) + getreg16(7); break;
    case 4: off += getreg16(6); break;
    case 5: off += getreg16(7); break;
    case 6: if (mode) off += getreg16(5); break;
    default: off += getreg16(3); break;
    }
    ea = (off & 0xffff) + useseg;
}
static uint8_t readrm8(void) { return mode < 3 ? rd8(ea) : getreg8(rm); }
static uint16_t readrm16(void) { return mode < 3 ? rd16(ea) : getreg16(rm); }
static void writerm8(uint8_t v) { if (mode < 3) wr8(ea, v); else putreg8(rm, v); }
static void writerm16(uint16_t v) { if (mode < 3) wr16(ea, v); else putreg16(rm, v); }

/* ALU 0..7 = ADD OR ADC SBB AND SUB XOR CMP; returns whether to write. */
static bool alu8(int op, uint8_t a, uint8_t b, uint8_t *res)
{
    switch (op) {
    case 0: *res = a + b; add_flags(8, a, b); return true;
    case 1: *res = a | b; logic_flags(8, *res); return true;
    case 2: *res = adc8(a, b); return true;
    case 3: *res = sbb8(a, b); return true;
    case 4: *res = a & b; logic_flags(8, *res); return true;
    case 5: *res = a - b; sub_flags(8, a, b); return true;
    case 6: *res = a ^ b; logic_flags(8, *res); return true;
    default: sub_flags(8, a, b); return false;
    }
}
static bool alu16(int op, uint16_t a, uint16_t b, uint16_t *res)
{
    switch (op) {
    case 0: *res = a + b; add_flags(16, a, b); return true;
    case 1: *res = a | b; logic_flags(16, *res); return true;
    case 2: *res = adc16(a, b); return true;
    case 3: *res = sbb16(a, b); return true;
    case 4: *res = a & b; logic_flags(16, *res); return true;
    case 5: *res = a - b; sub_flags(16, a, b); return true;
    case 6: *res = a ^ b; logic_flags(16, *res); return true;
    default: sub_flags(16, a, b); return false;
    }
}

static void ref_push(uint16_t v)
{
    putreg16(4, getreg16(4) - 2);
    wr16(R.seg_base[2] + getreg16(4), v);
}
static uint16_t ref_pop(void)
{
    const uint16_t v = rd16(R.seg_base[2] + getreg16(4));
    putreg16(4, getreg16(4) + 2);
    return v;
}

static void ref_step(void)
{
    if (R.halted) return;
    useseg = R.seg_base[3];
    segoverride = false;
    uint8_t op = fetch8();
    for (;;) {
        if (op == 0x26) useseg = R.seg_base[0];
        else if (op == 0x2e) useseg = R.seg_base[1];
        else if (op == 0x36) useseg = R.seg_base[2];
        else if (op == 0x3e) useseg = R.seg_base[3];
        else break;
        segoverride = true;
        op = fetch8();
    }
    uint8_t r8;
    uint16_t r16;
    if (op < 0x40 && (op & 7) < 6) {
        const int alu = op >> 3;
        switch (op & 7) {
        case 0: modregrm(); if (alu8(alu, readrm8(), getreg8(reg), &r8)) writerm8(r8); break;
        case 1: modregrm(); if (alu16(alu, readrm16(), getreg16(reg), &r16)) writerm16(r16); break;
        case 2: modregrm(); if (alu8(alu, getreg8(reg), readrm8(), &r8)) putreg8(reg, r8); break;
        case 3: modregrm(); if (alu16(alu, getreg16(reg), readrm16(), &r16)) putreg16(reg, r16); break;
        case 4: { uint8_t i = fetch8(); if (alu8(alu, getreg8(0), i, &r8)) putreg8(0, r8); break; }
        default: { uint16_t i = fetch16(); if (alu16(alu, getreg16(0), i, &r16)) putreg16(0, r16); break; }
        }
        return;
    }
    if (op >= 0x40 && op <= 0x4f) {
        putreg16(op & 7, (uint16_t)inc_dec(getreg16(op & 7), op >= 0x48, 16));
        return;
    }
    if (op >= 0x70 && op <= 0x7f) {
        int8_t d = (int8_t)fetch8();
        if (condition(op)) R.ip = (uint16_t)(R.ip + d);
        return;
    }
    if (op >= 0x50 && op <= 0x57) { ref_push(getreg16(op & 7)); return; }
    if (op >= 0x58 && op <= 0x5f) { const uint16_t v = ref_pop(); putreg16(op & 7, v); return; }
    if (op == 0x0f) {
        const uint8_t op2 = fetch8();
        if (op2 >= 0x80 && op2 <= 0x8f) {
            const uint16_t d = fetch16();
            if (condition(op2)) R.ip = (uint16_t)(R.ip + d);
            return;
        }
        R.bad = R.halted = true;
        return;
    }
    switch (op) {
    case 0x80: modregrm(); { uint8_t i = fetch8(); if (alu8(reg, readrm8(), i, &r8)) writerm8(r8); } return;
    case 0x81: modregrm(); { uint16_t a = readrm16(); uint16_t i = fetch16(); if (alu16(reg, a, i, &r16)) writerm16(r16); } return;
    case 0x83: modregrm(); { uint16_t a = readrm16(); uint16_t i = (uint16_t)(int16_t)(int8_t)fetch8(); if (alu16(reg, a, i, &r16)) writerm16(r16); } return;
    case 0x84: modregrm(); logic_flags(8, getreg8(reg) & readrm8()); return;
    case 0x85: modregrm(); logic_flags(16, getreg16(reg) & readrm16()); return;
    case 0x86: modregrm(); { uint8_t a = readrm8(); writerm8(getreg8(reg)); putreg8(reg, a); } return;
    case 0x87: modregrm(); { uint16_t a = readrm16(); writerm16(getreg16(reg)); putreg16(reg, a); } return;
    case 0x88: modregrm(); writerm8(getreg8(reg)); return;
    case 0x89: modregrm(); writerm16(getreg16(reg)); return;
    case 0x8a: modregrm(); putreg8(reg, readrm8()); return;
    case 0x8b: modregrm(); putreg16(reg, readrm16()); return;
    case 0x8d: modregrm(); if (mode == 3) break; /* #UD; XRIP uses a stale EA */ putreg16(reg, (uint16_t)(ea - useseg)); return;
    case 0x90: return;
    case 0x68: ref_push(fetch16()); return;
    case 0x6a: ref_push((uint16_t)(int16_t)(int8_t)fetch8()); return;
    case 0xe8: { uint16_t d = fetch16(); ref_push(R.ip); R.ip = (uint16_t)(R.ip + d); return; }
    case 0xc3: R.ip = ref_pop(); return;
    case 0x9a: {   /* CALL far: CS changes */
        const uint16_t off = fetch16(), sel = fetch16();
        ref_push(R.cs); ref_push(R.ip);
        R.cs = sel; R.seg_base[1] = (uint32_t)sel << 4; R.ip = off;
        return;
    }
    case 0xcb: case 0xca: {
        const uint16_t n = op == 0xca ? fetch16() : 0;
        R.ip = ref_pop();
        R.cs = ref_pop(); R.seg_base[1] = (uint32_t)R.cs << 4;
        putreg16(4, getreg16(4) + n);
        return;
    }
    case 0xc2: { uint16_t n = fetch16(); R.ip = ref_pop(); putreg16(4, getreg16(4) + n); return; }
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint16_t t = getreg16(0); putreg16(0, getreg16(op & 7)); putreg16(op & 7, t); return;
    }
    case 0xa0: putreg8(0, rd8(useseg + fetch16())); return;
    case 0xa1: putreg16(0, rd16(useseg + fetch16())); return;
    case 0xa2: wr8(useseg + fetch16(), getreg8(0)); return;
    case 0xa3: wr16(useseg + fetch16(), getreg16(0)); return;
    case 0xa8: logic_flags(8, getreg8(0) & fetch8()); return;
    case 0xa9: logic_flags(16, getreg16(0) & fetch16()); return;
    case 0xc6: modregrm(); { uint8_t i = fetch8(); writerm8(i); } return;
    case 0xc7: modregrm(); { uint16_t i = fetch16(); writerm16(i); } return;
    case 0xe2: { int8_t d = (int8_t)fetch8(); putreg16(1, getreg16(1) - 1); if (getreg16(1)) R.ip = (uint16_t)(R.ip + d); return; }
    case 0xe3: { int8_t d = (int8_t)fetch8(); if (!getreg16(1)) R.ip = (uint16_t)(R.ip + d); return; }
    case 0xe9: { uint16_t d = fetch16(); R.ip = (uint16_t)(R.ip + d); return; }
    case 0xeb: { int8_t d = (int8_t)fetch8(); R.ip = (uint16_t)(R.ip + d); return; }
    case 0xf4: R.ip--; R.halted = true; return;
    case 0xf5: SETF(0, !CF()); return;
    case 0xf6:
        modregrm();
        if (reg == 0) { logic_flags(8, readrm8() & fetch8()); return; }
        if (reg == 2) { writerm8((uint8_t)~readrm8()); return; }
        if (reg == 3) { const uint8_t v = readrm8(); sub_flags(8, 0, v); writerm8((uint8_t)-v); return; }
        break;
    case 0xf7:
        modregrm();
        if (reg == 0) { uint16_t a = readrm16(); logic_flags(16, a & fetch16()); return; }
        if (reg == 2) { writerm16((uint16_t)~readrm16()); return; }
        if (reg == 3) { const uint16_t v = readrm16(); sub_flags(16, 0, v); writerm16((uint16_t)-v); return; }
        if (reg == 4) {   /* MUL: CF = OF = (DX != 0); the rest as after a logic op */
            const uint32_t r = (uint32_t)getreg16(0) * readrm16();
            putreg16(0, (uint16_t)r); putreg16(2, (uint16_t)(r >> 16));
            logic_flags(16, (uint16_t)r);
            SETF(0, r >> 16); SETF(11, r >> 16);
            return;
        }
        break;
    case 0x98: putreg16(0, (uint16_t)(int16_t)(int8_t)getreg8(0)); return;
    case 0x99: putreg16(2, (getreg16(0) & 0x8000) ? 0xffff : 0); return;
    case 0xfc: R.eflags &= ~0x400u; return;
    case 0xfd: R.eflags |= 0x400u; return;
    case 0xaa: case 0xab: case 0xac: case 0xad: case 0xa4: case 0xa5: {
        const int w = op & 1, step = (R.eflags & 0x400u ? -1 : 1) * (w ? 2 : 1);
        const uint32_t src = useseg + getreg16(6), dst = R.seg_base[0] + getreg16(7);
        if (op <= 0xa5) { if (w) wr16(dst, rd16(src)); else wr8(dst, rd8(src)); }
        else if (op <= 0xab) { if (w) wr16(dst, getreg16(0)); else wr8(dst, getreg8(0)); }
        else { if (w) putreg16(0, rd16(src)); else putreg8(0, rd8(src)); }
        if (op <= 0xa5 || op >= 0xac) putreg16(6, (uint16_t)(getreg16(6) + step));
        if (op <= 0xab) putreg16(7, (uint16_t)(getreg16(7) + step));
        return;
    }
    case 0xf8: SETF(0, 0); return;
    case 0xf9: SETF(0, 1); return;
    case 0xfe: modregrm(); if (reg > 1) break; writerm8((uint8_t)inc_dec(readrm8(), reg != 0, 8)); return;
    case 0xff: modregrm();
        if (reg == 6) { ref_push(readrm16()); return; }
        if (reg > 1) break;
        writerm16((uint16_t)inc_dec(readrm16(), reg != 0, 16)); return;
    case 0x8f: modregrm(); if (reg) break; { const uint16_t v = ref_pop(); writerm16(v); } return;
    default:
        if (op >= 0xb0 && op <= 0xb7) { putreg8(op & 7, fetch8()); return; }
        if (op >= 0xb8 && op <= 0xbf) { putreg16(op & 7, fetch16()); return; }
        break;
    }
    R.bad = R.halted = true;
}

/* The test's xlat_interp_one(): the reference interpreter is the "XRIP". */
uint32_t xlat_interp_one(xlat_rt_t *rt, uint32_t flags)
{
    const uint32_t may_write = flags & 1u;
    if (flags & 4u)
        side_calls++;
    R.ip = (uint16_t)rt->ip;
    R.flags_result = rt->lazy_result; R.flags_dst = rt->lazy_dst; R.flags_src = rt->lazy_src;
    R.flags_op = rt->lazy_op; R.flags_cf = rt->lazy_cf; R.eflags = rt->eflags;
    ref_step();
    rt->ip = R.ip;
    rt->lazy_result = R.flags_result; rt->lazy_dst = R.flags_dst; rt->lazy_src = R.flags_src;
    rt->lazy_op = R.flags_op; rt->lazy_cf = R.flags_cf; rt->eflags = R.eflags;
    interp_calls++;
    if (R.halted)
        return 0;
    if (R.cs != rt->cs) {
        rt->cs = R.cs;
        rt->seg_base[1] = R.seg_base[1];
        return 2;
    }
    if (may_write && !xlat_region_still_valid(rt))
        return 0;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Random program generator                                               */
/* ---------------------------------------------------------------------- */

static uint32_t rng;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }

typedef struct { uint8_t b[12]; uint8_t len; int8_t fwd; uint8_t rel16; int8_t call; } ginsn_t;
/* fwd: branch target (body index) patched as rel8 / rel16 in the last bytes;
 * call: subroutine number, rel16 patched after the subroutines are laid out */

#define MAX_SUBS 4
static int nsubs;
static uint16_t gen_cs;   /* CS the program runs in: far calls use the alias CS-1 */
static struct { uint32_t pos; int sub; } call_fix[64];
static int ncall_fix;

static ginsn_t body[64];
static int nbody;
static bool allow_mem;

static int pick_reg16_dst(void)
{
    static const int r[] = {0, 2, 3, 5, 6, 7};
    return rn(40) == 0 ? 4 : r[rn(6)];          /* rarely SP: not translated */
}
static int pick_reg8_dst(void)
{
    static const int r[] = {0, 2, 3, 4, 6, 7};  /* not CL(1), CH(5) */
    return r[rn(6)];
}

/* ModR/M for an E operand; dst: E is written (so no CX in register form). */
static void put_modrm(ginsn_t *g, int regf, bool word, bool dst)
{
    if (!allow_mem || rn(5) < 3) {
        int e = dst ? (word ? pick_reg16_dst() : pick_reg8_dst()) : (int)rn(8);
        g->b[g->len++] = (uint8_t)(0xc0 | regf << 3 | e);
        return;
    }
    int m = rn(3), r = rn(8);
    g->b[g->len++] = (uint8_t)(m << 6 | regf << 3 | r);
    if (m == 0 && r == 6) {
        uint16_t d = (uint16_t)rnd();
        g->b[g->len++] = (uint8_t)d;
        g->b[g->len++] = (uint8_t)(d >> 8);
    } else if (m == 1) {
        g->b[g->len++] = (uint8_t)rnd();
    } else if (m == 2) {
        uint16_t d = (uint16_t)rnd();
        g->b[g->len++] = (uint8_t)d;
        g->b[g->len++] = (uint8_t)(d >> 8);
    }
}

static void gen_one(ginsn_t *g, int index, int total)
{
    memset(g, 0, sizeof(*g));
    g->fwd = -1;
    g->call = -1;
    if (total > 0 && rn(14) == 0) {    /* stack: PUSH/POP, CALL to a subroutine */
        const int t = rn(5);
        if (t == 0 && nsubs) {
            g->call = (int8_t)rn((uint32_t)nsubs);
            if (g->call & 1) {      /* odd subroutines are far: CALL CS-1:off+16 */
                g->b[g->len++] = 0x9a; g->b[g->len++] = 0; g->b[g->len++] = 0;
                g->b[g->len++] = (uint8_t)(gen_cs - 1); g->b[g->len++] = (uint8_t)((gen_cs - 1) >> 8);
            } else {
                g->b[g->len++] = 0xe8; g->b[g->len++] = 0; g->b[g->len++] = 0;
            }
        } else if (t == 1) {
            g->b[g->len++] = rn(2) ? 0x68 : 0x6a;
            g->b[g->len++] = (uint8_t)rnd();
            if (g->b[0] == 0x68) g->b[g->len++] = (uint8_t)rnd();
        } else if (t == 2) {
            g->b[g->len++] = (uint8_t)(0x58 | pick_reg16_dst());
        } else if (t == 3 && allow_mem) {   /* PUSH / POP r/m16 */
            const bool pop = rn(2);
            g->b[g->len++] = pop ? 0x8f : 0xff;
            put_modrm(g, pop ? 0 : 6, true, true);
        } else {
            g->b[g->len++] = (uint8_t)(0x50 | rn(8));
        }
        return;
    }
    if (rn(12) == 0) {
        static const uint8_t seg[] = {0x26, 0x2e, 0x36, 0x3e};
        g->b[g->len++] = seg[rn(4)];
    }
    const int kind = rn(20);
    const bool word = rn(2);
    int alu = rn(8);
    if (alu == 2 || alu == 3) { if (rn(3)) alu = rn(2) ? 0 : 5; }   /* ADC/SBB rarer */
    switch (kind) {
    case 0: case 1: case 2: {      /* ALU E,G / G,E */
        const bool to_g = rn(2);
        g->b[g->len++] = (uint8_t)(alu << 3 | (to_g ? 2 : 0) | word);
        const int gr = to_g && alu != 7 ? (word ? pick_reg16_dst() : pick_reg8_dst()) : (int)rn(8);
        put_modrm(g, gr, word, !to_g && alu != 7);
        break;
    }
    case 3: {                      /* ALU acc, imm */
        g->b[g->len++] = (uint8_t)(alu << 3 | 4 | word);
        g->b[g->len++] = (uint8_t)rnd();
        if (word) g->b[g->len++] = (uint8_t)rnd();
        break;
    }
    case 4: case 5: {              /* 80/81/83 */
        const uint8_t opc = word ? (rn(2) ? 0x81 : 0x83) : 0x80;
        g->b[g->len++] = opc;
        put_modrm(g, alu, word, alu != 7);
        g->b[g->len++] = (uint8_t)(rn(4) ? rn(8) : rnd());
        if (opc == 0x81) g->b[g->len++] = (uint8_t)(rn(3) ? 0 : rnd());
        break;
    }
    case 6: {                      /* TEST */
        const int t = rn(3);
        if (t == 0) { g->b[g->len++] = (uint8_t)(0x84 | word); put_modrm(g, rn(8), word, false); }
        else if (t == 1) { g->b[g->len++] = (uint8_t)(0xa8 | word); g->b[g->len++] = (uint8_t)rnd(); if (word) g->b[g->len++] = (uint8_t)rnd(); }
        else { g->b[g->len++] = (uint8_t)(0xf6 | word); put_modrm(g, 0, word, false); g->b[g->len++] = (uint8_t)rnd(); if (word) g->b[g->len++] = (uint8_t)rnd(); }
        break;
    }
    case 7: case 8: {              /* INC/DEC */
        if (rn(2)) {
            int r = pick_reg16_dst();
            g->b[g->len++] = (uint8_t)((rn(2) ? 0x48 : 0x40) | r);
        } else {
            g->b[g->len++] = (uint8_t)(0xfe | word);
            put_modrm(g, rn(2), word, true);
        }
        break;
    }
    case 9: case 10: case 11: {    /* MOV */
        const int t = rn(5);
        if (t == 0) { int r = word ? pick_reg16_dst() : pick_reg8_dst(); g->b[g->len++] = (uint8_t)(0xb0 | word << 3 | r); g->b[g->len++] = (uint8_t)rnd(); if (word) g->b[g->len++] = (uint8_t)rnd(); }
        else if (t == 1 && allow_mem) { g->b[g->len++] = (uint8_t)(0xa0 | (rn(2) << 1) | word); uint16_t d = (uint16_t)rnd(); g->b[g->len++] = (uint8_t)d; g->b[g->len++] = (uint8_t)(d >> 8); }
        else if (t == 2) { g->b[g->len++] = (uint8_t)(0xc6 | word); put_modrm(g, 0, word, true); g->b[g->len++] = (uint8_t)rnd(); if (word) g->b[g->len++] = (uint8_t)rnd(); }
        else {
            const bool to_g = rn(2);
            g->b[g->len++] = (uint8_t)(0x88 | (to_g ? 2 : 0) | word);
            const int gr = to_g ? (word ? pick_reg16_dst() : pick_reg8_dst()) : (int)rn(8);
            put_modrm(g, gr, word, !to_g);
        }
        break;
    }
    case 12: {                     /* LEA */
        bool save = allow_mem;
        allow_mem = true;
        g->b[g->len++] = 0x8d;
        int m = rn(3), r = rn(8);
        g->b[g->len++] = (uint8_t)(m << 6 | pick_reg16_dst() << 3 | r);
        if ((m == 0 && r == 6) || m == 2) { g->b[g->len++] = (uint8_t)rnd(); g->b[g->len++] = (uint8_t)rnd(); }
        else if (m == 1) g->b[g->len++] = (uint8_t)rnd();
        allow_mem = save;
        break;
    }
    case 13: {                     /* XCHG reg, reg / NOP */
        const int t = rn(3);
        if (t == 0) { g->b[g->len++] = 0x90; }
        else if (t == 1) { int r = pick_reg16_dst(); g->b[g->len++] = (uint8_t)(r ? 0x90 | r : 0x90); }
        else {
            g->b[g->len++] = (uint8_t)(0x86 | word);
            int a = word ? pick_reg16_dst() : pick_reg8_dst();
            int b = word ? pick_reg16_dst() : pick_reg8_dst();
            g->b[g->len++] = (uint8_t)(0xc0 | a << 3 | b);
        }
        break;
    }
    case 14: {                     /* instructions the translator leaves to the interpreter */
        const int t = rn(9);
        if (t < 3) {
            static const uint8_t f[] = {0xf8, 0xf9, 0xf5};
            g->b[g->len++] = f[t];
        } else if (t == 3) {
            g->b[g->len++] = rn(2) ? 0x98 : 0x99;
        } else if (t == 4) {
            g->b[g->len++] = rn(2) ? 0xfc : 0xfd;
        } else if (t == 5 && allow_mem) {
            static const uint8_t str[] = {0xaa, 0xab, 0xac, 0xad, 0xa4, 0xa5};
            g->b[g->len++] = str[rn(6)];
        } else if (t == 6) {     /* NOT / NEG / MUL */
            const int sub = 2 + (int)rn(3);
            g->b[g->len++] = (uint8_t)(sub == 4 ? 0xf7 : 0xf6 | word);
            put_modrm(g, sub, sub == 4 || word, true);
        } else if (t == 7) {     /* SP arithmetic: add/sub sp, imm8 */
            g->b[g->len++] = 0x83;
            g->b[g->len++] = rn(2) ? 0xc4 : 0xec;
            g->b[g->len++] = (uint8_t)(rn(4) * 2);
        } else {
            g->b[g->len++] = 0xf8;
        }
        break;
    }
    default: {                     /* forward branch */
        if (index + 1 >= total) { g->b[g->len++] = 0x90; break; }
        const int t = rn(7);
        if (t == 6) {   /* Jcc rel16 */
            g->b[g->len++] = 0x0f;
            g->b[g->len++] = (uint8_t)(0x80 | rn(16));
            g->b[g->len++] = 0;
            g->rel16 = 1;
        } else
        g->b[g->len++] = t < 4 ? (uint8_t)(0x70 | rn(16)) : t == 4 ? 0xeb : 0xe3;
        if (t < 4 && rn(2)) {   /* compare then branch */
            ginsn_t c;
            memset(&c, 0, sizeof(c));
            const int w = rn(2);
            if (rn(2)) { c.b[c.len++] = (uint8_t)(0x38 | w); c.b[c.len++] = (uint8_t)(0xc0 | rn(8) << 3 | rn(8)); }
            else { c.b[c.len++] = (uint8_t)(0x3c | w); c.b[c.len++] = (uint8_t)rn(4); if (w) c.b[c.len++] = 0; }
            memmove(g->b + c.len, g->b, g->len);
            memcpy(g->b, c.b, c.len);
            g->len += c.len;
        }
        if (!g->rel16) g->b[g->len++] = 0;
        g->fwd = (int8_t)(index + 1 + rn((uint32_t)(total - index)));   /* may be total: past body */
        break;
    }
    }
}

static uint32_t layout(uint8_t *code, uint16_t at_ip)
{
    uint32_t pos[65];
    uint32_t p = 0;
    for (int i = 0; i < nbody; ++i) {
        pos[i] = p;
        p += body[i].len;
    }
    pos[nbody] = p;
    p = 0;
    for (int i = 0; i < nbody; ++i) {
        memcpy(code + p, body[i].b, body[i].len);
        if (body[i].fwd >= 0) {
            int32_t rel = (int32_t)pos[body[i].fwd] - (int32_t)(pos[i] + body[i].len);
            if (body[i].rel16) {
                code[p + body[i].len - 2] = (uint8_t)rel;
                code[p + body[i].len - 1] = (uint8_t)(rel >> 8);
            } else {
                if (rel > 127) rel = 0;
                code[p + body[i].len - 1] = (uint8_t)rel;
            }
        }
        if (body[i].call >= 0 && ncall_fix < 64) {
            call_fix[ncall_fix].pos = at_ip + p + body[i].len - ((body[i].call & 1) ? 4 : 2);
            call_fix[ncall_fix].sub = body[i].call;
            ncall_fix++;
        }
        p += body[i].len;
    }
    return p;
}

/* program: prologue, loop { body } (dec cx/jnz | loop | sub cx,1/jnz), maybe a
 * second loop, hlt */
static uint32_t gen_program(uint8_t *code)
{
    uint32_t p = 0;
    nsubs = rn(3) ? 1 + (int)rn(MAX_SUBS) : 0;
    ncall_fix = 0;
    int nprologue = rn(4);
    for (int k = 0; k < nprologue; ++k) {
        ginsn_t g;
        gen_one(&g, 0, 0);
        if (g.fwd >= 0) continue;
        memcpy(code + p, g.b, g.len);
        p += g.len;
    }
    const int loops = 1 + (rn(3) == 0);
    for (int l = 0; l < loops; ++l) {
        if (l) {   /* reload CX for the second loop */
            code[p++] = 0xb9;
            code[p++] = (uint8_t)(1 + rn(60));
            code[p++] = 0;
        }
        const uint32_t head = p;
        nbody = 1 + rn(14);
        for (int i = 0; i < nbody; ++i)
            gen_one(&body[i], i, nbody);
        p += layout(code + p, (uint16_t)p);
        const int ctl = rn(3);
        int32_t rel;
        if (ctl == 0) { code[p++] = 0x49; code[p++] = 0x75; rel = (int32_t)head - (int32_t)(p + 1); }
        else if (ctl == 1) { code[p++] = 0xe2; rel = (int32_t)head - (int32_t)(p + 1); }
        else { code[p++] = 0x83; code[p++] = 0xe9; code[p++] = 0x01; code[p++] = 0x75; rel = (int32_t)head - (int32_t)(p + 1); }
        if (rel < -128) return 0;
        code[p++] = (uint8_t)rel;
    }
    code[p++] = 0xf4;
    /* subroutines: straight-line code and RET (sometimes RET n) */
    uint32_t sub_at[MAX_SUBS];
    for (int k = 0; k < nsubs; ++k) {
        sub_at[k] = p;
        const int n = (int)rn(6);
        for (int j = 0; j < n; ++j) {
            ginsn_t g;
            gen_one(&g, 0, 0);
            if (g.fwd >= 0 || g.call >= 0)
                continue;
            if (g.len == 1 && (g.b[0] & 0xf0) == 0x50)
                continue;   /* no PUSH/POP inside: keep the return address */
            memcpy(code + p, g.b, g.len);
            p += g.len;
        }
        if (k & 1) {
            code[p++] = rn(2) ? 0xcb : 0xca;
            if (code[p - 1] == 0xca) { code[p++] = (uint8_t)(rn(3) * 2); code[p++] = 0; }
        } else if (rn(4)) {
            code[p++] = 0xc3;
        } else {
            code[p++] = 0xc2; code[p++] = (uint8_t)(rn(3) * 2); code[p++] = 0;
        }
    }
    for (int k = 0; k < ncall_fix; ++k) {
        if (call_fix[k].sub & 1) {   /* far: offset in the CS-1 alias */
            const uint32_t off = sub_at[call_fix[k].sub] + 16;
            code[call_fix[k].pos] = (uint8_t)off;
            code[call_fix[k].pos + 1] = (uint8_t)(off >> 8);
            continue;
        }
        const int32_t rel = (int32_t)sub_at[call_fix[k].sub] - (int32_t)(call_fix[k].pos + 2);
        code[call_fix[k].pos] = (uint8_t)rel;
        code[call_fix[k].pos + 1] = (uint8_t)(rel >> 8);
    }
    return p;
}

/* ---------------------------------------------------------------------- */
/* Driver                                                                 */
/* ---------------------------------------------------------------------- */

static uint8_t mem_init[MEM_BYTES];
static uint8_t zero_byte, one_byte = 1;
static bool lockstep, smc, reuse_cs;
static uint16_t last_cs = 0x1000;

static void setup_state(ref_t *s, uint32_t seed_regs)
{
    (void)seed_regs;
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 8; ++i) s->gpr[i] = rnd();
    s->gpr[1] = (s->gpr[1] & 0xffff0000u) | (1 + (rn(3) ? rn(25) : rn(400)));
    if (rn(3) == 0) {   /* keep addresses near the data */
        s->gpr[3] &= 0xffff00ffu;
        s->gpr[6] &= 0xffff00ffu;
        s->gpr[7] &= 0xffff00ffu;
        s->gpr[5] &= 0xffff00ffu;
    }
    if (rn(3) == 0) {   /* small values: equal compares, zero results */
        for (int i = 0; i < 8; ++i) if (i != 1 && rn(2)) s->gpr[i] = rn(4);
    }
    if (rn(4) == 0) {   /* clean upper halves, offsets that wrap at 64 KiB */
        for (int i = 0; i < 8; ++i) if (i != 1) s->gpr[i] = rn(2) ? 0xff00u + rn(0x100) : rn(0x10000);
    }
    s->cs = reuse_cs ? last_cs : 0x1000 + (uint16_t)rn(0x100) * 0x10;
    last_cs = s->cs;
    s->seg_base[1] = (uint32_t)s->cs << 4;
    for (int k = 0; k < 4; ++k) {
        if (k == 1) continue;
        uint32_t sel = rn(8) == 0 ? s->cs : rn(5) ? 0x2000 + rn(0x7000) : 0x2000 + rn(0xd000);
        s->seg_base[k] = sel << 4;
    }
    if (smc) {          /* data writes land on the code */
        s->seg_base[3] = s->seg_base[0] = s->seg_base[2] = s->seg_base[1];
        s->gpr[3] = rn(0x30); s->gpr[6] = rn(0x10); s->gpr[7] = rn(0x10); s->gpr[5] = rn(0x20);
    }
    s->eflags = 2 | (rnd() & 0x8d5u);
    s->ip = 0;
}

/* ---------------------------------------------------------------------- */
/* Replay of regions logged by the firmware (XLAT_LOG xlat.log)           */
/* ---------------------------------------------------------------------- */

#define REPLAY_SPAN 0x50000u   /* code and data used by replay stay below this */

typedef struct {
    uint16_t cs, ip;
    int nlines;
    struct { uint16_t ip; uint8_t n; uint8_t b[16]; } line[64];
} replay_region_t;

static int replay_parse(const char *path, replay_region_t *out, int max)
{
    FILE *f = fopen(path, "r");
    char buf[256];
    int n = -1;
    if (!f) { perror(path); return 0; }
    while (fgets(buf, sizeof(buf), f)) {
        unsigned cs, ip;
        if (buf[0] == 'T' && sscanf(buf, "T %x:%x", &cs, &ip) == 2) {
            if (n + 1 >= max) break;
            ++n;
            out[n].cs = (uint16_t)cs; out[n].ip = (uint16_t)ip; out[n].nlines = 0;
        } else if (n >= 0 && buf[0] == ' ' && buf[1] == ' ' &&
                   sscanf(buf + 2, "%x:%x", &cs, &ip) == 2 && out[n].nlines < 64) {
            char *p = strchr(buf + 2, ':') + 5;
            int k = 0;
            unsigned v;
            int used;
            while (k < 16 && sscanf(p, " %2x%n", &v, &used) == 1) { out[n].line[out[n].nlines].b[k++] = (uint8_t)v; p += used; }
            out[n].line[out[n].nlines].ip = (uint16_t)ip;
            out[n].line[out[n].nlines].n = (uint8_t)k;
            out[n].nlines++;
        }
    }
    fclose(f);
    return n + 1;
}

static volatile uint32_t test_clock = 12345u;   /* a stopped microsecond clock */

static int replay(const char *path, int states)
{
    static replay_region_t reg[256];
    static uint8_t base_mem[REPLAY_SPAN], before_mem[REPLAY_SPAN], after_mem[REPLAY_SPAN];
    const int nreg = replay_parse(path, reg, 256);
    int bad = 0, untranslated = 0, checked = 0;
    for (uint32_t k = 0; k < REPLAY_SPAN; ++k) base_mem[k] = (uint8_t)rnd();
    for (int r = 0; r < nreg; ++r) {
        int ran_states = 0, fails = 0;
        for (int t = 0; t < states && fails < 3; ++t) {
            /* memory: random data, the region's segment filled with F1 and its code */
            memcpy(mem, base_mem, REPLAY_SPAN);
            const uint32_t cs_base = (uint32_t)reg[r].cs << 4;
            memset(mem + cs_base, 0xf1, 0x10000);   /* F1: decoded by neither decoder */
            for (int l = 0; l < reg[r].nlines; ++l)
                memcpy(mem + cs_base + reg[r].line[l].ip, reg[r].line[l].b, reg[r].line[l].n);
            for (int k = 0; k < 64; ++k) mem[rn(REPLAY_SPAN)] = (uint8_t)rn(4);   /* small values */
            memset(&R, 0, sizeof(R));
            for (int i = 0; i < 8; ++i) R.gpr[i] = rn(3) ? rnd() : (rnd() & 0xffff0000u) | rn(8);
            R.gpr[1] = (R.gpr[1] & 0xffff0000u) | (rn(2) ? rn(40) : (uint16_t)rnd());
            R.cs = reg[r].cs;
            R.seg_base[1] = cs_base;
            for (int k = 0; k < 4; ++k)
                if (k != 1) R.seg_base[k] = rn(4) == 0 ? cs_base : rn(0x3000) << 4;
            R.ip = reg[r].ip;
            R.eflags = 2 | (rnd() & 0x8d5u);
            if (rn(2)) { uint16_t r16; alu16((int)(rn(2) ? 0 : 5 + rn(3)), (uint16_t)rnd(), (uint16_t)rnd(), &r16); }
            const ref_t before = R;
            memcpy(before_mem, mem, REPLAY_SPAN);
            xlat_flush();
            bool ran = false;
            xlat_rt_t rt;
            for (int attempt = 0; attempt < 4 && !ran; ++attempt) {
                rt.mem = mem; rt.mem_size = 0xa0000u;
                memcpy(rt.seg_base, R.seg_base, sizeof(rt.seg_base));
                rt.budget = 1 + (int32_t)rn(300);
                rt.clock = &test_clock; rt.extra = 0;
                rt.deadline = test_clock;   /* expired: logged loops may never end */
                rt.intr = &zero_byte; rt.gpr = R.gpr;
                rt.lazy_result = R.flags_result; rt.lazy_dst = R.flags_dst; rt.lazy_src = R.flags_src;
                rt.eflags = R.eflags; rt.lazy_op = R.flags_op; rt.lazy_cf = R.flags_cf;
                rt.cs = R.cs; rt.ip = R.ip;
                ran = xlat_run(&rt);
            }
            if (!ran) continue;
            ran_states++;
            R.ip = (uint16_t)rt.ip;
            R.flags_result = rt.lazy_result; R.flags_dst = rt.lazy_dst; R.flags_src = rt.lazy_src;
            R.flags_op = rt.lazy_op; R.flags_cf = rt.lazy_cf; R.eflags = rt.eflags;
            ref_t after = R;
            memcpy(after_mem, mem, REPLAY_SPAN);
            /* the reference from 'before' must reach the same registers, flags and memory */
            memcpy(mem, before_mem, REPLAY_SPAN);
            R = before;
            bool found = false;
            for (int k = 0; k < 40000 && !R.halted; ++k) {
                if (k) ref_step();
                if (R.ip == after.ip && !memcmp(R.gpr, after.gpr, sizeof(R.gpr))) {
                    ref_t a = after, b = R;
                    R = a; flags_now(); a = R;
                    R = b; flags_now(); b = R;
                    if ((a.eflags & 0xcd5u) == (b.eflags & 0xcd5u) && !memcmp(mem, after_mem, REPLAY_SPAN)) {
                        found = true;
                        break;
                    }
                }
            }
            checked++;
            if (!found) {
                fails++;
                bad++;
                printf("MISMATCH region %04X:%04X state %d: entry %04X exit %04X kind %u\n",
                       reg[r].cs, reg[r].ip, t, before.ip, after.ip, rt.exit_kind);
            }
        }
        if (!ran_states) {
            untranslated++;
            printf("not translated here: %04X:%04X\n", reg[r].cs, reg[r].ip);
        }
    }
    printf("replay: regions %d, states checked %d, mismatches %d, untranslated %d\n",
           nreg, checked, bad, untranslated);
    printf("stats: translations %u entries %u exits b/budget/side %u/%u/%u native insns %u\n",
           xlat_stats.translations, xlat_stats.entries, xlat_stats.exits[0], xlat_stats.exits[1],
           xlat_stats.exits[2], xlat_stats.guest_insns);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--replay")) {
        rng = 0x12345u;
        return replay(argv[2], argc > 3 ? atoi(argv[3]) : 300);
    }
    const int programs = argc > 1 ? atoi(argv[1]) : 2000;
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1;
    int run = 0, skipped = 0, failures = 0;
    lockstep = getenv("XLAT_LOCKSTEP") != NULL;
    static uint8_t code[4096];

    for (int prog = 0; prog < programs; ++prog, ++seed) {
        rng = seed * 2654435761u | 1;
        for (int k = 0; k < 8; ++k) rnd();
        allow_mem = rn(4) != 0;
        smc = allow_mem && (rn(10) == 0 || getenv("XLAT_SMC"));
        reuse_cs = rn(4) == 0;
        ref_t init;
        setup_state(&init, seed);
        gen_cs = init.cs;
        const uint32_t len = gen_program(code);
        if (!len) { skipped++; continue; }
        /* random memory contents near likely data, code at CS:0 */
        for (uint32_t k = 0; k < 64; ++k) {
            const uint32_t base = rnd() % (MEM_BYTES - 256);
            for (uint32_t j = 0; j < 256; ++j) mem_init[base + j] = (uint8_t)rnd();
        }
        memcpy(mem_init + init.seg_base[1], code, len);
        if (rn(2)) {   /* start with a lazy state */
            R = init;
            uint16_t r16;
            alu16(rn(8) == 2 ? 0 : (int)rn(8), (uint16_t)rnd(), (uint16_t)rnd(), &r16);
            init = R;
        }

        /* reference */
        memcpy(mem, mem_init, MEM_BYTES);
        R = init;
        long steps = 0;
        while (!R.halted && steps < 200000) { ref_step(); steps++; }
        if (!R.halted || R.bad) {
            skipped++;
            memcpy(mem_init + init.seg_base[1], "\0\0\0\0", 4);
            continue;
        }
        flags_now();
        ref_t want = R;
        static uint8_t want_mem[MEM_BYTES];
        memcpy(want_mem, mem, MEM_BYTES);

        /* translated */
        memcpy(mem, mem_init, MEM_BYTES);
        R = init;
        if (!reuse_cs && rn(8)) xlat_flush();
        const uint32_t mem_size = rn(3) ? 0xa0000u : 0x20000u + rn(0x80000);
        long guard = 0;
        while (!R.halted && guard++ < 400000) {
            xlat_rt_t rt;
            rt.mem = mem;
            rt.mem_size = mem_size;
            memcpy(rt.seg_base, R.seg_base, sizeof(rt.seg_base));
            rt.budget = 1 + (int32_t)rn(300);
            rt.clock = &test_clock; rt.extra = 0;
            rt.deadline = test_clock + (rn(2) ? 0u : 1000u);   /* expired / never reached */
            rt.intr = rn(20) ? &zero_byte : &one_byte;
            rt.gpr = R.gpr;
            rt.lazy_result = R.flags_result;
            rt.lazy_dst = R.flags_dst;
            rt.lazy_src = R.flags_src;
            rt.eflags = R.eflags;
            rt.lazy_op = R.flags_op;
            rt.lazy_cf = R.flags_cf;
            rt.cs = R.cs;
            rt.ip = R.ip;
            rt.exit_kind = 0xff;
            ref_t before = R;
            static uint8_t before_mem[MEM_BYTES];
            if (lockstep) memcpy(before_mem, mem, MEM_BYTES);
            if (getenv("XLAT_TRACE2")) printf("  run? ip %04x sp %04x cx %04x\n", R.ip, getreg16(4), getreg16(1));
            if (xlat_run(&rt)) {
                R.cs = rt.cs; R.seg_base[1] = rt.seg_base[1];
                if (getenv("XLAT_TRACE2")) printf("  ran -> ip %04x kind %u sp %04x cx %04x\n", rt.ip, rt.exit_kind, getreg16(4), getreg16(1));
                R.ip = (uint16_t)rt.ip;
                R.flags_result = rt.lazy_result;
                R.flags_dst = rt.lazy_dst;
                R.flags_src = rt.lazy_src;
                R.flags_op = rt.lazy_op;
                R.flags_cf = rt.lazy_cf;
                R.eflags = rt.eflags;
                if (lockstep) {
                    /* the reference must reach the same state from 'before' */
                    ref_t after = R;
                    static uint8_t after_mem[MEM_BYTES];
                    memcpy(after_mem, mem, MEM_BYTES);
                    memcpy(mem, before_mem, MEM_BYTES);
                    R = before;
                    bool found = false;
                    /* a run past its budget (deadline not reached) did up to extra more */
                    const long lim = 20000 + 2L * (long)rt.extra;
                    for (long k = 0; k < lim && !R.halted; ++k) {
                        if (getenv("XLAT_TRACE") && k < 6) printf("   ref ip %04x op %02x efl %03x res %08x\n", R.ip, R.flags_op, R.eflags, R.flags_result);
                        if (k) ref_step();
                        if (R.ip == after.ip && R.cs == after.cs && !memcmp(R.gpr, after.gpr, sizeof(R.gpr))) {
                            ref_t a = after, b = R;
                            R = a; flags_now(); a = R;
                            R = b; flags_now(); b = R;
                            if ((a.eflags & 0xcd5u) == (b.eflags & 0xcd5u)) { found = true; break; }
                            printf("  lockstep: ip %04x regs match, flags want %03x got %03x (k=%ld)\n",
                                   after.ip, b.eflags & 0x8d5u, a.eflags & 0x8d5u, k);
                            printf("  got lazy op %02x res %08x dst %08x src %08x cf %d; want op %02x res %08x dst %08x src %08x cf %d\n",
                                   after.flags_op, after.flags_result, after.flags_dst, after.flags_src, after.flags_cf,
                                   R.flags_op, R.flags_result, R.flags_dst, R.flags_src, R.flags_cf);
                            R = b;
                        }
                    }
                    if (!found && getenv("XLAT_TRACE3")) {
                        memcpy(mem, before_mem, MEM_BYTES);
                        R = before;
                        for (int k = 0; k < 300 && !R.halted; ++k) {
                            printf("   r %04x: %02x %02x %02x %02x  ax %04x cx %04x dx %04x bx %04x sp %04x bp %04x si %04x di %04x\n",
                                   R.ip, mem[R.seg_base[1] + R.ip], mem[R.seg_base[1] + R.ip + 1], mem[R.seg_base[1] + R.ip + 2], mem[R.seg_base[1] + R.ip + 3],
                                   getreg16(0), getreg16(1), getreg16(2), getreg16(3), getreg16(4), getreg16(5), getreg16(6), getreg16(7));
                            ref_step();
                        }
                        printf("   jit final: ax %04x cx %04x dx %04x bx %04x sp %04x bp %04x si %04x di %04x\n",
                               (uint16_t)after.gpr[0], (uint16_t)after.gpr[1], (uint16_t)after.gpr[2], (uint16_t)after.gpr[3], (uint16_t)after.gpr[4], (uint16_t)after.gpr[5], (uint16_t)after.gpr[6], (uint16_t)after.gpr[7]);
                    }
                    if (!found) {
                        printf("seed %u: lockstep divergence entering at %04x, exit %04x kind %u; before op %02x res %08x dst %08x src %08x cf %d efl %03x\n",
                               seed, before.ip, after.ip, rt.exit_kind, before.flags_op, before.flags_result, before.flags_dst, before.flags_src, before.flags_cf, before.eflags);
                        printf("  code at entry:");
                        for (int k = 0; k < 48; ++k) printf(" %02x", before_mem[before.seg_base[1] + ((before.ip + k) & 0xffff)]);
                        printf("\n  regs before:");
                        for (int k = 0; k < 8; ++k) printf(" %08x", before.gpr[k]);
                        printf("\n  segs ES CS SS DS: %05x %05x %05x %05x\n", before.seg_base[0], before.seg_base[1], before.seg_base[2], before.seg_base[3]);
                        failures++;
                        memcpy(mem, after_mem, MEM_BYTES);
                        R = after;
                        break;
                    }
                    memcpy(mem, after_mem, MEM_BYTES);
                    R = after;
                }
            }
            const int n = (int)rn(40);
            for (int k = 0; k < n && !R.halted; ++k) { if (getenv("XLAT_TRACE2")) printf("  ref %04x sp %04x\n", R.ip, getreg16(4)); ref_step(); }
        }
        flags_now();
        const bool regs_ok = !memcmp(want.gpr, R.gpr, sizeof(R.gpr));
        const bool flags_ok = (want.eflags & 0xcd5u) == (R.eflags & 0xcd5u);
        const bool ip_ok = want.ip == R.ip && want.cs == R.cs && R.halted;
        const bool mem_ok = !memcmp(want_mem, mem, MEM_BYTES);
        if (!(regs_ok && flags_ok && ip_ok && mem_ok) || R.bad) {
            failures++;
            printf("FAIL seed %u: regs %d flags %d (want %03x got %03x) ip %d (%04x/%04x) mem %d\n",
                   seed, regs_ok, flags_ok, want.eflags & 0x8d5u, R.eflags & 0x8d5u, ip_ok,
                   want.ip, R.ip, mem_ok);
            for (int i = 0; i < 8; ++i)
                if (want.gpr[i] != R.gpr[i])
                    printf("  gpr%d want %08x got %08x\n", i, want.gpr[i], R.gpr[i]);
            printf("  code:");
            for (uint32_t k = 0; k < len; ++k) printf(" %02x", code[k]);
            printf("\n");
            if (failures > 10) break;
        }
        run++;
        memset(mem_init + init.seg_base[1], 0, len);
    }
    printf("programs %d, skipped %d, failures %d\n", run, skipped, failures);
    printf("stats: translations %u rejects %u entries %u invalid %u flushes %u exits b/budget/side/stop %u/%u/%u/%u native insns %u interp %lu side %lu\n",
           xlat_stats.translations, xlat_stats.rejects, xlat_stats.entries, xlat_stats.invalidations,
           xlat_stats.flushes, xlat_stats.exits[0], xlat_stats.exits[1], xlat_stats.exits[2],
           xlat_stats.exits[3], xlat_stats.guest_insns, interp_calls, side_calls);
    return failures ? 1 : 0;
}
