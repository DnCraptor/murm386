#ifndef FDOS_XLAT_WRITE_H
#define FDOS_XLAT_WRITE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XLAT_CODE_BLOCK_SHIFT 6u
#define XLAT_CODE_LIMIT 0x110000u
#define XLAT_CODE_MAP_BYTES (XLAT_CODE_LIMIT >> (XLAT_CODE_BLOCK_SHIFT + 3u))
extern uint8_t xlat_code_map[XLAT_CODE_MAP_BYTES];
extern uint32_t xlat_write_lo, xlat_write_hi;

/* Record a synchronous physical write at a known code-map hit. */
void xlat_record_write(uint32_t linear, uint32_t len);
/* Arbitrary-length write; clipped to the translatable address space. */
void xlat_note_host_write(uint32_t linear, uint32_t len);

static inline void xlat_note_write_n(uint32_t linear, uint32_t n)
{
    if (!n || linear >= XLAT_CODE_LIMIT)
        return;
    if (n > 16u) {
        xlat_note_host_write(linear, n);
        return;
    }
    if (xlat_code_map[linear >> (XLAT_CODE_BLOCK_SHIFT + 3)] &
        (1u << ((linear >> XLAT_CODE_BLOCK_SHIFT) & 7u))) {
        if (n > XLAT_CODE_LIMIT - linear)
            n = XLAT_CODE_LIMIT - linear;
        xlat_record_write(linear, n);
    }
}

static inline void xlat_note_write(uint32_t linear)
{
    xlat_note_write_n(linear, 16u);
}

#ifdef __cplusplus
}
#endif
#endif
