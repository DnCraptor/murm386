/*
 * pe_api.c - a Win32 program's calls of WINN's functions: the generic
 * ones (core/pe_sigs.h: the native function, its arguments as they are,
 * pointers being the program's own memory), KERNEL32's with code of their
 * own (its process, modules, memory, files, time, strings, objects), the
 * start (its DLLs' entries, then its own).
 *
 * One thread: what waits (WaitForSingleObject, Sleep) lets the other
 * tasks run meanwhile; a critical section has nothing to wait for.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
#include "wincore.h"
#include "pe_int.h"
#include "x86bridge.h"


#define NFNS win_pe_nfns

/* WINN's modules as HMODULEs: where Windows 95 has KERNEL32 */
#define NATIVE_HMODULE(m) (0xbff70000u + ((uint32_t)(m) << 16))
#define IS_NATIVE_HMODULE(h) ((h) >= 0xbff70000u && (h) < NATIVE_HMODULE(PE_MODULES) && !((h) & 0xffffu))

#define PE_HANDLE(i) (0x100u + (uint32_t)(i))
#define CURRENT_PROCESS 0xffffffffu
#define CURRENT_THREAD 0xfffffffeu

enum {
    ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3, ERROR_TOO_MANY_OPEN_FILES = 4,
    ERROR_ACCESS_DENIED = 5, ERROR_INVALID_HANDLE = 6, ERROR_NOT_ENOUGH_MEMORY = 8,
    ERROR_NO_MORE_FILES = 18, ERROR_NOT_SUPPORTED = 50, ERROR_FILE_EXISTS = 80,
    ERROR_INVALID_PARAMETER = 87, ERROR_INSUFFICIENT_BUFFER = 122, ERROR_MOD_NOT_FOUND = 126,
    ERROR_PROC_NOT_FOUND = 127, ERROR_ALREADY_EXISTS = 183, ERROR_ENVVAR_NOT_FOUND = 203
};

/* the last c in s, NULL none (strrchr: WINN's target has none) */
static char *last_of(const char *s, char c)
{
    const char *r = NULL;
    for (; *s; s++)
        if (*s == c)
            r = s;
    return (char *)r;
}

/* ---------------------------------------------------------------- formatting */

int pe_fmt(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t n = 0;
#define PUT(c) do { if (n + 1 < cap) out[n] = (c); n++; } while (0)
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            PUT(*f);
            continue;
        }
        f++;
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1;
        for (;; f++) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else if (*f == '#') alt = 1;
            else break;
        }
        if (*f == '*') {
            width = va_arg(ap, int);
            f++;
        } else {
            while (*f >= '0' && *f <= '9')
                width = width * 10 + (*f++ - '0');
        }
        if (*f == '.') {
            f++;
            prec = 0;
            if (*f == '*') {
                prec = va_arg(ap, int);
                f++;
            } else {
                while (*f >= '0' && *f <= '9')
                    prec = prec * 10 + (*f++ - '0');
            }
        }
        while (*f == 'l' || *f == 'h')
            f++;
        char tmp[40];
        const char *s = tmp;
        int len = 0;
        char sign = 0;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            unsigned long v;
            if (*f == 'd' || *f == 'i') {
                long x = va_arg(ap, int);
                v = x < 0 ? 0ul - (unsigned long)x : (unsigned long)x;
                sign = x < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
            } else {
                v = va_arg(ap, unsigned);
            }
            unsigned base = *f == 'o' ? 8u : (*f == 'x' || *f == 'X') ? 16u : 10u;
            const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char digits[24], prefix[3];
            int nd = 0, np = 0;
            while (v || (!nd && prec)) {
                digits[nd++] = dig[v % base];
                v /= base;
            }
            while (nd < prec && nd < 22)
                digits[nd++] = '0';
            if (sign)
                prefix[np++] = sign;
            if (alt && base == 16u) {
                prefix[np++] = '0';
                prefix[np++] = *f;
            }
            int zeros = zero && !left && prec < 0 && width > np + nd ? width - np - nd : 0;
            if (zeros > 16)
                zeros = 16;
            for (int i = 0; i < np; i++)
                tmp[len++] = prefix[i];
            while (zeros-- > 0)
                tmp[len++] = '0';
            while (nd)
                tmp[len++] = digits[--nd];
            prec = -1;
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (s[len] && (prec < 0 || len < prec))
                len++;
            break;
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        default:
            va_end(ap);
            if (cap)
                out[n < cap ? n : cap - 1] = 0;
            return (int)n;
        }
        int pad = width > len ? width - len : 0;
        if (!left)
            while (pad-- > 0) PUT(' ');
        for (int i = 0; i < len; i++)
            PUT(s[i]);
        if (left)
            while (pad-- > 0) PUT(' ');
    }
#undef PUT
    va_end(ap);
    if (cap)
        out[n < cap ? n : cap - 1] = 0;
    return (int)n;
}

/* ---------------------------------------------------------------- the program's memory */

const char *pe_str(pe_args *a, uint32_t addr)
{
    if (!addr)
        return NULL;
    win_pe_image *img = a->ctx->img;
    const char *p = (const char *)win_pe_ptr(img, addr, 1);
    for (uint32_t n = 0; p && n < 0x100000u; n++) {
        if (n && !(n & 255u) && !win_pe_ptr(img, addr + n, 1))
            break;
        if (!p[n])
            return p;
    }
    a->bad = 1;
    return NULL;
}

void *pe_buf(pe_args *a, uint32_t addr, uint32_t bytes)
{
    if (!addr)
        return NULL;
    void *p = win_pe_ptr(a->ctx->img, addr, bytes);
    if (!p)
        a->bad = 1;
    return p;
}

/* a resource's or a class's name as the native functions take it: a
 * number (below 10000h) or a string */
static const char *res_name(pe_args *a, uint32_t v)
{
    return v < 0x10000u ? (const char *)(uintptr_t)v : pe_str(a, v);
}

/* ---------------------------------------------------------------- heaps */

/* A heap's chunks are guest memory; each block has a header of 8 bytes:
 * its size with the header (a multiple of 8; bit 0: in use), a mark. The
 * chunk ends with a header of size 0. */
#define HEAP_MARK 0x50454850u
#define HEAP_CHUNKS 64
typedef struct {
    uint32_t chunk[HEAP_CHUNKS];
    uint32_t size[HEAP_CHUNKS];
    int used;
} pe_heap;

static pe_heap *heap_of(win_pe_ctx *ctx, uint32_t h)
{
    uint32_t i = h - PE_HANDLE(0);
    if (i >= PE_HANDLES || ctx->handle[i].kind != PE_H_HEAP)
        return NULL;
    return (pe_heap *)(uintptr_t)ctx->handle[i].value;
}

static uint8_t *block(win_pe_image *img, uint32_t at) { return (uint8_t *)win_pe_ptr(img, at, 8); }

static uint32_t heap_take(win_pe_ctx *ctx, pe_heap *h, uint32_t need)
{
    win_pe_image *img = ctx->img;
    for (int c = 0; c < h->used; c++) {
        uint32_t at = h->chunk[c];
        for (;;) {
            uint8_t *b = block(img, at);
            uint32_t s = pe_rd32(b);
            if (!s)
                break;
            if (!(s & 1u)) {                /* free: those after it joined */
                for (;;) {
                    uint8_t *n = block(img, at + s);
                    uint32_t ns = pe_rd32(n);
                    if (!ns || (ns & 1u))
                        break;
                    s += ns;
                }
                pe_wr32(b, s);
                if (s >= need) {
                    if (s - need >= 16u) {
                        uint8_t *rest = block(img, at + need);
                        pe_wr32(rest, s - need);
                        pe_wr32(rest + 4, HEAP_MARK);
                        s = need;
                    }
                    pe_wr32(b, s | 1u);
                    pe_wr32(b + 4, HEAP_MARK);
                    return at + 8u;
                }
            }
            at += s & ~1u;
        }
    }
    if (h->used == HEAP_CHUNKS)
        return 0;
    uint32_t size = need + 8u > 0x20000u ? need + 8u : 0x20000u;
    uint32_t chunk = win_pe_alloc(img, size, 16, PE_MEM_HEAP);
    if (!chunk)
        return 0;
    h->chunk[h->used] = chunk;
    h->size[h->used++] = size;
    uint8_t *b = block(img, chunk);
    pe_wr32(b, size - 8u);
    pe_wr32(b + 4, HEAP_MARK);
    pe_wr32(block(img, chunk + size - 8u), 0);
    return heap_take(ctx, h, need);
}

/* the header of a block of the heap's, NULL if p is none */
static uint8_t *heap_block(win_pe_ctx *ctx, pe_heap *h, uint32_t p)
{
    if (!h || p < 8u)
        return NULL;
    for (int c = 0; c < h->used; c++)
        if (p - h->chunk[c] < h->size[c]) {
            uint8_t *b = block(ctx->img, p - 8u);
            return b && (pe_rd32(b) & 1u) && pe_rd32(b + 4) == HEAP_MARK ? b : NULL;
        }
    return NULL;
}

static uint32_t need_of(uint32_t bytes)
{
    uint32_t n = (bytes + 8u + 7u) & ~7u;
    return n < 16u ? 16u : n;
}

uint32_t pe_heap_alloc(win_pe_ctx *ctx, uint32_t heap, uint32_t bytes, int zero)
{
    pe_heap *h = heap_of(ctx, heap);
    if (!h || bytes > 0x4000000u)
        return 0;
    uint32_t p = heap_take(ctx, h, need_of(bytes));
    if (p && zero) {
        uint8_t *b = heap_block(ctx, h, p);
        memset(b + 8, 0, (pe_rd32(b) & ~1u) - 8u);
    }
    return p;
}

int pe_heap_free(win_pe_ctx *ctx, uint32_t heap, uint32_t p)
{
    uint8_t *b = heap_block(ctx, heap_of(ctx, heap), p);
    if (!b)
        return 0;
    pe_wr32(b, pe_rd32(b) & ~1u);
    return 1;
}

static uint32_t heap_size(win_pe_ctx *ctx, uint32_t heap, uint32_t p)
{
    uint8_t *b = heap_block(ctx, heap_of(ctx, heap), p);
    return b ? (pe_rd32(b) & ~1u) - 8u : 0xffffffffu;
}

static uint32_t heap_realloc(win_pe_ctx *ctx, uint32_t heap, uint32_t p, uint32_t bytes,
                             uint32_t flags)
{
    pe_heap *h = heap_of(ctx, heap);
    uint8_t *b = heap_block(ctx, h, p);
    if (!b)
        return 0;
    uint32_t s = pe_rd32(b) & ~1u, need = need_of(bytes), old = s - 8u;
    /* in place: smaller, or what follows it free and enough */
    for (;;) {
        uint8_t *n = block(ctx->img, p - 8u + s);
        uint32_t ns = pe_rd32(n);
        if (s >= need || !ns || (ns & 1u))
            break;
        s += ns;
    }
    if (s >= need) {
        if (s - need >= 16u) {
            uint8_t *rest = block(ctx->img, p - 8u + need);
            pe_wr32(rest, s - need);
            pe_wr32(rest + 4, HEAP_MARK);
            s = need;
        }
        pe_wr32(b, s | 1u);
        if ((flags & 8u) && bytes > old)
            memset(win_pe_ptr(ctx->img, p + old, bytes - old), 0, bytes - old);
        return p;
    }
    pe_wr32(b, (pe_rd32(b) & ~1u) | 1u);
    if (flags & 0x10u)                      /* HEAP_REALLOC_IN_PLACE_ONLY */
        return 0;
    uint32_t q = pe_heap_alloc(ctx, heap, bytes, (flags & 8u) != 0);
    if (!q)
        return 0;
    memcpy(win_pe_ptr(ctx->img, q, old), win_pe_ptr(ctx->img, p, old), old < bytes ? old : bytes);
    pe_heap_free(ctx, heap, p);
    return q;
}

