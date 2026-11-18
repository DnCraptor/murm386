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
#include "windll.h"


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

uint32_t pe_heap_size(win_pe_ctx *ctx, uint32_t heap, uint32_t p)
{
    uint32_t n = heap_size(ctx, heap, p);
    return n == 0xffffffffu ? 0 : n;
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
    win_pe_dlls_end(ctx);
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
        GetSystemDirectory(path, sizeof(path) - 16);
        lstrcat(path, "\\");
        lstrcat(path, win_pe_module_names[(h - NATIVE_HMODULE(0)) >> 16]);
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
static int kernel32_win32s(pe_args *a, uint16_t service, uint32_t *eax);

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
        return kernel32_win32s(a, service, eax);
    }
    *eax = v;
    return 1;
}

/* ---------------------------------------------------------------- exceptions */

/* An exception goes to the program's own dispatcher (its code, made at
 * the first one), on its stack: a block of the exception's record and
 * CONTEXT and the dispatcher's state. The dispatcher asks WINN for the next
 * handler of the chain at FS:[0] (WIN_PE_SERVICE_SEH, with the last one's
 * result), calls it (cdecl: the record, its frame, the CONTEXT, 0), and so
 * on; WINN's 0: the CONTEXT is loaded, the program goes on there. A
 * handler that does not return (RtlUnwind, then its __except block) leaves
 * nothing of WINN's behind. None to handle it: the program's
 * UnhandledExceptionFilter, then the Application Error. */
#define SEH_BLOCK   (24u + 80u + 0x2ccu)
#define SEH_RECORD  24u
#define SEH_CONTEXT (24u + 80u)

/* the dispatcher: [esp] the record, [esp+4] the CONTEXT, [esp+8] the frame
 * being called, [esp+12] the vector */
static uint32_t seh_dispatcher(win_pe_ctx *ctx)
{
    if (ctx->seh_code)
        return ctx->seh_code;
    win_pe_image *img = ctx->img;
    uint32_t thunk = win_pe_thunk(img, WIN_PE_SERVICE_SEH, 4);
    uint32_t code = thunk ? win_pe_alloc(img, 128, 16, PE_MEM_OTHER) : 0;
    uint8_t *p = code ? (uint8_t *)win_pe_ptr(img, code, 128) : NULL;
    if (!p)
        return 0;
    static const uint8_t body[] = {
        0x6a, 0xff,                         /* push -1: the first */
        0xe8, 0, 0, 0, 0,                   /* next: call the service (ret 4) */
        0x85, 0xc0,                         /* test eax,eax */
        0x74, 0x16,                         /* jz resume */
        0x6a, 0x00,                         /* push 0 */
        0xff, 0x74, 0x24, 0x08,             /* push [esp+8]: the CONTEXT */
        0xff, 0x74, 0x24, 0x10,             /* push [esp+16]: the frame */
        0xff, 0x74, 0x24, 0x0c,             /* push [esp+12]: the record */
        0xff, 0xd0,                         /* call eax */
        0x83, 0xc4, 0x10,                   /* add esp,16 */
        0x50,                               /* push eax: its result */
        0xeb, 0xe1,                         /* jmp next */
        /* resume: the CONTEXT's registers, EFLAGS, EIP on its stack */
        0x8b, 0x5c, 0x24, 0x04,             /* mov ebx,[esp+4] */
        0x8b, 0xa3, 0xc4, 0, 0, 0,          /* mov esp,[ebx+Esp] */
        0xff, 0xb3, 0xb8, 0, 0, 0,          /* push [ebx+Eip] */
        0xff, 0xb3, 0xc0, 0, 0, 0,          /* push [ebx+EFlags] */
        0x8b, 0x83, 0xb0, 0, 0, 0,          /* mov eax,[ebx+Eax] */
        0x8b, 0x8b, 0xac, 0, 0, 0,          /* mov ecx,[ebx+Ecx] */
        0x8b, 0x93, 0xa8, 0, 0, 0,          /* mov edx,[ebx+Edx] */
        0x8b, 0xb3, 0xa0, 0, 0, 0,          /* mov esi,[ebx+Esi] */
        0x8b, 0xbb, 0x9c, 0, 0, 0,          /* mov edi,[ebx+Edi] */
        0x8b, 0xab, 0xb4, 0, 0, 0,          /* mov ebp,[ebx+Ebp] */
        0x8b, 0x9b, 0xa4, 0, 0, 0,          /* mov ebx,[ebx+Ebx] */
        0x9d,                               /* popfd */
        0xc3,                               /* ret */
    };
    memcpy(p, body, sizeof(body));
    pe_wr32(p + 3, thunk - (code + 7u));
    ctx->seh_code = code;
    return code;
}

/* the vector a fault's exception code stands for (the Application
 * Error's words), -1 none */
static int code_vector(uint32_t code)
{
    switch (code) {
    case 0xc0000094u: return 0;
    case 0xc000001du: return 6;
    case 0xc00000fdu: return 12;
    case 0xc0000005u: case 0xc0000096u: return 13;
    default: return -1;
    }
}

uint32_t win_pe_fault_code(win_pe_ctx *ctx, int vector, uint32_t eip, uint32_t *params,
                           uint32_t *nparams)
{
    *nparams = 0;
    switch (vector) {
    case 0: return 0xc0000094u;             /* INTEGER_DIVIDE_BY_ZERO */
    case 1: return 0x80000004u;             /* SINGLE_STEP */
    case 3: return 0x80000003u;             /* BREAKPOINT */
    case 4: return 0xc0000095u;             /* INTEGER_OVERFLOW */
    case 5: return 0xc000008cu;             /* ARRAY_BOUNDS_EXCEEDED */
    case 6: case 7: return 0xc000001du;     /* ILLEGAL_INSTRUCTION */
    case 12: return 0xc00000fdu;            /* STACK_OVERFLOW */
    case 16: return 0xc0000090u;            /* FLT_INVALID_OPERATION */
    case 17: return 0x80000002u;            /* DATATYPE_MISALIGNMENT */
    case 13: {
        /* a privileged instruction, INT 3 (the gate a ring 0 one), or an
         * access violation */
        const uint8_t *p = (const uint8_t *)win_pe_ptr(ctx->img, eip, 2);
        uint8_t op = p ? p[0] : 0;
        if (op == 0xcc)
            return 0x80000003u;
        if (op == 0xfa || op == 0xfb || op == 0xf4 || (op >= 0xe4 && op <= 0xe7) ||
            (op >= 0xec && op <= 0xef) || (op >= 0x6c && op <= 0x6f) ||
            (op == 0x0f && p && (p[1] == 0x20 || p[1] == 0x22 || p[1] == 0x01)))
            return 0xc0000096u;             /* PRIVILEGED_INSTRUCTION */
        break;
    }
    }
    params[0] = 0;                          /* ACCESS_VIOLATION: reading, where unknown */
    params[1] = 0xffffffffu;
    *nparams = 2;
    return 0xc0000005u;
}

