/*
 * murm386 xlat: decoder for the 16-bit real-mode subset the translator
 * handles. Anything else returns false and ends the region there (XRIP runs
 * it). Semantics follow src/xrip386/cpu.c.
 */
#include "xlat_internal.h"

#define SEG_ES 0
#define SEG_CS 1
#define SEG_SS 2
#define SEG_DS 3
#define REG_SP 4

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* ModR/M at code[*pos]; E operand into *e, G register number into *g. */
static bool decode_modrm(const uint8_t *code, unsigned *pos, int seg_override,
                         x86_insn_t *in, x86_opd_t *e, uint8_t *g)
{
    const uint8_t modrm = code[(*pos)++];
    const uint8_t mod = modrm >> 6, rm = modrm & 7;
    *g = (modrm >> 3) & 7;
    if (mod == 3) {
        e->kind = OPD_REG;
        e->reg = rm;
        return true;
    }
    e->kind = OPD_MEM;
    in->mem_mod = mod;
    in->mem_rm = rm;
    in->mem_direct = 0;
    in->disp = 0;
    bool ss_default = rm == 2 || rm == 3;
    if (mod == 0) {
        if (rm == 6) {
            in->disp = rd16(code + *pos);
            *pos += 2;
            in->mem_direct = 1;
        }
    } else if (mod == 1) {
        in->disp = (uint16_t)(int16_t)(int8_t)code[(*pos)++];
        ss_default |= rm == 6;
    } else {
        in->disp = rd16(code + *pos);
        *pos += 2;
        ss_default |= rm == 6;
    }
    in->mem_seg = seg_override >= 0 ? (uint8_t)seg_override : ss_default ? SEG_SS : SEG_DS;
    return true;
}

static bool reg16_ok(const x86_opd_t *o, int size)
{
    return o->kind != OPD_REG || size != 16 || o->reg != REG_SP;
}

