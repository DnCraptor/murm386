/*
 * ne_guest.h - Native WINN's guest-memory NE staging and 16-bit descriptors.
 *
 * A staging operation runs entirely in DOS real mode. It allocates DOS-owned
 * memory, writes a GDT and relocated NE segments, and emits FAR-callable x86
 * thunks. It does NOT load GDTR, enable PE or start a Win16 task.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_NE_GUEST_H
#define WINN_NE_GUEST_H

#include <stdint.h>
#include "ne.h"

#define WIN_NE_GUEST_MAX_SEGMENTS 128u
#define WIN_NE_THUNK_BYTES 16u /* includes FAR return + Pascal stack adjustment */
/* the first half of the thunk segment (ne_boot.h): the API thunks */
#define WIN_NE_GUEST_ACTIVE_THUNKS 128u
/* The program's PSP (its own selector, ES after InitTask): the command line
 * at 81h, as DOS has it, but ending with a NUL (Windows). */
#define WIN_NE_GUEST_PSP_BYTES 256u
/* GDT entries for the program's global memory blocks (GlobalAlloc) */
#define WIN_NE_GUEST_GLOBALS 64u
/* Service numbers beyond KERNEL's (its ordinals), USER's (0x1000 + n) and
 * GDI's (0x3000 + n): KEYBOARD's, and KERNEL.102 DOS3Call, whose thunk is
 * an INT 21h of its own (the registers are its arguments) */
#define WIN_NE_SERVICE_KEYBOARD 0x4000u
#define WIN_NE_SERVICE_DOS3CALL 102u

/* Guest linear allocations must be addressable via an x86 segment base and
 * remain allocated until the entire staged image is released. The callbacks
 * must not switch the shared CPU to protected mode. */
typedef struct {
    int (*alloc)(void *opaque, uint32_t bytes, uint32_t *linear);
    void (*free)(void *opaque, uint32_t linear);
    int (*write)(void *opaque, uint32_t linear, const void *bytes, uint32_t size);
} win_ne_guest_io;

typedef struct {
    uint32_t linear;
    uint32_t size;
    uint16_t selector;
    uint16_t flags;
} win_ne_guest_segment;

typedef struct {
    win_ne_info info;
    win_ne_guest_io io;
    void *opaque;
    win_ne_guest_segment *segments; /* 1-based; 0 unused */
    uint32_t gdt_linear, thunk_linear;
    uint16_t gdt_limit;
    /* the thunk segment as ring 0 code (WINN's) and as ring 3 code (the
     * thunks and stubs the program calls), the call gates to ring 0 (its
     * API calls, its entry's and its procedures' return), the TSS, the
     * ring 0 area's data */
    uint16_t thunk_selector, thunk3_selector;
    uint16_t api_gate, exit_gate, cbret_gate, tss_selector, ring0_selector;
    win_ne_guest_segment psp;       /* a data segment of its own */
    /* its global memory blocks, a selector each (0: a free entry) */
    win_ne_guest_segment globals[WIN_NE_GUEST_GLOBALS];
    uint16_t cs, ip, ss, sp;
    uint16_t thunk_count;
    /* Each active slot holds a Win16 service with a known Pascal ABI. */
    uint16_t thunk_services[WIN_NE_GUEST_ACTIVE_THUNKS];
    /* Owned host-side descriptor image; copied into guest RAM on success. */
    uint8_t *gdt;
} win_ne_guest_image;

/* Complete staging is atomic with respect to allocations (all allocated
 * guest blocks are freed on error). Only services with implemented Win16
 * argument marshalling are resolved. Unsupported imports return WIN_NE_UNRESOLVED;
 * no ARM code address is exposed to guest execution. */
int win_ne_guest_load(const char *path, win_ne_guest_image *out,
                      const win_ne_guest_io *io, void *opaque);
void win_ne_guest_release(win_ne_guest_image *image);

/* The first import a program needs that WINN does not have (the load
 * fails with WIN_NE_UNRESOLVED): 1 with it in u, 0 if there is none. */
typedef struct {
    char module[16];
    char name[40];
    uint16_t ordinal;               /* 0: by name */
} win_ne_unresolved;
int win_ne_guest_unresolved(const char *path, win_ne_unresolved *u);

/* A global memory block of the program (DOS memory, a data selector of its
 * own, zeroed): its selector, 0 if there is no memory or no entry left.
 * Resized (moved: same selector), freed (all of them go with the image). */
uint16_t win_ne_guest_global_alloc(win_ne_guest_image *g, uint32_t bytes);
int win_ne_guest_global_realloc(win_ne_guest_image *g, uint16_t sel, uint32_t bytes,
                                int (*read)(void *, uint32_t, void *, uint32_t));
int win_ne_guest_global_free(win_ne_guest_image *g, uint16_t sel);
/* A data segment of the program (index) made bigger, moved as GlobalReAlloc
 * moves a block: DGROUP when its local heap grows. */
int win_ne_guest_segment_grow(win_ne_guest_image *g, uint16_t index, uint32_t bytes,
                              int (*read)(void *, uint32_t, void *, uint32_t));
win_ne_guest_segment *win_ne_guest_global(win_ne_guest_image *g, uint16_t sel);

/* The marshalling of a generic function (core/ne_sigs.h) by its service
 * number, NULL if it is not one: "result:arguments" */
const char *win_ne_generic_sig(uint16_t service);

/* Returns the DOS INT 21h-backed guest allocator on the RP2350 target.
 * Allocation/free must run before entering protected mode or after leaving it. */
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
const win_ne_guest_io *win_ne_guest_dos_io(void);
#endif

#endif /* WINN_NE_GUEST_H */