int win_pe_exception(win_pe_ctx *ctx, const win_pe_regs *r, int vector, uint32_t code,
                     uint32_t flags, uint32_t nparams, const uint32_t *params, uint32_t *eip,
                     uint32_t *esp)
{
    win_pe_image *img = ctx->img;
    uint32_t disp = seh_dispatcher(ctx);
    uint32_t block = (r->esp - 16u - SEH_BLOCK) & ~15u;
    uint8_t *b = disp && block - img->stack_lo >= 1024u && block < img->stack_top ?
                 (uint8_t *)win_pe_ptr(img, block, SEH_BLOCK) : NULL;
    if (!b) {                               /* no stack left for it: as Windows ends it */
        ctx->fault.vector = vector;
        ctx->fault.flat = 1;
        ctx->fault.eip = r->eip;
        int m = win_pe_module_of(img, r->eip);
        if (m >= 0) {
            memcpy(ctx->fault.module, img->mod[m].name, sizeof(ctx->fault.module));
            ctx->fault.eip -= img->mod[m].base;
        }
        ctx->dead = 1;
        return WINX86_SERVICE_EXIT;
    }
    memset(b, 0, SEH_BLOCK);
    pe_wr32(b, block + SEH_RECORD);
    pe_wr32(b + 4, block + SEH_CONTEXT);
    pe_wr32(b + 8, 0xffffffffu);
    pe_wr32(b + 12, (uint32_t)vector);
    pe_wr32(b + 16, block + SEH_RECORD);    /* EXCEPTION_POINTERS */
    pe_wr32(b + 20, block + SEH_CONTEXT);
    uint8_t *rec = b + SEH_RECORD;
    pe_wr32(rec, code);
    pe_wr32(rec + 4, flags);
    pe_wr32(rec + 12, r->eip);
    if (nparams > 15)
        nparams = 15;
    pe_wr32(rec + 16, nparams);
    for (uint32_t i = 0; i < nparams; i++)
        pe_wr32(rec + 20 + 4 * i, params[i]);
    uint8_t *c = b + SEH_CONTEXT;
    pe_wr32(c, 0x10007u);                   /* CONTEXT_FULL */
    pe_wr32(c + 0x90, img->fs_selector);
    pe_wr32(c + 0x94, img->data_selector);
    pe_wr32(c + 0x98, img->data_selector);
    pe_wr32(c + 0x9c, r->edi);
    pe_wr32(c + 0xa0, r->esi);
    pe_wr32(c + 0xa4, r->ebx);
    pe_wr32(c + 0xa8, r->edx);
    pe_wr32(c + 0xac, r->ecx);
    pe_wr32(c + 0xb0, r->eax);
    pe_wr32(c + 0xb4, r->ebp);
    pe_wr32(c + 0xb8, r->eip);
    pe_wr32(c + 0xbc, img->code_selector);
    pe_wr32(c + 0xc0, r->eflags & ~0x100u);
    pe_wr32(c + 0xc4, r->esp);
    pe_wr32(c + 0xc8, img->data_selector);
    *eip = disp;
    *esp = block;
    return 1;
}

/* none of its handlers took it: the program's filter
 * (SetUnhandledExceptionFilter's), then the Application Error; 1 its
 * filter's EXCEPTION_CONTINUE_EXECUTION, 0 it ends (exited, or dead) */
static int unhandled(win_pe_ctx *ctx, uint32_t pointers, int vector)
{
    win_pe_image *img = ctx->img;
    const uint8_t *ptrs = (const uint8_t *)win_pe_ptr(img, pointers, 8);
    const uint8_t *rec = ptrs ? (const uint8_t *)win_pe_ptr(img, pe_rd32(ptrs), 20) : NULL;
    uint32_t code = rec ? pe_rd32(rec) : 0, where = rec ? pe_rd32(rec + 12) : 0;
    if (ctx->exception_filter && !ctx->filtering) {
        const uint32_t args[1] = { pointers };
        ctx->filtering = 1;
        int32_t f = (int32_t)win_pe_call(ctx, ctx->exception_filter, args, 1, NULL, 0, NULL);
        ctx->filtering = 0;
        if (ctx->dead || ctx->exited || ctx->stopped)
            return 0;
        if (f == -1)
            return 1;                       /* EXCEPTION_CONTINUE_EXECUTION */
        if (f == 1) {                       /* EXCEPTION_EXECUTE_HANDLER: it ends */
            ctx->exited = 1;
            ctx->exit_code = code;
            return 0;
        }
    }
    ctx->fault.vector = vector >= 0 ? vector : code_vector(code);
    ctx->fault.flat = 1;
    ctx->fault.eip = where;
    int m = win_pe_module_of(img, where);
    if (m >= 0) {
        memcpy(ctx->fault.module, img->mod[m].name, sizeof(ctx->fault.module));
        ctx->fault.eip -= img->mod[m].base;
    }
    ctx->dead = 1;
    return 0;
}

/* the dispatcher's call (its handler's result, -1 the first; the record,
 * the CONTEXT, the frame called, the vector): the next handler, 0 to go on
 * with the CONTEXT */
static int seh_next(pe_args *a, uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    win_pe_image *img = ctx->img;
    int32_t result = (int32_t)pe_arg(a, 0);
    uint32_t rec = pe_arg(a, 1), frame = pe_arg(a, 3);
    int vector = (int)pe_arg(a, 4);
    uint8_t *cursor = (uint8_t *)pe_buf(a, a->esp + 16u, 4);
    const uint8_t *r = (const uint8_t *)pe_buf(a, rec, 8);
    if (!cursor || !r)
        return 0;
    if (result == 0 && !(pe_rd32(r + 4) & 1u)) {   /* ContinueExecution, if it may */
        *eax = 0;
        return 1;
    }
    if (result == -1) {
        const uint8_t *tib = (const uint8_t *)win_pe_ptr(img, img->tib, 4);
        frame = tib ? pe_rd32(tib) : 0xffffffffu;
    } else {
        const uint8_t *f = (const uint8_t *)win_pe_ptr(img, frame, 8);
        frame = f ? pe_rd32(f) : 0xffffffffu;
    }
    const uint8_t *f = frame - img->stack_lo < img->stack_top - img->stack_lo ?
                       (const uint8_t *)win_pe_ptr(img, frame, 8) : NULL;
    if (!f || !pe_rd32(f + 4)) {
        if (unhandled(ctx, a->esp + 24u, vector)) {
            *eax = 0;
            return 1;
        }
        return WINX86_SERVICE_EXIT;
    }
    pe_wr32(cursor, frame);
    *eax = pe_rd32(f + 4);
    return 1;
}

/* RtlUnwind(target frame, target IP, record, return value): the handlers
 * above the target's called to unwind (EXCEPTION_UNWINDING), FS:[0] the
 * target's */
