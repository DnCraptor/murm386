/*
 * gdi.c - device contexts, GDI objects, palette and drawing primitives.
 *
 * The mapping modes: logical -> device is (logical - window org) scaled by
 * viewport ext / window ext, plus viewport org (MM_TEXT: no scale; the
 * metric and English ones and MM_TWIPS at 96 pixels an inch, y up;
 * MM_ISOTROPIC keeping the axes alike); device -> surface adds the DC
 * origin, which USER sets to the window position for window DCs. Clipping
 * regions are kept in surface coordinates.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

void (*gdi_overlay_hook)(HWND source, const RECT *damage);

#define DC_OR_RET(h, r) wdc *dc = gdi_dc(h); if (!dc) return r

/* ---------------------------------------------------------------- palette */

/* Windows 3.x static colours: 0-9 and 246-255, ~i is the inverse of i. */
static const uint8_t static_lo[10][3] = {
    { 0x00, 0x00, 0x00 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x80, 0x00 },
    { 0x80, 0x80, 0x00 }, { 0x00, 0x00, 0x80 }, { 0x80, 0x00, 0x80 },
    { 0x00, 0x80, 0x80 }, { 0xC0, 0xC0, 0xC0 }, { 0xC0, 0xDC, 0xC0 },
    { 0xA6, 0xCA, 0xF0 },
};
static const uint8_t static_hi[10][3] = {
    { 0xFF, 0xFB, 0xF0 }, { 0xA0, 0xA0, 0xA4 }, { 0x80, 0x80, 0x80 },
    { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0xFF, 0xFF, 0x00 },
    { 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF }, { 0x00, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF },
};

/* The free middle (10..245) starts as a 6x6x6 cube plus a grey ramp so
 * RGB() colours look reasonable before any application palette exists. */
static void syspal_default(void)
{
    PALETTEENTRY *p = win_sys.syspal;
    for (int i = 0; i < 10; i++) {
        p[i].peRed = static_lo[i][0]; p[i].peGreen = static_lo[i][1]; p[i].peBlue = static_lo[i][2];
        p[246 + i].peRed = static_hi[i][0]; p[246 + i].peGreen = static_hi[i][1];
        p[246 + i].peBlue = static_hi[i][2];
        p[i].peFlags = p[246 + i].peFlags = 0;
    }
    int k = 10;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++, k++) {
                p[k].peRed = (BYTE)(r * 51); p[k].peGreen = (BYTE)(g * 51);
                p[k].peBlue = (BYTE)(b * 51); p[k].peFlags = 0;
            }
    for (int i = 0; k < 246; i++, k++) {
        BYTE v = (BYTE)(8 + i * 12);
        p[k].peRed = p[k].peGreen = p[k].peBlue = v;
        p[k].peFlags = 0;
    }
}

static void syspal_upload(int first, int count)
{
    uint8_t rgb[3 * 256];
    if (!win_sys.disp || !win_sys.disp->set_palette)
        return;
    for (int i = 0; i < count; i++) {
        const PALETTEENTRY *e = &win_sys.syspal[first + i];
        rgb[3 * i] = e->peRed; rgb[3 * i + 1] = e->peGreen; rgb[3 * i + 2] = e->peBlue;
    }
    win_sys.disp->set_palette(first, count, rgb);
}

/* Small cache: the same few colours are asked for all the time. */
static struct { COLORREF c; uint8_t idx; uint8_t valid; } color_cache[16];

static void color_cache_flush(void) { memset(color_cache, 0, sizeof(color_cache)); }

static uint8_t nearest_index(COLORREF c)
{
    unsigned h = (unsigned)((c ^ (c >> 8) ^ (c >> 16)) & 15);
    if (color_cache[h].valid && color_cache[h].c == c)
        return color_cache[h].idx;
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    long best = 0x7FFFFFFF;
    int bi = 0;
    /* static colours first, so exact matches resolve to them and keep the
     * ~index = inverse property for XOR drawing */
    for (int k = 0; k < 256; k++) {
        int i = k < 10 ? 246 + k : k - 10;
        const PALETTEENTRY *e = &win_sys.syspal[i];
        long dr = e->peRed - r, dg = e->peGreen - g, db = e->peBlue - b;
        long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < best) {
            best = d;
            bi = i;
            if (!d)
                break;
        }
    }
    color_cache[h].c = c;
    color_cache[h].idx = (uint8_t)bi;
    color_cache[h].valid = 1;
    return (uint8_t)bi;
}

static wpalette *dc_palette(wdc *dc)
{
    return (wpalette *)win_handle_get(dc->pal ? dc->pal : win_sys.stock[DEFAULT_PALETTE], WOBJ_PALETTE);
}

uint8_t gdi_color_index(wdc *dc, COLORREF c)
{
    if (dc && dc->surf && dc->surf->bpp == 1) {
        if ((c >> 24) == 1) {           /* PALETTEINDEX: look the colour up */
            wpalette *p = dc_palette(dc);
            unsigned i = c & 0xFFFF;
            const PALETTEENTRY *e = (p && i < (unsigned)p->n) ? &p->e[i] : &win_sys.syspal[0];
            c = RGB(e->peRed, e->peGreen, e->peBlue);
        }
        return (GetRValue(c) + GetGValue(c) + GetBValue(c)) >= 384 ? 1 : 0;
    }
    if ((c >> 24) == 1) {
        wpalette *p = dc ? dc_palette(dc) : NULL;
        unsigned i = c & 0xFFFF;
        if (p && i < (unsigned)p->n)
            return p->map[i];
        return 0;
    }
    return nearest_index(c & 0xFFFFFF);
}

static COLORREF index_color(const wsurf *s, uint8_t i)
{
    if (s && s->bpp == 1)
        return i ? RGB(255, 255, 255) : RGB(0, 0, 0);
    const PALETTEENTRY *e = &win_sys.syspal[i];
    return RGB(e->peRed, e->peGreen, e->peBlue);
}

/* ---------------------------------------------------------------- objects */

static HGDIOBJ new_obj(int type, size_t size, void **out)
{
    void *o = win_alloc(size);
    if (!o)
        return 0;
    HGDIOBJ h = win_handle_alloc(type, o);
    if (!h) {
        win_free(o);
        return 0;
    }
    *out = o;
    return h;
}

HPEN WINAPI CreatePen(int style, int width, COLORREF color)
{
    wpen *p;
    HPEN h = new_obj(WOBJ_PEN, sizeof(*p), (void **)&p);
    if (!h)
        return 0;
    p->style = (style < PS_SOLID || style > PS_INSIDEFRAME) ? PS_SOLID : style;
    p->width = width < 1 ? 1 : width;
    p->color = color;
    if (p->width > 1 && p->style != PS_NULL && p->style != PS_INSIDEFRAME)
        p->style = PS_SOLID;            /* wide pens are always solid */
    return h;
}

HPEN WINAPI CreatePenIndirect(const LOGPEN *lp)
{
    return CreatePen((int)lp->lopnStyle, lp->lopnWidth.x, lp->lopnColor);
}

HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
    wbrush *b;
    HBRUSH h = new_obj(WOBJ_BRUSH, sizeof(*b), (void **)&b);
    if (!h)
        return 0;
    b->style = WBR_SOLID;
    b->color = color;
    return h;
}

HBRUSH WINAPI CreateHatchBrush(int style, COLORREF color)
{
    wbrush *b;
    HBRUSH h = new_obj(WOBJ_BRUSH, sizeof(*b), (void **)&b);
    if (!h)
        return 0;
    b->style = WBR_HATCH;
    b->color = color;
    b->hatch = (style < HS_HORIZONTAL || style > HS_DIAGCROSS) ? HS_HORIZONTAL : style;
    return h;
}

HBRUSH WINAPI CreatePatternBrush(HBITMAP hbmp)
{
    wbitmap *bm = (wbitmap *)win_handle_get(hbmp, WOBJ_BITMAP);
    if (!bm)
        return 0;
    wbrush *b;
    HBRUSH h = new_obj(WOBJ_BRUSH, sizeof(*b), (void **)&b);
    if (!h)
        return 0;
    b->style = WBR_PATTERN;
    b->pattern = hbmp;
    b->pat_is_mono = bm->s.bpp == 1;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t v = dib_get_pixel(&bm->s, x % (bm->s.width ? bm->s.width : 1),
                                      y % (bm->s.height ? bm->s.height : 1));
            b->pat8[y * 8 + x] = v;
            if (v)
                b->patmono[y] |= (uint8_t)(0x80 >> x);
        }
    return h;
}

HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH *lb)
{
    switch (lb->lbStyle) {
    case BS_NULL: return win_sys.stock[NULL_BRUSH];
    case BS_HATCHED: return CreateHatchBrush(lb->lbHatch, lb->lbColor);
    case BS_PATTERN: return CreatePatternBrush((HBITMAP)lb->lbHatch);
    default: return CreateSolidBrush(lb->lbColor);
    }
}

int gdi_bitmap_pitch(int w, int bpp)
{
    return bpp == 1 ? ((w + 15) >> 4) << 1 : (w + 1) & ~1;
}

static HBITMAP bitmap_new(int w, int h, int bpp)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    wbitmap *bm;
    HBITMAP hb = new_obj(WOBJ_BITMAP, sizeof(*bm), (void **)&bm);
    if (!hb)
        return 0;
    bm->s.width = w;
    bm->s.height = h;
    bm->s.bpp = bpp;
    bm->s.pitch = gdi_bitmap_pitch(w, bpp);
    bm->s.bits = (uint8_t *)win_alloc((size_t)bm->s.pitch * (size_t)h);
    if (!bm->s.bits) {
        win_handle_free(hb);
        win_free(bm);
        return 0;
    }
    return hb;
}

HBITMAP WINAPI CreateBitmap(int w, int h, UINT planes, UINT bpp, const void *bits)
{
    int depth = (planes * bpp == 1) ? 1 : 8;
    HBITMAP hb = bitmap_new(w, h, depth);
    if (hb && bits && planes * bpp == (UINT)depth) {
        wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
        memcpy(bm->s.bits, bits, (size_t)bm->s.pitch * (size_t)bm->s.height);
    }
    return hb;
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int w, int h)
{
    wdc *dc = gdi_dc(hdc);
    int bpp = dc && dc->surf ? dc->surf->bpp : 8;
    return bitmap_new(w, h, bpp);
}

/* ---------------------------------------------------------------- DIBs */

/* A device-independent bitmap as Win16 resources hold them: a
 * BITMAPCOREHEADER (12 bytes) or BITMAPINFOHEADER (40 or more, BI_RGB),
 * the palette, the rows bottom up, each a multiple of 4 bytes; 1, 4, 8 or
 * 24 bits a pixel. An icon's or a cursor's has twice its height (the XOR
 * image, then the AND mask): halves 2. */
int gdi_dib_parse(const uint8_t *d, uint32_t n, int halves, gdi_dib *out)
{
    memset(out, 0, sizeof(*out));
    if (!d || n < 12 || halves < 1)
        return 0;
    uint32_t hs = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
    int32_t w, h;
    int bpp, entry;
    uint32_t colors;
    if (hs == 12) {
        w = d[4] | d[5] << 8;
        h = d[6] | d[7] << 8;
        bpp = d[10] | d[11] << 8;
        colors = bpp <= 8 ? 1u << bpp : 0;
        entry = 3;
    } else if (hs >= 40 && hs <= n) {
        w = (int32_t)(d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24);
        h = (int32_t)(d[8] | d[9] << 8 | d[10] << 16 | (uint32_t)d[11] << 24);
        bpp = d[14] | d[15] << 8;
        uint32_t comp = d[16] | d[17] << 8 | d[18] << 16 | (uint32_t)d[19] << 24;
        uint32_t used = d[32] | d[33] << 8 | d[34] << 16 | (uint32_t)d[35] << 24;
        if (comp)
            return 0;                   /* RLE: not here */
        colors = bpp <= 8 ? (used && used < (1u << bpp) ? used : 1u << bpp) : 0;
        entry = 4;
    } else {
        return 0;
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24)
        return 0;
    int top_down = h < 0;
    if (top_down)
        h = -h;
    h /= halves;
    if (w < 1 || h < 1 || w > 4096 || h > 4096)
        return 0;
    out->w = w;
    out->h = h;
    out->bpp = bpp;
    out->top_down = top_down;
    out->entry = entry;
    out->colors = (int)colors;
    out->pal = d + hs;
    out->pitch = (int)(((uint32_t)w * (uint32_t)bpp + 31u) / 32u * 4u);
    out->bits = out->pal + colors * (uint32_t)entry;
    /* the image, then (an icon's, a cursor's) the mask: 1 bit a pixel */
    uint64_t need = (uint64_t)(out->bits - d) + (uint64_t)out->pitch * (uint64_t)h +
                    (halves > 1 ? ((uint64_t)w + 31u) / 32u * 4u * (uint64_t)h : 0);
    if (need > n)
        return 0;
    return 1;
}

/* the raw value (a palette index, or 0xRRGGBB for 24 bits) of the pixel at
 * x, y (y from the top) of the half (0 the XOR image, 1 the AND mask) */
uint32_t gdi_dib_value(const gdi_dib *b, int half, int x, int y)
{
    int bpp = half ? 1 : b->bpp;
    int pitch = half ? (int)(((uint32_t)b->w + 31u) / 32u * 4u) : b->pitch;
    const uint8_t *base = half ? b->bits + (size_t)b->pitch * (size_t)b->h : b->bits;
    int row = b->top_down ? y : b->h - 1 - y;
    const uint8_t *r = base + (size_t)row * (size_t)pitch;
    switch (bpp) {
    case 1: return (r[x >> 3] >> (7 - (x & 7))) & 1;
    case 4: return (r[x >> 1] >> (x & 1 ? 0 : 4)) & 15;
    case 8: return r[x];
    default: return (uint32_t)r[3 * x + 2] << 16 | (uint32_t)r[3 * x + 1] << 8 | r[3 * x];
    }
}

/* the colour of the XOR image's pixel */
COLORREF gdi_dib_pixel(const gdi_dib *b, int x, int y)
{
    uint32_t v = gdi_dib_value(b, 0, x, y);
    if (b->bpp == 24)
        return RGB(v >> 16, (v >> 8) & 255, v & 255);
    if ((int)v >= b->colors)
        return 0;
    const uint8_t *e = b->pal + v * (uint32_t)b->entry;
    return RGB(e[2], e[1], e[0]);
}

/* a bitmap's row y from the DIB's row sy (from the top): monochrome, or
 * the screen's palette indices */
