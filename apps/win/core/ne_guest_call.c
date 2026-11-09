/*
 * ne_guest_call.c - more of a Win16 program's calls: the generic ones,
 * marshalled by the signatures of core/ne_sigs.h (sdk/gen_ne_sigs.py) into
 * the native export table; its memory (global blocks with selectors of
 * their own, the local heap in DGROUP); Win16 structures to and from the
 * native ones (TEXTMETRIC, LOGFONT ...); messages whose lParam points to
 * something (strings for the controls, CREATESTRUCT and MINMAXINFO for its
 * own window procedures); timers, enumerations and subclassing with
 * procedures of its own.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

enum { M_KERNEL, M_USER, M_GDI };

/* ---------------------------------------------------------------- guest memory helpers */

static int rd(args *a, uint32_t far, void *dst, uint32_t n)
{
    if (!n)
        return 1;
    if (!win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, n))
        return a->bad = 1, 0;
    return 1;
}

static int wr(args *a, uint32_t far, const void *src, uint32_t n)
{
    if (!n)
        return 1;
    if (!win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, src, n))
        return a->bad = 1, 0;
    return 1;
}

static int str(args *a, uint32_t far, char *dst, uint32_t cap)
{
    if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, cap))
        return a->bad = 1, dst[0] = 0, 0;
    return 1;
}

/* ---------------------------------------------------------------- structures */

/* A structure as fields: i int, u UINT (2 in the guest), h a handle, w a
 * WORD (2 both), d a dword, b a byte, cN char[N], p a pointer (4: a far
 * one in the guest, none natively). The guest's is packed, the native one
 * has its fields aligned. */
static uint32_t pack(const char *f, uint8_t *g, uint8_t *n, int to_native)
{
    uint32_t go = 0, no = 0;
    for (; *f; f++) {
        uint32_t gs, ns, al;
        int count = 0;
        switch (*f) {
        case 'i': case 'u': gs = 2; ns = 4; al = 4; break;
        case 'h': case 'w': gs = 2; ns = 2; al = 2; break;
        case 'd': case 'p': gs = 4; ns = 4; al = 4; break;
        case 'b': gs = 1; ns = 1; al = 1; break;
        case 'c':
            while (f[1] >= '0' && f[1] <= '9')
                count = count * 10 + (*++f - '0');
            gs = ns = (uint32_t)count;
            al = 1;
            break;
        default:
            return no;
        }
        no = (no + al - 1) & ~(al - 1);
        if (g && n) {
            uint8_t *gp = g + go, *np = n + no;
            switch (*f) {
            case 'i':
                if (to_native) { int v = (int16_t)rd16(gp); memcpy(np, &v, 4); }
                else { int v; memcpy(&v, np, 4); wr16(gp, (uint16_t)v); }
                break;
            case 'u':
                if (to_native) { UINT v = rd16(gp); memcpy(np, &v, 4); }
                else { UINT v; memcpy(&v, np, 4); wr16(gp, (uint16_t)v); }
                break;
            case 'p':
                if (to_native) memset(np, 0, 4); else memset(gp, 0, 4);
                break;
            default:
                if (to_native) memcpy(np, gp, gs); else memcpy(gp, np, gs);
                break;
            }
        }
        go += gs;
        no += ns;
    }
    return (no + 3) & ~3u;
}

static uint32_t guest_size(const char *f)
{
    uint32_t n = 0;
    for (; *f; f++) {
        if (*f == 'c') {
            uint32_t c = 0;
            while (f[1] >= '0' && f[1] <= '9')
                c = c * 10 + (uint32_t)(*++f - '0');
            n += c;
        } else {
            n += strchr("iuhw", *f) ? 2u : *f == 'b' ? 1u : 4u;
        }
    }
    return n;
}

#define F_LOGPEN     "uiid"
#define F_LOGBRUSH   "udi"
#define F_LOGFONT    "iiiiibbbbbbbbc32"
#define F_TEXTMETRIC "iiiiiiiibbbbbbbbbiii"
#define F_BITMAP     "iiiibbp"
#define F_MINMAX     "iiiiiiiiii"
#define F_WINDOWPOS  "hhiiiiu"
#define F_RECT       "iiii"

_Static_assert(sizeof(TEXTMETRIC) == 56 && sizeof(LOGFONT) == 60 && sizeof(LOGPEN) == 16 &&
               sizeof(LOGBRUSH) == 12 && sizeof(BITMAP) == 24 && sizeof(WINDOWPOS) == 24 &&
               sizeof(MINMAXINFO) == 40, "the native layouts the field strings describe");

/* a structure of the program's (far) into a native one */
static int struct_in(args *a, const char *f, uint32_t far, void *native)
{
    uint8_t g[64];
    uint32_t n = guest_size(f);
    if (n > sizeof(g) || !rd(a, far, g, n))
        return 0;
    pack(f, g, (uint8_t *)native, 1);
    return 1;
}

static int struct_out(args *a, const char *f, uint32_t far, void *native)
{
    uint8_t g[64];
    uint32_t n = guest_size(f);
    if (n > sizeof(g))
        return 0;
    pack(f, g, (uint8_t *)native, 0);
    return wr(a, far, g, n);
}

/* ---------------------------------------------------------------- the generic marshaller */

typedef uint32_t (*native16)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t, uint32_t);

static uint32_t arg_bytes(char c) { return strchr("wiI", c) ? 2u : 4u; }

/* A buffer for an argument (strings, rectangles ...): from one block for
 * the call, freed after it. */
typedef struct {
    uint8_t *mem;
    uint32_t used, size;
} pool;

static void *take(pool *p, uint32_t n)
{
    n = (n + 3) & ~3u;
    if (p->used + n > p->size)
        return NULL;
    void *r = p->mem + p->used;
    p->used += n;
    memset(r, 0, n);
    return r;
}

