/*
 * ne_guest.c - stage relocatable Win16 NE segments in x86 guest memory.
 *
 * This layer does not touch CPU internals.  It creates 16-bit (D=0, G=0)
 * descriptors in a GDT image and emits guest x86 API thunks. The caller
 * remains the native WINN executor. Running the GDT is a separate step.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ne_guest.h"
#include "ne_boot.h"
#include "x86bridge.h"

#define GDT_ENTRY_BYTES 8u
#define GDT_THUNK_INDEX 1u
#define GDT_SEGMENT_INDEX(i) ((i) + 1u)
#define THUNK_BYTES WIN_NE_THUNK_BYTES

/* Never allow allocated API slots or the PM bootstrap to overlap. */
_Static_assert(WIN_NE_GUEST_ACTIVE_THUNKS * THUNK_BYTES <= WIN_NE_BOOT_OFFSET,
               "NE API thunks overlap the PM trampoline");
_Static_assert(WIN_NE_BOOT_OFFSET + WIN_NE_BOOT_CAPACITY <=
               WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES,
               "NE PM trampoline exceeds thunk segment");

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Access byte: present, DPL=0, executable/readable or data/read-write.
 * Flags: G=0 (byte granularity), D=0 (16-bit code/stack). */
static void descriptor(uint8_t *p, uint32_t base, uint32_t size, int code)
{
    uint32_t limit = size - 1u;
    put16(p, (uint16_t)limit);
    put16(p + 2, (uint16_t)base);
    p[4] = (uint8_t)(base >> 16);
    p[5] = code ? 0x9au : 0x92u;
    p[6] = (uint8_t)(limit >> 16) & 0x0fu;
    p[7] = (uint8_t)(base >> 24);
}

static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return 0;
    }
    return !*a && !*b;
}

void win_ne_guest_release(win_ne_guest_image *image)
{
    if (!image)
        return;
    if (image->io.free) {
        if (image->segments)
            for (uint16_t i = 1; i <= image->info.segment_count; i++)
                if (image->segments[i].linear)
                    image->io.free(image->opaque, image->segments[i].linear);
        if (image->thunk_linear)
            image->io.free(image->opaque, image->thunk_linear);
        if (image->gdt_linear)
            image->io.free(image->opaque, image->gdt_linear);
    }
    free(image->segments);
    free(image->gdt);
    memset(image, 0, sizeof(*image));
}

static int guest_alloc(win_ne_guest_image *g, uint32_t bytes, uint32_t *linear)
{
    *linear = 0;
    if (!g->io.alloc(g->opaque, bytes, linear))
        return 0;
    if (!*linear || (*linear & 15u) || *linear > UINT32_MAX - bytes) {
        if (*linear)
            g->io.free(g->opaque, *linear);
        *linear = 0;
        return 0;
    }
    return 1;
}

static int assign_selector(void *opaque, uint16_t index, uint16_t flags,
                           uint32_t bytes, uint16_t *selector)
{
    win_ne_guest_image *g = (win_ne_guest_image *)opaque;
    if (!index || index > g->info.segment_count || !bytes || bytes > 65536u)
        return 0;
    win_ne_guest_segment *s = &g->segments[index];
    if (s->linear || !guest_alloc(g, bytes, &s->linear))
        return 0;
    s->size = bytes;
    s->flags = flags;
    s->selector = (uint16_t)(GDT_SEGMENT_INDEX(index) * GDT_ENTRY_BYTES);
    *selector = s->selector;
    descriptor(g->gdt + s->selector, s->linear, s->size, !(flags & 1u));
    return 1;
}

/* Win16 ordinals are distinct per DLL. The encoded service number lives
 * entirely inside WINN; it is not a guest pointer or an emulated IRQ. */
static int service_id(const win_ne_import *imp, uint16_t *id)
{
    if (name_eq(imp->module, "KERNEL") || name_eq(imp->module, "KERNEL.EXE")) {
        if ((imp->ordinal == 3 && !imp->procedure[0]) ||
            (!imp->ordinal && name_eq(imp->procedure, "GetVersion")))
            *id = 3;
        else if ((imp->ordinal == 36 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "GetCurrentTask")))
            *id = 36;
        else if ((imp->ordinal == 152 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "GetNumTasks")))
            *id = 152;
        else if ((imp->ordinal == 88 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "lstrcpy")))
            *id = 88;
        else if ((imp->ordinal == 89 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "lstrcat")))
            *id = 89;
        else if ((imp->ordinal == 90 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "lstrlen")))
            *id = 90;
        else
            return 0;
    } else if (name_eq(imp->module, "USER") ||
               name_eq(imp->module, "USER.EXE")) {
        if ((imp->ordinal == 13 && !imp->procedure[0]) ||
            (!imp->ordinal && name_eq(imp->procedure, "GetTickCount")))
            *id = 0x100du;
        else if ((imp->ordinal == 179 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "GetSystemMetrics")))
            *id = 0x10b3u;
        else if ((imp->ordinal == 180 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "GetSysColor")))
            *id = 0x10b4u;
        else if ((imp->ordinal == 1 && !imp->procedure[0]) ||
                 (!imp->ordinal && name_eq(imp->procedure, "MessageBox")))
            *id = 0x1001u;
        else
            return 0;
    } else {
        return 0;
    }
    return 1;
}

