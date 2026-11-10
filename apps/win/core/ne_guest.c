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
_Static_assert(WIN_NE_BOOT_OFFSET + WIN_NE_BOOT_CAPACITY <= WIN_NE_DOS_STUB_OFFSET,
               "NE PM trampoline overlaps the DOS stubs");
_Static_assert(WIN_NE_DOS_STUB_OFFSET + WIN_NE_DOS_STUB_BYTES <= WIN_NE_FAULT_IDT_OFFSET,
               "NE DOS stubs overlap the private IDT");
_Static_assert(WIN_NE_FAULT_IDT_OFFSET + WIN_NE_FAULT_IDT_BYTES <=
               WIN_NE_FAULT_CODE_OFFSET, "NE private IDT overlaps fault code");
_Static_assert(WIN_NE_FAULT_CODE_OFFSET + WIN_NE_FAULT_CODE_BYTES <=
               WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES,
               "NE fault recovery exceeds thunk segment");

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
        if (image->psp.linear)
            image->io.free(image->opaque, image->psp.linear);
        for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++)
            if (image->globals[i].linear)
                image->io.free(image->opaque, image->globals[i].linear);
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

/* The Win16 functions the guest can import: module, ordinal, name and the
 * bytes of PASCAL arguments its RETF releases (0 for the C ones: the caller
 * does). The service number, KERNEL ordinal n, USER 0x1000 + n, GDI
 * 0x3000 + n, lives entirely inside WINN; it is not a guest pointer or an
 * emulated IRQ. */