static __attribute__((noinline)) int generic(args *a, ne_user *u, uint16_t service,
                                             const char *sig, uint32_t *v, int *dword)
{
    static const uint16_t base[] = { 0, 0x1000u, 0x3000u };
    int module = service >= 0x3000u ? M_GDI : service >= 0x1000u ? M_USER : M_KERNEL;
    static const int table[] = { WINMOD_KERNEL, WINMOD_USER, WINMOD_GDI };
    const void *fn = win_export_tables[table[module]][service - base[module]];
    const char *codes = strchr(sig, ':') + 1;
    int n = (int)strlen(codes);
    if (!fn || n > 16)
        return 0;
    /* where each argument is: the last pushed is first above the return */
    uint32_t off[16], at = 4;
    for (int i = n - 1; i >= 0; i--) {
        off[i] = at;
        at += arg_bytes(codes[i]);
    }
    uint32_t raw[16], x[16];
    for (int i = 0; i < n; i++)
        raw[i] = arg_bytes(codes[i]) == 2 ? w(a, off[i]) : dw(a, off[i]);
    for (int i = n; i < 16; i++)
        x[i] = 0;
    if (a->bad)
        return 1;
    pool p = { NULL, 0, 0 };
    for (int i = 0; i < n; i++) {           /* what the buffers need */
        uint32_t size = (i + 1 < n) ? (uint32_t)(int16_t)raw[i + 1] : 0;
        switch (codes[i]) {
        case 's': p.size += 516; break;
        case 'c': p.size += (size > 1024u ? 1028u : size + 4u); break;
        case 'S': case 'B': case 'b': p.size += (raw[i + 1] & 0xffffu) + 4u; break;
        case 'A': p.size += (raw[i + 1] & 0xffffu) * sizeof(POINT) + 4u; break;
        default: p.size += 16; break;
        }
    }
    p.mem = (uint8_t *)win_alloc(p.size);
    if (!p.mem)
        return 1;
    for (int i = 0; i < n && !a->bad; i++) {
        uint32_t r = raw[i];
        uint32_t next = i + 1 < n ? raw[i + 1] & 0xffffu : 0;
        switch (codes[i]) {
        case 'w': x[i] = r & 0xffffu; break;
        case 'i': x[i] = (uint32_t)(int)(int16_t)r; break;
        case 'I': x[i] = instance(u, (uint16_t)r); break;
        case 'd': x[i] = r; break;
        case 's':                           /* NULL, a number, a string */
            if (!r)
                x[i] = 0;
            else if (!(r >> 16))
                x[i] = (uint32_t)(uintptr_t)MAKEINTRESOURCE((WORD)r);
            else {
                char *s = (char *)take(&p, 512);
                str(a, r, s, 512);
                x[i] = (uint32_t)(uintptr_t)s;
            }
            break;
        case 'c': {                         /* a string of a length (-1: to its NUL) */
            int len = (int16_t)next;
            char *s = (char *)take(&p, len < 0 || len > 1024 ? 1028 : (uint32_t)len + 4u);
            if (len < 0 || len > 1024)
                str(a, r, s, 1024);
            else
                rd(a, r, s, (uint32_t)len);
            x[i] = (uint32_t)(uintptr_t)s;
            break;
        }
        case 'S': case 'b': case 'B': {     /* a buffer of the next argument's size */
            uint8_t *b = (uint8_t *)take(&p, next + 1u);
            if (r && codes[i] != 'S')
                rd(a, r, b, next);          /* in (out: unchanged where not set) */
            x[i] = r ? (uint32_t)(uintptr_t)b : 0;
            break;
        }
        case 'R': case 'r': {
            RECT *rc = (RECT *)take(&p, sizeof(RECT));
            if (r)
                struct_in(a, F_RECT, r, rc);
            x[i] = r ? (uint32_t)(uintptr_t)rc : 0;
            break;
        }
        case 'p': {
            POINT *pt = (POINT *)take(&p, sizeof(POINT));
            uint8_t g[4];
            if (r && rd(a, r, g, 4)) {
                pt->x = (int16_t)rd16(g);
                pt->y = (int16_t)rd16(g + 2);
            }
            x[i] = r ? (uint32_t)(uintptr_t)pt : 0;
            break;
        }
        case 'A': {
            POINT *pts = (POINT *)take(&p, next * (uint32_t)sizeof(POINT));
            for (uint32_t k = 0; k < next && pts; k++) {
                uint8_t g[4];
                if (!rd(a, r + 4u * k, g, 4))
                    break;
                pts[k].x = (int16_t)rd16(g);
                pts[k].y = (int16_t)rd16(g + 2);
            }
            x[i] = (uint32_t)(uintptr_t)pts;
            break;
        }
        case 'q': {
            int *q = (int *)take(&p, sizeof(int));
            x[i] = r ? (uint32_t)(uintptr_t)q : 0;
            break;
        }
        }
    }
    uint32_t res = 0;
    if (!a->bad)
        res = ((native16)fn)(x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9],
                             x[10], x[11], x[12], x[13], x[14], x[15]);
    for (int i = 0; i < n && !a->bad; i++) {    /* what comes back */
        uint32_t r = raw[i];
        uint32_t next = i + 1 < n ? raw[i + 1] & 0xffffu : 0;
        if (!r)
            continue;
        switch (codes[i]) {
        case 'S': {
            const char *s = (const char *)(uintptr_t)x[i];
            uint32_t len = 0;
            while (len + 1 < next && s[len])
                len++;
            if (next)
                wr(a, r, s, len + 1);
            break;
        }
        case 'b':
            wr(a, r, (const void *)(uintptr_t)x[i], next);
            break;
        case 'r':
            struct_out(a, F_RECT, r, (void *)(uintptr_t)x[i]);
            break;
        case 'p': {
            const POINT *pt = (const POINT *)(uintptr_t)x[i];
            uint8_t g[4];
            wr16(g, (uint16_t)pt->x);
            wr16(g + 2, (uint16_t)pt->y);
            wr(a, r, g, 4);
            break;
        }
        case 'q': {
            uint8_t g[2];
            wr16(g, (uint16_t)*(const int *)(uintptr_t)x[i]);
            wr(a, r, g, 2);
            break;
        }
        }
    }
    win_free(p.mem);
    *v = sig[0] == 'i' ? (uint16_t)res : sig[0] == 'w' ? (uint16_t)res : res;
    *dword = sig[0] == 'd';
    return 1;
}

