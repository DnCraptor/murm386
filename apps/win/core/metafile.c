/*
 * metafile.c - Windows metafiles: a DC of CreateMetaFile's records the GDI
 * calls made on it, as Windows 3.x has them (a header, then records: the
 * size in words, the function, its arguments the last first), until
 * CloseMetaFile makes them a metafile; PlayMetaFile, EnumMetaFile and
 * PlayMetaFileRecord do them again on another DC. In memory, or a file's
 * (GetMetaFile, CopyMetaFile; a placeable one's header skipped), or a
 * global block's (GetMetaFileBits, SetMetaFileBits).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdarg.h>
#include <string.h>

#include "wincore.h"

#define MF_HEADER   18              /* bytes: METAHEADER as a file has it */
#define MF_OBJECTS  128             /* the objects one recording keeps */
#define MF_PIXELS   (1L << 20)      /* the most a blit's bits become */

typedef struct wmrec {
    uint8_t *buf;
    uint32_t len, cap;              /* bytes */
    uint32_t max;                   /* the longest record, in words */
    int      bad;                   /* out of memory: CloseMetaFile fails */
    int      nobj;                  /* the table's slots used so far */
    HGDIOBJ  obj[MF_OBJECTS];       /* what each record's index stands for */
    char    *path;                  /* CreateMetaFile(name)'s file, or NULL */
    wdc     *dc;
    struct wmrec *next;
} wmrec;

typedef struct wmeta {
    uint32_t size;
    uint8_t *bits;
} wmeta;

static wmrec *recorders;            /* the DCs recording */

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }
static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }

/* ---------------------------------------------------------------- recording */

/* a record of function fn with bytes of arguments (made even, zero): where
 * they go; NULL when there is no memory */
static uint8_t *mf_new(wmrec *m, int fn, uint32_t bytes)
{
    if (!m || m->bad)
        return NULL;
    bytes = (bytes + 1) & ~1u;
    uint32_t need = m->len + 6 + bytes;
    if (need > m->cap) {
        uint32_t cap = m->cap ? m->cap : 256;
        while (cap < need)
            cap *= 2;
        uint8_t *b = (uint8_t *)win_realloc(m->buf, cap);
        if (!b) {
            m->bad = 1;
            return NULL;
        }
        m->buf = b;
        m->cap = cap;
    }
    uint8_t *r = m->buf + m->len;
    uint32_t words = (6 + bytes) / 2;
    put32(r, words);
    put16(r + 4, (unsigned)fn);
    memset(r + 6, 0, bytes);
    m->len = need;
    if (words > m->max)
        m->max = words;
    return r + 6;
}

BOOL mf_rec(wdc *dc, int fn, int n, ...)
{
    uint8_t *p = mf_new(dc->meta, fn, 2u * (uint32_t)n);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        int v = va_arg(ap, int);
        if (p)
            put16(p + 2 * (n - 1 - i), (unsigned)v);
    }
    va_end(ap);
    return p != NULL;
}

/* a packed DIB, its rows of the pattern: 1 bit (black and white) or the
 * screen's indices */
static uint32_t pattern_dib(const wbrush *b, uint8_t *d)
{
    int mono = b->pat_is_mono, ncol = mono ? 2 : 256, pitch = mono ? 4 : 8;
    uint32_t len = 40 + 4u * (uint32_t)ncol + 8u * (uint32_t)pitch;
    if (!d)
        return len;
    memset(d, 0, len);
    put32(d, 40);
    put32(d + 4, 8);
    put32(d + 8, 8);
    put16(d + 12, 1);
    put16(d + 14, mono ? 1 : 8);
    put32(d + 20, 8u * (uint32_t)pitch);
    uint8_t *c = d + 40;
    for (int i = 0; i < ncol; i++, c += 4) {
        const PALETTEENTRY *e = &win_sys.syspal[i];
        c[0] = mono ? (uint8_t)(i ? 255 : 0) : e->peBlue;
        c[1] = mono ? (uint8_t)(i ? 255 : 0) : e->peGreen;
        c[2] = mono ? (uint8_t)(i ? 255 : 0) : e->peRed;
    }
    for (int y = 0; y < 8; y++) {                   /* bottom up */
        uint8_t *row = c + (7 - y) * pitch;
        if (mono)
            row[0] = b->patmono[y];
        else
            memcpy(row, b->pat8 + 8 * y, 8);
    }
    return len;
}

/* the region's rectangles from the top down, each a scan of its own */
static int rect_order(const void *a, const void *b)
{
    const RECT *p = (const RECT *)a, *q = (const RECT *)b;
    return p->top != q->top ? (p->top < q->top ? -1 : 1)
                            : p->left != q->left ? (p->left < q->left ? -1 : 1) : 0;
}

static int create_region(wmrec *m, const wrgn *g)
{
    int n = g->n;
    uint32_t size = 22 + 12u * (uint32_t)n;
    uint8_t *p = mf_new(m, META_CREATEREGION, size);
    RECT *r = n ? (RECT *)win_alloc(sizeof(RECT) * (size_t)n) : NULL;
    if (!p || (n && !r)) {
        win_free(r);
        return 0;
    }
    if (n) {
        memcpy(r, g->r, sizeof(RECT) * (size_t)n);
        for (int i = 1; i < n; i++)                 /* by insertion: they are few */
            for (int j = i; j > 0 && rect_order(&r[j - 1], &r[j]) > 0; j--) {
                RECT t = r[j];
                r[j] = r[j - 1];
                r[j - 1] = t;
            }
    }
    put16(p + 2, 6);                                /* a region */
    put16(p + 8, size);
    put16(p + 10, (unsigned)n);
    put16(p + 12, n ? 2 : 0);
    if (n) {
        put16(p + 14, (unsigned)g->box.left);
        put16(p + 16, (unsigned)g->box.top);
        put16(p + 18, (unsigned)g->box.right);
        put16(p + 20, (unsigned)g->box.bottom);
    }
    uint8_t *s = p + 22;
    for (int i = 0; i < n; i++, s += 12) {
        put16(s, 2);
        put16(s + 2, (unsigned)r[i].top);
        put16(s + 4, (unsigned)r[i].bottom);
        put16(s + 6, (unsigned)r[i].left);
        put16(s + 8, (unsigned)r[i].right);
        put16(s + 10, 2);
    }
    win_free(r);
    return 1;
}

