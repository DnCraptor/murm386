#ifndef FDOS_PSRAM_LAYOUT_H
#define FDOS_PSRAM_LAYOUT_H

#include <stddef.h>

/* Physical guest-memory offset immediately above the HMA.  Both XMS EMBs and
 * the native application PSRAM heap start here in direct-QSPI builds. */
#define FDOS_XMS_EMB_BASE_PHYS          0x00110000ul

/* Fixed top-of-QSPI native stack reserve.  The reclaimable FatFs L2 cache must
 * stay below this arena even while no native application is running. */
#define ARM_ELF_NATIVE_STACK_ARENA_SIZE (256u * 1024u)

/* Bottom of the native stack arena, outside the XMS and FatFs pools. */
#define FDOS_NATIVE_SPANS 64u
#define FDOS_NATIVE_SPAN_TABLE_SIZE 768u

/* The guest RAM limit excludes any Lo-tech EMS backing store. */
static inline size_t fdos_native_stack_top(size_t usable, size_t guest)
{
    return usable < guest ? usable : guest;
}

#endif
