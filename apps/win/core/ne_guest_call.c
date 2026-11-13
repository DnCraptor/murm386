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

/* USER's keyboard layout is shared by native and NE applications. */
extern int user_keyboard_layout(void);
extern void user_set_keyboard_layout(int russian);

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
#define F_PLACEMENT  "uuuiiiiiiii"

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
                /* A 16:16 pointer must never carry its offset into the selector. */
                uint32_t off = (r & 0xffffu) + 4u * k;
                if (off > 0xfffcu) {
                    a->bad = 1;
                    break;
                }
                if (!rd(a, (r & 0xffff0000u) | off, g, 4))
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
            /* Profile key enumeration returns a MULTI_SZ, not a single ASCIIZ.
             * Include the final extra NUL instead of stopping at the first key.
             * Both WIN.INI's GetProfileString and GetPrivateProfileString use it,
             * and the sections' names (Get(Private)ProfileSectionNames). */
            if (((service == 58u || service == 128u) && i == 3 && !raw[1]) ||
                ((service == 142u || service == 143u) && i == 0)) {
                uint32_t count = res < next ? res + 1u : next;
                if (count < 2u && next >= 2u)
                    count = 2u;
                if (count)
                    wr(a, r, s, count);
                break;
            }
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

/* LocalAlloc and the rest: blocks in a local heap, the caller's DS's (the
 * program's DGROUP, or a DLL's); a handle is the offset. DGROUP's is
 * between its data and its stack, as the NE header sizes it, then beyond
 * the stack: DGROUP grows, up to 64K, as Windows grows it. Its first 256
 * bytes are left to the start-up code (Watcom's copies the command line
 * there). A DLL's is where its LocalInit puts it (after its data, by
 * default: the end of its DGROUP), then grows the same way. */
static uint16_t dgroup_index(win_ne_guest_api_context *c)
{
    const win_ne_guest_image *g = c->image;
    return g->info.auto_data ? g->info.auto_data : g->info.ss_index;
}

static uint16_t dgroup_sel(win_ne_guest_api_context *c)
{
    return c->image->segments[dgroup_index(c)].selector;
}

/* the heap of the caller's DS (made at the first call: DGROUP's from the
 * header, a DLL's at its segment's end), -1 if none can be */
static int heap_of(args *a, ne_user *u, uint16_t ds)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    uint16_t own = dgroup_sel(a->ctx);
    if (!ds || win_ne_guest_dll_of(g, ds) < 0)
        ds = own;
    int free_slot = -1;
    for (int i = 0; i < NE_HEAPS; i++) {
        if (u->heap[i].sel && (u->heap[i].sel & ~3u) == (ds & ~3u))
            return i;
        if (!u->heap[i].sel && free_slot < 0)
            free_slot = i;
    }
    win_ne_guest_segment *s = win_ne_guest_segment_of(g, ds);
    if (free_slot < 0 || !s)
        return -1;
    uint32_t lo, hi, end;
    if ((ds & ~3u) == (own & ~3u)) {
        end = g->sp ? g->sp : s->size;
        hi = end - g->info.stack_size;
        lo = hi - g->info.heap_size + 256u;
        if (g->info.heap_size <= 256u || lo >= hi)
            lo = hi;                    /* none there: beyond the stack only */
    } else {                            /* a DLL's: its header's size at the end */
        const win_ne_guest_dll *d = &g->dlls[win_ne_guest_dll_of(g, ds)];
        end = hi = s->size;
        lo = d->heap_size < hi ? hi - d->heap_size : hi;
    }
    u->heap[free_slot].sel = ds;
    u->heap[free_slot].lo = (uint16_t)lo;
    u->heap[free_slot].hi = (uint16_t)hi;
    u->heap[free_slot].end = (uint16_t)(end > 0xfff0u ? 0xfff0u : end);
    return free_slot;
}

/* The local heaps' blocks, as Windows 3.1's: a fixed block's handle is its
 * near pointer (a multiple of 4); a moveable one's is the place of its
 * handle table entry (2 more than a multiple of 4): the block's near
 * pointer (0 when discarded), its lock count, its flags (LMEM_DISCARDABLE
 * and LMEM_DISCARDED >> 8), which a program may read itself (*(PSTR *)h).
 * A block moves only as LocalReAlloc grows it, its entry kept up to date. */
enum { L_FREE, L_FIXED, L_MOVEABLE, L_ENTRY };
#define LHE_DISCARDED 0x40u

/* a record of the program's; NULL if it has no room for more */
static ne_local *local_new(ne_user *u)
{
    for (int i = 0; i < u->nlocals; i++)
        if (u->local[i].kind == L_FREE)
            return &u->local[i];
    int n = u->nlocals ? u->nlocals * 2 : 64;
    if (n > 4096)
        return NULL;
    ne_local *l = (ne_local *)win_realloc(u->local, sizeof(ne_local) * (size_t)n);
    if (!l)
        return NULL;
    memset(l + u->nlocals, 0, sizeof(ne_local) * (size_t)(n - u->nlocals));
    u->local = l;
    ne_local *r = &l[u->nlocals];
    u->nlocals = n;
    return r;
}

/* a block by its handle (a moveable one's entry, a fixed one's pointer) */
static ne_local *local_of(ne_user *u, int heap, uint16_t h)
{
    if (!h)
        return NULL;
    int moveable = (h & 3u) == 2u;
    for (int i = 0; i < u->nlocals; i++) {
        ne_local *l = &u->local[i];
        if (l->heap == heap && (moveable ? l->kind == L_MOVEABLE && l->handle == h
                                         : l->kind == L_FIXED && l->at == h))
            return l;
    }
    return NULL;
}

/* the first place for size bytes in [lo, hi) clear of the blocks, at a
 * multiple of 4 plus off, or 0 */
static uint32_t local_fit(ne_user *u, int heap, uint32_t lo, uint32_t hi, uint32_t size,
                          uint32_t off)
{
    uint32_t at = ((lo + 3u - off) & ~3u) + off;
    for (int again = 1; again;) {
        again = 0;
        for (int i = 0; i < u->nlocals; i++) {
            const ne_local *l = &u->local[i];
            if (l->kind != L_FREE && l->size && l->heap == heap &&
                at < (uint32_t)l->at + l->size && l->at < at + size) {
                at = (((uint32_t)l->at + l->size + 3u - off) & ~3u) + off;
                again = 1;
            }
        }
    }
    return at && at + size <= hi ? at : 0;
}

/* the heap's segment at least end bytes (in 1K steps, 64K at most) */
static int local_room(args *a, ne_user *u, int heap, uint32_t end)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    uint16_t sel = u->heap[heap].sel;
    win_ne_guest_segment *d = win_ne_guest_segment_of(g, sel);
    if (!d)
        return 0;
    if (end <= d->size)
        return 1;
    uint32_t want = (end + 0x3ffu) & ~0x3ffu;
    if (want > 0x10000u)
        want = 0x10000u;
    return win_ne_guest_grow(g, sel, want, a->ctx->read);
}

static void local_zero(args *a, uint16_t sel, uint32_t at, uint32_t size)
{
    uint8_t zero[64];
    memset(zero, 0, sizeof(zero));
    for (uint32_t k = 0; k < size; k += sizeof(zero))
        win_ne_write(a->ctx, sel, at + k, zero,
                     size - k < sizeof(zero) ? size - k : (uint32_t)sizeof(zero));
}

/* size bytes (zeroed) of the heap: their place, 0 if there is none; in
 * the heap, then beyond its segment's end (the segment made big enough,
 * in 1K steps, its offsets below 0xFFF0) */
static uint16_t local_place(args *a, ne_user *u, int heap, uint32_t size, uint32_t off)
{
    uint32_t at = local_fit(u, heap, u->heap[heap].lo, u->heap[heap].hi, size, off);
    if (!at && (at = local_fit(u, heap, u->heap[heap].end, 0xfff0u, size, off)) != 0 &&
        !local_room(a, u, heap, at + size))
        at = 0;
    if (at)
        local_zero(a, u->heap[heap].sel, at, size);
    return (uint16_t)at;
}

/* a block's record, its place taken; NULL if there is no room */
static ne_local *local_block(args *a, ne_user *u, int heap, uint32_t size, int kind)
{
    ne_local *l = local_new(u);
    if (!l)
        return NULL;
    uint16_t at = local_place(a, u, heap, size, kind == L_ENTRY ? 2u : 0u);
    if (!at)
        return NULL;
    l->kind = (uint8_t)kind;
    l->heap = (uint8_t)heap;
    l->at = at;
    l->size = (uint16_t)size;
    l->handle = 0;
    return l;
}

/* a moveable block's entry: its pointer, lock count and flags */
static void entry_write(args *a, ne_user *u, const ne_local *l, int lock, unsigned flags)
{
    uint8_t e[4] = { (uint8_t)l->at, (uint8_t)(l->at >> 8), (uint8_t)lock, (uint8_t)flags };
    win_ne_write(a->ctx, u->heap[l->heap].sel, l->handle, e, sizeof(e));
}