/* the record that makes the object again */
static int create(wmrec *m, HGDIOBJ obj)
{
    uint8_t *p;
    switch (win_handle_type(obj)) {
    case WOBJ_PEN: {
        const wpen *w = (const wpen *)win_handle_get(obj, WOBJ_PEN);
        if (!(p = mf_new(m, META_CREATEPENINDIRECT, 10)))
            return 0;
        put16(p, (unsigned)w->style);
        put16(p + 2, (unsigned)w->width);
        put32(p + 6, w->color);
        return 1;
    }
    case WOBJ_BRUSH: {
        const wbrush *b = (const wbrush *)win_handle_get(obj, WOBJ_BRUSH);
        if (b->style == WBR_PATTERN) {
            uint32_t len = pattern_dib(b, NULL);
            if (!(p = mf_new(m, META_DIBCREATEPATTERNBRUSH, 4 + len)))
                return 0;
            put16(p, 5);                            /* BS_DIBPATTERN */
            put16(p + 2, DIB_RGB_COLORS);
            pattern_dib(b, p + 4);
            return 1;
        }
        if (!(p = mf_new(m, META_CREATEBRUSHINDIRECT, 8)))
            return 0;
        put16(p, b->style == WBR_NULL ? BS_NULL : b->style == WBR_HATCH ? BS_HATCHED : BS_SOLID);
        put32(p + 2, b->color);
        put16(p + 6, (unsigned)b->hatch);
        return 1;
    }
    case WOBJ_FONT: {
        const LOGFONT *lf = &((const wfont *)win_handle_get(obj, WOBJ_FONT))->lf;
        if (!(p = mf_new(m, META_CREATEFONTINDIRECT, 50)))
            return 0;
        put16(p, (unsigned)lf->lfHeight);
        put16(p + 2, (unsigned)lf->lfWidth);
        put16(p + 4, (unsigned)lf->lfEscapement);
        put16(p + 6, (unsigned)lf->lfOrientation);
        put16(p + 8, (unsigned)lf->lfWeight);
        p[10] = lf->lfItalic; p[11] = lf->lfUnderline; p[12] = lf->lfStrikeOut;
        p[13] = lf->lfCharSet; p[14] = lf->lfOutPrecision; p[15] = lf->lfClipPrecision;
        p[16] = lf->lfQuality; p[17] = lf->lfPitchAndFamily;
        memcpy(p + 18, lf->lfFaceName, LF_FACESIZE);
        p[18 + LF_FACESIZE - 1] = 0;
        return 1;
    }
    case WOBJ_PALETTE: {
        const wpalette *w = (const wpalette *)win_handle_get(obj, WOBJ_PALETTE);
        if (!(p = mf_new(m, META_CREATEPALETTE, 4 + 4u * (uint32_t)w->n)))
            return 0;
        put16(p, 0x300);
        put16(p + 2, (unsigned)w->n);
        for (int i = 0; i < w->n; i++)
            memcpy(p + 4 + 4 * i, &w->e[i], 4);
        return 1;
    }
    case WOBJ_REGION:
        return create_region(m, gdi_region(obj));
    default:
        return 0;                                   /* bitmaps: not in metafiles */
    }
}

int mf_object(wdc *dc, HGDIOBJ obj)
{
    wmrec *m = dc->meta;
    if (!m || !obj)
        return -1;
    int slot = -1;
    for (int i = 0; i < m->nobj; i++) {
        if (m->obj[i] == obj)
            return i;
        if (!m->obj[i] && slot < 0)
            slot = i;
    }
    if (slot < 0 && m->nobj == MF_OBJECTS)
        return -1;
    if (!create(m, obj))
        return -1;
    if (slot < 0)                                   /* the player's way: the first free */
        slot = m->nobj++;
    m->obj[slot] = obj;
    return slot;
}

void mf_select(wdc *dc, HGDIOBJ obj)
{
    int i = mf_object(dc, obj);
    if (i >= 0)
        mf_rec(dc, win_handle_type(obj) == WOBJ_PALETTE ? META_SELECTPALETTE : META_SELECTOBJECT,
               1, i);
}

void mf_forget(HGDIOBJ obj)
{
    for (wmrec *m = recorders; m; m = m->next)
        for (int i = 0; i < m->nobj; i++)
            if (m->obj[i] == obj) {
                mf_rec(m->dc, META_DELETEOBJECT, 1, i);
                m->obj[i] = 0;
            }
}

static void unlink_rec(wmrec *m)
{
    for (wmrec **pp = &recorders; *pp; pp = &(*pp)->next)
        if (*pp == m) {
            *pp = m->next;
            break;
        }
}

void mf_dc_gone(wdc *dc)
{
    wmrec *m = dc->meta;
    if (!m)
        return;
    unlink_rec(m);
    dc->meta = NULL;
    win_free(m->buf);
    win_free(m->path);
    win_free(m);
}

/* TEXTOUT: the count, the string, y, x; EXTTEXTOUT: y, x, the count, the
 * options, the rectangle (when they have it), the string, the widths */
