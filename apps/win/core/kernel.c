/*
 * kernel.c - handle table, memory, strings, atoms, time (KERNEL subset).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "wincore.h"

wsys win_sys;

/* ---------------------------------------------------------------- memory */

void *win_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p)
        memset(p, 0, n ? n : 1);
    return p;
}

void *win_realloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void  win_free(void *p) { free(p); }

char *win_strdup(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *d = (char *)win_alloc(n + 1);
    if (d && n)
        memcpy(d, s, n);
    return d;
}

/* ---------------------------------------------------------------- handles */

/*
 * Handles are 16-bit: index into one table shared by all object kinds.
 * Index 0 is never used (NULL handle). Values are multiplied by 4 and
 * offset so they look like Win16 handles (small even numbers, never 0..3).
 */
#define WH_MAX   4096
#define WH_TO_INDEX(h) (((unsigned)(h) - 0x10u) >> 2)
#define WH_FROM_INDEX(i) ((HANDLE)(((unsigned)(i) << 2) + 0x10u))

typedef struct {
    uint8_t type;
    uint8_t stock;
    void   *obj;
} whandle;

static whandle *wh_tab;
static int      wh_cap;
static int      wh_hint = 1;

HANDLE win_handle_alloc(int type, void *obj)
{
    if (!wh_tab) {
        wh_cap = 256;
        wh_tab = (whandle *)win_alloc(sizeof(whandle) * wh_cap);
        if (!wh_tab)
            return 0;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = (pass ? 1 : wh_hint); i < wh_cap; i++) {
            if (wh_tab[i].type == WOBJ_FREE) {
                wh_tab[i].type = (uint8_t)type;
                wh_tab[i].stock = 0;
                wh_tab[i].obj = obj;
                wh_hint = i + 1;
                return WH_FROM_INDEX(i);
            }
        }
    }
    if (wh_cap >= WH_MAX)
        return 0;
    int ncap = wh_cap * 2 > WH_MAX ? WH_MAX : wh_cap * 2;
    whandle *n = (whandle *)win_realloc(wh_tab, sizeof(whandle) * ncap);
    if (!n)
        return 0;
    memset(n + wh_cap, 0, sizeof(whandle) * (ncap - wh_cap));
    int i = wh_cap;
    wh_tab = n;
    wh_cap = ncap;
    wh_tab[i].type = (uint8_t)type;
    wh_tab[i].obj = obj;
    wh_hint = i + 1;
    return WH_FROM_INDEX(i);
}

static whandle *wh_entry(HANDLE h)
{
    if (!wh_tab || h < 0x14 || ((h - 0x10u) & 3u))
        return NULL;
    unsigned i = WH_TO_INDEX(h);
    if (i == 0 || i >= (unsigned)wh_cap)
        return NULL;
    return &wh_tab[i];
}

void *win_handle_get(HANDLE h, int type)
{
    whandle *e = wh_entry(h);
    return (e && e->type == type) ? e->obj : NULL;
}

int win_handle_type(HANDLE h)
{
    whandle *e = wh_entry(h);
    return e ? e->type : WOBJ_FREE;
}

void win_handle_free(HANDLE h)
{
    whandle *e = wh_entry(h);
    if (!e)
        return;
    e->type = WOBJ_FREE;
    e->obj = NULL;
    unsigned i = WH_TO_INDEX(h);
    if ((int)i < wh_hint)
        wh_hint = (int)i;
}

void win_handle_set_stock(HANDLE h)
{
    whandle *e = wh_entry(h);
    if (e)
        e->stock = 1;
}

int win_handle_is_stock(HANDLE h)
{
    whandle *e = wh_entry(h);
    return e ? e->stock : 0;
}

/* ---------------------------------------------------------------- Global / Local */

typedef struct wmem {
    void  *p;
    DWORD  size;
    UINT   flags;
    int    lock;
} wmem;