static void dib_row_to(const gdi_dib *b, int sy, wsurf *s, int y)
{
    uint8_t *row = s->bits + (size_t)y * (size_t)s->pitch;
    if (s->bpp == 1)
        memset(row, 0, (size_t)s->pitch);
    for (int x = 0; x < b->w && x < s->width; x++) {
        COLORREF c = gdi_dib_pixel(b, x, sy);
        if (s->bpp != 1)
            row[x] = nearest_index(c);
        else if (GetRValue(c) + GetGValue(c) + GetBValue(c) >= 384)
            row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
    }
}

/* a bitmap of a parsed DIB: monochrome (mono), or of palette indices */
static HBITMAP bitmap_of_dib(const gdi_dib *b, int mono)
{
    HBITMAP hb = bitmap_new(b->w, b->h, mono ? 1 : 8);
    wbitmap *bm = hb ? (wbitmap *)win_handle_get(hb, WOBJ_BITMAP) : NULL;
    if (!bm)
        return 0;
    for (int y = 0; y < b->h; y++)
        dib_row_to(b, y, &bm->s, y);
    return hb;
}

/* A bitmap from a DIB (LoadBitmap): monochrome from a 1-bit one, else of
 * the screen's palette indices */
HBITMAP gdi_bitmap_from_dib(const uint8_t *d, uint32_t n)
{
    gdi_dib b;
    if (!gdi_dib_parse(d, n, 1, &b))
        return 0;
    return bitmap_of_dib(&b, b.bpp == 1);
}

/*
 * A DIB as the API has it: a header (BITMAPINFO or BITMAPCOREINFO, its
 * colours after it) and the bits apart, lines rows of them (0: all).
 * DIB_PAL_COLORS: the colours are indices into the DC's palette, made
 * colours in tmp (256 RGBQUADs).
 */
static int dib_of(wdc *dc, const BITMAPINFO *bi, const void *bits, UINT usage, int lines,
                  gdi_dib *b, RGBQUAD *tmp)
{
    if (!bi || !bits || !gdi_dib_parse((const uint8_t *)bi, 0x7fffffffu, 1, b))
        return 0;
    if (usage == DIB_PAL_COLORS && b->colors) {
        wpalette *p = dc ? dc_palette(dc) : NULL;
        const uint8_t *ix = (const uint8_t *)bi + bi->bmiHeader.biSize;
        for (int i = 0; i < b->colors && i < 256; i++) {
            int k = ix[2 * i] | ix[2 * i + 1] << 8;
            const PALETTEENTRY *e = p && k < p->n ? &p->e[k] : NULL;
            COLORREF c = e ? RGB(e->peRed, e->peGreen, e->peBlue)
                           : index_color(&win_sys.screen, (uint8_t)k);
            tmp[i].rgbRed = GetRValue(c);
            tmp[i].rgbGreen = GetGValue(c);
            tmp[i].rgbBlue = GetBValue(c);
            tmp[i].rgbReserved = 0;
        }
        b->pal = (const uint8_t *)tmp;
        b->entry = 4;
    }
    b->bits = (const uint8_t *)bits;
    if (lines > 0 && lines < b->h)
        b->h = lines;
    return 1;
}

/* a DIB's rows (a band of them) drawn: a bitmap of them, blitted */
static int dib_draw(HDC h, wdc *dc, const gdi_dib *b, int x, int y, int w, int hh, int sx,
                    int sy, int sw, int sh, DWORD rop, int stretch)
{
    HBITMAP hb = bitmap_of_dib(b, dc->surf->bpp == 1);
    HDC mem = hb ? CreateCompatibleDC(h) : 0;
    if (!mem) {
        if (hb)
            DeleteObject(hb);
        return 0;
    }
    HGDIOBJ old = SelectObject(mem, hb);
    if (stretch)
        StretchBlt(h, x, y, w, hh, mem, sx, sy, sw, sh, rop);
    else
        BitBlt(h, x, y, w, hh, mem, sx, sy, rop);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(hb);
    return 1;
}

/* the source's y from the bottom (as a DIB counts, the bits of a band
 * starting at row start) made from the top of those rows */
static int dib_top(const gdi_dib *b, int start, int sy, int sh)
{
    return b->top_down ? sy - start : b->h - (sy - start) - sh;
}

int WINAPI SetDIBitsToDevice(HDC h, int x, int y, int w, int hh, int sx, int sy,
                             UINT start, UINT lines, const void *bits, const BITMAPINFO *bi,
                             UINT usage)
{
    DC_OR_RET(h, 0);
    RGBQUAD *tmp = (RGBQUAD *)win_alloc(256 * sizeof(RGBQUAD));
    gdi_dib b;
    int r = tmp && lines && dib_of(dc, bi, bits, usage, (int)lines, &b, tmp) &&
            dib_draw(h, dc, &b, x, y, w, hh, sx, dib_top(&b, (int)start, sy, hh), w, hh,
                     SRCCOPY, 0) ? (int)lines : 0;
    win_free(tmp);
    return r;
}

int WINAPI StretchDIBits(HDC h, int x, int y, int w, int hh, int sx, int sy, int sw, int sh,
                         const void *bits, const BITMAPINFO *bi, UINT usage, DWORD rop)
{
    DC_OR_RET(h, 0);
    RGBQUAD *tmp = (RGBQUAD *)win_alloc(256 * sizeof(RGBQUAD));
    gdi_dib b;
    int r = tmp && dib_of(dc, bi, bits, usage, 0, &b, tmp) &&
            dib_draw(h, dc, &b, x, y, w, hh, sx, dib_top(&b, 0, sy, sh), sw, sh, rop, 1) ? sh : 0;
    win_free(tmp);
    return r;
}

/* rows of a DIB into a bitmap: the band's from row start (from the
 * bottom); the number set */
int WINAPI SetDIBits(HDC h, HBITMAP hb, UINT start, UINT lines, const void *bits,
                     const BITMAPINFO *bi, UINT usage)
{
    wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
    RGBQUAD *tmp = (RGBQUAD *)win_alloc(256 * sizeof(RGBQUAD));
    gdi_dib b;
    if (!tmp || !bm || !lines || !dib_of(gdi_dc(h), bi, bits, usage, (int)lines, &b, tmp)) {
        win_free(tmp);
        return 0;
    }
    int n = 0;
    for (int r = 0; r < b.h; r++) {
        int y = b.top_down ? (int)start + r : bm->s.height - 1 - ((int)start + (b.h - 1 - r));
        if (y < 0 || y >= bm->s.height)
            continue;
        dib_row_to(&b, r, &bm->s, y);
        n++;
    }
    win_free(tmp);
    return n;
}

/* a bitmap the DC's kind (monochrome for a monochrome one), its bits from
 * the DIB if CBM_INIT */
HBITMAP WINAPI CreateDIBitmap(HDC h, const BITMAPINFOHEADER *bih, DWORD init, const void *bits,
                              const BITMAPINFO *bi, UINT usage)
{
    if (!bih)
        return 0;
    int w, hh;
    if (bih->biSize == sizeof(BITMAPCOREHEADER)) {
        const BITMAPCOREHEADER *c = (const BITMAPCOREHEADER *)bih;
        w = c->bcWidth;
        hh = c->bcHeight;
    } else {
        w = (int)bih->biWidth;
        hh = (int)(bih->biHeight < 0 ? -bih->biHeight : bih->biHeight);
    }
    wdc *dc = gdi_dc(h);
    HBITMAP hb = bitmap_new(w, hh, dc && dc->surf->bpp == 1 ? 1 : 8);
    wbitmap *bm = hb ? (wbitmap *)win_handle_get(hb, WOBJ_BITMAP) : NULL;
    if (!bm)
        return 0;
    memset(bm->s.bits, 0, (size_t)bm->s.pitch * (size_t)bm->s.height);
    if ((init & CBM_INIT) && bits && bi)
        SetDIBits(h, hb, 0, (UINT)hh, bits, bi, usage);
    return hb;
}

/* the 16 colours a 4-bit DIB of GetDIBits has */
static const COLORREF dib16[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255),
};

static int nearest_of(const COLORREF *t, int n, COLORREF c)
{
    long best = 0x7fffffff;
    int bi = 0;
    for (int i = 0; i < n; i++) {
        long dr = (long)GetRValue(c) - GetRValue(t[i]), dg = (long)GetGValue(c) - GetGValue(t[i]),
             db = (long)GetBValue(c) - GetBValue(t[i]);
        long d = dr * dr + dg * dg + db * db;
        if (d < best) {
            best = d;
            bi = i;
        }
    }
    return bi;
}

/*
 * A bitmap's rows as a DIB of the header's format (1, 4, 8 or 24 bits; 0:
 * the header filled in, the bitmap's own), the colours after the header;
 * the band's rows from row start (from the bottom); the number got.
 */
int WINAPI GetDIBits(HDC h, HBITMAP hb, UINT start, UINT lines, void *bits, BITMAPINFO *bi,
                     UINT usage)
{
    (void)usage;
    wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
    if (!bm || !bi || bi->bmiHeader.biSize < sizeof(BITMAPINFOHEADER))
        return 0;
    (void)h;
    BITMAPINFOHEADER *hd = &bi->bmiHeader;
    if (!hd->biBitCount) {
        hd->biWidth = bm->s.width;
        hd->biHeight = bm->s.height;
        hd->biPlanes = 1;
        hd->biBitCount = (WORD)(bm->s.bpp == 1 ? 1 : 8);
        hd->biCompression = BI_RGB;
        hd->biSizeImage = (DWORD)((bm->s.width * hd->biBitCount + 31) / 32 * 4) *
                          (DWORD)bm->s.height;
        hd->biClrUsed = hd->biClrImportant = 0;
        return 1;
    }
    int bpp = hd->biBitCount;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24)
        return 0;
    int w = bm->s.width, hh = bm->s.height, pitch = (w * bpp + 31) / 32 * 4;
    hd->biWidth = w;
    hd->biHeight = hh;
    hd->biPlanes = 1;
    hd->biCompression = BI_RGB;
    hd->biSizeImage = (DWORD)pitch * (DWORD)hh;
    hd->biClrUsed = hd->biClrImportant = 0;
    int ncol = bpp == 24 ? 0 : 1 << bpp;
    RGBQUAD *col = (RGBQUAD *)((uint8_t *)bi + hd->biSize);
    for (int i = 0; i < ncol; i++) {
        COLORREF c = bpp == 1 ? (i ? RGB(255, 255, 255) : 0)
                     : bpp == 4 ? dib16[i] : index_color(&win_sys.screen, (uint8_t)i);
        col[i].rgbRed = GetRValue(c);
        col[i].rgbGreen = GetGValue(c);
        col[i].rgbBlue = GetBValue(c);
        col[i].rgbReserved = 0;
    }
    if (!bits)
        return hh;
    int n = 0;
    for (UINT r = 0; r < lines; r++) {
        int y = hh - 1 - (int)(start + r);     /* bottom up */
        if (y < 0 || y >= hh)
            break;
        uint8_t *out = (uint8_t *)bits + (size_t)r * (size_t)pitch;
        memset(out, 0, (size_t)pitch);
        for (int x = 0; x < w; x++) {
            uint8_t v = dib_get_pixel(&bm->s, x, y);
            COLORREF c = index_color(&bm->s, v);
            switch (bpp) {
            case 1:
                if (GetRValue(c) + GetGValue(c) + GetBValue(c) >= 384)
                    out[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
                break;
            case 4:
                out[x >> 1] |= (uint8_t)(nearest_of(dib16, 16, c) << (x & 1 ? 0 : 4));
                break;
            case 8:
                out[x] = bm->s.bpp == 8 ? v : nearest_index(c);
                break;
            default:
                out[3 * x] = GetBValue(c);
                out[3 * x + 1] = GetGValue(c);
                out[3 * x + 2] = GetRValue(c);
                break;
            }
        }
        n++;
    }
    return n;
}

/* a pattern brush of a packed DIB (a global block's) */
HBRUSH WINAPI CreateDIBPatternBrush(HGLOBAL hg, UINT usage)
{
    const BITMAPINFO *bi = (const BITMAPINFO *)GlobalLock(hg);
    RGBQUAD *tmp = (RGBQUAD *)win_alloc(256 * sizeof(RGBQUAD));
    gdi_dib b;
    HBRUSH br = 0;
    /* the bits after the colours: RGBQUADs, or (DIB_PAL_COLORS) indices */
    if (tmp && bi && gdi_dib_parse((const uint8_t *)bi, GlobalSize(hg), 1, &b) &&
        dib_of(NULL, bi, usage == DIB_PAL_COLORS ? (const uint8_t *)bi + bi->bmiHeader.biSize +
                                                   2u * (uint32_t)b.colors : b.bits,
               usage, 0, &b, tmp)) {
        HBITMAP hb = bitmap_of_dib(&b, 0);
        if (hb) {
            br = CreatePatternBrush(hb);
            wbrush *w = br ? (wbrush *)win_handle_get(br, WOBJ_BRUSH) : NULL;
            if (w)
                w->pattern = 0;         /* its own copy of the pattern */
            DeleteObject(hb);
        }
    }
    if (bi)
        GlobalUnlock(hg);
    win_free(tmp);
    return br;
}

LONG WINAPI SetBitmapBits(HBITMAP hb, DWORD count, const void *bits)
{
    wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
    if (!bm || !bits)
        return 0;
    DWORD size = (DWORD)bm->s.pitch * (DWORD)bm->s.height;
    if (count > size)
        count = size;
    memcpy(bm->s.bits, bits, count);
    return (LONG)count;
}

LONG WINAPI GetBitmapBits(HBITMAP hb, LONG count, LPVOID bits)
{
    wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
    if (!bm || !bits || count <= 0)
        return 0;
    LONG size = (LONG)bm->s.pitch * bm->s.height;
    if (count > size)
        count = size;
    memcpy(bits, bm->s.bits, (size_t)count);
    return count;
}

/* ---------------------------------------------------------------- regions */

wrgn *gdi_region(HRGN h) { return (wrgn *)win_handle_get(h, WOBJ_REGION); }

HRGN WINAPI CreateRectRgn(int l, int t, int r, int b)
{
    wrgn *g;
    HRGN h = new_obj(WOBJ_REGION, sizeof(*g), (void **)&g);
    if (!h)
        return 0;
    RECT rc = { l, t, r, b };
    if (rc.left > rc.right) { int x = rc.left; rc.left = rc.right; rc.right = x; }
    if (rc.top > rc.bottom) { int x = rc.top; rc.top = rc.bottom; rc.bottom = x; }
    wrgn_set_rect(g, &rc);
    return h;
}

HRGN WINAPI CreateRectRgnIndirect(const RECT *rc)
{
    return CreateRectRgn(rc->left, rc->top, rc->right, rc->bottom);
}

void WINAPI SetRectRgn(HRGN h, int l, int t, int r, int b)
{
    wrgn *g = gdi_region(h);
    RECT rc = { l, t, r, b };
    if (g)
        wrgn_set_rect(g, &rc);
}