BOOL mf_text(wdc *dc, int x, int y, UINT opts, const RECT *rc, LPCSTR s, int n,
             const int *dx, int ext)
{
    if (n < 0)
        n = 0;
    uint32_t sn = ((uint32_t)n + 1) & ~1u;
    if (!ext) {
        uint8_t *p = mf_new(dc->meta, META_TEXTOUT, 2 + sn + 4);
        if (!p)
            return FALSE;
        put16(p, (unsigned)n);
        memcpy(p + 2, s, (size_t)n);
        put16(p + 2 + sn, (unsigned)y);
        put16(p + 4 + sn, (unsigned)x);
        return TRUE;
    }
    if (!rc)
        opts &= ~(UINT)(ETO_OPAQUE | ETO_CLIPPED);
    uint32_t rn = opts & (ETO_OPAQUE | ETO_CLIPPED) ? 8 : 0;
    uint8_t *p = mf_new(dc->meta, META_EXTTEXTOUT, 8 + rn + sn + (dx ? 2u * (uint32_t)n : 0));
    if (!p)
        return FALSE;
    put16(p, (unsigned)y);
    put16(p + 2, (unsigned)x);
    put16(p + 4, (unsigned)n);
    put16(p + 6, opts);
    if (rn) {
        put16(p + 8, (unsigned)rc->left);
        put16(p + 10, (unsigned)rc->top);
        put16(p + 12, (unsigned)rc->right);
        put16(p + 14, (unsigned)rc->bottom);
    }
    memcpy(p + 8 + rn, s, (size_t)n);
    if (dx)
        for (int i = 0; i < n; i++)
            put16(p + 8 + rn + sn + 2 * i, (unsigned)dx[i]);
    return TRUE;
}

/* POLYGON, POLYLINE: the count, the points; POLYPOLYGON (counts): the
 * number of polygons, their counts, all the points */
BOOL mf_points(wdc *dc, const POINT *pts, const int *counts, int npoly, int fn)
{
    int total = 0;
    for (int i = 0; i < npoly; i++)
        total += counts[i] > 0 ? counts[i] : 0;
    int lead = fn == META_POLYPOLYGON ? 1 + npoly : 1;
    uint8_t *p = mf_new(dc->meta, fn, 2u * (uint32_t)lead + 4u * (uint32_t)total);
    if (!p)
        return FALSE;
    if (fn == META_POLYPOLYGON) {
        put16(p, (unsigned)npoly);
        for (int i = 0; i < npoly; i++)
            put16(p + 2 + 2 * i, (unsigned)(counts[i] > 0 ? counts[i] : 0));
    } else {
        put16(p, (unsigned)total);
    }
    uint8_t *q = p + 2 * lead;
    for (int i = 0; i < total; i++, q += 4) {
        put16(q, (unsigned)pts[i].x);
        put16(q + 2, (unsigned)pts[i].y);
    }
    return TRUE;
}

/* a DIB's header with its colours, and the bytes of its rows (lines of them:
 * 0 all) */
static uint32_t dib_head(const BITMAPINFO *bi, UINT usage)
{
    const BITMAPINFOHEADER *h = &bi->bmiHeader;
    if (h->biSize == 12) {                          /* BITMAPCOREHEADER */
        const uint8_t *c = (const uint8_t *)bi;
        int bpp = (int)get16(c + 10);
        return 12 + (bpp <= 8 ? (usage == DIB_PAL_COLORS ? 2u : 3u) << bpp : 0);
    }
    uint32_t ncol = h->biClrUsed ? h->biClrUsed : h->biBitCount <= 8 ? 1u << h->biBitCount : 0;
    return h->biSize + ncol * (usage == DIB_PAL_COLORS ? 2u : 4u);
}

static uint32_t dib_rows(const BITMAPINFO *bi, int lines)
{
    const BITMAPINFOHEADER *h = &bi->bmiHeader;
    long w, hh;
    int bpp;
    if (h->biSize == 12) {
        const uint8_t *c = (const uint8_t *)bi;
        w = (long)get16(c + 4);
        hh = (long)get16(c + 6);
        bpp = (int)get16(c + 10);
    } else {
        if (h->biCompression != BI_RGB)
            return h->biSizeImage;
        w = h->biWidth;
        hh = h->biHeight < 0 ? -h->biHeight : h->biHeight;
        bpp = h->biBitCount;
    }
    if (lines > 0 && lines < hh)
        hh = lines;
    return (uint32_t)((w * bpp + 31) / 32 * 4) * (uint32_t)hh;
}

BOOL mf_dib(wdc *dc, int fn, const int *words, int n, const BITMAPINFO *bi, const void *bits,
            UINT usage, int lines)
{
    if (!bi || !bits)
        return FALSE;
    uint32_t head = dib_head(bi, usage), rows = dib_rows(bi, lines);
    uint8_t *p = mf_new(dc->meta, fn, 2u * (uint32_t)n + head + rows);
    if (!p)
        return FALSE;
    for (int i = 0; i < n; i++)
        put16(p + 2 * (n - 1 - i), (unsigned)words[i]);
    memcpy(p + 2 * n, bi, head);
    memcpy(p + 2 * n + head, bits, rows);
    return TRUE;
}

/* BitBlt, StretchBlt: the source's pixels a DIB of their own (8 bits, the
 * screen's colours); without a source, PATBLT */