static HANDLE mem_alloc(int type, UINT flags, DWORD bytes)
{
    wmem *m = (wmem *)win_alloc(sizeof(*m));
    if (!m)
        return 0;
    m->p = win_alloc(bytes);      /* always zeroed: GMEM_ZEROINIT for free */
    if (!m->p) {
        win_free(m);
        return 0;
    }
    m->size = bytes;
    m->flags = flags;
    HANDLE h = win_handle_alloc(type, m);
    if (!h) {
        win_free(m->p);
        win_free(m);
    }
    return h;
}

static HANDLE mem_realloc(int type, HANDLE h, DWORD bytes, UINT flags)
{
    wmem *m = (wmem *)win_handle_get(h, type);
    if (!m)
        return 0;
    if (flags & GMEM_MODIFY) {
        m->flags = (m->flags & ~GMEM_MOVEABLE) | (flags & GMEM_MOVEABLE);
        return h;
    }
    /* A pointer returned by GlobalLock/LocalLock must not move. */
    if (m->lock && bytes != m->size)
        return 0;
    void *n = win_realloc(m->p, bytes ? bytes : 1);
    if (!n)
        return 0;
    if (bytes > m->size)
        memset((char *)n + m->size, 0, bytes - m->size);
    m->p = n;
    m->size = bytes;
    return h;
}

static HANDLE mem_free(int type, HANDLE h)
{
    wmem *m = (wmem *)win_handle_get(h, type);
    if (!m)
        return h;
    win_free(m->p);
    win_free(m);
    win_handle_free(h);
    return 0;
}

HGLOBAL WINAPI GlobalAlloc(UINT flags, DWORD bytes) { return mem_alloc(WOBJ_GLOBAL, flags, bytes); }
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL h, DWORD bytes, UINT flags) { return mem_realloc(WOBJ_GLOBAL, h, bytes, flags); }
HGLOBAL WINAPI GlobalFree(HGLOBAL h) { return mem_free(WOBJ_GLOBAL, h); }

LPVOID WINAPI GlobalLock(HGLOBAL h)
{
    wmem *m = (wmem *)win_handle_get(h, WOBJ_GLOBAL);
    if (!m)
        return NULL;
    m->lock++;
    return m->p;
}

BOOL WINAPI GlobalUnlock(HGLOBAL h)
{
    wmem *m = (wmem *)win_handle_get(h, WOBJ_GLOBAL);
    if (!m)
        return FALSE;
    if (m->lock > 0)
        m->lock--;
    return m->lock > 0;
}

DWORD WINAPI GlobalSize(HGLOBAL h)
{
    wmem *m = (wmem *)win_handle_get(h, WOBJ_GLOBAL);
    return m ? m->size : 0;
}

HLOCAL WINAPI LocalAlloc(UINT flags, UINT bytes) { return mem_alloc(WOBJ_LOCAL, flags, bytes); }
HLOCAL WINAPI LocalReAlloc(HLOCAL h, UINT bytes, UINT flags) { return mem_realloc(WOBJ_LOCAL, h, bytes, flags); }
HLOCAL WINAPI LocalFree(HLOCAL h) { return mem_free(WOBJ_LOCAL, h); }

LPVOID WINAPI LocalLock(HLOCAL h)
{
    wmem *m = (wmem *)win_handle_get(h, WOBJ_LOCAL);
    if (!m)
        return NULL;
    m->lock++;
    return m->p;
}

BOOL WINAPI LocalUnlock(HLOCAL h)
{
    wmem *m = (wmem *)win_handle_get(h, WOBJ_LOCAL);
    if (!m)
        return FALSE;
    if (m->lock > 0)
        m->lock--;
    return m->lock > 0;
}

UINT WINAPI LocalSize(HLOCAL h)
{
    wmem *m = (wmem *)win_handle_get(h, WOBJ_LOCAL);
    return m ? (UINT)m->size : 0;
}

/* ---------------------------------------------------------------- misc */

DWORD WINAPI GetTickCount(void)
{
    return win_sys.input ? win_sys.input->ticks_ms() : 0;
}

DWORD WINAPI GetCurrentTime(void) { return GetTickCount(); }

/* Windows 3.10 on DOS 7.0 (HIWORD = DOS version, LOWORD = 0x0A03). */
DWORD WINAPI GetVersion(void) { return 0x07000A03UL; }