int WINAPI CombineRgn(HRGN hd, HRGN ha, HRGN hb, int mode)
{
    wrgn *d = gdi_region(hd), *a = gdi_region(ha), *b = gdi_region(hb);
    if (!d || !a || (mode != RGN_COPY && !b))
        return ERROR;
    switch (mode) {
    case RGN_AND:  wrgn_and(d, a, b); break;
    case RGN_OR:   wrgn_or(d, a, b); break;
    case RGN_XOR:  wrgn_xor(d, a, b); break;
    case RGN_DIFF: wrgn_diff(d, a, b); break;
    case RGN_COPY: wrgn_copy(d, a); break;
    default: return ERROR;
    }
    return wrgn_type(d);
}

BOOL WINAPI EqualRgn(HRGN a, HRGN b)
{
    wrgn *ga = gdi_region(a), *gb = gdi_region(b);
    return ga && gb && wrgn_equal(ga, gb);
}

int WINAPI OffsetRgn(HRGN h, int dx, int dy)
{
    wrgn *g = gdi_region(h);
    if (!g)
        return ERROR;
    wrgn_offset(g, dx, dy);
    return wrgn_type(g);
}

int WINAPI GetRgnBox(HRGN h, LPRECT rc)
{
    wrgn *g = gdi_region(h);
    if (!g)
        return ERROR;
    *rc = g->box;
    return wrgn_type(g);
}

BOOL WINAPI PtInRegion(HRGN h, int x, int y)
{
    wrgn *g = gdi_region(h);
    return g && wrgn_pt_in(g, x, y);
}

BOOL WINAPI RectInRegion(HRGN h, const RECT *rc)
{
    wrgn *g = gdi_region(h);
    return g && wrgn_rect_in(g, rc);
}

/* ---------------------------------------------------------------- palettes */

HPALETTE WINAPI CreatePalette(const LOGPALETTE *lp)
{
    if (!lp || lp->palNumEntries == 0 || lp->palNumEntries > 256)
        return 0;
    wpalette *p;
    HPALETTE h = new_obj(WOBJ_PALETTE, sizeof(*p), (void **)&p);
    if (!h)
        return 0;
    p->n = lp->palNumEntries;
    memcpy(p->e, lp->palPalEntry, sizeof(PALETTEENTRY) * (size_t)p->n);
    for (int i = 0; i < p->n; i++)
        p->map[i] = nearest_index(RGB(p->e[i].peRed, p->e[i].peGreen, p->e[i].peBlue));
    return h;
}

HPALETTE WINAPI SelectPalette(HDC hdc, HPALETTE hp, BOOL bg)
{
    (void)bg;
    wdc *dc = gdi_dc(hdc);
    if (!dc || !win_handle_get(hp, WOBJ_PALETTE))
        return 0;
    HPALETTE old = dc->pal ? dc->pal : win_sys.stock[DEFAULT_PALETTE];
    dc->pal = hp;
    return old;
}

/* The free middle of the system palette (10..245) takes the logical
 * palette's colours in order; exact matches of static colours reuse them. */
UINT WINAPI RealizePalette(HDC hdc)
{
    wdc *dc = gdi_dc(hdc);
    if (!dc)
        return 0;
    wpalette *p = dc_palette(dc);
    if (!p || p == win_handle_get(win_sys.stock[DEFAULT_PALETTE], WOBJ_PALETTE))
        return 0;
    int next = 10;
    UINT changed = 0;
    for (int i = 0; i < p->n; i++) {
        const PALETTEENTRY *e = &p->e[i];
        int found = -1;
        for (int k = 0; k < 10 && found < 0; k++) {
            const PALETTEENTRY *a = &win_sys.syspal[k], *b = &win_sys.syspal[246 + k];
            if (a->peRed == e->peRed && a->peGreen == e->peGreen && a->peBlue == e->peBlue)
                found = k;
            else if (b->peRed == e->peRed && b->peGreen == e->peGreen && b->peBlue == e->peBlue)
                found = 246 + k;
        }
        if (found < 0 && next < 246) {
            found = next++;
            win_sys.syspal[found] = *e;
            win_sys.syspal[found].peFlags = 0;
            changed++;
        }
        p->map[i] = (uint8_t)(found < 0
            ? nearest_index(RGB(e->peRed, e->peGreen, e->peBlue)) : found);
    }
    p->realized = 1;
    color_cache_flush();
    if (changed)
        syspal_upload(10, 236);
    return changed;
}

UINT WINAPI GetPaletteEntries(HPALETTE hp, UINT first, UINT n, LPPALETTEENTRY pe)
{
    wpalette *p = (wpalette *)win_handle_get(hp, WOBJ_PALETTE);
    if (!p || first >= (UINT)p->n)
        return 0;
    if (first + n > (UINT)p->n)
        n = (UINT)p->n - first;
    if (pe)
        memcpy(pe, &p->e[first], sizeof(PALETTEENTRY) * n);
    return n;
}

UINT WINAPI SetPaletteEntries(HPALETTE hp, UINT first, UINT n, const PALETTEENTRY *pe)
{
    wpalette *p = (wpalette *)win_handle_get(hp, WOBJ_PALETTE);
    if (!p || first >= (UINT)p->n)
        return 0;
    if (first + n > (UINT)p->n)
        n = (UINT)p->n - first;
    memcpy(&p->e[first], pe, sizeof(PALETTEENTRY) * n);
    return n;
}

UINT WINAPI GetSystemPaletteEntries(HDC hdc, UINT first, UINT n, LPPALETTEENTRY pe)
{
    (void)hdc;
    if (first >= 256)
        return 0;
    if (first + n > 256)
        n = 256 - first;
    memcpy(pe, &win_sys.syspal[first], sizeof(PALETTEENTRY) * n);
    return n;
}

/* ---------------------------------------------------------------- stock */

static HGDIOBJ stock_set(int idx, HGDIOBJ h)
{
    win_sys.stock[idx] = h;
    win_handle_set_stock(h);
    return h;
}

static HBITMAP stock_bitmap;    /* 1x1 mono, default for memory DCs */

/* the system palette into the hardware again (after the start-up logo) */
void gdi_palette_reset(void)
{
    syspal_upload(0, 256);
}

int gdi_init(void)
{
    syspal_default();
    color_cache_flush();

    stock_set(WHITE_BRUSH, CreateSolidBrush(RGB(255, 255, 255)));
    stock_set(LTGRAY_BRUSH, CreateSolidBrush(RGB(192, 192, 192)));
    stock_set(GRAY_BRUSH, CreateSolidBrush(RGB(128, 128, 128)));
    stock_set(DKGRAY_BRUSH, CreateSolidBrush(RGB(64, 64, 64)));
    stock_set(BLACK_BRUSH, CreateSolidBrush(RGB(0, 0, 0)));
    {
        wbrush *b;
        HBRUSH h = new_obj(WOBJ_BRUSH, sizeof(*b), (void **)&b);
        if (h)
            b->style = WBR_NULL;
        stock_set(NULL_BRUSH, h);
    }
    stock_set(WHITE_PEN, CreatePen(PS_SOLID, 1, RGB(255, 255, 255)));
    stock_set(BLACK_PEN, CreatePen(PS_SOLID, 1, RGB(0, 0, 0)));
    stock_set(NULL_PEN, CreatePen(PS_NULL, 1, 0));

    {
        LOGPALETTE *lp = (LOGPALETTE *)win_alloc(sizeof(LOGPALETTE) + 20 * sizeof(PALETTEENTRY));
        if (!lp)
            return -1;
        lp->palVersion = 0x300;
        lp->palNumEntries = 20;
        for (int i = 0; i < 10; i++) {
            lp->palPalEntry[i] = win_sys.syspal[i];
            lp->palPalEntry[10 + i] = win_sys.syspal[246 + i];
        }
        HPALETTE hp = CreatePalette(lp);
        win_free(lp);
        wpalette *p = (wpalette *)win_handle_get(hp, WOBJ_PALETTE);
        if (!p)
            return -1;
        for (int i = 0; i < 10; i++) {
            p->map[i] = (uint8_t)i;
            p->map[10 + i] = (uint8_t)(246 + i);
        }
        stock_set(DEFAULT_PALETTE, hp);
    }
    stock_bitmap = CreateBitmap(1, 1, 1, 1, NULL);
    win_handle_set_stock(stock_bitmap);

    gdi_text_init();
    syspal_upload(0, 256);
    return 0;
}

void gdi_shutdown(void) {}

HGDIOBJ WINAPI GetStockObject(int index)
{
    if (index < 0 || index > SYSTEM_FIXED_FONT)
        return 0;
    return win_sys.stock[index];
}

/* ---------------------------------------------------------------- DCs */

wdc *gdi_dc(HDC h) { return (wdc *)win_handle_get(h, WOBJ_DC); }

static void dc_defaults(wdc *dc)
{
    dc->pen = win_sys.stock[BLACK_PEN];
    dc->brush = win_sys.stock[WHITE_BRUSH];
    dc->font = win_sys.stock[SYSTEM_FONT];
    dc->pal = 0;
    dc->text = RGB(0, 0, 0);
    dc->bk = RGB(255, 255, 255);
    dc->bkmode = OPAQUE;
    dc->rop2 = R2_COPYPEN;
    dc->polyfill = ALTERNATE;
    dc->stretch = BLACKONWHITE;
    dc->char_extra = dc->break_extra = dc->break_count = 0;
    dc->align = TA_TOP | TA_LEFT | TA_NOUPDATECP;
    dc->vp_x = dc->vp_y = dc->win_x = dc->win_y = 0;
    dc->mapmode = MM_TEXT;
    dc->wex = dc->wey = dc->vex = dc->vey = 1;
    dc->brush_x = dc->brush_y = 0;
    dc->cur.x = dc->cur.y = 0;
}

static HDC dc_new(wdc **out)
{
    wdc *dc;
    HDC h = new_obj(WOBJ_DC, sizeof(*dc), (void **)&dc);
    if (!h)
        return 0;
    wrgn_init(&dc->vis);
    wrgn_init(&dc->clip);
    wrgn_init(&dc->eff);
    dc->eff_dirty = 1;
    dc_defaults(dc);
    *out = dc;
    return h;
}

HDC gdi_create_screen_dc(HWND hwnd)
{
    wdc *dc;
    HDC h = dc_new(&dc);
    if (!h)
        return 0;
    dc->surf = &win_sys.screen;
    dc->is_screen = 1;
    dc->hwnd = hwnd;
    RECT rc = { 0, 0, win_sys.screen.width, win_sys.screen.height };
    wrgn_set_rect(&dc->vis, &rc);
    return h;
}

void gdi_set_dc_origin(HDC h, int x, int y)
{
    wdc *dc = gdi_dc(h);
    if (dc) {
        dc->org_x = x;
        dc->org_y = y;
    }
}

void gdi_set_dc_vis(HDC h, const wrgn *vis)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return;
    wrgn_copy(&dc->vis, vis);
    dc->eff_dirty = 1;
}

static void dc_reset_state(wdc *dc)
{
    dc_defaults(dc);
    dc->has_clip = 0;
    wrgn_clear(&dc->clip);
    dc->eff_dirty = 1;
}

/* USER reuses window DCs: GetDC resets attributes this way. */
void gdi_reset_dc(HDC h)
{
    wdc *dc = gdi_dc(h);
    if (dc) {
        while (dc->saved) {
            wdc *s = dc->saved;
            dc->saved = s->saved;
            wrgn_free(&s->clip);
            win_free(s);
        }
        dc->save_level = 0;
        dc_reset_state(dc);
    }
}

HDC WINAPI CreateDC(LPCSTR driver, LPCSTR device, LPCSTR output, const void *init)
{
    (void)device; (void)output; (void)init;
    if (driver && lstrcmpi(driver, "DISPLAY"))
        return 0;
    return gdi_create_screen_dc(0);
}

static void dc_set_bitmap_vis(wdc *dc)
{
    RECT rc = { 0, 0, dc->surf->width, dc->surf->height };
    wrgn_set_rect(&dc->vis, &rc);
    dc->eff_dirty = 1;
}

HDC WINAPI CreateCompatibleDC(HDC hdc)
{
    (void)hdc;
    wdc *dc;
    HDC h = dc_new(&dc);
    if (!h)
        return 0;
    wbitmap *bm = (wbitmap *)win_handle_get(stock_bitmap, WOBJ_BITMAP);
    dc->bitmap = stock_bitmap;
    dc->surf = &bm->s;
    dc_set_bitmap_vis(dc);
    return h;
}

BOOL WINAPI DeleteDC(HDC h)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return FALSE;
    gdi_reset_dc(h);
    if (dc->bitmap && dc->bitmap != stock_bitmap) {
        wbitmap *bm = (wbitmap *)win_handle_get(dc->bitmap, WOBJ_BITMAP);
        if (bm)
            bm->selected = 0;
    }
    wrgn_free(&dc->vis);
    wrgn_free(&dc->clip);
    wrgn_free(&dc->eff);
    win_free(dc);
    win_handle_free(h);
    return TRUE;
}

int WINAPI SaveDC(HDC h)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return 0;
    wdc *s = (wdc *)win_alloc(sizeof(*s));
    if (!s)
        return 0;
    *s = *dc;
    wrgn_init(&s->vis);
    wrgn_init(&s->eff);
    wrgn_init(&s->clip);
    wrgn_copy(&s->clip, &dc->clip);
    s->saved = dc->saved;
    dc->saved = s;
    return ++dc->save_level;
}

BOOL WINAPI RestoreDC(HDC h, int level)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return FALSE;
    if (level < 0)
        level = dc->save_level + level + 1;
    if (level <= 0 || level > dc->save_level)
        return FALSE;
    while (dc->save_level >= level) {
        wdc *s = dc->saved;
        if (!s)
            return FALSE;
        dc->saved = s->saved;
        dc->save_level--;
        if (dc->save_level + 1 == level) {
            /* restore attributes; surface, vis and origin stay the DC's */
            dc->pen = s->pen; dc->brush = s->brush; dc->font = s->font;
            dc->pal = s->pal; dc->text = s->text; dc->bk = s->bk;
            dc->bkmode = s->bkmode; dc->rop2 = s->rop2; dc->polyfill = s->polyfill;
            dc->stretch = s->stretch; dc->char_extra = s->char_extra;
            dc->break_extra = s->break_extra; dc->break_count = s->break_count;
            dc->align = s->align; dc->vp_x = s->vp_x; dc->vp_y = s->vp_y;
            dc->win_x = s->win_x; dc->win_y = s->win_y;
            dc->mapmode = s->mapmode;
            dc->wex = s->wex; dc->wey = s->wey; dc->vex = s->vex; dc->vey = s->vey;
            dc->brush_x = s->brush_x; dc->brush_y = s->brush_y; dc->cur = s->cur;
            dc->has_clip = s->has_clip;
            wrgn_copy(&dc->clip, &s->clip);
            dc->eff_dirty = 1;
            if (s->bitmap != dc->bitmap && win_handle_get(s->bitmap, WOBJ_BITMAP))
                SelectObject(h, s->bitmap);
        }
        wrgn_free(&s->clip);
        win_free(s);
    }
    return TRUE;
}