BOOL mf_blt(wdc *dc, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh,
            DWORD rop, int stretch)
{
    if (!((rop >> 2 ^ rop) & 0x00330000ul))
        return mf_rec(dc, META_PATBLT, 6, x, y, w, h, MF_DW(rop));
    if (!stretch) {
        sw = w;
        sh = h;
    }
    if (sw < 0)
        sw = -sw;
    if (sh < 0)
        sh = -sh;
    if (!gdi_dc(src) || !sw || !sh || (long)sw * sh > MF_PIXELS)
        return FALSE;
    HDC m = CreateCompatibleDC(0);
    HBITMAP b = m ? CreateBitmap(sw, sh, 1, 8, NULL) : 0;
    uint32_t pitch = ((uint32_t)sw + 3) & ~3u, head = 40 + 256 * 4;
    uint8_t *d = b ? (uint8_t *)win_alloc(head + pitch * (uint32_t)sh) : NULL;
    BOOL ok = FALSE;
    if (d) {
        HGDIOBJ old = SelectObject(m, b);
        BitBlt(m, 0, 0, sw, sh, src, sx, sy, SRCCOPY);
        SelectObject(m, old);
        BITMAPINFO *bi = (BITMAPINFO *)d;
        bi->bmiHeader.biSize = 40;
        bi->bmiHeader.biBitCount = 8;
        if (GetDIBits(m, b, 0, (UINT)sh, d + head, bi, DIB_RGB_COLORS)) {
            int a[10] = { x, y, w, h, 0, 0, sw, sh, (int)(rop >> 16), (int)(rop & 0xffffu) };
            if (stretch)
                ok = mf_dib(dc, META_DIBSTRETCHBLT, a, 10, bi, d + head, DIB_RGB_COLORS, 0);
            else {
                int c[8] = { x, y, w, h, 0, 0, a[8], a[9] };
                ok = mf_dib(dc, META_DIBBITBLT, c, 8, bi, d + head, DIB_RGB_COLORS, 0);
            }
        }
    }
    win_free(d);
    if (b)
        DeleteObject(b);
    if (m)
        DeleteDC(m);
    return ok;
}

/* FILLREGION (the region, the brush), FRAMEREGION (the region, the brush,
 * w, h; the last first), INVERTREGION, PAINTREGION (the region) */
BOOL mf_region_op(wdc *dc, int fn, HRGN rgn, HBRUSH br, int w, int h)
{
    int r = mf_object(dc, rgn), b = br ? mf_object(dc, br) : 0;
    if (r < 0 || b < 0)
        return FALSE;
    if (fn == META_FILLREGION) {
        uint8_t *p = mf_new(dc->meta, fn, 4);
        if (p) {
            put16(p, (unsigned)r);
            put16(p + 2, (unsigned)b);
        }
        return p != NULL;
    }
    if (fn == META_FRAMEREGION)
        return mf_rec(dc, fn, 4, r, b, w, h);
    return mf_rec(dc, fn, 1, r);
}

/* ---------------------------------------------------------------- metafiles */

static wmeta *meta_of(HMETAFILE h) { return (wmeta *)win_handle_get(h, WOBJ_METAFILE); }

/* a header Windows would play, its records within size */
static int valid(const uint8_t *p, uint32_t size)
{
    if (size < MF_HEADER)
        return 0;
    unsigned type = get16(p), head = get16(p + 2), ver = get16(p + 4);
    return (type == 1 || type == 2) && head == 9 && (ver == 0x100 || ver == 0x300) &&
           get32(p + 6) >= 9 && get32(p + 6) <= size / 2;
}

/* a metafile of size bytes (taken: freed if not) */
static HMETAFILE meta_new(uint8_t *bits, uint32_t size)
{
    wmeta *m = bits ? (wmeta *)win_alloc(sizeof(*m)) : NULL;
    HMETAFILE h = m ? win_handle_alloc(WOBJ_METAFILE, m) : 0;
    if (!h) {
        win_free(m);
        win_free(bits);
        return 0;
    }
    m->bits = bits;
    m->size = size;
    return h;
}

HMETAFILE mf_from_bits(const uint8_t *p, uint32_t size)
{
    if (!p || !valid(p, size))
        return 0;
    size = get32(p + 6) * 2;
    uint8_t *b = (uint8_t *)win_alloc(size);
    if (!b)
        return 0;
    memcpy(b, p, size);
    return meta_new(b, size);
}

const uint8_t *mf_bits(HMETAFILE mf, uint32_t *size)
{
    wmeta *m = meta_of(mf);
    if (!m)
        return NULL;
    *size = m->size;
    return m->bits;
}

/* without files (the GDI tests): none written, none read (file.c's) */
__attribute__((weak)) BOOL win_file_save(const char *path, const void *p, uint32_t size)
{
    (void)path; (void)p; (void)size;
    return FALSE;
}

__attribute__((weak)) uint8_t *win_file_load(const char *path, uint32_t *size, uint32_t max)
{
    (void)path; (void)size; (void)max;
    return NULL;
}

__attribute__((weak)) uint8_t *win_file_load_parts(
    const char *path, uint32_t *size, uint32_t max, uint32_t head,
    int (*part)(void *ctx, const uint8_t *d, uint32_t size, unsigned i, uint32_t *at, uint32_t *len),
    void *ctx)
{
    (void)path; (void)size; (void)max; (void)head; (void)part; (void)ctx;
    return NULL;
}

static BOOL write_file(LPCSTR path, uint8_t *bits, uint32_t size)
{
    unsigned type = get16(bits);
    put16(bits, 2);                                 /* a disk metafile there */
    BOOL ok = win_file_save(path, bits, size);
    put16(bits, type);
    return ok;
}

HDC WINAPI CreateMetaFile(LPCSTR file)
{
    HDC h = CreateCompatibleDC(0);
    wdc *dc = h ? gdi_dc(h) : NULL;
    wmrec *m = dc ? (wmrec *)win_alloc(sizeof(*m)) : NULL;
    int ok = m && (!file || (m->path = win_strdup(file)) != NULL);
    if (ok && file)                                 /* it is made now, as Windows does */
        ok = win_file_save(file, "", 0);
    if (ok && (m->buf = (uint8_t *)win_alloc(256)) != NULL) {
        m->cap = 256;
        m->len = MF_HEADER;
        m->max = 3;
        m->dc = dc;
        dc->meta = m;
        m->next = recorders;
        recorders = m;
        return h;
    }
    if (m) {
        win_free(m->path);
        win_free(m);
    }
    if (h)
        DeleteDC(h);
    return 0;
}