enum { M_KERNEL, M_USER, M_GDI };
static const uint16_t module_base[] = { 0, 0x1000u, 0x3000u };
static const struct {
    uint8_t module;                 /* M_* */
    uint16_t ordinal;
    const char *name;
    uint16_t args;
} services[] = {
    { M_KERNEL, 3, "GetVersion", 0 },
    { M_KERNEL, 30, "WaitEvent", 2 },
    { M_KERNEL, 36, "GetCurrentTask", 0 },
    { M_KERNEL, 49, "GetModuleFileName", 8 },
    { M_KERNEL, 88, "lstrcpy", 8 },
    { M_KERNEL, 89, "lstrcat", 8 },
    { M_KERNEL, 90, "lstrlen", 4 },
    { M_KERNEL, 91, "InitTask", 0 },
    { M_KERNEL, 152, "GetNumTasks", 0 },
    { M_KERNEL, 51, "MakeProcInstance", 6 },
    { M_KERNEL, 52, "FreeProcInstance", 4 },
    { M_USER, 1, "MessageBox", 12 },
    { M_USER, 5, "InitApp", 2 },
    { M_USER, 13, "GetTickCount", 0 },
    { M_USER, 179, "GetSystemMetrics", 2 },
    { M_USER, 180, "GetSysColor", 2 },
    { M_USER, 6, "PostQuitMessage", 2 },
    { M_USER, 39, "BeginPaint", 6 },
    { M_USER, 40, "EndPaint", 6 },
    { M_USER, 41, "CreateWindow", 30 },
    { M_USER, 42, "ShowWindow", 4 },
    { M_USER, 53, "DestroyWindow", 2 },
    { M_USER, 57, "RegisterClass", 4 },
    { M_USER, 107, "DefWindowProc", 10 },
    { M_USER, 108, "GetMessage", 10 },
    { M_USER, 113, "TranslateMessage", 4 },
    { M_USER, 114, "DispatchMessage", 4 },
    { M_USER, 124, "UpdateWindow", 2 },
    { M_USER, 125, "InvalidateRect", 8 },
    { M_USER, 173, "LoadCursor", 6 },
    { M_USER, 174, "LoadIcon", 6 },
    { M_USER, 420, "wsprintf", 0 },
    { M_USER, 87, "DialogBox", 12 },
    { M_USER, 88, "EndDialog", 4 },
    { M_USER, 91, "GetDlgItem", 4 },
    { M_USER, 92, "SetDlgItemText", 8 },
    { M_USER, 93, "GetDlgItemText", 10 },
    { M_USER, 94, "SetDlgItemInt", 8 },
    { M_USER, 95, "GetDlgItemInt", 10 },
    { M_USER, 150, "LoadMenu", 6 },
    { M_USER, 176, "LoadString", 10 },
    { M_USER, 177, "LoadAccelerators", 6 },
    { M_USER, 178, "TranslateAccelerator", 8 },
    { M_GDI, 33, "TextOut", 12 },
    { M_GDI, 87, "GetStockObject", 2 },
    /* ne_guest_call.c */
    { M_KERNEL, 4, "LocalInit", 6 },
    { M_KERNEL, 5, "LocalAlloc", 4 },
    { M_KERNEL, 6, "LocalReAlloc", 6 },
    { M_KERNEL, 7, "LocalFree", 2 },
    { M_KERNEL, 8, "LocalLock", 2 },
    { M_KERNEL, 9, "LocalUnlock", 2 },
    { M_KERNEL, 10, "LocalSize", 2 },
    { M_KERNEL, 15, "GlobalAlloc", 6 },
    { M_KERNEL, 16, "GlobalReAlloc", 8 },
    { M_KERNEL, 17, "GlobalFree", 2 },
    { M_KERNEL, 18, "GlobalLock", 2 },
    { M_KERNEL, 19, "GlobalUnlock", 2 },
    { M_KERNEL, 20, "GlobalSize", 2 },
    { M_KERNEL, 21, "GlobalHandle", 2 },
    { M_KERNEL, 22, "GlobalFlags", 2 },
    { M_KERNEL, 23, "LockSegment", 2 },
    { M_KERNEL, 24, "UnlockSegment", 2 },
    { M_USER, 10, "SetTimer", 10 },
    { M_USER, 12, "KillTimer", 4 },
    { M_USER, 30, "WindowFromPoint", 4 },
    { M_USER, 54, "EnumWindows", 8 },
    { M_USER, 55, "EnumChildWindows", 10 },
    { M_USER, 76, "PtInRect", 8 },
    { M_USER, 89, "CreateDialog", 12 },
    { M_USER, 90, "IsDialogMessage", 6 },
    { M_USER, 101, "SendDlgItemMessage", 12 },
    { M_USER, 109, "PeekMessage", 12 },
    { M_USER, 110, "PostMessage", 10 },
    { M_USER, 111, "SendMessage", 10 },
    { M_USER, 122, "CallWindowProc", 14 },
    { M_USER, 129, "GetClassWord", 4 },
    { M_USER, 131, "GetClassLong", 4 },
    { M_USER, 133, "GetWindowWord", 4 },
    { M_USER, 134, "SetWindowWord", 6 },
    { M_USER, 135, "GetWindowLong", 4 },
    { M_USER, 136, "SetWindowLong", 8 },
    { M_USER, 191, "ChildWindowFromPoint", 6 },
    { M_USER, 308, "DefDlgProc", 10 },
    { M_USER, 421, "wvsprintf", 12 },
    { M_USER, 431, "AnsiUpper", 4 },
    { M_USER, 432, "AnsiLower", 4 },
    { M_USER, 472, "AnsiNext", 4 },
    { M_USER, 473, "AnsiPrev", 8 },
    { M_GDI, 50, "CreateBrushIndirect", 4 },
    { M_GDI, 57, "CreateFontIndirect", 4 },
    { M_GDI, 62, "CreatePenIndirect", 4 },
    { M_GDI, 82, "GetObject", 8 },
    { M_GDI, 93, "GetTextMetrics", 6 },
};

/* the generic ones (sdk/gen_ne_sigs.py) */
static const struct {
    uint8_t module;
    uint16_t ordinal;
    const char *name;
    const char *sig;
} generic[] = {
#include "ne_sigs.h"
};

/* the bytes of PASCAL arguments of a signature's codes */
static uint16_t sig_args(const char *sig)
{
    uint16_t n = 0;
    for (const char *p = strchr(sig, ':') + 1; *p; p++)
        n = (uint16_t)(n + (strchr("wiI", *p) ? 2 : 4));
    return n;
}

static int generic_index(int module, const win_ne_import *imp)
{
    for (int i = 0; i < (int)(sizeof(generic) / sizeof(generic[0])); i++)
        if (generic[i].module == module &&
            (imp->ordinal ? imp->ordinal == generic[i].ordinal && !imp->procedure[0]
                          : name_eq(imp->procedure, generic[i].name)))
            return i;
    return -1;
}

const char *win_ne_generic_sig(uint16_t service)
{
    for (unsigned i = 0; i < sizeof(generic) / sizeof(generic[0]); i++)
        if (module_base[generic[i].module] + generic[i].ordinal == service)
            return generic[i].sig;
    return NULL;
}