int WINAPI GetDeviceCaps(HDC h, int index)
{
    wdc *dc = gdi_dc(h);
    const wsurf *s = dc ? dc->surf : &win_sys.screen;
    switch (index) {
    case HORZRES: return s->width;
    case VERTRES: return s->height;
    case BITSPIXEL: return s->bpp;
    case PLANES: return 1;
    case NUMCOLORS:
        if (s->bpp == 1)
            return 2;
        if (dc && dc->is_screen && GetDisplayDriverInfoN(NULL, 0) <= 16)
            return (int)GetDisplayDriverInfoN(NULL, 0);
        return 20;
    case LOGPIXELSX: case LOGPIXELSY: return 96;
    case HORZSIZE: return s->width * 254 / 960;     /* mm, at 96 an inch */
    case VERTSIZE: return s->height * 254 / 960;
    case ASPECTX: case ASPECTY: return 10;          /* square pixels */
    case ASPECTXY: return 14;
    case TECHNOLOGY: return DT_RASDISPLAY;
    case DRIVERVERSION: return 0x300;
    case CLIPCAPS: return CP_RECTANGLE;
    case CURVECAPS: return CC_CIRCLES | CC_PIE | CC_CHORD | CC_ELLIPSES | CC_WIDE |
                           CC_INTERIORS | CC_ROUNDRECT;
    case LINECAPS: return LC_POLYLINE | LC_WIDE | LC_STYLED | LC_INTERIORS;
    case POLYGONALCAPS: return PC_POLYGON | PC_RECTANGLE | PC_SCANLINE | PC_WIDE |
                               PC_INTERIORS;
    case TEXTCAPS: return 0;
    case NUMBRUSHES: return -1;
    case NUMPENS: case NUMFONTS: return s->bpp == 1 ? 2 : 16;
    case COLORRES: return s->bpp == 8 ? 18 : 0;
    case SIZEPALETTE: return s->bpp == 8 ? 256 : 0;
    case NUMRESERVED: return s->bpp == 8 ? 20 : 0;
    case RASTERCAPS: return s->bpp == 8 ? RC_PALETTE : 0;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- selection */

HGDIOBJ WINAPI SelectObject(HDC h, HGDIOBJ obj)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return 0;
    HGDIOBJ old;
    switch (win_handle_type(obj)) {
    case WOBJ_PEN:   old = dc->pen;   dc->pen = obj;   return old;
    case WOBJ_BRUSH: old = dc->brush; dc->brush = obj; return old;
    case WOBJ_FONT:  old = dc->font;  dc->font = obj;  return old;
    case WOBJ_REGION:
        return (HGDIOBJ)SelectClipRgn(h, obj);
    case WOBJ_BITMAP: {
        if (!dc->bitmap)
            return 0;               /* only memory DCs take bitmaps */
        wbitmap *bm = (wbitmap *)win_handle_get(obj, WOBJ_BITMAP);
        if (obj != stock_bitmap && bm->selected && obj != dc->bitmap)
            return 0;
        wbitmap *ob = (wbitmap *)win_handle_get(dc->bitmap, WOBJ_BITMAP);
        if (ob)
            ob->selected = 0;
        old = dc->bitmap;
        dc->bitmap = obj;
        if (obj != stock_bitmap)
            bm->selected = 1;
        dc->surf = &bm->s;
        dc_set_bitmap_vis(dc);
        return old;
    }
    default:
        return 0;
    }
}

BOOL WINAPI DeleteObject(HGDIOBJ obj)
{
    if (win_handle_is_stock(obj))
        return TRUE;
    int t = win_handle_type(obj);
    void *o = win_handle_get(obj, t);
    if (!o)
        return FALSE;
    switch (t) {
    case WOBJ_PEN: case WOBJ_BRUSH: case WOBJ_FONT: case WOBJ_PALETTE:
        break;
    case WOBJ_BITMAP: {
        wbitmap *bm = (wbitmap *)o;
        if (bm->selected)
            return FALSE;
        win_free(bm->s.bits);
        break;
    }
    case WOBJ_REGION:
        wrgn_free((wrgn *)o);
        break;
    default:
        return FALSE;
    }
    win_free(o);
    win_handle_free(obj);
    return TRUE;
}

int WINAPI GetObject(HGDIOBJ obj, int size, LPVOID buf)
{
    int t = win_handle_type(obj);
    void *o = win_handle_get(obj, t);
    if (!o)
        return 0;
    switch (t) {
    case WOBJ_PEN: {
        wpen *p = (wpen *)o;
        LOGPEN lp = { (UINT)p->style, { p->width, 0 }, p->color };
        if (size > (int)sizeof(lp)) size = (int)sizeof(lp);
        if (buf) memcpy(buf, &lp, (size_t)size);
        return size;
    }
    case WOBJ_BRUSH: {
        wbrush *b = (wbrush *)o;
        LOGBRUSH lb = { 0, b->color, b->hatch };
        lb.lbStyle = b->style == WBR_NULL ? BS_NULL : b->style == WBR_HATCH ? BS_HATCHED
                   : b->style == WBR_PATTERN ? BS_PATTERN : BS_SOLID;
        if (b->style == WBR_PATTERN)
            lb.lbHatch = b->pattern;
        if (size > (int)sizeof(lb)) size = (int)sizeof(lb);
        if (buf) memcpy(buf, &lb, (size_t)size);
        return size;
    }
    case WOBJ_FONT: {
        const wfont *f = (const wfont *)o;
        if (size > (int)sizeof(LOGFONT)) size = (int)sizeof(LOGFONT);
        if (buf) memcpy(buf, &f->lf, (size_t)size);
        return size;
    }
    case WOBJ_BITMAP: {
        wbitmap *bm = (wbitmap *)o;
        BITMAP b = { 0, bm->s.width, bm->s.height, bm->s.pitch, 1, (BYTE)bm->s.bpp, NULL };
        if (size > (int)sizeof(b)) size = (int)sizeof(b);
        if (buf) memcpy(buf, &b, (size_t)size);
        return size;
    }
    case WOBJ_PALETTE: {
        WORD n = (WORD)((wpalette *)o)->n;
        if (size >= 2 && buf) memcpy(buf, &n, 2);
        return 2;
    }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- attributes */


COLORREF WINAPI SetTextColor(HDC h, COLORREF c) { DC_OR_RET(h, 0); COLORREF o = dc->text; dc->text = c; return o; }
COLORREF WINAPI GetTextColor(HDC h) { DC_OR_RET(h, 0); return dc->text; }
COLORREF WINAPI SetBkColor(HDC h, COLORREF c) { DC_OR_RET(h, 0); COLORREF o = dc->bk; dc->bk = c; return o; }
COLORREF WINAPI GetBkColor(HDC h) { DC_OR_RET(h, 0); return dc->bk; }
int WINAPI SetBkMode(HDC h, int m) { DC_OR_RET(h, 0); int o = dc->bkmode; dc->bkmode = m; return o; }
int WINAPI GetBkMode(HDC h) { DC_OR_RET(h, 0); return dc->bkmode; }
int WINAPI SetROP2(HDC h, int r) { DC_OR_RET(h, 0); int o = dc->rop2; if (r >= 1 && r <= 16) dc->rop2 = r; return o; }
int WINAPI GetROP2(HDC h) { DC_OR_RET(h, 0); return dc->rop2; }
UINT WINAPI SetTextAlign(HDC h, UINT a) { DC_OR_RET(h, 0); UINT o = dc->align; dc->align = a; return o; }
UINT WINAPI GetTextAlign(HDC h) { DC_OR_RET(h, 0); return dc->align; }
int WINAPI SetPolyFillMode(HDC h, int m) { DC_OR_RET(h, 0); int o = dc->polyfill; dc->polyfill = m; return o; }

#define MKDW(x, y) ((DWORD)(WORD)(x) | ((DWORD)(WORD)(y) << 16))

int WINAPI GetPolyFillMode(HDC h) { DC_OR_RET(h, 0); return dc->polyfill; }

/* ---------------------------------------------------------------- mapping */

/* v * num / den rounded to the nearest */
static int scale(long v, long num, long den)
{
    long long p = (long long)v * num;
    if (!den)
        return 0;
    return (int)((p < 0) == (den < 0) ? (p + den / 2) / den : (p - den / 2) / den);
}

int gdi_lx_to_dx(const wdc *dc, int lx) { return scale(lx, dc->vex, dc->wex); }
int gdi_ly_to_dy(const wdc *dc, int ly) { return scale(ly, dc->vey, dc->wey); }
int gdi_dx_to_lx(const wdc *dc, int dx) { return scale(dx, dc->wex, dc->vex); }
int gdi_dy_to_ly(const wdc *dc, int dy) { return scale(dy, dc->wey, dc->vey); }

/* a size in logical units as device pixels, at least one if it had any */
static int dev_size(const wdc *dc, int l, int y)
{
    int d = y ? gdi_ly_to_dy(dc, l) : gdi_lx_to_dx(dc, l);
    if (d < 0)
        d = -d;
    return l && !d ? 1 : d;
}

/* MM_ISOTROPIC: the viewport made as large on both axes as the smaller */
static void isotropic(wdc *dc)
{
    if (dc->mapmode != MM_ISOTROPIC)
        return;
    long long xs = (long long)(dc->vex < 0 ? -dc->vex : dc->vex) * (dc->wey < 0 ? -dc->wey : dc->wey);
    long long ys = (long long)(dc->vey < 0 ? -dc->vey : dc->vey) * (dc->wex < 0 ? -dc->wex : dc->wex);
    if (xs > ys) {
        int v = scale(dc->vey < 0 ? -dc->vey : dc->vey, dc->wex < 0 ? -dc->wex : dc->wex,
                      dc->wey < 0 ? -dc->wey : dc->wey);
        dc->vex = (dc->vex < 0 ? -1 : 1) * (v ? v : 1);
    } else if (ys > xs) {
        int v = scale(dc->vex < 0 ? -dc->vex : dc->vex, dc->wey < 0 ? -dc->wey : dc->wey,
                      dc->wex < 0 ? -dc->wex : dc->wex);
        dc->vey = (dc->vey < 0 ? -1 : 1) * (v ? v : 1);
    }
}

int WINAPI SetMapMode(HDC h, int mode)
{
    DC_OR_RET(h, 0);
    int old = dc->mapmode;
    static const int units[] = { 0, 0, 254, 2540, 100, 1000, 1440 };   /* an inch */
    if (mode < MM_TEXT || mode > MM_ANISOTROPIC)
        return 0;
    dc->mapmode = mode;
    if (mode == MM_TEXT) {
        dc->wex = dc->wey = dc->vex = dc->vey = 1;
    } else if (mode <= MM_TWIPS) {
        dc->wex = dc->wey = units[mode];
        dc->vex = 96;
        dc->vey = -96;
    } else if (mode == MM_ISOTROPIC && old != MM_ISOTROPIC && old != MM_ANISOTROPIC) {
        dc->wex = dc->wey = 254;        /* as MM_LOMETRIC, until set */
        dc->vex = 96;
        dc->vey = -96;
    }
    return old;
}

int WINAPI GetMapMode(HDC h) { DC_OR_RET(h, 0); return dc->mapmode; }

/* an extent: only MM_ISOTROPIC's and MM_ANISOTROPIC's change; none is 0 */
static BOOL set_ext(wdc *dc, int view, int x, int y, LPSIZE old)
{
    if (old) {
        old->cx = view ? dc->vex : dc->wex;
        old->cy = view ? dc->vey : dc->wey;
    }
    if (dc->mapmode != MM_ISOTROPIC && dc->mapmode != MM_ANISOTROPIC)
        return TRUE;
    if (!x || !y)
        return FALSE;
    if (view) {
        dc->vex = x;
        dc->vey = y;
    } else {
        dc->wex = x;
        dc->wey = y;
    }
    isotropic(dc);
    return TRUE;
}

BOOL WINAPI SetWindowExtEx(HDC h, int x, int y, LPSIZE old)
{
    DC_OR_RET(h, FALSE);
    return set_ext(dc, 0, x, y, old);
}

BOOL WINAPI SetViewportExtEx(HDC h, int x, int y, LPSIZE old)
{
    DC_OR_RET(h, FALSE);
    return set_ext(dc, 1, x, y, old);
}

DWORD WINAPI SetWindowExt(HDC h, int x, int y)
{
    SIZE o = { 0, 0 };
    return SetWindowExtEx(h, x, y, &o) ? MKDW(o.cx, o.cy) : 0;
}

DWORD WINAPI SetViewportExt(HDC h, int x, int y)
{
    SIZE o = { 0, 0 };
    return SetViewportExtEx(h, x, y, &o) ? MKDW(o.cx, o.cy) : 0;
}

BOOL WINAPI GetWindowExtEx(HDC h, LPSIZE sz)
{
    DC_OR_RET(h, FALSE);
    sz->cx = dc->wex;
    sz->cy = dc->wey;
    return TRUE;
}

BOOL WINAPI GetViewportExtEx(HDC h, LPSIZE sz)
{
    DC_OR_RET(h, FALSE);
    sz->cx = dc->vex;
    sz->cy = dc->vey;
    return TRUE;
}

DWORD WINAPI GetWindowExt(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->wex, dc->wey); }
DWORD WINAPI GetViewportExt(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->vex, dc->vey); }

/* ScaleViewportExt, ScaleWindowExt: ext * xn / xd, ext * yn / yd */
static BOOL scale_ext(wdc *dc, int view, int xn, int xd, int yn, int yd, LPSIZE old)
{
    if (!xd || !yd)
        return FALSE;
    int x = scale(view ? dc->vex : dc->wex, xn, xd), y = scale(view ? dc->vey : dc->wey, yn, yd);
    return set_ext(dc, view, x, y, old);
}

BOOL WINAPI ScaleViewportExtEx(HDC h, int xn, int xd, int yn, int yd, LPSIZE old)
{
    DC_OR_RET(h, FALSE);
    return scale_ext(dc, 1, xn, xd, yn, yd, old);
}

BOOL WINAPI ScaleWindowExtEx(HDC h, int xn, int xd, int yn, int yd, LPSIZE old)
{
    DC_OR_RET(h, FALSE);
    return scale_ext(dc, 0, xn, xd, yn, yd, old);
}

DWORD WINAPI ScaleViewportExt(HDC h, int xn, int xd, int yn, int yd)
{
    SIZE o = { 0, 0 };
    return ScaleViewportExtEx(h, xn, xd, yn, yd, &o) ? MKDW(o.cx, o.cy) : 0;
}

DWORD WINAPI ScaleWindowExt(HDC h, int xn, int xd, int yn, int yd)
{
    SIZE o = { 0, 0 };
    return ScaleWindowExtEx(h, xn, xd, yn, yd, &o) ? MKDW(o.cx, o.cy) : 0;
}

/* the origins: set, got, moved by (dx, dy) */
static BOOL set_org(wdc *dc, int view, int x, int y, int rel, LPPOINT old)
{
    int *px = view ? &dc->vp_x : &dc->win_x, *py = view ? &dc->vp_y : &dc->win_y;
    if (old) {
        old->x = *px;
        old->y = *py;
    }
    *px = rel ? *px + x : x;
    *py = rel ? *py + y : y;
    return TRUE;
}

BOOL WINAPI SetWindowOrgEx(HDC h, int x, int y, LPPOINT old)
{
    DC_OR_RET(h, FALSE);
    return set_org(dc, 0, x, y, 0, old);
}

BOOL WINAPI SetViewportOrgEx(HDC h, int x, int y, LPPOINT old)
{
    DC_OR_RET(h, FALSE);
    return set_org(dc, 1, x, y, 0, old);
}

BOOL WINAPI OffsetWindowOrgEx(HDC h, int x, int y, LPPOINT old)
{
    DC_OR_RET(h, FALSE);
    return set_org(dc, 0, x, y, 1, old);
}

BOOL WINAPI OffsetViewportOrgEx(HDC h, int x, int y, LPPOINT old)
{
    DC_OR_RET(h, FALSE);
    return set_org(dc, 1, x, y, 1, old);
}

DWORD WINAPI SetWindowOrg(HDC h, int x, int y)
{
    POINT o = { 0, 0 };
    return SetWindowOrgEx(h, x, y, &o) ? MKDW(o.x, o.y) : 0;
}

DWORD WINAPI SetViewportOrg(HDC h, int x, int y)
{
    POINT o = { 0, 0 };
    return SetViewportOrgEx(h, x, y, &o) ? MKDW(o.x, o.y) : 0;
}

DWORD WINAPI OffsetWindowOrg(HDC h, int x, int y)
{
    POINT o = { 0, 0 };
    return OffsetWindowOrgEx(h, x, y, &o) ? MKDW(o.x, o.y) : 0;
}

DWORD WINAPI OffsetViewportOrg(HDC h, int x, int y)
{
    POINT o = { 0, 0 };
    return OffsetViewportOrgEx(h, x, y, &o) ? MKDW(o.x, o.y) : 0;
}

DWORD WINAPI GetWindowOrg(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->win_x, dc->win_y); }
DWORD WINAPI GetViewportOrg(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->vp_x, dc->vp_y); }

