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
#include <limits.h>
#include <sys/mman.h>

#include "xlat.h"
#include "fpu_glue.h"
#include "xlat_internal.h"   /* XLAT_REJECT_RETRY */

#define MEM_BYTES 0x110100u

static uint8_t mem[MEM_BYTES];

/* ---------------------------------------------------------------------- */
/* Host hooks                                                             */
/* ---------------------------------------------------------------------- */

/* conventional memory only, as before upper-memory code */
bool xlat_code_direct(uint32_t linear, uint32_t len)
{
    return linear + len >= linear && linear + len <= 0xa0000u;
}

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

/* Replay: reference writes above undo_from are logged so the reference run
 * can start from the same memory (translated code is kept below it). */
static uint32_t undo_from = UINT32_MAX;
static struct { uint32_t a; uint8_t v; } undo_log[1 << 17];
static int undo_n;
static void undo_note(uint32_t a)
{
    if (a >= undo_from && undo_n < (int)(sizeof(undo_log) / sizeof(undo_log[0]))) {
        undo_log[undo_n].a = a;
        undo_log[undo_n].v = mem[a];
        undo_n++;
    }
}
static void undo_apply(void)
{
    xlat_dirty = true;
    while (undo_n > 0) {
        --undo_n;
        mem[undo_log[undo_n].a] = undo_log[undo_n].v;
    }
}

static uint8_t rd8(uint32_t a) { return mem[a]; }
static uint16_t rd16(uint32_t a) { return (uint16_t)(mem[a] | mem[a + 1] << 8); }
/* XRIP's write path: a write into translated code sets xlat_dirty */
static void note_w(uint32_t a) { if (a < XLAT_CODE_MAP_BYTES * 8u << XLAT_CODE_BLOCK_SHIFT) xlat_note_write(a); }
static void wr8(uint32_t a, uint8_t v) { undo_note(a); note_w(a); mem[a] = v; }
/* x87 stores (fpu_glue.c) into the test memory */
void tfpu_note_write(uint32_t a, uint32_t n)
{
    if (tfpu_mem == mem)
        for (uint32_t k = 0; k < n; ++k) undo_note(a + k);
}
static void wr16(uint32_t a, uint16_t v) { undo_note(a); undo_note(a + 1); note_w(a); mem[a] = (uint8_t)v; mem[a + 1] = (uint8_t)(v >> 8); }

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
/* XRIP op_grp3 MUL / IMUL / DIV / IDIV (cpu.c). A #DE makes the program
 * unusable for the comparison (the reference has no interrupts). */
static void ref_grp3_md(int r, uint16_t oper, bool w)
{
    if (!w) {
        const uint8_t b = (uint8_t)oper;
        if (r == 4) {
            const uint32_t t = (uint32_t)b * getreg8(0);
            putreg16(0, (uint16_t)t);
            flag_szp8((uint8_t)t);
            SETF(0, getreg8(4)); SETF(11, getreg8(4));
        } else if (r == 5) {
            const int32_t t = (int32_t)(int8_t)getreg8(0) * (int32_t)(int8_t)b;
            const int16_t res = (int16_t)t;
            const int8_t tr = (int8_t)res;
            SETF(0, res != (int16_t)tr); SETF(11, res != (int16_t)tr);
            putreg8(0, (uint8_t)tr); putreg8(4, (uint8_t)(res >> 8));
        } else if (r == 6) {
            const uint16_t v = getreg16(0);
            if (b == 0 || v / b > 0xff) { R.bad = R.halted = true; return; }
            putreg8(4, (uint8_t)(v % b)); putreg8(0, (uint8_t)(v / b));
        } else {
            const int16_t v = (int16_t)getreg16(0);
            const int8_t d = (int8_t)b;
            if (d == 0) { R.bad = R.halted = true; return; }
            const int16_t q = (int16_t)(v / d), rm_ = (int16_t)(v % d);
            if (q < -128 || q > 127) { R.bad = R.halted = true; return; }
            putreg8(0, (uint8_t)q); putreg8(4, (uint8_t)rm_);
        }
        return;
    }
    if (r == 4) {
        const uint32_t t = (uint32_t)oper * getreg16(0);
        putreg16(0, (uint16_t)t); putreg16(2, (uint16_t)(t >> 16));
        flag_szp16((uint16_t)t);
        SETF(0, getreg16(2)); SETF(11, getreg16(2));
    } else if (r == 5) {
        const int32_t t = (int32_t)(int16_t)getreg16(0) * (int32_t)(int16_t)oper;
        const int16_t tr = (int16_t)t;
        putreg16(0, (uint16_t)tr); putreg16(2, (uint16_t)(t >> 16));
        SETF(0, t != (int32_t)tr); SETF(11, t != (int32_t)tr);
    } else if (r == 6) {
        const uint32_t v = (uint32_t)getreg16(2) << 16 | getreg16(0);
        if (oper == 0 || v / oper > 0xffff) { R.bad = R.halted = true; return; }
        putreg16(2, (uint16_t)(v % oper)); putreg16(0, (uint16_t)(v / oper));
    } else {
        const int32_t v = (int32_t)((uint32_t)getreg16(2) << 16 | getreg16(0));
        const int16_t d = (int16_t)oper;
        if (d == 0 || (v == INT32_MIN && d == -1)) { R.bad = R.halted = true; return; }
        const int32_t q = v / d, rm_ = v % d;
        if (q < -32768 || q > 32767) { R.bad = R.halted = true; return; }
        putreg16(0, (uint16_t)q); putreg16(2, (uint16_t)rm_);
    }
}