static int new_handle(win_pe_ctx *ctx, uint8_t kind, int32_t fd, uint32_t value)
{
    for (int i = 0; i < PE_HANDLES; i++)
        if (!ctx->handle[i].kind) {
            ctx->handle[i].kind = kind;
            ctx->handle[i].fd = fd;
            ctx->handle[i].value = value;
            return i;
        }
    return -1;
}

static uint32_t heap_create(win_pe_ctx *ctx)
{
    pe_heap *h = (pe_heap *)calloc(1, sizeof(pe_heap));
    int i = h ? new_handle(ctx, PE_H_HEAP, 0, (uint32_t)(uintptr_t)h) : -1;
    if (i < 0) {
        free(h);
        return 0;
    }
    return PE_HANDLE(i);
}

static void heap_destroy(win_pe_ctx *ctx, uint32_t heap)
{
    pe_heap *h = heap_of(ctx, heap);
    if (!h)
        return;
    for (int c = 0; c < h->used; c++)
        win_pe_free(ctx->img, h->chunk[c]);
    free(h);
    ctx->handle[heap - PE_HANDLE(0)].kind = PE_H_FREE;
}

uint32_t pe_strdup(win_pe_ctx *ctx, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1u;
    uint32_t p = pe_heap_alloc(ctx, ctx->heap, n, 0);
    if (p)
        memcpy(win_pe_ptr(ctx->img, p, n), s, n);
    return p;
}

/* ---------------------------------------------------------------- the start */

void win_pe_start(win_pe_ctx *ctx, const char *cmdline)
{
    /* the standard handles (the first three): what is written to them
     * goes nowhere */
    for (int i = 0; i < 3; i++)
        new_handle(ctx, PE_H_STD, i, 0);
    ctx->heap = heap_create(ctx);
    char line[400];
    pe_fmt(line, sizeof(line), "%s%s%s", ctx->path, cmdline && *cmdline ? " " : "",
             cmdline ? cmdline : "");
    ctx->cmdline = pe_strdup(ctx, line);
    uint32_t n = (uint32_t)strlen(line) + 1u;
    ctx->cmdline_w = pe_heap_alloc(ctx, ctx->heap, 2u * n, 0);
    if (ctx->cmdline_w) {
        uint8_t *w = (uint8_t *)win_pe_ptr(ctx->img, ctx->cmdline_w, 2u * n);
        for (uint32_t i = 0; i < n; i++)
            pe_wr16(w + 2 * i, (uint8_t)line[i]);
    }
    char *env = (char *)win_alloc(4096);
    int len = env ? win_file_environment(env, 4096) : 0;
    if (len > 0) {
        ctx->environment = pe_heap_alloc(ctx, ctx->heap, (uint32_t)len, 0);
        if (ctx->environment)
            memcpy(win_pe_ptr(ctx->img, ctx->environment, (uint32_t)len), env, (size_t)len);
    }
    win_free(env);
    uint8_t *peb = (uint8_t *)win_pe_ptr(ctx->img, ctx->img->tib + WIN_PE_TIB_PEB, 0x20);
    if (peb)
        pe_wr32(peb + 0x18, ctx->heap);     /* ProcessHeap */
    pe_user_start(ctx);
}

void win_pe_end(win_pe_ctx *ctx)
{
    pe_user_end(ctx);
    for (int i = 0; i < PE_HANDLES; i++)
        switch (ctx->handle[i].kind) {
        case PE_H_FILE:
            _lclose((HFILE)ctx->handle[i].fd);
            break;
        case PE_H_HEAP:
        case PE_H_FIND:
            free((void *)(uintptr_t)ctx->handle[i].value);
            break;
        default:
            break;
        }
}

/* the DLLs' entries not called yet: DllMain(instance, DLL_PROCESS_ATTACH,
 * 0), those loaded first first */
static void attach(win_pe_ctx *ctx)
{
    win_pe_image *img = ctx->img;
    for (;;) {
        int next = -1;
        for (int i = 1; i < img->nmod; i++)
            if (img->mod[i].base && img->mod[i].dll && !img->mod[i].attached &&
                (next < 0 || img->mod[i].order < img->mod[next].order))
                next = i;
        if (next < 0 || ctx->dead || ctx->exited || ctx->stopped)
            return;
        img->mod[next].attached = 1;
        if (img->mod[next].entry) {
            const uint32_t args[3] = { img->mod[next].base, 1, 0 };
            win_pe_call(ctx, img->mod[next].entry, args, 3, NULL, 0, NULL);
        }
    }
}

int pe_module_of_instance(win_pe_ctx *ctx, uint32_t h)
{
    if (!h || h == (uint32_t)ctx->task)
        return 0;
    return win_pe_module_of(ctx->img, h);
}

/* ---------------------------------------------------------------- the generic marshaller */

typedef uint32_t (*native16)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t, uint32_t);

static int generic(pe_args *a, const win_pe_fn *f, uint32_t *eax)
{
    const char *codes = strchr(f->sig, ':') + 1;
    int n = (int)strlen(codes);
    uint32_t raw[16], x[16];
    if (n > 16)
        return 0;
    for (int i = 0; i < n; i++)
        raw[i] = pe_arg(a, (unsigned)i);
    for (int i = 0; i < 16; i++)
        x[i] = 0;
    for (int i = 0; i < n && !a->bad; i++) {
        uint32_t r = raw[i], next = i + 1 < n ? raw[i + 1] : 0;
        switch (codes[i]) {
        case 'd': x[i] = r; break;
        case 'w': x[i] = r & 0xffffu; break;
        case 'y': x[i] = r & 0xffu; break;
        case 'h': x[i] = (uint32_t)(int32_t)(int16_t)r; break;
        case 'I': x[i] = (uint32_t)pe_instance(a->ctx, r); break;
        case 's': x[i] = (uint32_t)(uintptr_t)res_name(a, r); break;
        case 'P': x[i] = (uint32_t)(uintptr_t)pe_buf(a, r, 1); break;
        case 'S': case 'B': x[i] = (uint32_t)(uintptr_t)pe_buf(a, r, next ? next : 1u); break;
        case 'r': x[i] = (uint32_t)(uintptr_t)pe_buf(a, r, 16); break;
        case 'p': x[i] = (uint32_t)(uintptr_t)pe_buf(a, r, 8); break;
        case 'q': x[i] = (uint32_t)(uintptr_t)pe_buf(a, r, 4); break;
        case 'A': x[i] = (uint32_t)(uintptr_t)pe_buf(a, r, next ? 8u * next : 8u); break;
        default: return 0;
        }
    }
    if (a->bad) {
        *eax = 0;
        return 1;
    }
    uint32_t res = ((native16)f->fn)(x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8],
                                      x[9], x[10], x[11], x[12], x[13], x[14], x[15]);
    switch (f->sig[0]) {
    case 'v': *eax = 1; break;              /* Win32's are BOOLs mostly */
    case 'w': *eax = res & 0xffffu; break;
    case 'P': *eax = win_pe_addr(a->ctx->img, (const void *)(uintptr_t)res); break;
    default: *eax = res; break;
    }
    return 1;
}

/* ---------------------------------------------------------------- time */

/* days from 1601-01-01 of a date; and back */
static int64_t days_of(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468 + 134774;   /* 1970 - 1601 */
}