HMODULE WINAPI GetModuleHandle(LPCSTR name)
{
    wtask *t = task_current();
    /* At this stage an application is one EZ image (one task). */
    if (!name || !*name || !lstrcmpi(name, t->name))
        return (HMODULE)t->h;
    return win_dll_handle(win_dll_find(name));      /* a native DLL loaded */
}

void WINAPI FatalExit(int code) { exit(code); }

void WINAPI OutputDebugString(LPCSTR str) { (void)str; }

/* ---------------------------------------------------------------- strings */

int WINAPI lstrlen(LPCSTR s) { return s ? (int)strlen(s) : 0; }

LPSTR WINAPI lstrcpy(LPSTR d, LPCSTR s) { return strcpy(d, s ? s : ""); }

LPSTR WINAPI lstrcpyn(LPSTR d, LPCSTR s, int n)
{
    if (n <= 0)
        return d;
    int i = 0;
    for (; i < n - 1 && s && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
    return d;
}

LPSTR WINAPI lstrcat(LPSTR d, LPCSTR s) { return strcat(d, s ? s : ""); }

int WINAPI lstrcmp(LPCSTR a, LPCSTR b) { return strcmp(a ? a : "", b ? b : ""); }

/* CP1251 letter pairs outside the contiguous 0xC0-0xFF Cyrillic range. */
static const unsigned char cp1251_case_pairs[][2] = {
    {0x80, 0x90}, /* U+0402/U+0452 */
    {0x81, 0x83}, /* U+0403/U+0453 */
    {0x8A, 0x9A}, /* U+0409/U+0459 */
    {0x8C, 0x9C}, /* U+040A/U+045A */
    {0x8D, 0x9D}, /* U+040C/U+045C */
    {0x8E, 0x9E}, /* U+040B/U+045B */
    {0x8F, 0x9F}, /* U+040F/U+045F */
    {0xA1, 0xA2}, /* U+040E/U+045E */
    {0xA3, 0xBC}, /* U+0408/U+0458 */
    {0xA5, 0xB4}, /* U+0490/U+0491 */
    {0xA8, 0xB8}, /* U+0401/U+0451 */
    {0xAA, 0xBA}, /* U+0404/U+0454 */
    {0xAF, 0xBF}, /* U+0407/U+0457 */
    {0xB2, 0xB3}, /* U+0406/U+0456 */
    {0xBD, 0xBE}, /* U+0405/U+0455 */
};

static int to_upper(int c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFF))
        return c - 0x20;
    for (unsigned i = 0; i < sizeof(cp1251_case_pairs) / sizeof(cp1251_case_pairs[0]); i++) {
        if (c == cp1251_case_pairs[i][1])
            return cp1251_case_pairs[i][0];
    }
    return c;
}

static int to_lower(int c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDF))
        return c + 0x20;
    for (unsigned i = 0; i < sizeof(cp1251_case_pairs) / sizeof(cp1251_case_pairs[0]); i++) {
        if (c == cp1251_case_pairs[i][0])
            return cp1251_case_pairs[i][1];
    }
    return c;
}

