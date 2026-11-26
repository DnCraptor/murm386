/*
 * murm386 xlat: ARMv8-M Mainline (Cortex-M33) Thumb-2 instruction encoder.
 *
 * Only 32-bit encodings are emitted (except BX/BLX), so every register
 * r0..r12/lr is usable and the sizes are fixed. Encodings follow the ARMv7-M /
 * ARMv8-M ARM; tools/xlat_test checks them by execution under qemu-arm.
 */
#include "xlat_internal.h"

void arm_e16(arm_emit_t *e, uint16_t hw)
{
    if (e->pos + 2 > e->cap) {
        e->full = true;
        e->over += 2;
        return;
    }
    e->buf[e->pos] = (uint8_t)hw;
    e->buf[e->pos + 1] = (uint8_t)(hw >> 8);
    e->pos += 2;
}

void arm_e32(arm_emit_t *e, uint16_t hw1, uint16_t hw2)
{
    if (e->pos + 4 > e->cap) {
        e->full = true;
        e->over += 4;
        return;
    }
    e->buf[e->pos] = (uint8_t)hw1;
    e->buf[e->pos + 1] = (uint8_t)(hw1 >> 8);
    e->buf[e->pos + 2] = (uint8_t)hw2;
    e->buf[e->pos + 3] = (uint8_t)(hw2 >> 8);
    e->pos += 4;
}

/* ThumbExpandImm in reverse: the 12-bit field for value, or -1. */
int arm_thumb_imm(uint32_t value)
{
    uint32_t byte = value & 0xffu;
    if (value <= 0xffu)
        return (int)value;
    if (byte && value == (byte | byte << 16))
        return (int)(0x100u | byte);
    byte = (value >> 8) & 0xffu;
    if (byte && value == (byte << 8 | byte << 24))
        return (int)(0x200u | byte);
    byte = value & 0xffu;
    if (byte && value == byte * 0x01010101u)
        return (int)(0x300u | byte);
    for (unsigned rot = 8; rot < 32; ++rot) {
        const uint32_t unrotated = value << rot | value >> (32u - rot);
        if (unrotated <= 0xffu && (unrotated & 0x80u))
            return (int)(rot << 7 | (unrotated & 0x7fu));
    }
    return -1;
}

void arm_dp_reg(arm_emit_t *e, int op, int s, int rd, int rn, int rm, int sh, int amount)
{
    const unsigned imm5 = (unsigned)amount & 31u; /* LSR/ASR #32 would be 0; not used */
    arm_e32(e, (uint16_t)(0xea00u | (unsigned)op << 5 | (unsigned)(s & 1) << 4 | (unsigned)rn),
            (uint16_t)((imm5 >> 2) << 12 | (unsigned)rd << 8 | (imm5 & 3u) << 6 |
                       (unsigned)sh << 4 | (unsigned)rm));
}

bool arm_dp_imm(arm_emit_t *e, int op, int s, int rd, int rn, uint32_t imm)
{
    const int imm12 = arm_thumb_imm(imm);
    if (imm12 < 0)
        return false;
    arm_e32(e, (uint16_t)(0xf000u | ((unsigned)imm12 >> 11 & 1u) << 10 | (unsigned)op << 5 |
                          (unsigned)(s & 1) << 4 | (unsigned)rn),
            (uint16_t)(((unsigned)imm12 >> 8 & 7u) << 12 | (unsigned)rd << 8 |
                       ((unsigned)imm12 & 0xffu)));
    return true;
}

static void plain_imm12(arm_emit_t *e, uint16_t base, int rd, int rn, uint32_t imm12)
{
    arm_e32(e, (uint16_t)(base | (imm12 >> 11 & 1u) << 10 | (unsigned)rn),
            (uint16_t)((imm12 >> 8 & 7u) << 12 | (unsigned)rd << 8 | (imm12 & 0xffu)));
}

void arm_addw(arm_emit_t *e, int rd, int rn, uint32_t imm12)
{
    plain_imm12(e, 0xf200u, rd, rn, imm12 & 0xfffu);
}

