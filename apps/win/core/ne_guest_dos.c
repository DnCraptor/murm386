/*
 * ne_guest_dos.c - back NE guest allocations with native DOS INT 21h memory.
 *
 * The DOS allocator runs only while the shared x86 CPU is in real mode.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_guest.h"

#if defined(__arm__) || defined(__thumb__)
#include <string.h>
#include "dos.h"
#include "dos_guest_ptr.h"

uint32_t win_ne_dos_used;     /* the bytes the programs' blocks have */

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

/* INT 21h 48h, the upper memory blocks too (DOS's UMBs, when it has them,
 * linked for it and tried first: the programs' segments are most of what
 * WINN asks DOS for), DOS's strategy and link as they were after */
static int ne_dos_alloc(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
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
    win_ne_dos_used += (bytes + 15u) & ~15u;
    return 1;
}

/* a block's size, its MCB's (the paragraph before it) */
static uint32_t mcb_bytes(uint32_t linear)
{
    const uint8_t *m = (const uint8_t *)dos_guest_linear_ptr(linear - 16u);
    return ((uint32_t)m[3] | (uint32_t)m[4] << 8) << 4;
}

/* the largest block DOS has free: in conventional memory, and with the UMBs */
void win_ne_dos_free(uint32_t *low, uint32_t *any)
{
    int no_strategy, no_link, cf;
    uint16_t strategy = dos_ax(0x5800, 0, &no_strategy);
    uint16_t link = dos_ax(0x5802, 0, &no_link);
    union REGS in = { 0 }, out = { 0 };
    in.w.ax = 0x4800;
    in.w.bx = 0xffff;
    if (!no_link)
        dos_ax(0x5803, 0, NULL);
    int386(0x21, &in, &out);
    *low = (uint32_t)out.w.bx << 4;
    *any = *low;
    if (!no_strategy && !no_link) {
        dos_ax(0x5803, 1, NULL);
        dos_ax(0x5801, 0x80, NULL);
        int386(0x21, &in, &out);
        *any = (uint32_t)out.w.bx << 4;
        dos_ax(0x5801, strategy, NULL);
    }
    if (!no_link)
        dos_ax(0x5803, (uint16_t)(link & 0xffu), &cf);
}

static void ne_dos_free(void *opaque, uint32_t linear)
{
    (void)opaque;
    uint32_t n = mcb_bytes(linear);
    win_ne_dos_used = n < win_ne_dos_used ? win_ne_dos_used - n : 0;
    union REGS in = { 0 }, out = { 0 };
    struct SREGS sr;
    segread(&sr);
    sr.es = (uint16_t)(linear >> 4);
    in.w.ax = 0x4900;
    int386x(0x21, &in, &out, &sr);
}

/* INT 21h 4Ah: the block at linear bytes long where it is */
static int ne_dos_resize(void *opaque, uint32_t linear, uint32_t bytes)
{
    (void)opaque;
    if (!bytes || bytes > 65536u)
        return 0;
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
    win_ne_dos_used = win_ne_dos_used - (was < win_ne_dos_used ? was : win_ne_dos_used) +
                      ((bytes + 15u) & ~15u);
    return 1;
}

static int ne_dos_write(void *opaque, uint32_t linear, const void *data,
                        uint32_t bytes)
{
    (void)opaque;
    if (!data || linear >= 0x100000u || bytes > 0x100000u - linear)
        return 0;
    memcpy(dos_guest_linear_ptr(linear), data, bytes);
    return 1;
}

const win_ne_guest_io *win_ne_guest_dos_io(void)
{
    static const win_ne_guest_io io = {ne_dos_alloc, ne_dos_free, ne_dos_write, ne_dos_resize};
    return &io;
}
#endif