/* ---------------------------------------------------------------- local heap */

/* LocalAlloc and the rest: blocks in DGROUP's local heap (between its data
 * and its stack, as the NE header sizes them); a handle is the offset. The
 * first 256 bytes are left to the start-up code (Watcom's copies the
 * command line there). */
static int heap_bounds(ne_user *u, win_ne_guest_api_context *c)
{
    if (u->heap_hi)
        return 1;
    const win_ne_guest_image *g = c->image;
    uint32_t top = g->sp ? g->sp : 0x10000u;
    uint32_t hi = top - g->info.stack_size, lo = hi - g->info.heap_size + 256u;
    if (g->info.heap_size <= 256u || lo >= hi)
        return 0;
    u->heap_lo = (uint16_t)lo;
    u->heap_hi = (uint16_t)hi;
    return 1;
}

static uint16_t dgroup_sel(win_ne_guest_api_context *c)
{
    const win_ne_guest_image *g = c->image;
    return g->segments[g->info.auto_data ? g->info.auto_data : g->info.ss_index].selector;
}

static int local_find(ne_user *u, uint16_t h)
{
    for (int i = 0; i < NE_LOCALS; i++)
        if (u->local[i].size && u->local[i].at == h)
            return i;
    return -1;
}

static uint16_t local_alloc(args *a, ne_user *u, uint32_t size)
{
    if (!size || size > 0xfff0u || !heap_bounds(u, a->ctx))
        return 0;
    size = (size + 1) & ~1u;
    uint32_t at = u->heap_lo;
    for (int again = 1; again;) {
        again = 0;
        for (int i = 0; i < NE_LOCALS; i++)
            if (u->local[i].size && at < (uint32_t)u->local[i].at + u->local[i].size &&
                u->local[i].at < at + size) {
                at = (uint32_t)u->local[i].at + u->local[i].size;
                again = 1;
            }
    }
    if (at + size > u->heap_hi)
        return 0;
    for (int i = 0; i < NE_LOCALS; i++)
        if (!u->local[i].size) {
            u->local[i].at = (uint16_t)at;
            u->local[i].size = (uint16_t)size;
            uint8_t zero[64];
            memset(zero, 0, sizeof(zero));
            for (uint32_t k = 0; k < size; k += sizeof(zero))
                win_ne_write(a->ctx, dgroup_sel(a->ctx), at + k, zero,
                             size - k < sizeof(zero) ? size - k : (uint32_t)sizeof(zero));
            return (uint16_t)at;
        }
    return 0;
}