static void entry_read(args *a, ne_user *u, const ne_local *l, uint8_t e[4])
{
    memset(e, 0, 4);
    win_ne_read(a->ctx, u->heap[l->heap].sel, l->handle, e, 4);
}

/* the entry's own block */
static ne_local *entry_of(ne_user *u, const ne_local *l)
{
    for (int i = 0; i < u->nlocals; i++)
        if (u->local[i].kind == L_ENTRY && u->local[i].heap == l->heap &&
            u->local[i].at == l->handle)
            return &u->local[i];
    return NULL;
}

/* LocalAlloc(flags, size): a fixed block's pointer, a moveable one's handle */
static uint16_t local_alloc(args *a, ne_user *u, int heap, uint16_t flags, uint32_t size)
{
    if (size > 0xfff0u || heap < 0)
        return 0;
    size = (size + 1) & ~1u;
    if (!(flags & LMEM_MOVEABLE)) {
        ne_local *l = size ? local_block(a, u, heap, size, L_FIXED) : NULL;
        return l ? l->at : 0;
    }
    ne_local *e = local_block(a, u, heap, 4, L_ENTRY);
    if (!e)
        return 0;
    /* the records may move as they grow: the entry's by its index */
    int ei = (int)(e - u->local);
    ne_local *l = local_new(u);
    uint16_t at = l && size ? local_place(a, u, heap, size, 0) : 0;
    e = &u->local[ei];
    if (!l || (size && !at)) {
        e->kind = L_FREE;
        return 0;
    }
    l->kind = L_MOVEABLE;
    l->heap = (uint8_t)heap;
    l->at = at;
    l->size = (uint16_t)size;
    l->handle = e->at;
    entry_write(a, u, l, 0, ((flags & LMEM_DISCARDABLE) ? 0x0fu : 0u) |
                            (size ? 0u : LHE_DISCARDED));
    return l->handle;
}

/* the bytes of a heap's blocks */
static uint32_t local_used(ne_user *u, int heap)
{
    uint32_t n = 0;
    for (int i = 0; i < u->nlocals; i++)
        if (u->local[i].kind != L_FREE && u->local[i].heap == heap)
            n += u->local[i].size;
    return n;
}

/* a moveable block moved to at (its bytes, its entry's pointer) */
static void local_move(args *a, ne_user *u, ne_local *o, uint16_t at)
{
    uint16_t sel = u->heap[o->heap].sel;
    uint8_t buf[64], e[4];
    for (uint32_t k = 0; k < o->size; k += sizeof(buf)) {
        uint32_t m = o->size - k < sizeof(buf) ? o->size - k : (uint32_t)sizeof(buf);
        win_ne_read(a->ctx, sel, (uint32_t)o->at + k, buf, m);
        win_ne_write(a->ctx, sel, (uint32_t)at + k, buf, m);
    }
    entry_read(a, u, o, e);
    o->at = at;
    entry_write(a, u, o, e[2], e[3]);
}

/* l made want bytes long where it is, as Windows' heap makes room: the
 * unlocked moveable blocks in the way moved elsewhere (those it cannot
 * place, discardable ones, discarded); 0 if a fixed or locked block (or
 * an entry) is in the way, or there is no room for them */
static int local_make_room(args *a, ne_user *u, int heap, ne_local *l, uint32_t want,
                           uint32_t hi)
{
    uint32_t from = (uint32_t)l->at + l->size, to = (uint32_t)l->at + want;
    if (to > hi || (to > u->heap[heap].end && !local_room(a, u, heap, to)))
        return 0;
    for (int i = 0; i < u->nlocals; i++) {          /* all of them movable? */
        ne_local *o = &u->local[i];
        uint8_t e[4];
        if (o == l || o->kind == L_FREE || !o->size || o->heap != heap ||
            o->at >= to || (uint32_t)o->at + o->size <= from)
            continue;
        if (o->kind != L_MOVEABLE)
            return 0;
        entry_read(a, u, o, e);
        if (e[2])
            return 0;
    }
    uint16_t size_was = l->size;
    l->size = (uint16_t)want;                       /* the room claimed */
    for (int i = 0; i < u->nlocals; i++) {
        ne_local *o = &u->local[i];
        if (o == l || o->kind != L_MOVEABLE || !o->size || o->heap != heap ||
            o->at >= to || (uint32_t)o->at + o->size <= from)
            continue;
        uint16_t at = local_place(a, u, heap, o->size, 0);
        if (at) {
            local_move(a, u, o, at);
            continue;
        }
        uint8_t e[4];
        entry_read(a, u, o, e);
        if (!(e[3] & 0x0fu)) {                      /* not discardable: no room */
            l->size = size_was;
            return 0;
        }
        o->at = 0;                                  /* discarded */
        o->size = 0;
        entry_write(a, u, o, 0, e[3] | LHE_DISCARDED);
    }
    l->size = size_was;
    return 1;
}

/* LocalReAlloc(h, size, flags): the handle (a moved fixed block's new
 * pointer), 0 if it fails */
static uint16_t local_realloc(args *a, ne_user *u, int heap, uint16_t h, uint32_t want,
                              uint16_t flags)
{
    ne_local *l = local_of(u, heap, h);
    if (!l)
        return 0;
    int moveable = l->kind == L_MOVEABLE;
    uint8_t e[4] = { 0, 0, 0, 0 };
    if (moveable)
        entry_read(a, u, l, e);
    if (flags & LMEM_MODIFY) {              /* its flags only */
        if (moveable && (flags & LMEM_DISCARDABLE))
            entry_write(a, u, l, e[2], (e[3] & LHE_DISCARDED) | 0x0fu);
        else if (moveable)
            entry_write(a, u, l, e[2], e[3] & LHE_DISCARDED);
        return h;
    }
    if (!want && moveable) {                /* discarded (LocalDiscard): unlocked only */
        if (e[2])
            return 0;
        l->at = 0;
        l->size = 0;
        entry_write(a, u, l, 0, e[3] | LHE_DISCARDED);
        return h;
    }
    want = want ? (want + 1) & ~1u : 2;
    if (want > 0xfff0u)
        return 0;
    uint16_t sel = u->heap[heap].sel;
    if (moveable && !l->size) {             /* a discarded one: made again */
        uint16_t at = local_place(a, u, heap, want, 0);
        if (!at)
            return 0;
        l->at = at;
        l->size = (uint16_t)want;
        entry_write(a, u, l, e[2], e[3] & ~LHE_DISCARDED);
        return h;
    }
    uint32_t old = l->size, tail = (uint32_t)l->at + old;
    if (want <= old) {                      /* smaller: where it is, what it has kept */
        l->size = (uint16_t)want;
        return h;
    }
    /* larger: where it is if what follows is free, the new part zero */
    uint32_t hi = tail <= u->heap[heap].hi ? u->heap[heap].hi : 0xfff0u;
    uint16_t size_was = l->size;
    l->size = 0;                            /* not in its own way */
    uint32_t fit = local_fit(u, heap, tail, hi, want - old, tail & 3u);
    l->size = size_was;
    if (fit == tail && local_room(a, u, heap, (uint32_t)l->at + want)) {
        local_zero(a, sel, tail, want - old);
        l->size = (uint16_t)want;
        return h;
    }
    /* where it is, the moveable blocks in its way moved */
    if (local_make_room(a, u, heap, l, want, hi)) {
        local_zero(a, sel, tail, want - old);
        l->size = (uint16_t)want;
        return h;
    }
    /* elsewhere: a fixed block, or a locked one, only when the caller lets
     * it move (LMEM_MOVEABLE), as Windows has it */
    if ((!moveable || e[2]) && !(flags & LMEM_MOVEABLE))
        return 0;
    uint16_t n = local_place(a, u, heap, want, 0);   /* elsewhere: moved */
    if (!n)
        return 0;
    uint8_t buf[64];
    for (uint32_t k = 0; k < old; k += sizeof(buf)) {
        uint32_t m = old - k < sizeof(buf) ? old - k : (uint32_t)sizeof(buf);
        win_ne_read(a->ctx, sel, (uint32_t)l->at + k, buf, m);
        win_ne_write(a->ctx, sel, (uint32_t)n + k, buf, m);
    }
    l->at = n;
    l->size = (uint16_t)want;
    if (moveable) {
        entry_write(a, u, l, e[2], e[3]);
        return h;
    }
    return n;
}

