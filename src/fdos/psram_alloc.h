#ifndef FDOS_PSRAM_ALLOC_H
#define FDOS_PSRAM_ALLOC_H

#include <stdint.h>

/* Native arenas use the same physical pool as XMS EMBs. Sizes and offsets
 * are in bytes and multiples of 1 KiB. Native arenas have no guest handle. */
typedef struct fdos_psram_span {
    uint32_t base;
    uint32_t size;
    uint16_t owner;
    uint8_t locks;
    uint8_t used;
} fdos_psram_span;

fdos_psram_span *fdos_native_span_next(uint16_t owner, fdos_psram_span *previous);
fdos_psram_span *fdos_native_span_reserve(uint16_t owner, uint32_t size);
uint32_t fdos_native_span_largest(void);
uint32_t fdos_native_span_capacity(const fdos_psram_span *span);
/* Callers grow within capacity; a size of zero releases the span. */
void fdos_native_span_set_size(fdos_psram_span *span, uint32_t size);
void fdos_native_span_release_owner(uint16_t owner);

#endif
