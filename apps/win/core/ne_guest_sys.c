/*
 * ne_guest_sys.c - Windows' own DLLs a Win16 program may import, as WINN
 * has them:
 *
 *   TOOLHELP  the tasks, WINN's modules, the timer, the memory as
 *             GetFreeSpace has it, MemoryRead/Write; notifications and
 *             interrupt handlers taken, never called
 *
 * and the native DLLs' calls (core/windll.c, ddi/windll.h: LZEXPAND, VER,
 * DDEML, MMSYSTEM): the program's arguments and memory for the DLL.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "windll.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define TH WIN_NE_SERVICE_TOOLHELP

static int rd_(args *a, uint32_t far, void *d, uint32_t n)
{
    if (!n)
        return 1;
    if (!(far >> 16) || !win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, d, n))
        return a->bad = 1, 0;
    return 1;
}

static int wr_(args *a, uint32_t far, const void *s, uint32_t n)
{
    if (!n)
        return 1;
    if (!(far >> 16) || !win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, s, n))
        return a->bad = 1, 0;
    return 1;
}

/* a string of the program's; NULL (0:0) is "" and fails */
static int str_(args *a, uint32_t far, char *d, uint32_t cap)
{
    d[0] = 0;
    if (!(far >> 16))
        return 0;
    if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, d, cap))
        return a->bad = 1, 0;
    return 1;
}

/* the most bytes from far on in its segment, of n */
static uint32_t in_segment(uint32_t far, uint32_t n)
{
    uint32_t room = 0x10000u - (uint16_t)far;
    return n < room ? n : room;
}

/* ================================================================ TOOLHELP */

/* a structure's dwSize at least n */
static int sized(args *a, uint32_t far, uint32_t n)
{
    uint8_t b[4];
    return rd_(a, far, b, 4) && rd32(b) >= n;
}

/* TASKENTRY of task t (0: none) */
static int task_entry(args *a, uint32_t far, HTASK t)
{
    wtask *k = t ? task_from_handle(t) : NULL;
    if (!k || !sized(a, far, 40))
        return 0;
    uint8_t e[40];
    memset(e, 0, sizeof(e));
    wr32(e, 40);
    wr16(e + 4, t);
    ne_user *n = ne_user_of(t);
    uint16_t inst = t;
    if (n) {
        const win_ne_guest_image *g = n->ctx->image;
        inst = g->segments[g->info.auto_data ? g->info.auto_data : g->info.ss_index].selector;
    }
    wr16(e + 8, inst);
    wr16(e + 10, inst);
    for (int i = 0; i < 9 && k->name[i] && k->name[i] != '.'; i++)
        e[26 + i] = (uint8_t)(k->name[i] >= 'a' && k->name[i] <= 'z' ? k->name[i] - 32
                                                                     : k->name[i]);
    wr16(e + 36, 0x100);
    wr16(e + 38, task_next(t));
    return wr_(a, far, e, sizeof(e));
}

/* MODULEENTRY of WINN's module m */
/* the modules: WINN's own (F000h + n), then the native DLLs loaded (their
 * handles); the one after h, 0 after the last */
static uint16_t module_after(uint16_t h)
{
    int s = -1;
    if (h >= 0xf000u && win_ne_guest_module_name(h - 0xf000 + 1))
        return (uint16_t)(h + 1);
    if (h < 0xf000u && (s = win_dll_of(h)) < 0)
        return 0;
    while (++s < WIN_DLL_SLOTS)
        if (win_dll_get(s))
            return win_dll_handle(s);
    return 0;
}

static int module_entry(args *a, uint32_t far, uint16_t h)
{
    int m = h >= 0xf000u ? h - 0xf000 : -1, s = h < 0xf000u ? win_dll_of(h) : -1;
    const char *name = m >= 0 ? win_ne_guest_module_name(m) : s >= 0 ? win_dll_get(s)->name : NULL;
    if (!name || !sized(a, far, 276))
        return 0;
    uint8_t e[276];
    char path[128];
    memset(e, 0, sizeof(e));
    wr32(e, 276);
    lstrcpyn((char *)e + 4, name, 10);
    wr16(e + 14, h);
    wr16(e + 16, 1);
    if (s >= 0) {
        lstrcpyn(path, win_dll_path(s), sizeof(path));
    } else {
        UINT n = GetSystemDirectory(path, sizeof(path) - 16);
        wsprintf(path + n, "%s%s.%s", n && path[n - 1] != '\\' ? "\\" : "", name,
                 m < 3 ? "EXE" : m == 3 ? "DRV" : "DLL");
    }
    lstrcpyn((char *)e + 18, path, 256);
    wr16(e + 274, module_after(h));
    return wr_(a, far, e, sizeof(e));
}