int WINAPI lstrcmpi(LPCSTR a, LPCSTR b)
{
    a = a ? a : "";
    b = b ? b : "";
    for (;; a++, b++) {
        int ca = to_upper((unsigned char)*a), cb = to_upper((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        if (!ca)
            return 0;
    }
}

LPSTR WINAPI AnsiUpper(LPSTR s)
{
    if (IS_INTRESOURCE(s))
        return (LPSTR)(uintptr_t)to_upper((int)(uintptr_t)s);
    for (char *p = s; *p; p++)
        *p = (char)to_upper((unsigned char)*p);
    return s;
}

LPSTR WINAPI AnsiLower(LPSTR s)
{
    if (IS_INTRESOURCE(s))
        return (LPSTR)(uintptr_t)to_lower((int)(uintptr_t)s);
    for (char *p = s; *p; p++)
        *p = (char)to_lower((unsigned char)*p);
    return s;
}

UINT WINAPI AnsiUpperBuff(LPSTR s, UINT n)
{
    UINT len = n ? n : 65536u;
    for (UINT i = 0; i < len; i++)
        s[i] = (char)to_upper((unsigned char)s[i]);
    return len;
}

UINT WINAPI AnsiLowerBuff(LPSTR s, UINT n)
{
    UINT len = n ? n : 65536u;
    for (UINT i = 0; i < len; i++)
        s[i] = (char)to_lower((unsigned char)s[i]);
    return len;
}

/* Classify unsigned CP1251 bytes using the same case mappings. */
BOOL WINAPI IsCharUpper(char c)
{
    int ch = (unsigned char)c;
    return to_lower(ch) != ch;
}

BOOL WINAPI IsCharLower(char c)
{
    int ch = (unsigned char)c;
    return to_upper(ch) != ch;
}

BOOL WINAPI IsCharAlpha(char c) { return IsCharUpper(c) || IsCharLower(c); }
BOOL WINAPI IsCharAlphaNumeric(char c) { return IsCharAlpha(c) || (c >= '0' && c <= '9'); }

LPSTR WINAPI AnsiNext(LPCSTR s) { return (LPSTR)(*s ? s + 1 : s); }
LPSTR WINAPI AnsiPrev(LPCSTR start, LPCSTR s) { return (LPSTR)(s > start ? s - 1 : start); }

/* wsprintf: %[-][0][width][l](d|i|u|x|X|s|c|%) - no floats, like Win16. */
static int fmt_num(char *out, unsigned long v, int neg, int base, int upper,
                   int width, int left, int zero)
{
    char tmp[16];
    int n = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        tmp[n++] = dig[v % (unsigned)base];
        v /= (unsigned)base;
    } while (v);
    int len = n + (neg ? 1 : 0);
    int pad = width > len ? width - len : 0;
    int o = 0;
    if (!left && !zero)
        while (pad-- > 0) out[o++] = ' ';
    if (neg)
        out[o++] = '-';
    if (!left && zero)
        while (pad-- > 0) out[o++] = '0';
    while (n)
        out[o++] = tmp[--n];
    if (left)
        while (pad-- > 0) out[o++] = ' ';
    return o;
}

int WINAPI wvsprintf(LPSTR buf, LPCSTR fmt, va_list ap)
{
    int o = 0;
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            buf[o++] = *f;
            continue;
        }
        f++;
        int left = 0, zero = 0, width = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (*f++ - '0');
        if (*f == 'l') f++;
        switch (*f) {
        case 'd': case 'i': {
            long v = va_arg(ap, int);           /* LONG is 32-bit too */
            unsigned long m = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
            o += fmt_num(buf + o, m, v < 0, 10, 0, width, left, zero);
            break;
        }
        case 'u': case 'x': case 'X': {
            unsigned long v = va_arg(ap, unsigned);
            o += fmt_num(buf + o, v, 0, *f == 'u' ? 10 : 16, *f == 'X', width, left, zero);
            break;
        }
        case 'c':
            buf[o++] = (char)va_arg(ap, int);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            int pad = width > len ? width - len : 0;
            if (!left) while (pad-- > 0) buf[o++] = ' ';
            memcpy(buf + o, s, (size_t)len);
            o += len;
            if (left) while (pad-- > 0) buf[o++] = ' ';
            break;
        }
        case '%':
            buf[o++] = '%';
            break;
        case 0:
            f--;
            break;
        default:
            buf[o++] = *f;
            break;
        }
    }
    buf[o] = 0;
    return o;
}