static int rtl_unwind(pe_args *a, uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    win_pe_image *img = ctx->img;
    uint32_t target = pe_arg(a, 0), rec = pe_arg(a, 2), value = pe_arg(a, 3);
    uint8_t blob[80 + 0x2cc];
    memset(blob, 0, sizeof(blob));
    for (;;) {
        uint8_t *tib = (uint8_t *)win_pe_ptr(img, img->tib, 4);
        uint32_t frame = tib ? pe_rd32(tib) : 0xffffffffu;
        if (!tib || frame == 0xffffffffu || frame == target)
            break;
        const uint8_t *f = frame - img->stack_lo < img->stack_top - img->stack_lo ?
                           (const uint8_t *)win_pe_ptr(img, frame, 8) : NULL;
        if (!f)
            break;
        uint32_t handler = pe_rd32(f + 4), next = pe_rd32(f);
        uint32_t at = win_pe_blob(ctx, sizeof(blob));
        uint8_t *r = rec ? (uint8_t *)pe_buf(a, rec, 8) : NULL;
        if (r) {
            pe_wr32(r + 4, pe_rd32(r + 4) | 2u | (target ? 0u : 4u));
        } else {                            /* STATUS_UNWIND, of its own */
            pe_wr32(blob, 0xc0000027u);
            pe_wr32(blob + 4, 2u | (target ? 0u : 4u));
        }
        const uint32_t args[4] = { r ? rec : at, frame, at + 80u, 0 };
        if (handler && at)
            win_pe_call(ctx, handler, args, 4, blob, sizeof(blob), NULL);
        if (ctx->dead || ctx->exited || ctx->stopped)
            return WINX86_SERVICE_EXIT;
        if ((tib = (uint8_t *)win_pe_ptr(img, img->tib, 4)) != NULL)
            pe_wr32(tib, next);
    }
    /* its target IP: there, as if returned to (the thunk's RET drops the
     * arguments) */
    uint8_t *ret = pe_arg(a, 1) ? (uint8_t *)pe_buf(a, a->esp, 4) : NULL;
    if (ret)
        pe_wr32(ret, pe_arg(a, 1));
    *eax = value;
    return 1;
}

/* ---------------------------------------------------------------- Win32s' more */

/* a handle waited for: 1 signaled (an auto-reset event reset, a
 * semaphore's count taken), 0 not yet, -1 none of the program's */
static int signaled(win_pe_ctx *ctx, uint32_t h)
{
    uint32_t i = h - PE_HANDLE(0);
    if (h == CURRENT_PROCESS || h == CURRENT_THREAD)
        return 0;
    if (i >= PE_HANDLES || !ctx->handle[i].kind)
        return -1;
    switch (ctx->handle[i].kind) {
    case PE_H_EVENT:
        if (!ctx->handle[i].value)
            return 0;
        if (!ctx->handle[i].fd)             /* auto-reset */
            ctx->handle[i].value = 0;
        return 1;
    case PE_H_SEMAPHORE:
        if (!ctx->handle[i].value)
            return 0;
        ctx->handle[i].value--;
        return 1;
    case PE_H_PROCESS:                      /* the task it started: gone */
        return !ctx->handle[i].value || !IsTask((HTASK)ctx->handle[i].value);
    default:
        return 1;                           /* one thread: its mutexes are its own */
    }
}

/* WaitForSingleObject, WaitForMultipleObjects: the handles' first one
 * signaled (all of them with all), the other tasks running meanwhile */
static int wait_for(pe_args *a, const uint32_t *h, uint32_t n, int all, uint32_t ms,
                    uint32_t *v)
{
    win_pe_ctx *ctx = a->ctx;
    DWORD start = GetTickCount();
    for (;;) {
        uint32_t done = 0;
        for (uint32_t k = 0; k < n; k++) {
            int s = signaled(ctx, h[k]);
            if (s < 0) {
                ctx->last_error = ERROR_INVALID_HANDLE;
                *v = 0xffffffffu;           /* WAIT_FAILED */
                return 1;
            }
            if (s && !all) {
                *v = k;                     /* WAIT_OBJECT_0 + k */
                return 1;
            }
            done += (uint32_t)s;
        }
        if (all && done == n) {
            *v = 0;
            return 1;
        }
        if (ms != 0xffffffffu && GetTickCount() - start >= ms) {
            *v = 0x102u;                    /* WAIT_TIMEOUT */
            return 1;
        }
        int waits = 0;                      /* only a task's end will come */
        for (uint32_t k = 0; k < n; k++) {
            uint32_t i = h[k] - PE_HANDLE(0);
            waits |= i < PE_HANDLES && ctx->handle[i].kind == PE_H_PROCESS;
        }
        if (!waits && ms == 0xffffffffu) {
            *v = 0;                         /* nothing else can set them */
            return 1;
        }
        if (ctx->yield && ctx->yield()) {
            ctx->stopped = 1;
            return WINX86_SERVICE_EXIT;
        }
        if (ms && win_sys.input && win_sys.input->idle)
            win_sys.input->idle();          /* waiting: the system's idle time */
    }
}

/* [section] of an INI file as "key=value\0...\0\0" */
static uint32_t profile_section(pe_args *a, const char *app, uint32_t buf, uint32_t size,
                                const char *file)
{
    char *keys = (char *)win_alloc(4096), val[256];
    char *out = size ? (char *)pe_buf(a, buf, size) : NULL;
    uint32_t n = 0;
    if (!keys || !out || size < 2) {
        win_free(keys);
        return 0;
    }
    if (file)
        GetPrivateProfileString(app, NULL, "", keys, 4096, file);
    else
        GetProfileString(app, NULL, "", keys, 4096);
    for (const char *k = keys; *k; k += strlen(k) + 1) {
        if (file)
            GetPrivateProfileString(app, k, "", val, sizeof(val), file);
        else
            GetProfileString(app, k, "", val, sizeof(val));
        uint32_t len = (uint32_t)(strlen(k) + 1 + strlen(val));
        if (n + len + 2 > size)
            break;
        pe_fmt(out + n, len + 1, "%s=%s", k, val);
        n += len + 1;
    }
    out[n] = 0;
    if (!n)
        out[1] = 0;
    win_free(keys);
    return n;
}

static uint32_t write_section(pe_args *a, const char *app, uint32_t list, const char *file)
{
    if (file)
        WritePrivateProfileString(app, NULL, NULL, file);
    else
        WriteProfileString(app, NULL, NULL);
    for (uint32_t at = list; list;) {
        const char *s = pe_str(a, at);
        if (!s || !*s)
            break;
        char key[128];
        const char *eq = strchr(s, '=');
        if (eq && (size_t)(eq - s) < sizeof(key)) {
            memcpy(key, s, (size_t)(eq - s));
            key[eq - s] = 0;
            if (file)
                WritePrivateProfileString(app, key, eq + 1, file);
            else
                WriteProfileString(app, key, eq + 1);
        }
        at += (uint32_t)strlen(s) + 1u;
    }
    return 1;
}