static __attribute__((noinline)) int th_service(args *a, ne_user *u, uint16_t service,
                                               uint32_t *v, int *dword)
{
    (void)u;
    unsigned o = service - TH;
    uint8_t e[44];
    switch (o) {
    case 50:                            /* GlobalHandleToSel: itself */
        *v = w(a, 4);
        return 1;
    case 53:                            /* GlobalInfo(GLOBALINFO FAR *) */
        if ((*v = sized(a, dw(a, 4), 10)) != 0) {
            memset(e, 0, 10);
            wr32(e, 10);
            wr16(e + 4, WIN_NE_GUEST_GLOBALS);
            wr16(e + 6, WIN_NE_GUEST_GLOBALS / 2);
            wr_(a, dw(a, 4), e, 10);
        }
        return 1;
    case 59:                            /* ModuleFirst */
        *v = (uint16_t)module_entry(a, dw(a, 4), 0xf000u);
        return 1;
    case 60: {                          /* ModuleNext: its wNext */
        uint8_t b[2];
        *v = rd_(a, dw(a, 4) + 274, b, 2) && rd16(b) ?
             (uint16_t)module_entry(a, dw(a, 4), rd16(b)) : 0;
        return 1;
    }
    case 61: {                          /* ModuleFindName(MODULEENTRY FAR *, LPCSTR) */
        char name[16];
        int m = str_(a, dw(a, 4), name, sizeof(name)) ? win_ne_guest_module(name) : -1;
        int s = m < 0 && name[0] ? win_dll_find(name) : -1;
        *v = m >= 0 ? (uint16_t)module_entry(a, dw(a, 8), (uint16_t)(0xf000u + (unsigned)m)) :
             s >= 0 ? (uint16_t)module_entry(a, dw(a, 8), win_dll_handle(s)) : 0;
        return 1;
    }
    case 62:                            /* ModuleFindHandle(MODULEENTRY FAR *, HMODULE) */
        *v = (uint16_t)module_entry(a, dw(a, 6), w(a, 4));
        return 1;
    case 63:                            /* TaskFirst */
        *v = (uint16_t)task_entry(a, dw(a, 4), task_next(0));
        return 1;
    case 64: {                          /* TaskNext: its hNext */
        uint8_t b[2];
        *v = rd_(a, dw(a, 4) + 38, b, 2) ? (uint16_t)task_entry(a, dw(a, 4), rd16(b)) : 0;
        return 1;
    }
    case 65:                            /* TaskFindHandle(TASKENTRY FAR *, HTASK) */
        *v = IsTask(w(a, 4)) ? (uint16_t)task_entry(a, dw(a, 6), w(a, 4)) : 0;
        return 1;
    case 71:                            /* SystemHeapInfo: USER's and GDI's, 80% free */
        if ((*v = sized(a, dw(a, 4), 12)) != 0) {
            memset(e, 0, 12);
            wr32(e, 12);
            wr16(e + 4, 80);
            wr16(e + 6, 80);
            wr_(a, dw(a, 4), e, 12);
        }
        return 1;
    case 72:                            /* MemManInfo: as GetFreeSpace, pages of 4K */
        if ((*v = sized(a, dw(a, 4), 42)) != 0) {
            static const uint32_t m[9] = { 0x80000u, 128, 128, 256, 128, 128, 256, 128, 0 };
            memset(e, 0, 42);
            wr32(e, 42);
            for (int i = 0; i < 9; i++)
                wr32(e + 4 + 4 * i, m[i]);
            wr16(e + 40, 4096);
            wr_(a, dw(a, 4), e, 42);
        }
        return 1;
    case 73: case 74: case 75: case 76:  /* notifications, interrupts: taken, never made */
        *v = 1;
        return 1;
    case 77:                            /* TerminateApp: not another's */
        return 1;
    case 78: case 79: {                 /* MemoryRead, MemoryWrite(sel, off, buf, count) */
        uint16_t sel = w(a, 16);
        uint32_t off = dw(a, 12), buf = dw(a, 8), n = dw(a, 4);
        if (off > 0xffffu)
            n = 0;
        n = in_segment(off, in_segment(buf, n));
        uint8_t *tmp = n ? (uint8_t *)win_alloc(n) : NULL;
        int ok = tmp && (o == 78 ? win_ne_read(a->ctx, sel, (uint16_t)off, tmp, n) &&
                                   win_ne_write(a->ctx, (uint16_t)(buf >> 16), (uint16_t)buf,
                                                tmp, n)
                                 : win_ne_read(a->ctx, (uint16_t)(buf >> 16), (uint16_t)buf,
                                               tmp, n) &&
                                   win_ne_write(a->ctx, sel, (uint16_t)off, tmp, n));
        win_free(tmp);
        *v = ok ? n : 0;
        *dword = 1;
        return 1;
    }
    case 80:                            /* TimerCount(TIMERINFO FAR *) */
        if ((*v = sized(a, dw(a, 4), 12)) != 0) {
            wr32(e, 12);
            wr32(e + 4, GetTickCount());
            wr32(e + 8, GetTickCount());
            wr_(a, dw(a, 4), e, 12);
        }
        return 1;
    case 51: case 52: case 54: case 55: case 56: case 57: case 58: case 66: case 67: case 68:
    case 69: case 70: case 84: case 85: case 86:
        *v = 0;                         /* the heaps' blocks, stacks, classes: not walked */
        return 1;
    }
    return 0;
}