static int local_service(args *a, ne_user *u, uint16_t service, uint32_t *v)
{
    int i;
    switch (service) {
    case 4:                             /* LocalInit(seg, start, end): DGROUP's is there */
        *v = 1;
        return 1;
    case 5:                             /* LocalAlloc(flags, size) */
        *v = local_alloc(a, u, w(a, 4));
        return 1;
    case 6: {                           /* LocalReAlloc(h, size, flags) */
        uint16_t h = w(a, 8), size = w(a, 6);
        *v = 0;
        if ((i = local_find(u, h)) < 0)
            return 1;
        uint16_t old = u->local[i].size;
        u->local[i].size = 0;           /* the new one may overlap it */
        uint16_t n = local_alloc(a, u, size);
        if (!n) {
            u->local[i].size = old;
            return 1;
        }
        uint8_t buf[64];
        uint16_t sel = dgroup_sel(a->ctx);
        uint16_t keep = old < size ? old : size;
        if (n != h)                     /* down: the regions may overlap */
            for (uint32_t k = 0; k < keep; k += sizeof(buf)) {
                uint32_t m = keep - k < sizeof(buf) ? keep - k : (uint32_t)sizeof(buf);
                uint32_t from = n < h ? k : keep - k - m;
                win_ne_read(a->ctx, sel, (uint32_t)h + from, buf, m);
                win_ne_write(a->ctx, sel, (uint32_t)n + from, buf, m);
            }
        *v = n;
        return 1;
    }
    case 7:                             /* LocalFree(h): 0 */
        if ((i = local_find(u, w(a, 4))) >= 0)
            u->local[i].size = 0;
        *v = i >= 0 ? 0 : w(a, 4);
        return 1;
    case 8:                             /* LocalLock(h): the near pointer */
        *v = local_find(u, w(a, 4)) >= 0 ? w(a, 4) : 0;
        return 1;
    case 9:                             /* LocalUnlock */
        *v = 0;
        return 1;
    case 10:                            /* LocalSize */
        *v = (i = local_find(u, w(a, 4))) >= 0 ? u->local[i].size : 0;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- global memory */

static int global_service(args *a, uint16_t service, uint32_t *v, int *dword)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    uint16_t h;
    win_ne_guest_segment *s;
    switch (service) {
    case 15: {                          /* GlobalAlloc(flags, DWORD size): a selector */
        uint32_t size = dw(a, 4);
        *v = a->bad ? 0 : win_ne_guest_global_alloc(g, size ? size : 1);
        return 1;
    }
    case 16:                            /* GlobalReAlloc(h, DWORD size, flags) */
        h = w(a, 10);
        *v = !a->bad && win_ne_guest_global_realloc(g, h, dw(a, 6), a->ctx->read) ? h : 0;
        return 1;
    case 17:                            /* GlobalFree(h): 0 */
        h = w(a, 4);
        *v = win_ne_guest_global_free(g, h) ? 0 : h;
        return 1;
    case 18:                            /* GlobalLock(h): sel:0 */
        h = w(a, 4);
        *v = win_ne_guest_global(g, h) ? (uint32_t)(h & ~7u) << 16 : 0;
        *dword = 1;
        return 1;
    case 19: case 24:                   /* GlobalUnlock, UnlockSegment */
        *v = 0;
        return 1;
    case 20:                            /* GlobalSize(h): DWORD */
        s = win_ne_guest_global(g, w(a, 4));
        *v = s ? s->size : 0;
        *dword = 1;
        return 1;
    case 21:                            /* GlobalHandle(sel): handle and selector */
        h = w(a, 4);
        *v = win_ne_guest_global(g, h) ? (uint32_t)h << 16 | h : 0;
        *dword = 1;
        return 1;
    case 22:                            /* GlobalFlags */
        *v = 0;
        return 1;
    case 23:                            /* LockSegment(sel) */
        *v = w(a, 4);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- pointers in messages */

/* A native pointer a procedure of the program gets as a guest copy (the
 * lParam of WM_CREATE ...): the copy is on its stack while it runs. Its
 * DefWindowProc gets the native one back, brought up to date. */
static int lend_find(ne_user *u, uint32_t far)
{
    for (int i = u->lents - 1; i >= 0; i--)
        if (u->lent[i].far == far)
            return i;
    return -1;
}

/* the Win16 CREATESTRUCT: lpCreateParams, hInstance, hMenu, hwndParent,
 * cy, cx, y, x, style, lpszName, lpszClass, dwExStyle (30 bytes), then
 * the two strings (63 characters at most here) */
static size_t clip(const char *s) { size_t n = s ? strlen(s) : 0; return n < 63 ? n : 63; }

static uint16_t create_size(const CREATESTRUCT *cs)
{
    size_t n = 30 + clip(cs->lpszName) + 1;
    if ((uintptr_t)cs->lpszClass >> 16)
        n += clip(cs->lpszClass) + 1;
    return (uint16_t)((n + 1) & ~1u);
}

static void create_to_guest(ne_user *u, const CREATESTRUCT *cs, uint32_t far, uint8_t *g)
{
    wr32(g, (uint32_t)(uintptr_t)cs->lpCreateParams);
    wr16(g + 4, cs->hInstance == (HINSTANCE)u->task ? dgroup_sel(u->ctx) : (uint16_t)cs->hInstance);
    wr16(g + 6, (uint16_t)cs->hMenu);
    wr16(g + 8, (uint16_t)cs->hwndParent);
    wr16(g + 10, (uint16_t)cs->cy);
    wr16(g + 12, (uint16_t)cs->cx);
    wr16(g + 14, (uint16_t)cs->y);
    wr16(g + 16, (uint16_t)cs->x);
    wr32(g + 18, (uint32_t)cs->style);
    wr32(g + 26, cs->dwExStyle);
    uint32_t n = 30;
    size_t len = clip(cs->lpszName);
    if (len)
        memcpy(g + n, cs->lpszName, len);
    g[n + len] = 0;
    wr32(g + 22, far + n);
    n += (uint32_t)len + 1;
    if ((uintptr_t)cs->lpszClass >> 16) {
        len = clip(cs->lpszClass);
        memcpy(g + n, cs->lpszClass, len);
        g[n + len] = 0;
        wr32(g + 26, far + n);
    } else {
        wr32(g + 26, (uint32_t)(uintptr_t)cs->lpszClass);       /* an atom */
    }
}

/* The program's window (or dialog, subclass) procedure proc with a message:
 * lParam a guest copy where it points to a structure it may read. */
LRESULT ne_message(ne_user *u, uint32_t proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    win_ne_guest_api_context *c = u->ctx;
    uint8_t blob[160];
    uint16_t len = 0;
    const char *fmt = NULL;
    if ((msg == WM_CREATE || msg == WM_NCCREATE) && lp) {
        const CREATESTRUCT *cs = (const CREATESTRUCT *)(uintptr_t)lp;
        len = create_size(cs);
        create_to_guest(u, cs, win_ne_blob_far(c, len), blob);
    } else if (msg == WM_GETMINMAXINFO && lp) {
        fmt = F_MINMAX;
    } else if ((msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED) && lp) {
        fmt = F_WINDOWPOS;
    } else if (msg == WM_NCCALCSIZE && !wp && lp) {
        fmt = F_RECT;
    }
    if (fmt) {
        len = (uint16_t)((guest_size(fmt) + 1) & ~1u);
        pack(fmt, blob, (uint8_t *)(uintptr_t)lp, 0);
    }
    if (!len || u->lents >= NE_LENT)
        return (LRESULT)(int32_t)win_ne_callback(c, proc, (uint16_t)h, (uint16_t)msg,
                                                 (uint16_t)wp, (uint32_t)lp);
    uint32_t far = win_ne_blob_far(c, len);
    u->lent[u->lents].far = far;
    u->lent[u->lents].native = (void *)(uintptr_t)lp;
    u->lent[u->lents++].msg = msg;
    const uint16_t words[5] = { (uint16_t)h, (uint16_t)msg, (uint16_t)wp,
                                (uint16_t)(far >> 16), (uint16_t)far };
    LRESULT r = (LRESULT)(int32_t)win_ne_call(c, proc, words, 5, blob, len);
    u->lents--;
    if (fmt)                            /* what the procedure set in its copy */
        pack(fmt, blob, (uint8_t *)(uintptr_t)lp, 1);
    return r;
}

/* lParam from the program for a native procedure: a copy lent to it, back
 * to the native structure (updated from the copy); restore after */
static LPARAM lp_native(args *a, ne_user *u, UINT msg, uint32_t lp, int *lent)
{
    *lent = lend_find(u, lp);
    if (*lent < 0)
        return (LPARAM)lp;
    const char *fmt = msg == WM_GETMINMAXINFO ? F_MINMAX :
                      msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED ? F_WINDOWPOS :
                      msg == WM_NCCALCSIZE ? F_RECT : NULL;
    if (fmt)
        struct_in(a, fmt, lp, u->lent[*lent].native);
    return (LPARAM)(uintptr_t)u->lent[*lent].native;
}

static void lp_back(args *a, ne_user *u, UINT msg, uint32_t lp, int lent)
{
    if (lent < 0)
        return;
    const char *fmt = msg == WM_GETMINMAXINFO ? F_MINMAX :
                      msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED ? F_WINDOWPOS :
                      msg == WM_NCCALCSIZE ? F_RECT : NULL;
    if (fmt)
        struct_out(a, fmt, lp, u->lent[lent].native);
}

LRESULT ne_defproc(args *a, ne_user *u, LRESULT (WINAPI *fn)(HWND, UINT, WPARAM, LPARAM),
                   HWND h, UINT msg, WPARAM wp, uint32_t lp)
{
    int lent;
    LPARAM l = lp_native(a, u, msg, lp, &lent);
    LRESULT r = fn(h, msg, wp, l);
    lp_back(a, u, msg, lp, lent);
    return r;
}

/* Win16 values (WM_USER + n) of the control messages with pointers */
enum {
    EM_GETRECT_ = 0x402, EM_SETRECT_ = 0x403, EM_REPLACESEL_ = 0x412, EM_GETLINE_ = 0x414,
    LB_ADDSTRING_ = 0x401, LB_INSERTSTRING_ = 0x402, LB_GETTEXT_ = 0x40a,
    LB_SELECTSTRING_ = 0x40d, LB_DIR_ = 0x40e, LB_FINDSTRING_ = 0x410, LB_GETITEMRECT_ = 0x419,
    CB_ADDSTRING_ = 0x403, CB_DIR_ = 0x405, CB_GETLBTEXT_ = 0x408, CB_INSERTSTRING_ = 0x40a,
    CB_FINDSTRING_ = 0x40c, CB_SELECTSTRING_ = 0x40d
};

/* SendMessage to a window of the program's own: lParam as it is (its
 * procedure gets its own pointer). To another (a control): a string in, a
 * buffer out, a rectangle, made native. */
static __attribute__((noinline)) uint32_t send(args *a, ne_user *u, HWND h, UINT msg, WPARAM wp,
                                               uint32_t lp, int post)
{
    if (!h || ne_own_window(u, h) || !(lp >> 16) || lend_find(u, lp) >= 0) {
        if (post)
            return (uint32_t)PostMessage(h, msg, wp, (LPARAM)lp);
        int lent;
        LPARAM l = lp_native(a, u, msg, lp, &lent);
        uint32_t r = (uint32_t)SendMessage(h, msg, wp, l);
        lp_back(a, u, msg, lp, lent);
        return r;
    }
    char cls[16];
    GetClassName(h, cls, sizeof(cls));
    int lb = !lstrcmpi(cls, "LISTBOX"), cb = !lstrcmpi(cls, "COMBOBOX"), ed = !lstrcmpi(cls, "EDIT");
    enum { NONE, IN, OUT, LINE, RECT_IN, RECT_OUT } kind = NONE;
    uint32_t size = 0;
    if (msg == WM_SETTEXT)
        kind = IN;
    else if (msg == WM_GETTEXT)
        kind = OUT, size = wp;
    else if (lb && (msg == LB_ADDSTRING_ || msg == LB_INSERTSTRING_ || msg == LB_SELECTSTRING_ ||
                    msg == LB_DIR_ || msg == LB_FINDSTRING_))
        kind = IN;
    else if (lb && msg == LB_GETTEXT_)
        kind = OUT, size = (uint32_t)SendMessage(h, LB_GETTEXTLEN, wp, 0) + 1u;
    else if (lb && msg == LB_GETITEMRECT_)
        kind = RECT_OUT;
    else if (cb && (msg == CB_ADDSTRING_ || msg == CB_INSERTSTRING_ || msg == CB_FINDSTRING_ ||
                    msg == CB_SELECTSTRING_ || msg == CB_DIR_))
        kind = IN;
    else if (cb && msg == CB_GETLBTEXT_)
        kind = OUT, size = (uint32_t)SendMessage(h, CB_GETLBTEXTLEN, wp, 0) + 1u;
    else if (ed && msg == EM_REPLACESEL_)
        kind = IN;
    else if (ed && msg == EM_GETLINE_)
        kind = LINE;
    else if (ed && msg == EM_GETRECT_)
        kind = RECT_OUT;
    else if (ed && msg == EM_SETRECT_)
        kind = RECT_IN;
    if (kind == NONE)
        return post ? (uint32_t)PostMessage(h, msg, wp, (LPARAM)lp)
                    : (uint32_t)SendMessage(h, msg, wp, (LPARAM)lp);
    uint32_t r = 0;
    if (kind == IN) {
        char *s = (char *)win_alloc(1024);
        if (s && str(a, lp, s, 1024))
            r = (uint32_t)SendMessage(h, msg, wp, (LPARAM)(uintptr_t)s);
        win_free(s);
    } else if (kind == OUT || kind == LINE) {
        if (kind == LINE) {                 /* the first word: the buffer's size */
            uint8_t g[2];
            if (!rd(a, lp, g, 2))
                return 0;
            size = rd16(g);
        }
        if (size > 4096)
            size = 4096;
        char *s = (char *)win_alloc(size + 2);
        if (!s)
            return 0;
        if (kind == LINE)
            wr16((uint8_t *)s, (uint16_t)size);
        r = (uint32_t)SendMessage(h, msg, wp, (LPARAM)(uintptr_t)s);
        uint32_t n = kind == LINE ? (r < size ? r : size) : (r < size ? r + 1u : size);
        if (kind != LINE && n)
            s[n - 1] = s[n - 1] && r >= size ? 0 : s[n - 1];
        if (n)
            wr(a, lp, s, n);
        win_free(s);
    } else {
        RECT rc;
        memset(&rc, 0, sizeof(rc));
        if (kind == RECT_IN)
            struct_in(a, F_RECT, lp, &rc);
        r = (uint32_t)SendMessage(h, msg, wp, (LPARAM)(uintptr_t)&rc);
        if (kind == RECT_OUT)
            struct_out(a, F_RECT, lp, &rc);
    }
    return r;
}

/* ---------------------------------------------------------------- timers, enumerations, subclassing */

static void CALLBACK ne_timerproc(HWND h, UINT msg, UINT id, DWORD time)
{
    ne_user *u = ne_user_of(h ? GetWindowTask(h) : GetCurrentTask());
    for (int i = 0; u && i < NE_TIMERS; i++)
        if (u->timer[i].proc && u->timer[i].hwnd == h && u->timer[i].id == id) {
            win_ne_callback(u->ctx, u->timer[i].proc, (uint16_t)h, (uint16_t)msg, (uint16_t)id,
                            time);
            return;
        }
}

typedef struct {
    ne_user *u;
    uint32_t proc;
    uint32_t lp;
} enum_ctx;

static BOOL CALLBACK ne_enumproc(HWND h, LPARAM lp)
{
    const enum_ctx *e = (const enum_ctx *)(uintptr_t)lp;
    const uint16_t words[3] = { (uint16_t)h, (uint16_t)(e->lp >> 16), (uint16_t)e->lp };
    return (uint16_t)win_ne_call(e->u->ctx, e->proc, words, 3, NULL, 0) != 0;
}

/* a window the program subclassed with a procedure of its own */
static LRESULT CALLBACK ne_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ne_user *u = ne_user_of(GetWindowTask(h));
    for (int i = 0; u && i < NE_SUBCLASSED; i++)
        if (u->sub[i].hwnd == h) {
            WNDPROC old = u->sub[i].old;
            if (u->ctx->dead || u->ctx->exited || u->ctx->stopped)
                return CallWindowProc(old, h, msg, wp, lp);
            LRESULT r = ne_message(u, u->sub[i].proc, h, msg, wp, lp);
            if (msg == WM_NCDESTROY)
                u->sub[i].hwnd = 0;
            return r;
        }
    return DefWindowProc(h, msg, wp, lp);
}

/* Native window procedures the program holds (GetWindowLong GWL_WNDPROC):
 * FFFF:n, n the number here, for CallWindowProc. */
static WNDPROC tokens[32];

static uint32_t token_of(WNDPROC p)
{
    for (unsigned i = 0; i < 32; i++)
        if (tokens[i] == p || !tokens[i]) {
            tokens[i] = p;
            return 0xffff0000u | i;
        }
    return 0;
}

static WNDPROC token_proc(uint32_t t)
{
    return (t >> 16) == 0xffffu && (t & 0xffffu) < 32 ? tokens[t & 0xffffu] : NULL;
}

/* the window procedure of a window, as the program sees it */
static uint32_t wndproc_of(ne_user *u, HWND h)
{
    for (int i = 0; i < NE_SUBCLASSED; i++)
        if (u->sub[i].hwnd == h)
            return u->sub[i].proc;
    char cls[32];
    GetClassName(h, cls, sizeof(cls));
    for (int i = 0; i < NE_CLASSES; i++)
        if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, cls))
            return u->cls[i].proc;
    return token_of((WNDPROC)(intptr_t)GetWindowLong(h, GWL_WNDPROC));
}