void arm_subw(arm_emit_t *e, int rd, int rn, uint32_t imm12)
{
    plain_imm12(e, 0xf2a0u, rd, rn, imm12 & 0xfffu);
}

static void mov16(arm_emit_t *e, uint16_t base, int rd, uint32_t imm16)
{
    arm_e32(e, (uint16_t)(base | (imm16 >> 11 & 1u) << 10 | (imm16 >> 12 & 0xfu)),
            (uint16_t)((imm16 >> 8 & 7u) << 12 | (unsigned)rd << 8 | (imm16 & 0xffu)));
}

void arm_movw(arm_emit_t *e, int rd, uint32_t imm16)
{
    mov16(e, 0xf240u, rd, imm16 & 0xffffu);
}

void arm_movt(arm_emit_t *e, int rd, uint32_t imm16)
{
    mov16(e, 0xf2c0u, rd, imm16 & 0xffffu);
}

void arm_mov32(arm_emit_t *e, int rd, uint32_t value)
{
    /* MOV.W (modified immediate, S=0) leaves the flags alone. */
    if (arm_dp_imm(e, DP_ORR, 0, rd, A_PC, value))
        return;
    arm_movw(e, rd, value & 0xffffu);
    if (value >> 16)
        arm_movt(e, rd, value >> 16);
}

void arm_mov(arm_emit_t *e, int rd, int rm)
{
    arm_dp_reg(e, DP_ORR, 0, rd, A_PC, rm, SH_LSL, 0);
}

void arm_lsl(arm_emit_t *e, int rd, int rm, int amount)
{
    arm_dp_reg(e, DP_ORR, 0, rd, A_PC, rm, SH_LSL, amount);
}

void arm_lsr(arm_emit_t *e, int rd, int rm, int amount)
{
    /* LSR #0 would encode LSR #32: a plain move (32-bit operations, s = 0) */
    arm_dp_reg(e, DP_ORR, 0, rd, A_PC, rm, amount ? SH_LSR : SH_LSL, amount);
}

void arm_lsr_reg(arm_emit_t *e, int rd, int rn, int rm)
{
    arm_e32(e, (uint16_t)(0xfa20u | (unsigned)rn), (uint16_t)(0xf000u | (unsigned)rd << 8 | (unsigned)rm));
}

void arm_bfi(arm_emit_t *e, int rd, int rn, int lsb, int width)
{
    const unsigned l = (unsigned)lsb, msb = (unsigned)(lsb + width - 1);
    arm_e32(e, (uint16_t)(0xf360u | (unsigned)rn),
            (uint16_t)((l >> 2) << 12 | (unsigned)rd << 8 | (l & 3u) << 6 | msb));
}

void arm_ubfx(arm_emit_t *e, int rd, int rn, int lsb, int width)
{
    const unsigned l = (unsigned)lsb;
    arm_e32(e, (uint16_t)(0xf3c0u | (unsigned)rn),
            (uint16_t)((l >> 2) << 12 | (unsigned)rd << 8 | (l & 3u) << 6 |
                       (unsigned)(width - 1)));
}

void arm_uxth(arm_emit_t *e, int rd, int rm)
{
    arm_e32(e, 0xfa1fu, (uint16_t)(0xf080u | (unsigned)rd << 8 | (unsigned)rm));
}

static const uint16_t ls_imm_base[] = {
    [LS_STR] = 0xf8c0u, [LS_STRB] = 0xf880u, [LS_STRH] = 0xf8a0u,
    [LS_LDR] = 0xf8d0u, [LS_LDRB] = 0xf890u, [LS_LDRH] = 0xf8b0u,
};

static const uint16_t ls_reg_base[] = {
    [LS_STR] = 0xf840u, [LS_STRB] = 0xf800u, [LS_STRH] = 0xf820u,
    [LS_LDR] = 0xf850u, [LS_LDRB] = 0xf810u, [LS_LDRH] = 0xf830u,
};