HMETAFILE WINAPI CloseMetaFile(HDC h)
{
    wdc *dc = gdi_dc(h);
    wmrec *m = dc ? dc->meta : NULL;
    if (!m)
        return 0;
    mf_new(m, 0, 0);                                /* the end */
    HMETAFILE r = 0;
    if (!m->bad) {
        uint8_t *b = m->buf;
        put16(b, m->path ? 2 : 1);
        put16(b + 2, 9);
        put16(b + 4, 0x300);
        put32(b + 6, m->len / 2);
        put16(b + 10, (unsigned)m->nobj);
        put32(b + 12, m->max);
        put16(b + 16, 0);
        if (!m->path || write_file(m->path, b, m->len)) {
            uint8_t *t = (uint8_t *)win_realloc(b, m->len);
            m->buf = NULL;
            r = meta_new(t ? t : b, m->len);
        }
    }
    mf_dc_gone(dc);
    DeleteDC(h);
    return r;
}

BOOL WINAPI DeleteMetaFile(HMETAFILE mf)
{
    wmeta *m = meta_of(mf);
    if (!m)
        return FALSE;
    win_free(m->bits);
    win_free(m);
    win_handle_free(mf);
    return TRUE;
}

BOOL WINAPI IsValidMetaFile(HMETAFILE mf)
{
    wmeta *m = meta_of(mf);
    return m && valid(m->bits, m->size);
}

HMETAFILE WINAPI GetMetaFile(LPCSTR file)
{
    uint32_t n = 0;
    uint8_t *b = file ? win_file_load(file, &n, 0x1000000u) : NULL;
    if (!b)
        return 0;
    uint32_t at = n >= 4 && get32(b) == 0x9AC6CDD7u ? 22 : 0;  /* a placeable one: its header */
    HMETAFILE h = n > at ? mf_from_bits(b + at, n - at) : 0;
    win_free(b);
    return h;
}

HMETAFILE WINAPI CopyMetaFile(HMETAFILE mf, LPCSTR file)
{
    wmeta *m = meta_of(mf);
    if (!m || (file && !write_file(file, m->bits, m->size)))
        return 0;
    HMETAFILE h = mf_from_bits(m->bits, m->size);
    wmeta *c = meta_of(h);
    if (c)
        put16(c->bits, file ? 2 : 1);
    return h;
}

HGLOBAL WINAPI GetMetaFileBits(HMETAFILE mf)
{
    wmeta *m = meta_of(mf);
    HGLOBAL g = m ? GlobalAlloc(GMEM_MOVEABLE, m->size) : 0;
    uint8_t *p = g ? (uint8_t *)GlobalLock(g) : NULL;
    if (!p) {
        if (g)
            GlobalFree(g);
        return 0;
    }
    memcpy(p, m->bits, m->size);
    GlobalUnlock(g);
    DeleteMetaFile(mf);
    return g;
}

HMETAFILE WINAPI SetMetaFileBits(HGLOBAL bits)
{
    const uint8_t *p = (const uint8_t *)GlobalLock(bits);
    HMETAFILE h = p ? mf_from_bits(p, GlobalSize(bits)) : 0;
    if (p)
        GlobalUnlock(bits);
    if (h)
        GlobalFree(bits);
    return h;
}

HMETAFILE WINAPI SetMetaFileBitsBetter(HGLOBAL bits)
{
    return SetMetaFileBits(bits);
}

/* ---------------------------------------------------------------- playing */

BOOL mf_enum(HDC h, HMETAFILE mf, mf_enum_fn fn, void *ctx)
{
    wmeta *m = meta_of(mf);
    if (!m || !gdi_dc(h) || !valid(m->bits, m->size))
        return FALSE;
    int n = (int)get16(m->bits + 10);
    HGDIOBJ *table = (HGDIOBJ *)win_alloc(sizeof(HGDIOBJ) * (size_t)(n ? n : 1));
    if (!table)
        return FALSE;
    int level = SaveDC(h);
    BOOL ok = TRUE;
    uint32_t at = get16(m->bits + 2) * 2u;
    while (at + 6 <= m->size) {
        uint32_t words = get32(m->bits + at);
        if (words < 3 || words > (m->size - at) / 2 || !get16(m->bits + at + 4))
            break;
        if (!fn(ctx, h, table, n, m->bits + at, words * 2)) {
            ok = FALSE;
            break;
        }
        m = meta_of(mf);                            /* the procedure may delete it */
        if (!m)
            break;
        at += words * 2;
    }
    if (level)
        RestoreDC(h, level);
    for (int i = 0; i < n; i++)
        if (table[i])
            DeleteObject(table[i]);
    win_free(table);
    return ok;
}

static int play_one(void *ctx, HDC h, HGDIOBJ *table, int n, const uint8_t *rec, uint32_t bytes)
{
    (void)ctx;
    mf_play_record(h, table, n, rec, bytes);
    return 1;
}

BOOL WINAPI PlayMetaFile(HDC h, HMETAFILE mf)
{
    return mf_enum(h, mf, play_one, NULL);
}

typedef struct {
    MFENUMPROC proc;
    LPARAM lp;
    uint8_t *copy;                  /* each record aligned, as the structure is */
    uint32_t cap;
} enum_ctx;