BOOL WINAPI GetWindowOrgEx(HDC h, LPPOINT pt)
{
    DC_OR_RET(h, FALSE);
    pt->x = dc->win_x;
    pt->y = dc->win_y;
    return TRUE;
}

BOOL WINAPI GetViewportOrgEx(HDC h, LPPOINT pt)
{
    DC_OR_RET(h, FALSE);
    pt->x = dc->vp_x;
    pt->y = dc->vp_y;
    return TRUE;
}

DWORD WINAPI SetBrushOrg(HDC h, int x, int y)
{
    DC_OR_RET(h, 0);
    DWORD o = MKDW(dc->brush_x, dc->brush_y);
    dc->brush_x = x; dc->brush_y = y;
    return o;
}

DWORD WINAPI GetBrushOrg(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->brush_x, dc->brush_y); }

BOOL WINAPI GetBrushOrgEx(HDC h, LPPOINT pt)
{
    DC_OR_RET(h, FALSE);
    pt->x = dc->brush_x;
    pt->y = dc->brush_y;
    return TRUE;
}

DWORD WINAPI GetCurrentPosition(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->cur.x, dc->cur.y); }

BOOL WINAPI GetCurrentPositionEx(HDC h, LPPOINT pt)
{
    DC_OR_RET(h, FALSE);
    *pt = dc->cur;
    return TRUE;
}

/* the DC's origin on the screen (its window's client area) */
DWORD WINAPI GetDCOrg(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->org_x, dc->org_y); }

COLORREF WINAPI GetNearestColor(HDC h, COLORREF c)
{
    wdc *dc = gdi_dc(h);
    return index_color(dc ? dc->surf : NULL, gdi_color_index(dc, c));
}

/* ---------------------------------------------------------------- clipping */

/* logical -> surface */
void gdi_lp_to_dp(wdc *dc, int *x, int *y)
{
    if (dc->mapmode == MM_TEXT) {
        *x = *x - dc->win_x + dc->vp_x + dc->org_x;
        *y = *y - dc->win_y + dc->vp_y + dc->org_y;
        return;
    }
    *x = scale(*x - dc->win_x, dc->vex, dc->wex) + dc->vp_x + dc->org_x;
    *y = scale(*y - dc->win_y, dc->vey, dc->wey) + dc->vp_y + dc->org_y;
}

static void lp_rect(wdc *dc, RECT *r)
{
    gdi_lp_to_dp(dc, &r->left, &r->top);
    gdi_lp_to_dp(dc, &r->right, &r->bottom);
    if (r->left > r->right) { int t = r->left; r->left = r->right; r->right = t; }
    if (r->top > r->bottom) { int t = r->top; r->top = r->bottom; r->bottom = t; }
}

static wrgn *dc_eff(wdc *dc)
{
    if (dc->eff_dirty) {
        if (dc->has_clip)
            wrgn_and(&dc->eff, &dc->vis, &dc->clip);
        else
            wrgn_copy(&dc->eff, &dc->vis);
        dc->eff_dirty = 0;
    }
    return &dc->eff;
}

const wclip *gdi_dc_clip(wdc *dc, wclip *tmp)
{
    wrgn *e = dc_eff(dc);
    tmp->r = e->r;
    tmp->n = e->n;
    return tmp;
}

/* Start a clip region equal to the visible area, if there is none yet. */
static void dc_clip_start(wdc *dc)
{
    if (!dc->has_clip) {
        wrgn_copy(&dc->clip, &dc->vis);
        dc->has_clip = 1;
    }
    dc->eff_dirty = 1;
}

int WINAPI SelectClipRgn(HDC h, HRGN hr)
{
    DC_OR_RET(h, ERROR);
    wrgn *g = gdi_region(hr);
    if (!g) {
        dc->has_clip = 0;
        wrgn_clear(&dc->clip);
    } else {
        wrgn_copy(&dc->clip, g);           /* device coordinates */
        wrgn_offset(&dc->clip, dc->org_x, dc->org_y);
        dc->has_clip = 1;
    }
    dc->eff_dirty = 1;
    return wrgn_type(dc_eff(dc));
}

int WINAPI IntersectClipRect(HDC h, int l, int t, int r, int b)
{
    DC_OR_RET(h, ERROR);
    RECT rc = { l, t, r, b };
    lp_rect(dc, &rc);
    dc_clip_start(dc);
    wrgn_and_rect(&dc->clip, &rc);
    return wrgn_type(dc_eff(dc));
}

int WINAPI ExcludeClipRect(HDC h, int l, int t, int r, int b)
{
    DC_OR_RET(h, ERROR);
    RECT rc = { l, t, r, b };
    lp_rect(dc, &rc);
    dc_clip_start(dc);
    wrgn_diff_rect(&dc->clip, &rc);
    return wrgn_type(dc_eff(dc));
}

/* surface -> logical */
static void dp_to_lp(wdc *dc, int *x, int *y)
{
    *x = scale(*x - dc->org_x - dc->vp_x, dc->wex, dc->vex) + dc->win_x;
    *y = scale(*y - dc->org_y - dc->vp_y, dc->wey, dc->vey) + dc->win_y;
}

/* LPtoDP, DPtoLP: logical and device (the DC's, not the surface's) points */
BOOL WINAPI LPtoDP(HDC h, LPPOINT pts, int n)
{
    DC_OR_RET(h, FALSE);
    for (int i = 0; i < n; i++) {
        gdi_lp_to_dp(dc, &pts[i].x, &pts[i].y);
        pts[i].x -= dc->org_x;
        pts[i].y -= dc->org_y;
    }
    return TRUE;
}

BOOL WINAPI DPtoLP(HDC h, LPPOINT pts, int n)
{
    DC_OR_RET(h, FALSE);
    for (int i = 0; i < n; i++) {
        pts[i].x += dc->org_x;
        pts[i].y += dc->org_y;
        dp_to_lp(dc, &pts[i].x, &pts[i].y);
    }
    return TRUE;
}

int WINAPI GetClipBox(HDC h, LPRECT rc)
{
    DC_OR_RET(h, ERROR);
    wrgn *e = dc_eff(dc);
    *rc = e->box;
    if (e->n) {
        dp_to_lp(dc, &rc->left, &rc->top);
        dp_to_lp(dc, &rc->right, &rc->bottom);
    }
    return wrgn_type(e);
}

BOOL WINAPI PtVisible(HDC h, int x, int y)
{
    DC_OR_RET(h, FALSE);
    gdi_lp_to_dp(dc, &x, &y);
    return wrgn_pt_in(dc_eff(dc), x, y);
}

BOOL WINAPI RectVisible(HDC h, const RECT *rc)
{
    DC_OR_RET(h, FALSE);
    RECT r = *rc;
    lp_rect(dc, &r);
    return wrgn_rect_in(dc_eff(dc), &r);
}

/* ---------------------------------------------------------------- drawing support */

void gdi_begin_draw(wdc *dc, const RECT *b)
{
    if (dc->is_screen)
        cursor_exclude(b);
}

void gdi_end_draw(wdc *dc, const RECT *b)
{
    if (!dc->is_screen)
        return;
    RECT r;
    RECT scr = { 0, 0, win_sys.screen.width, win_sys.screen.height };
    if (IntersectRect(&r, b, &scr) && IntersectRect(&r, &r, &dc_eff(dc)->box)) {
        if (win_sys.disp && win_sys.disp->flush && !win_sys.screen_real)
            win_sys.disp->flush(r.left, r.top, r.right, r.bottom);
        if (gdi_overlay_hook)
            gdi_overlay_hook(dc->hwnd, &r);
    }
    cursor_unexclude();
}

static const uint8_t hatch_rows[6][8] = {
    { 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00 },     /* HS_HORIZONTAL */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 },     /* HS_VERTICAL */
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },     /* HS_FDIAGONAL \ */
    { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 },     /* HS_BDIAGONAL / */
    { 0x10, 0x10, 0x10, 0xFF, 0x10, 0x10, 0x10, 0x10 },     /* HS_CROSS */
    { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 },     /* HS_DIAGCROSS */
};

void gdi_realize_brush(wdc *dc, HBRUSH hb, wfill *f)
{
    wbrush *b = (wbrush *)win_handle_get(hb, WOBJ_BRUSH);
    memset(f, 0, sizeof(*f));
    f->org_x = dc->org_x + dc->brush_x;
    f->org_y = dc->org_y + dc->brush_y;
    if (!b || b->style == WBR_NULL) {
        f->kind = WFILL_NONE;
        return;
    }
    if (b->style == WBR_SOLID) {
        f->kind = WFILL_SOLID;
        f->solid = gdi_color_index(dc, b->color);
        return;
    }
    f->kind = WFILL_PATTERN;
    if (b->style == WBR_HATCH) {
        uint8_t fg = gdi_color_index(dc, b->color), bk = gdi_color_index(dc, dc->bk);
        f->masked = dc->bkmode == TRANSPARENT;
        for (int i = 0; i < 64; i++) {
            int on = (hatch_rows[b->hatch][i >> 3] >> (7 - (i & 7))) & 1;
            f->pat[i] = on ? fg : bk;
            f->pat_mask[i] = (uint8_t)on;
        }
        return;
    }
    /* pattern brush */
    int mono_dst = dc->surf->bpp == 1;
    if (b->pat_is_mono) {
        uint8_t fg = gdi_color_index(dc, dc->text), bk = gdi_color_index(dc, dc->bk);
        for (int i = 0; i < 64; i++) {
            int on = (b->patmono[i >> 3] >> (7 - (i & 7))) & 1;
            f->pat[i] = mono_dst ? (uint8_t)on : (on ? bk : fg);
        }
    } else {
        for (int i = 0; i < 64; i++)
            f->pat[i] = mono_dst ? (uint8_t)(b->pat8[i] == gdi_color_index(dc, dc->bk))
                                 : b->pat8[i];
    }
}

/* the DC's pen; its width (logical) in pixels if the mapping mode scales */
static wpen *dc_pen(wdc *dc)
{
    static wpen scaled;
    wpen *p = (wpen *)win_handle_get(dc->pen, WOBJ_PEN);
    if (!p)
        p = (wpen *)win_handle_get(win_sys.stock[BLACK_PEN], WOBJ_PEN);
    if (dc->mapmode == MM_TEXT || p->width <= 1)
        return p;
    scaled = *p;
    scaled.width = dev_size(dc, p->width, 0);
    return &scaled;
}

static void fill_rect_dev(wdc *dc, const RECT *rc, const wfill *f, int rop3)
{
    wclip cl;
    dib_fill_rect(dc->surf, gdi_dc_clip(dc, &cl), rc, f, rop3);
}

/* ---------------------------------------------------------------- pixels, lines */

COLORREF WINAPI SetPixel(HDC h, int x, int y, COLORREF c)
{
    DC_OR_RET(h, (COLORREF)-1);
    gdi_lp_to_dp(dc, &x, &y);
    uint8_t idx = gdi_color_index(dc, c);
    RECT b = { x, y, x + 1, y + 1 };
    wclip cl;
    gdi_begin_draw(dc, &b);
    dib_set_pixel(dc->surf, gdi_dc_clip(dc, &cl), x, y, idx, R2_COPYPEN);
    gdi_end_draw(dc, &b);
    return index_color(dc->surf, idx);
}

COLORREF WINAPI GetPixel(HDC h, int x, int y)
{
    DC_OR_RET(h, (COLORREF)-1);
    gdi_lp_to_dp(dc, &x, &y);
    if (!wrgn_pt_in(dc_eff(dc), x, y))
        return (COLORREF)-1;
    RECT b = { x, y, x + 1, y + 1 };
    gdi_begin_draw(dc, &b);           /* read under the cursor */
    uint8_t v = dib_get_pixel(dc->surf, x, y);
    if (dc->is_screen)
        cursor_unexclude();
    return index_color(dc->surf, v);
}

