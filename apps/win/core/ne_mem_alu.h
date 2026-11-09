/* Exact 8/16-bit ALU values and FLAGS for checked Win16 memory operands.
 * No memory access or modification of the emulator's CPU state occurs here. */
#ifndef WINN_NE_MEM_ALU_H
#define WINN_NE_MEM_ALU_H
#include "ne_cmp_flow.h"

static inline unsigned win_ne_mem_value(unsigned group, unsigned width,
                                        unsigned lhs, unsigned rhs)
{
    const unsigned mask = width == 8u ? 0xffu : 0xffffu;
    lhs &= mask;
    rhs &= mask;
    switch (group) {
    case 0: return (lhs + rhs) & mask;           /* ADD */
    case 1: return lhs | rhs;                    /* OR */
    case 4: return lhs & rhs;                    /* AND */
    case 5: return (lhs - rhs) & mask;           /* SUB */
    case 6: return lhs ^ rhs;                    /* XOR */
    default: return lhs;                        /* CMP: no write */
    }
}

static inline win_ne_flag_proof win_ne_mem_flags(unsigned group,
        unsigned width, unsigned lhs, unsigned rhs, size_t next)
{
    const unsigned mask = width == 8u ? 0xffu : 0xffffu;
    const unsigned sign = width == 8u ? 0x80u : 0x8000u;
    lhs &= mask;
    rhs &= mask;
    if (group == 5u || group == 7u)
        return win_ne_cmp(lhs, rhs, width, next);
    if (group == 0u) {
        unsigned result = (lhs + rhs) & mask;
        win_ne_flag_proof f = {next, 0u, WIN_NE_ARITH_FLAGS};
        if (lhs + rhs > mask) f.bits |= WIN_NE_CF;
        if (!(result & mask)) f.bits |= WIN_NE_ZF;
        if (result & sign) f.bits |= WIN_NE_SF;
        if ((~(lhs ^ rhs) & (lhs ^ result) & sign)) f.bits |= WIN_NE_OF;
        f.bits |= win_ne_parity(result);
        return f;
    }
    const unsigned result = win_ne_mem_value(group, width, lhs, rhs);
    return win_ne_test(result, result, width, next);
}

/* INC/DEC preserve the incoming CF; it must never be invented. */
static inline win_ne_flag_proof win_ne_mem_incdec(unsigned before,
        int delta, unsigned width, size_t next)
{
    const unsigned mask = width == 8u ? 0xffu : 0xffffu;
    const unsigned sign = width == 8u ? 0x80u : 0x8000u;
    before &= mask;
    unsigned result = (before + (unsigned)delta) & mask;
    win_ne_flag_proof f = {next, 0u, WIN_NE_ARITH_FLAGS & ~WIN_NE_CF};
    if (!result) f.bits |= WIN_NE_ZF;
    if (result & sign) f.bits |= WIN_NE_SF;
    if ((delta > 0 && before == sign - 1u) ||
        (delta < 0 && before == sign)) f.bits |= WIN_NE_OF;
    f.bits |= win_ne_parity(result);
    return f;
}
#endif