/* XRIP op_grp2_8 / op_grp2_16 (cpu.c), cf/of as flag bits. */
#define XCF_SET(v) SETF(0, v)
#define XOF_SET(v) SETF(11, v)
static uint8_t ref_grp2_8(uint8_t r, uint8_t cnt, uint8_t oper1b)
{
    uint16_t s = oper1b;
    cnt &= 0x1F;
    if (!cnt) return (uint8_t)s;
    switch (r) {
    case 0: { const uint8_t rotate = cnt & 7;
        s = (uint8_t)((s << rotate) | (s >> ((8 - rotate) & 7)));
        XCF_SET(s & 1); XOF_SET(CF() ^ ((s >> 7) & 1)); break; }
    case 1: { const uint8_t rotate = cnt & 7;
        s = (uint8_t)((s >> rotate) | (s << ((8 - rotate) & 7)));
        XCF_SET(s >> 7); XOF_SET((s >> 7) ^ ((s >> 6) & 1)); break; }
    case 2: { const uint32_t rotate = cnt % 9;
        const uint32_t ring = ((uint32_t)(uint8_t)s << 1) | CF();
        const uint32_t shifted = ((ring << rotate) | (ring >> (9 - rotate))) & 0x1FFu;
        s = (uint16_t)(shifted >> 1); XCF_SET(shifted & 1); XOF_SET(CF() ^ ((s >> 7) & 1)); break; }
    case 3: { const uint32_t rotate = cnt % 9;
        const uint32_t ring = ((uint32_t)CF() << 8) | (uint8_t)s;
        const uint32_t shifted = ((ring >> rotate) | (ring << (9 - rotate))) & 0x1FFu;
        s = (uint16_t)(shifted & 0xFFu); XCF_SET(shifted >> 8); XOF_SET((s >> 7) ^ ((s >> 6) & 1)); break; }
    case 4: case 6:
        XCF_SET(cnt <= 8 ? (s >> (8 - cnt)) & 1 : 0);
        s = (uint16_t)(((uint32_t)s << cnt) & 0xFF);
        XOF_SET(CF() ^ (s >> 7)); flag_szp8((uint8_t)s); break;
    case 5:
        XOF_SET(cnt == 1 && (s & 0x80));
        XCF_SET(cnt <= 8 ? (s >> (cnt - 1)) & 1 : 0);
        s >>= cnt; flag_szp8((uint8_t)s); break;
    case 7:
        XCF_SET((s >> ((cnt <= 8 ? cnt : 8) - 1)) & 1);
        s = (uint8_t)((int32_t)(int8_t)s >> cnt);
        XOF_SET(0); flag_szp8((uint8_t)s); break;
    }
    return (uint8_t)s;
}
static uint16_t ref_grp2_16(uint8_t r, uint8_t cnt, uint16_t oper1)
{
    uint32_t s = oper1;
    cnt &= 0x1F;
    if (!cnt) return (uint16_t)s;
    switch (r) {
    case 0: { const uint8_t rotate = cnt & 15;
        s = ((s << rotate) | (s >> ((16 - rotate) & 15))) & 0xFFFF;
        XCF_SET(s & 1); XOF_SET(CF() ^ ((s >> 15) & 1)); break; }
    case 1: { const uint8_t rotate = cnt & 15;
        s = ((s >> rotate) | (s << ((16 - rotate) & 15))) & 0xFFFF;
        XCF_SET(s >> 15); XOF_SET((s >> 15) ^ ((s >> 14) & 1)); break; }
    case 2: { const uint32_t rotate = cnt >= 17 ? cnt - 17 : cnt;
        const uint32_t ring = ((uint32_t)(uint16_t)s << 1) | CF();
        const uint32_t shifted = ((ring << rotate) | (ring >> (17 - rotate))) & 0x1FFFFu;
        s = shifted >> 1; XCF_SET(shifted & 1); XOF_SET(CF() ^ ((s >> 15) & 1)); break; }
    case 3: { const uint32_t rotate = cnt >= 17 ? cnt - 17 : cnt;
        const uint32_t ring = ((uint32_t)CF() << 16) | (uint16_t)s;
        const uint32_t shifted = ((ring >> rotate) | (ring << (17 - rotate))) & 0x1FFFFu;
        s = shifted & 0xFFFFu; XCF_SET(shifted >> 16); XOF_SET((s >> 15) ^ ((s >> 14) & 1)); break; }
    case 4: case 6:
        XCF_SET(cnt <= 16 ? (s >> (16 - cnt)) & 1 : 0);
        s = (s << cnt) & 0xFFFF;
        XOF_SET(CF() ^ (s >> 15)); flag_szp16((uint16_t)s); break;
    case 5:
        XOF_SET(cnt == 1 && (s & 0x8000));
        XCF_SET(cnt <= 16 ? (s >> (cnt - 1)) & 1 : 0);
        s >>= cnt; flag_szp16((uint16_t)s); break;
    case 7:
        XCF_SET((s >> ((cnt <= 16 ? cnt : 16) - 1)) & 1);
        s = (uint16_t)((int32_t)(int16_t)s >> cnt);
        XOF_SET(0); flag_szp16((uint16_t)s); break;
    }
    return (uint16_t)s;
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

/* The test's ports: a read returns a function of everything done on the
 * ports before (port, value, order), so the reference and the translated
 * code agree only if they do the same accesses in the same order. */
static uint8_t zero_byte, one_byte = 1;
static uint32_t io_sum;
static uint32_t tio(uint32_t port, uint32_t bytes, bool out, uint32_t value)
{
    io_sum = ((io_sum ^ port * 0x9e3779b1u ^ value * 0x85ebca6bu) + (out ? 0x1234567u : 0u) + bytes) *
             0xc2b2ae35u + 1u;
    return out ? 0u : (io_sum >> 7) & (bytes == 2 ? 0xffffu : 0xffu);
}

/* 66h in 16-bit code: XRIP's exec386() (cpu386.c.inl) for the forms the
 * generator makes; anything else makes the program unusable. */
static uint32_t rd32(uint32_t a) { return rd16(a) | (uint32_t)rd16(a + 2) << 16; }
static void wr32(uint32_t a, uint32_t v) { wr16(a, (uint16_t)v); wr16(a + 2, (uint16_t)(v >> 16)); }
static uint32_t fetch32(void) { const uint32_t lo = fetch16(); return lo | (uint32_t)fetch16() << 16; }
static uint32_t readrm32(void) { return mode < 3 ? rd32(ea) : R.gpr[rm]; }
static void writerm32(uint32_t v) { if (mode < 3) wr32(ea, v); else R.gpr[rm] = v; }
static bool alu32(int op, uint32_t a, uint32_t b, uint32_t *res)
{
    switch (op) {
    case 0: *res = a + b; add_flags(32, a, b); return true;
    case 1: *res = a | b; logic_flags(32, *res); return true;
    case 4: *res = a & b; logic_flags(32, *res); return true;
    case 5: *res = a - b; sub_flags(32, a, b); return true;
    case 6: *res = a ^ b; logic_flags(32, *res); return true;
    case 7: sub_flags(32, a, b); return false;
    default: R.bad = R.halted = true; return false;   /* ADC / SBB: not generated */
    }
}
static bool ref_step32(uint8_t op, int rep)
{
    uint32_t r32;
    if (op < 0x40 && (op & 7) < 6 && !(op & 1))
        return false;   /* byte operation: 66h does nothing */
    if (op < 0x40 && (op & 7) < 6 && (op & 1)) {
        const int alu = op >> 3;
        switch (op & 7) {
        case 1: modregrm(); if (alu32(alu, readrm32(), R.gpr[reg], &r32)) writerm32(r32); return true;
        case 3: modregrm(); if (alu32(alu, R.gpr[reg], readrm32(), &r32)) R.gpr[reg] = r32; return true;
        default: { const uint32_t i = fetch32(); if (alu32(alu, R.gpr[0], i, &r32)) R.gpr[0] = r32; return true; }
        }
    }
    if (op >= 0x40 && op <= 0x4f) { R.gpr[op & 7] = inc_dec(R.gpr[op & 7], op >= 0x48, 32); return true; }
    if (op >= 0xb8 && op <= 0xbf) { R.gpr[op & 7] = fetch32(); return true; }
    if (op >= 0x91 && op <= 0x97) { const uint32_t t = R.gpr[0]; R.gpr[0] = R.gpr[op & 7]; R.gpr[op & 7] = t; return true; }
    switch (op) {
    case 0x0f: {
        const uint8_t op2 = fetch8();
        modregrm();
        switch (op2) {
        case 0xb6: R.gpr[reg] = readrm8(); return true;
        case 0xbe: R.gpr[reg] = (uint32_t)(int32_t)(int8_t)readrm8(); return true;
        case 0xb7: R.gpr[reg] = readrm16(); return true;
        case 0xbf: R.gpr[reg] = (uint32_t)(int32_t)(int16_t)readrm16(); return true;
        default: break;
        }
        break;
    }
    case 0x81: case 0x83: {
        modregrm();
        const uint32_t v = readrm32();
        const uint32_t i = op == 0x81 ? fetch32() : (uint32_t)(int32_t)(int8_t)fetch8();
        if (alu32(reg, v, i, &r32)) writerm32(r32);
        return true;
    }
    case 0x85: modregrm(); alu32(4, readrm32(), R.gpr[reg], &r32); return true;
    case 0x87: modregrm(); { const uint32_t v = readrm32(); writerm32(R.gpr[reg]); R.gpr[reg] = v; } return true;
    case 0x89: modregrm(); writerm32(R.gpr[reg]); return true;
    case 0x8b: modregrm(); R.gpr[reg] = readrm32(); return true;
    case 0x98: R.gpr[0] = (uint32_t)(int32_t)(int16_t)getreg16(0); return true;
    case 0x99: R.gpr[2] = (int32_t)R.gpr[0] < 0 ? 0xffffffffu : 0; return true;
    case 0xa1: R.gpr[0] = rd32(useseg + fetch16()); return true;
    case 0xa3: wr32(useseg + fetch16(), R.gpr[0]); return true;
    case 0xa9: alu32(4, R.gpr[0], fetch32(), &r32); return true;
    case 0xc7: modregrm(); if (reg) break; { const uint32_t i = fetch32(); writerm32(i); } return true;
    case 0xf7:
        modregrm();
        if (reg == 0) { const uint32_t a = readrm32(); alu32(4, a, fetch32(), &r32); return true; }
        if (reg == 2) { writerm32(~readrm32()); return true; }
        if (reg == 3) { const uint32_t v = readrm32(); sub_flags(32, 0, v); writerm32(0u - v); return true; }
        break;
    case 0xa5: case 0xab: case 0xad: {
        const int step = R.eflags & 0x400u ? -4 : 4;
        while (!rep || getreg16(1)) {
            const uint32_t src = useseg + getreg16(6), dst = R.seg_base[0] + getreg16(7);
            if (op == 0xa5) wr32(dst, rd32(src));
            else if (op == 0xab) wr32(dst, R.gpr[0]);
            else R.gpr[0] = rd32(src);
            if (op != 0xab) putreg16(6, (uint16_t)(getreg16(6) + step));
            if (op != 0xad) putreg16(7, (uint16_t)(getreg16(7) + step));
            if (!rep) break;
            putreg16(1, (uint16_t)(getreg16(1) - 1));
        }
        return true;
    }
    case 0xa7: case 0xaf: {
        const int step = R.eflags & 0x400u ? -4 : 4;
        while (!rep || getreg16(1)) {
            const uint32_t dst = R.seg_base[0] + getreg16(7);
            if (op == 0xa7) {
                sub_flags(32, rd32(useseg + getreg16(6)), rd32(dst));
                putreg16(6, (uint16_t)(getreg16(6) + step));
            } else {
                sub_flags(32, R.gpr[0], rd32(dst));
            }
            putreg16(7, (uint16_t)(getreg16(7) + step));
            if (!rep) break;
            putreg16(1, (uint16_t)(getreg16(1) - 1));
            if (rep == 1 ? !get_zf() : get_zf()) break;
        }
        return true;
    }
    default:
        break;
    }
    R.bad = R.halted = true;
    return true;
}

static void ref_step(void)
{
    if (R.halted) return;
    useseg = R.seg_base[3];
    segoverride = false;
    uint8_t op = fetch8();
    int rep = 0;   /* 1 REP / REPE (F3), 2 REPNE (F2) */
    bool o32 = false;
    for (;;) {
        if (op == 0xf2 || op == 0xf3) { rep = op == 0xf3 ? 1 : 2; op = fetch8(); continue; }
        if (op == 0x66) { o32 = true; op = fetch8(); continue; }
        if (op == 0x26) useseg = R.seg_base[0];
        else if (op == 0x2e) useseg = R.seg_base[1];
        else if (op == 0x36) useseg = R.seg_base[2];
        else if (op == 0x3e) useseg = R.seg_base[3];
        else break;
        segoverride = true;
        op = fetch8();
    }
    if (o32 && ref_step32(op, rep)) return;
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
        /* XRIP op_0f_386: MOVZX / MOVSX with a 16-bit operand size */
        if (op2 == 0xb6) { modregrm(); putreg16(reg, readrm8()); return; }
        if (op2 == 0xbe) { modregrm(); putreg16(reg, (uint16_t)(int16_t)(int8_t)readrm8()); return; }
        if (op2 == 0xb7 || op2 == 0xbf) { modregrm(); putreg16(reg, readrm16()); return; }
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
    case 0xea: {   /* JMP far */
        const uint16_t off = fetch16(), sel = fetch16();
        R.cs = sel; R.seg_base[1] = (uint32_t)sel << 4; R.ip = off;
        return;
    }
    case 0x60: {   /* XRIP PUSHA */
        const uint16_t old = getreg16(4);
        ref_push(getreg16(0)); ref_push(getreg16(1)); ref_push(getreg16(2)); ref_push(getreg16(3));
        ref_push(old); ref_push(getreg16(5)); ref_push(getreg16(6)); ref_push(getreg16(7));
        return;
    }
    case 0x61:     /* XRIP POPA: the SP word is skipped */
        putreg16(7, ref_pop()); putreg16(6, ref_pop()); putreg16(5, ref_pop());
        putreg16(4, getreg16(4) + 2);
        putreg16(3, ref_pop()); putreg16(2, ref_pop()); putreg16(1, ref_pop()); putreg16(0, ref_pop());
        return;
    case 0xc8: {   /* XRIP ENTER */
        const uint16_t size = fetch16();
        const uint8_t level = fetch8() & 0x1f;
        ref_push(getreg16(5));
        const uint16_t frame = getreg16(4);
        if (level) {
            for (int k = 1; k < level; ++k) {
                putreg16(5, getreg16(5) - 2);
                ref_push(rd16(R.seg_base[2] + getreg16(5)));
            }
            ref_push(frame);
        }
        putreg16(5, frame);
        putreg16(4, getreg16(4) - size);
        return;
    }
    case 0xc9: putreg16(4, getreg16(5)); putreg16(5, ref_pop()); return;   /* LEAVE */
    case 0x9c: ref_push((uint16_t)(2 | (*flags_now() & 0x7fd5u))); return;   /* PUSHF */
    case 0x9d: { const uint16_t v = ref_pop(); *flags_now() = (*flags_now() & ~0x7fd5u) | (v & 0x7fd5u); return; }   /* POPF */
    case 0x9f: putreg8(4, (uint8_t)(2 | (*flags_now() & 0x7fd5u))); return;  /* LAHF */
    case 0x9e: {   /* SAHF: decodeflagsword((makeflagsword() & 0xff00) | AH) */
        const uint32_t v = ((2 | (*flags_now() & 0x7fd5u)) & 0xff00u) | getreg8(4);
        *flags_now() = (*flags_now() & ~0x7fd5u) | (v & 0x7fd5u);
        return;
    }
    case 0xd7: putreg8(0, rd8(useseg + (uint16_t)(getreg16(3) + getreg8(0)))); return;   /* XLAT */
    case 0xe0: case 0xe1: {   /* LOOPNZ / LOOPZ */
        const int8_t d = (int8_t)fetch8();
        putreg16(1, getreg16(1) - 1);
        if (getreg16(1) && (op == 0xe1 ? get_zf() : !get_zf())) R.ip = (uint16_t)(R.ip + d);
        return;
    }
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
        ref_grp3_md(reg, readrm8(), false);
        return;
    case 0xf7:
        modregrm();
        if (reg == 0) { uint16_t a = readrm16(); logic_flags(16, a & fetch16()); return; }
        if (reg == 2) { writerm16((uint16_t)~readrm16()); return; }
        if (reg == 3) { const uint16_t v = readrm16(); sub_flags(16, 0, v); writerm16((uint16_t)-v); return; }
        ref_grp3_md(reg, readrm16(), true);
        return;
    case 0x69: case 0x6b: {   /* XRIP: IMUL Gv, Ev, Iv / Ib */
        modregrm();
        int32_t t1 = (int32_t)(int16_t)readrm16();
        const int32_t t2 = op == 0x69 ? (int32_t)(int16_t)fetch16() : (int32_t)(int8_t)fetch8();
        t1 *= t2;
        putreg16(reg, (uint16_t)(int16_t)t1);
        SETF(0, t1 != (int32_t)(int16_t)t1); SETF(11, t1 != (int32_t)(int16_t)t1);
        return;
    }
    case 0x98: putreg16(0, (uint16_t)(int16_t)(int8_t)getreg8(0)); return;
    case 0x99: putreg16(2, (getreg16(0) & 0x8000) ? 0xffff : 0); return;
    case 0xfc: R.eflags &= ~0x400u; return;
    case 0xfd: R.eflags |= 0x400u; return;
    case 0xfa: R.eflags &= ~0x200u; return;
    case 0xfb: R.eflags |= 0x200u; return;
    case 0xe4: case 0xe5: case 0xe6: case 0xe7: case 0xec: case 0xed: case 0xee: case 0xef: {
        const uint32_t port = op & 8 ? getreg16(2) : fetch8(), bytes = op & 1 ? 2u : 1u;
        if (op & 2) {
            xlat_dirty = true;   /* XRIP: an OUT may start a device writing RAM */
            tio(port, bytes, true, bytes == 2 ? getreg16(0) : getreg8(0));
        }
        else if (bytes == 2)
            putreg16(0, (uint16_t)tio(port, 2, false, 0));
        else
            putreg8(0, (uint8_t)tio(port, 1, false, 0));
        return;
    }
    case 0xaa: case 0xab: case 0xac: case 0xad: case 0xa4: case 0xa5: {
        const int w = op & 1, step = (R.eflags & 0x400u ? -1 : 1) * (w ? 2 : 1);
        while (!rep || getreg16(1)) {
            const uint32_t src = useseg + getreg16(6), dst = R.seg_base[0] + getreg16(7);
            if (op <= 0xa5) { if (w) wr16(dst, rd16(src)); else wr8(dst, rd8(src)); }
            else if (op <= 0xab) { if (w) wr16(dst, getreg16(0)); else wr8(dst, getreg8(0)); }
            else { if (w) putreg16(0, rd16(src)); else putreg8(0, rd8(src)); }
            if (op <= 0xa5 || op >= 0xac) putreg16(6, (uint16_t)(getreg16(6) + step));
            if (op <= 0xab) putreg16(7, (uint16_t)(getreg16(7) + step));
            if (!rep) break;
            putreg16(1, (uint16_t)(getreg16(1) - 1));
        }
        return;
    }
    case 0xc0: modregrm(); { const uint8_t v = readrm8(); const uint8_t n = fetch8(); writerm8(ref_grp2_8(reg, n, v)); } return;
    case 0xc1: modregrm(); { const uint16_t v = readrm16(); const uint8_t n = fetch8(); writerm16(ref_grp2_16(reg, n, v)); } return;
    case 0xd0: modregrm(); writerm8(ref_grp2_8(reg, 1, readrm8())); return;
    case 0xd1: modregrm(); writerm16(ref_grp2_16(reg, 1, readrm16())); return;
    case 0xd2: modregrm(); writerm8(ref_grp2_8(reg, getreg8(1), readrm8())); return;
    case 0xd3: modregrm(); writerm16(ref_grp2_16(reg, getreg8(1), readrm16())); return;
    case 0xa6: case 0xa7: case 0xae: case 0xaf: {   /* XRIP CMPS / SCAS */
        const int w = op & 1, step = (R.eflags & 0x400u ? -1 : 1) * (w ? 2 : 1);
        while (!rep || getreg16(1)) {
            const uint32_t dst = R.seg_base[0] + getreg16(7);
            if (op <= 0xa7) {
                const uint32_t src = useseg + getreg16(6);
                if (w) sub_flags(16, rd16(src), rd16(dst)); else sub_flags(8, rd8(src), rd8(dst));
                putreg16(6, (uint16_t)(getreg16(6) + step));
            } else {
                if (w) sub_flags(16, getreg16(0), rd16(dst)); else sub_flags(8, getreg8(0), rd8(dst));
            }
            putreg16(7, (uint16_t)(getreg16(7) + step));
            if (!rep) break;
            putreg16(1, (uint16_t)(getreg16(1) - 1));
            if (rep == 1 ? !get_zf() : get_zf()) break;
        }
        return;
    }
    case 0x06: case 0x0e: case 0x16: case 0x1e: ref_push((uint16_t)(R.seg_base[op >> 3] >> 4)); return;
    case 0x07: case 0x1f: R.seg_base[op >> 3] = (uint32_t)ref_pop() << 4; return;
    case 0x8e: modregrm(); if (reg != 0 && reg != 3) break; R.seg_base[reg] = (uint32_t)readrm16() << 4; return;
    case 0x8c: modregrm(); if (reg > 3) break; writerm16((uint16_t)(R.seg_base[reg] >> 4)); return;
    case 0xc4: case 0xc5: {
        modregrm();
        if (mode == 3) break;
        const uint16_t off = rd16(ea), sel = rd16(ea + 2);
        putreg16(reg, off);
        R.seg_base[op == 0xc4 ? 0 : 3] = (uint32_t)sel << 4;
        return;
    }
    case 0xf8: SETF(0, 0); return;
    case 0xf9: SETF(0, 1); return;
    case 0xfe: modregrm(); if (reg > 1) break; writerm8((uint8_t)inc_dec(readrm8(), reg != 0, 8)); return;
    case 0xff: modregrm();
        if (reg == 6) { ref_push(readrm16()); return; }
        if (reg == 2) { const uint16_t t = readrm16(); ref_push(R.ip); R.ip = t; return; }
        if (reg == 4) { R.ip = readrm16(); return; }
        if (reg == 3 || reg == 5) {   /* far through m16:16 (the register form: XRIP's stale EA) */
            if (mode == 3) break;
            const uint16_t off = rd16(ea), sel = rd16(ea + 2);
            if (reg == 3) { ref_push(R.cs); ref_push(R.ip); }
            R.cs = sel; R.seg_base[1] = (uint32_t)sel << 4; R.ip = off;
            return;
        }
        if (reg > 1) break;
        writerm16((uint16_t)inc_dec(readrm16(), reg != 0, 16)); return;
    case 0x8f: modregrm(); if (reg) break; { const uint16_t v = ref_pop(); writerm16(v); } return;
    case 0x9b: return;   /* FWAIT (CR0.MP/TS clear) */
    case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf: {
        /* XRIP's xrip_fpu(): fpu.c (CR0.EM/TS clear); the forms with the CPU
         * flags (FCOMI, FCMOV) are not generated */
        modregrm();
        const int fop = op - 0xd8;
        if (mode == 3 && ((fop == 2 && reg < 4) || (fop == 3 && (reg < 4 || reg == 5 || reg == 6)) ||
                          (fop == 7 && (reg == 5 || reg == 6))))
            break;
        const int exc = tfpu_exceptions;
        uint16_t ax = getreg16(0);
        xlat_dirty = true;   /* XRIP: fpu.c stores past mem_write */
        tfpu_exec(fop, reg, rm, mode != 3, ea, &ax);
        putreg16(0, ax);
        if (tfpu_exceptions != exc) break;
        return;
    }
    default:
        if (op >= 0xb0 && op <= 0xb7) { putreg8(op & 7, fetch8()); return; }
        if (op >= 0xb8 && op <= 0xbf) { putreg16(op & 7, fetch16()); return; }
        break;
    }
    R.bad = R.halted = true;
}