static int enum_one(void *ctx, HDC h, HGDIOBJ *table, int n, const uint8_t *rec, uint32_t bytes)
{
    enum_ctx *e = (enum_ctx *)ctx;
    if (bytes > e->cap) {
        uint8_t *c = (uint8_t *)win_realloc(e->copy, bytes);
        if (!c)
            return 0;
        e->copy = c;
        e->cap = bytes;
    }
    memcpy(e->copy, rec, bytes);
    return e->proc(h, (HANDLETABLE *)table, (METARECORD *)e->copy, n, e->lp);
}

BOOL WINAPI EnumMetaFile(HDC h, HMETAFILE mf, MFENUMPROC proc, LPARAM lp)
{
    if (!proc)
        return FALSE;
    enum_ctx e = { proc, lp, NULL, 0 };
    BOOL r = mf_enum(h, mf, enum_one, &e);
    win_free(e.copy);
    return r;
}

void WINAPI PlayMetaFileRecord(HDC h, HANDLETABLE *ht, METARECORD *mr, UINT n)
{
    if (ht && mr && mr->rdSize >= 3)
        mf_play_record(h, ht->objectHandle, (int)n, (const uint8_t *)mr, mr->rdSize * 2);
}

/* the record's arguments: word i (0 past its end), signed */
typedef struct {
    const uint8_t *p;
    uint32_t n;                     /* words */
} margs;

static unsigned P(const margs *a, uint32_t i) { return i < a->n ? get16(a->p + 2 * i) : 0; }
static int S(const margs *a, uint32_t i) { return (int16_t)P(a, i); }
static DWORD D(const margs *a, uint32_t i) { return P(a, i) | (DWORD)P(a, i + 1) << 16; }

static HGDIOBJ object_at(HGDIOBJ *table, int n, unsigned i)
{
    return (int)i < n ? table[i] : 0;
}

/* a made object into the first free slot; one that could not be made
 * holds its slot all the same */
static void keep(HGDIOBJ *table, int n, HGDIOBJ obj)
{
    for (int i = 0; i < n; i++)
        if (!table[i]) {
            table[i] = obj ? obj : GetStockObject(NULL_BRUSH);
            return;
        }
    if (obj)
        DeleteObject(obj);
}

/* the points from word at on (count of them) */
static POINT *points(const margs *a, uint32_t at, uint32_t count)
{
    if (!count || at + 2 * count > a->n)
        return NULL;
    POINT *pt = (POINT *)win_alloc(sizeof(POINT) * count);
    for (uint32_t i = 0; pt && i < count; i++) {
        pt[i].x = S(a, at + 2 * i);
        pt[i].y = S(a, at + 2 * i + 1);
    }
    return pt;
}

/* the DIB from word at on: a copy, aligned; its bits after its header */
static uint8_t *dib_at(const margs *a, uint32_t at, UINT usage, const BITMAPINFO **bi,
                       const void **bits)
{
    if (at + 20 > a->n)
        return NULL;
    uint32_t len = (a->n - at) * 2;
    uint8_t *d = (uint8_t *)win_alloc(len);
    if (!d)
        return NULL;
    memcpy(d, a->p + 2 * at, len);
    *bi = (const BITMAPINFO *)d;
    uint32_t head = dib_head(*bi, usage);
    if (head > len) {
        win_free(d);
        return NULL;
    }
    *bits = d + head;
    return d;
}

static HRGN region_of(const margs *a)
{
    HRGN r = CreateRectRgn(0, 0, 0, 0);
    unsigned scans = P(a, 5);
    uint32_t k = 11;
    for (unsigned s = 0; r && s < scans && k + 3 <= a->n; s++) {
        unsigned cnt = P(a, k);
        int top = S(a, k + 1), bottom = S(a, k + 2);
        for (unsigned i = 0; i + 1 < cnt; i += 2) {
            HRGN b = CreateRectRgn(S(a, k + 3 + i), top, S(a, k + 4 + i), bottom);
            if (b) {
                CombineRgn(r, r, b, RGN_OR);
                DeleteObject(b);
            }
        }
        k += 4 + cnt;
    }
    return r;
}

static HFONT font_of(const margs *a)
{
    if (a->n < 9)
        return 0;
    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = S(a, 0);
    lf.lfWidth = S(a, 1);
    lf.lfEscapement = S(a, 2);
    lf.lfOrientation = S(a, 3);
    lf.lfWeight = S(a, 4);
    const uint8_t *b = a->p + 10;
    lf.lfItalic = b[0]; lf.lfUnderline = b[1]; lf.lfStrikeOut = b[2]; lf.lfCharSet = b[3];
    lf.lfOutPrecision = b[4]; lf.lfClipPrecision = b[5]; lf.lfQuality = b[6];
    lf.lfPitchAndFamily = b[7];
    uint32_t room = a->n * 2 > 18 ? a->n * 2 - 18 : 0;
    if (room > LF_FACESIZE - 1)
        room = LF_FACESIZE - 1;
    memcpy(lf.lfFaceName, a->p + 18, room);
    return CreateFontIndirect(&lf);
}

static HPALETTE palette_of(const margs *a)
{
    unsigned n = P(a, 1);
    if (!n || n > 256 || 2 + 2 * n > a->n)
        return 0;
    LOGPALETTE *lp = (LOGPALETTE *)win_alloc(sizeof(LOGPALETTE) + 4 * n);
    if (!lp)
        return 0;
    lp->palVersion = 0x300;
    lp->palNumEntries = (WORD)n;
    memcpy(lp->palPalEntry, a->p + 4, 4 * n);
    HPALETTE p = CreatePalette(lp);
    win_free(lp);
    return p;
}

static HBRUSH pattern_of(const margs *a)
{
    if (a->n < 2 + 20)
        return 0;
    uint32_t len = (a->n - 2) * 2;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, len);
    uint8_t *p = g ? (uint8_t *)GlobalLock(g) : NULL;
    HBRUSH b = 0;
    if (p) {
        memcpy(p, a->p + 4, len);
        GlobalUnlock(g);
        b = CreateDIBPatternBrush(g, P(a, 1));
    }
    if (g)
        GlobalFree(g);
    return b;
}

