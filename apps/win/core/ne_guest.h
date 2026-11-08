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
#define WIN_NE_GUEST_MAX_THUNKS 128u
#define WIN_NE_THUNK_BYTES 16u /* includes FAR return + Pascal stack adjustment */
/* Upper half of thunk segment is reserved for the PM trampoline. */
#define WIN_NE_GUEST_ACTIVE_THUNKS 64u

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
    uint16_t gdt_limit, thunk_selector;
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

/* Returns the DOS INT 21h-backed guest allocator on the RP2350 target.
 * Allocation/free must run before entering protected mode or after leaving it. */
#if defined(__arm__) || defined(__thumb__)
const win_ne_guest_io *win_ne_guest_dos_io(void);
#endif

#endif /* WINN_NE_GUEST_H */