int WINAPI wsprintf(LPSTR buf, LPCSTR fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

/* ---------------------------------------------------------------- atoms */

/* String atoms are 0xC000..0xFFFF, integer atoms are the value itself. */
#define ATOM_BASE 0xC000
#define ATOM_MAX  512

typedef struct { char *name; int refs; } watom;
static watom atoms[ATOM_MAX];

/* String atoms use only the allocated range, not all 0xC000..0xFFFF. */
static int atom_is_stored(ATOM a)
{
    return a >= ATOM_BASE && (unsigned)(a - ATOM_BASE) < ATOM_MAX;
}

static int atom_is_int(LPCSTR name, ATOM *out)
{
    if (IS_INTRESOURCE(name)) {
        uintptr_t v = (uintptr_t)name;
        *out = (ATOM)(v && v < ATOM_BASE ? v : 0);
        return 1;
    }
    if (name[0] == '#' && name[1]) {
        unsigned v = 0;
        for (const char *p = name + 1; *p; p++) {
            if (*p < '0' || *p > '9')
                return 0;
            unsigned digit = (unsigned)(*p - '0');
            if (v > (ATOM_BASE - 1u - digit) / 10u)
                return 0;
            v = v * 10u + digit;
        }
        if (!v)
            return 0;
        *out = (ATOM)v;
        return 1;
    }
    return 0;
}

ATOM WINAPI FindAtom(LPCSTR name)
{
    ATOM a;
    if (!name)
        return 0;
    if (atom_is_int(name, &a))
        return a;
    for (int i = 0; i < ATOM_MAX; i++)
        if (atoms[i].name && !lstrcmpi(atoms[i].name, name))
            return (ATOM)(ATOM_BASE + i);
    return 0;
}

ATOM WINAPI AddAtom(LPCSTR name)
{
    ATOM a;
    if (!name)
        return 0;
    if (atom_is_int(name, &a))
        return a;
    a = FindAtom(name);
    if (a) {
        if (atom_is_stored(a))
            atoms[a - ATOM_BASE].refs++;
        return a;
    }
    for (int i = 0; i < ATOM_MAX; i++) {
        if (!atoms[i].name) {
            atoms[i].name = win_strdup(name);
            if (!atoms[i].name)
                return 0;
            atoms[i].refs = 1;
            return (ATOM)(ATOM_BASE + i);
        }
    }
    return 0;
}

ATOM WINAPI DeleteAtom(ATOM a)
{
    if (a < ATOM_BASE)
        return 0;
    if (!atom_is_stored(a))
        return a;
    watom *e = &atoms[a - ATOM_BASE];
    if (!e->name)
        return a;
    if (--e->refs <= 0) {
        win_free(e->name);
        e->name = NULL;
    }
    return 0;
}

/* the global atom table: the same one (WINN has one for all) */
ATOM WINAPI GlobalAddAtom(LPCSTR name) { return AddAtom(name); }
ATOM WINAPI GlobalFindAtom(LPCSTR name) { return FindAtom(name); }
ATOM WINAPI GlobalDeleteAtom(ATOM a) { return DeleteAtom(a); }
UINT WINAPI GlobalGetAtomName(ATOM a, LPSTR buf, int size) { return GetAtomName(a, buf, size); }

UINT WINAPI GetAtomName(ATOM a, LPSTR buf, int size)
{
    if (!buf || size <= 0)
        return 0;
    if (a < ATOM_BASE) {
        char tmp[8];
        wsprintf(tmp, "#%u", (unsigned)a);
        lstrcpyn(buf, tmp, size);
        return (UINT)lstrlen(buf);
    }
    if (!atom_is_stored(a))
        return 0;
    watom *e = &atoms[a - ATOM_BASE];
    if (!e->name)
        return 0;
    lstrcpyn(buf, e->name, size);
    return (UINT)lstrlen(buf);
}

/* ---------------------------------------------------------------- the rest of 3.1's */

BOOL WINAPI DefineHandleTable(WORD offset) { (void)offset; return TRUE; }
void WINAPI ValidateCodeSegments(void) {}

/* the swap area: as large as asked, at most 64K paragraphs */
LONG WINAPI SetSwapAreaSize(UINT size)
{
    return MAKELONG(size, 0xFFFF);
}

void WINAPI LimitEMSPages(DWORD kb) { (void)kb; }

/* the atom table: WINN's grows as it must; an atom is its own handle */
BOOL WINAPI InitAtomTable(int size) { return size > 0; }
HANDLE WINAPI GetAtomHandle(ATOM atom) { return (HANDLE)atom; }

/* WINN's global memory is never discarded: no order to tell */
HGLOBAL WINAPI GlobalLRUOldest(HGLOBAL h) { return h; }
HGLOBAL WINAPI GlobalLRUNewest(HGLOBAL h) { return h; }