static int local_service(args *a, ne_user *u, uint16_t service, uint32_t *v)
{
    ne_local *l;
    if ((service < 4 || service > 13) && service != 121 && service != 161 && service != 162 &&
        service != 310)
        return 0;
    int heap = heap_of(a, u, a->cpu ? a->cpu->ds : 0);
    /* the heap's size: its place in the segment, and as far as it may grow */
    uint32_t size = heap < 0 ? 0 : (uint32_t)(u->heap[heap].hi - u->heap[heap].lo) +
                                   (0xfff0u - u->heap[heap].end);
    switch (service) {
    case 11: {                          /* LocalHandle(near pointer): its handle */
        uint16_t p = w(a, 4);
        *v = 0;
        for (int i = 0; p && i < u->nlocals; i++) {
            ne_local *k = &u->local[i];
            if (k->heap == heap && k->at == p && (k->kind == L_FIXED || k->kind == L_MOVEABLE))
                *v = k->kind == L_FIXED ? p : k->handle;
        }
        return 1;
    }
    case 12: {                          /* LocalFlags(h): lock count, flags << 8 */
        uint8_t e[4];
        *v = 0;
        if ((l = local_of(u, heap, w(a, 4))) != NULL && l->kind == L_MOVEABLE) {
            entry_read(a, u, l, e);
            *v = (uint32_t)e[2] | (uint32_t)e[3] << 8;
        }
        return 1;
    }
    case 310:                           /* LocalHandleDelta(n): the handle table's
                                         * growth (WINN's grows by one); 0 asks */
        if (w(a, 4))
            u->handle_delta = w(a, 4);
        *v = u->handle_delta ? u->handle_delta : 0x20;
        return 1;
    case 13: case 121:                  /* LocalCompact(UINT), LocalShrink(HLOCAL, UINT) */
    case 161: case 162:                 /* LocalCountFree, LocalHeapSize */
        *v = service == 162 || service == 121 ? size :
             heap < 0 ? 0 : size - local_used(u, heap);
        if (*v > 0xfff0u)
            *v = 0xfff0u;
        return 1;
    case 4: {                           /* LocalInit(seg, start, end) */
        uint16_t seg = w(a, 8), start = w(a, 6), end = w(a, 4);
        int h = seg ? heap_of(a, u, seg) : heap;
        *v = h >= 0;
        if (h < 0 || (u->heap[h].sel & ~3u) == (dgroup_sel(a->ctx) & ~3u))
            return 1;                   /* DGROUP's is the header's */
        /* a DLL's: start 0, end its size, after the data (the heap's
         * place as made); else start..end */
        if (start && end > start) {
            u->heap[h].lo = start;
            u->heap[h].hi = end;
        } else if (end) {
            u->heap[h].hi = (uint16_t)((uint32_t)u->heap[h].lo + end > u->heap[h].end ?
                                       u->heap[h].end : u->heap[h].lo + end);
        }
        return 1;
    }
    case 5:                             /* LocalAlloc(flags, size) */
        *v = local_alloc(a, u, heap, w(a, 6), w(a, 4));
        return 1;
    case 6:                             /* LocalReAlloc(h, size, flags) */
        *v = local_realloc(a, u, heap, w(a, 8), w(a, 6), w(a, 4));
        return 1;
    case 7:                             /* LocalFree(h): 0 */
        *v = w(a, 4);
        if ((l = local_of(u, heap, w(a, 4))) != NULL) {
            if (l->kind == L_MOVEABLE) {
                ne_local *e = entry_of(u, l);
                if (e)
                    e->kind = L_FREE;
            }
            l->kind = L_FREE;
            *v = 0;
        }
        return 1;
    case 8:                             /* LocalLock(h): the near pointer */
        *v = 0;
        if ((l = local_of(u, heap, w(a, 4))) != NULL) {
            if (l->kind == L_MOVEABLE) {
                uint8_t e[4];
                entry_read(a, u, l, e);
                if (l->size && e[2] < 0xffu)
                    entry_write(a, u, l, e[2] + 1, e[3]);
            }
            *v = l->size ? l->at : 0;
        }
        return 1;
    case 9:                             /* LocalUnlock(h): still locked */
        *v = 0;
        if ((l = local_of(u, heap, w(a, 4))) != NULL && l->kind == L_MOVEABLE) {
            uint8_t e[4];
            entry_read(a, u, l, e);
            if (e[2] && e[2] < 0xffu)
                entry_write(a, u, l, --e[2], e[3]);
            *v = e[2] != 0;
        }
        return 1;
    case 10:                            /* LocalSize */
        *v = (l = local_of(u, heap, w(a, 4))) != NULL ? l->size : 0;
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
        *v = h;
        if (win_ne_guest_global_free(g, h)) {
            win_ne_forget_selector(a->ctx, a->cpu, h);
            *v = 0;
        }
        return 1;
    case 18: case 111:                  /* GlobalLock, GlobalWire(h): sel:0 */
        h = w(a, 4);
        *v = win_ne_guest_global(g, h) ? (uint32_t)(h & ~7u) << 16 : 0;
        *dword = 1;
        return 1;
    case 19: case 24:                   /* GlobalUnlock, UnlockSegment */
    case 192: case 197: case 198:       /* GlobalPageUnlock, GlobalFix, GlobalUnfix */
        *v = 0;
        return 1;
    case 112: case 191:                 /* GlobalUnWire: TRUE; GlobalPageLock: its count */
        *v = win_ne_guest_global(g, w(a, 4)) != NULL;
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
static WNDPROC token_proc(uint32_t t);

LRESULT ne_message(ne_user *u, uint32_t proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    win_ne_guest_api_context *c = u->ctx;
    if (msg >= 0x3E0 && msg <= 0x3E8)
        lp = (LPARAM)ne_dde_in(c, u, msg, (uint32_t)lp, 1);
    WNDPROC native = token_proc(proc);
    if (native)                         /* a native one it holds (a superclass) */
        return CallWindowProc(native, h, msg, wp, lp);
    if (msg >= 0xC000u && lp) {         /* a native DLL's (COMMDLG's Find): its structure */
        uint32_t far = win_dll_ne_lparam(msg, (uint32_t)lp);
        if (far)
            return (LRESULT)(int32_t)win_ne_callback(c, proc, (uint16_t)h, (uint16_t)msg,
                                                     (uint16_t)wp, far);
    }
    uint8_t blob[160];
    uint16_t len = 0;
    const char *fmt = NULL;
    if ((msg == WM_CREATE || msg == WM_NCCREATE) && lp) {
        const CREATESTRUCT *cs = (const CREATESTRUCT *)(uintptr_t)lp;
        len = create_size(cs);
        create_to_guest(u, cs, win_ne_blob_far(c, len), blob);
    } else if (msg == WM_SETTEXT &&
               (!lp || !win_ne_read(c, (uint16_t)((uint32_t)lp >> 16),
                                    (uint16_t)lp, blob, 1))) {
        /* Native SetWindowText/SendMessage passes an ARM pointer, not a
         * Win16 selector:offset. A Win16 WndProc may dereference lParam
         * itself (MPLAYER.EXE does), so lend it a string on its own stack.
         * A valid guest far pointer, sent by the NE program itself, stays
         * unchanged. The lent pointer also lets DefWindowProc recover the
         * original native string rather than treating SS:offset as ARM. */
        const char *s = (const char *)(uintptr_t)lp;
        size_t n = 0;
        if (s)
            while (n + 1 < sizeof(blob) && s[n])
                n++;
        if (n)
            memcpy(blob, s, n);
        blob[n] = 0;
        len = (uint16_t)(n + 1);
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
    if (msg == WM_SETTEXT && (lp >> 16) && lend_find(u, lp) < 0) {
        /* A Win16 caller's own far string also needs converting in the
         * opposite direction when it forwards WM_SETTEXT to USER. */
        char text[160];
        if (!str(a, lp, text, sizeof(text)))
            return 0;
        return fn(h, msg, wp, (LPARAM)(uintptr_t)text);
    }
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
static uint32_t dde_out(win_ne_guest_api_context *c, ne_user *u, UINT msg, uint32_t lp);

static __attribute__((noinline)) uint32_t send(args *a, ne_user *u, HWND h, UINT msg, WPARAM wp,
                                               uint32_t lp, int post)
{
    if (msg >= 0x3E0 && msg <= 0x3E8 && h && h != 0xFFFF && GetWindowTask(h) != u->task)
        lp = dde_out(a->ctx, u, msg, lp);   /* DDE's memory: the other one's copy */
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
    HTASK task;                         /* EnumTaskWindows': its windows only */
} enum_ctx;

static BOOL CALLBACK ne_enumproc(HWND h, LPARAM lp)
{
    const enum_ctx *e = (const enum_ctx *)(uintptr_t)lp;
    if (e->task && GetWindowTask(h) != e->task)
        return TRUE;
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

/* ---------------------------------------------------------------- DDE's memory */

/* DDE's messages carry global memory between programs (Windows: shared,
 * GMEM_DDESHARE): WM_DDE_DATA's, POKE's, ADVISE's in lParam's low word,
 * EXECUTE's and its ACK's in the high one. A program's block is its own
 * (one of its selectors, odd), the other end's a native block (WINN's
 * handles are even): a copy goes, the one it came from remembered, so the
 * ACK that gives an EXECUTE's commands back gives the sender its own (the
 * copy freed). DATA and POKE with fRelease: the receiver's to free, the
 * sender's freed as it is copied. */
enum { DDE_ACK_ = 0x3E4, DDE_ADVISE_ = 0x3E2, DDE_DATA_ = 0x3E5, DDE_POKE_ = 0x3E7,
       DDE_EXECUTE_ = 0x3E8 };
#define DDE_COPIES 32

static struct {
    HGLOBAL native;
    HTASK task;
    uint16_t sel;
    uint8_t copy;                       /* the program's block is WINN's copy */
} dde[DDE_COPIES];
static unsigned dde_next;

/* where its handle is: 0 none, 1 the low word, 2 the high one */
static int dde_where(UINT msg)
{
    return msg == DDE_DATA_ || msg == DDE_POKE_ || msg == DDE_ADVISE_ ? 1 :
           msg == DDE_EXECUTE_ || msg == DDE_ACK_ ? 2 : 0;
}

static uint32_t dde_with(uint32_t lp, int where, uint16_t h)
{
    return where == 1 ? (lp & 0xffff0000u) | h : (lp & 0xffffu) | (uint32_t)h << 16;
}

static int dde_find(HGLOBAL n, HTASK t, uint16_t sel)
{
    for (int i = 0; i < DDE_COPIES; i++)
        if (dde[i].task == t && (n ? dde[i].native == n : dde[i].sel == sel) &&
            (dde[i].native || dde[i].sel))
            return i;
    return -1;
}

static void dde_note(HGLOBAL n, HTASK t, uint16_t sel, int copy)
{
    int i = (int)(dde_next++ % DDE_COPIES);
    dde[i].native = n;
    dde[i].task = t;
    dde[i].sel = sel;
    dde[i].copy = (uint8_t)copy;
}

/* DDEDATA's fRelease (DATA, POKE) */
static int dde_release(UINT msg, const uint8_t *p, DWORD n)
{
    return (msg == DDE_DATA_ || msg == DDE_POKE_) && n >= 2 && (p[1] & 0x20);
}

/* the program sends or posts it to another task's window */
static uint32_t dde_out(win_ne_guest_api_context *c, ne_user *u, UINT msg, uint32_t lp)
{
    int where = dde_where(msg);
    uint16_t sel = (uint16_t)(where == 1 ? lp : lp >> 16);
    win_ne_guest_image *g = (win_ne_guest_image *)c->image;
    win_ne_guest_segment *s = where && (sel & 1) ? win_ne_guest_global(g, sel) : NULL;
    if (!s)
        return lp;
    int i = dde_find(0, u->task, sel);
    if (i >= 0) {                       /* one it had from another, or sent before */
        HGLOBAL n = dde[i].native;
        if (msg == DDE_ACK_ && dde[i].copy) {   /* the commands back: the copy goes */
            win_ne_guest_global_free(g, sel);
            dde[i].native = 0;
            dde[i].sel = 0;
        }
        return dde_with(lp, where, n);
    }
    DWORD size = s->size;
    HGLOBAL n = size ? GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size) : 0;
    uint8_t *p = n ? (uint8_t *)GlobalLock(n) : NULL;
    if (!p || !win_ne_read(c, sel, 0, p, size)) {
        if (n) {
            GlobalUnlock(n);
            GlobalFree(n);
        }
        return lp;
    }
    int release = dde_release(msg, p, size);
    GlobalUnlock(n);
    if (release)
        win_ne_guest_global_free(g, sel);
    else
        dde_note(n, u->task, sel, 0);
    return dde_with(lp, where, n);
}

/* the program takes it (from its queue: remove; sent) */
static uint32_t dde_in(win_ne_guest_api_context *c, ne_user *u, UINT msg, uint32_t lp, int remove)
{
    int where = dde_where(msg);
    HGLOBAL n = (HGLOBAL)(where == 1 ? lp & 0xffffu : lp >> 16);
    if (!where || !n || (n & 1) || !GlobalSize(n))
        return lp;
    int i = dde_find(n, u->task, 0);
    if (i >= 0) {                       /* its own block back, or the copy made before */
        uint16_t sel = dde[i].sel;
        if (remove && msg == DDE_ACK_ && !dde[i].copy) {
            GlobalFree(n);
            dde[i].native = 0;
            dde[i].sel = 0;
        } else if (remove && dde[i].copy) {
            uint8_t head[2] = { 0, 0 };
            win_ne_read(c, sel, 0, head, 2);
            if (dde_release(msg, head, 2)) {
                GlobalFree(n);
                dde[i].native = 0;
                dde[i].sel = 0;
            }
        }
        return dde_with(lp, where, sel);
    }
    DWORD size = GlobalSize(n);
    win_ne_guest_image *g = (win_ne_guest_image *)c->image;
    const uint8_t *p = size <= 0x10000u ? (const uint8_t *)GlobalLock(n) : NULL;
    uint16_t sel = p ? win_ne_guest_global_alloc(g, size) : 0;
    if (sel && !win_ne_write(c, sel, 0, p, size)) {
        win_ne_guest_global_free(g, sel);
        sel = 0;
    }
    int release = p && dde_release(msg, p, size);
    if (p)
        GlobalUnlock(n);
    if (!sel)
        return lp;
    if (remove && release)
        GlobalFree(n);
    else
        dde_note(n, u->task, sel, 1);
    return dde_with(lp, where, sel);
}

/* a program's MSG retrieved: its DDE memory its own */
uint32_t ne_dde_in(win_ne_guest_api_context *c, ne_user *u, UINT msg, uint32_t lp, int remove)
{
    return msg >= 0x3E0 && msg <= 0x3E8 ? dde_in(c, u, msg, lp, remove) : lp;
}

static void dde_task_end(ne_user *u)
{
    for (int i = 0; i < DDE_COPIES; i++)
        if (dde[i].task == u->task)
            memset(&dde[i], 0, sizeof(dde[i]));
}

/* ---------------------------------------------------------------- the clipboard */

/* a format whose data is a GDI object (the same handle for all): not memory */
static int clip_gdi(UINT fmt)
{
    return fmt == CF_BITMAP || fmt == CF_PALETTE || fmt == CF_DSPBITMAP ||
           (fmt >= CF_GDIOBJFIRST && fmt <= CF_GDIOBJLAST) || fmt == CF_OWNERDISPLAY;
}

/* its copies of the clipboard's data freed: of a format, or all (0) */
static void clip_free(args *a, ne_user *u, UINT fmt)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    for (int i = 0; i < 8; i++)
        if (u->clip[i].sel && (!fmt || u->clip[i].fmt == fmt)) {
            win_ne_guest_global_free(g, u->clip[i].sel);
            win_ne_forget_selector(a->ctx, a->cpu, u->clip[i].sel);
            u->clip[i].sel = 0;
        }
}

/* SetClipboardData(format, HANDLE): its global block's bytes the
 * clipboard's (native memory, for every program), the block freed (the
 * clipboard's now); a GDI object's handle as it is */
static uint16_t clip_set(args *a, ne_user *u, UINT fmt, uint16_t h)
{
    (void)u;
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    if (!h || clip_gdi(fmt))
        return SetClipboardData(fmt, h) ? (h ? h : 1) : 0;
    win_ne_guest_segment *s = win_ne_guest_global(g, h);
    if (!s)
        return 0;
    HGLOBAL n = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, s->size);
    uint8_t *p = n ? (uint8_t *)GlobalLock(n) : NULL;
    if (!p || !rd(a, (uint32_t)h << 16, p, s->size)) {
        if (n) {
            GlobalUnlock(n);
            GlobalFree(n);
        }
        return 0;
    }
    GlobalUnlock(n);
    if (!SetClipboardData(fmt, n)) {
        GlobalFree(n);
        return 0;
    }
    win_ne_guest_global_free(g, h);
    win_ne_forget_selector(a->ctx, a->cpu, h);
    return h;
}

/* GetClipboardData(format): a copy of the clipboard's bytes in a block of
 * its own (the clipboard's: it is not to free it), a GDI object as it is */
static uint16_t clip_get(args *a, ne_user *u, UINT fmt)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    HANDLE n = GetClipboardData(fmt);
    if (!n || clip_gdi(fmt))
        return n;
    DWORD size = GlobalSize(n);
    const uint8_t *p = (const uint8_t *)GlobalLock(n);
    if (!p || !size || size > 0x10000u) {
        if (p)
            GlobalUnlock(n);
        return 0;
    }
    clip_free(a, u, fmt);
    int i = 0;
    while (i < 7 && u->clip[i].sel)
        i++;
    if (u->clip[i].sel)
        clip_free(a, u, u->clip[i].fmt);
    uint16_t sel = win_ne_guest_global_alloc(g, size);
    if (sel && !wr(a, (uint32_t)sel << 16, p, size)) {
        win_ne_guest_global_free(g, sel);
        sel = 0;
    }
    GlobalUnlock(n);
    if (sel) {
        u->clip[i].fmt = fmt;
        u->clip[i].sel = sel;
    }
    return sel;
}

/* EnumProps' procedure of the program's: the window, the property's name
 * (a string on its stack, or an atom's number), its data. The native
 * callback has no context of its own: the one enumeration going on. */
typedef struct {
    win_ne_guest_api_context *c;
    uint32_t proc;
} props_ctx;

static const props_ctx *props_now;

static BOOL CALLBACK ne_propproc(HWND h, LPCSTR name, HANDLE data)
{
    const props_ctx *e = props_now;
    char blob[64];
    uint16_t len = 0;
    uint32_t far = (uint32_t)(uintptr_t)name & 0xffffu;
    if (!IS_INTRESOURCE(name)) {
        lstrcpyn(blob, name, sizeof(blob));
        len = (uint16_t)((strlen(blob) + 2u) & ~1u);
        far = win_ne_blob_far(e->c, len);
    }
    const uint16_t words[4] = { (uint16_t)h, (uint16_t)(far >> 16), (uint16_t)far,
                                (uint16_t)data };
    return (uint16_t)win_ne_call(e->c, e->proc, words, 4, len ? blob : NULL, len) != 0;
}

static PROPENUMPROC ne_propproc_with(const props_ctx *e)
{
    props_now = e;
    return ne_propproc;
}

/* USER's calls with buffers of their own: apart, as ne_call's frame is on
 * the stack of every call (and of the callbacks under it) */
static __attribute__((noinline)) int user_buffers(args *a, ne_user *u, uint16_t service,
                                                  uint32_t *v)
{
    win_ne_guest_api_context *c = a->ctx;
    (void)u;
    switch (service) {
    case 0x1064: case 0x10c3: {         /* DlgDirList, DlgDirListComboBox(HWND, LPSTR, int, int,
                                         * UINT): spec left the pattern */
        uint32_t sp = dw(a, 10);
        char spec[128];
        spec[0] = 0;
        if (sp && !str(a, sp, spec, sizeof(spec)))
            return 1;
        *v = (uint16_t)(service == 0x1064 ? DlgDirList : DlgDirListComboBox)(
            w(a, 14), sp ? spec : NULL, (int16_t)w(a, 8), (int16_t)w(a, 6), w(a, 4));
        if (sp)
            wr(a, sp, spec, (uint32_t)strlen(spec) + 1u);
        return 1;
    }
    case 0x1063: case 0x10c2: {         /* DlgDirSelect, DlgDirSelectComboBox(HWND, LPSTR, int) */
        char out[128];
        out[0] = 0;
        *v = (uint16_t)(service == 0x1063 ? DlgDirSelect : DlgDirSelectComboBox)(
            w(a, 10), out, (int16_t)w(a, 4));
        wr(a, dw(a, 6), out, (uint32_t)strlen(out) + 1u);
        return 1;
    }
    case 0x1196: case 0x1197: {         /* CreateCursor(HINSTANCE, hx, hy, w, h, and, xor),
                                         * CreateIcon(HINSTANCE, w, h, planes, bpp, and, xor) */
        uint8_t and_m[128], xor_m[512];
        int cursor = service == 0x1196;
        int wd = (int16_t)w(a, cursor ? 14 : 18), ht = (int16_t)w(a, cursor ? 12 : 16);
        uint32_t xn = cursor || (w(a, 12) & 0xff) == 1 ? 128u : 512u;
        if (wd != 32 || ht != 32 || !rd(a, dw(a, 8), and_m, 128) || !rd(a, dw(a, 4), xor_m, xn))
            return 1;
        *v = cursor ? CreateCursor(0, (int16_t)w(a, 18), (int16_t)w(a, 16), 32, 32, and_m, xor_m)
                    : CreateIcon(0, 32, 32, (BYTE)w(a, 14), (BYTE)w(a, 12), and_m, xor_m);
        return 1;
    }
    case 0x10de: case 0x10df: {         /* GetKeyboardState, SetKeyboardState(LPBYTE) */
        BYTE keys[256];
        if (service == 0x10de) {
            GetKeyboardState(keys);
            wr(a, dw(a, 4), keys, sizeof(keys));
        } else if (rd(a, dw(a, 4), keys, sizeof(keys))) {
            SetKeyboardState(keys);
        }
        return 1;
    }
    case 0x10b9: {                      /* GrayString(HDC, HBRUSH, proc, LPARAM, n, x, y, cx, cy) */
        HDC dc = w(a, 24);
        uint32_t proc = dw(a, 18), lp = dw(a, 14);
        int n = (int16_t)w(a, 12), x = (int16_t)w(a, 10), y = (int16_t)w(a, 8);
        if (a->bad)
            return 1;
        if (proc) {                     /* its own drawing, as it is (not grayed) */
            const uint16_t words[4] = { (uint16_t)dc, (uint16_t)(lp >> 16), (uint16_t)lp,
                                        (uint16_t)n };
            *v = (uint16_t)win_ne_call(c, proc, words, 4, NULL, 0) != 0;
            return 1;
        }
        char text[256];
        if (!str(a, lp, text, sizeof(text)))
            return 1;
        if (n <= 0 || n > (int)strlen(text))
            n = (int)strlen(text);
        COLORREF old = SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        int mode = SetBkMode(dc, TRANSPARENT);
        *v = (uint16_t)TextOut(dc, x, y, text, n);
        SetBkMode(dc, mode);
        SetTextColor(dc, old);
        return 1;
    }
    }
    return 0;
}

/* the program's class (its index) a window is of, -1 if none */
static int class_of(ne_user *u, HWND h)
{
    char cls[32];
    if (!GetClassName(h, cls, sizeof(cls)))
        return -1;
    for (int i = 0; i < NE_CLASSES; i++)
        if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, cls))
            return i;
    return -1;
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