/* Win16 PASCAL return releases the argument words after popping CS:IP. */
static uint16_t service_args(uint16_t id)
{
    switch (id) {
    case 88: case 89: return 8;    /* KERNEL.lstrcpy/lstrcat: two FAR pointers */
    case 90: return 4;             /* KERNEL.lstrlen: far pointer */
    case 0x1001: return 12;        /* USER.MessageBox(HWND, FAR, FAR, WORD) */
    case 0x10b3: case 0x10b4: return 2; /* USER: int parameter */
    default: return 0;
    }
}

static int resolve_import(void *opaque, const win_ne_import *imp,
                          uint16_t *selector, uint16_t *offset)
{
    win_ne_guest_image *g = (win_ne_guest_image *)opaque;
    uint16_t id;
    if (!service_id(imp, &id))
        return 0;
    for (uint16_t i = 0; i < g->thunk_count; i++) {
        if (g->thunk_services[i] == id) {
            *selector = g->thunk_selector;
            *offset = (uint16_t)(i * THUNK_BYTES);
            return 1;
        }
    }
    if (g->thunk_count >= WIN_NE_GUEST_ACTIVE_THUNKS)
        return 0;
    uint16_t slot = g->thunk_count++;
    g->thunk_services[slot] = id;
    *selector = g->thunk_selector;
    *offset = (uint16_t)(slot * THUNK_BYTES);
    return 1;
}

int win_ne_guest_load(const char *path, win_ne_guest_image *out,
                      const win_ne_guest_io *io, void *opaque)
{
    if (!out || !io || !io->alloc || !io->free || !io->write)
        return WIN_NE_BAD_IMAGE;
    memset(out, 0, sizeof(*out));
    win_ne_info info;
    int rc = win_ne_scan(path, &info, NULL, NULL);
    if (rc)
        return rc;
    if (!info.cs_index || info.segment_count > WIN_NE_GUEST_MAX_SEGMENTS ||
        info.target_os != 2u)
        return WIN_NE_UNSUPPORTED;

    out->io = *io;
    out->opaque = opaque;
    out->info = info;
    out->segments = (win_ne_guest_segment *)calloc((size_t)info.segment_count + 1u,
                                                    sizeof(*out->segments));
    uint32_t gdtsize = ((uint32_t)info.segment_count + 2u) * GDT_ENTRY_BYTES;
    out->gdt = (uint8_t *)calloc(gdtsize, 1);
    rc = WIN_NE_NO_MEMORY;
    if (!out->segments || !out->gdt)
        goto fail;
    out->gdt_limit = (uint16_t)(gdtsize - 1u);
    out->thunk_selector = GDT_THUNK_INDEX * GDT_ENTRY_BYTES;
    if (!guest_alloc(out, gdtsize, &out->gdt_linear) ||
        !guest_alloc(out, WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES,
                     &out->thunk_linear))
        goto fail;
    descriptor(out->gdt + out->thunk_selector, out->thunk_linear,
               WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES, 1);

    win_ne_image image;
    rc = win_ne_load_image(path, &image, assign_selector,
                           resolve_import, out);
    if (rc)
        goto fail;

    /* Code segments, stack, and thunks must be visible to x86 before
     * loading GDTR. A failed guest write leaves no partially-owned blocks. */
    rc = WIN_NE_IO_ERROR;
    for (uint16_t i = 1; i <= info.segment_count; i++) {
        const win_ne_guest_segment *s = &out->segments[i];
        if (!io->write(opaque, s->linear, image.segments[i].data, s->size))
            goto loaded_fail;
    }
    {
        /* Initialise every unused thunk byte to INT3, so an unallocated
         * offset never executes stale DOS heap contents. */
        uint8_t thunks[WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES];
        memset(thunks, 0xcc, sizeof(thunks));
        for (uint16_t i = 0; i < out->thunk_count; i++) {
            uint16_t id = out->thunk_services[i];
            uint8_t *p = thunks + i * THUNK_BYTES;
            /* CALL FAR the shared PM->RM->PM bridge. The OUT is executed
             * in real mode, allowing its native handler to use DOS/BIOS.
             * RETF imm16 restores the Win16 PASCAL caller's SP. */
            uint16_t args = service_args(id);
            p[0] = 0xb8; put16(p + 1, id); /* mov ax,service */
            p[3] = 0x9a;                   /* call far PM gateway */
            put16(p + 4, WIN_NE_BOOT_GATEWAY_OFFSET);
            put16(p + 6, out->thunk_selector);
            if (args) {
                p[8] = 0xca; put16(p + 9, args);
            } else {
                p[8] = 0xcb;
            }
        }
        if (!io->write(opaque, out->thunk_linear, thunks, sizeof(thunks)))
            goto loaded_fail;
    }
    if (!io->write(opaque, out->gdt_linear, out->gdt, gdtsize))
        goto loaded_fail;

    out->cs = out->segments[info.cs_index].selector;
    out->ip = info.ip;
    if (info.ss_index) {
        out->ss = out->segments[info.ss_index].selector;
        out->sp = info.sp;
    }
    rc = WIN_NE_OK;
loaded_fail:
    win_ne_free_image(&image);
    if (!rc)
        return WIN_NE_OK;
fail:
    win_ne_guest_release(out);
    return rc;
}
