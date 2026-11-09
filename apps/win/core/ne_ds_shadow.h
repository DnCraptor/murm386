/* Sparse path-local symbolic DATA bytes for bounded WINN NE preflight.
 * This is NOT a snapshot of the guest DATA segment. Only guest writes
 * already proven to address DS are recorded. All initial bytes are unknown.
 * No saved DS selector can be forged by loading these numeric bytes.
 */
#ifndef WINN_NE_DS_SHADOW_H
#define WINN_NE_DS_SHADOW_H
#include <stdint.h>

#define WIN_NE_DS_SHADOW_BYTES 24u

typedef struct {
    uint16_t address;
    uint8_t value;
    uint8_t known;
} win_ne_ds_byte;

typedef struct {
    unsigned used;
    win_ne_ds_byte bytes[WIN_NE_DS_SHADOW_BYTES];
} win_ne_ds_shadow;

static inline void win_ne_ds_clear(win_ne_ds_shadow *s)
{
    s->used = 0;
}

static inline void win_ne_ds_put8(win_ne_ds_shadow *s, uint16_t addr,
                                  int known, unsigned value)
{
    unsigned i;
    for (i = 0; i < s->used; ++i)
        if (s->bytes[i].address == addr) break;
    if (i == s->used) {
        if (s->used == WIN_NE_DS_SHADOW_BYTES)
            win_ne_ds_clear(s); /* Evict knowledge, never invent it. */
        i = s->used++;
    }
    s->bytes[i].address = addr;
    s->bytes[i].known = (uint8_t)(known != 0);
    s->bytes[i].value = (uint8_t)value;
}

static inline void win_ne_ds_put16(win_ne_ds_shadow *s, uint16_t addr,
                                   int known, unsigned value)
{
    /* Caller has verified full 16-bit DS range; cannot wrap at FFFF. */
    win_ne_ds_put8(s, addr, known, value);
    win_ne_ds_put8(s, (uint16_t)(addr + 1u), known, value >> 8);
}

static inline int win_ne_ds_get8(const win_ne_ds_shadow *s, uint16_t addr,
                                 unsigned *value)
{
    for (unsigned i = 0; i < s->used; ++i)
        if (s->bytes[i].address == addr) {
            if (!s->bytes[i].known) return 0;
            *value = s->bytes[i].value;
            return 1;
        }
    return 0;
}

static inline int win_ne_ds_get16(const win_ne_ds_shadow *s, uint16_t addr,
                                  unsigned *value)
{
    unsigned lo, hi;
    if (addr == UINT16_MAX || !win_ne_ds_get8(s, addr, &lo) ||
        !win_ne_ds_get8(s, (uint16_t)(addr + 1u), &hi)) return 0;
    *value = lo | (hi << 8);
    return 1;
}
#endif