/* ---------------------------------------------------------------- text, bitmaps, classes */

/* a string of n characters (-1: up to the NUL) at far, at most cap - 1 */
static int text_in(args *a, uint32_t far, int n, char *buf, int cap)
{
    if (n < 0) {
        if (!str(a, far, buf, (uint32_t)cap))
            return -1;
        return lstrlen(buf);
    }
    if (n >= cap)
        n = cap - 1;
    if (!rd(a, far, buf, (uint32_t)n))
        return -1;
    buf[n] = 0;
    return n;
}

/* int FAR * of n entries (Win16 ints) as native ones */
static int ints_in(args *a, uint32_t far, int n, int *out)
{
    uint8_t g[2 * 64];
    if (!far || n <= 0)
        return 1;
    if (n > 64)
        n = 64;
    if (!rd(a, far, g, 2u * (uint32_t)n))
        return 0;
    for (int i = 0; i < n; i++)
        out[i] = (int16_t)rd16(g + 2 * i);
    return 1;
}

static __attribute__((noinline)) int text_service(args *a, uint16_t service, uint32_t *v,
                                                  int *dword)
{
    char s[256];
    int tabs[64], n;
    switch (service) {
    case 0x315f: {                      /* ExtTextOut(HDC, x, y, UINT, LPRECT, LPCSTR, UINT, LPINT) */
        uint32_t dx = dw(a, 4), sp = dw(a, 10), rp = dw(a, 14);
        int count = w(a, 8);
        RECT rc;
        int widths[256];
        n = count ? text_in(a, sp, count, s, sizeof(s)) : 0;
        if (a->bad || n < 0 || (rp && !struct_in(a, F_RECT, rp, &rc)))
            return 1;
        if (dx) {
            for (int i = 0; i < n; i += 64)
                if (!ints_in(a, dx + 2u * (uint32_t)i, n - i < 64 ? n - i : 64, widths + i))
                    return 1;
        }
        *v = (uint16_t)ExtTextOut(w(a, 24), (int16_t)w(a, 22), (int16_t)w(a, 20), w(a, 18),
                                  rp ? &rc : NULL, s, (UINT)n, dx ? widths : NULL);
        return 1;
    }
    case 0x31d7: {                      /* GetTextExtentPoint(HDC, LPCSTR, int, LPSIZE) */
        SIZE sz;
        uint8_t g[4];
        n = text_in(a, dw(a, 10), (int16_t)w(a, 8), s, sizeof(s));
        if (a->bad || n < 0 || !GetTextExtentPoint(w(a, 14), s, n, &sz))
            return 1;
        wr16(g, (uint16_t)sz.cx);
        wr16(g + 2, (uint16_t)sz.cy);
        *v = (uint16_t)wr(a, dw(a, 4), g, 4);
        return 1;
    }
    case 0x10c4: {                      /* TabbedTextOut(HDC, x, y, LPCSTR, n, ntabs, LPINT, origin) */
        int nt = (int16_t)w(a, 10);
        n = text_in(a, dw(a, 14), (int16_t)w(a, 12), s, sizeof(s));
        if (a->bad || n < 0 || !ints_in(a, dw(a, 6), nt, tabs))
            return 1;
        *v = (uint32_t)TabbedTextOut(w(a, 22), (int16_t)w(a, 20), (int16_t)w(a, 18), s, n,
                                     nt > 64 ? 64 : nt, dw(a, 6) ? tabs : NULL, (int16_t)w(a, 4));
        *dword = 1;
        return 1;
    }
    case 0x10c5: {                      /* GetTabbedTextExtent(HDC, LPCSTR, n, ntabs, LPINT) */
        int nt = (int16_t)w(a, 8);
        n = text_in(a, dw(a, 12), (int16_t)w(a, 10), s, sizeof(s));
        if (a->bad || n < 0 || !ints_in(a, dw(a, 4), nt, tabs))
            return 1;
        *v = GetTabbedTextExtent(w(a, 16), s, n, nt > 64 ? 64 : nt, dw(a, 4) ? tabs : NULL);
        *dword = 1;
        return 1;
    }
    case 0x305c: {                      /* GetTextFace(HDC, int, LPSTR) */
        int size = (int16_t)w(a, 8);
        char face[64];
        if (a->bad || size <= 0)
            return 1;
        n = GetTextFace(w(a, 10), sizeof(face), face);
        if (n >= size)
            n = size - 1;
        face[n] = 0;
        if (wr(a, dw(a, 4), face, (uint32_t)n + 1))
            *v = (uint16_t)n;
        return 1;
    }
    }
    return 0;
}