DWORD WINAPI MoveTo(HDC h, int x, int y)
{
    DC_OR_RET(h, 0);
    DWORD o = MKDW(dc->cur.x, dc->cur.y);
    dc->cur.x = x; dc->cur.y = y;
    return o;
}

BOOL WINAPI MoveToEx(HDC h, int x, int y, LPPOINT old)
{
    DC_OR_RET(h, FALSE);
    if (old)
        *old = dc->cur;
    dc->cur.x = x; dc->cur.y = y;
    return TRUE;
}

/* Device-coordinate line with the DC pen; wide pens stamp squares. */
static void line_dev(wdc *dc, wpen *p, int x0, int y0, int x1, int y1, int skip_last)
{
    wclip cl;
    const wclip *c = gdi_dc_clip(dc, &cl);
    uint8_t col = gdi_color_index(dc, p->color);
    if (p->width <= 1) {
        dib_line(dc->surf, c, x0, y0, x1, y1, col, dc->rop2,
                 p->style == PS_INSIDEFRAME ? PS_SOLID : p->style, skip_last);
        return;
    }
    /* Wide pen: the stroke of a square moved along the line is convex, so
     * every row is one span; fill spans once so XOR modes stay exact. */
    wfill f;
    memset(&f, 0, sizeof(f));
    f.kind = WFILL_SOLID;
    f.solid = col;
    int rop = dib_rop2_to_rop3(dc->rop2), w = p->width, h0 = (w - 1) / 2;
    int ytop = (y0 < y1 ? y0 : y1) - h0, rows = (y0 > y1 ? y0 - y1 : y1 - y0) + w;
    int *mn = (int *)win_alloc(sizeof(int) * 2 * (size_t)rows);
    if (!mn)
        return;
    int *mx = mn + rows;
    for (int i = 0; i < rows; i++) {
        mn[i] = 0x7FFFFFFF;
        mx[i] = -0x7FFFFFFF;
    }
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int steps = (dx > dy ? dx : dy) + (skip_last ? 0 : 1);
    int err = (dx > dy ? dx : -dy) / 2, x = x0, y = y0;
    for (int n = 0; n < steps; n++) {
        for (int k = 0; k < w; k++) {
            int i = y - h0 + k - ytop;
            if (x - h0 < mn[i]) mn[i] = x - h0;
            if (x - h0 + w > mx[i]) mx[i] = x - h0 + w;
        }
        int e2 = err;
        if (e2 > -dx) { err -= dy; x += sx; }
        if (e2 < dy) { err += dx; y += sy; }
    }
    for (int i = 0; i < rows; i++)
        if (mn[i] < mx[i]) {
            RECT r = { mn[i], ytop + i, mx[i], ytop + i + 1 };
            dib_fill_rect(dc->surf, c, &r, &f, rop);
        }
    win_free(mn);
}

BOOL WINAPI LineTo(HDC h, int x, int y)
{
    DC_OR_RET(h, FALSE);
    wpen *p = dc_pen(dc);
    int x0 = dc->cur.x, y0 = dc->cur.y;
    dc->cur.x = x; dc->cur.y = y;
    if (p->style == PS_NULL)
        return TRUE;
    gdi_lp_to_dp(dc, &x0, &y0);
    gdi_lp_to_dp(dc, &x, &y);
    int hw = p->width;
    RECT b = { (x0 < x ? x0 : x) - hw, (y0 < y ? y0 : y) - hw,
               (x0 > x ? x0 : x) + hw + 1, (y0 > y ? y0 : y) + hw + 1 };
    gdi_begin_draw(dc, &b);
    line_dev(dc, p, x0, y0, x, y, 1);
    gdi_end_draw(dc, &b);
    return TRUE;
}

static void points_bounds(wdc *dc, const POINT *pts, int n, int pad, RECT *b)
{
    SetRectEmpty(b);
    for (int i = 0; i < n; i++) {
        int x = pts[i].x, y = pts[i].y;
        gdi_lp_to_dp(dc, &x, &y);
        if (i == 0) {
            b->left = b->right = x;
            b->top = b->bottom = y;
        }
        if (x < b->left) b->left = x;
        if (x > b->right) b->right = x;
        if (y < b->top) b->top = y;
        if (y > b->bottom) b->bottom = y;
    }
    InflateRect(b, pad, pad);
    b->right++;
    b->bottom++;
}

BOOL WINAPI Polyline(HDC h, const POINT *pts, int n)
{
    DC_OR_RET(h, FALSE);
    if (n < 2)
        return FALSE;
    wpen *p = dc_pen(dc);
    if (p->style == PS_NULL)
        return TRUE;
    RECT b;
    points_bounds(dc, pts, n, p->width, &b);
    gdi_begin_draw(dc, &b);
    for (int i = 1; i < n; i++) {
        int x0 = pts[i - 1].x, y0 = pts[i - 1].y, x1 = pts[i].x, y1 = pts[i].y;
        gdi_lp_to_dp(dc, &x0, &y0);
        gdi_lp_to_dp(dc, &x1, &y1);
        line_dev(dc, p, x0, y0, x1, y1, 1);
    }
    gdi_end_draw(dc, &b);
    return TRUE;
}

/* ---------------------------------------------------------------- filled shapes */

static unsigned long long isqrt64(unsigned long long v)
{
    unsigned long long r = 0, bit = 1ull << 62;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/*
 * Row extent of a rounded rectangle with box [l,r) x [t,b) and corner
 * ellipse ew x eh (ew >= width and eh >= height gives an ellipse, 0 a plain
 * rectangle). Returns 0 if the row is outside; *xl..*xr inclusive.
 */
static int shape_row(int l, int t, int r, int b, int ew, int eh, int y, int *xl, int *xr)
{
    if (y < t || y >= b || l >= r)
        return 0;
    int w = r - l, h = b - t;
    if (ew > w) ew = w;
    if (eh > h) eh = h;
    if (ew < 2 || eh < 2) {
        *xl = l;
        *xr = r - 1;
        return 1;
    }
    /* map y into the corner ellipse box [t, t+eh) */
    int top_half = eh / 2, bot_half = eh - top_half;
    int ey;
    if (y < t + top_half)
        ey = y;
    else if (y >= b - bot_half)
        ey = y - (h - eh);
    else {
        *xl = l;
        *xr = r - 1;
        return 1;
    }
    long D = eh, v = 2L * ey + 1 - (2L * t + eh);
    if (v < 0) v = -v;
    if (v > D)
        return 0;
    long hw = (long)(isqrt64((unsigned long long)ew * (unsigned long long)ew *
                             (unsigned long long)(D * D - v * v)) / (unsigned long long)D);
    long c2 = 2L * l + ew;                       /* doubled centre of corner box */
    long lo = (c2 - hw) / 2, hi = (c2 + hw - 1) / 2;
    if (lo > hi)
        lo = hi = (c2 - 1) / 2;
    if (lo < l) lo = l;
    *xl = (int)lo;
    *xr = (int)(hi + (w - ew));
    if (*xr > r - 1) *xr = r - 1;
    return 1;
}

/*
 * Draw a shape: interior with the brush, outline = outer minus the shape
 * inset by the pen width. Box in device (surface) coordinates, exclusive.
 */
static void draw_shape(wdc *dc, RECT box, int ew, int eh)
{
    wpen *p = dc_pen(dc);
    int has_pen = p->style != PS_NULL;
    int pw = has_pen ? p->width : 0;
    if (!has_pen) {               /* NULL pen: Windows fills one pixel less */
        box.right--;
        box.bottom--;
    } else if (pw > 1 && p->style != PS_INSIDEFRAME) {
        int o = (pw - 1) / 2;     /* centred on the box edge */
        InflateRect(&box, o, o);
        if (ew) { ew += 2 * o; eh += 2 * o; }
    }
    if (box.left >= box.right || box.top >= box.bottom)
        return;

    wfill bf, pf;
    gdi_realize_brush(dc, dc->brush, &bf);
    pf.kind = WFILL_SOLID;
    pf.solid = has_pen ? gdi_color_index(dc, p->color) : 0;
    pf.masked = 0;
    int rop = dib_rop2_to_rop3(dc->rop2);
    int iw = ew ? ew - 2 * pw : 0, ih = eh ? eh - 2 * pw : 0;
    if (ew && iw < 0) iw = 0;
    if (eh && ih < 0) ih = 0;

    gdi_begin_draw(dc, &box);
    wclip cl;
    const wclip *c = gdi_dc_clip(dc, &cl);
    int dashed = has_pen && pw == 1 && p->style >= PS_DASH && p->style <= PS_DASHDOTDOT;

    for (int y = box.top; y < box.bottom; y++) {
        int ol, orr, il = 0, ir = -1;
        if (!shape_row(box.left, box.top, box.right, box.bottom, ew, eh, y, &ol, &orr))
            continue;
        int inner = shape_row(box.left + pw, box.top + pw, box.right - pw, box.bottom - pw,
                              iw, ih, y, &il, &ir);
        if (!inner || il > ir) {
            if (!dashed) {
                RECT r = { ol, y, orr + 1, y + 1 };
                dib_fill_rect(dc->surf, c, &r, has_pen ? &pf : &bf, rop);
            }
            continue;
        }
        if (bf.kind != WFILL_NONE) {
            RECT r = { il, y, ir + 1, y + 1 };
            dib_fill_rect(dc->surf, c, &r, &bf, rop);
        }
        if (has_pen && !dashed) {
            RECT a = { ol, y, il, y + 1 }, b = { ir + 1, y, orr + 1, y + 1 };
            dib_fill_rect(dc->surf, c, &a, &pf, rop);
            dib_fill_rect(dc->surf, c, &b, &pf, rop);
        }
    }
    if (dashed) {
        /* dashed outlines only for rectangles; curves fall back to the
         * straight edges of their box (rarely used) */
        uint8_t col = pf.solid;
        int l = box.left, t = box.top, r = box.right - 1, b = box.bottom - 1;
        dib_line(dc->surf, c, l, t, r, t, col, dc->rop2, p->style, 1);
        dib_line(dc->surf, c, r, t, r, b, col, dc->rop2, p->style, 1);
        dib_line(dc->surf, c, r, b, l, b, col, dc->rop2, p->style, 1);
        dib_line(dc->surf, c, l, b, l, t, col, dc->rop2, p->style, 1);
    }
    gdi_end_draw(dc, &box);
}

static int shape_box(wdc *dc, int l, int t, int r, int b, RECT *out)
{
    RECT rc = { l, t, r, b };
    lp_rect(dc, &rc);
    *out = rc;
    return rc.left < rc.right && rc.top < rc.bottom;
}

/* a * b / c rounded (Win16's: -32768 for c 0 or a result past 16 bits) */
int WINAPI MulDiv(int a, int b, int c)
{
    if (!c)
        return -32768;
    long long p = (long long)a * b;
    long long q = (p < 0) == (c < 0) ? (p + c / 2) / c : (p - c / 2) / c;
    return q < -32768 || q > 32767 ? -32768 : (int)q;
}

BOOL WINAPI Rectangle(HDC h, int l, int t, int r, int b)
{
    DC_OR_RET(h, FALSE);
    RECT box;
    if (shape_box(dc, l, t, r, b, &box))
        draw_shape(dc, box, 0, 0);
    return TRUE;
}

BOOL WINAPI RoundRect(HDC h, int l, int t, int r, int b, int w, int hh)
{
    DC_OR_RET(h, FALSE);
    RECT box;
    if (shape_box(dc, l, t, r, b, &box))
        draw_shape(dc, box, dev_size(dc, w < 0 ? -w : w, 0), dev_size(dc, hh < 0 ? -hh : hh, 1));
    return TRUE;
}

BOOL WINAPI Ellipse(HDC h, int l, int t, int r, int b)
{
    DC_OR_RET(h, FALSE);
    RECT box;
    if (shape_box(dc, l, t, r, b, &box))
        draw_shape(dc, box, box.right - box.left, box.bottom - box.top);
    return TRUE;
}

/* The spans of polygons (device points; npoly of them, counts[i] points
 * each, closed): pixel centres, ALTERNATE (even-odd) or WINDING; span(x0,
 * x1 exclusive, y) for each. */
typedef void (*span_fn)(void *ctx, int x0, int x1, int y);

static int poly_spans(const POINT *dp, const int *counts, int npoly, int mode, span_fn span,
                      void *ctx)
{
    int total = 0;
    for (int p = 0; p < npoly; p++)
        total += counts[p];
    if (total < 2)
        return 1;
    int *xs = (int *)win_alloc(sizeof(int) * (size_t)total);
    int *dir = (int *)win_alloc(sizeof(int) * (size_t)total);
    if (!xs || !dir) {
        win_free(xs);
        win_free(dir);
        return 0;
    }
    int ymin = dp[0].y, ymax = dp[0].y;
    for (int i = 1; i < total; i++) {
        if (dp[i].y < ymin) ymin = dp[i].y;
        if (dp[i].y > ymax) ymax = dp[i].y;
    }
    for (int y = ymin; y < ymax; y++) {
        int k = 0;
        long yc2 = 2L * y + 1;                 /* doubled pixel centre */
        for (int p = 0, base = 0; p < npoly; base += counts[p], p++)
            for (int i = 0; i < counts[p]; i++) {
                POINT a = dp[base + i], c = dp[base + (i + 1) % counts[p]];
                if (a.y == c.y)
                    continue;
                int d = 1;
                if (a.y > c.y) { POINT t = a; a = c; c = t; d = -1; }
                if (yc2 < 2L * a.y || yc2 >= 2L * c.y)
                    continue;
                /* x at the centre line, doubled, then rounded to pixel start */
                long num = (yc2 - 2L * a.y) * (c.x - a.x);
                long x2 = 2L * a.x + num / (c.y - a.y);
                xs[k] = (int)((x2 + 1) >> 1);   /* first pixel whose centre >= x */
                dir[k] = d;
                k++;
            }
        for (int i = 1; i < k; i++)                 /* insertion sort */
            for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) {
                int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
                t = dir[j]; dir[j] = dir[j - 1]; dir[j - 1] = t;
            }
        if (mode == WINDING) {
            int wsum = 0;
            for (int i = 0; i + 1 < k; i++) {
                wsum += dir[i];
                if (wsum && xs[i] < xs[i + 1])
                    span(ctx, xs[i], xs[i + 1], y);
            }
        } else {
            for (int i = 0; i + 1 < k; i += 2)
                if (xs[i] < xs[i + 1])
                    span(ctx, xs[i], xs[i + 1], y);
        }
    }
    win_free(xs);
    win_free(dir);
    return 1;
}

typedef struct { wdc *dc; const wfill *f; int rop; } fill_ctx;

static void fill_span(void *ctx, int x0, int x1, int y)
{
    const fill_ctx *c = (const fill_ctx *)ctx;
    RECT r = { x0, y, x1, y + 1 };
    fill_rect_dev(c->dc, &r, c->f, c->rop);
}