static int module_of(const win_ne_import *imp)
{
    if (name_eq(imp->module, "KERNEL") || name_eq(imp->module, "KERNEL.EXE"))
        return M_KERNEL;
    if (name_eq(imp->module, "USER") || name_eq(imp->module, "USER.EXE"))
        return M_USER;
    if (name_eq(imp->module, "GDI") || name_eq(imp->module, "GDI.EXE"))
        return M_GDI;
    return -1;
}

/* a function WINN has its own code for: its index in services[], -1 */
static int service_index(const win_ne_import *imp)
{
    int module = module_of(imp);
    for (int i = 0; module >= 0 && i < (int)(sizeof(services) / sizeof(services[0])); i++)
        if (services[i].module == module &&
            (imp->ordinal ? imp->ordinal == services[i].ordinal && !imp->procedure[0]
                          : name_eq(imp->procedure, services[i].name)))
            return i;
    return -1;
}

/* the service number of an import, and the bytes of its arguments */
static int service_id_args(const win_ne_import *imp, uint16_t *id, uint16_t *args)
{
    int i = service_index(imp), module = module_of(imp);
    if (i >= 0) {
        *id = (uint16_t)(module_base[services[i].module] + services[i].ordinal);
        *args = services[i].args;
        return 1;
    }
    if (module >= 0 && (i = generic_index(module, imp)) >= 0) {
        *id = (uint16_t)(module_base[module] + generic[i].ordinal);
        *args = sig_args(generic[i].sig);
        return 1;
    }
    return 0;
}

static int service_id(const win_ne_import *imp, uint16_t *id)
{
    uint16_t args;
    return service_id_args(imp, id, &args);
}

/* Win16 PASCAL return releases the argument words after popping CS:IP. */
static uint16_t service_args(uint16_t id)
{
    for (unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
        if (module_base[services[i].module] + services[i].ordinal == id)
            return services[i].args;
    const char *sig = win_ne_generic_sig(id);
    return sig ? sig_args(sig) : 0;
}

static int note_unresolved(void *opaque, const win_ne_import *imp)
{
    uint16_t id;
    if (service_id(imp, &id))
        return 0;
    win_ne_unresolved *u = (win_ne_unresolved *)opaque;
    size_t n = strlen(imp->module);
    if (n >= sizeof(u->module))
        n = sizeof(u->module) - 1;
    memcpy(u->module, imp->module, n);
    u->module[n] = 0;
    n = strlen(imp->procedure);
    if (n >= sizeof(u->name))
        n = sizeof(u->name) - 1;
    memcpy(u->name, imp->procedure, n);
    u->name[n] = 0;
    u->ordinal = imp->ordinal;
    return 1;                           /* the first one is enough */
}

int win_ne_guest_unresolved(const char *path, win_ne_unresolved *u)
{
    memset(u, 0, sizeof(*u));
    win_ne_info info;
    return win_ne_scan(path, &info, note_unresolved, u) == WIN_NE_CANCELLED;
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
    /* null, thunks, the segments, the PSP, the global blocks */
    uint32_t gdtsize = ((uint32_t)info.segment_count + 3u + WIN_NE_GUEST_GLOBALS) * GDT_ENTRY_BYTES;
    out->gdt = (uint8_t *)calloc(gdtsize, 1);
    rc = WIN_NE_NO_MEMORY;
    if (!out->segments || !out->gdt)
        goto fail;
    out->gdt_limit = (uint16_t)(gdtsize - 1u);
    out->thunk_selector = GDT_THUNK_INDEX * GDT_ENTRY_BYTES;
    out->psp.selector = (uint16_t)(GDT_SEGMENT_INDEX(info.segment_count + 1u) * GDT_ENTRY_BYTES);
    if (!guest_alloc(out, gdtsize, &out->gdt_linear) ||
        !guest_alloc(out, WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES,
                     &out->thunk_linear) ||
        !guest_alloc(out, WIN_NE_GUEST_PSP_BYTES, &out->psp.linear))
        goto fail;
    descriptor(out->gdt + out->thunk_selector, out->thunk_linear,
               WIN_NE_GUEST_MAX_THUNKS * THUNK_BYTES, 1);
    out->psp.size = WIN_NE_GUEST_PSP_BYTES;
    out->psp.flags = 1;                 /* data */
    descriptor(out->gdt + out->psp.selector, out->psp.linear, WIN_NE_GUEST_PSP_BYTES, 0);

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
        /* SP 0: the top of the segment, after DGROUP's data, heap, stack */
        const win_ne_guest_segment *ss = &out->segments[info.ss_index];
        out->ss = ss->selector;
        out->sp = info.sp ? info.sp : (uint16_t)(ss->size < 65536u ? ss->size : 0xfffeu);
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

/* ---------------------------------------------------------------- global memory */

/* Publish descriptor changes only after the guest GDT write succeeds. */
static int set_descriptor(win_ne_guest_image *g, uint16_t sel, uint32_t linear, uint32_t size)
{
    uint8_t desc[GDT_ENTRY_BYTES];
    descriptor(desc, linear, size, 0);
    if (!g->io.write(g->opaque, g->gdt_linear + sel, desc, sizeof(desc)))
        return 0;
    memcpy(g->gdt + sel, desc, sizeof(desc));
    return 1;
}

static uint16_t global_selector(const win_ne_guest_image *g, unsigned i)
{
    return (uint16_t)(GDT_SEGMENT_INDEX(g->info.segment_count + 2u + i) * GDT_ENTRY_BYTES);
}

win_ne_guest_segment *win_ne_guest_global(win_ne_guest_image *g, uint16_t sel)
{
    for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++)
        if (g->globals[i].linear && g->globals[i].selector == (sel & ~7u))
            return &g->globals[i];
    return NULL;
}

uint16_t win_ne_guest_global_alloc(win_ne_guest_image *g, uint32_t bytes)
{
    if (!bytes || bytes > 65536u)
        return 0;
    for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++) {
        win_ne_guest_segment *s = &g->globals[i];
        if (s->linear)
            continue;
        if (!guest_alloc(g, bytes, &s->linear))
            return 0;
        uint8_t zero[256];
        memset(zero, 0, sizeof(zero));
        for (uint32_t at = 0; at < bytes; at += sizeof(zero)) {
            uint32_t n = bytes - at < sizeof(zero) ? bytes - at : (uint32_t)sizeof(zero);
            if (!g->io.write(g->opaque, s->linear + at, zero, n))
                goto alloc_fail;
        }
        uint16_t selector = global_selector(g, i);
        if (!set_descriptor(g, selector, s->linear, bytes))
            goto alloc_fail;
        s->size = bytes;
        s->flags = 1;
        s->selector = selector;
        return selector;
alloc_fail:
        g->io.free(g->opaque, s->linear);
        memset(s, 0, sizeof(*s));
        return 0;
    }
    return 0;
}