static void date_of(int64_t days, int *y, int *m, int *d)
{
    int64_t z = days - 134774 + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    *d = (int)(doy - (153u * mp + 2u) / 5u + 1u);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static uint64_t ft_of(const SYSTEMTIME_N *t)
{
    int64_t days = days_of(t->wYear, t->wMonth, t->wDay);
    uint64_t s = (uint64_t)days * 86400u + t->wHour * 3600u + t->wMinute * 60u + t->wSecond;
    return s * 10000000u + (uint64_t)t->wMilliseconds * 10000u;
}

static void st_of(uint64_t ft, SYSTEMTIME_N *t)
{
    uint64_t ms = ft / 10000u, s = ms / 1000u;
    int64_t days = (int64_t)(s / 86400u);
    int y, m, d;
    date_of(days, &y, &m, &d);
    t->wYear = (WORD)y;
    t->wMonth = (WORD)m;
    t->wDay = (WORD)d;
    t->wDayOfWeek = (WORD)((days + 1) % 7);     /* 1601-01-01 a Monday */
    t->wHour = (WORD)(s % 86400u / 3600u);
    t->wMinute = (WORD)(s % 3600u / 60u);
    t->wSecond = (WORD)(s % 60u);
    t->wMilliseconds = (WORD)(ms % 1000u);
}

static uint64_t rd64(const uint8_t *p) { return pe_rd32(p) | (uint64_t)pe_rd32(p + 4) << 32; }
static void wr64(uint8_t *p, uint64_t v) { pe_wr32(p, (uint32_t)v); pe_wr32(p + 4, (uint32_t)(v >> 32)); }

static uint64_t ft_of_dos(WORD date, WORD time)
{
    SYSTEMTIME_N t = { 0 };
    t.wYear = (WORD)(1980 + (date >> 9));
    t.wMonth = (WORD)((date >> 5) & 15);
    t.wDay = (WORD)(date & 31);
    t.wHour = (WORD)(time >> 11);
    t.wMinute = (WORD)((time >> 5) & 63);
    t.wSecond = (WORD)((time & 31) * 2);
    if (!t.wMonth)
        t.wMonth = 1;
    if (!t.wDay)
        t.wDay = 1;
    return ft_of(&t);
}

static void dos_of_ft(uint64_t ft, WORD *date, WORD *time)
{
    SYSTEMTIME_N t;
    st_of(ft, &t);
    *date = (WORD)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
    *time = (WORD)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
}

/* ---------------------------------------------------------------- files */

typedef struct {
    int n, at;
    WINFIND_N e[1];
} pe_find;

static int handle_index(win_pe_ctx *ctx, uint32_t h, uint8_t kind)
{
    uint32_t i = h - PE_HANDLE(0);
    return i < PE_HANDLES && ctx->handle[i].kind == kind ? (int)i : -1;
}

/* a std handle (GetStdHandle's: STD_INPUT -10 ...) */
static int std_of(win_pe_ctx *ctx, uint32_t h)
{
    int i = handle_index(ctx, h, PE_H_STD);
    return i >= 0 ? ctx->handle[i].fd : -1;
}

static void fill_find(uint8_t *d, const WINFIND_N *e)
{
    memset(d, 0, 320);
    pe_wr32(d, e->attr ? e->attr : 0x80u);
    uint64_t ft = ft_of_dos(e->date, e->time);
    wr64(d + 4, ft);
    wr64(d + 12, ft);
    wr64(d + 20, ft);
    pe_wr32(d + 32, e->size);
    memcpy(d + 44, e->name, 12);
    memcpy(d + 304, e->name, 12);
}

static __attribute__((noinline)) uint32_t create_file(pe_args *a, win_pe_ctx *ctx)
{
    const char *name = pe_str(a, pe_arg(a, 0));
    uint32_t access = pe_arg(a, 1), how = pe_arg(a, 4);
    if (!name || a->bad)
        return 0xffffffffu;
    if (!lstrcmpi(name, "CON") || !lstrcmpi(name, "CONOUT$") || !lstrcmpi(name, "CONIN$"))
        return PE_HANDLE(lstrcmpi(name, "CONIN$") ? 1 : 0);
    int exists = win_file_attr(name, 0, 0) >= 0;
    int mode = (access & 0xc0000000u) == 0xc0000000u ? OF_READWRITE :
               (access & 0x40000000u) ? OF_WRITE : OF_READ;
    HFILE f = HFILE_ERROR;
    switch (how) {
    case 1:                                 /* CREATE_NEW */
        if (exists) {
            ctx->last_error = ERROR_FILE_EXISTS;
            return 0xffffffffu;
        }
        /* fall through */
    case 2:                                 /* CREATE_ALWAYS */
    case 5:                                 /* TRUNCATE_EXISTING */
        if (how == 5 && !exists)
            break;
        f = _lcreat(name, 0);
        if (f != HFILE_ERROR && mode != OF_WRITE) {
            _lclose(f);
            f = _lopen(name, mode);
        }
        break;
    case 3:                                 /* OPEN_EXISTING */
        f = _lopen(name, mode);
        break;
    case 4:                                 /* OPEN_ALWAYS */
        f = exists ? _lopen(name, mode) : _lcreat(name, 0);
        if (!exists && f != HFILE_ERROR && mode != OF_WRITE) {
            _lclose(f);
            f = _lopen(name, mode);
        }
        break;
    default:
        ctx->last_error = ERROR_INVALID_PARAMETER;
        return 0xffffffffu;
    }
    if (f == HFILE_ERROR) {
        ctx->last_error = exists ? ERROR_ACCESS_DENIED : ERROR_FILE_NOT_FOUND;
        return 0xffffffffu;
    }
    int i = new_handle(ctx, PE_H_FILE, (int32_t)f, 0);
    if (i < 0) {
        _lclose(f);
        ctx->last_error = ERROR_TOO_MANY_OPEN_FILES;
        return 0xffffffffu;
    }
    ctx->last_error = (how == 2 || how == 4) && exists ? ERROR_ALREADY_EXISTS : 0;
    return PE_HANDLE(i);
}

static int file_of(win_pe_ctx *ctx, uint32_t h)
{
    int i = handle_index(ctx, h, PE_H_FILE);
    if (i < 0) {
        ctx->last_error = ERROR_INVALID_HANDLE;
        return -1;
    }
    return ctx->handle[i].fd;
}

/* the drive of a root ("C:\\"), the current one for NULL: 0 = A */
static int drive_of(const char *root)
{
    if (root && root[0] && root[1] == ':')
        return (root[0] | 0x20) - 'a';
    return win_file_drive(-1);
}

static uint32_t full_path(const char *name, char *out, uint32_t cap)
{
    char cwd[160];
    if (name[0] && name[1] == ':' && (name[2] == '\\' || name[2] == '/')) {
        pe_fmt(out, cap, "%s", name);
    } else if (name[0] == '\\' || name[0] == '/') {
        GetCurrentDirectoryN(cwd, sizeof(cwd));
        cwd[2] = 0;
        pe_fmt(out, cap, "%s%s", cwd, name);
    } else {
        int n = GetCurrentDirectoryN(cwd, sizeof(cwd));
        if (name[0] && name[1] == ':')
            name += 2;
        pe_fmt(out, cap, n && cwd[n - 1] == '\\' ? "%s%s" : "%s\\%s", cwd, name);
    }
    for (char *p = out; *p; p++)
        if (*p == '/')
            *p = '\\';
    return (uint32_t)strlen(out);
}

/* a string into the program's buffer of size bytes: its length (without
 * the NUL), or the size it needs when it is too small */
static uint32_t put_str(pe_args *a, uint32_t buf, uint32_t size, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (!buf || size <= n)
        return n + 1u;
    char *p = (char *)pe_buf(a, buf, n + 1u);
    if (p)
        memcpy(p, s, n + 1u);
    return n;
}

/* ---------------------------------------------------------------- environment */

static const char *env_find(win_pe_ctx *ctx, const char *name)
{
    const char *e = (const char *)win_pe_ptr(ctx->img, ctx->environment, 1);
    size_t n = strlen(name);
    while (e && *e) {
        if (!strncmp(e, name, n) && e[n] == '=')
            return e + n + 1;
        /* names are matched without case, as Windows does */
        int same = 1;
        for (size_t k = 0; k < n && same; k++)
            same = (e[k] | 0x20) == (name[k] | 0x20) && e[k];
        if (same && e[n] == '=')
            return e + n + 1;
        e += strlen(e) + 1;
    }
    return NULL;
}

static int env_set(win_pe_ctx *ctx, const char *name, const char *value)
{
    const char *e = (const char *)win_pe_ptr(ctx->img, ctx->environment, 1);
    char *buf = (char *)win_alloc(8192);
    if (!buf)
        return 0;
    size_t len = 0, n = strlen(name);
    while (e && *e) {
        size_t l = strlen(e) + 1;
        int same = !strncmp(e, name, n) && e[n] == '=';
        if (!same && len + l < 8000) {
            memcpy(buf + len, e, l);
            len += l;
        }
        e += l;
    }
    if (value && len + n + strlen(value) + 3 < 8000)
        len += (size_t)pe_fmt(buf + len, 8192 - len, "%s=%s", name, value) + 1;
    buf[len++] = 0;
    uint32_t p = pe_heap_alloc(ctx, ctx->heap, (uint32_t)len, 0);
    if (p) {
        memcpy(win_pe_ptr(ctx->img, p, (uint32_t)len), buf, len);
        pe_heap_free(ctx, ctx->heap, ctx->environment);
        ctx->environment = p;
    }
    win_free(buf);
    return p != 0;
}

/* ---------------------------------------------------------------- KERNEL32 */

/* the module of an HMODULE: the program's, a DLL's, WINN's (NATIVE_HMODULE),
 * -1 none */
static int module_index(win_pe_ctx *ctx, uint32_t h)
{
    if (!h)
        return 0;
    int m = pe_module_of_instance(ctx, h);
    return m >= 0 && ctx->img->mod[m].base == h ? m : (h == (uint32_t)ctx->task ? 0 : -1);
}

static uint32_t load_library(win_pe_ctx *ctx, const char *name, int attach_it)
{
    int nm = win_pe_native_module(name);
    if (nm >= 0)
        return NATIVE_HMODULE(nm);
    int m = win_pe_load_library(ctx->img, name);
    if (m < 0) {
        ctx->last_error = ERROR_MOD_NOT_FOUND;
        return 0;
    }
    ctx->img->mod[m].refs++;
    if (attach_it)
        attach(ctx);
    return ctx->img->mod[m].base;
}

static __attribute__((noinline)) uint32_t module_file(pe_args *a, win_pe_ctx *ctx, int wide)
{
    uint32_t h = pe_arg(a, 0), buf = pe_arg(a, 1), size = pe_arg(a, 2);
    char path[200];
    if (IS_NATIVE_HMODULE(h)) {
        static const char *const names[PE_MODULES] = { "KERNEL32", "USER32", "GDI32" };
        GetSystemDirectory(path, sizeof(path) - 16);
        lstrcat(path, "\\");
        lstrcat(path, names[(h - NATIVE_HMODULE(0)) >> 16]);
        lstrcat(path, ".DLL");
    } else {
        int m = module_index(ctx, h);
        if (m < 0) {
            ctx->last_error = ERROR_MOD_NOT_FOUND;
            return 0;
        }
        full_path(m ? ctx->img->mod[m].path : ctx->path, path, sizeof(path));
    }
    uint32_t n = (uint32_t)strlen(path);
    if (!size)
        return 0;
    if (n >= size)
        n = size - 1u;
    if (wide) {
        uint8_t *w = (uint8_t *)pe_buf(a, buf, 2u * (n + 1u));
        for (uint32_t i = 0; w && i <= n; i++)
            pe_wr16(w + 2 * i, i < n ? (uint8_t)path[i] : 0);
    } else {
        char *p = (char *)pe_buf(a, buf, n + 1u);
        if (p) {
            memcpy(p, path, n);
            p[n] = 0;
        }
    }
    return n;
}

static int ctype(int c)
{
    int t = 0;
    if (c >= 'A' && c <= 'Z') t |= 0x101;
    if (c >= 'a' && c <= 'z') t |= 0x102;
    if (c >= 0xc0 && c <= 0xde && c != 0xd7) t |= 0x101;
    if (c >= 0xdf && c <= 0xff && c != 0xf7) t |= 0x102;
    if (c >= '0' && c <= '9') t |= 0x84;
    if (c == ' ' || (c >= 9 && c <= 13)) t |= 0x08;
    if (c == ' ' || c == 9) t |= 0x40;
    if (c < 32 || c == 127) t |= 0x20;
    if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) t |= 0x80;
    if ((c > 32 && c < 127 && !(t & 0x104)) || (c >= 0xa1 && c < 0xc0) || c == 0xd7 || c == 0xf7)
        t |= 0x10;
    return t;
}

static int locale_info(uint32_t type, char *out, size_t cap)
{
    static const struct { uint32_t type; const char *value; } info[] = {
        { 0x1004, "1252" }, { 0x000b, "437" }, { 0x000e, "." }, { 0x000f, "," },
        { 0x001d, "/" }, { 0x001e, ":" }, { 0x1001, "English" }, { 0x0003, "ENU" },
        { 0x0006, "United States" }, { 0x0005, "1" }, { 0x000c, "," }, { 0x000d, "1" },
        { 0x0014, "$" }, { 0x0001, "0409" }, { 0x0002, "English (United States)" },
        { 0x0007, "USA" }, { 0x001f, "M/d/yy" }, { 0x0020, "dddd, MMMM dd, yyyy" },
        { 0x1003, "h:mm:ss tt" }, { 0x0028, "AM" }, { 0x0029, "PM" }, { 0x0021, "0" },
        { 0x0023, "0" }, { 0x0025, "0" }, { 0x0010, "3;0" }, { 0x0011, "2" },
        { 0x0012, "1" }, { 0x0019, "2" }, { 0x0015, "." }, { 0x0016, "," },
    };
    for (unsigned i = 0; i < sizeof(info) / sizeof(info[0]); i++)
        if (info[i].type == (type & 0xffffu)) {
            pe_fmt(out, cap, "%s", info[i].value);
            return 1;
        }
    return 0;
}

static const char *const months[] = { "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December" };
static const char *const days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
    "Friday", "Saturday" };