/* the bits of a Win16 bitmap: rows of words (WINN's layout for 1 and 8
 * bits a pixel); count bytes between the guest and a native buffer */
static __attribute__((noinline)) int bitmap_service(args *a, uint16_t service, uint32_t *v,
                                                    int *dword)
{
    switch (service) {
    case 0x3030:                        /* CreateBitmap(w, h, planes, bpp, const void FAR *) */
    case 0x3031: {                      /* CreateBitmapIndirect(const BITMAP FAR *) */
        int wd, ht, planes, bpp;
        uint32_t bits;
        if (service == 0x3030) {
            bits = dw(a, 4);
            bpp = w(a, 8);
            planes = w(a, 10);
            ht = (int16_t)w(a, 12);
            wd = (int16_t)w(a, 14);
        } else {
            uint8_t g[14];
            if (!rd(a, dw(a, 4), g, sizeof(g)))
                return 1;
            wd = (int16_t)rd16(g + 2);
            ht = (int16_t)rd16(g + 4);
            planes = g[8];
            bpp = g[9];
            bits = rd32(g + 10);
        }
        if (a->bad || wd <= 0 || ht <= 0 || wd > 4096 || ht > 4096)
            return 1;
        int depth = planes * bpp;
        uint32_t size = (uint32_t)gdi_bitmap_pitch(wd, depth == 1 ? 1 : 8) * (uint32_t)ht;
        uint8_t *buf = bits && (depth == 1 || depth == 8) && size <= 0x10000u ?
                       (uint8_t *)win_alloc(size) : NULL;
        if (buf && !rd(a, bits, buf, size)) {
            win_free(buf);
            return 1;
        }
        *v = CreateBitmap(wd, ht, (UINT)planes, (UINT)bpp, buf);
        win_free(buf);
        return 1;
    }
    case 0x304a:                        /* GetBitmapBits(HBITMAP, LONG, LPVOID) */
    case 0x306a: {                      /* SetBitmapBits(HBITMAP, DWORD, const void FAR *) */
        uint32_t count = dw(a, 8), far = dw(a, 4);
        if (count > 0x10000u - (far & 0xffffu))
            count = 0x10000u - (far & 0xffffu);
        uint8_t *buf = !a->bad && count ? (uint8_t *)win_alloc(count) : NULL;
        if (!buf)
            return 1;
        if (service == 0x304a) {
            LONG got = GetBitmapBits(w(a, 12), (LONG)count, buf);
            if (got > 0 && wr(a, far, buf, (uint32_t)got))
                *v = (uint32_t)got;
        } else if (rd(a, far, buf, count)) {
            *v = (uint32_t)SetBitmapBits(w(a, 12), count, buf);
        }
        win_free(buf);
        *dword = 1;
        return 1;
    }
    case 349:                           /* _hread(HFILE, void huge *, LONG) */
    case 350: {                         /* _hwrite(HFILE, const void huge *, LONG) */
        uint32_t n = dw(a, 4), far = dw(a, 8), done = 0;
        HFILE f = (HFILE)w(a, 12);
        uint8_t buf[256];
        *dword = 1;
        *v = (uint32_t)HFILE_ERROR;
        if (a->bad)
            return 1;
        if (n > 0x10000u - (far & 0xffffu))     /* within its block */
            n = 0x10000u - (far & 0xffffu);
        while (done < n) {
            uint32_t k = n - done < sizeof(buf) ? n - done : (uint32_t)sizeof(buf);
            uint32_t at = (far & 0xffff0000u) | ((far + done) & 0xffffu);
            LONG got;
            if (service == 350) {
                if (!rd(a, at, buf, k))
                    return 1;
                got = _hwrite(f, buf, (LONG)k);
            } else {
                got = _hread(f, buf, (LONG)k);
                if (got > 0 && !wr(a, at, buf, (uint32_t)got))
                    return 1;
            }
            if (got < 0)
                return done ? (*v = done, 1) : 1;
            done += (uint32_t)got;
            if ((uint32_t)got < k)
                break;
        }
        *v = done;
        return 1;
    }
    }
    return 0;
}