int win_ne_guest_global_realloc(win_ne_guest_image *g, uint16_t sel, uint32_t bytes,
                                int (*read)(void *, uint32_t, void *, uint32_t))
{
    win_ne_guest_segment *s = win_ne_guest_global(g, sel);
    uint32_t linear;
    if (!s || !read || !bytes || bytes > 65536u || !guest_alloc(g, bytes, &linear))
        return 0;
    uint8_t buf[256];
    for (uint32_t at = 0; at < bytes; at += sizeof(buf)) {
        uint32_t n = bytes - at < sizeof(buf) ? bytes - at : (uint32_t)sizeof(buf);
        memset(buf, 0, sizeof(buf));
        if (at < s->size && !read(NULL, s->linear + at, buf,
                                 s->size - at < n ? s->size - at : n))
            goto realloc_fail;
        if (!g->io.write(g->opaque, linear + at, buf, n))
            goto realloc_fail;
    }
    if (!set_descriptor(g, s->selector, linear, bytes))
        goto realloc_fail;
    g->io.free(g->opaque, s->linear);
    s->linear = linear;
    s->size = bytes;
    return 1;
realloc_fail:
    g->io.free(g->opaque, linear);
    return 0;
}

int win_ne_guest_global_free(win_ne_guest_image *g, uint16_t sel)
{
    win_ne_guest_segment *s = win_ne_guest_global(g, sel);
    if (!s)
        return 0;
    uint8_t absent[GDT_ENTRY_BYTES] = {0};
    /* A failed descriptor write must not release still-addressable memory. */
    if (!g->io.write(g->opaque, g->gdt_linear + s->selector,
                     absent, sizeof(absent)))
        return 0;
    memcpy(g->gdt + s->selector, absent, sizeof(absent));
    g->io.free(g->opaque, s->linear);
    memset(s, 0, sizeof(*s));
    return 1;
}
