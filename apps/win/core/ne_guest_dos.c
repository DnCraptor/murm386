/*
 * ne_guest_dos.c - the memory of the programs' segments: extended memory
 * first, then DOS's (INT 21h, its upper memory blocks first). What must be
 * below 1M (the trampoline, the PSP, the descriptor table, GlobalDOSAlloc's)
 * is DOS's always.
 *
 * The extended memory is WINN's own heap's: a native program's heap is
 * the PSRAM above the HMA, which is the guest's extended memory itself (a
 * block of it at ARM address p is guest linear p - DOS_GUEST_RAM_BASE), and
 * XMS hands out the same memory, so the heap it is. SYSTEM.INI [memory]
 * ext= kilobytes the programs' segments may have of it at most (0: none);
 * without the key, as much as there is.
 *
 * The DOS allocator runs only while the shared x86 CPU is in real mode.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_guest.h"

#if defined(__arm__) || defined(__thumb__)
#include <stdlib.h>
#include <string.h>
#include "dos.h"
#include "dos_guest_ptr.h"
#include "wincore.h"

#define EXT_BLOCKS 1024

static uint32_t dos_used;           /* the bytes the programs' DOS blocks have */
static struct {
    uint32_t linear, size;          /* the segment's (16-aligned), 0: free */
    void *raw;                      /* malloc's */
} ext[EXT_BLOCKS];
static uint32_t ext_used, ext_limit;    /* the bytes they have; at most (0: none) */
static uint32_t ext_lo, ext_hi;         /* where they have been: from, to */
static int ext_started;

static uint16_t dos_ax(uint16_t ax, uint16_t bx, int *carry)
{
    union REGS in = { 0 }, out = { 0 };
    in.w.ax = ax;
    in.w.bx = bx;
    int386(0x21, &in, &out);
    if (carry)
        *carry = out.w.cflag != 0;
    return out.w.ax;
}

static void dos_free_seg(uint16_t seg)
{
    union REGS in = { 0 }, out = { 0 };
    struct SREGS sr;
    segread(&sr);
    sr.es = seg;
    in.w.ax = 0x4900;
    int386x(0x21, &in, &out, &sr);
}

/* ---------------------------------------------------------------- extended */

static void ext_start(void)
{
    if (ext_started)
        return;
    ext_started = 1;
    char limit[16];
    GetPrivateProfileString("memory", "ext", "", limit, sizeof(limit), "SYSTEM.INI");
    ext_limit = 0xffffffffu;
    if (limit[0]) {
        uint32_t kb = 0;
        for (const char *c = limit; *c >= '0' && *c <= '9'; c++)
            kb = kb * 10u + (uint32_t)(*c - '0');
        ext_limit = kb > 0x3fffffu ? 0xffffffffu : kb * 1024u;
    }
}

/* a block of the heap above 1M, 16-aligned; 0 if there is none */
static int ext_alloc(uint32_t bytes, uint32_t *linear)
{
    ext_start();
    bytes = (bytes + 15u) & ~15u;
    if (bytes > ext_limit - ext_used || ext_used > ext_limit)
        return 0;
    int i = 0;
    while (i < EXT_BLOCKS && ext[i].size)
        i++;
    if (i == EXT_BLOCKS)
        return 0;
    void *raw = malloc(bytes + 15u);
    if (!raw)
        return 0;
    uintptr_t p = ((uintptr_t)raw + 15u) & ~(uintptr_t)15u;
    if (p < DOS_GUEST_RAM_BASE + 0x110000u) {   /* DOS's (malloc's own fallback) */
        free(raw);
        return 0;
    }
    ext[i].linear = (uint32_t)(p - DOS_GUEST_RAM_BASE);
    ext[i].size = bytes;
    ext[i].raw = raw;
    ext_used += bytes;
    if (!ext_hi || ext[i].linear < ext_lo)
        ext_lo = ext[i].linear;
    if (ext[i].linear + bytes > ext_hi)
        ext_hi = ext[i].linear + bytes;
    *linear = ext[i].linear;
    return 1;
}

static int ext_find(uint32_t linear)
{
    if (linear < 0x110000u)
        return -1;
    for (int i = 0; i < EXT_BLOCKS; i++)
        if (ext[i].size && ext[i].linear == linear)
            return i;
    return -1;
}

void win_ne_guest_mem_end(void)
{
    for (int i = 0; i < EXT_BLOCKS; i++)
        if (ext[i].size) {
            free(ext[i].raw);
            ext[i].size = 0;
        }
    ext_used = 0;
}

/* ---------------------------------------------------------------- DOS */

/* INT 21h 48h, the upper memory blocks too (DOS's UMBs, when it has them,
 * linked for it and tried first), DOS's strategy and link as they were
 * after */