static void play_text(HDC h, const margs *a, int ext)
{
    if (!ext) {
        unsigned n = P(a, 0), k = (n + 1) / 2;
        if (1 + k + 2 <= a->n)
            TextOut(h, S(a, 2 + k), S(a, 1 + k), (LPCSTR)(a->p + 2), (int)n);
        return;
    }
    unsigned n = P(a, 2), opts = P(a, 3), at = 4, k = (n + 1) / 2;
    RECT rc, *prc = NULL;
    if (opts & (ETO_OPAQUE | ETO_CLIPPED)) {
        SetRect(&rc, S(a, 4), S(a, 5), S(a, 6), S(a, 7));
        prc = &rc;
        at = 8;
    }
    if (at + k > a->n)
        return;
    int *dx = NULL;
    if (n && at + k + n <= a->n && (dx = (int *)win_alloc(sizeof(int) * n)) != NULL)
        for (unsigned i = 0; i < n; i++)
            dx[i] = S(a, at + k + i);
    ExtTextOut(h, S(a, 1), S(a, 0), opts, prc, (LPCSTR)(a->p + 2 * at), n, dx);
    win_free(dx);
}

static void play_poly(HDC h, const margs *a, int fn)
{
    if (fn != META_POLYPOLYGON) {
        POINT *pt = points(a, 1, P(a, 0));
        if (pt) {
            if (fn == META_POLYGON)
                Polygon(h, pt, (int)P(a, 0));
            else
                Polyline(h, pt, (int)P(a, 0));
        }
        win_free(pt);
        return;
    }
    unsigned np = P(a, 0);
    uint32_t total = 0;
    if (!np || 1 + np > a->n)
        return;
    int *counts = (int *)win_alloc(sizeof(int) * np);
    for (unsigned i = 0; counts && i < np; i++)
        total += (uint32_t)(counts[i] = (int)P(a, 1 + i));
    POINT *pt = counts ? points(a, 1 + np, total) : NULL;
    if (pt)
        PolyPolygon(h, pt, counts, (int)np);
    win_free(pt);
    win_free(counts);
}

/* the DIB blits: DIBBITBLT, DIBSTRETCHBLT, STRETCHDIB, SETDIBTODEV */
static void play_dib(HDC h, const margs *a, int fn)
{
    const BITMAPINFO *bi;
    const void *bits;
    uint8_t *d;
    switch (fn) {
    case META_DIBBITBLT:
        if (a->n <= 9) {                            /* no source: a pattern */
            PatBlt(h, S(a, 8), S(a, 7), S(a, 6), S(a, 5), D(a, 0));
            return;
        }
        if ((d = dib_at(a, 8, DIB_RGB_COLORS, &bi, &bits)) != NULL)
            StretchDIBits(h, S(a, 7), S(a, 6), S(a, 5), S(a, 4), S(a, 3), S(a, 2), S(a, 5),
                          S(a, 4), bits, bi, DIB_RGB_COLORS, D(a, 0));
        break;
    case META_DIBSTRETCHBLT:
        if (a->n <= 11) {
            PatBlt(h, S(a, 9), S(a, 8), S(a, 7), S(a, 6), D(a, 0));
            return;
        }
        if ((d = dib_at(a, 10, DIB_RGB_COLORS, &bi, &bits)) != NULL)
            StretchDIBits(h, S(a, 9), S(a, 8), S(a, 7), S(a, 6), S(a, 5), S(a, 4), S(a, 3),
                          S(a, 2), bits, bi, DIB_RGB_COLORS, D(a, 0));
        break;
    case META_STRETCHDIB:
        if ((d = dib_at(a, 11, P(a, 2), &bi, &bits)) != NULL)
            StretchDIBits(h, S(a, 10), S(a, 9), S(a, 8), S(a, 7), S(a, 6), S(a, 5), S(a, 4),
                          S(a, 3), bits, bi, P(a, 2), D(a, 0));
        break;
    default:                                        /* SETDIBTODEV */
        if ((d = dib_at(a, 9, P(a, 0), &bi, &bits)) != NULL)
            SetDIBitsToDevice(h, S(a, 8), S(a, 7), S(a, 6), S(a, 5), S(a, 4), S(a, 3), P(a, 2),
                              P(a, 1), bits, bi, P(a, 0));
        break;
    }
    win_free(d);
}

