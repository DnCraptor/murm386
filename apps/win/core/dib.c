/*
 * dib.c - raster engine for 1 bpp (MSB = left) and 8 bpp surfaces.
 *
 * Every operation takes the destination area in surface coordinates and a
 * list of disjoint clip rectangles. Raster operations work bit by bit on
 * pixel values (palette indices), as on a Windows palette device; the
 * system palette is laid out so that ~index is the inverse colour.
 *
 * ROP3 bit k is the result for (P << 2) | (S << 1) | D.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

/* ---------------------------------------------------------------- ROPs */

static uint8_t rop3_generic(int rop, uint8_t p, uint8_t s, uint8_t d)
{
    uint8_t r = 0;
    for (int k = 0; k < 8; k++) {
        if (!(rop & (1 << k)))
            continue;
        uint8_t m = (uint8_t)(((k & 4) ? p : ~p) & ((k & 2) ? s : ~s) & ((k & 1) ? d : ~d));
        r |= m;
    }
    return r;
}

static inline uint8_t rop3_px(int rop, uint8_t p, uint8_t s, uint8_t d)
{
    switch (rop) {
    case 0x00: return 0;
    case 0xFF: return 0xFF;
    case 0xCC: return s;
    case 0x33: return (uint8_t)~s;
    case 0xF0: return p;
    case 0x0F: return (uint8_t)~p;
    case 0xAA: return d;
    case 0x55: return (uint8_t)~d;
    case 0x5A: return p ^ d;
    case 0x66: return s ^ d;
    case 0x88: return s & d;
    case 0xEE: return s | d;
    case 0xC0: return p & s;
    case 0xBB: return (uint8_t)(~s | d);
    case 0x44: return (uint8_t)(s & ~d);
    case 0x11: return (uint8_t)~(s | d);
    case 0xE2: return (uint8_t)((s & p) | (~s & d));   /* DSPDxax */
    case 0xB8: return (uint8_t)((s & d) | (~s & p));   /* PSDPxax */
    default:   return rop3_generic(rop, p, s, d);
    }
}

/* ROP2 (pen P, dest D) as the equivalent ROP3 that ignores S. */
int dib_rop2_to_rop3(int rop2)
{
    int t = (rop2 - 1) & 15, r = 0;
    for (int k = 0; k < 8; k++) {
        int p = (k >> 2) & 1, d = k & 1;
        if (t & (1 << ((p << 1) | d)))
            r |= 1 << k;
    }
    return r;
}

static inline int rop_uses_dst(int rop) { return ((rop >> 1) & 0x55) != (rop & 0x55); }
static inline int rop_uses_src(int rop) { return ((rop >> 2) & 0x33) != (rop & 0x33); }
static inline int rop_uses_pat(int rop) { return ((rop >> 4) & 0x0F) != (rop & 0x0F); }

/* ---------------------------------------------------------------- pixels */

static inline uint8_t px_get(const wsurf *s, int x, int y)
{
    const uint8_t *row = s->bits + y * s->pitch;
    if (s->bpp == 8)
        return row[x];
    return (uint8_t)((row[x >> 3] >> (7 - (x & 7))) & 1);
}

static inline void px_put(wsurf *s, int x, int y, uint8_t v)
{
    uint8_t *row = s->bits + y * s->pitch;
    if (s->bpp == 8) {
        row[x] = v;
        return;
    }
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (v & 1)
        row[x >> 3] |= m;
    else
        row[x >> 3] &= (uint8_t)~m;
}

static inline uint8_t fill_px(const wfill *f, int x, int y, int *draw)
{
    *draw = 1;
    if (f->kind == WFILL_SOLID)
        return f->solid;
    if (f->kind == WFILL_PATTERN) {
        int i = (((y - f->org_y) & 7) << 3) | ((x - f->org_x) & 7);
        if (f->masked && !f->pat_mask[i])
            *draw = 0;
        return f->pat[i];
    }
    return 0;
}

uint8_t dib_get_pixel(const wsurf *s, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height)
        return 0;
    return px_get(s, x, y);
}

/* Surface bounds and clip list intersected with rc, one rect at a time. */
static int clip_next(const wsurf *s, const wclip *c, int *i, const RECT *rc, RECT *out)
{
    RECT surf = { 0, 0, s->width, s->height }, a;
    if (!IntersectRect(&a, rc, &surf))
        return 0;
    if (!c) {
        if (*i)
            return 0;
        (*i)++;
        *out = a;
        return 1;
    }
    while (*i < c->n) {
        if (IntersectRect(out, &a, &c->r[(*i)++]))
            return 1;
    }
    return 0;
}

