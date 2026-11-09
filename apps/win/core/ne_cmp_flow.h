/* Exact 8086/386 arithmetic flags for bounded Win16 back-edges.
 * Internal to the NE preflight; no emulator CPU state is modified.
 */
#ifndef WINN_NE_CMP_FLOW_H
#define WINN_NE_CMP_FLOW_H
#include <stddef.h>
#include <stdint.h>

enum {
    WIN_NE_CF = 0x0001u, WIN_NE_PF = 0x0004u,
    WIN_NE_ZF = 0x0040u, WIN_NE_SF = 0x0080u,
    WIN_NE_OF = 0x0800u,
    WIN_NE_ARITH_FLAGS = WIN_NE_CF | WIN_NE_PF | WIN_NE_ZF |
                         WIN_NE_SF | WIN_NE_OF
};
typedef struct {
    size_t next_ip;       /* proof is valid only at the immediately next IP */
    uint16_t bits;        /* computed x86 FLAGS, subset above */
    uint16_t mask;        /* flags independently proven by this instruction */
} win_ne_flag_proof;

static inline uint16_t win_ne_parity(unsigned result)
{
    unsigned p = 1u; /* x86 PF=1 for even number of set low-byte bits */
    result &= 255u;
    while (result) { p ^= result & 1u; result >>= 1u; }
    return p ? WIN_NE_PF : 0u;
}
static inline win_ne_flag_proof win_ne_cmp(unsigned lhs, unsigned rhs,
                                           unsigned width, size_t next)
{
    win_ne_flag_proof f = {next, 0u, WIN_NE_ARITH_FLAGS};
    unsigned mask = width == 8u ? 0xffu : 0xffffu;
    unsigned sign = width == 8u ? 0x80u : 0x8000u;
    lhs &= mask; rhs &= mask;
    unsigned result = (lhs - rhs) & mask;
    if (lhs < rhs) f.bits |= WIN_NE_CF;
    if (!result) f.bits |= WIN_NE_ZF;
    if (result & sign) f.bits |= WIN_NE_SF;
    if (((lhs ^ rhs) & (lhs ^ result) & sign)) f.bits |= WIN_NE_OF;
    f.bits |= win_ne_parity(result);
    return f;
}
static inline win_ne_flag_proof win_ne_test(unsigned lhs, unsigned rhs,
                                            unsigned width, size_t next)
{
    win_ne_flag_proof f = {next, 0u, WIN_NE_ARITH_FLAGS};
    unsigned sign = width == 8u ? 0x80u : 0x8000u;
    unsigned mask = width == 8u ? 0xffu : 0xffffu;
    unsigned result = (lhs & rhs) & mask;
    if (!result) f.bits |= WIN_NE_ZF;
    if (result & sign) f.bits |= WIN_NE_SF;
    f.bits |= win_ne_parity(result);
    return f;
}
static inline win_ne_flag_proof win_ne_incdec(unsigned before, int delta,
                                              size_t next)
{
    unsigned result = (before + (unsigned)delta) & 0xffffu;
    win_ne_flag_proof f = {next, 0u, WIN_NE_ARITH_FLAGS & ~WIN_NE_CF};
    if (!result) f.bits |= WIN_NE_ZF;
    if (result & 0x8000u) f.bits |= WIN_NE_SF;
    if ((delta > 0 && before == 0x7fffu) ||
        (delta < 0 && before == 0x8000u)) f.bits |= WIN_NE_OF;
    f.bits |= win_ne_parity(result);
    return f;
}

/* Returns 0/1 for a proven condition, -1 when an input flag is unknown.
 * cond is the low nibble of either 70..7F or 0F 80..8F. */
static inline int win_ne_jcc(const win_ne_flag_proof *f,
                              unsigned cond, size_t ip)
{
    unsigned needs;
    switch (cond & 15u) {
    case 0: case 1: needs = WIN_NE_OF; break;
    case 2: case 3: needs = WIN_NE_CF; break;
    case 4: case 5: needs = WIN_NE_ZF; break;
    case 6: case 7: needs = WIN_NE_CF | WIN_NE_ZF; break;
    case 8: case 9: needs = WIN_NE_SF; break;
    case 10: case 11: needs = WIN_NE_PF; break;
    case 12: case 13: needs = WIN_NE_SF | WIN_NE_OF; break;
    default: needs = WIN_NE_SF | WIN_NE_OF | WIN_NE_ZF; break;
    }
    if (f->next_ip != ip || (f->mask & needs) != needs) return -1;
    int cf = !!(f->bits & WIN_NE_CF), zf = !!(f->bits & WIN_NE_ZF);
    int sf = !!(f->bits & WIN_NE_SF), of = !!(f->bits & WIN_NE_OF);
    int pf = !!(f->bits & WIN_NE_PF);
    int taken;
    switch (cond & 15u) {
    case 0: taken = of; break;
    case 2: taken = cf; break;
    case 4: taken = zf; break;
    case 6: taken = cf || zf; break;
    case 8: taken = sf; break;
    case 10: taken = pf; break;
    case 12: taken = sf != of; break;
    case 14: taken = zf || sf != of; break;
    default:
        /* The odd opcode is the complement of its preceding even opcode. */
        return 1 - win_ne_jcc(f, cond - 1u, ip);
    }
    return taken;
}
#endif