void arm_ls_imm(arm_emit_t *e, int kind, int rt, int rn, uint32_t imm12)
{
    arm_e32(e, (uint16_t)(ls_imm_base[kind] | (unsigned)rn),
            (uint16_t)((unsigned)rt << 12 | (imm12 & 0xfffu)));
}

void arm_ls_reg(arm_emit_t *e, int kind, int rt, int rn, int rm)
{
    arm_e32(e, (uint16_t)(ls_reg_base[kind] | (unsigned)rn),
            (uint16_t)((unsigned)rt << 12 | (unsigned)rm));
}

void arm_strd(arm_emit_t *e, int rt, int rt2, int rn, uint32_t imm)
{
    arm_e32(e, (uint16_t)(0xe9c0u | (unsigned)rn),
            (uint16_t)((unsigned)rt << 12 | (unsigned)rt2 << 8 | (imm >> 2 & 0xffu)));
}

void arm_push(arm_emit_t *e, uint16_t list)
{
    arm_e32(e, 0xe92du, list);   /* STMDB SP!, {list} */
}

void arm_pop(arm_emit_t *e, uint16_t list)
{
    arm_e32(e, 0xe8bdu, list);   /* LDMIA SP!, {list} */
}

void arm_bx(arm_emit_t *e, int rm)
{
    arm_e16(e, (uint16_t)(0x4700u | (unsigned)rm << 3));
}

void arm_blx(arm_emit_t *e, int rm)
{
    arm_e16(e, (uint16_t)(0x4780u | (unsigned)rm << 3));
}

uint32_t arm_branch_slot(arm_emit_t *e)
{
    const uint32_t at = e->pos;
    arm_e32(e, 0xbf00u, 0xbf00u); /* two NOPs until patched */
    return at;
}

bool arm_patch_branch(arm_emit_t *e, uint32_t at, uint32_t target, int cond)
{
    const int32_t off = (int32_t)target - (int32_t)(at + 4u);
    uint16_t hw1, hw2;
    if (at + 4u > e->cap)
        return false;
    if (cond == C_AL) {
        /* B.W, T4: S:I1:I2:imm10:imm11:0 */
        if (off < -(1 << 24) || off >= (1 << 24))
            return false;
        const uint32_t u = (uint32_t)off;
        const uint32_t s = u >> 24 & 1u, i1 = u >> 23 & 1u, i2 = u >> 22 & 1u;
        const uint32_t j1 = (~i1 ^ s) & 1u, j2 = (~i2 ^ s) & 1u;
        hw1 = (uint16_t)(0xf000u | s << 10 | (u >> 12 & 0x3ffu));
        hw2 = (uint16_t)(0x9000u | j1 << 13 | j2 << 11 | (u >> 1 & 0x7ffu));
    } else {
        /* B<c>.W, T3: S:J2:J1:imm6:imm11:0 */
        if (off < -(1 << 20) || off >= (1 << 20))
            return false;
        const uint32_t u = (uint32_t)off;
        const uint32_t s = u >> 20 & 1u, j2 = u >> 19 & 1u, j1 = u >> 18 & 1u;
        hw1 = (uint16_t)(0xf000u | s << 10 | (uint32_t)cond << 6 | (u >> 12 & 0x3fu));
        hw2 = (uint16_t)(0x8000u | j1 << 13 | j2 << 11 | (u >> 1 & 0x7ffu));
    }
    e->buf[at] = (uint8_t)hw1;
    e->buf[at + 1] = (uint8_t)(hw1 >> 8);
    e->buf[at + 2] = (uint8_t)hw2;
    e->buf[at + 3] = (uint8_t)(hw2 >> 8);
    return true;
}

/* BL target: the B.W (T4) encoding with bit 14 of the second halfword set. */
bool arm_bl(arm_emit_t *e, uint32_t target)
{
    const uint32_t at = arm_branch_slot(e);
    if (e->full || !arm_patch_branch(e, at, target, C_AL))
        return false;
    e->buf[at + 3] |= 0x40u;
    return true;
}