/* USER.404 GetClassInfo(HINSTANCE, LPCSTR, WNDCLASS FAR *): its own
 * class's procedure and menu as it registered them, a native class's
 * procedure as one it can call (CallWindowProc, or a superclass's) */
uint16_t ne_class_info(args *a, ne_user *u, uint16_t inst, uint32_t name_far, uint32_t out)
{
    char name[32];
    WNDCLASS wc;
    LPCSTR nm = name_or_atom(a, name_far, name, sizeof(name));
    if (a->bad || (!GetClassInfo(inst ? instance(u, inst) : 0, nm, &wc) &&
                   !GetClassInfo(0, nm, &wc)))
        return 0;
    uint32_t proc = 0, menu = 0;
    for (int i = 0; i < NE_CLASSES; i++)
        if (u->cls[i].proc && (name_far >> 16) && !lstrcmpi(u->cls[i].name, name)) {
            proc = u->cls[i].proc;
            menu = u->cls[i].menu_id;
        }
    if (!proc)
        proc = token_of(wc.lpfnWndProc);
    uint8_t g[26];
    wr16(g, (uint16_t)wc.style);
    wr32(g + 2, proc);
    wr16(g + 6, (uint16_t)wc.cbClsExtra);
    wr16(g + 8, (uint16_t)wc.cbWndExtra);
    wr16(g + 10, wc.hInstance == (HINSTANCE)u->task ? dgroup_sel(a->ctx) : 0);
    wr16(g + 12, (uint16_t)wc.hIcon);
    wr16(g + 14, (uint16_t)wc.hCursor);
    wr16(g + 16, (uint16_t)(uintptr_t)wc.hbrBackground);
    wr32(g + 18, menu);
    wr32(g + 22, name_far);
    return (uint16_t)wr(a, out, g, sizeof(g));
}

/* the strings changed in place: apart, their buffers not in ne_call's frame
 * (a program's procedure, called back, calls again) */
static __attribute__((noinline)) int call_strings(args *a, uint16_t service, uint32_t *v,
                                                 int *dword)
{
    win_ne_guest_api_context *c = a->ctx;
    switch (service) {
    case 0x11b5: case 0x11b6: {         /* AnsiUpperBuff, AnsiLowerBuff(LPSTR, UINT) */
        uint32_t n = w(a, 4) ? w(a, 4) : 0x10000u, p = dw(a, 6), base, limit;
        int code;
        char buf[128];
        /* 0: 64K, as far as its segment goes */
        if (win_ne_guest_selector_get(c->image, (uint16_t)(p >> 16), &base, &limit, &code) &&
            (uint16_t)p <= limit && n > limit - (uint16_t)p + 1u)
            n = limit - (uint16_t)p + 1u;
        for (uint32_t k = 0; !a->bad && k < n; k += sizeof(buf)) {
            uint32_t m = n - k < sizeof(buf) ? n - k : (uint32_t)sizeof(buf);
            if (!rd(a, p + k, buf, m))
                break;
            if (service == 0x11b5)
                AnsiUpperBuff(buf, m);
            else
                AnsiLowerBuff(buf, m);
            wr(a, p + k, buf, m);
        }
        *v = (uint16_t)n;
        return 1;
    }
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
    }
    return 0;
}

/* ---------------------------------------------------------------- hooks */

/* The program's hooks are taken (SetWindowsHook, SetWindowsHookEx) and
 * USER calls them (user.h has when) with the task's input, messages and
 * windows: those of the task that retrieves a message, makes or destroys
 * a window, activates one ...; WH_CALLWNDPROC's, with each message sent
 * to a window of its task. The journals' see nothing. HHOOK: "HK" and the
 * hook's number; the next in the chain the one made before it. */
#define HOOK_TYPES 12                   /* WH_MSGFILTER (-1) to WH_SHELL (10) */
static uint16_t hook_ids;
static int hook_counts[HOOK_TYPES];
static LRESULT ne_hook(int type, int code, WPARAM wp, LPARAM lp);

static void hooks_count(int type, int add)
{
    if (type >= -1 && type < HOOK_TYPES - 1)
        hook_counts[type + 1] += add;
    unsigned types = 0;
    for (int t = -1; t < HOOK_TYPES - 1; t++)
        if (hook_counts[t + 1] > 0 && t != WH_JOURNALRECORD && t != WH_JOURNALPLAYBACK)
            types |= USER_HOOK_BIT(t);
    user_set_hooks(types, types ? ne_hook : NULL);
}