/* Polygons filled with the brush, outlined with the pen */
static BOOL poly_polygon(wdc *dc, const POINT *pts, const int *counts, int npoly)
{
    int total = 0;
    for (int p = 0; p < npoly; p++) {
        if (counts[p] < 2)
            return FALSE;
        total += counts[p];
    }
    if (!total)
        return FALSE;
    POINT *dp = (POINT *)win_alloc(sizeof(POINT) * (size_t)total);
    if (!dp)
        return FALSE;
    for (int i = 0; i < total; i++) {
        dp[i] = pts[i];
        gdi_lp_to_dp(dc, &dp[i].x, &dp[i].y);
    }
    wpen *p = dc_pen(dc);
    RECT b;
    points_bounds(dc, pts, total, p->width, &b);
    gdi_begin_draw(dc, &b);
    wfill bf;
    gdi_realize_brush(dc, dc->brush, &bf);
    if (bf.kind != WFILL_NONE) {
        fill_ctx fc = { dc, &bf, dib_rop2_to_rop3(dc->rop2) };
        poly_spans(dp, counts, npoly, dc->polyfill, fill_span, &fc);
    }
    if (p->style != PS_NULL)
        for (int q = 0, base = 0; q < npoly; base += counts[q], q++)
            for (int i = 0; i < counts[q]; i++) {
                const POINT *a = &dp[base + i], *c = &dp[base + (i + 1) % counts[q]];
                line_dev(dc, p, a->x, a->y, c->x, c->y, 1);
            }
    gdi_end_draw(dc, &b);
    win_free(dp);
    return TRUE;
}

BOOL WINAPI Polygon(HDC h, const POINT *pts, int n)
{
    DC_OR_RET(h, FALSE);
    return n >= 2 && poly_polygon(dc, pts, &n, 1);
}

BOOL WINAPI PolyPolygon(HDC h, const POINT *pts, const int *counts, int npoly)
{
    DC_OR_RET(h, FALSE);
    return npoly > 0 && poly_polygon(dc, pts, counts, npoly);
}

/* ---------------------------------------------------------------- arcs */

/* whether v (y up) is in the sweep counterclockwise from s to e */
static int in_sweep(long long sx, long long sy, long long ex, long long ey, long long vx,
                    long long vy)
{
    long long se = sx * ey - sy * ex;
    if (se >= 0)
        return sx * vy - sy * vx >= 0 && vx * ey - vy * ex >= 0;
    return !(ex * vy - ey * vx > 0 && vx * sy - vy * sx > 0);
}

/* the point of the ellipse (box, exclusive) in the direction of p: the
 * ends of an arc */
static POINT ellipse_point(const RECT *box, POINT p)
{
    long long a2 = box->right - box->left - 1, b2 = box->bottom - box->top - 1;   /* doubled axes */
    long long cx2 = box->left + box->right - 1, cy2 = box->top + box->bottom - 1;
    long long vx = 2LL * p.x - cx2, vy = 2LL * p.y - cy2;
    POINT r = { (int)(cx2 / 2), (int)(cy2 / 2) };
    unsigned long long d = isqrt64((unsigned long long)(vx * vx * b2 * b2 + vy * vy * a2 * a2));
    if (!d)
        return r;
    r.x = (int)((cx2 + vx * a2 * b2 / (long long)d) / 2);
    r.y = (int)((cy2 + vy * a2 * b2 / (long long)d) / 2);
    return r;
}

enum { ARC_ARC, ARC_CHORD, ARC_PIE };

typedef struct {
    wdc *dc;
    const wclip *c;
    int rop;
} run_ctx;

static void run_fill(const run_ctx *r, int x0, int x1, int y, const wfill *f)
{
    if (x0 < x1) {
        RECT rc = { x0, y, x1, y + 1 };
        dib_fill_rect(r->dc->surf, r->c, &rc, f, r->rop);
    }
}

/*
 * Arc, Chord, Pie: the ellipse in box (device, exclusive), the part from the
 * radial through s to the one through e counterclockwise (as it is seen);
 * the arc with the pen, a chord's or a pie's inside with the brush, their
 * straight edges with the pen.
 */
static void draw_arc(wdc *dc, RECT box, POINT s, POINT e, int kind)
{
    wpen *p = dc_pen(dc);
    int has_pen = p->style != PS_NULL;
    int pw = has_pen ? p->width : 0;
    if (!has_pen) {
        box.right--;
        box.bottom--;
    } else if (pw > 1 && p->style != PS_INSIDEFRAME) {
        int o = (pw - 1) / 2;
        InflateRect(&box, o, o);
    }
    if (box.left >= box.right || box.top >= box.bottom)
        return;
    long long cx2 = box.left + box.right - 1, cy2 = box.top + box.bottom - 1;
    long long sx = 2LL * s.x - cx2, sy = -(2LL * s.y - cy2);
    long long ex = 2LL * e.x - cx2, ey = -(2LL * e.y - cy2);
    int full = (!sx && !sy) || (!ex && !ey) || (sx * ey - sy * ex == 0 && sx * ex + sy * ey > 0);
    POINT ps = ellipse_point(&box, s), pe = ellipse_point(&box, e);
    long long cx = ps.x - pe.x, cy = -(ps.y - pe.y);   /* the chord, e to s (y up) */

    wfill bf, pf;
    gdi_realize_brush(dc, dc->brush, &bf);
    memset(&pf, 0, sizeof(pf));
    pf.kind = WFILL_SOLID;
    pf.solid = has_pen ? gdi_color_index(dc, p->color) : 0;
    int fill = kind != ARC_ARC && bf.kind != WFILL_NONE;
    int w = box.right - box.left, h = box.bottom - box.top;
    int iw = w - 2 * pw, ih = h - 2 * pw;

    RECT bounds = box;
    InflateRect(&bounds, pw, pw);
    gdi_begin_draw(dc, &bounds);
    wclip cl;
    run_ctx rc = { dc, gdi_dc_clip(dc, &cl), dib_rop2_to_rop3(dc->rop2) };
    for (int y = box.top; y < box.bottom; y++) {
        int ol, orr, il = 0, ir = -1;
        if (!shape_row(box.left, box.top, box.right, box.bottom, w, h, y, &ol, &orr))
            continue;
        int inner = iw > 0 && ih > 0 &&
                    shape_row(box.left + pw, box.top + pw, box.right - pw, box.bottom - pw,
                              iw, ih, y, &il, &ir) && il <= ir;
        /* runs of one kind: 0 nothing, 1 the pen, 2 the brush */
        int run = 0, from = ol;
        for (int x = ol; x <= orr + 1; x++) {
            int k = 0;
            if (x <= orr) {
                long long vx = 2LL * x - cx2, vy = -(2LL * y - cy2);
                int ring = !inner || x < il || x > ir;
                int swept = full || in_sweep(sx, sy, ex, ey, vx, vy);
                int inside = kind == ARC_PIE ? swept :     /* a chord's: left of e to s */
                             full || cx * -(long long)(y - pe.y) - cy * (x - pe.x) >= 0;
                if (ring && has_pen && swept)
                    k = 1;
                else if (fill && inside && (!ring || !has_pen))
                    k = 2;
            }
            if (k != run || x > orr) {
                if (run)
                    run_fill(&rc, from, x, y, run == 1 ? &pf : &bf);
                run = k;
                from = x;
            }
        }
    }
    if (has_pen && kind != ARC_ARC && !full) {
        POINT c = { (int)(cx2 / 2), (int)(cy2 / 2) };
        if (kind == ARC_PIE) {
            line_dev(dc, p, c.x, c.y, ps.x, ps.y, 0);
            line_dev(dc, p, c.x, c.y, pe.x, pe.y, 0);
        } else {
            line_dev(dc, p, ps.x, ps.y, pe.x, pe.y, 0);
        }
    }
    gdi_end_draw(dc, &bounds);
}

static BOOL arc_call(HDC h, int l, int t, int r, int b, int x1, int y1, int x2, int y2,
                     int kind)
{
    DC_OR_RET(h, FALSE);
    RECT box;
    if (!shape_box(dc, l, t, r, b, &box))
        return TRUE;
    POINT s = { x1, y1 }, e = { x2, y2 };
    gdi_lp_to_dp(dc, &s.x, &s.y);
    gdi_lp_to_dp(dc, &e.x, &e.y);
    draw_arc(dc, box, s, e, kind);
    return TRUE;
}

BOOL WINAPI Arc(HDC h, int l, int t, int r, int b, int x1, int y1, int x2, int y2)
{
    return arc_call(h, l, t, r, b, x1, y1, x2, y2, ARC_ARC);
}

BOOL WINAPI Chord(HDC h, int l, int t, int r, int b, int x1, int y1, int x2, int y2)
{
    return arc_call(h, l, t, r, b, x1, y1, x2, y2, ARC_CHORD);
}

BOOL WINAPI Pie(HDC h, int l, int t, int r, int b, int x1, int y1, int x2, int y2)
{
    return arc_call(h, l, t, r, b, x1, y1, x2, y2, ARC_PIE);
}

/* USER's system colour brushes: FillRect takes COLOR_xxx + 1 as a brush,
 * as in Windows 3.x */
HBRUSH (*gdi_sys_color_brush)(int index);

int WINAPI FillRect(HDC h, const RECT *rc, HBRUSH br)
{
    DC_OR_RET(h, 0);
    if (br && (uintptr_t)br <= COLOR_MAX && gdi_sys_color_brush &&
        !win_handle_get(br, WOBJ_BRUSH))
        br = gdi_sys_color_brush((int)(uintptr_t)br - 1);
    RECT r = *rc;
    lp_rect(dc, &r);
    if (IsRectEmpty(&r))
        return 1;
    wfill f;
    gdi_realize_brush(dc, br, &f);
    gdi_begin_draw(dc, &r);
    fill_rect_dev(dc, &r, &f, 0xF0);
    gdi_end_draw(dc, &r);
    return 1;
}

int WINAPI FrameRect(HDC h, const RECT *rc, HBRUSH br)
{
    DC_OR_RET(h, 0);
    RECT r = *rc;
    lp_rect(dc, &r);
    if (IsRectEmpty(&r))
        return 1;
    wfill f;
    gdi_realize_brush(dc, br, &f);
    gdi_begin_draw(dc, &r);
    RECT e[4] = {
        { r.left, r.top, r.right, r.top + 1 },
        { r.left, r.bottom - 1, r.right, r.bottom },
        { r.left, r.top + 1, r.left + 1, r.bottom - 1 },
        { r.right - 1, r.top + 1, r.right, r.bottom - 1 },
    };
    for (int i = 0; i < 4; i++)
        fill_rect_dev(dc, &e[i], &f, 0xF0);
    gdi_end_draw(dc, &r);
    return 1;
}

void WINAPI InvertRect(HDC h, const RECT *rc)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return;
    RECT r = *rc;
    lp_rect(dc, &r);
    gdi_begin_draw(dc, &r);
    fill_rect_dev(dc, &r, NULL, 0x55);
    gdi_end_draw(dc, &r);
}

/* Dotted XOR frame: drawing it twice restores the screen. */
void WINAPI DrawFocusRect(HDC h, const RECT *rc)
{
    wdc *dc = gdi_dc(h);
    if (!dc)
        return;
    RECT r = *rc;
    lp_rect(dc, &r);
    if (IsRectEmpty(&r))
        return;
    wfill f;
    memset(&f, 0, sizeof(f));
    f.kind = WFILL_PATTERN;
    f.masked = 1;
    for (int i = 0; i < 64; i++) {
        f.pat[i] = dc->surf->bpp == 1 ? 1 : 0xFF;
        f.pat_mask[i] = (uint8_t)(((i >> 3) + (i & 7) + 1) & 1);
    }
    gdi_begin_draw(dc, &r);
    RECT e[4] = {
        { r.left, r.top, r.right, r.top + 1 },
        { r.left, r.bottom - 1, r.right, r.bottom },
        { r.left, r.top + 1, r.left + 1, r.bottom - 1 },
        { r.right - 1, r.top + 1, r.right, r.bottom - 1 },
    };
    for (int i = 0; i < 4; i++)
        fill_rect_dev(dc, &e[i], &f, 0x5A);
    gdi_end_draw(dc, &r);
}

BOOL WINAPI PatBlt(HDC h, int x, int y, int w, int hh, DWORD rop)
{
    DC_OR_RET(h, FALSE);
    RECT r = { x, y, x + w, y + hh };
    lp_rect(dc, &r);
    wfill f;
    gdi_realize_brush(dc, dc->brush, &f);
    gdi_begin_draw(dc, &r);
    fill_rect_dev(dc, &r, &f, (int)((rop >> 16) & 0xFF));
    gdi_end_draw(dc, &r);
    return TRUE;
}

static void blt_colors(wdc *d, wdc *s, uint8_t *fg, uint8_t *bk)
{
    if (s && s->surf->bpp == 1 && d->surf->bpp != 1) {
        *fg = gdi_color_index(d, d->text);
        *bk = gdi_color_index(d, d->bk);
    } else if (s && s->surf->bpp != 1 && d->surf->bpp == 1) {
        *fg = 0;
        *bk = gdi_color_index(s, s->bk);
    } else {
        *fg = *bk = 0;
    }
}

BOOL WINAPI BitBlt(HDC hd, int x, int y, int w, int h, HDC hs, int sx, int sy, DWORD rop)
{
    wdc *d = gdi_dc(hd), *s = gdi_dc(hs);
    if (!d)
        return FALSE;
    int r3 = (int)((rop >> 16) & 0xFF);
    int need_src = ((r3 >> 2) & 0x33) != (r3 & 0x33);
    if (need_src && !s)
        return FALSE;
    /* a mapping mode that scales: both rectangles in pixels, stretched
     * (turned over) if they differ */
    if (d->mapmode != MM_TEXT || (need_src && s->mapmode != MM_TEXT))
        return need_src ? StretchBlt(hd, x, y, w, h, hs, sx, sy, w, h, rop)
                        : PatBlt(hd, x, y, w, h, rop);
    gdi_lp_to_dp(d, &x, &y);
    if (s)
        gdi_lp_to_dp(s, &sx, &sy);
    RECT b = { x, y, x + w, y + h };
    wfill f;
    gdi_realize_brush(d, d->brush, &f);
    uint8_t fg, bk;
    blt_colors(d, s, &fg, &bk);
    wclip cl;
    gdi_begin_draw(d, &b);
    int src_screen = need_src && s->is_screen;
    if (src_screen) {
        RECT sb = { sx, sy, sx + w, sy + h };
        cursor_exclude(&sb);
    }
    dib_blit(d->surf, gdi_dc_clip(d, &cl), x, y, w, h, need_src ? s->surf : NULL,
             sx, sy, &f, r3, fg, bk);
    if (src_screen)
        cursor_unexclude();
    gdi_end_draw(d, &b);
    return TRUE;
}