/* The test's xlat_rt_fpu(): fpu.c as the core's; every 7th call refuses
 * (as with CR0.EM set): XRIP (the reference) does that one. */
static unsigned long fpu_calls;
uint32_t xlat_rt_fpu(xlat_rt_t *rt, uint32_t ctl, uint32_t addr)
{
    if (++fpu_calls % 7 == 0)
        return 0;
    if (ctl & XLAT_FPU_WAIT)
        return 1;
    uint16_t ax = (uint16_t)rt->gpr[0];
    tfpu_exec((int)(ctl & 7u), (int)((ctl >> 3) & 7u), (int)((ctl >> 6) & 7u), (ctl & XLAT_FPU_MEM) != 0,
              addr, &ax);
    rt->gpr[0] = (rt->gpr[0] & 0xffff0000u) | ax;
    return 1;
}

/* The test's xlat_rt_io(): the test ports; every 5th call refuses (XRIP,
 * the reference, does that one). */
static const volatile uint8_t *test_irq;   /* rt->intr when IF is set */
static unsigned long io_calls;
uint32_t xlat_rt_io(xlat_rt_t *rt, uint32_t ctl, uint32_t value)
{
    if (ctl & (XLAT_IO_CLI | XLAT_IO_STI | XLAT_IO_SET_IF)) {
        const bool enable = (ctl & XLAT_IO_STI) || ((ctl & XLAT_IO_SET_IF) && value);
        const bool shadow = (ctl & XLAT_IO_STI) && !(rt->eflags & 0x200u);
        rt->eflags = (rt->eflags & ~0x200u) | (enable ? 0x200u : 0u);
        rt->intr = enable && test_irq ? test_irq : &zero_byte;
        return XLAT_IO_DONE | ((shadow || *rt->intr) ? XLAT_IO_BOUNDARY : 0u);
    }
    if (++io_calls % 5 == 0)
        return 0;
    const uint32_t bytes = ctl >> 16 & 0xfu;
    const bool out = (ctl & XLAT_IO_OUT) != 0;
    return XLAT_IO_DONE | tio(ctl & 0xffffu, bytes, out, out ? value & (bytes == 2 ? 0xffffu : 0xffu) : 0u);
}

/* Model the same decoded memory access in the host reference. */
uint32_t xlat_bus_read(uint32_t address, unsigned bytes)
{
    ++xlat_stats.bus_read;
    return bytes == 1 ? rd8(address) : bytes == 2 ? rd16(address) : rd32(address);
}

/* Model the physical VGA aperture using the host's guest memory. */
uint32_t xlat_vga_read(uint32_t address, unsigned bytes)
{
    return bytes == 1 ? rd8(address) : bytes == 2 ? rd16(address) : rd32(address);
}

void xlat_vga_write(uint32_t address, uint32_t value, unsigned bytes)
{
    if (bytes == 1)
        wr8(address, (uint8_t)value);
    else if (bytes == 2)
        wr16(address, (uint16_t)value);
    else
        wr32(address, value);
}

/* The host reference has no native BIOS ROM dispatcher. */
bool xlat_try_native_escape(xlat_rt_t *rt)
{
    (void)rt;
    return false;
}

bool xlat_try_native_thunk(xlat_rt_t *rt, unsigned kind)
{
    (void)rt; (void)kind;
    return false;
}

#if XLAT_PRECOMPILE
static void (*exec_inflight)(void);
#endif