/* a structure kept as hex digits and their checksum, as Windows does */
static uint32_t profile_struct(pe_args *a, int write)
{
    const char *sec = pe_str(a, pe_arg(a, 0)), *key = pe_str(a, pe_arg(a, 1));
    const char *file = pe_arg(a, 4) ? pe_str(a, pe_arg(a, 4)) : "WIN.INI";
    uint32_t n = pe_arg(a, 3);
    uint8_t *d = n ? (uint8_t *)pe_buf(a, pe_arg(a, 2), n) : NULL;
    static const char hex[] = "0123456789ABCDEF";
    char text[1028];
    if (!sec || !key || !file || !d || n > 500)
        return 0;
    if (write) {
        uint8_t sum = 0;
        for (uint32_t i = 0; i < n; i++) {
            text[2 * i] = hex[d[i] >> 4];
            text[2 * i + 1] = hex[d[i] & 15];
            sum = (uint8_t)(sum + d[i]);
        }
        text[2 * n] = hex[sum >> 4];
        text[2 * n + 1] = hex[sum & 15];
        text[2 * n + 2] = 0;
        return WritePrivateProfileString(sec, key, text, file);
    }
    if (GetPrivateProfileString(sec, key, "", text, sizeof(text), file) != (int)(2 * n + 2))
        return 0;
    uint8_t sum = 0, b[512];
    for (uint32_t i = 0; i <= n; i++) {
        int hi = text[2 * i], lo = text[2 * i + 1];
        hi = hi <= '9' ? hi - '0' : (hi | 0x20) - 'a' + 10;
        lo = lo <= '9' ? lo - '0' : (lo | 0x20) - 'a' + 10;
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15)
            return 0;
        b[i] = (uint8_t)(hi << 4 | lo);
        if (i < n)
            sum = (uint8_t)(sum + b[i]);
    }
    if (sum != b[n])
        return 0;
    memcpy(d, b, n);
    return 1;
}