bool x86_decode(const uint8_t *code, uint16_t ip, x86_insn_t *in)
{
    unsigned pos = 0;
    int seg = -1;
    uint8_t g;

    for (;;) {
        const uint8_t p = code[pos];
        int s = p == 0x26 ? SEG_ES : p == 0x2e ? SEG_CS : p == 0x36 ? SEG_SS :
                p == 0x3e ? SEG_DS : -1;
        if (s < 0)
            break;
        if (seg >= 0)
            return false;   /* one override is enough for real code */
        seg = s;
        ++pos;
    }

    const uint8_t op = code[pos++];
    in->ip = ip;
    in->target = 0;
    in->op = 0;
    in->sub = 0;
    in->size = 16;
    in->a.kind = in->b.kind = OPD_NONE;
    in->a.reg = in->b.reg = 0;
    in->a.imm = in->b.imm = 0;
    in->mem_mod = in->mem_rm = in->mem_direct = 0;
    in->mem_seg = SEG_DS;
    in->disp = 0;

    if (op < 0x40 && (op & 7) < 6) {
        const uint8_t alu = op >> 3;
        in->op = XO_ALU;
        in->sub = alu;
        in->size = (op & 1) ? 16 : 8;
        switch (op & 7) {
        case 0: case 1:   /* E, G */
            decode_modrm(code, &pos, seg, in, &in->a, &g);
            in->b.kind = OPD_REG;
            in->b.reg = g;
            break;
        case 2: case 3:   /* G, E */
            decode_modrm(code, &pos, seg, in, &in->b, &g);
            in->a.kind = OPD_REG;
            in->a.reg = g;
            break;
        case 4:
            in->a.kind = OPD_REG;
            in->b.kind = OPD_IMM;
            in->b.imm = code[pos++];
            break;
        default:
            in->a.kind = OPD_REG;
            in->b.kind = OPD_IMM;
            in->b.imm = rd16(code + pos);
            pos += 2;
            break;
        }
    } else if (op >= 0x40 && op <= 0x4f) {
        in->op = XO_INCDEC;
        in->sub = op >= 0x48;
        in->a.kind = OPD_REG;
        in->a.reg = op & 7;
    } else if (op >= 0x70 && op <= 0x7f) {
        in->op = XO_JCC;
        in->sub = op & 0x0f;
        const int8_t rel = (int8_t)code[pos++];
        in->target = (uint16_t)(ip + pos + rel);
    } else if (op >= 0x50 && op <= 0x57) {
        in->op = XO_PUSH;
        in->b.kind = OPD_REG;
        in->b.reg = op & 7;
    } else if (op >= 0x58 && op <= 0x5f) {
        in->op = XO_POP;
        in->a.kind = OPD_REG;
        in->a.reg = op & 7;
    } else if (op == 0x68 || op == 0x6a) {
        in->op = XO_PUSH;
        in->b.kind = OPD_IMM;
        if (op == 0x68) {
            in->b.imm = rd16(code + pos);
            pos += 2;
        } else {
            in->b.imm = (uint16_t)(int16_t)(int8_t)code[pos++];
        }
    } else if (op == 0xe8) {
        in->op = XO_CALL;
        const uint16_t rel = rd16(code + pos);
        pos += 2;
        in->target = (uint16_t)(ip + pos + rel);
    } else if (op == 0xc3 || op == 0xc2) {
        in->op = XO_RET;
        if (op == 0xc2) {
            in->b.imm = rd16(code + pos);
            pos += 2;
        }
    } else if (op == 0x0f && code[pos] >= 0x80 && code[pos] <= 0x8f) {
        in->op = XO_JCC;
        in->sub = code[pos++] & 0x0f;
        const uint16_t rel = rd16(code + pos);
        pos += 2;
        in->target = (uint16_t)(ip + pos + rel);
    } else if (op == 0x80 || op == 0x81 || op == 0x83) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);
        in->op = XO_ALU;
        in->sub = g;
        in->size = op == 0x80 ? 8 : 16;
        in->b.kind = OPD_IMM;
        if (op == 0x81) {
            in->b.imm = rd16(code + pos);
            pos += 2;
        } else if (op == 0x83) {
            in->b.imm = (uint16_t)(int16_t)(int8_t)code[pos++];
        } else {
            in->b.imm = code[pos++];
        }
    } else if (op == 0x84 || op == 0x85) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);
        in->op = XO_ALU;
        in->sub = ALU_TEST;
        in->size = op == 0x84 ? 8 : 16;
        in->b.kind = OPD_REG;
        in->b.reg = g;
    } else if (op == 0x86 || op == 0x87) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);
        if (in->a.kind != OPD_REG)
            return false;
        in->op = XO_XCHG;
        in->size = op == 0x86 ? 8 : 16;
        in->b.kind = OPD_REG;
        in->b.reg = g;
    } else if (op >= 0x88 && op <= 0x8b) {
        in->op = XO_MOV;
        in->size = (op & 1) ? 16 : 8;
        if (op < 0x8a) {
            decode_modrm(code, &pos, seg, in, &in->a, &g);
            in->b.kind = OPD_REG;
            in->b.reg = g;
        } else {
            decode_modrm(code, &pos, seg, in, &in->b, &g);
            in->a.kind = OPD_REG;
            in->a.reg = g;
        }
    } else if (op == 0x8d) {
        decode_modrm(code, &pos, seg, in, &in->b, &g);
        if (in->b.kind != OPD_MEM)
            return false;
        in->op = XO_LEA;
        in->a.kind = OPD_REG;
        in->a.reg = g;
    } else if (op == 0x90) {
        in->op = XO_NOP;
    } else if (op >= 0x91 && op <= 0x97) {
        in->op = XO_XCHG;
        in->a.kind = in->b.kind = OPD_REG;
        in->a.reg = 0;
        in->b.reg = op & 7;
    } else if (op >= 0xa0 && op <= 0xa3) {
        in->op = XO_MOV;
        in->size = (op & 1) ? 16 : 8;
        x86_opd_t *m = op < 0xa2 ? &in->b : &in->a;
        x86_opd_t *r = op < 0xa2 ? &in->a : &in->b;
        r->kind = OPD_REG;
        r->reg = 0;
        m->kind = OPD_MEM;
        in->mem_direct = 1;
        in->mem_seg = seg >= 0 ? (uint8_t)seg : SEG_DS;
        in->disp = rd16(code + pos);
        pos += 2;
    } else if (op == 0xa8 || op == 0xa9) {
        in->op = XO_ALU;
        in->sub = ALU_TEST;
        in->size = op == 0xa8 ? 8 : 16;
        in->a.kind = OPD_REG;
        in->b.kind = OPD_IMM;
        if (op == 0xa8) {
            in->b.imm = code[pos++];
        } else {
            in->b.imm = rd16(code + pos);
            pos += 2;
        }
    } else if (op >= 0xb0 && op <= 0xbf) {
        in->op = XO_MOV;
        in->size = op >= 0xb8 ? 16 : 8;
        in->a.kind = OPD_REG;
        in->a.reg = op & 7;
        in->b.kind = OPD_IMM;
        if (op >= 0xb8) {
            in->b.imm = rd16(code + pos);
            pos += 2;
        } else {
            in->b.imm = code[pos++];
        }
    } else if (op == 0xc6 || op == 0xc7) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);
        if (g != 0)
            return false;
        in->op = XO_MOV;
        in->size = op == 0xc6 ? 8 : 16;
        in->b.kind = OPD_IMM;
        if (op == 0xc6) {
            in->b.imm = code[pos++];
        } else {
            in->b.imm = rd16(code + pos);
            pos += 2;
        }
    } else if (op == 0xe2 || op == 0xe3 || op == 0xeb) {
        in->op = op == 0xe2 ? XO_LOOP : op == 0xe3 ? XO_JCXZ : XO_JMP;
        const int8_t rel = (int8_t)code[pos++];
        in->target = (uint16_t)(ip + pos + rel);
    } else if (op == 0xe9) {
        in->op = XO_JMP;
        const uint16_t rel = rd16(code + pos);
        pos += 2;
        in->target = (uint16_t)(ip + pos + rel);
    } else if (op == 0xf6 || op == 0xf7) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);
        if (g != 0)
            return false;
        in->op = XO_ALU;
        in->sub = ALU_TEST;
        in->size = op == 0xf6 ? 8 : 16;
        in->b.kind = OPD_IMM;
        if (op == 0xf6) {
            in->b.imm = code[pos++];
        } else {
            in->b.imm = rd16(code + pos);
            pos += 2;
        }
    } else if (op == 0xff && ((code[pos] >> 3) & 7) == 6) {
        decode_modrm(code, &pos, seg, in, &in->b, &g);   /* PUSH r/m16 */
        in->op = XO_PUSH;
    } else if (op == 0x8f && ((code[pos] >> 3) & 7) == 0) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);   /* POP r/m16 */
        in->op = XO_POP;
    } else if (op == 0xfe || op == 0xff) {
        decode_modrm(code, &pos, seg, in, &in->a, &g);
        if (g > 1)
            return false;
        in->op = XO_INCDEC;
        in->sub = g;
        in->size = op == 0xfe ? 8 : 16;
    } else {
        return false;
    }

    /* 8-bit immediates of 8-bit operations stay 8-bit. */
    if (in->size == 8 && in->b.kind == OPD_IMM)
        in->b.imm &= 0xffu;
    if (!reg16_ok(&in->a, in->size) || !reg16_ok(&in->b, in->size))
        return false;   /* SP stays in memory: not handled yet */
    /* No instruction may run past the 64 KiB code segment. */
    if ((uint32_t)ip + pos > 0x10000u)
        return false;
    in->len = (uint8_t)pos;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Length of any other 16-bit real-mode instruction, for instructions the  */