static uint32_t set_wndproc(ne_user *u, HWND h, uint32_t proc)
{
    uint32_t old = wndproc_of(u, h);
    WNDPROC native = token_proc(proc);
    if (native) {                       /* back to a native one */
        for (int i = 0; i < NE_SUBCLASSED; i++)
            if (u->sub[i].hwnd == h)
                u->sub[i].hwnd = 0;
        SetWindowLong(h, GWL_WNDPROC, (LONG)(intptr_t)native);
        return old;
    }
    for (int i = 0; i < NE_SUBCLASSED; i++)
        if (u->sub[i].hwnd == h) {
            u->sub[i].proc = proc;
            return old;
        }
    for (int i = 0; i < NE_SUBCLASSED; i++)
        if (!u->sub[i].hwnd) {
            u->sub[i].hwnd = h;
            u->sub[i].proc = proc;
            u->sub[i].old = (WNDPROC)(intptr_t)SetWindowLong(h, GWL_WNDPROC,
                                                             (LONG)(intptr_t)ne_subclass);
            return old;
        }
    return 0;
}

/* ---------------------------------------------------------------- the services */

int ne_call(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    win_ne_guest_api_context *c = a->ctx;
    if (service < 0x1000u && (local_service(a, u, service, v) || global_service(a, service, v, dword)))
        return 1;
    switch (service) {
    case 0x100a: {                      /* SetTimer(HWND, UINT, UINT, TIMERPROC) */
        uint32_t proc = dw(a, 4);
        UINT ms = w(a, 8), id = w(a, 10);
        HWND h = w(a, 12);
        if (a->bad)
            return 1;
        if (!proc) {
            *v = (uint16_t)SetTimer(h, id, ms, NULL);
            return 1;
        }
        int i = 0;
        while (i < NE_TIMERS && u->timer[i].proc)
            i++;
        if (i == NE_TIMERS)
            return 1;
        UINT r = SetTimer(h, id, ms, ne_timerproc);
        if (r) {
            u->timer[i].hwnd = h;
            u->timer[i].id = h ? id : r;
            u->timer[i].proc = proc;
        }
        *v = (uint16_t)r;
        return 1;
    }
    case 0x100c: {                      /* KillTimer(HWND, UINT) */
        UINT id = w(a, 4);
        HWND h = w(a, 6);
        for (int i = 0; i < NE_TIMERS; i++)
            if (u->timer[i].proc && u->timer[i].hwnd == h && u->timer[i].id == id)
                u->timer[i].proc = 0;
        *v = (uint16_t)KillTimer(h, id);
        return 1;
    }
    case 0x101e: {                      /* WindowFromPoint(POINT) */
        POINT pt = { (int16_t)w(a, 4), (int16_t)w(a, 6) };
        *v = WindowFromPoint(pt);
        return 1;
    }
    case 0x10bf: {                      /* ChildWindowFromPoint(HWND, POINT) */
        POINT pt = { (int16_t)w(a, 4), (int16_t)w(a, 6) };
        *v = ChildWindowFromPoint(w(a, 8), pt);
        return 1;
    }
    case 0x104c: {                      /* PtInRect(const RECT FAR *, POINT) */
        POINT pt = { (int16_t)w(a, 4), (int16_t)w(a, 6) };
        RECT rc;
        *v = struct_in(a, F_RECT, dw(a, 8), &rc) ? (uint16_t)PtInRect(&rc, pt) : 0;
        return 1;
    }
    case 0x1036: case 0x1037: {         /* EnumWindows(proc, LPARAM),
                                         * EnumChildWindows(HWND, proc, LPARAM) */
        enum_ctx e = { u, dw(a, 8), dw(a, 4) };
        if (a->bad)
            return 1;
        *v = (uint16_t)(service == 0x1036 ? EnumWindows(ne_enumproc, (LPARAM)(uintptr_t)&e)
                                          : EnumChildWindows(w(a, 12), ne_enumproc,
                                                             (LPARAM)(uintptr_t)&e));
        return 1;
    }
    case 0x105a: {                      /* IsDialogMessage(HWND, LPMSG) */
        MSG m;
        if (msg_from_guest(a, dw(a, 4), &m))
            *v = (uint16_t)IsDialogMessage(w(a, 8), &m);
        return 1;
    }
    case 0x1065:                        /* SendDlgItemMessage(HWND, int, UINT, WPARAM, LPARAM) */
        *v = send(a, u, GetDlgItem(w(a, 14), (int16_t)w(a, 12)), w(a, 10), w(a, 8), dw(a, 4), 0);
        *dword = 1;
        return 1;
    case 0x106d: {                      /* PeekMessage(LPMSG, HWND, UINT, UINT, UINT) */
        uint32_t p = dw(a, 12);
        UINT remove = w(a, 4), hi = w(a, 6), lo = w(a, 8);
        HWND h = w(a, 10);
        MSG m;
        if (a->bad)
            return 1;
        *v = (uint16_t)PeekMessage(&m, h, lo, hi, remove);
        if (*v) {
            uint8_t g[18];
            msg_to_guest(&m, g);
            wr(a, p, g, sizeof(g));
        }
        return 1;
    }
    case 0x106e:                        /* PostMessage(HWND, UINT, WPARAM, LPARAM) */
        *v = send(a, u, w(a, 12), w(a, 10), w(a, 8), dw(a, 4), 1);
        return 1;
    case 0x106f:                        /* SendMessage(HWND, UINT, WPARAM, LPARAM) */
        *v = send(a, u, w(a, 12), w(a, 10), w(a, 8), dw(a, 4), 0);
        *dword = 1;
        return 1;
    case 0x107a: {                      /* CallWindowProc(proc, HWND, UINT, WPARAM, LPARAM) */
        uint32_t proc = dw(a, 14), lp = dw(a, 4);
        HWND h = w(a, 12);
        UINT msg = w(a, 10);
        WPARAM wp = w(a, 8);
        WNDPROC native = token_proc(proc);
        if (a->bad)
            return 1;
        if (native) {
            int lent;
            LPARAM l = lp_native(a, u, msg, lp, &lent);
            *v = (uint32_t)CallWindowProc(native, h, msg, wp, l);
            lp_back(a, u, msg, lp, lent);
        }
        else if (lend_find(u, lp) >= 0 || !(lp >> 16))
            *v = win_ne_callback(c, proc, (uint16_t)h, (uint16_t)msg, (uint16_t)wp, lp);
        else
            *v = (uint32_t)ne_message(u, proc, h, msg, wp, (LPARAM)lp);
        *dword = 1;
        return 1;
    }
    case 0x1081: case 0x1083: {         /* GetClassWord, GetClassLong(HWND, int) */
        int idx = (int16_t)w(a, 4);
        HWND h = w(a, 6);
        if (service == 0x1081)
            *v = GetClassWord(h, idx);
        else
            *v = (uint32_t)GetClassLong(h, idx), *dword = 1;
        return 1;
    }
    case 0x1085: case 0x1087: {         /* GetWindowWord, GetWindowLong(HWND, int) */
        int idx = (int16_t)w(a, 4);
        HWND h = w(a, 6);
        if (service == 0x1087 && idx == GWL_WNDPROC)
            *v = wndproc_of(u, h);
        else if (idx == GWW_HINSTANCE)
            *v = GetWindowWord(h, idx) == (WORD)u->task ? dgroup_sel(c) : GetWindowWord(h, idx);
        else
            *v = service == 0x1085 ? GetWindowWord(h, idx) : (uint32_t)GetWindowLong(h, idx);
        *dword = service == 0x1087;
        return 1;
    }
    case 0x1086:                        /* SetWindowWord(HWND, int, WORD) */
        *v = SetWindowWord(w(a, 8), (int16_t)w(a, 6), w(a, 4));
        return 1;
    case 0x1088: {                      /* SetWindowLong(HWND, int, LONG) */
        uint32_t val = dw(a, 4);
        int idx = (int16_t)w(a, 8);
        HWND h = w(a, 10);
        if (a->bad)
            return 1;
        *v = idx == GWL_WNDPROC ? set_wndproc(u, h, val) : (uint32_t)SetWindowLong(h, idx, (LONG)val);
        *dword = 1;
        return 1;
    }
    case 0x1134:                        /* DefDlgProc(HWND, UINT, WPARAM, LPARAM) */
        *v = (uint32_t)ne_defproc(a, u, DefDlgProc, w(a, 12), w(a, 10), w(a, 8), dw(a, 4));
        *dword = 1;
        return 1;
    case 0x11a5:                        /* wvsprintf(LPSTR, LPCSTR, LPSTR args) */
        *v = (uint16_t)ne_format(a, dw(a, 12), dw(a, 8), dw(a, 4));
        return 1;
    case 0x11af: case 0x11b0: {         /* AnsiUpper, AnsiLower(LPSTR or a char) */
        uint32_t p = dw(a, 4);
        int up = service == 0x11af;
        if (!(p >> 16)) {
            char s[2] = { (char)p, 0 };
            up ? AnsiUpper(s) : AnsiLower(s);
            *v = (uint8_t)s[0];
        } else {
            char s[512];
            if (str(a, p, s, sizeof(s))) {
                up ? AnsiUpper(s) : AnsiLower(s);
                wr(a, p, s, (uint32_t)strlen(s));
            }
            *v = p;
        }
        *dword = 1;
        return 1;
    }
    case 0x11d8: {                      /* AnsiNext(LPCSTR) */
        uint32_t p = dw(a, 4);
        char ch = 0;
        rd(a, p, &ch, 1);
        *v = ch ? p + 1 : p;
        *dword = 1;
        return 1;
    }
    case 0x11d9: {                      /* AnsiPrev(LPCSTR start, LPCSTR cur) */
        uint32_t cur = dw(a, 4), start = dw(a, 8);
        *v = (cur & 0xffffu) > (start & 0xffffu) ? cur - 1 : start;
        *dword = 1;
        return 1;
    }
    case 0x3032: case 0x3039: case 0x303e: {    /* CreateBrush/Font/PenIndirect */
        uint32_t p = dw(a, 4);
        union { LOGBRUSH b; LOGFONT f; LOGPEN p; } l;
        memset(&l, 0, sizeof(l));
        if (service == 0x3032 && struct_in(a, F_LOGBRUSH, p, &l.b))
            *v = CreateBrushIndirect(&l.b);
        else if (service == 0x3039 && struct_in(a, F_LOGFONT, p, &l.f))
            *v = CreateFontIndirect(&l.f);
        else if (service == 0x303e && struct_in(a, F_LOGPEN, p, &l.p))
            *v = CreatePenIndirect(&l.p);
        return 1;
    }
    case 0x3052: {                      /* GetObject(HGDIOBJ, int, LPVOID) */
        uint32_t p = dw(a, 4);
        int size = (int16_t)w(a, 8);
        HGDIOBJ h = w(a, 10);
        union { LOGBRUSH b; LOGFONT f; LOGPEN p; BITMAP m; uint8_t raw[64]; } l;
        memset(&l, 0, sizeof(l));
        int n = GetObject(h, sizeof(l), &l);
        const char *fmt = n == (int)sizeof(LOGPEN) ? F_LOGPEN : n == (int)sizeof(LOGBRUSH) ? F_LOGBRUSH :
                          n == (int)sizeof(LOGFONT) ? F_LOGFONT : n == (int)sizeof(BITMAP) ? F_BITMAP : NULL;
        if (!fmt || !p)
            return 1;
        uint8_t g[64];
        uint32_t gs = guest_size(fmt);
        pack(fmt, g, l.raw, 0);
        if (size > (int)gs)
            size = (int)gs;
        if (size > 0 && wr(a, p, g, (uint32_t)size))
            *v = (uint16_t)size;
        return 1;
    }
    case 0x305d: {                      /* GetTextMetrics(HDC, LPTEXTMETRIC) */
        TEXTMETRIC tm;
        uint32_t p = dw(a, 4);
        if (GetTextMetrics(w(a, 8), &tm) && struct_out(a, F_TEXTMETRIC, p, &tm))
            *v = 1;
        return 1;
    }
    }
    const char *sig = win_ne_generic_sig(service);
    return sig ? generic(a, u, service, sig, v, dword) : 0;
}

void ne_call_end(ne_user *u)
{
    for (int i = 0; i < NE_TIMERS; i++)
        if (u->timer[i].proc)
            KillTimer(u->timer[i].hwnd, u->timer[i].id);
}

#endif
