/*
 * gdi.c - device contexts, GDI objects, palette and drawing primitives.
 *
 * Only MM_TEXT mapping exists: logical -> device is a translation by
 * (viewport org - window org); device -> surface adds the DC origin, which
 * USER sets to the window position for window DCs. Clipping regions are kept
 * in surface coordinates.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

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
    dc->align = TA_TOP | TA_LEFT | TA_NOUPDATECP;
    dc->vp_x = dc->vp_y = dc->win_x = dc->win_y = 0;
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
            dc->align = s->align; dc->vp_x = s->vp_x; dc->vp_y = s->vp_y;
            dc->win_x = s->win_x; dc->win_y = s->win_y;
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

#define DC_OR_RET(h, r) wdc *dc = gdi_dc(h); if (!dc) return r

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

DWORD WINAPI SetWindowOrg(HDC h, int x, int y)
{
    DC_OR_RET(h, 0);
    DWORD o = MKDW(dc->win_x, dc->win_y);
    dc->win_x = x; dc->win_y = y;
    return o;
}
DWORD WINAPI GetWindowOrg(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->win_x, dc->win_y); }
DWORD WINAPI SetViewportOrg(HDC h, int x, int y)
{
    DC_OR_RET(h, 0);
    DWORD o = MKDW(dc->vp_x, dc->vp_y);
    dc->vp_x = x; dc->vp_y = y;
    return o;
}
DWORD WINAPI GetViewportOrg(HDC h) { DC_OR_RET(h, 0); return MKDW(dc->vp_x, dc->vp_y); }
DWORD WINAPI SetBrushOrg(HDC h, int x, int y)
{
    DC_OR_RET(h, 0);
    DWORD o = MKDW(dc->brush_x, dc->brush_y);
    dc->brush_x = x; dc->brush_y = y;
    return o;
}

COLORREF WINAPI GetNearestColor(HDC h, COLORREF c)
{
    wdc *dc = gdi_dc(h);
    return index_color(dc ? dc->surf : NULL, gdi_color_index(dc, c));
}

/* ---------------------------------------------------------------- clipping */

/* logical -> surface */
void gdi_lp_to_dp(wdc *dc, int *x, int *y)
{
    *x = *x - dc->win_x + dc->vp_x + dc->org_x;
    *y = *y - dc->win_y + dc->vp_y + dc->org_y;
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
    *x = *x - dc->org_x - dc->vp_x + dc->win_x;
    *y = *y - dc->org_y - dc->vp_y + dc->win_y;
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
    if (win_sys.disp && win_sys.disp->flush && IntersectRect(&r, b, &scr) &&
        IntersectRect(&r, &r, &dc_eff(dc)->box))
        win_sys.disp->flush(r.left, r.top, r.right, r.bottom);
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

static wpen *dc_pen(wdc *dc)
{
    wpen *p = (wpen *)win_handle_get(dc->pen, WOBJ_PEN);
    return p ? p : (wpen *)win_handle_get(win_sys.stock[BLACK_PEN], WOBJ_PEN);
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
        draw_shape(dc, box, w < 0 ? -w : w, hh < 0 ? -hh : hh);
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

/* Scanline polygon fill: pixel centres, ALTERNATE (even-odd) or WINDING. */
BOOL WINAPI Polygon(HDC h, const POINT *pts, int n)
{
    DC_OR_RET(h, FALSE);
    if (n < 2)
        return FALSE;
    POINT *dp = (POINT *)win_alloc(sizeof(POINT) * (size_t)(n + 1));
    int *xs = (int *)win_alloc(sizeof(int) * (size_t)n);
    int *dir = (int *)win_alloc(sizeof(int) * (size_t)n);
    if (!dp || !xs || !dir) {
        win_free(dp); win_free(xs); win_free(dir);
        return FALSE;
    }
    for (int i = 0; i < n; i++) {
        dp[i] = pts[i];
        gdi_lp_to_dp(dc, &dp[i].x, &dp[i].y);
    }
    dp[n] = dp[0];
    wpen *p = dc_pen(dc);
    RECT b;
    points_bounds(dc, pts, n, p->width, &b);
    gdi_begin_draw(dc, &b);

    wfill bf;
    gdi_realize_brush(dc, dc->brush, &bf);
    if (bf.kind != WFILL_NONE) {
        int rop = dib_rop2_to_rop3(dc->rop2);
        int ymin = dp[0].y, ymax = dp[0].y;
        for (int i = 1; i < n; i++) {
            if (dp[i].y < ymin) ymin = dp[i].y;
            if (dp[i].y > ymax) ymax = dp[i].y;
        }
        for (int y = ymin; y < ymax; y++) {
            int k = 0;
            long yc2 = 2L * y + 1;                 /* doubled pixel centre */
            for (int i = 0; i < n; i++) {
                POINT a = dp[i], c = dp[i + 1];
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
            if (dc->polyfill == WINDING) {
                int wsum = 0;
                for (int i = 0; i + 1 < k; i++) {
                    wsum += dir[i];
                    if (wsum) {
                        RECT r = { xs[i], y, xs[i + 1], y + 1 };
                        fill_rect_dev(dc, &r, &bf, rop);
                    }
                }
            } else {
                for (int i = 0; i + 1 < k; i += 2) {
                    RECT r = { xs[i], y, xs[i + 1], y + 1 };
                    fill_rect_dev(dc, &r, &bf, rop);
                }
            }
        }
    }
    if (p->style != PS_NULL)
        for (int i = 0; i < n; i++)
            line_dev(dc, p, dp[i].x, dp[i].y, dp[i + 1].x, dp[i + 1].y, 1);
    gdi_end_draw(dc, &b);
    win_free(dp); win_free(xs); win_free(dir);
    return TRUE;
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
    gdi_lp_to_dp(d, &x, &y);
    gdi_lp_to_dp(s, &sx, &sy);
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