static __attribute__((noinline)) int kernel32_win32s(pe_args *a, uint16_t service, uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    uint32_t v = 0;
    switch (service) {
    case PEF_OpenFile: {
        const char *s = pe_str(a, pe_arg(a, 0));
        OFSTRUCT *of = (OFSTRUCT *)pe_buf(a, pe_arg(a, 1), sizeof(OFSTRUCT));
        v = s && of ? (uint32_t)(int32_t)OpenFile(s, of, pe_arg(a, 2) & 0xffffu) : 0xffffffffu;
        break;
    }
    case PEF_DisableThreadLibraryCalls: v = 1; break;
    case PEF_IsDebuggerPresent: v = 0; break;
    case PEF_GetPrivateProfileSectionA:
    case PEF_GetProfileSectionA: {
        int priv = service == PEF_GetPrivateProfileSectionA;
        const char *app = pe_str(a, pe_arg(a, 0));
        const char *file = priv ? pe_str(a, pe_arg(a, 3)) : NULL;
        v = app && (!priv || file) ? profile_section(a, app, pe_arg(a, 1), pe_arg(a, 2), file) : 0;
        break;
    }
    case PEF_WritePrivateProfileSectionA:
    case PEF_WriteProfileSectionA: {
        int priv = service == PEF_WritePrivateProfileSectionA;
        const char *app = pe_str(a, pe_arg(a, 0));
        const char *file = priv ? pe_str(a, pe_arg(a, 2)) : NULL;
        v = app && (!priv || file) ? write_section(a, app, pe_arg(a, 1), file) : 0;
        break;
    }
    case PEF_GetPrivateProfileStructA: v = profile_struct(a, 0); break;
    case PEF_WritePrivateProfileStructA: v = profile_struct(a, 1); break;
    case PEF_ExpandEnvironmentStringsA: {
        const char *s = pe_str(a, pe_arg(a, 0));
        char out[1024];
        uint32_t n = 0;
        while (s && *s && n + 1 < sizeof(out)) {
            const char *end = *s == '%' ? strchr(s + 1, '%') : NULL;
            char name[64];
            const char *val = NULL;
            if (end && (size_t)(end - s - 1) < sizeof(name)) {
                memcpy(name, s + 1, (size_t)(end - s - 1));
                name[end - s - 1] = 0;
                val = name[0] ? env_find(ctx, name) : NULL;
            }
            if (val) {
                while (*val && n + 1 < sizeof(out))
                    out[n++] = *val++;
                s = end + 1;
            } else {
                out[n++] = *s++;
            }
        }
        out[n] = 0;
        if (!s)
            break;
        v = n + 1u;
        if (pe_arg(a, 2) >= v)
            put_str(a, pe_arg(a, 1), pe_arg(a, 2), out);
        break;
    }
    case PEF_GetComputerNameA: {
        uint8_t *size = (uint8_t *)pe_buf(a, pe_arg(a, 1), 4);
        char name[16];
        GetProfileString("Network", "ComputerName", "WINN", name, sizeof(name));
        uint32_t need = (uint32_t)strlen(name) + 1u;
        if (!size)
            break;
        if (pe_rd32(size) < need) {
            pe_wr32(size, need);
            ctx->last_error = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        put_str(a, pe_arg(a, 0), need, name);
        pe_wr32(size, need - 1u);
        v = 1;
        break;
    }
    case PEF_GlobalCompact:
    case PEF_LocalCompact: {
        win_ne_mem_info mi;
        win_ne_guest_mem_info(&mi);
        v = mi.ext_size > mi.ext_used ? mi.ext_size - mi.ext_used : 0;
        break;
    }
    case PEF_LocalShrink: v = 0x10000u; break;
    case PEF_GlobalFix:
    case PEF_GlobalUnfix:
    case PEF_GlobalUnWire:
        v = 1;
        break;
    case PEF_GlobalWire: v = pe_arg(a, 0); break;
    case PEF_InterlockedCompareExchange: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 0), 4);
        if (!p)
            break;
        v = pe_rd32(p);
        if (v == pe_arg(a, 2))
            pe_wr32(p, pe_arg(a, 1));
        break;
    }
    case PEF_InterlockedExchangeAdd: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 0), 4);
        if (!p)
            break;
        v = pe_rd32(p);
        pe_wr32(p, v + pe_arg(a, 1));
        break;
    }
    case PEF_LockFile:
    case PEF_UnlockFile:
        v = 1;
        break;
    case PEF_GetLogicalDriveStringsA: {
        char list[4 * 26 + 1];
        uint32_t n = 0;
        for (int d = 0; d < 26; d++)
            if (GetDriveType(d)) {
                list[n++] = (char)('A' + d);
                list[n++] = ':';
                list[n++] = '\\';
                list[n++] = 0;
            }
        list[n] = 0;
        v = n;
        if (pe_arg(a, 0) > n) {
            char *p = (char *)pe_buf(a, pe_arg(a, 1), n + 1u);
            if (p)
                memcpy(p, list, n + 1u);
        } else {
            v = n + 1u;
        }
        break;
    }
    case PEF_LoadModule: {
        /* LOADPARMS32: environment, the command tail (a counted string), the
         * show command (two words), reserved */
        const char *name = pe_str(a, pe_arg(a, 0));
        const uint8_t *p = pe_arg(a, 1) ? (const uint8_t *)pe_buf(a, pe_arg(a, 1), 16) : NULL;
        char cmd[300], tail[128];
        UINT show = SW_SHOWNORMAL;
        tail[0] = 0;
        if (!name)
            break;
        if (p) {
            const uint8_t *t = pe_rd32(p + 4) ? (const uint8_t *)pe_buf(a, pe_rd32(p + 4), 1) : NULL;
            if (t && pe_buf(a, pe_rd32(p + 4), 1u + t[0])) {
                uint32_t n = t[0] < sizeof(tail) ? t[0] : sizeof(tail) - 1u;
                memcpy(tail, t + 1, n);
                tail[n] = 0;
            }
            const uint8_t *s = pe_rd32(p + 8) ? (const uint8_t *)pe_buf(a, pe_rd32(p + 8), 4) : NULL;
            if (s)
                show = pe_rd16(s + 2);
        }
        pe_fmt(cmd, sizeof(cmd), "%s %s", name, tail);
        v = WinExec(cmd, show);
        break;
    }
    case PEF_GetThreadPriority: v = 0; break;
    case PEF_SetThreadPriority:
    case PEF_SetPriorityClass:
        v = 1;
        break;
    case PEF_GetPriorityClass: v = 0x20; break;     /* NORMAL_PRIORITY_CLASS */
    case PEF_ResumeThread:
    case PEF_SuspendThread:
        v = 0xffffffffu;
        ctx->last_error = ERROR_INVALID_HANDLE;
        break;
    case PEF_GetProcessVersion: v = 0x00030000u; break;
    case PEF_IsValidLocale: v = 1; break;
    case PEF_GetExitCodeThread: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 1), 4);
        if (p)
            pe_wr32(p, 259);                /* STILL_ACTIVE: its only thread */
        v = p != NULL;
        break;
    }
    case PEF_MoveFileExA: {
        const char *s = pe_str(a, pe_arg(a, 0));
        const char *d = pe_arg(a, 1) ? pe_str(a, pe_arg(a, 1)) : NULL;
        if (!s || a->bad)
            break;
        if (!d) {                           /* to delete it */
            v = DeleteFileN(s);
            break;
        }
        if ((pe_arg(a, 2) & 1u) && win_file_attr(d, 0, 0) >= 0)    /* REPLACE_EXISTING */
            DeleteFileN(d);
        v = MoveFileN(s, d);
        if (!v && (pe_arg(a, 2) & 2u)) {    /* COPY_ALLOWED */
            HFILE in = _lopen(s, OF_READ), out = in == HFILE_ERROR ? HFILE_ERROR : _lcreat(d, 0);
            char buf[512];
            UINT n;
            v = out != HFILE_ERROR;
            while (v && (n = _lread(in, buf, sizeof(buf))) > 0 && n != (UINT)-1)
                v = _lwrite(out, buf, n) == n;
            if (in != HFILE_ERROR)
                _lclose(in);
            if (out != HFILE_ERROR)
                _lclose(out);
            if (v)
                DeleteFileN(s);
        }
        break;
    }
    case PEF_CreateSemaphoreA: {
        int i = new_handle(ctx, PE_H_SEMAPHORE, (int32_t)pe_arg(a, 2), pe_arg(a, 1));
        v = i >= 0 ? PE_HANDLE(i) : 0;
        break;
    }
    case PEF_ReleaseSemaphore: {
        int i = handle_index(ctx, pe_arg(a, 0), PE_H_SEMAPHORE);
        if (i < 0)
            break;
        uint8_t *prev = pe_arg(a, 2) ? (uint8_t *)pe_buf(a, pe_arg(a, 2), 4) : NULL;
        if (prev)
            pe_wr32(prev, ctx->handle[i].value);
        if (ctx->handle[i].value + pe_arg(a, 1) > (uint32_t)ctx->handle[i].fd)
            break;                          /* past its maximum */
        ctx->handle[i].value += pe_arg(a, 1);
        v = 1;
        break;
    }
    case PEF_PulseEvent: v = handle_index(ctx, pe_arg(a, 0), PE_H_EVENT) >= 0; break;
    case PEF_WaitForMultipleObjects: {
        uint32_t n = pe_arg(a, 0), h[64];
        const uint8_t *p = n && n <= 64 ? (const uint8_t *)pe_buf(a, pe_arg(a, 1), 4u * n) : NULL;
        if (!p) {
            ctx->last_error = ERROR_INVALID_PARAMETER;
            v = 0xffffffffu;
            break;
        }
        for (uint32_t k = 0; k < n; k++)
            h[k] = pe_rd32(p + 4 * k);
        int r = wait_for(a, h, n, pe_arg(a, 2) != 0, pe_arg(a, 3), &v);
        if (r != 1)
            return r;
        break;
    }
    case PEF_CreateProcessA: {
        /* the program started as WinExec does; PROCESS_INFORMATION: its
         * task as the process (its end signals it) */
        const char *app = pe_arg(a, 0) ? pe_str(a, pe_arg(a, 0)) : NULL;
        const char *line = pe_arg(a, 1) ? pe_str(a, pe_arg(a, 1)) : NULL;
        const uint8_t *si = pe_arg(a, 8) ? (const uint8_t *)pe_buf(a, pe_arg(a, 8), 68) : NULL;
        uint8_t *pi = (uint8_t *)pe_buf(a, pe_arg(a, 9), 16);
        char cmd[300];
        if (a->bad || !pi || (!app && !line))
            break;
        if (app && line) {                  /* the line's first word is the program's name */
            const char *rest = line;
            int quoted = *rest == '"';
            rest += quoted;
            while (*rest && (quoted ? *rest != '"' : *rest != ' '))
                rest++;
            rest += *rest == '"';
            pe_fmt(cmd, sizeof(cmd), "%s%s", app, rest);
        } else {
            pe_fmt(cmd, sizeof(cmd), "%s", app ? app : line);
        }
        UINT show = si && (pe_rd32(si + 44) & 1u) ? pe_rd16(si + 48) : SW_SHOWNORMAL;
        UINT r = WinExec(cmd, show);
        if (r < 32) {
            ctx->last_error = r == 2 ? ERROR_FILE_NOT_FOUND : r == 3 ? ERROR_PATH_NOT_FOUND :
                              r == 8 ? ERROR_NOT_ENOUGH_MEMORY : 193;  /* ERROR_BAD_EXE_FORMAT */
            break;
        }
        int ip = new_handle(ctx, PE_H_PROCESS, 0, IsTask((HTASK)r) ? r : 0);
        int it = new_handle(ctx, PE_H_PROCESS, 0, IsTask((HTASK)r) ? r : 0);
        pe_wr32(pi, ip >= 0 ? PE_HANDLE(ip) : 0);
        pe_wr32(pi + 4, it >= 0 ? PE_HANDLE(it) : 0);
        pe_wr32(pi + 8, r);
        pe_wr32(pi + 12, r);
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
    case PEF_UnhandledExceptionFilter:
        /* the program's filter, or the Application Error: it ends */
        if (!ctx->filtering) {
            if (unhandled(ctx, pe_arg(a, 0), -1)) {
                v = 0xffffffffu;            /* EXCEPTION_CONTINUE_EXECUTION */
                break;
            }
            return WINX86_SERVICE_EXIT;
        }
        v = 0;                              /* its own filter's call: CONTINUE_SEARCH */
        break;
    case PEF_RtlUnwind: return rtl_unwind(a, eax);
    case PEF_Sleep: {
        uint32_t ms = pe_arg(a, 0);
        DWORD start = GetTickCount();
        do {
            if (ctx->yield && ctx->yield()) {
                ctx->stopped = 1;
                return WINX86_SERVICE_EXIT;
            }
            if (ms && win_sys.input && win_sys.input->idle)
                win_sys.input->idle();      /* a thread asleep: the system's idle time */
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
        uint32_t h = pe_arg(a, 0);
        int r = wait_for(a, &h, 1, 0, pe_arg(a, 1), &v);
        if (r != 1)
            return r;
        break;
    }
    case PEF_GetExitCodeProcess: {
        uint8_t *p = (uint8_t *)pe_buf(a, pe_arg(a, 1), 4);
        int i = handle_index(ctx, pe_arg(a, 0), PE_H_PROCESS);
        if (p)                              /* STILL_ACTIVE; a task's end: 0 */
            pe_wr32(p, i >= 0 && signaled(ctx, pe_arg(a, 0)) > 0 ? 0 : 259);
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

/* ---------------------------------------------------------------- WINMM */

/* WINMM's functions MMSYSTEM.DLL has: its name, the native one's, the
 * generic codes */
static const struct { const char *name, *native, *sig; } mm_fns[] = {
    { "timeGetTime", "timeGetTime", "d:" },
    { "timeBeginPeriod", "timeBeginPeriod", "d:d" },
    { "timeEndPeriod", "timeEndPeriod", "d:d" },
    { "timeGetDevCaps", "timeGetDevCaps", "d:Bd" },
    { "timeGetSystemTime", "timeGetSystemTime", "d:Bd" },
    { "sndPlaySoundA", "sndPlaySound", "d:sd" },
    { "PlaySoundA", "PlaySound", "d:sdd" },
    { "PlaySound", "PlaySound", "d:sdd" },
    { "mciSendStringA", "mciSendString", "d:sSdw" },
    { "mciGetErrorStringA", "mciGetErrorString", "d:dSd" },
    { "mmsystemGetVersion", "mmsystemGetVersion", "d:" },
    { "waveOutGetNumDevs", "waveOutGetNumDevs", "d:" },
    { "waveInGetNumDevs", "waveInGetNumDevs", "d:" },
    { "midiOutGetNumDevs", "midiOutGetNumDevs", "d:" },
    { "midiInGetNumDevs", "midiInGetNumDevs", "d:" },
    { "auxGetNumDevs", "auxGetNumDevs", "d:" },
    { "joyGetNumDevs", "joyGetNumDevs", "d:" },
    { "waveOutGetErrorTextA", "waveOutGetErrorText", "d:dSd" },
    { "midiOutGetErrorTextA", "midiOutGetErrorText", "d:dSd" },
    { "waveInGetErrorTextA", "waveInGetErrorText", "d:dSd" },
    { "midiInGetErrorTextA", "midiInGetErrorText", "d:dSd" },
    { "timeKillEvent", "timeKillEvent", "d:d" },
    { "mmioOpenA", "mmioOpen", "d:sPd" },
    { "mmioClose", "mmioClose", "d:dd" },
    { "mmioRead", "mmioRead", "d:dSd" },
    { "mmioWrite", "mmioWrite", "d:dSd" },
    { "mmioSeek", "mmioSeek", "d:ddd" },
    { "mmioFlush", "mmioFlush", "d:dd" },
    { "mmioDescend", "mmioDescend", "d:dPPd" },
    { "mmioAscend", "mmioAscend", "d:dPd" },
    { "mmioCreateChunk", "mmioCreateChunk", "d:dPd" },
    { "mmioStringToFOURCCA", "mmioStringToFOURCC", "d:sd" },
};
#define MM_FNS (sizeof(mm_fns) / sizeof(mm_fns[0]))

/* WINMM's functions MMSYSTEM.DLL has for Win16 callers only (its ne_call):
 * the program's arguments made a Win16 caller's (PASCAL, its far pointers
 * flat addresses), as Win32's structures have Win16's layouts (WAVEHDR,
 * MIDIHDR, WAVEFORMAT, MCI's but those with a RECT); the capabilities
 * (UINT versions) and MMTIME converted. Their types: w a WORD, d a DWORD,
 * p a pointer, h a handle's (its DWORD zeroed first: Win16's is a word). */
enum { MM_PLAIN, MM_WAVECAPS, MM_MIDICAPS, MM_AUXCAPS, MM_TIME, MM_MCI };

static const struct { const char *name; uint16_t ordinal; uint8_t kind; const char *types; }
mm16_fns[] = {
    { "waveOutGetDevCapsA", 402, MM_WAVECAPS, "wpw" }, { "waveOutOpen", 404, MM_PLAIN, "hwpddd" },
    { "waveOutClose", 405, MM_PLAIN, "w" }, { "waveOutPrepareHeader", 406, MM_PLAIN, "wpw" },
    { "waveOutUnprepareHeader", 407, MM_PLAIN, "wpw" }, { "waveOutWrite", 408, MM_PLAIN, "wpw" },
    { "waveOutPause", 409, MM_PLAIN, "w" }, { "waveOutRestart", 410, MM_PLAIN, "w" },
    { "waveOutReset", 411, MM_PLAIN, "w" }, { "waveOutGetPosition", 412, MM_TIME, "wpw" },
    { "waveOutGetPitch", 413, MM_PLAIN, "wh" }, { "waveOutSetPitch", 414, MM_PLAIN, "wd" },
    { "waveOutGetVolume", 415, MM_PLAIN, "wp" }, { "waveOutSetVolume", 416, MM_PLAIN, "wd" },
    { "waveOutGetPlaybackRate", 417, MM_PLAIN, "wh" },
    { "waveOutSetPlaybackRate", 418, MM_PLAIN, "wd" }, { "waveOutBreakLoop", 419, MM_PLAIN, "w" },
    { "waveOutGetID", 420, MM_PLAIN, "wh" }, { "waveOutMessage", 421, MM_PLAIN, "wwdd" },
    { "midiOutGetDevCapsA", 202, MM_MIDICAPS, "wpw" }, { "midiOutOpen", 204, MM_PLAIN, "hwddd" },
    { "midiOutClose", 205, MM_PLAIN, "w" }, { "midiOutPrepareHeader", 206, MM_PLAIN, "wpw" },
    { "midiOutUnprepareHeader", 207, MM_PLAIN, "wpw" }, { "midiOutShortMsg", 208, MM_PLAIN, "wd" },
    { "midiOutLongMsg", 209, MM_PLAIN, "wpw" }, { "midiOutReset", 210, MM_PLAIN, "w" },
    { "midiOutGetVolume", 211, MM_PLAIN, "wp" }, { "midiOutSetVolume", 212, MM_PLAIN, "wd" },
    { "midiOutCachePatches", 213, MM_PLAIN, "wwpw" },
    { "midiOutCacheDrumPatches", 214, MM_PLAIN, "wwpw" }, { "midiOutGetID", 215, MM_PLAIN, "wh" },
    { "midiOutMessage", 216, MM_PLAIN, "wwdd" },
    { "waveInGetDevCapsA", 502, MM_WAVECAPS, "wpw" }, { "midiInGetDevCapsA", 302, MM_MIDICAPS, "wpw" },
    { "auxGetDevCapsA", 351, MM_AUXCAPS, "wpw" }, { "auxGetVolume", 352, MM_PLAIN, "wp" },
    { "auxSetVolume", 353, MM_PLAIN, "wd" }, { "auxOutMessage", 354, MM_PLAIN, "wwdd" },
    { "joyGetDevCapsA", 102, MM_PLAIN, "wpw" }, { "joyGetPos", 103, MM_PLAIN, "wp" },
    { "joyGetThreshold", 104, MM_PLAIN, "wp" }, { "joyReleaseCapture", 105, MM_PLAIN, "w" },
    { "joySetCapture", 106, MM_PLAIN, "wwww" }, { "joySetThreshold", 107, MM_PLAIN, "ww" },
    { "joyGetPosEx", 110, MM_PLAIN, "wp" },
    { "timeSetEvent", 602, MM_PLAIN, "wwpdw" },
    { "mciSendCommandA", 701, MM_MCI, "wwdd" }, { "mciGetDeviceIDA", 703, MM_PLAIN, "p" },
    { "mciGetCreatorTask", 717, MM_PLAIN, "w" },
};
#define MM16_FNS (sizeof(mm16_fns) / sizeof(mm16_fns[0]))

/* the program's memory for the DLL (ne_guest_sys.c's win_dll_call_flat) */
int win_pe_flat_mem(void *pe, uint32_t addr, void *buf, uint32_t n, int write)
{
    win_pe_ctx *ctx = (win_pe_ctx *)pe;
    void *p = ctx && n ? win_pe_ptr(ctx->img, addr, n) : NULL;
    if (!p)
        return 0;
    if (write)
        memcpy(p, buf, n);
    else
        memcpy(buf, p, n);
    return 1;
}

uint32_t win_pe_task_callback(void *pe, uint32_t proc, const uint16_t *words, int n)
{
    uint32_t args[8];
    int k = 0;
    if (n == 8) {                           /* a driver callback's: WORD, WORD, 3 DWORDs */
        args[k++] = words[0];
        args[k++] = words[1];
        for (int i = 2; i < 8; i += 2)
            args[k++] = (uint32_t)words[i] << 16 | words[i + 1];
    } else {
        for (int i = 0; i < n && k < 8; i++)
            args[k++] = words[i];
    }
    return win_pe_call((win_pe_ctx *)pe, proc, args, k, NULL, 0, NULL);
}

/* the call made a Win16 caller's (the arguments args, their types) */
static uint32_t mm16_call(pe_args *a, unsigned i, const uint32_t *args, int *ok)
{
    const char *t = mm16_fns[i].types;
    uint8_t frame[40];
    unsigned total = 4, n = (unsigned)strlen(t);      /* the far return address's first */
    for (unsigned k = 0; k < n; k++)
        total += t[k] == 'w' ? 2u : 4u;
    unsigned off = total;
    memset(frame, 0, 4);
    for (unsigned k = 0; k < n; k++) {
        unsigned size = t[k] == 'w' ? 2u : 4u;
        off -= size;
        uint8_t *handle = t[k] == 'h' && args[k] ? (uint8_t *)pe_buf(a, args[k], 4) : NULL;
        if (handle)
            pe_wr32(handle, 0);
        if (size == 2)
            pe_wr16(frame + off, (uint16_t)args[k]);
        else
            pe_wr32(frame + off, args[k]);
    }
    int slot = win_dll_use("MMSYSTEM");
    uint32_t v = 0;
    int r = slot >= 0 ? win_dll_call_flat(slot, mm16_fns[i].ordinal, frame, total, a->ctx, &v) :
            WINDLL_NONE;
    *ok = r != WINDLL_NONE;
    return r == WINDLL_WORD ? v & 0xffffu : v;
}

/* Win16's capabilities (wMid, wPid, a WORD version, the name, ...) made
 * Win32's (a UINT version, the rest after it: a WORD then a reserved one
 * before the last DWORD) */
static uint32_t mm16_caps(pe_args *a, unsigned i, int *ok)
{
    static const uint8_t win16[] = { 0, 48, 50, 44 }, win32[] = { 0, 52, 52, 48 };
    unsigned kind = mm16_fns[i].kind;
    uint32_t scratch = win_pe_blob(a->ctx, 64), cap = pe_arg(a, 2);
    uint8_t *out = cap ? (uint8_t *)pe_buf(a, pe_arg(a, 1), cap) : NULL;
    if (!scratch || !out) {
        *ok = 1;
        return 11;                          /* MMSYSERR_INVALPARAM */
    }
    const uint32_t args[3] = { pe_arg(a, 0), scratch, win16[kind] };
    uint32_t r = mm16_call(a, i, args, ok);
    const uint8_t *s = (const uint8_t *)win_pe_ptr(a->ctx->img, scratch, 64);
    if (r || !s)
        return r;
    uint8_t d[52];
    memset(d, 0, sizeof(d));
    memcpy(d, s, 4);                        /* wMid, wPid */
    pe_wr32(d + 4, pe_rd16(s + 4));         /* vDriverVersion */
    memcpy(d + 8, s + 6, 32);               /* szPname */
    if (kind == MM_WAVECAPS) {              /* dwFormats, wChannels, dwSupport */
        memcpy(d + 40, s + 38, 6);
        memcpy(d + 48, s + 44, 4);
    } else if (kind == MM_MIDICAPS) {       /* four WORDs, dwSupport */
        memcpy(d + 40, s + 38, 12);
    } else {                                /* wTechnology, dwSupport */
        memcpy(d + 40, s + 38, 2);
        memcpy(d + 44, s + 40, 4);
    }
    memcpy(out, d, cap < win32[kind] ? cap : win32[kind]);
    return 0;
}

/* MMTIME: Win16's a WORD wType, Win32's a UINT */
static uint32_t mm16_time(pe_args *a, unsigned i, int *ok)
{
    uint32_t scratch = win_pe_blob(a->ctx, 16);
    uint8_t *t = pe_arg(a, 2) >= 12 ? (uint8_t *)pe_buf(a, pe_arg(a, 1), 12) : NULL;
    uint8_t *s = scratch ? (uint8_t *)win_pe_ptr(a->ctx->img, scratch, 10) : NULL;
    if (!t || !s) {
        *ok = 1;
        return 11;
    }
    pe_wr16(s, (uint16_t)pe_rd32(t));
    const uint32_t args[3] = { pe_arg(a, 0), scratch, 10 };
    uint32_t r = mm16_call(a, i, args, ok);
    if (!r && (s = (uint8_t *)win_pe_ptr(a->ctx->img, scratch, 10)) != NULL) {
        pe_wr32(t, pe_rd16(s));
        memcpy(t + 4, s + 2, 8);
    }
    return r;
}

/* mciSendCommand: MCI_PUT's and MCI_WHERE's RECT (Win16's four shorts) */
static uint32_t mm16_mci(pe_args *a, unsigned i, int *ok)
{
    uint32_t msg = pe_arg(a, 1) & 0xffffu, parms = pe_arg(a, 3);
    uint32_t args[4] = { pe_arg(a, 0), pe_arg(a, 1), pe_arg(a, 2), parms };
    if ((msg != 0x0842u && msg != 0x0843u) || !parms)       /* MCI_PUT, MCI_WHERE */
        return mm16_call(a, i, args, ok);
    uint8_t *p = (uint8_t *)pe_buf(a, parms, 20);
    uint32_t scratch = win_pe_blob(a->ctx, 16);
    uint8_t *s = scratch ? (uint8_t *)win_pe_ptr(a->ctx->img, scratch, 12) : NULL;
    if (!p || !s) {
        *ok = 1;
        return 11;
    }
    pe_wr32(s, pe_rd32(p));
    for (int k = 0; k < 4; k++)
        pe_wr16(s + 4 + 2 * k, (uint16_t)pe_rd32(p + 4 + 4 * k));
    args[3] = scratch;
    uint32_t r = mm16_call(a, i, args, ok);
    if (!r && msg == 0x0843u && (s = (uint8_t *)win_pe_ptr(a->ctx->img, scratch, 12)) != NULL &&
        (p = (uint8_t *)pe_buf(a, parms, 20)) != NULL)
        for (int k = 0; k < 4; k++)
            pe_wr32(p + 4 + 4 * k, (uint32_t)(int32_t)(int16_t)pe_rd16(s + 4 + 2 * k));
    return r;
}

const char *win_pe_mm_name(uint16_t service)
{
    unsigned i = service - WIN_PE_SERVICE_MM16;
    if (i < MM16_FNS)
        return mm16_fns[i].name;
    i = service - WIN_PE_SERVICE_MM;
    return i < sizeof(mm_fns) / sizeof(mm_fns[0]) ? mm_fns[i].name : NULL;
}

static int winmm16(pe_args *a, unsigned i, uint32_t *eax)
{
    int ok = 0;
    switch (mm16_fns[i].kind) {
    case MM_WAVECAPS: case MM_MIDICAPS: case MM_AUXCAPS: *eax = mm16_caps(a, i, &ok); break;
    case MM_TIME: *eax = mm16_time(a, i, &ok); break;
    case MM_MCI: *eax = mm16_mci(a, i, &ok); break;
    default: {
        uint32_t args[8];
        unsigned n = (unsigned)strlen(mm16_fns[i].types);
        for (unsigned k = 0; k < n; k++)
            args[k] = pe_arg(a, (int)k);
        *eax = mm16_call(a, i, args, &ok);
    }
    }
    return ok;
}

uint32_t win_pe_mm_proc(win_pe_image *img, const char *name)
{
    for (unsigned i = 0; i < MM_FNS; i++)
        if (!strcmp(mm_fns[i].name, name))
            return win_pe_thunk(img, (uint16_t)(WIN_PE_SERVICE_MM + i),
                                4 * (int)strlen(strchr(mm_fns[i].sig, ':') + 1));
    for (unsigned i = 0; i < MM16_FNS; i++)
        if (!strcmp(mm16_fns[i].name, name))
            return win_pe_thunk(img, (uint16_t)(WIN_PE_SERVICE_MM16 + i),
                                4 * (int)strlen(mm16_fns[i].types));
    return 0;
}

static int winmm(pe_args *a, unsigned i, uint32_t *eax)
{
    int slot = win_dll_use("MMSYSTEM");     /* the task's until it ends */
    const win_dll_export *e = slot >= 0 ? win_dll_export_of(slot, 0, mm_fns[i].native) : NULL;
    if (!e || !e->native)
        return 0;
    const win_pe_fn f = { PE_WINMM, mm_fns[i].name, mm_fns[i].sig, e->native, 0 };
    return generic(a, &f, eax);
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
    if ((service >= WIN_PE_SERVICE_NATIVE && service < WIN_PE_SERVICE_NATIVE + WIN_PE_NATIVE_PROCS) ||
        (service >= WIN_PE_SERVICE_WING && service < WIN_PE_SERVICE_WING + 0x100u)) {
        r = pe_user_api(&a, service, eax);
        if (a.bad)
            ctx->invalid_args = 1;
        return r;
    }
    if (service == WIN_PE_SERVICE_SEH) {
        r = seh_next(&a, eax);
        if (a.bad)
            ctx->invalid_args = 1;
        return r;
    }
    if (service >= WIN_PE_SERVICE_DLLS && service < WIN_PE_SERVICE_DLLS + 0x100u) {
        r = win_pe_dll_api(&a, service, eax);
        if (a.bad)
            ctx->invalid_args = 1;
        if (r <= 0) {
            ctx->unsupported = service;
            win_pe_service_name(service, ctx->undefined, sizeof(ctx->undefined));
        }
        return r;
    }
    if (service >= WIN_PE_SERVICE_MM16 && service < WIN_PE_SERVICE_MM16 + MM16_FNS) {
        r = winmm16(&a, service - WIN_PE_SERVICE_MM16, eax);
        if (a.bad)
            ctx->invalid_args = 1;
        if (r <= 0) {
            ctx->unsupported = service;
            pe_fmt(ctx->undefined, sizeof(ctx->undefined), "WINMM.%s",
                   mm16_fns[service - WIN_PE_SERVICE_MM16].name);
        }
        return r;
    }
    if (service >= WIN_PE_SERVICE_MM && service < WIN_PE_SERVICE_MM + MM_FNS) {
        r = winmm(&a, service - WIN_PE_SERVICE_MM, eax);
        if (a.bad)
            ctx->invalid_args = 1;
        if (r <= 0) {
            ctx->unsupported = service;
            pe_fmt(ctx->undefined, sizeof(ctx->undefined), "WINMM.%s",
                   mm_fns[service - WIN_PE_SERVICE_MM].name);
        }
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
