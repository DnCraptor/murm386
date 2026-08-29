#pragma once
/*
 * EMS (Lo-tech 2MB EMS board) shared access helpers.
 *
 * ems.c.inl is #included directly into i386.c so its symbols are local to
 * that translation unit.  This header re-exposes the same constants and a
 * small set of inline helpers for use in other TUs (disk handler, i8257 DMA).
 *
 * Include this header wherever you need to read/write guest memory that may
 * fall in the EMS window and you cannot go through the normal pload/pstore
 * path (e.g. bulk memcpy in BIOS disk callbacks or DMA engine).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#if EMULATE_LTEMS

#ifndef EMS_START
#define EMS_START  (0xD0000ul)
#endif
#ifndef EMS_END
#define EMS_END    (0xE0000ul)
#endif

extern uint8_t ems_pages[4];
extern uint32_t ems_backing_linear_base;

typedef uint8_t  (*ems_read8_fn)(uint32_t addr);
typedef uint16_t (*ems_read16_fn)(uint32_t addr);
typedef uint32_t (*ems_read32_fn)(uint32_t addr);
typedef void (*ems_write8_fn)(uint32_t addr, uint8_t value);
typedef void (*ems_write16_fn)(uint32_t addr, uint16_t value);
typedef void (*ems_write32_fn)(uint32_t addr, uint32_t value);
typedef uint8_t *(*ems_span_ptr_fn)(uint32_t addr, uint32_t *span,
                                    bool write_access);

extern ems_read8_fn ems_mem_read8;
extern ems_read16_fn ems_mem_read16;
extern ems_read32_fn ems_mem_read32;
extern ems_write8_fn ems_mem_write8;
extern ems_write16_fn ems_mem_write16;
extern ems_write32_fn ems_mem_write32;
extern ems_span_ptr_fn ems_mem_span_ptr;

void ems_select_direct_backend(uint32_t linear_base);
#if !defined(NO_PAGING)
void ems_select_paged_backend(uint32_t linear_base);
#endif

#define EMS_WINDOW(addr) ((addr - EMS_START) < (EMS_END - EMS_START))

#endif /* EMULATE_LTEMS */