/* ================================================================ native DLLs */

/* a call of the program's for a DLL: its arguments and memory */
struct win_dll_call {
    args *a;
};

static uint16_t call_arg16(win_dll_call *c, unsigned off) { return w(c->a, off); }
static uint32_t call_arg32(win_dll_call *c, unsigned off) { return dw(c->a, off); }
static int call_read(win_dll_call *c, uint32_t far, void *dst, uint32_t n) { return rd_(c->a, far, dst, n); }
static int call_write(win_dll_call *c, uint32_t far, const void *src, uint32_t n)
{
    return wr_(c->a, far, src, n);
}
static int call_string(win_dll_call *c, uint32_t far, char *dst, uint32_t cap)
{
    return str_(c->a, far, dst, cap);
}

static uint32_t task_call(uint16_t task, uint32_t proc, const uint16_t *words, int n)
{
    ne_user *u = ne_user_of((HTASK)task);
    return u && u->ctx && proc ? win_ne_call(u->ctx, proc, words, n, NULL, 0) : 0;
}

static const win_dll_ne dll_ne = {
    sizeof(win_dll_ne), call_arg16, call_arg32, call_read, call_write, call_string, task_call,
};

const win_dll_ne *win_dll_ne_glue(void) { return &dll_ne; }

/* a native DLL's service: its slot, its export */
static int dll_slot_of(uint16_t id, const win_dll_export **e)
{
    int s = (int)((id - WIN_NE_SERVICE_DLL) >> 10);
    *e = win_dll_export_at(s, (id - WIN_NE_SERVICE_DLL) & 0x3ffu);
    return s;
}

static __attribute__((noinline)) int dll_service(args *a, uint16_t service, uint32_t *v,
                                                int *dword)
{
    const win_dll_export *e;
    const win_dll_info *d = win_dll_get(dll_slot_of(service, &e));
    win_dll_call c = { a };
    int r = d && e && d->ne_call ? d->ne_call(&c, e->ordinal, v) : WINDLL_NONE;
    *dword = r == WINDLL_DWORD;
    return r != WINDLL_NONE;
}

/* the service of a native DLL's export (its slot's) */
uint16_t win_ne_dll_service(int slot, const win_dll_export *e)
{
    const win_dll_info *d = win_dll_get(slot);
    return (uint16_t)(d && e ? WIN_NE_DLL_SERVICE(slot, (unsigned)(e - d->exports)) : 0);
}

int win_ne_native_import(const char *module, uint16_t ordinal, const char *name, uint16_t *id)
{
    int s = win_dll_load(module);
    if (s < 0)
        return 0;
    const win_dll_export *e = win_dll_export_of(s, ordinal, name);
    if (!e) {                           /* not the DLL's: an undefined call's */
        win_dll_unref(s);
        return -1;
    }
    *id = win_ne_dll_service(s, e);
    return 1;
}

int win_ne_native_has(const char *module, uint16_t ordinal, const char *name)
{
    uint16_t id;
    const win_dll_export *e;
    int r = win_ne_native_import(module, ordinal, name, &id);
    if (r > 0)
        win_dll_unref(dll_slot_of(id, &e));
    return r;
}

uint16_t win_ne_native_args(uint16_t id)
{
    const win_dll_export *e;
    dll_slot_of(id, &e);
    return e ? e->argbytes : 0;
}

const char *win_ne_native_name(uint16_t id, char *buf, unsigned cap)
{
    const win_dll_export *e;
    const win_dll_info *d = win_dll_get(dll_slot_of(id, &e));
    if (!d || !e)
        return NULL;
    if (e->name)
        wsprintf(buf, "%s.%s", d->name, e->name);
    else
        wsprintf(buf, "%s.%u", d->name, (unsigned)e->ordinal);
    (void)cap;
    return buf;
}

/* ================================================================ */

int ne_sys_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    if (service >= TH && service < TH + 0x100u)
        return th_service(a, u, service, v, dword);
    if (service >= WIN_NE_SERVICE_DLL && service < WIN_NE_SERVICE_UNDEFINED)
        return dll_service(a, service, v, dword);
    return 0;
}

#endif
