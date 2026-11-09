/* Symbolic BP-local storage for the bounded Win16 NE preflight.
 * Each bit represents a physically reserved two-byte SS stack slot.
 * Saved registers and pending PUSH arguments NEVER allocate local slots.
 */
#ifndef WINN_NE_FRAME_LOCALS_H
#define WINN_NE_FRAME_LOCALS_H
#include <stdint.h>

#define WIN_NE_LOCAL_SLOTS 10u  /* ENTRY_STACK_BUDGET / 2 */
enum { WIN_NE_LOCAL_WORD = 1, WIN_NE_LOCAL_DS = 2 };

typedef struct {
    uint16_t reserved;
    uint16_t known;
    uint8_t tag[WIN_NE_LOCAL_SLOTS];
    uint16_t value[WIN_NE_LOCAL_SLOTS];
} win_ne_frame_locals;

/* depth is SP's distance below BP BEFORE SUB/ADD. Both are even. */
static inline int win_ne_local_reserve(win_ne_frame_locals *f,
                                       unsigned depth, unsigned bytes)
{
    if ((depth | bytes) & 1u || depth + bytes > WIN_NE_LOCAL_SLOTS * 2u)
        return 0;
    uint16_t bits = 0;
    for (unsigned off = depth + 2u; off <= depth + bytes; off += 2u)
        bits |= (uint16_t)(1u << (off / 2u - 1u));
    if (f->reserved & bits) return 0;
    f->reserved |= bits;
    f->known &= (uint16_t)~bits;
    for (unsigned i = 0; i < WIN_NE_LOCAL_SLOTS; i++)
        if (bits & (1u << i)) {
            f->tag[i] = WIN_NE_LOCAL_WORD;
            f->value[i] = 0;
        }
    return 1;
}

static inline int win_ne_local_release(win_ne_frame_locals *f,
                                       unsigned depth, unsigned bytes)
{
    if ((depth | bytes) & 1u || bytes > depth ||
        depth > WIN_NE_LOCAL_SLOTS * 2u) return 0;
    uint16_t bits = 0;
    for (unsigned off = depth; off > depth - bytes; off -= 2u)
        bits |= (uint16_t)(1u << (off / 2u - 1u));
    if ((f->reserved & bits) != bits) return 0;
    f->reserved &= (uint16_t)~bits;
    f->known &= (uint16_t)~bits;
    return 1;
}

/* off=2 is the low byte of [BP-2], off=1 is its high byte.
 * Word accesses may be unaligned, but must occupy allocated slots only. */
static inline int win_ne_local_access(const win_ne_frame_locals *f,
                                      unsigned off, unsigned width)
{
    if (!off || (width != 1u && width != 2u) || off < width ||
        off > WIN_NE_LOCAL_SLOTS * 2u) return 0;
    for (unsigned n = 0; n < width; n++) {
        unsigned slot = (off - n - 1u) / 2u;
        if (!(f->reserved & (1u << slot))) return 0;
    }
    return 1;
}

static inline void win_ne_local_store(win_ne_frame_locals *f,
                                      unsigned off, unsigned width,
                                      unsigned tag, int known, uint16_t value)
{
    unsigned slot = (off - 1u) / 2u;
    uint16_t bit = (uint16_t)(1u << slot);
    if (width == 2u && !(off & 1u)) {
        f->tag[slot] = (uint8_t)tag;
        f->value[slot] = value;
        if (known) f->known |= bit;
        else f->known &= (uint16_t)~bit;
    } else if (width == 1u) {
        /* Any byte write destroys FAR-selector provenance of that word. */
        f->tag[slot] = WIN_NE_LOCAL_WORD;
        if (known && (f->known & bit)) {
            unsigned shift = (off & 1u) ? 8u : 0u;
            f->value[slot] = (uint16_t)((f->value[slot] &
                (uint16_t)~(0xffu << shift)) | ((value & 0xffu) << shift));
        } else f->known &= (uint16_t)~bit;
    } else {
        /* Unaligned word write touches two slots: conservatively taint both. */
        unsigned second = (off - 2u) / 2u;
        f->known &= (uint16_t)~(bit | (uint16_t)(1u << second));
        f->tag[slot] = f->tag[second] = WIN_NE_LOCAL_WORD;
    }
}

static inline void win_ne_local_load(const win_ne_frame_locals *f,
                                     unsigned off, unsigned width,
                                     unsigned *tag, int *known, uint16_t *value)
{
    unsigned slot = (off - 1u) / 2u;
    *tag = WIN_NE_LOCAL_WORD;
    *known = 0;
    *value = 0;
    if (width == 2u && !(off & 1u)) {
        *tag = f->tag[slot];
        *known = (f->known & (1u << slot)) != 0;
        *value = f->value[slot];
    } else if (width == 1u && (f->known & (1u << slot))) {
        unsigned shift = (off & 1u) ? 8u : 0u;
        *value = (uint16_t)((f->value[slot] >> shift) & 0xffu);
        *known = 1;
    }
}
#endif