/* the hook before the one numbered id (0: the last made) of the type */
static int hook_next(ne_user *u, int type, uint16_t id)
{
    int best = -1;
    for (int i = 0; i < NE_HOOKS; i++) {
        uint16_t k = u->hook[i].id;
        if (k && u->hook[i].type == type && (!id || k < id) &&
            (!u->hook[i].task || u->hook[i].task == (uint16_t)u->task) &&
            (best < 0 || k > u->hook[best].id))
            best = i;
    }
    return best;
}

static uint32_t hook_call(ne_user *u, int i, int code, uint16_t wp, uint32_t lp,
                          uint8_t *blob, uint16_t len)
{
    const uint16_t words[4] = { (uint16_t)code, wp, (uint16_t)(lp >> 16), (uint16_t)lp };
    return win_ne_call(u->ctx, u->hook[i].proc, words, 4, blob, len);
}

static uint16_t hook_add(ne_user *u, int type, uint32_t proc, uint16_t task)
{
    for (int i = 0; i < NE_HOOKS; i++)
        if (!u->hook[i].id) {
            if (!++hook_ids)
                hook_ids = 1;
            u->hook[i].type = (int16_t)type;
            u->hook[i].proc = proc;
            u->hook[i].task = task;
            u->hook[i].id = hook_ids;
            hooks_count(type, 1);
            return hook_ids;
        }
    return 0;
}

static void hook_remove(ne_user *u, int i)
{
    int type = u->hook[i].type;
    memset(&u->hook[i], 0, sizeof(u->hook[i]));
    hooks_count(type, -1);
}

/* a MSG a hook may have changed, from its copy (msg_to_guest's) */
static void msg_from_copy(MSG *m, const uint8_t *b)
{
    m->hwnd = rd16(b);
    m->message = rd16(b + 2);
    m->wParam = rd16(b + 4);
    m->lParam = (LPARAM)(int32_t)rd32(b + 6);
    m->time = rd32(b + 10);
    m->pt.x = (int16_t)rd16(b + 14);
    m->pt.y = (int16_t)rd16(b + 16);
}

/* USER's call: the program's hook with copies of what lParam points to (a
 * CWPSTRUCT, a CBT_CREATEWND and its CREATESTRUCT, a CBTACTIVATESTRUCT, a
 * MSG, a MOUSEHOOKSTRUCT); a MSG it changed back; what it returns. The
 * program: a message sent, the window's; the others, the current task. */
static __attribute__((noinline)) LRESULT ne_hook(int type, int code, WPARAM wp, LPARAM lp)
{
    int msg = type == WH_GETMESSAGE || type == WH_MSGFILTER || type == WH_SYSMSGFILTER;
    int points = type == WH_CALLWNDPROC || msg || type == WH_MOUSE ||
                 (type == WH_CBT && (code == HCBT_CREATEWND || code == HCBT_ACTIVATE));
    if (points && !lp)
        return 0;
    if (type == WH_CBT && code != HCBT_CREATEWND && code != HCBT_ACTIVATE &&
        code != HCBT_DESTROYWND && code != HCBT_SETFOCUS && code != HCBT_MINMAX &&
        code != HCBT_SYSCOMMAND)
        return 0;
    const CWPSTRUCT *cwp = (const CWPSTRUCT *)(uintptr_t)lp;
    ne_user *u = ne_user_of(type == WH_CALLWNDPROC ? GetWindowTask(cwp->hwnd) : GetCurrentTask());
    win_ne_guest_api_context *c = u ? u->ctx : NULL;
    if (!c || c->dead || c->exited || c->stopped)
        return 0;
    int i = hook_next(u, type, 0);
    if (i < 0)
        return 0;
    uint8_t blob[10 + 30 + 64 + 64];
    if (!points)                        /* lParam no pointer */
        return (LRESULT)(int32_t)hook_call(u, i, code, (uint16_t)wp, (uint32_t)lp, NULL, 0);
    if (msg || type == WH_MOUSE || (type == WH_CBT && code == HCBT_ACTIVATE)) {
        uint16_t len = msg ? 18 : type == WH_MOUSE ? 12 : 4;
        uint32_t far = win_ne_blob_far(c, len);
        if (msg) {
            msg_to_guest((const MSG *)(uintptr_t)lp, blob);
        } else if (type == WH_MOUSE) {
            const MOUSEHOOKSTRUCT *mh = (const MOUSEHOOKSTRUCT *)(uintptr_t)lp;
            wr16(blob, (uint16_t)mh->pt.x);
            wr16(blob + 2, (uint16_t)mh->pt.y);
            wr16(blob + 4, (uint16_t)mh->hwnd);
            wr16(blob + 6, (uint16_t)mh->wHitTestCode);
            wr32(blob + 8, (uint32_t)mh->dwExtraInfo);
        } else {
            const CBTACTIVATESTRUCT *ca = (const CBTACTIVATESTRUCT *)(uintptr_t)lp;
            wr16(blob, (uint16_t)ca->fMouse);
            wr16(blob + 2, (uint16_t)ca->hWndActive);
        }
        LRESULT r = (LRESULT)(int32_t)hook_call(u, i, code, (uint16_t)wp, far, blob, len);
        if (msg)
            msg_from_copy((MSG *)(uintptr_t)lp, blob);
        return r;
    }
    const CBT_CREATEWND *cw = (const CBT_CREATEWND *)(uintptr_t)lp;
    const CREATESTRUCT *cs = type == WH_CBT ? cw->lpcs :
                             (cwp->message == WM_CREATE || cwp->message == WM_NCCREATE) &&
                             cwp->lParam ? (const CREATESTRUCT *)(uintptr_t)cwp->lParam : NULL;
    uint16_t head = type == WH_CBT ? 6 : 10;
    uint16_t len = (uint16_t)(head + (cs ? create_size(cs) : 0));
    uint32_t far = win_ne_blob_far(c, len);
    if (cs)
        create_to_guest(u, cs, far + head, blob + head);
    if (type == WH_CBT) {
        wr32(blob, far + head);
        wr16(blob + 4, (uint16_t)cw->hwndInsertAfter);
    } else {                            /* lParam, wParam, message, hwnd */
        wr32(blob, cs ? far + head : (uint32_t)cwp->lParam);
        wr16(blob + 4, (uint16_t)cwp->wParam);
        wr16(blob + 6, (uint16_t)cwp->message);
        wr16(blob + 8, (uint16_t)cwp->hwnd);
    }
    return (LRESULT)(int32_t)hook_call(u, i, code, (uint16_t)wp, far, blob, len);
}

static int hook_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    switch (service) {
    case 0x1123:                        /* SetWindowsHookEx(int, HOOKPROC, HINSTANCE, HTASK) */
        *v = a->bad ? 0 : hook_add(u, (int16_t)w(a, 12), dw(a, 8), w(a, 4));
        if (*v)
            *v |= 0x484b0000u;
        *dword = 1;
        return 1;
    case 0x1079: {                      /* SetWindowsHook(int, HOOKPROC): the one before */
        int type = (int16_t)w(a, 8), i = hook_next(u, type, 0);
        *v = i >= 0 ? u->hook[i].proc : 0;
        *dword = 1;
        if (!a->bad)
            hook_add(u, type, dw(a, 4), 0);
        return 1;
    }
    case 0x1124:                        /* UnhookWindowsHookEx(HHOOK) */
    case 0x10ea: {                      /* UnhookWindowsHook(int, HOOKPROC) */
        uint32_t h = dw(a, 4);
        int type = (int16_t)w(a, 8);
        *v = 0;
        for (int i = 0; !a->bad && i < NE_HOOKS; i++)
            if (u->hook[i].id && (service == 0x1124 ? u->hook[i].id == (uint16_t)h :
                                  u->hook[i].type == type && u->hook[i].proc == h)) {
                hook_remove(u, i);
                *v = 1;
                break;
            }
        if (service == 0x1124 && (h >> 16) != 0x484bu)
            *v = 1;                     /* not one of these: nothing to undo */
        return 1;
    }
    case 0x1125: {                      /* CallNextHookEx(HHOOK, int, WPARAM, LPARAM) */
        uint32_t h = dw(a, 12);
        int at = -1, i = -1;
        for (int k = 0; k < NE_HOOKS; k++)
            if (u->hook[k].id && u->hook[k].id == (uint16_t)h)
                at = k;
        if (!a->bad && (h >> 16) == 0x484bu && at >= 0)
            i = hook_next(u, u->hook[at].type, u->hook[at].id);
        *v = i >= 0 ? hook_call(u, i, (int16_t)w(a, 10), w(a, 8), dw(a, 4), NULL, 0) : 0;
        *dword = 1;
        return 1;
    }
    case 0x10eb: {                      /* DefHookProc(int, WPARAM, LPARAM, HHOOK FAR *): the
                                         * one before (that *lphhook has), if any */
        uint8_t b[4] = { 0 };
        uint32_t prev = !a->bad && dw(a, 4) && rd(a, dw(a, 4), b, 4) ? rd32(b) : 0;
        int i = -1;
        for (int k = 0; prev && k < NE_HOOKS; k++)
            if (u->hook[k].id && u->hook[k].proc == prev)
                i = k;
        *v = i >= 0 ? hook_call(u, i, (int16_t)w(a, 14), w(a, 12), dw(a, 8), NULL, 0) : 0;
        *dword = 1;
        return 1;
    }
    case 0x107b: {                      /* CallMsgFilter(LPMSG, int): the filters' */
        MSG m;
        *v = 0;
        if (a->bad || !dw(a, 6) || !msg_from_guest(a, dw(a, 6), &m))
            return 1;
        *v = (uint16_t)CallMsgFilter(&m, (int16_t)w(a, 4));
        return 1;
    }
    }
    return 0;
}