static int clip_has(const wsurf *s, const wclip *c, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height)
        return 0;
    if (!c)
        return 1;
    POINT p = { x, y };
    for (int i = 0; i < c->n; i++)
        if (PtInRect(&c->r[i], p))
            return 1;
    return 0;
}

void dib_set_pixel(wsurf *s, const wclip *c, int x, int y, uint8_t color, int rop2)
{
    if (!clip_has(s, c, x, y))
        return;
    int rop = dib_rop2_to_rop3(rop2);
    uint8_t v = rop3_px(rop, color, 0, px_get(s, x, y));
    px_put(s, x, y, s->bpp == 1 ? (v & 1) : v);
}

/* ---------------------------------------------------------------- fills */

static void fill_span(wsurf *s, int y, int x0, int x1, const wfill *f, int rop)
{
    if (x0 >= x1)
        return;
    int up = rop_uses_pat(rop), ud = rop_uses_dst(rop);
    int solid = !up || (f->kind == WFILL_SOLID);
    uint8_t p = up && f->kind == WFILL_SOLID ? f->solid : 0;

    if (s->bpp == 8 && solid) {
        uint8_t *row = s->bits + y * s->pitch;
        if (!ud) {
            memset(row + x0, rop3_px(rop, p, 0, 0), (size_t)(x1 - x0));
            return;
        }
        if (rop == 0x55) {
            for (int x = x0; x < x1; x++)
                row[x] = (uint8_t)~row[x];
            return;
        }
        if (rop == 0x5A) {
            for (int x = x0; x < x1; x++)
                row[x] ^= p;
            return;
        }
        for (int x = x0; x < x1; x++)
            row[x] = rop3_px(rop, p, 0, row[x]);
        return;
    }
    for (int x = x0; x < x1; x++) {
        int draw = 1;
        uint8_t pv = up ? fill_px(f, x, y, &draw) : 0;
        if (!draw)
            continue;
        uint8_t d = ud ? px_get(s, x, y) : 0;
        uint8_t v = rop3_px(rop, pv, 0, d);
        px_put(s, x, y, v);
    }
}

void dib_fill_rect(wsurf *s, const wclip *c, const RECT *rc, const wfill *f, int rop3)
{
    static const wfill none = { WFILL_NONE };
    if (!f)
        f = &none;
    if (f->kind == WFILL_NONE && rop_uses_pat(rop3))
        return;
    RECT r;
    int i = 0;
    while (clip_next(s, c, &i, rc, &r))
        for (int y = r.top; y < r.bottom; y++)
            fill_span(s, y, r.left, r.right, f, rop3);
}

void dib_hline(wsurf *s, const wclip *c, int x0, int x1, int y, uint8_t color, int rop2)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    RECT rc = { x0, y, x1, y + 1 }, r;
    wfill f;
    f.kind = WFILL_SOLID;
    f.solid = color;
    int rop = dib_rop2_to_rop3(rop2), i = 0;
    while (clip_next(s, c, &i, &rc, &r))
        fill_span(s, y, r.left, r.right, &f, rop);
}

/* ---------------------------------------------------------------- lines */

/* On/off run lengths in pixels for PS_DASH..PS_DASHDOTDOT. */
static const uint8_t line_styles[4][6] = {
    { 18, 6, 0 },
    { 3, 3, 0 },
    { 9, 6, 3, 6, 0 },
    { 9, 3, 3, 3, 3, 3 },
};