/* GetDateFormat's and GetTimeFormat's pictures */
static void format_time(const SYSTEMTIME_N *t, const char *pic, char *out, size_t cap)
{
    size_t n = 0;
    while (*pic && n + 16 < cap) {
        char c = *pic;
        int run = 1;
        while (pic[run] == c)
            run++;
        if (c == '\'') {
            pic++;
            while (*pic && *pic != '\'' && n + 1 < cap)
                out[n++] = *pic++;
            if (*pic)
                pic++;
            continue;
        }
        char v[32] = "";
        switch (c) {
        case 'd':
            if (run >= 4) pe_fmt(v, sizeof(v), "%s", days[t->wDayOfWeek % 7]);
            else if (run == 3) pe_fmt(v, sizeof(v), "%.3s", days[t->wDayOfWeek % 7]);
            else pe_fmt(v, sizeof(v), run == 2 ? "%02u" : "%u", t->wDay);
            break;
        case 'M':
            if (run >= 4) pe_fmt(v, sizeof(v), "%s", months[(t->wMonth + 11) % 12]);
            else if (run == 3) pe_fmt(v, sizeof(v), "%.3s", months[(t->wMonth + 11) % 12]);
            else pe_fmt(v, sizeof(v), run == 2 ? "%02u" : "%u", t->wMonth);
            break;
        case 'y':
            pe_fmt(v, sizeof(v), run >= 3 ? "%u" : run == 2 ? "%02u" : "%u",
                     run >= 3 ? t->wYear : t->wYear % 100u);
            break;
        case 'h': pe_fmt(v, sizeof(v), run == 2 ? "%02u" : "%u", (t->wHour + 11) % 12 + 1); break;
        case 'H': pe_fmt(v, sizeof(v), run == 2 ? "%02u" : "%u", t->wHour); break;
        case 'm': pe_fmt(v, sizeof(v), run == 2 ? "%02u" : "%u", t->wMinute); break;
        case 's': pe_fmt(v, sizeof(v), run == 2 ? "%02u" : "%u", t->wSecond); break;
        case 't': pe_fmt(v, sizeof(v), "%.*s", run == 1 ? 1 : 2, t->wHour < 12 ? "AM" : "PM"); break;
        default:
            pe_fmt(v, sizeof(v), "%.*s", run < 31 ? run : 31, pic);
            break;
        }
        pic += run;
        for (const char *p = v; *p && n + 1 < cap; p++)
            out[n++] = *p;
    }
    out[n] = 0;
}

/* the calls with buffers of their own: not in kernel32()'s frame, which
 * nested window procedures stack up */
static __attribute__((noinline)) int kernel32_more(pe_args *a, uint16_t service, uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    uint32_t v = 0;
    switch (service) {
    case PEF_GetFullPathNameA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        uint32_t size = pe_arg(a, 1), buf = pe_arg(a, 2), part = pe_arg(a, 3);
        char full[260];
        if (!name)
            break;
        full_path(name, full, sizeof(full));
        v = put_str(a, buf, size, full);
        if (part && v < size) {
            uint8_t *pp = (uint8_t *)pe_buf(a, part, 4);
            const char *last = last_of(full, '\\');
            if (pp)
                pe_wr32(pp, buf + (uint32_t)(last ? last - full + 1 : 0));
        }
        break;
    }
    case PEF_GetTempPathA: {
        char tmp[160];
        const char *t = env_find(ctx, "TEMP");
        if (!t)
            t = env_find(ctx, "TMP");
        pe_fmt(tmp, sizeof(tmp), "%s", t ? t : "C:\\");
        size_t n = strlen(tmp);
        if (n && tmp[n - 1] != '\\' && n + 1 < sizeof(tmp))
            strcat(tmp, "\\");
        v = put_str(a, pe_arg(a, 1), pe_arg(a, 0), tmp);
        break;
    }
    case PEF_SearchPathA: {
        uint32_t path = pe_arg(a, 0), size = pe_arg(a, 3), buf = pe_arg(a, 4), part = pe_arg(a, 5);
        const char *file = pe_str(a, pe_arg(a, 1));
        const char *ext = pe_arg(a, 2) ? pe_str(a, pe_arg(a, 2)) : NULL;
        if (!file)
            break;
        char name[160], dirs[4][160], full[300];
        pe_fmt(name, sizeof(name), "%s%s", file, ext && !strchr(file, '.') ? ext : "");
        int nd = 0;
        if (path && pe_str(a, path))
            pe_fmt(dirs[nd++], 160, "%s", pe_str(a, path));
        else {
            pe_fmt(dirs[nd], 160, "%s", ctx->path);
            char *s = last_of(dirs[nd], '\\');
            if (s)
                *s = 0, nd++;
            GetCurrentDirectoryN(dirs[nd++], 160);
            GetWindowsDirectory(dirs[nd++], 160);
            GetSystemDirectory(dirs[nd++], 160);
        }
        for (int i = 0; i < nd && !v; i++) {
            pe_fmt(full, sizeof(full), "%s\\%s", dirs[i], name);
            if (win_file_attr(full, 0, 0) >= 0) {
                v = put_str(a, buf, size, full);
                if (part && v < size) {
                    uint8_t *pp = (uint8_t *)pe_buf(a, part, 4);
                    if (pp)
                        pe_wr32(pp, buf + (uint32_t)(last_of(full, '\\') - full + 1));
                }
            }
        }
        if (!v)
            ctx->last_error = ERROR_FILE_NOT_FOUND;
        break;
    }
    case PEF_GetLocaleInfoA: {
        char s[64];
        if (!locale_info(pe_arg(a, 1), s, sizeof(s))) {
            ctx->last_error = ERROR_INVALID_PARAMETER;
            break;
        }
        uint32_t n = (uint32_t)strlen(s) + 1u, size = pe_arg(a, 3);
        if (!size)
            v = n;
        else if (size >= n && pe_buf(a, pe_arg(a, 2), n))
            memcpy(pe_buf(a, pe_arg(a, 2), n), s, n), v = n;
        else
            ctx->last_error = ERROR_INSUFFICIENT_BUFFER;
        break;
    }
    case PEF_FormatMessageA: {
        uint32_t flags = pe_arg(a, 0), id = pe_arg(a, 2), buf = pe_arg(a, 4), size = pe_arg(a, 5);
        char text[300];
        if (flags & 0x400u) {               /* FORMAT_MESSAGE_FROM_STRING */
            const char *s = pe_str(a, pe_arg(a, 1));
            pe_fmt(text, sizeof(text), "%s", s ? s : "");
        } else {
            pe_fmt(text, sizeof(text), "Error %lu.\r\n", (unsigned long)id);
        }
        uint32_t n = (uint32_t)strlen(text);
        if (flags & 0x100u) {               /* FORMAT_MESSAGE_ALLOCATE_BUFFER */
            uint8_t *pp = (uint8_t *)pe_buf(a, buf, 4);
            uint32_t p = pe_strdup(ctx, text);
            if (pp && p) {
                pe_wr32(pp, p);
                v = n;
            }
        } else if (size > n) {
            char *p = (char *)pe_buf(a, buf, n + 1u);
            if (p) {
                memcpy(p, text, n + 1u);
                v = n;
            }
        }
        break;
    }
    case PEF_CopyFileA: {
        const char *s = pe_str(a, pe_arg(a, 0)), *d = pe_str(a, pe_arg(a, 1));
        if (!s || !d)
            break;
        if (pe_arg(a, 2) && win_file_attr(d, 0, 0) >= 0) {
            ctx->last_error = ERROR_FILE_EXISTS;
            break;
        }
        HFILE in = _lopen(s, OF_READ), out = in == HFILE_ERROR ? HFILE_ERROR : _lcreat(d, 0);
        char *buf = (char *)win_alloc(4096);
        LONG n = 0;
        v = in != HFILE_ERROR && out != HFILE_ERROR && buf;
        while (v && (n = _hread(in, buf, 4096)) > 0)
            v = _hwrite(out, buf, n) == n;
        win_free(buf);
        if (in != HFILE_ERROR)
            _lclose(in);
        if (out != HFILE_ERROR)
            _lclose(out);
        if (!v)
            ctx->last_error = ERROR_FILE_NOT_FOUND;
        break;
    }
    case PEF_GetVolumeInformationA: {
        uint32_t serial = pe_arg(a, 3), maxlen = pe_arg(a, 4), flags = pe_arg(a, 5);
        if (pe_arg(a, 1) && pe_arg(a, 2))
            put_str(a, pe_arg(a, 1), pe_arg(a, 2), "WINN");
        if (pe_arg(a, 6) && pe_arg(a, 7))
            put_str(a, pe_arg(a, 6), pe_arg(a, 7), "FAT");
        const uint32_t vals[3] = { 0x19950824u, 12, 0 };
        const uint32_t at[3] = { serial, maxlen, flags };
        for (unsigned k = 0; k < 3; k++) {
            uint8_t *p = at[k] ? (uint8_t *)pe_buf(a, at[k], 4) : NULL;
            if (p)
                pe_wr32(p, vals[k]);
        }
        v = 1;
        break;
    }
    case PEF_GetDiskFreeSpaceA: {
        const char *root = pe_arg(a, 0) ? pe_str(a, pe_arg(a, 0)) : NULL;
        DWORD free_bytes = 0, total = 0;
        if (!GetDiskFreeSpaceN(drive_of(root), &free_bytes, &total))
            break;
        const uint32_t vals[4] = { 8, 512, free_bytes / 4096u, total / 4096u };
        for (unsigned k = 0; k < 4; k++) {
            uint8_t *p = pe_arg(a, k + 1) ? (uint8_t *)pe_buf(a, pe_arg(a, k + 1), 4) : NULL;
            if (p)
                pe_wr32(p, vals[k]);
        }
        v = 1;
        break;
    }
    default:
        return -1;
    }
    *eax = v;
    return 1;
}