static int dos_alloc(uint32_t bytes, uint32_t *linear)
{
    if (!bytes || bytes > 65536u)
        return 0;
    int no_strategy, no_link, cf;
    uint16_t strategy = dos_ax(0x5800, 0, &no_strategy);
    uint16_t link = dos_ax(0x5802, 0, &no_link);
    if (!no_strategy && !no_link) {
        dos_ax(0x5803, 1, NULL);                    /* UMBs linked */
        dos_ax(0x5801, 0x80, NULL);                 /* high first, then low */
    }
    uint16_t seg = dos_ax(0x4800, (uint16_t)((bytes + 15u) >> 4), &cf);
    if (!no_strategy && !no_link) {
        dos_ax(0x5801, strategy, NULL);
        dos_ax(0x5803, (uint16_t)(link & 0xffu), NULL);
    }
    if (cf || !seg)
        return 0;
    *linear = (uint32_t)seg << 4;
    dos_used += (bytes + 15u) & ~15u;
    return 1;
}

/* a DOS block's size, its MCB's (the paragraph before it) */
static uint32_t mcb_bytes(uint32_t linear)
{
    const uint8_t *m = (const uint8_t *)dos_guest_linear_ptr(linear - 16u);
    return ((uint32_t)m[3] | (uint32_t)m[4] << 8) << 4;
}

void win_ne_guest_mem_info(win_ne_mem_info *m)
{
    memset(m, 0, sizeof(*m));
    m->dos_used = dos_used;
    m->ext_used = ext_used;
    m->ext_size = ext_limit == 0xffffffffu ? (uint32_t)malloc_largest_block() + ext_used : ext_limit;
    int no_strategy, no_link, cf;
    uint16_t strategy = dos_ax(0x5800, 0, &no_strategy);
    uint16_t link = dos_ax(0x5802, 0, &no_link);
    union REGS in = { 0 }, out = { 0 };
    in.w.ax = 0x4800;
    in.w.bx = 0xffff;
    if (!no_link)
        dos_ax(0x5803, 0, NULL);
    int386(0x21, &in, &out);
    m->dos_free = (uint32_t)out.w.bx << 4;
    m->dos_free_umb = m->dos_free;
    if (!no_strategy && !no_link) {
        dos_ax(0x5803, 1, NULL);
        dos_ax(0x5801, 0x80, NULL);
        int386(0x21, &in, &out);
        m->dos_free_umb = (uint32_t)out.w.bx << 4;
        dos_ax(0x5801, strategy, NULL);
    }
    if (!no_link)
        dos_ax(0x5803, (uint16_t)(link & 0xffu), &cf);
}

/* ---------------------------------------------------------------- the programs' */

static int ne_alloc(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
    if (!bytes || bytes > 65536u)
        return 0;
    return ext_alloc(bytes, linear) || dos_alloc(bytes, linear);
}

static int ne_alloc_low(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
    return dos_alloc(bytes, linear);
}

static void ne_free(void *opaque, uint32_t linear)
{
    (void)opaque;
    int i = ext_find(linear);
    if (i >= 0) {
        free(ext[i].raw);
        ext_used -= ext[i].size;
        ext[i].size = 0;
        return;
    }
    uint32_t n = mcb_bytes(linear);
    dos_used = n < dos_used ? dos_used - n : 0;
    dos_free_seg((uint16_t)(linear >> 4));
}

/* where it is: an extended one within its rounding, or INT 21h 4Ah */
static int ne_resize(void *opaque, uint32_t linear, uint32_t bytes)
{
    (void)opaque;
    if (!bytes || bytes > 65536u)
        return 0;
    int i = ext_find(linear);
    if (i >= 0) {                       /* smaller, or within its 16-rounding */
        if (((bytes + 15u) & ~15u) > ext[i].size)
            return 0;
        return 1;
    }
    union REGS in = { 0 }, out = { 0 };
    struct SREGS sr;
    segread(&sr);
    sr.es = (uint16_t)(linear >> 4);
    in.w.ax = 0x4a00;
    in.w.bx = (uint16_t)((bytes + 15u) >> 4);
    uint32_t was = mcb_bytes(linear);
    int386x(0x21, &in, &out, &sr);
    if (out.w.cflag)
        return 0;
    dos_used = dos_used - (was < dos_used ? was : dos_used) + ((bytes + 15u) & ~15u);
    return 1;
}

int win_ne_guest_mapped(uint32_t linear, uint32_t bytes)
{
    if (linear < 0x100000u && bytes <= 0x100000u - linear)
        return 1;
    /* where the programs' extended blocks are (the selectors' limits keep
     * each read and write inside its segment) */
    return ext_hi && linear >= ext_lo && linear <= ext_hi && bytes <= ext_hi - linear;
}

static int ne_write(void *opaque, uint32_t linear, const void *data, uint32_t bytes)
{
    (void)opaque;
    if (!data || !win_ne_guest_mapped(linear, bytes))
        return 0;
    memcpy(dos_guest_linear_ptr(linear), data, bytes);
    return 1;
}

const win_ne_guest_io *win_ne_guest_dos_io(void)
{
    static const win_ne_guest_io io = {ne_alloc, ne_free, ne_write, ne_resize, ne_alloc_low};
    return &io;
}
#endif