/* region runs through the interpreter (XO_INTERP).                        */
/* ---------------------------------------------------------------------- */

/* ModR/M plus 16-bit displacement; *pos at the ModR/M byte. */
static void skip_modrm16(const uint8_t *code, unsigned *pos, uint8_t *mod)
{
    const uint8_t m = code[(*pos)++];
    *mod = m >> 6;
    if (*mod == 1)
        *pos += 1;
    else if (*mod == 2 || (*mod == 0 && (m & 7) == 6))
        *pos += 2;
}

/* Operand-size dependent immediate: 2 bytes, 4 with 66h. */
#define IZ(o32) ((o32) ? 4u : 2u)

bool x86_decode_interp(const uint8_t *code, uint16_t ip, x86_insn_t *in)
{
    unsigned pos = 0;
    bool o32 = false, mem = false, reg_only = false, fall = true;
    uint8_t mod = 3, op;

    for (;;) {
        op = code[pos];
        if (op == 0x26 || op == 0x2e || op == 0x36 || op == 0x3e || op == 0x64 ||
            op == 0x65 || op == 0xf0 || op == 0xf2 || op == 0xf3) {
            ++pos;
        } else if (op == 0x66) {
            o32 = true;
            ++pos;
        } else if (op == 0x67) {
            return false;   /* 32-bit addressing: not decoded here */
        } else {
            break;
        }
        if (pos > 6)
            return false;
    }
    ++pos;

    if (op == 0x0f) {
        const uint8_t op2 = code[pos++];
        if (op2 == 0xff || op2 == 0x0b)
            return false;   /* native BIOS escape / UD2 */
        if (op2 >= 0x80 && op2 <= 0x8f) {
            pos += IZ(o32);
            fall = true;
        } else if ((op2 >= 0x90 && op2 <= 0x9f) || op2 == 0xa3 || op2 == 0xab ||
                   op2 == 0xb3 || op2 == 0xbb || op2 == 0xa5 || op2 == 0xad || op2 == 0xaf ||
                   (op2 >= 0xb0 && op2 <= 0xb7) || (op2 >= 0xbc && op2 <= 0xbf) ||
                   op2 <= 0x03) {
            skip_modrm16(code, &pos, &mod);
            mem = mod != 3;
            reg_only = mod == 3 && op2 > 0x03;
        } else if (op2 == 0xa4 || op2 == 0xac || op2 == 0xba) {
            skip_modrm16(code, &pos, &mod);
            pos += 1;
            mem = mod != 3;
            reg_only = mod == 3;
        } else if (op2 == 0xa0 || op2 == 0xa1 || op2 == 0xa8 || op2 == 0xa9 || op2 == 0xa2 ||
                   op2 == 0x06 || (op2 >= 0xc8 && op2 <= 0xcf)) {
            reg_only = (op2 >= 0xc8 && op2 <= 0xcf) || op2 == 0xa2;
        } else {
            return false;
        }
    } else if (op < 0x40) {
        if ((op & 7) < 4) {
            skip_modrm16(code, &pos, &mod);
            mem = mod != 3;
            reg_only = mod == 3;
        } else if ((op & 7) == 4) {
            pos += 1;
            reg_only = true;
        } else if ((op & 7) == 5) {
            pos += IZ(o32);
            reg_only = true;
        } else {
            /* 06/07 0E 16/17 1E/1F: segment push/pop; 27 2F 37 3F: BCD adjust */
            reg_only = (op & 7) == 7 && op >= 0x27;
        }
    } else if (op < 0x60) {
        reg_only = op < 0x50;        /* INC/DEC; PUSH/POP use the stack */
    } else if (op == 0x60 || op == 0x61 || (op >= 0x6c && op <= 0x6f)) {
        /* PUSHA/POPA, INS/OUTS */
    } else if (op == 0x62 || op == 0x63 || op == 0x69 || op == 0x6b) {
        skip_modrm16(code, &pos, &mod);
        pos += op == 0x69 ? IZ(o32) : op == 0x6b ? 1u : 0u;
        mem = mod != 3;
        reg_only = mod == 3 && (op == 0x69 || op == 0x6b);
    } else if (op == 0x68) {
        pos += IZ(o32);
    } else if (op == 0x6a) {
        pos += 1;
    } else if (op >= 0x70 && op <= 0x7f) {
        pos += 1;
    } else if (op >= 0x80 && op <= 0x8f) {
        skip_modrm16(code, &pos, &mod);
        if (op == 0x80 || op == 0x82 || op == 0x83)
            pos += 1;
        else if (op == 0x81)
            pos += IZ(o32);
        mem = mod != 3;
        reg_only = mod == 3 && op != 0x8e && op != 0x8f;   /* MOV Sreg / POP r/m */
    } else if (op >= 0x90 && op <= 0x9f) {
        if (op == 0x9a) {
            pos += IZ(o32) + 2;
            fall = false;
        }
        reg_only = op <= 0x99 || op == 0x9b || op == 0x9e || op == 0x9f;
    } else if (op >= 0xa0 && op <= 0xa3) {
        pos += 2;
        mem = true;
    } else if (op >= 0xa4 && op <= 0xaf) {
        if (op == 0xa8)
            pos += 1;
        else if (op == 0xa9)
            pos += IZ(o32);
        reg_only = op == 0xa8 || op == 0xa9;
        mem = !reg_only;
    } else if (op >= 0xb0 && op <= 0xbf) {
        pos += op < 0xb8 ? 1u : IZ(o32);
        reg_only = true;
    } else if (op == 0xc0 || op == 0xc1 || op == 0xc6 || op == 0xc7 || op == 0xc4 ||
               op == 0xc5) {
        skip_modrm16(code, &pos, &mod);
        pos += op == 0xc7 ? IZ(o32) : (op == 0xc4 || op == 0xc5) ? 0u : 1u;
        mem = mod != 3;
        reg_only = mod == 3 && (op == 0xc0 || op == 0xc1);
    } else if (op == 0xc2 || op == 0xca) {
        pos += 2;
        fall = false;
    } else if (op == 0xc3 || op == 0xcb || op == 0xcf) {
        fall = false;
    } else if (op == 0xc8) {
        pos += 3;
    } else if (op == 0xc9 || op == 0xcc || op == 0xce) {
        /* LEAVE, INT3, INTO */
    } else if (op == 0xcd) {
        pos += 1;
    } else if (op >= 0xd0 && op <= 0xd3) {
        skip_modrm16(code, &pos, &mod);
        mem = mod != 3;
        reg_only = mod == 3;
    } else if (op == 0xd4 || op == 0xd5) {
        pos += 1;
        reg_only = true;
    } else if (op == 0xd6 || op == 0xd7) {
        reg_only = true;
    } else if (op >= 0xd8 && op <= 0xdf) {
        skip_modrm16(code, &pos, &mod);   /* FPU */
        mem = mod != 3;
    } else if (op >= 0xe0 && op <= 0xe7) {
        pos += 1;
        reg_only = op <= 0xe5;            /* LOOPcc/JCXZ, IN; OUT may start DMA */
    } else if (op == 0xe8 || op == 0xe9) {
        pos += IZ(o32);
        fall = op == 0xe8;
    } else if (op == 0xea) {
        pos += IZ(o32) + 2;
        fall = false;
    } else if (op == 0xeb) {
        pos += 1;
        fall = false;
    } else if (op >= 0xec && op <= 0xef) {
        reg_only = op <= 0xed;
    } else if (op == 0xf4 || op == 0xf5 || (op >= 0xf8 && op <= 0xfd)) {
        reg_only = true;      /* F1 (ICEBP) stays unknown: it ends a region */
    } else if (op == 0xf6 || op == 0xf7) {
        const uint8_t g = (code[pos] >> 3) & 7;
        skip_modrm16(code, &pos, &mod);
        if (g < 2)
            pos += op == 0xf6 ? 1u : IZ(o32);
        mem = mod != 3;
        reg_only = mod == 3;
    } else if (op == 0xfe || op == 0xff) {
        const uint8_t g = (code[pos] >> 3) & 7;
        skip_modrm16(code, &pos, &mod);
        if (op == 0xff && (g == 4 || g == 5))
            fall = false;                 /* JMP near/far indirect */
        mem = mod != 3;
        reg_only = mod == 3 && g < 2;
    } else {
        return false;
    }
    (void)mem;
    if ((uint32_t)ip + pos > 0x10000u || pos > 15)
        return false;

    in->ip = ip;
    in->target = 0;
    in->len = (uint8_t)pos;
    in->op = XO_INTERP;
    in->sub = (uint8_t)((reg_only ? 0 : XI_MAY_WRITE) | (fall ? 0 : XI_NO_FALL));
    in->size = 16;
    in->a.kind = in->b.kind = OPD_NONE;
    in->mem_mod = in->mem_rm = in->mem_direct = 0;
    in->mem_seg = SEG_DS;
    in->disp = 0;
    return true;
}
