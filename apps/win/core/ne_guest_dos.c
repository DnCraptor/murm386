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

static int ne_dos_alloc(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
    if (!bytes || bytes > 65536u)
        return 0;
    union REGS in = { 0 }, out = { 0 };
    in.w.ax = 0x4800;
    in.w.bx = (uint16_t)((bytes + 15u) >> 4);
    int386(0x21, &in, &out);
    if (out.w.cflag || !out.w.ax)
        return 0;
    *linear = (uint32_t)out.w.ax << 4;
    return 1;
}

static void ne_dos_free(void *opaque, uint32_t linear)
{
    (void)opaque;
    union REGS in = { 0 }, out = { 0 };
    struct SREGS sr;
    segread(&sr);
    sr.es = (uint16_t)(linear >> 4);
    in.w.ax = 0x4900;
    int386x(0x21, &in, &out, &sr);
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
    static const win_ne_guest_io io = {ne_dos_alloc, ne_dos_free, ne_dos_write};
    return &io;
}
#endif