static int kernel32(pe_args *a, uint16_t service, uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    win_pe_image *img = ctx->img;
    uint32_t v = 0;
    switch (service) {
    case PEF_ExitProcess:
        ctx->exited = 1;
        ctx->exit_code = pe_arg(a, 0);
        *eax = 0;
        return WINX86_SERVICE_EXIT;
    case PEF_FatalAppExitA: {
        const char *s = pe_str(a, pe_arg(a, 1));
        MessageBox(0, s ? s : "", "Fatal Application Exit", MB_OK | MB_ICONSTOP);
        ctx->exited = 1;
        ctx->exit_code = 255;
        return WINX86_SERVICE_EXIT;
    }
    case PEF_TerminateProcess:
        if (pe_arg(a, 0) == CURRENT_PROCESS) {
            ctx->exited = 1;
            ctx->exit_code = pe_arg(a, 1);
            return WINX86_SERVICE_EXIT;
        }
        v = 0;
        break;
    case PEF_GetCommandLineA: v = ctx->cmdline; break;
    case PEF_GetCommandLineW: v = ctx->cmdline_w; break;
    case PEF_GetModuleHandleA: {
        uint32_t p = pe_arg(a, 0);
        if (!p) {
            v = img->mod[0].base;
            break;
        }
        const char *name = pe_str(a, p);
        if (!name)
            break;
        int nm = win_pe_native_module(name);
        if (nm >= 0) {
            v = NATIVE_HMODULE(nm);
            break;
        }
        char s[16];
        int k = 0;
        for (const char *q = name; *q; q++)
            if (*q == '\\' || *q == '/' || *q == ':')
                k = (int)(q - name) + 1;
        pe_fmt(s, sizeof(s), "%s", name + k);
        for (char *q = s; *q; q++) {
            if (*q == '.')
                *q = 0;
            else if (*q >= 'a' && *q <= 'z')
                *q = (char)(*q - 32);
            if (!*q)
                break;
        }
        for (int i = 0; i < img->nmod; i++)
            if (img->mod[i].base && !strcmp(img->mod[i].name, s))
                v = img->mod[i].base;
        if (!v)
            ctx->last_error = ERROR_MOD_NOT_FOUND;
        break;
    }
    case PEF_GetModuleFileNameA: v = module_file(a, ctx, 0); break;
    case PEF_GetModuleFileNameW: v = module_file(a, ctx, 1); break;
    case PEF_GetProcAddress: {
        uint32_t h = pe_arg(a, 0), p = pe_arg(a, 1);
        const char *name = p >= 0x10000u ? pe_str(a, p) : NULL;
        if (IS_NATIVE_HMODULE(h))
            v = win_pe_native_proc(img, (int)((h - NATIVE_HMODULE(0)) >> 16), name, (uint16_t)p);
        else
            v = win_pe_export(img, module_index(ctx, h), name, (uint16_t)p);
        if (!v)
            ctx->last_error = ERROR_PROC_NOT_FOUND;
        break;
    }
    case PEF_LoadLibraryA:
    case PEF_LoadLibraryExA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        if (name)
            v = load_library(ctx, name, service == PEF_LoadLibraryA || !(pe_arg(a, 2) & 3u));
        break;
    }
    case PEF_FreeLibrary: {
        int m = module_index(ctx, pe_arg(a, 0));
        if (m > 0 && img->mod[m].refs)
            img->mod[m].refs--;
        v = 1;
        break;
    }
    case PEF_GetVersion: v = 0xc3b60004u; break;     /* Windows 95 */
    case PEF_GetVersionExA: {
        uint8_t *o = (uint8_t *)pe_buf(a, pe_arg(a, 0), 148);
        if (!o)
            break;
        memset(o + 4, 0, 144);
        pe_wr32(o + 4, 4);
        pe_wr32(o + 8, 0);
        pe_wr32(o + 12, 0x040003b6u);
        pe_wr32(o + 16, 1);                 /* VER_PLATFORM_WIN32_WINDOWS */
        v = 1;
        break;
    }
    case PEF_GetStartupInfoA: {
        uint8_t *s = (uint8_t *)pe_buf(a, pe_arg(a, 0), 68);
        if (s) {
            memset(s, 0, 68);
            pe_wr32(s, 68);
            pe_wr32(s + 44, 1);             /* STARTF_USESHOWWINDOW */
            pe_wr16(s + 48, (uint16_t)ctx->show);
            pe_wr32(s + 56, 0xffffffffu);   /* no standard handles of its own */
            pe_wr32(s + 60, 0xffffffffu);
            pe_wr32(s + 64, 0xffffffffu);
        }
        v = 1;
        break;
    }
    case PEF_GetEnvironmentStrings:
    case PEF_GetEnvironmentStringsA: v = ctx->environment; break;
    case PEF_FreeEnvironmentStringsA: v = 1; break;
    case PEF_GetEnvironmentVariableA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        const char *val = name ? env_find(ctx, name) : NULL;
        if (!val) {
            ctx->last_error = ERROR_ENVVAR_NOT_FOUND;
            break;
        }
        v = put_str(a, pe_arg(a, 1), pe_arg(a, 2), val);
        break;
    }
    case PEF_SetEnvironmentVariableA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        const char *val = pe_arg(a, 1) ? pe_str(a, pe_arg(a, 1)) : NULL;
        v = name && !a->bad && env_set(ctx, name, val);
        break;
    }
    case PEF_GetCurrentProcess: v = CURRENT_PROCESS; break;
    case PEF_GetCurrentThread: v = CURRENT_THREAD; break;
    case PEF_GetCurrentProcessId:
    case PEF_GetCurrentThreadId: v = (uint32_t)ctx->task; break;
    case PEF_GetLastError: *eax = ctx->last_error; return 1;
    case PEF_SetLastError: ctx->last_error = pe_arg(a, 0); *eax = 0; return 1;
    case PEF_SetUnhandledExceptionFilter:
        v = ctx->exception_filter;
        ctx->exception_filter = pe_arg(a, 0);
        break;
    case PEF_UnhandledExceptionFilter: v = 1; break;     /* EXCEPTION_EXECUTE_HANDLER */
    case PEF_RaiseException:
        /* no handlers of its own are looked for yet: as a fault */
        ctx->fault.vector = -1;
        ctx->fault.flat = 1;
        ctx->fault.eip = pe_arg(a, 0);
        ctx->dead = 1;
        return WINX86_SERVICE_EXIT;
    case PEF_RtlUnwind: v = 0; break;
    case PEF_Sleep: {
        uint32_t ms = pe_arg(a, 0);
        DWORD start = GetTickCount();
        do {
            if (ctx->yield && ctx->yield()) {
                ctx->stopped = 1;
                return WINX86_SERVICE_EXIT;
            }
        } while (ms && ms != 0xffffffffu && GetTickCount() - start < ms);
        v = 0;
        break;
    }
    case PEF_QueryPerformanceFrequency:
    case PEF_QueryPerformanceCounter: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 0), 8);
        if (p)
            wr64(p, service == PEF_QueryPerformanceFrequency ? 1000u : GetTickCount());
        v = p != NULL;
        break;
    }
    case PEF_GetSystemTime:
    case PEF_GetLocalTime: {
        SYSTEMTIME_N *t = (SYSTEMTIME_N *)pe_buf(a, pe_arg(a, 0), 16);
        if (t)
            GetLocalTimeN(t);
        v = 1;
        break;
    }
    case PEF_GetSystemTimeAsFileTime: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 0), 8);
        SYSTEMTIME_N t;
        GetLocalTimeN(&t);
        if (p)
            wr64(p, ft_of(&t));
        v = 1;
        break;
    }
    case PEF_FileTimeToSystemTime: {
        const uint8_t *f = (const uint8_t *)pe_buf(a, pe_arg(a, 0), 8);
        SYSTEMTIME_N *t = (SYSTEMTIME_N *)pe_buf(a, pe_arg(a, 1), 16);
        if (f && t)
            st_of(rd64(f), t);
        v = f && t;
        break;
    }
    case PEF_SystemTimeToFileTime: {
        const SYSTEMTIME_N *t = (const SYSTEMTIME_N *)pe_buf(a, pe_arg(a, 0), 16);
        uint8_t *f = (uint8_t *)pe_buf(a, pe_arg(a, 1), 8);
        if (f && t)
            wr64(f, ft_of(t));
        v = f && t;
        break;
    }
    case PEF_FileTimeToLocalFileTime:
    case PEF_LocalFileTimeToFileTime: {
        const uint8_t *f = (const uint8_t *)pe_buf(a, pe_arg(a, 0), 8);
        uint8_t *t = (uint8_t *)pe_buf(a, pe_arg(a, 1), 8);
        if (f && t)
            memmove(t, f, 8);
        v = f && t;
        break;
    }
    case PEF_CompareFileTime: {
        const uint8_t *x = (const uint8_t *)pe_buf(a, pe_arg(a, 0), 8);
        const uint8_t *y = (const uint8_t *)pe_buf(a, pe_arg(a, 1), 8);
        if (x && y)
            v = rd64(x) < rd64(y) ? 0xffffffffu : rd64(x) > rd64(y);
        break;
    }
    case PEF_FileTimeToDosDateTime: {
        const uint8_t *f = (const uint8_t *)pe_buf(a, pe_arg(a, 0), 8);
        uint8_t *d = (uint8_t *)pe_buf(a, pe_arg(a, 1), 2);
        uint8_t *t = (uint8_t *)pe_buf(a, pe_arg(a, 2), 2);
        if (f && d && t) {
            WORD date, time;
            dos_of_ft(rd64(f), &date, &time);
            pe_wr16(d, date);
            pe_wr16(t, time);
        }
        v = f && d && t;
        break;
    }
    case PEF_DosDateTimeToFileTime: {
        uint8_t *f = (uint8_t *)pe_buf(a, pe_arg(a, 2), 8);
        if (f)
            wr64(f, ft_of_dos((WORD)pe_arg(a, 0), (WORD)pe_arg(a, 1)));
        v = f != NULL;
        break;
    }
    case PEF_GetTimeZoneInformation: {
        uint8_t *z = (uint8_t *)pe_buf(a, pe_arg(a, 0), 172);
        if (z)
            memset(z, 0, 172);
        v = 0;                              /* TIME_ZONE_ID_UNKNOWN */
        break;
    }
    case PEF_GetSystemTimeAdjustment: v = 0; break;
    case PEF_GetDateFormatA:
    case PEF_GetTimeFormatA: {
        uint32_t flags = pe_arg(a, 1), st = pe_arg(a, 2), pic = pe_arg(a, 3);
        uint32_t buf = pe_arg(a, 4), size = pe_arg(a, 5);
        SYSTEMTIME_N t;
        if (st) {
            const SYSTEMTIME_N *s = (const SYSTEMTIME_N *)pe_buf(a, st, 16);
            if (!s)
                break;
            t = *s;
        } else {
            GetLocalTimeN(&t);
        }
        t.wDayOfWeek = (WORD)((days_of(t.wYear, t.wMonth, t.wDay) + 1) % 7);
        const char *p = pic ? pe_str(a, pic) : NULL;
        if (!p)
            p = service == PEF_GetTimeFormatA ? "h:mm:ss tt" :
                (flags & 2u) ? "dddd, MMMM dd, yyyy" : "M/d/yy";
        char out[96];
        format_time(&t, p, out, sizeof(out));
        uint32_t n = (uint32_t)strlen(out) + 1u;
        if (!size)
            v = n;
        else if (size < n)
            ctx->last_error = ERROR_INSUFFICIENT_BUFFER;
        else if ((p = (const char *)pe_buf(a, buf, n)) != NULL)
            memcpy((char *)p, out, n), v = n;
        break;
    }
    case PEF_GetACP: v = 1252; break;
    case PEF_GetOEMCP: v = 437; break;
    case PEF_IsValidCodePage: {
        uint32_t cp = pe_arg(a, 0);
        v = cp == 437 || cp == 850 || cp == 866 || cp == 1252;
        break;
    }
    case PEF_GetCPInfo: {
        uint8_t *c = (uint8_t *)pe_buf(a, pe_arg(a, 1), 20);
        if (c) {
            memset(c, 0, 20);
            pe_wr32(c, 1);
            c[4] = '?';
        }
        v = c != NULL;
        break;
    }
    case PEF_IsDBCSLeadByte: v = 0; break;
    case PEF_MultiByteToWideChar: {
        uint32_t src = pe_arg(a, 2), dst = pe_arg(a, 4);
        int32_t len = (int32_t)pe_arg(a, 3), size = (int32_t)pe_arg(a, 5);
        const char *s = len < 0 ? pe_str(a, src) : (const char *)pe_buf(a, src, (uint32_t)len);
        if (!s && len)
            break;
        uint32_t n = len < 0 ? (uint32_t)strlen(s) + 1u : (uint32_t)len;
        if (!size) {
            v = n;
            break;
        }
        if ((uint32_t)size < n) {
            ctx->last_error = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        uint8_t *d = (uint8_t *)pe_buf(a, dst, 2u * n);
        for (uint32_t i = 0; d && i < n; i++)
            pe_wr16(d + 2 * i, (uint8_t)s[i]);
        v = d ? n : 0;
        break;
    }
    case PEF_WideCharToMultiByte: {
        uint32_t src = pe_arg(a, 2), dst = pe_arg(a, 4), used = pe_arg(a, 7);
        int32_t len = (int32_t)pe_arg(a, 3), size = (int32_t)pe_arg(a, 5);
        uint32_t n = 0;
        if (len < 0) {
            const uint8_t *w;
            while ((w = (const uint8_t *)pe_buf(a, src + 2u * n, 2)) != NULL && pe_rd16(w))
                n++;
            n++;
        } else {
            n = (uint32_t)len;
        }
        const uint8_t *w = (const uint8_t *)pe_buf(a, src, 2u * n);
        if (!w && n)
            break;
        if (!size) {
            v = n;
            break;
        }
        if ((uint32_t)size < n) {
            ctx->last_error = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        char *d = (char *)pe_buf(a, dst, n);
        int missed = 0;
        for (uint32_t i = 0; d && i < n; i++) {
            uint16_t c = pe_rd16(w + 2 * i);
            d[i] = c < 256 ? (char)c : '?';
            missed |= c >= 256;
        }
        if (used) {
            uint8_t *u = (uint8_t *)pe_buf(a, used, 4);
            if (u)
                pe_wr32(u, (uint32_t)missed);
        }
        v = d ? n : 0;
        break;
    }
    case PEF_GetStringTypeA:
    case PEF_GetStringTypeW: {
        int w = service == PEF_GetStringTypeW;
        uint32_t type = pe_arg(a, w ? 0 : 1), src = pe_arg(a, w ? 1 : 2);
        int32_t len = (int32_t)pe_arg(a, w ? 2 : 3);
        uint32_t out = pe_arg(a, w ? 3 : 4), n = 0;
        if (len < 0) {
            if (w) {
                const uint8_t *c;
                while ((c = (const uint8_t *)pe_buf(a, src + 2u * n, 2)) != NULL && pe_rd16(c))
                    n++;
            } else {
                const char *s = pe_str(a, src);
                n = s ? (uint32_t)strlen(s) : 0;
            }
            n++;
        } else {
            n = (uint32_t)len;
        }
        const uint8_t *s = (const uint8_t *)pe_buf(a, src, w ? 2u * n : n);
        uint8_t *o = (uint8_t *)pe_buf(a, out, 2u * n);
        if (type != 1 || !s || !o)
            break;
        for (uint32_t i = 0; i < n; i++)
            pe_wr16(o + 2 * i, (uint16_t)ctype(w ? (pe_rd16(s + 2 * i) < 256 ? pe_rd16(s + 2 * i) : 'A')
                                                : s[i]));
        v = 1;
        break;
    }
    case PEF_LCMapStringA:
    case PEF_LCMapStringW: {
        int w = service == PEF_LCMapStringW, sz = w ? 2 : 1;
        uint32_t flags = pe_arg(a, 1), src = pe_arg(a, 2), dst = pe_arg(a, 4);
        int32_t len = (int32_t)pe_arg(a, 3), size = (int32_t)pe_arg(a, 5);
        uint32_t n = 0;
        if (len < 0) {
            const uint8_t *c;
            while ((c = (const uint8_t *)pe_buf(a, src + (uint32_t)sz * n, (uint32_t)sz)) != NULL &&
                   (w ? pe_rd16(c) : *c))
                n++;
            n++;
        } else {
            n = (uint32_t)len;
        }
        const uint8_t *s = (const uint8_t *)pe_buf(a, src, (uint32_t)sz * n);
        if (!s)
            break;
        if (!size) {
            v = n;
            break;
        }
        if ((uint32_t)size < n)
            break;
        uint8_t *d = (uint8_t *)pe_buf(a, dst, (uint32_t)sz * n);
        for (uint32_t i = 0; d && i < n; i++) {
            unsigned c = w ? pe_rd16(s + 2 * i) : s[i];
            if ((flags & 0x100u) && c < 256 && (ctype((int)c) & 1))
                c += 32;
            else if ((flags & 0x200u) && c < 256 && (ctype((int)c) & 2) && c != 0xdf && c != 0xff)
                c -= 32;
            if (w)
                pe_wr16(d + 2 * i, (uint16_t)c);
            else
                d[i] = (uint8_t)c;
        }
        v = d ? n : 0;
        break;
    }
    case PEF_CompareStringA: {
        uint32_t flags = pe_arg(a, 1);
        int32_t n1 = (int32_t)pe_arg(a, 3), n2 = (int32_t)pe_arg(a, 5);
        const char *s1 = n1 < 0 ? pe_str(a, pe_arg(a, 2)) : (const char *)pe_buf(a, pe_arg(a, 2), (uint32_t)n1);
        const char *s2 = n2 < 0 ? pe_str(a, pe_arg(a, 4)) : (const char *)pe_buf(a, pe_arg(a, 4), (uint32_t)n2);
        if ((!s1 && n1) || (!s2 && n2))
            break;
        size_t l1 = n1 < 0 ? strlen(s1) : (size_t)n1, l2 = n2 < 0 ? strlen(s2) : (size_t)n2;
        int r = 0;
        for (size_t i = 0; !r && i < l1 && i < l2; i++) {
            int c1 = (uint8_t)s1[i], c2 = (uint8_t)s2[i];
            if (flags & 1u) {
                if (ctype(c1) & 1) c1 += 32;
                if (ctype(c2) & 1) c2 += 32;
            }
            r = c1 - c2;
        }
        if (!r)
            r = l1 < l2 ? -1 : l1 > l2;
        v = r < 0 ? 1 : r > 0 ? 3 : 2;
        break;
    }
    case PEF_GetStdHandle: {
        int32_t which = (int32_t)pe_arg(a, 0);
        v = which >= -12 && which <= -10 ? PE_HANDLE(-10 - which) : 0xffffffffu;
        if (v != 0xffffffffu && ctx->handle[v - PE_HANDLE(0)].value)
            v = ctx->handle[v - PE_HANDLE(0)].value;     /* SetStdHandle's */
        break;
    }
    case PEF_SetStdHandle: {
        int32_t which = (int32_t)pe_arg(a, 0);
        if (which >= -12 && which <= -10)
            ctx->handle[-10 - which].value = pe_arg(a, 1) == PE_HANDLE(-10 - which) ? 0 : pe_arg(a, 1);
        v = 1;
        break;
    }
    case PEF_GetFileType: {
        uint32_t h = pe_arg(a, 0);
        v = std_of(ctx, h) >= 0 ? 2 : handle_index(ctx, h, PE_H_FILE) >= 0 ? 1 : 0;
        break;
    }
    case PEF_CreateFileA: v = create_file(a, ctx); break;
    case PEF_ReadFile:
    case PEF_WriteFile: {
        uint32_t h = pe_arg(a, 0), buf = pe_arg(a, 1), n = pe_arg(a, 2), done = pe_arg(a, 3);
        uint8_t *dp = done ? (uint8_t *)pe_buf(a, done, 4) : NULL;
        void *p = n ? pe_buf(a, buf, n) : NULL;
        if (a->bad)
            break;
        uint32_t got = 0;
        int s = std_of(ctx, h);
        if (s >= 0) {
            got = service == PEF_WriteFile ? n : 0;     /* nowhere, nothing */
            v = 1;
        } else {
            int fd = file_of(ctx, h);
            if (fd < 0)
                break;
            LONG r = service == PEF_ReadFile ? _hread((HFILE)fd, p, (LONG)n)
                                             : _hwrite((HFILE)fd, p, (LONG)n);
            if (r < 0) {
                ctx->last_error = ERROR_ACCESS_DENIED;
                r = 0;
            } else {
                v = 1;
            }
            got = (uint32_t)r;
        }
        if (dp)
            pe_wr32(dp, got);
        break;
    }
    case PEF_SetFilePointer: {
        int fd = file_of(ctx, pe_arg(a, 0));
        uint32_t high = pe_arg(a, 2);
        uint8_t *hp = high ? (uint8_t *)pe_buf(a, high, 4) : NULL;
        if (fd < 0 || a->bad) {
            v = 0xffffffffu;
            break;
        }
        LONG r = _llseek((HFILE)fd, (LONG)pe_arg(a, 1), (int)pe_arg(a, 3));
        if (hp)
            pe_wr32(hp, 0);
        v = r < 0 ? 0xffffffffu : (uint32_t)r;
        break;
    }
    case PEF_GetFileSize: {
        int fd = file_of(ctx, pe_arg(a, 0));
        uint32_t high = pe_arg(a, 1);
        uint8_t *hp = high ? (uint8_t *)pe_buf(a, high, 4) : NULL;
        if (fd < 0) {
            v = 0xffffffffu;
            break;
        }
        LONG at = _llseek((HFILE)fd, 0, 1), end = _llseek((HFILE)fd, 0, 2);
        _llseek((HFILE)fd, at, 0);
        if (hp)
            pe_wr32(hp, 0);
        v = (uint32_t)end;
        break;
    }
    case PEF_FlushFileBuffers: v = 1; break;
    case PEF_SetEndOfFile: {
        int fd = file_of(ctx, pe_arg(a, 0));
        v = fd >= 0 && _hwrite((HFILE)fd, "", 0) == 0;
        break;
    }
    case PEF_GetFileTime:
    case PEF_SetFileTime: {
        int fd = file_of(ctx, pe_arg(a, 0));
        if (fd < 0)
            break;
        WORD date = 0, time = 0;
        uint8_t *w = pe_arg(a, 3) ? (uint8_t *)pe_buf(a, pe_arg(a, 3), 8) : NULL;
        if (service == PEF_SetFileTime) {
            if (w) {
                dos_of_ft(rd64(w), &date, &time);
                v = win_file_time((HFILE)fd, 1, &date, &time);
            } else {
                v = 1;
            }
            break;
        }
        if (!win_file_time((HFILE)fd, 0, &date, &time))
            break;
        uint64_t ft = ft_of_dos(date, time);
        for (unsigned k = 1; k <= 3; k++) {
            uint8_t *p = pe_arg(a, k) ? (uint8_t *)pe_buf(a, pe_arg(a, k), 8) : NULL;
            if (p)
                wr64(p, ft);
        }
        v = 1;
        break;
    }
    case PEF_CloseHandle: {
        uint32_t i = pe_arg(a, 0) - PE_HANDLE(0);
        if (i >= PE_HANDLES || !ctx->handle[i].kind) {
            ctx->last_error = ERROR_INVALID_HANDLE;
            break;
        }
        if (ctx->handle[i].kind == PE_H_STD) {
            v = 1;
            break;
        }
        if (ctx->handle[i].kind == PE_H_FILE)
            _lclose((HFILE)ctx->handle[i].fd);
        if (ctx->handle[i].kind == PE_H_FIND || ctx->handle[i].kind == PE_H_HEAP)
            break;                          /* not theirs to close */
        ctx->handle[i].kind = PE_H_FREE;
        v = 1;
        break;
    }
    case PEF_DuplicateHandle: {
        uint8_t *d = (uint8_t *)pe_buf(a, pe_arg(a, 3), 4);
        if (d)
            pe_wr32(d, pe_arg(a, 1));
        v = d != NULL;
        break;
    }
    case PEF_GetFileAttributesA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        int at = name ? win_file_attr(name, 0, 0) : -1;
        if (at < 0) {
            ctx->last_error = ERROR_FILE_NOT_FOUND;
            v = 0xffffffffu;
        } else {
            v = at & 0x37 ? (uint32_t)(at & 0x37) : 0x80u;
        }
        break;
    }
    case PEF_SetFileAttributesA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        v = name && win_file_attr(name, 1, pe_arg(a, 1) & 0x27u) >= 0;
        break;
    }
    case PEF_FindFirstFileA: {
        const char *spec = pe_str(a, pe_arg(a, 0));
        uint8_t *d = (uint8_t *)pe_buf(a, pe_arg(a, 1), 320);
        v = 0xffffffffu;
        if (!spec || !d)
            break;
        pe_find *f = (pe_find *)malloc(sizeof(pe_find) + 255 * sizeof(WINFIND_N));
        if (!f)
            break;
        f->n = WinFindFilesN(spec, 0x16, f->e, 256);
        f->at = 0;
        int i = f->n ? new_handle(ctx, PE_H_FIND, 0, (uint32_t)(uintptr_t)f) : -1;
        if (i < 0) {
            free(f);
            ctx->last_error = ERROR_FILE_NOT_FOUND;
            break;
        }
        fill_find(d, &f->e[f->at++]);
        v = PE_HANDLE(i);
        break;
    }
    case PEF_FindNextFileA: {
        int i = handle_index(ctx, pe_arg(a, 0), PE_H_FIND);
        uint8_t *d = (uint8_t *)pe_buf(a, pe_arg(a, 1), 320);
        if (i < 0 || !d)
            break;
        pe_find *f = (pe_find *)(uintptr_t)ctx->handle[i].value;
        if (f->at >= f->n) {
            ctx->last_error = ERROR_NO_MORE_FILES;
            break;
        }
        fill_find(d, &f->e[f->at++]);
        v = 1;
        break;
    }
    case PEF_FindClose: {
        int i = handle_index(ctx, pe_arg(a, 0), PE_H_FIND);
        if (i < 0)
            break;
        free((void *)(uintptr_t)ctx->handle[i].value);
        ctx->handle[i].kind = PE_H_FREE;
        v = 1;
        break;
    }
    case PEF_DeleteFileA: { const char *s = pe_str(a, pe_arg(a, 0)); v = s && DeleteFileN(s); break; }
    case PEF_CreateDirectoryA: { const char *s = pe_str(a, pe_arg(a, 0)); v = s && CreateDirectoryN(s); break; }
    case PEF_RemoveDirectoryA: { const char *s = pe_str(a, pe_arg(a, 0)); v = s && RemoveDirectoryN(s); break; }
    case PEF_MoveFileA: {
        const char *s = pe_str(a, pe_arg(a, 0)), *d = pe_str(a, pe_arg(a, 1));
        v = s && d && MoveFileN(s, d);
        break;
    }
    case PEF_GetShortPathNameA: {
        const char *name = pe_str(a, pe_arg(a, 0));
        if (name)
            v = put_str(a, pe_arg(a, 1), pe_arg(a, 2), name);
        break;
    }
    case PEF_GetCurrentDirectoryA: {
        char cwd[160];
        GetCurrentDirectoryN(cwd, sizeof(cwd));
        v = put_str(a, pe_arg(a, 1), pe_arg(a, 0), cwd);
        break;
    }
    case PEF_SetCurrentDirectoryA: {
        const char *s = pe_str(a, pe_arg(a, 0));
        v = s && win_file_chdir(s);
        if (!v)
            ctx->last_error = ERROR_PATH_NOT_FOUND;
        break;
    }
    case PEF_GetDriveTypeA: {
        const char *root = pe_arg(a, 0) ? pe_str(a, pe_arg(a, 0)) : NULL;
        v = GetDriveType(drive_of(root));
        if (!v)
            v = 1;                          /* DRIVE_NO_ROOT_DIR */
        break;
    }
    case PEF_GetLogicalDrives:
        for (int d = 0; d < 26; d++)
            if (GetDriveType(d))
                v |= 1u << d;
        break;
    case PEF_GetTempFileNameA: {
        const char *dir = pe_str(a, pe_arg(a, 0)), *pre = pe_str(a, pe_arg(a, 1));
        uint32_t unique = pe_arg(a, 2) & 0xffffu;
        char *buf = (char *)pe_buf(a, pe_arg(a, 3), 260);
        if (!dir || !pre || !buf)
            break;
        size_t n = strlen(dir);
        for (int tries = 0; tries < 1000; tries++) {
            uint32_t u = unique ? unique : (GetTickCount() + (uint32_t)tries) & 0xffffu;
            if (!u)
                continue;
            pe_fmt(buf, 260, "%s%s%.3s%X.TMP", dir, n && dir[n - 1] != '\\' ? "\\" : "", pre,
                     (unsigned)u);
            if (unique) {
                v = u;
                break;
            }
            if (win_file_attr(buf, 0, 0) < 0) {
                HFILE f = _lcreat(buf, 0);
                if (f != HFILE_ERROR)
                    _lclose(f);
                v = u;
                break;
            }
        }
        break;
    }
    case PEF__lopen: { const char *s = pe_str(a, pe_arg(a, 0)); v = s ? (uint32_t)(int32_t)_lopen(s, (int)pe_arg(a, 1)) : 0xffffffffu; break; }
    case PEF__lcreat: { const char *s = pe_str(a, pe_arg(a, 0)); v = s ? (uint32_t)(int32_t)_lcreat(s, (int)pe_arg(a, 1)) : 0xffffffffu; break; }
    case PEF__lclose: v = (uint32_t)(int32_t)_lclose((HFILE)pe_arg(a, 0)); break;
    case PEF__llseek: v = (uint32_t)_llseek((HFILE)pe_arg(a, 0), (LONG)pe_arg(a, 1), (int)pe_arg(a, 2)); break;
    case PEF__lread:
    case PEF__lwrite: {
        uint32_t n = pe_arg(a, 2);
        void *p = n ? pe_buf(a, pe_arg(a, 1), n) : NULL;
        if (a->bad)
            break;
        v = (uint32_t)(service == PEF__lread ? _hread((HFILE)pe_arg(a, 0), p, (LONG)n)
                                             : _hwrite((HFILE)pe_arg(a, 0), p, (LONG)n));
        break;
    }
    case PEF_SetFileApisToOEM: v = 0; break;
    case PEF_AreFileApisANSI: v = 1; break;

    /* memory */
    case PEF_VirtualAlloc: {
        uint32_t at = pe_arg(a, 0), size = pe_arg(a, 1), type = pe_arg(a, 2);
        win_pe_mem *m = at ? win_pe_mem_of(img, at) : NULL;
        if (m && m->kind == PE_MEM_VIRTUAL) {
            v = at <= m->base + m->size - size ? at & ~0xfffu : 0;   /* committed already */
            break;
        }
        if (at && !(type & 0x2000u) && !m) {
            ctx->last_error = ERROR_INVALID_PARAMETER;
            break;
        }
        size = (size + 0xfffu) & ~0xfffu;
        v = size <= 0x1000000u ? win_pe_alloc(img, size, 4096, PE_MEM_VIRTUAL) : 0;
        if (!v)
            ctx->last_error = ERROR_NOT_ENOUGH_MEMORY;
        break;
    }
    case PEF_VirtualFree: {
        win_pe_mem *m = win_pe_mem_of(img, pe_arg(a, 0));
        if (!m || m->kind != PE_MEM_VIRTUAL)
            break;
        v = (pe_arg(a, 2) & 0x8000u) ? win_pe_free(img, m->base) : 1;
        break;
    }
    case PEF_VirtualQuery: {
        uint32_t at = pe_arg(a, 0);
        uint8_t *mbi = (uint8_t *)pe_buf(a, pe_arg(a, 1), 28);
        if (!mbi)
            break;
        memset(mbi, 0, 28);
        win_pe_mem *m = win_pe_mem_of(img, at);
        uint32_t page = at & ~0xfffu;
        pe_wr32(mbi, page);
        if (m) {
            pe_wr32(mbi + 4, m->base);
            pe_wr32(mbi + 8, 0x40);
            pe_wr32(mbi + 12, m->base + m->size - page);
            pe_wr32(mbi + 16, 0x1000);      /* MEM_COMMIT */
            pe_wr32(mbi + 20, 0x40);        /* PAGE_EXECUTE_READWRITE */
            pe_wr32(mbi + 24, m->kind == PE_MEM_IMAGE ? 0x1000000u : 0x20000u);
        } else {
            pe_wr32(mbi + 12, 0x1000);
            pe_wr32(mbi + 16, 0x10000);     /* MEM_FREE */
            pe_wr32(mbi + 20, 1);
        }
        v = 28;
        break;
    }
    case PEF_VirtualProtect: {
        uint8_t *old = (uint8_t *)pe_buf(a, pe_arg(a, 3), 4);
        if (old)
            pe_wr32(old, 0x40);
        v = 1;
        break;
    }
    case PEF_GetProcessHeap: v = ctx->heap; break;
    case PEF_HeapCreate: v = heap_create(ctx); break;
    case PEF_HeapDestroy:
        if (pe_arg(a, 0) != ctx->heap && heap_of(ctx, pe_arg(a, 0))) {
            heap_destroy(ctx, pe_arg(a, 0));
            v = 1;
        }
        break;
    case PEF_HeapAlloc:
        v = pe_heap_alloc(ctx, pe_arg(a, 0), pe_arg(a, 2), (pe_arg(a, 1) & 8u) != 0);
        if (!v)
            ctx->last_error = ERROR_NOT_ENOUGH_MEMORY;
        break;
    case PEF_HeapFree: v = pe_heap_free(ctx, pe_arg(a, 0), pe_arg(a, 2)) || !pe_arg(a, 2); break;
    case PEF_HeapReAlloc: v = heap_realloc(ctx, pe_arg(a, 0), pe_arg(a, 2), pe_arg(a, 3), pe_arg(a, 1)); break;
    case PEF_HeapSize: v = heap_size(ctx, pe_arg(a, 0), pe_arg(a, 2)); break;
    case PEF_HeapValidate: v = 1; break;
    case PEF_HeapCompact: v = 0x10000u; break;
    /* global and local memory: blocks of the process's heap, the handle
     * the pointer */
    case PEF_GlobalAlloc:
    case PEF_LocalAlloc:
        v = pe_heap_alloc(ctx, ctx->heap, pe_arg(a, 1) ? pe_arg(a, 1) : 1u, (pe_arg(a, 0) & 0x40u) != 0);
        if (!v)
            ctx->last_error = ERROR_NOT_ENOUGH_MEMORY;
        break;
    case PEF_GlobalReAlloc:
    case PEF_LocalReAlloc:
        if (pe_arg(a, 2) & 0x80u) {         /* GMEM_MODIFY */
            v = pe_arg(a, 0);
            break;
        }
        v = heap_realloc(ctx, ctx->heap, pe_arg(a, 0), pe_arg(a, 1) ? pe_arg(a, 1) : 1u,
                         (pe_arg(a, 2) & 0x40u) ? 8u : 0u);
        break;
    case PEF_GlobalFree:
    case PEF_LocalFree:
        v = pe_heap_free(ctx, ctx->heap, pe_arg(a, 0)) || !pe_arg(a, 0) ? 0 : pe_arg(a, 0);
        break;
    case PEF_GlobalLock:
    case PEF_LocalLock:
    case PEF_GlobalHandle:
    case PEF_LocalHandle:
        v = heap_block(ctx, heap_of(ctx, ctx->heap), pe_arg(a, 0)) ? pe_arg(a, 0) : 0;
        break;
    case PEF_GlobalUnlock:
    case PEF_LocalUnlock:
    case PEF_GlobalFlags:
    case PEF_LocalFlags:
        v = 0;
        break;
    case PEF_GlobalSize:
    case PEF_LocalSize:
        v = heap_size(ctx, ctx->heap, pe_arg(a, 0));
        if (v == 0xffffffffu)
            v = 0;
        break;
    case PEF_GlobalMemoryStatus: {
        uint8_t *m = (uint8_t *)pe_buf(a, pe_arg(a, 0), 32);
        win_ne_mem_info mi;
        win_ne_guest_mem_info(&mi);
        if (m) {
            uint32_t avail = mi.ext_size > mi.ext_used ? mi.ext_size - mi.ext_used : 0;
            pe_wr32(m, 32);
            pe_wr32(m + 4, mi.ext_size ? (uint32_t)((uint64_t)mi.ext_used * 100u / mi.ext_size) : 0);
            pe_wr32(m + 8, mi.ext_size);
            pe_wr32(m + 12, avail);
            pe_wr32(m + 16, mi.ext_size);
            pe_wr32(m + 20, avail);
            pe_wr32(m + 24, 0x7ffe0000u);
            pe_wr32(m + 28, avail);
        }
        v = 0;
        break;
    }
    case PEF_GetSystemInfo: {
        uint8_t *s = (uint8_t *)pe_buf(a, pe_arg(a, 0), 36);
        if (s) {
            memset(s, 0, 36);
            pe_wr32(s + 4, 4096);
            pe_wr32(s + 8, 0x10000);
            pe_wr32(s + 12, 0x7ffeffffu);
            pe_wr32(s + 16, 1);
            pe_wr32(s + 20, 1);
            pe_wr32(s + 24, 386);
            pe_wr32(s + 28, 0x10000);
            pe_wr16(s + 32, 3);
        }
        v = 0;
        break;
    }
    case PEF_IsBadReadPtr:
    case PEF_IsBadWritePtr:
    case PEF_IsBadCodePtr: {
        uint32_t n = service == PEF_IsBadCodePtr ? 1u : pe_arg(a, 1);
        v = n && !win_pe_ptr(img, pe_arg(a, 0), n);
        break;
    }
    case PEF_IsBadStringPtrA: {
        pe_args b = *a;
        v = !pe_str(&b, pe_arg(a, 0));
        break;
    }

    /* the thread's */
    case PEF_TlsAlloc:
        v = 0xffffffffu;
        for (uint32_t i = 0; i < WIN_PE_TLS_SLOTS; i++)
            if (!(ctx->tls_used[i / 32] & (1u << (i % 32)))) {
                ctx->tls_used[i / 32] |= 1u << (i % 32);
                pe_wr32((uint8_t *)win_pe_ptr(img, img->tib + WIN_PE_TIB_TLS + 4u * i, 4), 0);
                v = i;
                break;
            }
        break;
    case PEF_TlsFree: {
        uint32_t i = pe_arg(a, 0);
        if (i < WIN_PE_TLS_SLOTS) {
            ctx->tls_used[i / 32] &= ~(1u << (i % 32));
            v = 1;
        }
        break;
    }
    case PEF_TlsGetValue:
    case PEF_TlsSetValue: {
        uint32_t i = pe_arg(a, 0);
        if (i >= WIN_PE_TLS_SLOTS) {
            ctx->last_error = ERROR_INVALID_PARAMETER;
            break;
        }
        uint8_t *slot = (uint8_t *)win_pe_ptr(img, img->tib + WIN_PE_TIB_TLS + 4u * i, 4);
        if (service == PEF_TlsSetValue) {
            pe_wr32(slot, pe_arg(a, 1));
            v = 1;
        } else {
            v = pe_rd32(slot);
            ctx->last_error = 0;
        }
        break;
    }
    case PEF_InitializeCriticalSection: {
        uint8_t *c = (uint8_t *)pe_buf(a, pe_arg(a, 0), 24);
        if (c)
            memset(c, 0, 24);
        v = 0;
        break;
    }
    case PEF_EnterCriticalSection:
    case PEF_LeaveCriticalSection:
    case PEF_DeleteCriticalSection:
        v = 0;
        break;
    case PEF_InterlockedIncrement:
    case PEF_InterlockedDecrement:
    case PEF_InterlockedExchange: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 0), 4);
        if (!p)
            break;
        uint32_t old = pe_rd32(p);
        v = service == PEF_InterlockedIncrement ? old + 1u :
            service == PEF_InterlockedDecrement ? old - 1u : pe_arg(a, 1);
        pe_wr32(p, v);
        if (service == PEF_InterlockedExchange)
            v = old;
        break;
    }
    case PEF_CreateEventA:
    case PEF_CreateMutexA: {
        int i = new_handle(ctx, service == PEF_CreateEventA ? PE_H_EVENT : PE_H_MUTEX,
                           service == PEF_CreateEventA ? (int32_t)pe_arg(a, 1) : 0,
                           service == PEF_CreateEventA ? pe_arg(a, 2) : 1);
        v = i < 0 ? 0 : PE_HANDLE(i);
        break;
    }
    case PEF_SetEvent:
    case PEF_ResetEvent: {
        int i = handle_index(ctx, pe_arg(a, 0), PE_H_EVENT);
        if (i >= 0) {
            ctx->handle[i].value = service == PEF_SetEvent;
            v = 1;
        }
        break;
    }
    case PEF_ReleaseMutex: v = handle_index(ctx, pe_arg(a, 0), PE_H_MUTEX) >= 0; break;
    case PEF_WaitForSingleObject: {
        uint32_t h = pe_arg(a, 0), ms = pe_arg(a, 1);
        int i = handle_index(ctx, h, PE_H_EVENT);
        if (i < 0 || ctx->handle[i].value) {
            if (i >= 0 && !ctx->handle[i].fd)       /* auto-reset */
                ctx->handle[i].value = 0;
            v = 0;                          /* WAIT_OBJECT_0 */
            break;
        }
        /* nothing else can set it: its time out, the other tasks running */
        DWORD start = GetTickCount();
        while (ms && ms != 0xffffffffu && GetTickCount() - start < ms)
            if (ctx->yield && ctx->yield()) {
                ctx->stopped = 1;
                return WINX86_SERVICE_EXIT;
            }
        v = ms == 0xffffffffu ? 0 : 0x102u;
        break;
    }
    case PEF_GetExitCodeProcess: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 1), 4);
        if (p)
            pe_wr32(p, 259);                /* STILL_ACTIVE */
        v = p != NULL;
        break;
    }
    case PEF_CreateThread: ctx->last_error = ERROR_NOT_SUPPORTED; v = 0; break;
    case PEF_SetConsoleCtrlHandler: v = 1; break;
    case PEF_GetConsoleMode:
    case PEF_SetConsoleMode:
    case PEF_GetNumberOfConsoleInputEvents:
        v = 0;
        break;

    /* resources */
    case PEF_FindResourceA:
    case PEF_FindResourceExA: {
        int ex = service == PEF_FindResourceExA;
        int m = module_index(ctx, pe_arg(a, 0));
        const char *name = res_name(a, pe_arg(a, ex ? 2 : 1));
        const char *type = res_name(a, pe_arg(a, ex ? 1 : 2));
        if (m >= 0 && !a->bad)
            v = win_pe_resource(img, m, type, name, NULL, NULL);
        if (!v)
            ctx->last_error = 1814;         /* ERROR_RESOURCE_NAME_NOT_FOUND */
        break;
    }
    case PEF_LoadResource: {
        int m = module_index(ctx, pe_arg(a, 0));
        v = win_pe_resource_data(img, m >= 0 ? m : win_pe_module_of(img, pe_arg(a, 1)),
                                 pe_arg(a, 1), NULL);
        break;
    }
    case PEF_LockResource: v = pe_arg(a, 0); break;
    case PEF_FreeResource: v = 0; break;
    case PEF_SizeofResource: {
        int m = module_index(ctx, pe_arg(a, 0));
        if (!win_pe_resource_data(img, m >= 0 ? m : win_pe_module_of(img, pe_arg(a, 1)),
                                  pe_arg(a, 1), &v))
            v = 0;
        break;
    }

    /* the rest */
    case PEF_DebugBreak: v = 0; break;
    case PEF_SetErrorMode: v = 0; break;
    case PEF_SetHandleCount: v = pe_arg(a, 0); break;
    case PEF_GetSystemDefaultLangID:
    case PEF_GetUserDefaultLangID:
    case PEF_GetSystemDefaultLCID:
    case PEF_GetUserDefaultLCID:
    case PEF_GetThreadLocale:
        v = 0x409;
        break;
    default:
        return kernel32_more(a, service, eax);
    }
    *eax = v;
    return 1;
}