/* The test's xlat_interp_one(): the reference interpreter is the "XRIP". */
uint32_t xlat_interp_one(xlat_rt_t *rt, uint32_t flags)
{
#if XLAT_PRECOMPILE
    if (exec_inflight) {
        exec_inflight();
        return 0;
    }
#endif
    if (flags & 4u)
        side_calls++;
    R.ip = (uint16_t)rt->ip;
    R.flags_result = rt->lazy_result; R.flags_dst = rt->lazy_dst; R.flags_src = rt->lazy_src;
    R.flags_op = rt->lazy_op; R.flags_cf = rt->lazy_cf; R.eflags = rt->eflags;
    R.seg_base[0] = rt->seg_base[0]; R.seg_base[3] = rt->seg_base[3];
    R.cs = rt->cs; R.seg_base[1] = rt->seg_base[1];   /* after a far transfer in the region */
    const bool old_if = (R.eflags & 0x200u) != 0;
    ref_step();
    /* Mirror the fused STI/POP helper using the reference instructions. */
    if ((flags & XLAT_XI_STI_POP) && (!old_if || !test_irq || !*test_irq))
        ref_step();
    rt->seg_base[0] = R.seg_base[0]; rt->seg_base[2] = R.seg_base[2]; rt->seg_base[3] = R.seg_base[3];
    rt->ip = R.ip;
    rt->lazy_result = R.flags_result; rt->lazy_dst = R.flags_dst; rt->lazy_src = R.flags_src;
    rt->lazy_op = R.flags_op; rt->lazy_cf = R.flags_cf; rt->eflags = R.eflags;
    rt->intr = (R.eflags & 0x200u) && test_irq ? test_irq : &zero_byte;
    interp_calls++;
    if (R.halted)
        return 0;
    const bool valid = xlat_region_still_valid(rt);
    if (R.cs != rt->cs) {
        rt->cs = R.cs;
        rt->seg_base[1] = R.seg_base[1];
        return valid ? 2 : 0;
    }
    return valid ? 1 : 0;
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

typedef struct {
    uint8_t b[12]; uint8_t len; int8_t fwd; uint8_t rel16; int8_t call;
    uint8_t ckind, abs16, abspos, absadd;
} ginsn_t;
/* fwd: branch target (body index) patched as rel8 / rel16 in the last bytes,
 * or (abs16) as the absolute offset + absadd at abspos; call: subroutine
 * number, patched after the subroutines are laid out as ckind says */
enum { CK_NEAR = 0, CK_FAR_ALIAS = 1, CK_FAR_LOCAL = 2, CK_NEAR_BX = 3 };

#define MAX_SUBS 4
static int nsubs;
static uint16_t gen_cs;   /* CS the program runs in: far calls use the alias CS-1 */
static struct { uint32_t pos; int sub; uint8_t kind; } call_fix[64];
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

static void put_mem_modrm(ginsn_t *g, int regf)
{
    const int m = (int)rn(3), r = (int)rn(8);
    g->b[g->len++] = (uint8_t)(m << 6 | regf << 3 | r);
    if ((m == 0 && r == 6) || m == 2) {
        const uint16_t d = (uint16_t)rnd();
        g->b[g->len++] = (uint8_t)d;
        g->b[g->len++] = (uint8_t)(d >> 8);
    } else if (m == 1) {
        g->b[g->len++] = (uint8_t)rnd();
    }
}

static void put_imm32(ginsn_t *g)
{
    const uint32_t v = rn(3) ? rn(16) : (rn(2) ? 0xffffffffu - rn(4) : ((uint32_t)rnd() << 16 | rnd()));
    for (int k = 0; k < 4; ++k)
        g->b[g->len++] = (uint8_t)(v >> 8 * k);
}

/* 66h forms: the ones x86_decode makes native, and a few it leaves to XRIP */
static void gen_o32(ginsn_t *g)
{
    static const uint8_t alus[6] = {0, 1, 4, 5, 6, 7};   /* no ADC / SBB (XRIP: eager flags) */
    const int alu = alus[rn(6)];
    const int t = (int)rn(14);
    if (t == 13) {   /* string, REP with a small count */
        static const uint8_t str[] = {0xa5, 0xab, 0xad, 0xa7, 0xaf};
        const uint8_t op = str[rn(5)];
        bool reps = rn(2);
        if (rn(3) == 0)
            g->b[g->len++] = rn(2) ? 0xfd : 0xfc;   /* STD / CLD */
        if (reps) {
            g->b[g->len++] = 0xb9;
            g->b[g->len++] = (uint8_t)rn(5);
            g->b[g->len++] = 0;
        }
        if (reps && rn(2)) { g->b[g->len++] = op == 0xa7 || op == 0xaf ? (rn(2) ? 0xf3 : 0xf2) : 0xf3; reps = false; }
        g->b[g->len++] = 0x66;
        if (reps) g->b[g->len++] = op == 0xa7 || op == 0xaf ? (rn(2) ? 0xf3 : 0xf2) : 0xf3;
        g->b[g->len++] = op;
        return;
    }
    g->b[g->len++] = 0x66;
    switch (t) {
    case 0: case 1: {   /* ALU E,G / G,E */
        const bool to_g = rn(2);
        g->b[g->len++] = (uint8_t)(alu << 3 | (to_g ? 3 : 1));
        const int gr = to_g && alu != 7 ? pick_reg16_dst() : (int)rn(8);
        put_modrm(g, gr, true, !to_g && alu != 7);
        break;
    }
    case 2:             /* ALU EAX, imm32 */
        g->b[g->len++] = (uint8_t)(alu << 3 | 5);
        put_imm32(g);
        break;
    case 3: {           /* 81 / 83 */
        const bool i8 = rn(2);
        g->b[g->len++] = i8 ? 0x83 : 0x81;
        put_modrm(g, alu, true, alu != 7);
        if (i8) g->b[g->len++] = (uint8_t)rnd(); else put_imm32(g);
        break;
    }
    case 4: {           /* TEST */
        const int k = (int)rn(3);
        if (k == 0) { g->b[g->len++] = 0x85; put_modrm(g, rn(8), true, false); }
        else if (k == 1) { g->b[g->len++] = 0xa9; put_imm32(g); }
        else { g->b[g->len++] = 0xf7; put_modrm(g, 0, true, false); put_imm32(g); }
        break;
    }
    case 5:             /* INC / DEC */
        g->b[g->len++] = (uint8_t)((rn(2) ? 0x48 : 0x40) | pick_reg16_dst());
        break;
    case 6: {           /* MOV */
        const int k = (int)rn(4);
        if (k == 0) { g->b[g->len++] = (uint8_t)(0xb8 | pick_reg16_dst()); put_imm32(g); }
        else if (k == 1 && allow_mem) { g->b[g->len++] = rn(2) ? 0xa1 : 0xa3; uint16_t d = (uint16_t)rnd(); g->b[g->len++] = (uint8_t)d; g->b[g->len++] = (uint8_t)(d >> 8); }
        else if (k == 2) { g->b[g->len++] = 0xc7; put_modrm(g, 0, true, true); put_imm32(g); }
        else {
            const bool to_g = rn(2);
            g->b[g->len++] = to_g ? 0x8b : 0x89;
            put_modrm(g, to_g ? pick_reg16_dst() : (int)rn(8), true, !to_g);
        }
        break;
    }
    case 7: {           /* XCHG */
        if (rn(2)) { const int r = pick_reg16_dst(); g->b[g->len++] = (uint8_t)(r ? 0x90 | r : 0x87); if (!r) g->b[g->len++] = 0xc0 | 3 << 3 | 2; }
        else { g->b[g->len++] = 0x87; g->b[g->len++] = (uint8_t)(0xc0 | pick_reg16_dst() << 3 | pick_reg16_dst()); }
        break;
    }
    case 8:             /* CWDE / CDQ */
        g->b[g->len++] = rn(2) ? 0x98 : 0x99;
        break;
    case 9: {           /* NOT / NEG */
        g->b[g->len++] = 0xf7;
        put_modrm(g, rn(2) ? 2 : 3, true, true);
        break;
    }
    case 10: {          /* MOVZX / MOVSX r32 */
        static const uint8_t m[4] = {0xb6, 0xb7, 0xbe, 0xbf};
        const uint8_t op2 = m[rn(4)];
        g->b[g->len++] = 0x0f;
        g->b[g->len++] = op2;
        put_modrm(g, pick_reg16_dst(), op2 & 1, false);
        break;
    }
    default: {          /* left to XRIP: ADD with a byte operand size (66 00) */
        g->b[g->len++] = 0x00;
        put_modrm(g, pick_reg8_dst(), false, true);
        break;
    }
    }
}

/* x87 forms (the translator's lists, and some it leaves to XRIP) */
static void gen_x87(ginsn_t *g)
{
    static const uint8_t memok[8] = {0xff, 0xbd, 0xff, 0xad, 0xff, 0xdd, 0xff, 0xfd};  /* + env/save */
    static const uint8_t regok_d9_4 = 0x33;   /* FCHS FABS FTST FXAM */
    if (rn(3) == 0) {   /* emulator fix-up padding, folded by x86_decode */
        static const uint8_t pads[4][3] = {{0x90}, {0x9b}, {0x90, 0x9b}, {0x9b, 0x90, 0x90}};
        const int k = (int)rn(4);
        for (int j = 0; j < 3 && pads[k][j]; ++j)
            g->b[g->len++] = pads[k][j];
        if (rn(4) == 0)
            return;     /* a lone wait run (90 9B before other code) */
    }
    if (allow_mem && rn(2)) {
        int op, grp;
        do { op = (int)rn(8); grp = (int)rn(8); } while (!(memok[op] >> grp & 1));
        if (rn(4) == 0) g->b[g->len++] = rn(2) ? 0x26 : 0x2e;
        g->b[g->len++] = (uint8_t)(0xd8 + op);
        put_mem_modrm(g, grp);
        return;
    }
    for (;;) {
        const int op = (int)rn(8), grp = (int)rn(8), rm = (int)rn(8);
        bool ok;
        switch (op) {
        case 0: case 4: case 6: ok = true; break;
        case 1: ok = grp == 2 ? rm == 0 : grp == 4 ? (regok_d9_4 >> rm & 1) : grp == 5 ? rm != 7 : true; break;
        case 2: ok = grp == 5 && rm == 1; break;
        case 3: ok = grp == 4 && (rm == 2 || rm == 3); break;
        case 5: ok = grp <= 5; break;
        default: ok = grp <= 3 || (grp == 4 && rm == 0); break;
        }
        if (!ok) continue;
        g->b[g->len++] = (uint8_t)(0xd8 + op);
        g->b[g->len++] = (uint8_t)(0xc0 | grp << 3 | rm);
        return;
    }
}

static void gen_one(ginsn_t *g, int index, int total)
{
    memset(g, 0, sizeof(*g));
    g->fwd = -1;
    g->call = -1;
    if (total > 0 && rn(14) == 0) {    /* stack: PUSH/POP, CALL to a subroutine */
        const int t = rn(7);
        if (t == 0 && nsubs) {
            g->call = (int8_t)rn((uint32_t)nsubs);
            if (g->call & 1) {      /* odd subroutines are far: CALL CS-1:off+16 or CS:off */
                const uint16_t sel = rn(2) ? gen_cs : (uint16_t)(gen_cs - 1);
                g->ckind = sel == gen_cs ? CK_FAR_LOCAL : CK_FAR_ALIAS;
                g->b[g->len++] = 0x9a; g->b[g->len++] = 0; g->b[g->len++] = 0;
                g->b[g->len++] = (uint8_t)sel; g->b[g->len++] = (uint8_t)(sel >> 8);
            } else if (rn(3) == 0) {   /* mov bx, offset; call bx */
                g->ckind = CK_NEAR_BX;
                g->b[g->len++] = 0xbb; g->b[g->len++] = 0; g->b[g->len++] = 0;
                g->b[g->len++] = 0xff; g->b[g->len++] = 0xd3;
            } else {
                g->b[g->len++] = 0xe8; g->b[g->len++] = 0; g->b[g->len++] = 0;
            }
        } else if (t == 5) {     /* PUSHA / POPA / PUSHF / ENTER / LEAVE / POPF */
            const int f = (int)rn(7);
            if (f >= 5) {        /* PUSHF / PUSH imm; POPF (IF, DF, and now and then TF) */
                if (f == 5) {
                    g->b[g->len++] = 0x9c;
                } else {
                    const uint16_t v = (uint16_t)((rnd() & 0x0ed7u) | (rn(16) ? 0u : 0x100u));
                    g->b[g->len++] = 0x68; g->b[g->len++] = (uint8_t)v; g->b[g->len++] = (uint8_t)(v >> 8);
                }
                g->b[g->len++] = 0x9d;
            } else if (f == 0) g->b[g->len++] = 0x60;
            else if (f == 1) g->b[g->len++] = 0x61;
            else if (f == 2) g->b[g->len++] = 0x9c;
            else if (f == 3) { g->b[g->len++] = 0xc8; g->b[g->len++] = (uint8_t)(rn(3) * 2); g->b[g->len++] = rn(8) ? 0 : (uint8_t)rnd(); g->b[g->len++] = (uint8_t)rn(3); }
            else g->b[g->len++] = 0xc9;
        } else if (t == 6 && index + 1 < total) {   /* JMP far CS / CS-1, JMP BX: forward */
            const int f = (int)rn(3);
            if (f < 2) {
                const uint16_t sel = f ? gen_cs : (uint16_t)(gen_cs - 1);
                g->b[g->len++] = 0xea; g->b[g->len++] = 0; g->b[g->len++] = 0;
                g->b[g->len++] = (uint8_t)sel; g->b[g->len++] = (uint8_t)(sel >> 8);
                g->absadd = f ? 0 : 16;
            } else {
                g->b[g->len++] = 0xbb; g->b[g->len++] = 0; g->b[g->len++] = 0;
                g->b[g->len++] = 0xff; g->b[g->len++] = 0xe3;
            }
            g->abs16 = 1;
            g->abspos = 1;
            g->fwd = (int8_t)(index + 1 + rn((uint32_t)(total - index)));
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
    if (rn(8) == 0) {
        gen_o32(g);
        return;
    }
    if (rn(24) == 0) {     /* IN / OUT (port imm8 or DX), CLI, STI */
        static const uint8_t ops[10] = {0xe4, 0xe5, 0xe6, 0xe7, 0xec, 0xed, 0xee, 0xef, 0xfa, 0xfb};
        const uint8_t op = ops[rn(10)];
        g->b[g->len++] = op;
        if ((op & 0xf8) == 0xe0)
            g->b[g->len++] = (uint8_t)rnd();
        return;
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
    case 13: {                     /* XCHG r/m, reg / NOP */
        const int t = rn(3);
        if (t == 0) { g->b[g->len++] = 0x90; }
        else if (t == 1) { int r = pick_reg16_dst(); g->b[g->len++] = (uint8_t)(r ? 0x90 | r : 0x90); }
        else {   /* XCHG r/m, r (the memory form now and then) */
            g->b[g->len++] = (uint8_t)(0x86 | word);
            put_modrm(g, word ? pick_reg16_dst() : pick_reg8_dst(), word, true);
        }
        break;
    }
    case 14: {                     /* instructions the translator leaves to the interpreter */
        const int t = rn(20);
        if (t == 18) {           /* x87 */
            gen_x87(g);
        } else if (t == 19) {    /* FWAIT, FNSTSW AX */
            if (rn(2)) g->b[g->len++] = 0x9b;
            else { g->b[g->len++] = 0xdf; g->b[g->len++] = 0xe0; }
        } else if (t == 15) {    /* LAHF / SAHF */
            g->b[g->len++] = rn(2) ? 0x9f : 0x9e;
        } else if (t == 16 && allow_mem) {   /* XLAT */
            if (rn(3) == 0) g->b[g->len++] = rn(2) ? 0x26 : 0x2e;
            g->b[g->len++] = 0xd7;
        } else if (t >= 16) {    /* MOVZX / MOVSX r16, r/m8 (r/m16) */
            static const uint8_t mx[] = {0xb6, 0xbe, 0xb6, 0xbe, 0xb7, 0xbf};
            const uint8_t op2 = mx[rn(6)];
            g->b[g->len++] = 0x0f;
            g->b[g->len++] = op2;
            const int dst = pick_reg16_dst();
            const size_t at = g->len;
            put_modrm(g, dst, op2 & 1, false);
            (void)at;
        } else if (t >= 13) {    /* segment registers: PUSH / POP, MOV, LES / LDS */
            const int f = (int)rn(5);
            if (f == 0) {
                g->b[g->len++] = (uint8_t)(0x06 | rn(4) << 3);
            } else if (f == 1) {
                g->b[g->len++] = rn(2) ? 0x07 : 0x1f;
            } else if (f == 2) {
                g->b[g->len++] = 0x8e;
                put_modrm(g, rn(2) ? 0 : 3, true, false);
            } else if (f == 3) {
                g->b[g->len++] = 0x8c;
                put_modrm(g, (int)rn(4), true, true);
            } else if (allow_mem) {
                g->b[g->len++] = rn(2) ? 0xc4 : 0xc5;
                const size_t at = g->len;
                put_modrm(g, pick_reg16_dst(), true, false);
                if ((g->b[at] >> 6) == 3)
                    g->b[at] &= 0x3f;    /* memory form only: mod 0 */
                if ((g->b[at] & 0xc7) == 0x06) { g->b[g->len++] = (uint8_t)rnd(); g->b[g->len++] = (uint8_t)rnd(); }
            } else {
                g->b[g->len++] = 0x1e;
                g->b[g->len++] = 0x07;   /* push ds; pop es */
            }
        } else if (t < 3) {
            static const uint8_t f[] = {0xf8, 0xf9, 0xf5};
            g->b[g->len++] = f[t];
        } else if (t == 3) {
            g->b[g->len++] = rn(2) ? 0x98 : 0x99;
        } else if (t == 4) {
            g->b[g->len++] = rn(2) ? 0xfc : 0xfd;
        } else if (t == 5 && allow_mem) {
            static const uint8_t str[] = {0xaa, 0xab, 0xac, 0xad, 0xa4, 0xa5, 0xa6, 0xa7, 0xae, 0xaf};
            if (rn(3) == 0) {     /* REP with a small count in CX */
                g->b[g->len++] = 0xb9;
                g->b[g->len++] = (uint8_t)rn(6);
                g->b[g->len++] = 0;
                g->b[g->len++] = rn(2) ? 0xf3 : 0xf2;
            }
            if (rn(4) == 0)
                g->b[g->len++] = rn(2) ? 0x26 : 0x2e;   /* source segment override */
            g->b[g->len++] = str[rn(10)];
        } else if (t == 6) {     /* NOT / NEG / MUL / IMUL / DIV / IDIV, IMUL r, r/m, imm */
            const int sub = 2 + (int)rn(7);
            if (sub == 8) {
                const bool i8 = rn(2);
                g->b[g->len++] = i8 ? 0x6b : 0x69;
                put_modrm(g, pick_reg16_dst(), true, false);
                g->b[g->len++] = (uint8_t)rnd();
                if (!i8) g->b[g->len++] = (uint8_t)rnd();
            } else {
                if (sub >= 6 && rn(2)) {     /* a small dividend: DX = 0, divisor != 0 mostly */
                    g->b[g->len++] = 0x31; g->b[g->len++] = 0xd2;   /* xor dx, dx */
                }
                g->b[g->len++] = (uint8_t)(0xf6 | word);
                put_modrm(g, sub, word, sub < 4);
            }
        } else if (t >= 8 && t < 13) {     /* shifts and rotates: count 1, CL or imm8 */
            const int form = (int)rn(3);
            g->b[g->len++] = (uint8_t)((form == 0 ? 0xd0 : form == 1 ? 0xd2 : 0xc0) | word);
            put_modrm(g, (int)rn(8), word, true);
            if (form == 2)
                g->b[g->len++] = (uint8_t)(rn(4) ? rn(18) : rnd());
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
        if (body[i].fwd >= 0 && body[i].abs16) {
            const uint32_t to = at_ip + pos[body[i].fwd] + body[i].absadd;
            code[p + body[i].abspos] = (uint8_t)to;
            code[p + body[i].abspos + 1] = (uint8_t)(to >> 8);
        } else if (body[i].fwd >= 0) {
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
            call_fix[ncall_fix].pos = at_ip + p + 1;
            call_fix[ncall_fix].sub = body[i].call;
            call_fix[ncall_fix].kind = body[i].ckind;
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
        const int ctl = rn(4);
        int32_t rel;
        if (ctl == 0) { code[p++] = 0x49; code[p++] = 0x75; rel = (int32_t)head - (int32_t)(p + 1); }
        else if (ctl == 1) { code[p++] = 0xe2; rel = (int32_t)head - (int32_t)(p + 1); }
        else if (ctl == 3) { code[p++] = rn(2) ? 0xe0 : 0xe1; rel = (int32_t)head - (int32_t)(p + 1); }
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
        if (call_fix[k].kind != CK_NEAR) {   /* absolute: in CS, or in the CS-1 alias */
            const uint32_t off = sub_at[call_fix[k].sub] + (call_fix[k].kind == CK_FAR_ALIAS ? 16 : 0);
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
    s->eflags = 2 | (rnd() & 0xad5u);
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

/* A random x87 state: FNINIT, a few integers loaded, the control word's
 * rounding random. */
static void fpu_random_state(uint8_t *image)
{
    static uint8_t scratch[16];
    uint8_t *const m = tfpu_mem;
    uint16_t ax = 0;
    tfpu_mem = scratch;
    tfpu_exec(3, 4, 3, 0, 0, &ax);                       /* FNINIT */
    const int n = (int)rn(6);
    for (int k = 0; k < n; ++k) {
        const uint16_t v = (uint16_t)(rn(3) ? rn(200) - 100 : rnd());
        memcpy(scratch, &v, 2);
        tfpu_exec(7, 0, 0, 1, 0, &ax);                   /* FILD m16 */
    }
    const uint16_t cw = (uint16_t)(0x037f | (rn(4) << 10));
    memcpy(scratch, &cw, 2);
    tfpu_exec(1, 5, 0, 1, 0, &ax);                       /* FLDCW */
    tfpu_mem = m;
    tfpu_state(image, 1);
}

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
            xlat_dirty = true, memcpy(mem, base_mem, REPLAY_SPAN);
            const uint32_t cs_base = (uint32_t)reg[r].cs << 4;
            memset(mem + cs_base, 0xf1, 0x10000);   /* F1: decoded by neither decoder */
            for (int l = 0; l < reg[r].nlines; ++l)
                memcpy(mem + cs_base + reg[r].line[l].ip, reg[r].line[l].b, reg[r].line[l].n);
            for (int k = 0; k < 64; ++k) mem[rn(REPLAY_SPAN)] = (uint8_t)rn(4);   /* small values */
            xlat_dirty = true;
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
            static uint8_t rfpu_before[94], rfpu_after[94], rfpu_now[94];
            fpu_random_state(rfpu_before);
            tfpu_state(rfpu_before, 0);
            const uint32_t io_before = io_sum = rnd();
            undo_n = 0;
            undo_from = REPLAY_SPAN;
            xlat_flush();
            bool ran = false;
            xlat_rt_t rt = {.fn_key = UINT32_MAX};
            for (int attempt = 0; attempt < 4 && !ran; ++attempt) {
                rt.mem = mem; rt.mem_size = REPLAY_SPAN;   /* above it: XRIP (logged) */
                memcpy(rt.seg_base, R.seg_base, sizeof(rt.seg_base));
                rt.budget = 1 + (int32_t)rn(300);
                rt.clock = &test_clock; rt.extra = 0;
                rt.deadline = test_clock;   /* expired: logged loops may never end */
                rt.intr = test_irq = &zero_byte; rt.gpr = R.gpr;
                rt.lazy_result = R.flags_result; rt.lazy_dst = R.flags_dst; rt.lazy_src = R.flags_src;
                rt.eflags = R.eflags; rt.lazy_op = R.flags_op; rt.lazy_cf = R.flags_cf;
                rt.cs = R.cs; rt.ip = R.ip;
                rt.fn_key = (uint32_t)R.cs << 16 | R.ip;   /* logged function regions too */
                ran = xlat_run(&rt);
            }
            if (!ran) continue;
            ran_states++;
            if (rt.ip > 0xffffu) {   /* XRIP takes rt->ip as it is */
                printf("BAD IP region %04X:%04X: %08x\n", reg[r].cs, reg[r].ip, rt.ip);
                bad++;
            }
            R.ip = (uint16_t)rt.ip;
            R.cs = rt.cs;
            memcpy(R.seg_base, rt.seg_base, sizeof(R.seg_base));
            R.flags_result = rt.lazy_result; R.flags_dst = rt.lazy_dst; R.flags_src = rt.lazy_src;
            R.flags_op = rt.lazy_op; R.flags_cf = rt.lazy_cf; R.eflags = rt.eflags;
            ref_t after = R;
            memcpy(after_mem, mem, REPLAY_SPAN);
            tfpu_state(rfpu_after, 1);
            tfpu_state(rfpu_before, 0);
            const uint32_t io_after = io_sum;
            io_sum = io_before;
            /* the reference from 'before' must reach the same registers, flags and memory */
            xlat_dirty = true, memcpy(mem, before_mem, REPLAY_SPAN);
            undo_apply();
            undo_from = UINT32_MAX;
            R = before;
            bool found = false;
            for (int k = 0; k < 40000 && !R.halted; ++k) {
                if (k) ref_step();
                if (R.ip == after.ip && R.cs == after.cs && !memcmp(R.gpr, after.gpr, sizeof(R.gpr)) &&
                    !memcmp(R.seg_base, after.seg_base, sizeof(R.seg_base))) {
                    ref_t a = after, b = R;
                    R = a; flags_now(); a = R;
                    R = b; flags_now(); b = R;
                    tfpu_state(rfpu_now, 1);
                    if ((a.eflags & 0xed5u) == (b.eflags & 0xed5u) && !memcmp(mem, after_mem, REPLAY_SPAN) &&
                        io_sum == io_after &&
                        !memcmp(rfpu_now, rfpu_after, 94)) {
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
    printf("stats: translations %u entries %u exits b/budget/side %u/%u/%u native insns %u gap %u\n",
           xlat_stats.translations, xlat_stats.entries, xlat_stats.exits[0], xlat_stats.exits[1],
           xlat_stats.exits[2], xlat_stats.guest_insns, xlat_stats.gap_calls);
    return bad ? 1 : 0;
}

/* xlat_rt_shift() against the reference group 2 for every operation, size
 * and count, many values, both lazy and resolved incoming flags. */
static int shift_check(void)
{
    int bad = 0, n = 0;
    for (int size = 8; size <= 16; size += 8)
        for (int op = 0; op < 8; ++op)
            for (int cnt = 0; cnt < 256; ++cnt)
                for (int k = 0; k < 64; ++k) {
                    const uint16_t v = (uint16_t)(k < 4 ? (k == 0 ? 0 : k == 1 ? 0xffff : k == 2 ? 0x8000 : 1) : rnd());
                    memset(&R, 0, sizeof(R));
                    R.eflags = 2 | (rnd() & 0x8d5u);
                    if (k & 1) { uint16_t r16; alu16((int)(rn(2) ? 0 : 5 + rn(3)), (uint16_t)rnd(), (uint16_t)rnd(), &r16); }
                    xlat_rt_t rt = {.fn_key = UINT32_MAX};
                    memset(&rt, 0, sizeof(rt));
                    rt.eflags = R.eflags; rt.lazy_op = R.flags_op; rt.lazy_cf = R.flags_cf;
                    rt.lazy_result = R.flags_result; rt.lazy_dst = R.flags_dst; rt.lazy_src = R.flags_src;
                    const uint32_t got = xlat_rt_shift(&rt, size == 8 ? (v & 0xffu) : v,
                                                       (uint32_t)op | (size == 16 ? 8u : 0u) | (uint32_t)cnt << 8);
                    const uint32_t want = size == 8 ? ref_grp2_8((uint8_t)op, (uint8_t)cnt, (uint8_t)v)
                                                    : ref_grp2_16((uint8_t)op, (uint8_t)cnt, v);
                    /* both sides resolved to compare the full image */
                    flags_now();
                    const uint32_t fe = R.eflags & 0x8d5u;
                    const ref_t save = R;
                    R.flags_op = rt.lazy_op; R.flags_cf = rt.lazy_cf; R.flags_result = rt.lazy_result;
                    R.flags_dst = rt.lazy_dst; R.flags_src = rt.lazy_src; R.eflags = rt.eflags;
                    flags_now();
                    const uint32_t fg = R.eflags & 0x8d5u;
                    R = save;
                    ++n;
                    if (got != want || fg != fe) {
                        if (bad++ < 10)
                            printf("shift size %d op %d cnt %d v %04x: got %04x/%03x want %04x/%03x\n",
                                   size, op, cnt, v, got, fg, want, fe);
                    }
                }
    printf("shift check: %d cases, %d bad\n", n, bad);
    /* xlat_rt_muldiv() against the reference: every operation and size,
     * random operands (small and edge values too), lazy and resolved flags */
    int mbad = 0, mn = 0;
    for (int k = 0; k < 400000; ++k) {
        const int op = 4 + (int)rn(5);                    /* 8: IMUL r, r/m, imm */
        const bool w = op == 8 || rn(2);
        static const uint16_t edge[] = {0, 1, 0xffff, 0x8000, 0x7fff, 2, 0xff, 0x80};
        const uint16_t v = rn(4) ? (uint16_t)rnd() : edge[rn(8)];
        const uint16_t ax = rn(4) ? (uint16_t)rnd() : edge[rn(8)];
        const uint16_t dx = rn(3) ? (uint16_t)rnd() : (uint16_t)(rn(2) ? 0 : rn(4));
        const uint16_t imm = rn(2) ? (uint16_t)rnd() : (uint16_t)(int16_t)(int8_t)rnd();
        memset(&R, 0, sizeof(R));
        R.eflags = 2 | (rnd() & 0x8d5u);
        if (k & 1) { uint16_t r16; alu16((int)(rn(2) ? 0 : 5 + rn(3)), (uint16_t)rnd(), (uint16_t)rnd(), &r16); }
        putreg16(0, ax); putreg16(2, dx);
        xlat_rt_t rt = {.fn_key = UINT32_MAX};
        memset(&rt, 0, sizeof(rt));
        rt.eflags = R.eflags; rt.lazy_op = R.flags_op; rt.lazy_cf = R.flags_cf;
        rt.lazy_result = R.flags_result; rt.lazy_dst = R.flags_dst; rt.lazy_src = R.flags_src;
        const uint64_t got = xlat_rt_muldiv(&rt, (uint32_t)op | (w ? 16u : 0u), w ? v : (v & 0xffu),
                                            op == 8 ? imm : (uint32_t)ax | (uint32_t)dx << 16);
        uint16_t want_lo = 0, want_hi = 0;
        if (op == 8) {
            int32_t t1 = (int32_t)(int16_t)v * (int32_t)(int16_t)imm;
            want_lo = (uint16_t)t1;
            SETF(0, t1 != (int32_t)(int16_t)t1); SETF(11, t1 != (int32_t)(int16_t)t1);
        } else {
            ref_grp3_md(op, w ? v : (uint16_t)(v & 0xffu), w);
            want_lo = getreg16(0); want_hi = getreg16(2);
        }
        const bool want_fault = R.bad;
        const bool got_fault = (got >> 32) != 0;
        flags_now();
        const uint32_t fe = R.eflags & 0x8d5u;
        const ref_t save = R;
        R.flags_op = rt.lazy_op; R.flags_cf = rt.lazy_cf; R.flags_result = rt.lazy_result;
        R.flags_dst = rt.lazy_dst; R.flags_src = rt.lazy_src; R.eflags = rt.eflags;
        flags_now();
        const uint32_t fg = R.eflags & 0x8d5u;
        R = save;
        ++mn;
        bool ok = got_fault == want_fault;
        if (ok && !want_fault)
            ok = (uint16_t)got == want_lo && (op == 8 || (uint16_t)(got >> 16) == want_hi) && fg == fe;
        if (!ok && mbad++ < 10)
            printf("muldiv op %d w %d v %04x ax %04x dx %04x imm %04x: got %08x f%d /%03x want %04x:%04x f%d /%03x\n",
                   op, w, v, ax, dx, imm, (unsigned)got, got_fault, fg, want_hi, want_lo, want_fault, fe);
    }
    printf("muldiv check: %d cases, %d bad\n", mn, mbad);
    return bad != 0 || mbad != 0;
}

/* Regions entered one after another in one xlat_run() are not compared
 * with their x86 bytes again unless something may have written x86 code
 * (st->epoch). Region A writes into region B's code (a MOV the translated
 * code leaves to XRIP: xlat_code_map) and then jumps to B in the same run:
 * B must notice. Also a near CALL whose callee ends in RETF to this CS and
 * a JMP CX back to the caller (stack probe) stay inside one region. */
static int chain_check(void)
{
    static const struct { uint16_t ip; uint8_t n; uint8_t b[20]; } code[] = {
        /* B: mov ax,1; add bx,ax; dec si; jnz B; jmp A */
        {0x0900, 11, {0xb8, 0x01, 0x00, 0x01, 0xc3, 0x4e, 0x75, 0xf8, 0xe9, 0xf5, 0xf8}},
        /* A (out of reach of B): mov [0901],cl; dec dx; jnz A; mov dx,2; mov si,3;
         * dec cx; jz +3; jmp B; hlt. B -> A -> B ... in one xlat_run(). */
        {0x0200, 20, {0x88, 0x0e, 0x01, 0x09, 0x4a, 0x75, 0xf9, 0xba, 0x02, 0x00, 0xbe, 0x03,
                      0x00, 0x49, 0x74, 0x03, 0xe9, 0xed, 0x06, 0xf4}},
        /* C: push cs; call F; inc si; dec cx; jnz C; hlt;  F: add di,3; call P; retf;
         * P (probe): pop dx; inc bp; jmp dx */
        {0x0300, 11, {0x0e, 0xe8, 0x06, 0x00, 0x46, 0x49, 0x75, 0xf8, 0xf4, 0x90, 0x90}},
        {0x030a, 7, {0x83, 0xc7, 0x03, 0xe8, 0x01, 0x00, 0xcb}},
        {0x0311, 3, {0x5a, 0x45, 0xff}},
        {0x0314, 1, {0xe2}},
        /* D: call G; dec cx; jnz D; hlt;  G: pop bx; push 2000h; push bx; retf
         * (a RETF to an IP that is a return site here, but in another CS) */
        {0x0400, 8, {0xe8, 0x05, 0x00, 0x49, 0x75, 0xfa, 0xf4, 0x90}},
        {0x0408, 6, {0x5b, 0x68, 0x00, 0x20, 0x53, 0xcb}},
        /* E: mov ax,1234h; add bx,ax; fld1; fistp word [si] (SI = 0501h: the
         * MOV's immediate); dec di; jnz E; hlt (an x87 store into the code) */
        {0x0500, 13, {0xb8, 0x34, 0x12, 0x01, 0xc3, 0xd9, 0xe8, 0xdf, 0x1c, 0x4f, 0x75, 0xf4, 0xf4}},
        /* L (no loop in reach: a trace, as at a hot CALL target): inc ax; add bx,ax; inc bx; inc si; inc di;
         * add si,ax; dec cx; jz +3; jmp X; hlt.  X (beyond the window): add di,bx;
         * inc bp; inc bp; add si,bx; jmp L. X is built at L's hot exit and both
         * are linked: L -> X -> L ... in one xlat_run(). */
        {0x0600, 15, {0x40, 0x01, 0xc3, 0x43, 0x46, 0x47, 0x01, 0xc6, 0x49, 0x74, 0x03, 0xe9, 0xf2,
                      0x07, 0xf4}},
        {0x0e00, 9, {0x01, 0xdf, 0x45, 0x45, 0x01, 0xde, 0xe9, 0xf7, 0xf7}},
        /* M: inc ax; T: inc bx; dec cx; jnz M; hlt. A burst start at T, in
         * M's code, is left to XRIP: no second region, M is not rebuilt
         * (add_entry is for region exits only). */
        {0x0700, 6, {0x40, 0x43, 0x49, 0x75, 0xfb, 0xf4}},
    };
    int bad = 0;
    for (int test = 0; test < 6; ++test) {
        if (test == 4 && !XLAT_TRACES)
            continue;
        const uint16_t cs = 0x1000;
        ref_t init;
        memset(&init, 0, sizeof(init));
        init.cs = cs;
        init.seg_base[0] = init.seg_base[1] = init.seg_base[3] = (uint32_t)cs << 4;
        init.seg_base[2] = 0x30000;
        init.gpr[1] = test ? 40 : 6;   /* CX */
        init.gpr[2] = 2;               /* DX */
        init.gpr[6] = test == 3 ? 0x0501 : 2;   /* SI */
        init.gpr[7] = 6;               /* DI */
        init.gpr[4] = 0xfff0;          /* SP */
        init.eflags = 2;
        memset(mem, 0, MEM_BYTES);
        for (size_t k = 0; k < sizeof(code) / sizeof(code[0]); ++k)
            memcpy(mem + ((uint32_t)cs << 4) + code[k].ip, code[k].b, code[k].n);
        mem[0x20403] = 0xf4;   /* 2000:0403: HLT */
        xlat_dirty = true;
        memcpy(mem_init, mem, MEM_BYTES);
        xlat_flush();
        /* make the regions: entries seen at burst starts, run on scratch state */
        static const uint16_t entries[6][2] = {{0x0900, 0x0200}, {0x0300, 0x0300}, {0x0400, 0x0400},
                                               {0x0500, 0x0500}, {0x0600, 0x0600}, {0x0700, 0x0701}};
        static const uint16_t starts[6] = {0x0900, 0x0300, 0x0400, 0x0500, 0x0600, 0x0701};
        const unsigned long added0 = xlat_stats.entries_added, tr0 = xlat_stats.translations;
        for (int e = 0; e < 2; ++e)
            for (int k = 0; k < 4; ++k) {
                R = init;
                R.ip = entries[test][e];
                xlat_rt_t rt = {.fn_key = UINT32_MAX};
                memset(&rt, 0, sizeof(rt));
                rt.mem = mem; rt.mem_size = 0xa0000;
                memcpy(rt.seg_base, R.seg_base, sizeof(rt.seg_base));
                rt.budget = 50; rt.clock = &test_clock; rt.deadline = test_clock;
                rt.intr = &zero_byte; rt.gpr = R.gpr; rt.eflags = R.eflags;
                rt.cs = R.cs; rt.ip = R.ip;
                rt.fn_key = test == 4 ? ((uint32_t)R.cs << 16 | R.ip) : UINT32_MAX;   /* L: as a hot CALL target */
                xlat_run(&rt);
                xlat_dirty = true, memcpy(mem, mem_init, MEM_BYTES);
            }
        const unsigned long translations = xlat_stats.translations;
        /* reference */
        R = init;
        R.ip = starts[test];
        while (!R.halted) ref_step();
        const ref_t want = R;
        static uint8_t want_mem[MEM_BYTES];
        memcpy(want_mem, mem, MEM_BYTES);
        /* translated, XRIP (the reference) where it stops */
        xlat_dirty = true, memcpy(mem, mem_init, MEM_BYTES);
        R = init;
        R.ip = starts[test];
        const unsigned long entries0 = xlat_stats.entries, links0 = xlat_stats.links;
        for (int guard = 0; guard < 10000 && !R.halted; ++guard) {
            xlat_rt_t rt = {.fn_key = UINT32_MAX};
            rt.mem = mem; rt.mem_size = 0xa0000;
            memcpy(rt.seg_base, R.seg_base, sizeof(rt.seg_base));
            rt.budget = 1000; rt.clock = &test_clock; rt.deadline = test_clock + 1000u;
            rt.intr = &zero_byte; rt.gpr = R.gpr;
            rt.lazy_result = R.flags_result; rt.lazy_dst = R.flags_dst; rt.lazy_src = R.flags_src;
            rt.eflags = R.eflags; rt.lazy_op = R.flags_op; rt.lazy_cf = R.flags_cf;
            rt.cs = R.cs; rt.ip = R.ip;
            if (xlat_run(&rt)) {
                R.ip = (uint16_t)rt.ip; R.cs = rt.cs;
                memcpy(R.seg_base, rt.seg_base, sizeof(R.seg_base));
                R.flags_result = rt.lazy_result; R.flags_dst = rt.lazy_dst; R.flags_src = rt.lazy_src;
                R.flags_op = rt.lazy_op; R.flags_cf = rt.lazy_cf; R.eflags = rt.eflags;
            }
            if (!R.halted) ref_step();
        }
        const bool ok = !memcmp(want.gpr, R.gpr, sizeof(R.gpr)) && want.ip == R.ip && want.cs == R.cs &&
                        !memcmp(want_mem, mem, MEM_BYTES);
        printf("chain test %d: translations %lu, entries %lu, %s\n", test, translations,
               xlat_stats.entries - entries0, ok ? "ok" : "MISMATCH");
        if (!ok) {
            for (int i = 0; i < 8; ++i)
                if (want.gpr[i] != R.gpr[i])
                    printf("  gpr%d want %08x got %08x\n", i, want.gpr[i], R.gpr[i]);
            bad++;
        }
        if (test == 5 && (xlat_stats.entries_added != added0 || translations - tr0 != 1)) {
            printf("  chain test 5: burst start inside a region rebuilt it (translations %lu)\n",
                   translations - tr0);
            bad++;
        }
        if (test == 5) {
            /* ... unless it stays hot: XLAT_REJECT_RETRY more burst starts
             * there give M that entry (sample_retry) */
            const unsigned long retries0 = xlat_stats.retries_built;
            for (int k = 0; k < XLAT_REJECT_RETRY + 1; ++k) {
                xlat_rt_t rt;
                memset(&rt, 0, sizeof(rt));
                rt.fn_key = UINT32_MAX;
                rt.mem = mem; rt.mem_size = 0xa0000;
                memcpy(rt.seg_base, init.seg_base, sizeof(rt.seg_base));
                ref_t scratch = init;
                rt.budget = 50; rt.clock = &test_clock; rt.deadline = test_clock;
                rt.intr = &zero_byte; rt.gpr = scratch.gpr; rt.eflags = init.eflags;
                rt.cs = init.cs; rt.ip = 0x0701;
                xlat_run(&rt);
                xlat_dirty = true, memcpy(mem, mem_init, MEM_BYTES);
            }
            if (xlat_stats.retries_built != retries0 + 1 || xlat_stats.entries_added != added0 + 1) {
                printf("  chain test 5: hot burst start inside a region got no entry\n");
                bad++;
            }
        }
        if (test == 4 && (!xlat_stats.exit_built || xlat_stats.links - links0 < 20)) {
            printf("  chain test 4: exit target not built or not linked (links %lu)\n",
                   xlat_stats.links - links0);
            bad++;
        }
        if (translations < (test != 1 && test != 4 ? 2u : 1u) - (test == 2)) {
            printf("  chain test %d: regions not translated\n", test);
            bad++;
        }
    }
    return bad;
}

/* --dump HEX [out.bin]: translate the x86 bytes HEX (at 1000:0100, as a
 * trace too) and write the Thumb code for a disassembler:
 * arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb out.bin */
static int dump_region(const char *hex, const char *path)
{
    static uint32_t ctx[64 * 1024];
    memset(mem, 0xf1, MEM_BYTES);
    uint32_t n = 0;
    for (const char *p = hex; p[0] && p[1]; p += 2) {
        unsigned v;
        if (sscanf(p, "%2x", &v) != 1) return 1;
        mem[0x10100 + n++] = (uint8_t)v;
    }
    xlat_arena_t a = {.base = xlat_test_calloc(0x10000), .size = 0x10000};
    if (!a.base || !xlat_emit_trampoline(&a) || xlat_translate_ctx_size() > sizeof(ctx)) return 1;
    const uint32_t tramp = a.code_top;
    a.data_bottom = a.size;
    xlat_rt_t rt = {.mem = mem, .mem_size = 0xa0000, .cs = 0x1000, .fn_key = UINT32_MAX};
    rt.seg_base[1] = 0x10000;
    static xlat_region_out_t out;
    bool full;
    if (!xlat_translate(&a, &rt, 0x100, 0, NULL, 0, &out, &full, ctx) &&
        !xlat_translate(&a, &rt, 0x100, 1, NULL, 0, &out, &full, ctx)) {
        printf("not translated: reason %u (insns %u)\n", out.reject, out.n_insns);
        return 1;
    }
    printf("trampoline %u bytes; region: %u x86 instructions, %u code bytes at %u, %u entries\n",
           tramp, out.n_insns, out.code_len, out.code_off, out.n_entries);
    for (unsigned k = 0; k < out.n_entries; ++k)
        printf("  entry %04X%s\n", out.entries[k].ip, out.entries[k].mark ? " marked" : "");
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fwrite(a.base + out.code_off, 1, out.code_len, f);
    fclose(f);
    return 0;
}

/* --size LOG: translate every region of an xlat.log (T lines) once and
 * print the Thumb code size by x86 instruction kind and stub kind. */
static int size_report(const char *path)
{
    static replay_region_t reg[2048];
    static uint32_t ctx[64 * 1024];
    const int nreg = replay_parse(path, reg, 2048);
    xlat_arena_t a = {.base = xlat_test_calloc(0x10000), .size = 0x10000};
    if (!a.base || !xlat_emit_trampoline(&a)) return 1;
    const uint32_t tramp = a.code_top;
    unsigned long insns = 0, code = 0, built = 0;
    static const char *const names[XLAT_SIZE_N] = {
        "other", "ALU", "INC/DEC", "MOV", "LEA", "XCHG", "NOP", "Jcc", "JMP", "LOOP", "JCXZ",
        "PUSH", "POP", "CALL", "RET", "interp", "string", "shift", "MUL/DIV", "misc", "seg",
        "far", "indirect", "stack", "x87", "flag", "LOOPcc", "IN/OUT",
        [XLAT_SIZE_RECON] = "(recon)", [XLAT_SIZE_BACK] = "back stub",
        [XLAT_SIZE_SIDE] = "side stub", [XLAT_SIZE_EXIT] = "exit stub", [XLAT_SIZE_MAT] = "(flags)"};
    for (int r = 0; r < nreg; ++r) {
        memset(mem, 0xf1, MEM_BYTES);
        const uint32_t cs_base = (uint32_t)reg[r].cs << 4;
        if (cs_base + 0x10000 > MEM_BYTES) continue;
        for (int l = 0; l < reg[r].nlines; ++l)
            memcpy(mem + cs_base + reg[r].line[l].ip, reg[r].line[l].b, reg[r].line[l].n);
        xlat_rt_t rt = {.mem = mem, .mem_size = 0xa0000, .cs = reg[r].cs, .fn_key = UINT32_MAX};
        rt.seg_base[1] = cs_base;
        a.code_top = tramp;
        a.data_bottom = a.size;
        static xlat_region_out_t out;
        bool full;
        if (getenv("XLAT_SIZE_TRACE")) fprintf(stderr, "region %d %04X:%04X\n", r, reg[r].cs, reg[r].ip);
        if (!xlat_translate(&a, &rt, reg[r].ip, XLAT_LINK_MIN, NULL, 0, &out, &full, ctx)) continue;
        built++;
        insns += out.n_insns;
        code += out.code_len;
    }
    printf("%d regions, %lu built: %lu x86 instructions, %lu code bytes (%.1f per instruction)\n",
           nreg, built, insns, code, insns ? (double)code / (double)insns : 0.0);
    for (int k = 0; k < XLAT_SIZE_N; ++k)
        if (xlat_size_count[k])
            printf("  %-10s n=%6u bytes=%7u (%4.1f%%) avg %.1f\n", names[k] ? names[k] : "?",
                   xlat_size_count[k], xlat_size_bytes[k], 100.0 * xlat_size_bytes[k] / (double)code,
                   (double)xlat_size_bytes[k] / xlat_size_count[k]);
    return 0;
}

#if XLAT_PRECOMPILE
static const uint8_t exec_loop[] = {0xb9, 5, 0, 0x40, 0xe2, 0xfd, 0xeb, 0x40};
enum { EXEC_FAR_CS = 0x1020, EXEC_FAR_ENTRY = EXEC_FAR_CS << 4 };

static void prepare_exec_test(uint16_t cs, uint16_t ip, uint32_t linear, uint32_t size)
{
    xlat_precompile_image(mem, 0xa0000u, (uint32_t)cs << 16 | ip, linear, size, &test_clock);
}

static xlat_rt_t exec_test_rt(uint16_t cs, uint16_t ip)
{
    return (xlat_rt_t){
        .mem = mem, .mem_size = 0xa0000u, .cs = cs, .ip = ip,
        .seg_base = {[1] = (uint32_t)cs << 4},
        .budget = 50, .deadline = test_clock, .wall_deadline = test_clock + XLAT_SLICE_US,
        .intr = &zero_byte, .clock = &test_clock, .gpr = R.gpr,
        .eflags = 0x202, .fn_key = UINT32_MAX,
    };
}

static void try_exec_inflight(void)
{
    exec_inflight = NULL;
    xlat_flush();
    prepare_exec_test(EXEC_FAR_CS, 0, EXEC_FAR_ENTRY, sizeof(exec_loop));
}

static int precompile_check(void)
{
    const uint16_t cs = 0x1000, ip = 0x100;
    const uint32_t entry = ((uint32_t)cs << 4) + ip;
    const uint32_t built0 = xlat_stats.translations, runs0 = xlat_stats.entries;
    const uint32_t time0 = xlat_stats.translate_us, evict0 = xlat_stats.evictions;
    const uint32_t retry0 = xlat_stats.ring_retries;
    tfpu_mem = mem;
    memcpy(mem + entry, exec_loop, sizeof(exec_loop));

    /* The LOOP displacement is outside the image, even though it is in RAM. */
    prepare_exec_test(cs, ip, entry, sizeof(exec_loop) - 3u);
    if (xlat_stats.translations != built0) {
        puts("precompile read an incomplete instruction beyond the image");
        return 1;
    }
    prepare_exec_test(cs, ip, entry, sizeof(exec_loop));
    if (xlat_stats.translations != built0 + 1u || xlat_stats.entries != runs0 ||
        R.gpr[0] || xlat_stats.translate_us != time0) {
        puts("precompile did not only publish the initial region");
        return 1;
    }
    xlat_rt_t rt = exec_test_rt(cs, ip);
    xlat_dirty = true;
    if (!xlat_run(&rt) || R.gpr[0] != 5) {
        puts("cold EXEC entry did not run the prepared code");
        return 1;
    }
    rt = exec_test_rt(cs, ip);
    wr8(entry + 3u, 0x48);        /* INC AX -> DEC AX after publication */
    if (xlat_run(&rt)) {
        puts("modified precompiled code was still executable");
        return 1;
    }

    xlat_flush();
    memcpy(mem + EXEC_FAR_ENTRY, exec_loop, sizeof(exec_loop));
    mem[entry] = 0xea;             /* JMP FAR 0000:0000 prior to fixup */
    wr16(entry + 1u, 0);
    wr16(entry + 3u, EXEC_FAR_CS);  /* post-relocation segment */
    prepare_exec_test(cs, ip, entry, 5);
    if (xlat_stats.translations != built0 + 1u) {
        puts("precompile followed a direct edge outside the image");
        return 1;
    }
    prepare_exec_test(cs, ip, entry, EXEC_FAR_ENTRY - entry + sizeof(exec_loop));
    R.gpr[0] = 0;
    rt = exec_test_rt(EXEC_FAR_CS, 0);
    if (xlat_stats.translations != built0 + 2u || !xlat_run(&rt) || R.gpr[0] != 5) {
        puts("precompile missed the direct target in the relocated segment");
        return 1;
    }

    /* A flush from inside a native return frame cannot recycle its arena. */
    xlat_flush();
    memcpy(mem + entry, exec_loop, sizeof(exec_loop) - 2u);
    mem[entry + 6u] = 0xcc;
    prepare_exec_test(cs, ip, entry, 7);
    const uint32_t running_built = xlat_stats.translations, running_flushes = xlat_stats.flushes;
    exec_inflight = try_exec_inflight;
    rt = exec_test_rt(cs, ip);
    if (!xlat_run(&rt) || exec_inflight || xlat_stats.translations != running_built ||
        xlat_stats.flushes != running_flushes + 1u) {
        puts("nested EXEC precompile did not honor the deferred flush");
        return 1;
    }

    xlat_flush();
    const uint32_t chain_built0 = xlat_stats.translations, ring0 = xlat_stats.code_bytes;
    const unsigned chain = XLAT_IMAGE_ROOTS + 2u;
    for (unsigned i = 0; i < chain; ++i) {
        const uint32_t linear = EXEC_FAR_ENTRY + 16u * i;
        memcpy(mem + linear, exec_loop, sizeof(exec_loop) - 2u);
        mem[linear + 6u] = 0xea;
        wr16(linear + 7u, 0);
        wr16(linear + 9u, (uint16_t)(EXEC_FAR_CS + i + 1u));
    }
    prepare_exec_test(EXEC_FAR_CS, 0, EXEC_FAR_ENTRY, 16u * chain);
    const unsigned built = xlat_stats.translations - chain_built0;
    const uint32_t bytes = xlat_stats.code_bytes - ring0;
    if (!built || built > XLAT_IMAGE_ROOTS || bytes > XLAT_ARENA_SIZE / 8u || bytes > 32768u ||
        xlat_stats.evictions != evict0 || xlat_stats.ring_retries != retry0) {
        puts("precompile exceeded its root or arena budget");
        return 1;
    }
    printf("EXEC precompile: cold entry, bounds, far target, invalidation, deferred flush, budget (%u roots) OK\n", built);
    return 0;
}
#endif

/* Compare uninterrupted and sliced emission, then change a decoded byte
 * while a job is paused. No execution of the unpublished native code. */
static int compile_resume_check(void)
{
    static uint32_t ctx[64 * 1024];
    static uint8_t expected[0x10000];
    static xlat_region_out_t want, got;
    static const uint8_t code[] = {0x40, 0x83, 0xf8, 0x20, 0x75, 0xfa, 0xc3};
    memcpy(mem + 0x10100, code, sizeof(code));
    xlat_arena_t a = {.base = xlat_test_calloc(0x10000), .size = 0x10000};
    if (!a.base || !xlat_emit_trampoline(&a) || xlat_translate_ctx_size() > sizeof(ctx))
        return 1;
    const uint32_t top = a.code_top;
    xlat_rt_t rt = {.mem = mem, .mem_size = 0xa0000, .cs = 0x1000};
    rt.seg_base[1] = 0x10000;
    bool full;
    if (!xlat_translate(&a, &rt, 0x100, 0, NULL, 0, &want, &full, ctx))
        return 1;
    memcpy(expected, a.base + want.code_off, want.code_len);
    for (uint32_t ticks = 1; ticks <= 16; ++ticks) {
        a.code_top = top;
        a.data_bottom = a.size;
        bool resume = false, ok = false;
        for (int step = 0; step < 4096; ++step) {
            xlat_test_compile_ticks = ticks;
            ok = xlat_translate_step(&a, &rt, 0x100, 0, NULL, 0, &got, &full, ctx, resume, NULL);
            if (ok || got.reject != XLAT_RJ_DEFER)
                break;
            resume = true;
        }
        xlat_test_compile_ticks = UINT32_MAX;
        if (!ok || got.code_len != want.code_len ||
            memcmp(expected, a.base + got.code_off, want.code_len))
            return 1;
    }
    /* Insufficient room must preserve analysis for a new emission pass. */
    a.code_top = top;
    a.data_bottom = top + 8u;
    if (xlat_translate_step(&a, &rt, 0x100, 0, NULL, 0, &got, &full, ctx, false, NULL) ||
        !full || got.reject != XLAT_RJ_FULL || a.code_top != top)
        return 1;
    a.data_bottom = a.size;
    if (!xlat_translate_step(&a, &rt, 0x100, 0, NULL, 0, &got, &full, ctx, true, NULL) ||
        got.code_len != want.code_len || memcmp(expected, a.base + got.code_off, want.code_len))
        return 1;
    a.code_top = top;
    a.data_bottom = a.size;
    xlat_test_compile_ticks = 1; /* first instruction decoded */
    bool ok = xlat_translate_step(&a, &rt, 0x100, 0, NULL, 0, &got, &full, ctx, false, NULL);
    xlat_test_compile_ticks = UINT32_MAX;
    if (ok || got.reject != XLAT_RJ_DEFER)
        return 1;
    mem[0x10100] = 0x48; /* INC -> DEC, same length */
    ok = xlat_translate_step(&a, &rt, 0x100, 0, NULL, 0, &got, &full, ctx, true, NULL);
    if (ok || got.reject != XLAT_RJ_CHANGED || a.code_top != top)
        return 1;
    puts("resumable compiler: OK");
    return 0;
}

/* Expired CPU deadlines do not cut a graph; a real IRQ defers it. */
static int compile_irq_check(void)
{
    static uint32_t ctx[64 * 1024];
    static xlat_region_out_t out;
    const uint16_t ip = 0x100;
    const unsigned n = 40;
    memset(mem + 0x10100, 0x40, n); /* INC AX */
    mem[0x10100 + n] = 0xeb;      /* JMP back to the loop head */
    mem[0x10101 + n] = (uint8_t)(-(int)n - 2);
    xlat_arena_t a = {.base = xlat_test_calloc(0x10000), .size = 0x10000};
    if (!a.base || !xlat_emit_trampoline(&a) || xlat_translate_ctx_size() > sizeof(ctx))
        return 1;
    const uint32_t top = a.code_top;
    uint32_t clock = 1000;
    uint8_t irq = 0;
    xlat_rt_t rt = {.mem = mem, .mem_size = 0xa0000, .cs = 0x1000,
                    .clock = &clock, .intr = &irq, .wall_deadline = 1};
    rt.seg_base[1] = 0x10000;
    bool full;
    if (!xlat_translate(&a, &rt, ip, 0, NULL, 0, &out, &full, ctx) ||
        out.n_insns != n + 1)
        return 1;
    a.code_top = top;
    irq = 1;
    if (xlat_translate_step(&a, &rt, ip, 0, NULL, 0, &out, &full, ctx, false, NULL) ||
        out.reject != XLAT_RJ_DEFER || a.code_top != top)
        return 1;
    irq = 0;
    if (!xlat_translate_step(&a, &rt, ip, 0, NULL, 0, &out, &full, ctx, true, NULL) ||
        out.n_insns != n + 1)
        return 1;
    /* The full long loop must emit identical code and entry/run metadata
     * when every compiler phase is repeatedly suspended. */
    static xlat_region_out_t want;
    static uint8_t expected[0x10000];
    a.code_top = top;
    if (!xlat_translate(&a, &rt, ip, 0, NULL, 0, &want, &full, ctx) ||
        want.code_len > sizeof(expected))
        return 1;
    memcpy(expected, a.base + want.code_off, want.code_len);
    for (uint32_t ticks = 1; ticks <= 16; ++ticks) {
        a.code_top = top;
        a.data_bottom = a.size;
        bool resume = false, ok = false;
        for (unsigned step = 0; step < 4096; ++step) {
            xlat_test_compile_ticks = ticks;
            ok = xlat_translate_step(&a, &rt, ip, 0, NULL, 0, &out, &full, ctx, resume, NULL);
            if (ok || out.reject != XLAT_RJ_DEFER)
                break;
            resume = true;
        }
        xlat_test_compile_ticks = UINT32_MAX;
        if (!ok || out.code_len != want.code_len || out.n_entries != want.n_entries ||
            out.n_runs != want.n_runs || out.data_size != want.data_size ||
            memcmp(expected, a.base + out.code_off, want.code_len))
            return 1;
        for (unsigned k = 0; k < out.n_entries; ++k)
            if (out.entries[k].ip != want.entries[k].ip ||
                out.entries[k].mark != want.entries[k].mark ||
                out.entries[k].code_off != want.entries[k].code_off)
                return 1;
        for (unsigned k = 0; k < out.n_runs; ++k)
            if (out.runs[k].ip != want.runs[k].ip || out.runs[k].len != want.runs[k].len)
                return 1;
    }
    puts("IRQ-only compiler suspension / full graph: OK");
    return 0;
}

/* Schedule at a different PC so this test only executes host C. */
static int compile_scheduler_check(void)
{
    static const uint8_t trace[] = {
        0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0xc3
    };
    static const uint8_t loop[] = {0x40, 0x83, 0xf8, 0x20, 0x75, 0xfa, 0xc3};
    static const uint16_t cs = 0x1000, call = 0x100, burst = 0x200, retry = 0x300;
    const uint32_t base = (uint32_t)cs << 4;
    uint32_t clock = 0;
    uint8_t irq = 0;
    xlat_rt_t rt = {.mem = mem, .mem_size = 0xa0000, .cs = cs, .ip = 0x800,
                    .clock = &clock, .intr = &irq, .wall_deadline = XLAT_SLICE_US,
                    .fn_key = UINT32_MAX};
    rt.seg_base[1] = base;
    memcpy(mem + base + call, trace, sizeof(trace));
    memcpy(mem + base + burst, loop, sizeof(loop));
    memcpy(mem + base + retry, trace, sizeof(trace));

    for (unsigned i = 1; i < XLAT_CALL_HOT; ++i)
        if (xlat_call_hot(base + call))
            return 1;
    if (!xlat_call_hot(base + call))
        return 1;
    for (unsigned i = 1; i < XLAT_CALL_RESCAN; ++i)
        if (xlat_call_hot(base + call))
            return 1;
    if (!xlat_call_hot(base + call))
        return 1;

    /* A CALL whose cut was displaced by an IRQ is queued at the next entry. */
    rt.fn_key = (uint32_t)cs << 16 | call;
    xlat_run(&rt);
    rt.fn_key = UINT32_MAX;
    for (unsigned i = 0; i < 2 * XLAT_CALL_RESCAN; ++i)
        if (xlat_call_hot(base + call))
            return 1;

    xlat_compile_credit(XLAT_COMPILE_CREDIT_US - 1);
    rt.eflags = 0x200;
    rt.ip++;
    xlat_run(&rt);
    if (xlat_stats.translations || xlat_stats.compile_defers)
        return 1;
    xlat_compile_credit(1);
    rt.eflags = 0;
    rt.ip++;
    xlat_run(&rt);
    rt.eflags = 0x200;
    irq = 1;
    rt.ip++;
    xlat_run(&rt);
    if (xlat_stats.translations || xlat_stats.compile_defers)
        return 1;

    irq = 0;
    rt.ip++;
    xlat_test_compile_ticks = 1;
    xlat_run(&rt);
    xlat_test_compile_ticks = UINT32_MAX;
    if (xlat_stats.compile_defers != 1 || xlat_stats.translations)
        return 1;
    rt.eflags = 0;
    rt.ip = burst;
    for (unsigned i = 0; i < XLAT_HOT_THRESHOLD; ++i)
        xlat_run(&rt);
    rt.eflags = 0x200;
    rt.ip = 0x810;
    xlat_run(&rt); /* same burst: no additional slice */
    if (xlat_stats.compile_resumes || xlat_stats.translations)
        return 1;
    rt.wall_deadline += XLAT_SLICE_US;
    rt.ip++;
    xlat_run(&rt);
    if (xlat_stats.compile_resumes != 1 || xlat_stats.translations != 1)
        return 1;
    rt.wall_deadline += XLAT_SLICE_US;
    rt.ip++;
    xlat_run(&rt); /* the burst sample taken with a paused job and IF=0 */
    if (xlat_stats.translations != 2 || xlat_stats.fn_regions != 1)
        return 1;

    rt.eflags = 0;
    rt.ip = retry;
    for (unsigned i = 0; i < XLAT_HOT_THRESHOLD; ++i)
        xlat_run(&rt);
    rt.eflags = 0x200;
    rt.wall_deadline += XLAT_SLICE_US;
    rt.ip = 0x813;
    xlat_run(&rt); /* first attempt: trace has no loop */
    if (xlat_stats.rejects != 1 || xlat_stats.retries)
        return 1;
    rt.eflags = 0;
    rt.ip = retry;
    for (unsigned i = 0; i < XLAT_REJECT_RETRY; ++i)
        xlat_run(&rt);
    rt.eflags = 0x200;
    rt.wall_deadline += XLAT_SLICE_US;
    rt.ip = 0x814;
    xlat_test_compile_ticks = 1;
    xlat_run(&rt);
    xlat_test_compile_ticks = UINT32_MAX;
    if (xlat_stats.retries != 1 || xlat_stats.retries_built)
        return 1;
    rt.wall_deadline += XLAT_SLICE_US;
    rt.ip++;
    xlat_run(&rt);
    if (xlat_stats.translations != 3 || xlat_stats.retries_built != 1)
        return 1;

    xlat_flush();
    const uint32_t drops = xlat_stats.compile_queue_drops;
    const uint16_t extra_call = 0x400 + 16 * XLAT_COMPILE_QUEUE;
    rt.eflags = 0;
    /* Fill the queue with CALLs, which have the highest priority. */
    for (unsigned i = 0; i < XLAT_COMPILE_QUEUE; ++i) {
        rt.ip = (uint16_t)(0x400 + 16 * i);
        memcpy(mem + base + rt.ip, trace, sizeof(trace));
        rt.fn_key = (uint32_t)cs << 16 | rt.ip;
        xlat_run(&rt);
    }
    rt.ip = extra_call;
    for (unsigned i = 1; i < XLAT_CALL_HOT; ++i)
        if (xlat_call_hot(base + rt.ip))
            return 1;
    if (!xlat_call_hot(base + rt.ip))
        return 1;
    rt.fn_key = (uint32_t)cs << 16 | rt.ip;
    xlat_run(&rt);
    rt.fn_key = UINT32_MAX;
    for (unsigned i = 0; i < 4 * XLAT_HOT_THRESHOLD; ++i) {
        if (xlat_call_hot(base + extra_call))
            return 1;
        rt.ip = 0x900;
        xlat_run(&rt);
    }
    if (xlat_stats.compile_queue_drops != drops + 2)
        return 1;
    xlat_compile_credit(XLAT_COMPILE_CREDIT_US);
    rt.eflags = 0x200;
    rt.wall_deadline += XLAT_SLICE_US;
    rt.ip++;
    xlat_run(&rt);
    if (xlat_stats.translations != 4)
        return 1;
    for (unsigned i = 1; i < XLAT_CALL_RESCAN; ++i)
        if (xlat_call_hot(base + extra_call))
            return 1;
    if (!xlat_call_hot(base + extra_call))
        return 1;
    rt.eflags = 0;
    rt.ip = extra_call;
    rt.fn_key = (uint32_t)cs << 16 | rt.ip;
    xlat_run(&rt);
    if (xlat_stats.compile_queue_drops != drops + 2 || xlat_call_queue_blocked)
        return 1;

    puts("compiler scheduler: OK");
    return 0;
}

int main(int argc, char **argv)
{
#if XLAT_PRECOMPILE
    if (argc > 1 && !strcmp(argv[1], "--precompile"))
        return precompile_check();
#endif
    if (argc > 1 && !strcmp(argv[1], "--compile-irq"))
        return compile_irq_check();
    if (argc > 1 && !strcmp(argv[1], "--compile-resume"))
        return compile_resume_check();
    if (argc > 1 && !strcmp(argv[1], "--compile-scheduler"))
        return compile_scheduler_check();
    tfpu_mem = mem;
    if (argc > 2 && !strcmp(argv[1], "--size"))
        return size_report(argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--dump"))
        return dump_region(argv[2], argc > 3 ? argv[3] : "region.bin");
    if (argc > 1 && !strcmp(argv[1], "--shift")) {
        rng = 0x9e3779b9u;
        const int r = shift_check();
        return chain_check() || r;
    }
    if (argc > 2 && !strcmp(argv[1], "--replay")) {
        rng = 0x12345u;
        return replay(argv[2], argc > 3 ? atoi(argv[3]) : 300);
    }
    const int programs = argc > 1 ? atoi(argv[1]) : 2000;
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1;
    int run = 0, skipped = 0, failures = 0;
    lockstep = getenv("XLAT_LOCKSTEP") != NULL;
    /* XLAT_KEEP: no flush between programs (the ring evicts instead) */
    const bool keep_cache = getenv("XLAT_KEEP") != NULL;
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

        static uint8_t fpu_init[94], fpu_want[94], fpu_got[94];
        fpu_random_state(fpu_init);

        /* reference */
        xlat_dirty = true, memcpy(mem, mem_init, MEM_BYTES);
        tfpu_state(fpu_init, 0);
        const uint32_t io_init = io_sum = rnd();
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
        tfpu_state(fpu_want, 1);
        const uint32_t io_want = io_sum;

        /* translated */
        xlat_dirty = true, memcpy(mem, mem_init, MEM_BYTES);
        tfpu_state(fpu_init, 0);
        io_sum = io_init;
        R = init;
        if (!reuse_cs && rn(8) && !keep_cache) xlat_flush();
        const uint32_t mem_size = rn(3) ? 0xa0000u : 0x20000u + rn(0x80000);
        const uint32_t umb_room = rn(2) ? 0x100000u - XLAT_UMB_BASE - XLAT_UMB_MARGIN : 0;
        long guard = 0;
        while (!R.halted && guard++ < 400000) {
            xlat_rt_t rt = {.fn_key = UINT32_MAX};
            rt.mem = mem;
            rt.mem_size = mem_size;
            rt.umb_room = umb_room;   /* the test's memory is plain up to MEM_BYTES */
            memcpy(rt.seg_base, R.seg_base, sizeof(rt.seg_base));
            rt.budget = 1 + (int32_t)rn(300);
            rt.clock = &test_clock; rt.extra = 0;
            rt.deadline = test_clock + (rn(2) ? 0u : 1000u);   /* expired / never reached */
            rt.intr = test_irq = rn(20) ? &zero_byte : &one_byte;
            rt.gpr = R.gpr;
            rt.lazy_result = R.flags_result;
            rt.lazy_dst = R.flags_dst;
            rt.lazy_src = R.flags_src;
            rt.eflags = R.eflags;
            rt.lazy_op = R.flags_op;
            rt.lazy_cf = R.flags_cf;
            rt.cs = R.cs;
            rt.ip = R.ip;
            rt.fn_key = rn(3) ? UINT32_MAX : ((uint32_t)R.cs << 16 | R.ip);   /* as at a hot CALL target */
            rt.exit_kind = 0xff;
            ref_t before = R;
            static uint8_t before_mem[MEM_BYTES], fpu_before[94], fpu_after[94], fpu_now[94];
            const uint32_t io_before = io_sum;
            if (lockstep) { memcpy(before_mem, mem, MEM_BYTES); tfpu_state(fpu_before, 1); }
            if (getenv("XLAT_TRACE2")) printf("  run? ip %04x sp %04x cx %04x\n", R.ip, getreg16(4), getreg16(1));
            if (xlat_run(&rt)) {
                if (rt.ip > 0xffffu) {   /* XRIP takes rt->ip as it is */
                    printf("seed %u: exit IP %08x\n", seed, rt.ip);
                    failures++;
                }
                R.cs = rt.cs; R.seg_base[1] = rt.seg_base[1];
                R.seg_base[0] = rt.seg_base[0]; R.seg_base[3] = rt.seg_base[3];
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
                    xlat_dirty = true, memcpy(mem, before_mem, MEM_BYTES);
                    tfpu_state(fpu_after, 1);
                    tfpu_state(fpu_before, 0);
                    const uint32_t io_after = io_sum;
                    io_sum = io_before;
                    R = before;
                    bool found = false;
                    /* a run past its budget (deadline not reached) did up to extra more */
                    const long lim = 20000 + 2L * (long)rt.extra;
                    for (long k = 0; k < lim && !R.halted; ++k) {
                        if (getenv("XLAT_TRACE") && k < 6) printf("   ref ip %04x op %02x efl %03x res %08x\n", R.ip, R.flags_op, R.eflags, R.flags_result);
                        if (k) ref_step();
                        if (R.ip == after.ip && R.cs == after.cs && !memcmp(R.gpr, after.gpr, sizeof(R.gpr)) &&
                            !memcmp(R.seg_base, after.seg_base, sizeof(R.seg_base))) {
                            ref_t a = after, b = R;
                            R = a; flags_now(); a = R;
                            R = b; flags_now(); b = R;
                            tfpu_state(fpu_now, 1);
                            if ((a.eflags & 0xed5u) == (b.eflags & 0xed5u) && !memcmp(fpu_now, fpu_after, 94) &&
                                io_sum == io_after) {
                                found = true;
                                break;
                            }
                            printf("  lockstep: ip %04x regs match, flags want %03x got %03x (k=%ld)\n",
                                   after.ip, b.eflags & 0x8d5u, a.eflags & 0x8d5u, k);
                            printf("  got lazy op %02x res %08x dst %08x src %08x cf %d; want op %02x res %08x dst %08x src %08x cf %d\n",
                                   after.flags_op, after.flags_result, after.flags_dst, after.flags_src, after.flags_cf,
                                   R.flags_op, R.flags_result, R.flags_dst, R.flags_src, R.flags_cf);
                            R = b;
                        }
                    }
                    if (!found && getenv("XLAT_TRACE3")) {
                        xlat_dirty = true, memcpy(mem, before_mem, MEM_BYTES);
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
                        xlat_dirty = true, memcpy(mem, after_mem, MEM_BYTES);
                        R = after;
                        break;
                    }
                    xlat_dirty = true, memcpy(mem, after_mem, MEM_BYTES);
                    tfpu_state(fpu_after, 0);
                    io_sum = io_after;
                    R = after;
                }
            }
            const int n = (int)rn(40);
            for (int k = 0; k < n && !R.halted; ++k) { if (getenv("XLAT_TRACE2")) printf("  ref %04x sp %04x\n", R.ip, getreg16(4)); ref_step(); }
        }
        flags_now();
        const bool regs_ok = !memcmp(want.gpr, R.gpr, sizeof(R.gpr)) &&
                             !memcmp(want.seg_base, R.seg_base, sizeof(R.seg_base));
        const bool flags_ok = (want.eflags & 0xed5u) == (R.eflags & 0xed5u);
        const bool ip_ok = want.ip == R.ip && want.cs == R.cs && R.halted;
        const bool mem_ok = !memcmp(want_mem, mem, MEM_BYTES);
        tfpu_state(fpu_got, 1);
        const bool fpu_ok = !memcmp(fpu_want, fpu_got, sizeof(fpu_got)) && io_sum == io_want;
        if (!(regs_ok && flags_ok && ip_ok && mem_ok && fpu_ok) || R.bad) {
            failures++;
            printf("FAIL seed %u: regs %d flags %d (want %03x got %03x) ip %d (%04x/%04x) mem %d x87 %d\n",
                   seed, regs_ok, flags_ok, want.eflags & 0x8d5u, R.eflags & 0x8d5u, ip_ok,
                   want.ip, R.ip, mem_ok, fpu_ok);
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
    printf("stats: translations %u (fn %u, at exits %u) links %u rejects %u entries %u invalid %u flushes %u evictions %u (slots %u, ring retries %u) exits b/budget/side/stop %u/%u/%u/%u native insns %u interp %lu side %lu\n",
           xlat_stats.translations, xlat_stats.fn_regions, xlat_stats.exit_built, xlat_stats.links,
           xlat_stats.rejects, xlat_stats.entries, xlat_stats.invalidations,
           xlat_stats.flushes, xlat_stats.evictions, xlat_stats.slot_evictions, xlat_stats.ring_retries,
           xlat_stats.exits[0], xlat_stats.exits[1],
           xlat_stats.exits[2], xlat_stats.exits[3], xlat_stats.guest_insns, interp_calls, side_calls);
    return failures ? 1 : 0;
}