void mf_play_record(HDC h, HGDIOBJ *table, int n, const uint8_t *rec, uint32_t bytes)
{
    if (bytes < 6)
        return;
    int fn = (int)get16(rec + 4);
    margs a = { rec + 6, (bytes - 6) / 2 };
    const margs *m = &a;
    switch (fn) {
    case META_SETBKCOLOR: SetBkColor(h, D(m, 0)); break;
    case META_SETTEXTCOLOR: SetTextColor(h, D(m, 0)); break;
    case META_SETBKMODE: SetBkMode(h, S(m, 0)); break;
    case META_SETMAPMODE: SetMapMode(h, S(m, 0)); break;
    case META_SETROP2: SetROP2(h, S(m, 0)); break;
    case META_SETPOLYFILLMODE: SetPolyFillMode(h, S(m, 0)); break;
    case META_SETSTRETCHBLTMODE: SetStretchBltMode(h, S(m, 0)); break;
    case META_SETTEXTCHAREXTRA: SetTextCharacterExtra(h, S(m, 0)); break;
    case META_SETTEXTJUSTIFICATION: SetTextJustification(h, S(m, 1), S(m, 0)); break;
    case META_SETTEXTALIGN: SetTextAlign(h, P(m, 0)); break;
    case META_SETWINDOWORG: SetWindowOrgEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_SETWINDOWEXT: SetWindowExtEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_SETVIEWPORTORG: SetViewportOrgEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_SETVIEWPORTEXT: SetViewportExtEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_OFFSETWINDOWORG: OffsetWindowOrgEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_OFFSETVIEWPORTORG: OffsetViewportOrgEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_SCALEWINDOWEXT:
        ScaleWindowExtEx(h, S(m, 3), S(m, 2), S(m, 1), S(m, 0), NULL);
        break;
    case META_SCALEVIEWPORTEXT:
        ScaleViewportExtEx(h, S(m, 3), S(m, 2), S(m, 1), S(m, 0), NULL);
        break;
    case META_LINETO: LineTo(h, S(m, 1), S(m, 0)); break;
    case META_MOVETO: MoveToEx(h, S(m, 1), S(m, 0), NULL); break;
    case META_EXCLUDECLIPRECT: ExcludeClipRect(h, S(m, 3), S(m, 2), S(m, 1), S(m, 0)); break;
    case META_INTERSECTCLIPRECT:
        IntersectClipRect(h, S(m, 3), S(m, 2), S(m, 1), S(m, 0));
        break;
    case META_OFFSETCLIPRGN: OffsetClipRgn(h, S(m, 1), S(m, 0)); break;
    case META_ARC: case META_PIE: case META_CHORD: {
        BOOL (WINAPI *f)(HDC, int, int, int, int, int, int, int, int) =
            fn == META_ARC ? Arc : fn == META_PIE ? Pie : Chord;
        f(h, S(m, 7), S(m, 6), S(m, 5), S(m, 4), S(m, 3), S(m, 2), S(m, 1), S(m, 0));
        break;
    }
    case META_ELLIPSE: Ellipse(h, S(m, 3), S(m, 2), S(m, 1), S(m, 0)); break;
    case META_RECTANGLE: Rectangle(h, S(m, 3), S(m, 2), S(m, 1), S(m, 0)); break;
    case META_ROUNDRECT:
        RoundRect(h, S(m, 5), S(m, 4), S(m, 3), S(m, 2), S(m, 1), S(m, 0));
        break;
    case META_PATBLT: PatBlt(h, S(m, 5), S(m, 4), S(m, 3), S(m, 2), D(m, 0)); break;
    case META_SETPIXEL: SetPixel(h, S(m, 3), S(m, 2), D(m, 0)); break;
    case META_FLOODFILL: FloodFill(h, S(m, 3), S(m, 2), D(m, 0)); break;
    case META_EXTFLOODFILL: ExtFloodFill(h, S(m, 4), S(m, 3), D(m, 1), P(m, 0)); break;
    case META_SAVEDC: SaveDC(h); break;
    case META_RESTOREDC: RestoreDC(h, S(m, 0)); break;
    case META_TEXTOUT: play_text(h, m, 0); break;
    case META_EXTTEXTOUT: play_text(h, m, 1); break;
    case META_POLYGON: case META_POLYLINE: case META_POLYPOLYGON: play_poly(h, m, fn); break;
    case META_DIBBITBLT: case META_DIBSTRETCHBLT: case META_STRETCHDIB: case META_SETDIBTODEV:
        play_dib(h, m, fn);
        break;
    case META_FILLREGION:
        FillRgn(h, object_at(table, n, P(m, 0)), object_at(table, n, P(m, 1)));
        break;
    case META_FRAMEREGION:
        FrameRgn(h, object_at(table, n, P(m, 3)), object_at(table, n, P(m, 2)), S(m, 1), S(m, 0));
        break;
    case META_INVERTREGION: InvertRgn(h, object_at(table, n, P(m, 0))); break;
    case META_PAINTREGION: PaintRgn(h, object_at(table, n, P(m, 0))); break;
    case META_SELECTCLIPREGION: SelectClipRgn(h, object_at(table, n, P(m, 0))); break;
    case META_SELECTOBJECT: {
        HGDIOBJ o = object_at(table, n, P(m, 0));
        if (o)
            SelectObject(h, o);
        break;
    }
    case META_SELECTPALETTE: {                      /* one word, or (some) force and it */
        HGDIOBJ o = object_at(table, n, P(m, a.n > 1 ? 1 : 0));
        if (o)
            SelectPalette(h, o, FALSE);
        break;
    }
    case META_REALIZEPALETTE: RealizePalette(h); break;
    case META_DELETEOBJECT:
        if ((int)P(m, 0) < n && table[P(m, 0)]) {
            DeleteObject(table[P(m, 0)]);
            table[P(m, 0)] = 0;
        }
        break;
    case META_CREATEPENINDIRECT: {
        LOGPEN lp = { (UINT)P(m, 0), { S(m, 1), S(m, 2) }, D(m, 3) };
        keep(table, n, CreatePenIndirect(&lp));
        break;
    }
    case META_CREATEBRUSHINDIRECT: {
        LOGBRUSH lb = { (UINT)P(m, 0), D(m, 1), (int)P(m, 3) };
        keep(table, n, CreateBrushIndirect(&lb));
        break;
    }
    case META_CREATEFONTINDIRECT: keep(table, n, font_of(m)); break;
    case META_CREATEPALETTE: keep(table, n, palette_of(m)); break;
    case META_CREATEREGION: keep(table, n, region_of(m)); break;
    case META_DIBCREATEPATTERNBRUSH: keep(table, n, pattern_of(m)); break;
    case META_CREATEBRUSH: case META_CREATEPATTERNBRUSH: case META_CREATEBITMAP:
    case META_CREATEBITMAPINDIRECT:
        keep(table, n, 0);                          /* Windows 2's: their slots only */
        break;
    default:
        break;                                      /* escapes, palette changes, the rest */
    }
}