/* ---------------------------------------------------------------- the dispatcher */

int win_pe_api(win_pe_ctx *ctx, uint32_t esp, uint16_t service, uint32_t *eax)
{
    pe_args a = { ctx, esp, 0 };
    *eax = 0;
    if (service == WIN_PE_SERVICE_START) {
        attach(ctx);
        *eax = ctx->img->mod[0].entry;
        return 1;
    }
    if (service >= WIN_PE_SERVICE_UNDEFINED && service < WIN_PE_SERVICE_UNDEFINED + WIN_PE_UNDEFINED) {
        ctx->unsupported = service;
        pe_fmt(ctx->undefined, sizeof(ctx->undefined), "%s",
                 ctx->img->undefined[service - WIN_PE_SERVICE_UNDEFINED]);
        return 0;
    }
    int r;
    if (service >= WIN_PE_SERVICE_NATIVE && service < WIN_PE_SERVICE_NATIVE + WIN_PE_NATIVE_PROCS) {
        r = pe_user_api(&a, service, eax);
        if (a.bad)
            ctx->invalid_args = 1;
        return r;
    }
    if (service >= NFNS)
        return 0;
    const win_pe_fn *f = &win_pe_fns[service];
    if (f->sig) {
        r = generic(&a, f, eax);
    } else {
        r = kernel32(&a, service, eax);
        if (r < 0)
            r = pe_user_api(&a, service, eax);
    }
    if (a.bad)
        ctx->invalid_args = 1;
    if (r <= 0) {
        ctx->unsupported = service;
        win_pe_service_name(service, ctx->undefined, sizeof(ctx->undefined));
        return 0;
    }
    return r;
}
#endif