void dib_line(wsurf *s, const wclip *c, int x0, int y0, int x1, int y1,
              uint8_t color, int rop2, int style, int skip_last)
{
    if (style == PS_NULL)
        return;
    int rop = dib_rop2_to_rop3(rop2);
    int mono = s->bpp == 1;

    /* Pick the clip rects that can contain any part of the line. */
    RECT bb = { x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
                (x0 > x1 ? x0 : x1) + 1, (y0 > y1 ? y0 : y1) + 1 };
    RECT local[16];
    wclip lc = { local, 0 };
    const wclip *cc = c;
    if (c && c->n <= 16) {
        for (int i = 0; i < c->n; i++)
            if (IntersectRect(&local[lc.n], &c->r[i], &bb))
                lc.n++;
        if (!lc.n)
            return;
        cc = &lc;
    }

    const uint8_t *pat = (style >= PS_DASH && style <= PS_DASHDOTDOT) ? line_styles[style - 1] : NULL;
    int seg = 0, left = pat ? pat[0] : 0;

    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int steps = dx > dy ? dx : dy;
    if (!skip_last)
        steps++;
    int err = (dx > dy ? dx : -dy) / 2;
    int x = x0, y = y0;
    for (int n = 0; n < steps; n++) {
        int on = !pat || !(seg & 1);
        if (on && clip_has(s, cc, x, y)) {
            uint8_t v = rop3_px(rop, color, 0, px_get(s, x, y));
            px_put(s, x, y, mono ? (v & 1) : v);
        }
        if (pat && --left <= 0) {
            seg++;
            if (seg >= 6 || !pat[seg])
                seg = 0;
            left = pat[seg];
        }
        int e2 = err;
        if (e2 > -dx) { err -= dy; x += sx; }
        if (e2 < dy) { err += dx; y += sy; }
    }
}

/* ---------------------------------------------------------------- blits */

typedef struct {
    const wsurf *s;
    int conv;          /* 0 same depth, 1 mono->color, 2 color->mono */
    uint8_t fg, bk;
} srcmap;

static inline uint8_t src_px(const srcmap *m, int x, int y)
{
    uint8_t v = px_get(m->s, x, y);
    switch (m->conv) {
    case 1: return v ? m->bk : m->fg;
    case 2: return v == m->bk ? 1 : 0;
    default: return v;
    }
}

static void blit_span(wsurf *d, int y, int x0, int x1, const srcmap *m, int ox, int oy,
                      const wfill *pat, int rop, int backwards)
{
    int up = rop_uses_pat(rop), ud = rop_uses_dst(rop), us = rop_uses_src(rop);
    int mono = d->bpp == 1;

    if (rop == 0xCC && m->conv == 0 && d->bpp == 8) {
        memmove(d->bits + y * d->pitch + x0,
                m->s->bits + (y + oy) * m->s->pitch + x0 + ox, (size_t)(x1 - x0));
        return;
    }
    int step = backwards ? -1 : 1;
    int xs = backwards ? x1 - 1 : x0, xe = backwards ? x0 - 1 : x1;
    for (int x = xs; x != xe; x += step) {
        int draw = 1;
        uint8_t pv = up ? fill_px(pat, x, y, &draw) : 0;
        if (!draw)
            continue;
        uint8_t sv = us ? src_px(m, x + ox, y + oy) : 0;
        uint8_t dv = ud ? px_get(d, x, y) : 0;
        uint8_t v = rop3_px(rop, pv, sv, dv);
        px_put(d, x, y, mono ? (v & 1) : v);
    }
}

void dib_blit(wsurf *d, const wclip *c, int dx, int dy, int w, int h,
              const wsurf *s, int sx, int sy, const wfill *pat, int rop3,
              uint8_t fg, uint8_t bk)
{
    static const wfill none = { WFILL_NONE };
    if (!pat)
        pat = &none;
    if (pat->kind == WFILL_NONE && rop_uses_pat(rop3))
        return;
    if (!s || !rop_uses_src(rop3)) {
        RECT rc = { dx, dy, dx + w, dy + h };
        dib_fill_rect(d, c, &rc, pat, rop3);
        return;
    }

    /* Clip the source rectangle to the source surface first. */
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > s->width) w = s->width - sx;
    if (sy + h > s->height) h = s->height - sy;
    if (w <= 0 || h <= 0)
        return;

    srcmap m = { s, 0, fg, bk };
    if (s->bpp == 1 && d->bpp == 8)
        m.conv = 1;
    else if (s->bpp == 8 && d->bpp == 1)
        m.conv = 2;

    int ox = sx - dx, oy = sy - dy;
    RECT rc = { dx, dy, dx + w, dy + h }, r;
    int same = s->bits == d->bits;

    if (!same) {
        int i = 0;
        while (clip_next(d, c, &i, &rc, &r))
            for (int y = r.top; y < r.bottom; y++)
                blit_span(d, y, r.left, r.right, &m, ox, oy, pat, rop3, 0);
        return;
    }

    /* Same surface: walk rows in the direction that never reads a pixel
     * already written, and the spans of one row from the far side. */
    RECT parts[64];
    int n = 0, i = 0;
    while (n < 64 && clip_next(d, c, &i, &rc, &parts[n]))
        n++;
    int bottom_up = oy < 0, right_left = oy == 0 && ox < 0;
    for (int k = 0; k < n; k++)             /* sort by left edge */
        for (int j = k + 1; j < n; j++)
            if (right_left ? parts[j].left > parts[k].left : parts[j].left < parts[k].left) {
                RECT t = parts[k]; parts[k] = parts[j]; parts[j] = t;
            }
    for (int yy = 0; yy < h; yy++) {
        int y = bottom_up ? dy + h - 1 - yy : dy + yy;
        for (int k = 0; k < n; k++) {
            if (y < parts[k].top || y >= parts[k].bottom)
                continue;
            blit_span(d, y, parts[k].left, parts[k].right, &m, ox, oy, pat, rop3, right_left);
        }
    }
}