int ne_call(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    win_ne_guest_api_context *c = a->ctx;
    if (service < 0x1000u && (local_service(a, u, service, v) || global_service(a, service, v, dword)))
        return 1;
    if (ne_dos_service(a, u, service, v, dword) || text_service(a, service, v, dword) ||
        bitmap_service(a, service, v, dword) || ne_kernel_service(a, u, service, v, dword) ||
        ne_sys_service(a, u, service, v, dword) || ne_more_service(a, u, service, v, dword))
        return 1;
    if (service == WIN_NE_SERVICE_KEYBOARD + 4u) { /* KEYBOARD.4 ToAscii */
        /* Microsoft RusLat (1992) uses private KEYBOARD.DRV virtual keys:
         * 88h selects Latin, 89h Russian, 8Ah queries the active layout.
         * Its first ToAscii argument is at SP+16 (14 PASCAL bytes). */
        uint16_t vk = w(a, 16);
        if (a->bad)
            return 1;
        if (vk == 0x88u)
            user_set_keyboard_layout(0);
        else if (vk == 0x89u)
            user_set_keyboard_layout(1);
        else if (vk != 0x8au)
            return 0; /* Other ToAscii calls still require a real handler. */
        *v = user_keyboard_layout() ? 0x89u : 0x88u;
        return 1;
    }
    if (service == 0x1194) {            /* GetClassInfo */
        *v = ne_class_info(a, u, w(a, 12), dw(a, 8), dw(a, 4));
        return 1;
    }
    if (service == 0x11ca) {            /* DestroyCursor(HCURSOR) */
        *v = (uint16_t)DestroyIcon(w(a, 4));
        return 1;
    }
    switch (service) {
    case 0x10ab: {                      /* WinHelp(HWND, LPCSTR, UINT, DWORD): a string or a
                                         * MULTIKEYHELP of the program's for some */
        char file[144], text[256];
        UINT cmd = w(a, 8);
        uint32_t data = dw(a, 4);
        file[0] = text[0] = 0;
        if (dw(a, 10))
            str(a, dw(a, 10), file, sizeof(file));
        if ((cmd == HELP_KEY || cmd == HELP_PARTIALKEY || cmd == HELP_COMMAND ||
             cmd == HELP_MULTIKEY) && data) {
            if (cmd == HELP_MULTIKEY) {
                uint8_t mk[3 + sizeof(text)];
                memset(mk, 0, sizeof(mk));
                str(a, data + 3, (char *)mk + 3, sizeof(text));
                memcpy(text, mk, sizeof(text));
                text[sizeof(text) - 1] = 0;
            } else {
                str(a, data, text, sizeof(text));
            }
            data = (uint32_t)(uintptr_t)text;
        }
        *v = !a->bad && WinHelp(w(a, 14), file, cmd, data);
        return 1;
    }
    case 0x11b5: case 0x11b6: case 0x11af: case 0x11b0:
        return call_strings(a, service, v, dword);
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
        enum_ctx e = { u, dw(a, 8), dw(a, 4), 0 };
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
            m.lParam = (LPARAM)ne_dde_in(c, u, m.message, (uint32_t)m.lParam, (remove & PM_REMOVE) != 0);
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
        int own = class_of(u, h);
        if (service == 0x1081)
            *v = GetClassWord(h, idx);
        else if (idx == GCL_WNDPROC)    /* its own procedure, or a native one's token */
            *v = own >= 0 ? u->cls[own].proc :
                 token_of((WNDPROC)(intptr_t)GetClassLong(h, idx)), *dword = 1;
        else
            *v = (uint32_t)GetClassLong(h, idx), *dword = 1;
        return 1;
    }
    case 0x1082:                        /* SetClassWord(HWND, int, WORD) */
        *v = SetClassWord(w(a, 8), (int16_t)w(a, 6), w(a, 4));
        return 1;
    case 0x1084: {                      /* SetClassLong(HWND, int, LONG) */
        int idx = (int16_t)w(a, 6), own = class_of(u, w(a, 8));
        uint32_t x = dw(a, 4);
        *dword = 1;
        if (idx != GCL_WNDPROC)
            *v = (uint32_t)SetClassLong(w(a, 8), idx, (LONG)x);
        else if (own >= 0 && x && !token_proc(x)) {
            *v = u->cls[own].proc;      /* its class's windows (those there too) */
            u->cls[own].proc = x;
        }
        return 1;
    }
    case 0x108d: case 0x108e: {         /* SetClipboardData, GetClipboardData */
        UINT fmt = service == 0x108d ? w(a, 6) : w(a, 4);
        *v = service == 0x108d ? clip_set(a, u, fmt, w(a, 4)) : clip_get(a, u, fmt);
        return 1;
    }
    case 0x108a: case 0x108b:           /* CloseClipboard, EmptyClipboard: its copies freed */
        clip_free(a, u, 0);
        *v = (uint16_t)(service == 0x108a ? CloseClipboard() : EmptyClipboard());
        return 1;
    case 0x1196: case 0x1197:           /* CreateCursor, CreateIcon */
    case 0x10de: case 0x10df:           /* GetKeyboardState, SetKeyboardState */
    case 0x10b9:                        /* GrayString */
    case 0x1064: case 0x10c3:           /* DlgDirList, DlgDirListComboBox */
    case 0x1063: case 0x10c2:           /* DlgDirSelect, DlgDirSelectComboBox */
        return user_buffers(a, u, service, v);
    case 0x1172: case 0x1173: {         /* GetWindowPlacement, SetWindowPlacement(HWND, ptr) */
        WINDOWPLACEMENT wp;
        memset(&wp, 0, sizeof(wp));
        if (service == 0x1172) {
            if (GetWindowPlacement(w(a, 8), &wp))
                *v = struct_out(a, F_PLACEMENT, dw(a, 4), &wp);
        } else if (struct_in(a, F_PLACEMENT, dw(a, 4), &wp)) {
            *v = (uint16_t)SetWindowPlacement(w(a, 8), &wp);
        }
        return 1;
    }
    case 0x101b: {                      /* EnumProps(HWND, proc) */
        props_ctx e = { c, dw(a, 4) };
        if (!a->bad && e.proc)
            *v = (uint16_t)EnumProps(w(a, 8), ne_propproc_with(&e));
        return 1;
    }
    case 0x1102: {                      /* MapWindowPoints(HWND, HWND, LPPOINT, UINT) */
        UINT n = w(a, 4);
        uint32_t p = dw(a, 6);
        HWND from = w(a, 12), to = w(a, 10);
        for (UINT i = 0; !a->bad && i < n; i++, p += 4) {
            uint8_t g[4];
            if (!rd(a, p, g, sizeof(g)))
                break;
            POINT pt = { (int16_t)rd16(g), (int16_t)rd16(g + 2) };
            MapWindowPoints(from, to, &pt, 1);
            wr16(g, (uint16_t)pt.x);
            wr16(g + 2, (uint16_t)pt.y);
            wr(a, p, g, sizeof(g));
        }
        return 1;
    }
    case 0x10e1: {                      /* EnumTaskWindows(HTASK, proc, LPARAM) */
        enum_ctx e = { u, dw(a, 8), dw(a, 4), w(a, 12) };
        if (!a->bad && e.task)
            *v = (uint16_t)EnumWindows(ne_enumproc, (LPARAM)(uintptr_t)&e);
        return 1;
    }
    case 0x1079: case 0x10eb: case 0x1125: case 0x1123: case 0x10ea: case 0x1124:
    case 0x107b:                        /* hooks */
        return hook_service(a, u, service, v, dword);
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
    case 0x3068: {                      /* GDI.104 RectVisibleOld(HDC, const RECT FAR *) */
        RECT rc;
        uint32_t rect = dw(a, 4);
        HDC hdc = w(a, 8);
        if (!a->bad && rect && struct_in(a, F_RECT, rect, &rc))
            *v = (uint16_t)RectVisible(hdc, &rc);
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
    dde_task_end(u);
    for (int i = 0; i < NE_HOOKS; i++)
        if (u->hook[i].id)
            hook_remove(u, i);
    for (int i = 0; i < NE_TIMERS; i++)
        if (u->timer[i].proc)
            KillTimer(u->timer[i].hwnd, u->timer[i].id);
}

#endif