BOOL WINAPI StretchBlt(HDC hd, int x, int y, int w, int h,
                       HDC hs, int sx, int sy, int sw, int sh, DWORD rop)
{
    wdc *d = gdi_dc(hd), *s = gdi_dc(hs);
    if (!d || !s)
        return FALSE;
    int x2 = x + w, y2 = y + h, sx2 = sx + sw, sy2 = sy + sh;   /* the corners, in pixels */
    gdi_lp_to_dp(d, &x, &y);
    gdi_lp_to_dp(d, &x2, &y2);
    gdi_lp_to_dp(s, &sx, &sy);
    gdi_lp_to_dp(s, &sx2, &sy2);
    w = x2 - x;
    h = y2 - y;
    sw = sx2 - sx;
    sh = sy2 - sy;
    if (w < 0 && sw < 0) {              /* both the other way: not turned over */
        x = x2; w = -w;
        sx = sx2; sw = -sw;
    }
    if (h < 0 && sh < 0) {
        y = y2; h = -h;
        sy = sy2; sh = -sh;
    }
    RECT b = { x < x + w ? x : x + w, y < y + h ? y : y + h,
               x > x + w ? x : x + w, y > y + h ? y : y + h };
    wfill f;
    gdi_realize_brush(d, d->brush, &f);
    uint8_t fg, bk;
    blt_colors(d, s, &fg, &bk);
    wclip cl;
    gdi_begin_draw(d, &b);
    dib_stretch(d->surf, gdi_dc_clip(d, &cl), x, y, w, h, s->surf, sx, sy, sw, sh,
                &f, (int)((rop >> 16) & 0xFF), fg, bk);
    gdi_end_draw(d, &b);
    return TRUE;
}

static BOOL fill_region(wdc *dc, wrgn *g, HBRUSH br, int rop3)
{
    wrgn t;
    wrgn_init(&t);
    wrgn_copy(&t, g);
    wrgn_offset(&t, dc->org_x, dc->org_y);
    wfill f;
    gdi_realize_brush(dc, br, &f);
    gdi_begin_draw(dc, &t.box);
    for (int i = 0; i < t.n; i++)
        fill_rect_dev(dc, &t.r[i], &f, rop3);
    gdi_end_draw(dc, &t.box);
    wrgn_free(&t);
    return TRUE;
}

BOOL WINAPI FillRgn(HDC h, HRGN hr, HBRUSH br)
{
    DC_OR_RET(h, FALSE);
    wrgn *g = gdi_region(hr);
    return g ? fill_region(dc, g, br, 0xF0) : FALSE;
}

BOOL WINAPI PaintRgn(HDC h, HRGN hr)
{
    DC_OR_RET(h, FALSE);
    wrgn *g = gdi_region(hr);
    return g ? fill_region(dc, g, dc->brush, dib_rop2_to_rop3(dc->rop2)) : FALSE;
}

BOOL WINAPI InvertRgn(HDC h, HRGN hr)
{
    DC_OR_RET(h, FALSE);
    wrgn *g = gdi_region(hr);
    return g ? fill_region(dc, g, 0, 0x55) : FALSE;      /* DSTINVERT */
}

/* the region's border w wide, h high, inside it: the region less itself
 * moved each way */
BOOL WINAPI FrameRgn(HDC h, HRGN hr, HBRUSH br, int w, int hh)
{
    DC_OR_RET(h, FALSE);
    wrgn *g = gdi_region(hr);
    if (!g || w < 1 || hh < 1)
        return FALSE;
    wrgn in, t, sh;
    wrgn_init(&in);
    wrgn_init(&t);
    wrgn_init(&sh);
    wrgn_copy(&in, g);
    static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < 4; i++) {
        wrgn_copy(&sh, g);
        wrgn_offset(&sh, d[i][0] * w, d[i][1] * hh);
        wrgn_and(&t, &in, &sh);
        wrgn_copy(&in, &t);
    }
    wrgn_diff(&t, g, &in);
    BOOL ok = fill_region(dc, &t, br, 0xF0);
    wrgn_free(&in);
    wrgn_free(&t);
    wrgn_free(&sh);
    return ok;
}

/* ---------------------------------------------------------------- region shapes */

static HRGN region_new(wrgn **g)
{
    HRGN h = new_obj(WOBJ_REGION, sizeof(**g), (void **)g);
    if (h)
        wrgn_init(*g);
    return h;
}

/* a rounded rectangle's region (an ellipse's: corners as large as it) */
HRGN WINAPI CreateRoundRectRgn(int l, int t, int r, int b, int w, int hh)
{
    wrgn *g;
    HRGN h = region_new(&g);
    if (!h)
        return 0;
    if (l > r) { int x = l; l = r; r = x; }
    if (t > b) { int x = t; t = b; b = x; }
    for (int y = t; y < b; y++) {
        int xl, xr;
        if (shape_row(l, t, r, b, w < 0 ? -w : w, hh < 0 ? -hh : hh, y, &xl, &xr)) {
            RECT rc = { xl, y, xr + 1, y + 1 };
            wrgn_add(g, &rc);
        }
    }
    wrgn_done(g);
    return h;
}

HRGN WINAPI CreateEllipticRgn(int l, int t, int r, int b)
{
    return CreateRoundRectRgn(l, t, r, b, r > l ? r - l : l - r, b > t ? b - t : t - b);
}

HRGN WINAPI CreateEllipticRgnIndirect(const RECT *rc)
{
    return CreateEllipticRgn(rc->left, rc->top, rc->right, rc->bottom);
}

static void region_span(void *ctx, int x0, int x1, int y)
{
    RECT rc = { x0, y, x1, y + 1 };
    wrgn_add((wrgn *)ctx, &rc);
}

HRGN WINAPI CreatePolyPolygonRgn(const POINT *pts, const int *counts, int npoly, int mode)
{
    wrgn *g;
    HRGN h = region_new(&g);
    if (!h)
        return 0;
    for (int i = 0; i < npoly; i++)
        if (counts[i] < 2) {
            npoly = 0;
            break;
        }
    if (npoly > 0)
        poly_spans(pts, counts, npoly, mode, region_span, g);
    wrgn_done(g);
    return h;
}

HRGN WINAPI CreatePolygonRgn(const POINT *pts, int n, int mode)
{
    return CreatePolyPolygonRgn(pts, &n, 1, mode);
}

int WINAPI OffsetClipRgn(HDC h, int x, int y)
{
    DC_OR_RET(h, ERROR);
    if (dc->has_clip) {
        wrgn_offset(&dc->clip, dc->mapmode == MM_TEXT ? x : gdi_lx_to_dx(dc, x),
                    dc->mapmode == MM_TEXT ? y : gdi_ly_to_dy(dc, y));
        dc->eff_dirty = 1;
    }
    return wrgn_type(dc_eff(dc));
}

/* ---------------------------------------------------------------- flood fill */

/*
 * FloodFill, ExtFloodFill: from (x, y) the pixels bounded by colour c
 * (FLOODFILLBORDER), or of colour c (FLOODFILLSURFACE), within the clip,
 * filled with the brush. The spans found first, filled after: the brush
 * may well be of those colours.
 */
BOOL WINAPI ExtFloodFill(HDC h, int x, int y, COLORREF c, UINT type)
{
    DC_OR_RET(h, FALSE);
    gdi_lp_to_dp(dc, &x, &y);
    wrgn *e = dc_eff(dc);
    if (!wrgn_pt_in(e, x, y))
        return FALSE;
    RECT box = e->box;
    int bw = box.right - box.left, bh = box.bottom - box.top;
    uint8_t want = gdi_color_index(dc, c);
    uint8_t *seen = (uint8_t *)win_alloc(((size_t)bw * (size_t)bh + 7) / 8);
    int cap = 256, top = 0;
    POINT *stack = (POINT *)win_alloc(sizeof(POINT) * (size_t)cap);
    wrgn spans;
    wrgn_init(&spans);
    if (!seen || !stack) {
        win_free(seen);
        win_free(stack);
        return FALSE;
    }
    gdi_begin_draw(dc, &box);
#define FF_SEEN(px, py) (seen[((size_t)((py) - box.top) * (size_t)bw + (size_t)((px) - box.left)) >> 3] & \
                         (1u << ((((size_t)((py) - box.top) * (size_t)bw + (size_t)((px) - box.left))) & 7)))
#define FF_MARK(px, py) (seen[((size_t)((py) - box.top) * (size_t)bw + (size_t)((px) - box.left)) >> 3] |= \
                         (uint8_t)(1u << ((((size_t)((py) - box.top) * (size_t)bw + (size_t)((px) - box.left))) & 7)))
#define FF_IN(px, py) (wrgn_pt_in(e, px, py) && !FF_SEEN(px, py) && \
                       ((dib_get_pixel(dc->surf, px, py) == want) == (type == FLOODFILLSURFACE)))
    int ok = FF_IN(x, y);
    if (ok)
        stack[top++] = (POINT){ x, y };
    while (top) {
        POINT p = stack[--top];
        if (!FF_IN(p.x, p.y))
            continue;
        int l = p.x, r = p.x;
        while (l > box.left && FF_IN(l - 1, p.y))
            l--;
        while (r + 1 < box.right && FF_IN(r + 1, p.y))
            r++;
        for (int i = l; i <= r; i++)
            FF_MARK(i, p.y);
        RECT rc = { l, p.y, r + 1, p.y + 1 };
        wrgn_add(&spans, &rc);
        for (int dy = -1; dy <= 1; dy += 2) {
            int ny = p.y + dy;
            if (ny < box.top || ny >= box.bottom)
                continue;
            for (int i = l; i <= r; i++) {
                if (!FF_IN(i, ny))
                    continue;
                if (top == cap) {
                    POINT *n = (POINT *)win_realloc(stack, sizeof(POINT) * (size_t)cap * 2);
                    if (!n)
                        break;
                    stack = n;
                    cap *= 2;
                }
                stack[top++] = (POINT){ i, ny };
                while (i + 1 <= r && FF_IN(i + 1, ny))
                    i++;
            }
        }
    }
#undef FF_SEEN
#undef FF_MARK
#undef FF_IN
    if (dc->is_screen)
        cursor_unexclude();
    win_free(seen);
    win_free(stack);
    wrgn_done(&spans);
    if (ok) {
        wfill f;
        gdi_realize_brush(dc, dc->brush, &f);
        gdi_begin_draw(dc, &spans.box);
        for (int i = 0; i < spans.n; i++)
            fill_rect_dev(dc, &spans.r[i], &f, 0xF0);
        gdi_end_draw(dc, &spans.box);
    }
    wrgn_free(&spans);
    return ok;
}

BOOL WINAPI FloodFill(HDC h, int x, int y, COLORREF c)
{
    return ExtFloodFill(h, x, y, c, FLOODFILLBORDER);
}

/* ---------------------------------------------------------------- the rest */

int WINAPI SetStretchBltMode(HDC h, int m) { DC_OR_RET(h, 0); int o = dc->stretch; dc->stretch = m; return o; }
int WINAPI GetStretchBltMode(HDC h) { DC_OR_RET(h, 0); return dc->stretch; }

int WINAPI SetTextCharacterExtra(HDC h, int n)
{
    DC_OR_RET(h, 0x8000);
    int o = dc->char_extra;
    dc->char_extra = n;
    return o;
}

int WINAPI GetTextCharacterExtra(HDC h) { DC_OR_RET(h, 0x8000); return dc->char_extra; }

/* the extra space for the breaks of the next line (noted, not drawn) */
int WINAPI SetTextJustification(HDC h, int extra, int count)
{
    DC_OR_RET(h, 0);
    dc->break_extra = extra;
    dc->break_count = count;
    return 1;
}

/* a brush's pattern lines up with the brush origin as it is drawn: nothing
 * to undo */
BOOL WINAPI UnrealizeObject(HGDIOBJ obj) { return obj != 0; }

HDC WINAPI CreateIC(LPCSTR driver, LPCSTR device, LPCSTR output, const void *init)
{
    return CreateDC(driver, device, output, init);
}

HBITMAP WINAPI CreateDiscardableBitmap(HDC hdc, int w, int h)
{
    return CreateCompatibleBitmap(hdc, w, h);
}

BOOL WINAPI SetBitmapDimensionEx(HBITMAP hb, int x, int y, LPSIZE old)
{
    wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
    if (!bm)
        return FALSE;
    if (old) {
        old->cx = bm->dim_x;
        old->cy = bm->dim_y;
    }
    bm->dim_x = x;
    bm->dim_y = y;
    return TRUE;
}

BOOL WINAPI GetBitmapDimensionEx(HBITMAP hb, LPSIZE sz)
{
    wbitmap *bm = (wbitmap *)win_handle_get(hb, WOBJ_BITMAP);
    if (!bm)
        return FALSE;
    sz->cx = bm->dim_x;
    sz->cy = bm->dim_y;
    return TRUE;
}

DWORD WINAPI SetBitmapDimension(HBITMAP hb, int x, int y)
{
    SIZE o = { 0, 0 };
    return SetBitmapDimensionEx(hb, x, y, &o) ? MKDW(o.cx, o.cy) : 0;
}

DWORD WINAPI GetBitmapDimension(HBITMAP hb)
{
    SIZE s = { 0, 0 };
    return GetBitmapDimensionEx(hb, &s) ? MKDW(s.cx, s.cy) : 0;
}

/* the entry of the logical palette nearest the colour */
UINT WINAPI GetNearestPaletteIndex(HPALETTE hp, COLORREF c)
{
    wpalette *p = (wpalette *)win_handle_get(hp, WOBJ_PALETTE);
    UINT best = 0;
    long bd = 0x7fffffff;
    for (int i = 0; p && i < p->n; i++) {
        long dr = (long)p->e[i].peRed - GetRValue(c), dg = (long)p->e[i].peGreen - GetGValue(c),
             db = (long)p->e[i].peBlue - GetBValue(c);
        long d = dr * dr + dg * dg + db * db;
        if (d < bd) {
            bd = d;
            best = (UINT)i;
        }
    }
    return best;
}

/* the points of a line (as LineTo has them, the last one not), each to proc */
void WINAPI LineDDA(int x0, int y0, int x1, int y1, LINEDDAPROC proc, LPARAM lp)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int steps = dx > dy ? dx : dy, err = (dx > dy ? dx : -dy) / 2, x = x0, y = y0;
    for (int n = 0; n < steps; n++) {
        proc(x, y, lp);
        int e2 = err;
        if (e2 > -dx) { err -= dy; x += sx; }
        if (e2 < dy) { err += dx; y += sy; }
    }
}