void dib_stretch(wsurf *d, const wclip *c, int dx, int dy, int dw, int dh,
                 const wsurf *s, int sx, int sy, int sw, int sh,
                 const wfill *pat, int rop3, uint8_t fg, uint8_t bk)
{
    if (dw == sw && dh == sh) {
        dib_blit(d, c, dx, dy, dw, dh, s, sx, sy, pat, rop3, fg, bk);
        return;
    }
    static const wfill none = { WFILL_NONE };
    if (!pat)
        pat = &none;
    if (!dw || !dh || !sw || !sh || (pat->kind == WFILL_NONE && rop_uses_pat(rop3)))
        return;
    /* Mirroring: negative extents flip. */
    int fx = 0, fy = 0;
    if (dw < 0) { dx += dw + 1; dw = -dw; fx ^= 1; }
    if (dh < 0) { dy += dh + 1; dh = -dh; fy ^= 1; }
    if (sw < 0) { sx += sw + 1; sw = -sw; fx ^= 1; }
    if (sh < 0) { sy += sh + 1; sh = -sh; fy ^= 1; }

    srcmap m = { s, 0, fg, bk };
    if (s->bpp == 1 && d->bpp == 8)
        m.conv = 1;
    else if (s->bpp == 8 && d->bpp == 1)
        m.conv = 2;
    int up = rop_uses_pat(rop3), ud = rop_uses_dst(rop3), mono = d->bpp == 1;

    RECT rc = { dx, dy, dx + dw, dy + dh }, r;
    int i = 0;
    while (clip_next(d, c, &i, &rc, &r)) {
        for (int y = r.top; y < r.bottom; y++) {
            int ry = y - dy;
            int syy = sy + (int)(((long)(fy ? dh - 1 - ry : ry) * sh) / dh);
            if (syy < 0 || syy >= s->height)
                continue;
            for (int x = r.left; x < r.right; x++) {
                int rx = x - dx;
                int sxx = sx + (int)(((long)(fx ? dw - 1 - rx : rx) * sw) / dw);
                if (sxx < 0 || sxx >= s->width)
                    continue;
                int draw = 1;
                uint8_t pv = up ? fill_px(pat, x, y, &draw) : 0;
                if (!draw)
                    continue;
                uint8_t v = rop3_px(rop3, pv, src_px(&m, sxx, syy), ud ? px_get(d, x, y) : 0);
                px_put(d, x, y, mono ? (v & 1) : v);
            }
        }
    }
}

/* ---------------------------------------------------------------- glyphs */

void dib_glyph8(wsurf *s, const wclip *c, int x, int y, const uint8_t *rows,
                int h, int w, int skip_left, uint8_t color, int bold)
{
    RECT rc = { x, y, x + w + (bold ? 1 : 0), y + h }, r;
    int i = 0;
    uint8_t v = s->bpp == 1 ? (color & 1) : color;
    while (clip_next(s, c, &i, &rc, &r)) {
        for (int yy = r.top; yy < r.bottom; yy++) {
            unsigned bits = (unsigned)(rows[yy - y] >> skip_left) & ((1u << w) - 1u);
            if (bold)
                bits |= bits << 1;
            if (!bits)
                continue;
            for (int xx = r.left; xx < r.right; xx++)
                if (bits & (1u << (xx - x)))
                    px_put(s, xx, yy, v);
        }
    }
}
