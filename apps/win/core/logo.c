/*
 * logo.c - the start-up logo.
 *
 * Between the display switching on and USER starting, WINN shows a BMP
 * from the Windows directory: WINLOG16.BMP on a 16-colour display,
 * WINLOGO.BMP otherwise (each falling back to the other). The picture is
 * 8 bpp, plain or RLE8, at most 236 colours: they go into the free middle
 * of the hardware palette (10..245) while the logo is up. It is centred
 * on the colour of its top-left pixel, scaled down when it is larger than
 * the screen. The shell loads and paints behind it: drawing goes to a copy
 * of the screen until the system is first idle (win_logo_end, from the
 * kernel loop) and the logo has been up for SYSTEM.INI [boot] logo.time
 * milliseconds (2000; 0: no logo); then the copy goes to the screen in one
 * piece. "WINN :" starts without it, as "WIN :" did.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

#define LOGO_MAXW 1024

static int      skip;
static int      shown;
static uint32_t shown_at, hold_ms;

void win_logo_skip(void) { skip = 1; }

/* ---------------------------------------------------------------- reading */

typedef struct {
    HFILE   f;
    uint8_t buf[512];
    int     pos, len;
} reader;

static int rd_byte(reader *r)
{
    if (r->pos >= r->len) {
        UINT n = _lread(r->f, r->buf, sizeof(r->buf));
        if (n == (UINT)HFILE_ERROR || !n)
            return -1;
        r->len = (int)n;
        r->pos = 0;
    }
    return r->buf[r->pos++];
}

static int rd_skip(reader *r, long n)
{
    while (n-- > 0)
        if (rd_byte(r) < 0)
            return -1;
    return 0;
}

static uint32_t le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    while (n--)
        v = (v << 8) | p[n];
    return v;
}

/* ---------------------------------------------------------------- drawing */

typedef struct {
    int w, h, bottom_up, ncolors;
    int dx, dy, dw, dh;             /* where it goes on the screen */
    int row;                        /* source rows done */
    uint8_t line[LOGO_MAXW];
} logo;

/* one decoded source row onto the screen rows it covers */
static void put_row(logo *L)
{
    const win_surface *s = &win_sys.screen;
    int sy = L->bottom_up ? L->h - 1 - L->row : L->row;
    L->row++;
    if (sy < 0 || sy >= L->h)
        return;
    int y0 = (sy * L->dh + L->h - 1) / L->h, y1 = ((sy + 1) * L->dh + L->h - 1) / L->h;
    for (int y = y0; y < y1; y++) {
        uint8_t *d = s->bits + (long)(L->dy + y) * s->pitch + L->dx;
        for (int x = 0; x < L->dw; x++) {
            int v = L->line[x * L->w / L->dw];
            d[x] = (uint8_t)(10 + (v < L->ncolors ? v : 0));
        }
    }
}

static int decode(reader *r, logo *L, int rle)
{
    if (!rle) {
        int pad = (4 - (L->w & 3)) & 3;
        for (int y = 0; y < L->h; y++) {
            for (int x = 0; x < L->w; x++) {
                int v = rd_byte(r);
                if (v < 0)
                    return -1;
                L->line[x] = (uint8_t)v;
            }
            if (rd_skip(r, pad))
                return -1;
            put_row(L);
        }
        return 0;
    }
    int x = 0;
    memset(L->line, 0, (size_t)L->w);
    while (L->row < L->h) {
        int n = rd_byte(r), v = rd_byte(r);
        if (n < 0 || v < 0)
            return -1;
        if (n) {                                    /* a run */
            while (n-- && x < L->w)
                L->line[x++] = (uint8_t)v;
            continue;
        }
        if (v == 0 || v == 1) {                     /* end of line, of bitmap */
            put_row(L);
            x = 0;
            memset(L->line, 0, (size_t)L->w);
            if (v == 1)
                break;
        } else if (v == 2) {                        /* delta */
            int ddx = rd_byte(r), ddy = rd_byte(r);
            if (ddx < 0 || ddy < 0)
                return -1;
            while (ddy-- > 0 && L->row < L->h) {
                put_row(L);
                memset(L->line, 0, (size_t)L->w);
            }
            x += ddx;
        } else {                                    /* literal pixels */
            for (int k = 0; k < v; k++) {
                int p = rd_byte(r);
                if (p < 0)
                    return -1;
                if (x < L->w)
                    L->line[x++] = (uint8_t)p;
            }
            if ((v & 1) && rd_byte(r) < 0)
                return -1;
        }
    }
    while (L->row < L->h) {                         /* rows left out: background */
        put_row(L);
        memset(L->line, 0, (size_t)L->w);
    }
    return 0;
}

static int show_file(const char *name)
{
    char path[128];
    win_path(name, path, sizeof(path));
    static reader r;
    memset(&r, 0, sizeof(r));
    r.f = _lopen(path, OF_READ);
    if (r.f == HFILE_ERROR)
        return 0;
    static logo L;
    memset(&L, 0, sizeof(L));
    uint8_t hd[54];
    int ok = 0;
    for (int i = 0; i < 54; i++) {
        int b = rd_byte(&r);
        if (b < 0)
            goto done;
        hd[i] = (uint8_t)b;
    }
    uint32_t off = le(hd + 10, 4), isz = le(hd + 14, 4), comp = le(hd + 30, 4), used = le(hd + 46, 4);
    int32_t w = (int32_t)le(hd + 18, 4), h = (int32_t)le(hd + 22, 4);
    if (hd[0] != 'B' || hd[1] != 'M' || isz < 40 || le(hd + 28, 2) != 8 || (comp != 0 && comp != 1) ||
        w <= 0 || w > LOGO_MAXW || h == 0)
        goto done;
    L.bottom_up = h > 0;
    L.w = w;
    L.h = h > 0 ? h : -h;
    L.ncolors = used ? (int)used : 256;
    if (L.ncolors > 236)
        goto done;
    if (comp == 1 && !L.bottom_up)
        goto done;
    /* the colours, into the middle of the hardware palette */
    if (rd_skip(&r, (long)isz - 40))
        goto done;
    uint8_t rgb[236 * 3];
    for (int i = 0; i < L.ncolors; i++) {
        int b = rd_byte(&r), g = rd_byte(&r), rr = rd_byte(&r), x = rd_byte(&r);
        if (x < 0)
            goto done;
        rgb[3 * i] = (uint8_t)rr;
        rgb[3 * i + 1] = (uint8_t)g;
        rgb[3 * i + 2] = (uint8_t)b;
    }
    long at = 54 + (long)isz - 40 + 4L * L.ncolors;
    if ((long)off < at || rd_skip(&r, (long)off - at))
        goto done;

    const win_surface *s = &win_sys.screen;
    L.dw = L.w;
    L.dh = L.h;
    if (L.dw > s->width || L.dh > s->height) {
        if ((long)L.w * s->height > (long)L.h * s->width) {
            L.dw = s->width;
            L.dh = (int)((long)L.h * s->width / L.w);
        } else {
            L.dh = s->height;
            L.dw = (int)((long)L.w * s->height / L.h);
        }
        if (L.dw < 1) L.dw = 1;
        if (L.dh < 1) L.dh = 1;
    }
    L.dx = (s->width - L.dw) / 2;
    L.dy = (s->height - L.dh) / 2;
    if (win_sys.disp->set_palette)
        win_sys.disp->set_palette(10, L.ncolors, rgb);
    if (decode(&r, &L, comp == 1))
        goto done;
    /* the rest of the screen in the colour of the top-left pixel */
    uint8_t bg = s->bits[(long)L.dy * s->pitch + L.dx];
    for (int y = 0; y < s->height; y++) {
        uint8_t *d = s->bits + (long)y * s->pitch;
        if (y < L.dy || y >= L.dy + L.dh) {
            memset(d, bg, (size_t)s->width);
        } else {
            memset(d, bg, (size_t)L.dx);
            memset(d + L.dx + L.dw, bg, (size_t)(s->width - L.dx - L.dw));
        }
    }
    ok = 1;
done:
    _lclose(r.f);
    return ok;
}

/* ---------------------------------------------------------------- start, end */

void win_logo_show(void)
{
    hold_ms = GetPrivateProfileInt("boot", "logo.time", 2000, "SYSTEM.INI");
    if (skip || !hold_ms || !win_sys.screen_ok)
        return;
    cursor_show(FALSE);
    const win_display_driver *d = win_sys.disp;
    int few = d->ddi_version >= 2 && d->colors && d->colors <= 16;
    shown = show_file(few ? "WINLOG16.BMP" : "WINLOGO.BMP") ||
            show_file(few ? "WINLOGO.BMP" : "WINLOG16.BMP");
    if (!shown) {
        gdi_palette_reset();            /* a file that broke off half way */
        cursor_show(TRUE);
        return;
    }
    if (d->flush)
        d->flush(0, 0, win_sys.screen.width, win_sys.screen.height);
    shown_at = GetTickCount();
    /* from here on the screen is drawn in a copy, behind the logo */
    size_t size = (size_t)win_sys.screen.pitch * (size_t)win_sys.screen.height;
    uint8_t *copy = (uint8_t *)win_alloc(size);     /* zeroed */
    if (copy) {
        win_sys.screen_real = win_sys.screen.bits;
        win_sys.screen.bits = copy;
    } else {
        win_logo_end();                 /* no memory for it: the logo goes first */
    }
}

/* The system is idle (the shell has painted) or ending: once the logo has
 * been up long enough, the system palette comes back and the screen drawn
 * behind it replaces it. */
void win_logo_end(void)
{
    if (!shown)
        return;
    shown = 0;
    while (GetTickCount() - shown_at < hold_ms && win_sys.input)
        win_sys.input->idle();
    win_surface *s = &win_sys.screen;
    gdi_palette_reset();
    if (win_sys.screen_real) {
        uint8_t *copy = s->bits;
        memcpy(win_sys.screen_real, copy, (size_t)s->pitch * (size_t)s->height);
        s->bits = win_sys.screen_real;
        win_sys.screen_real = NULL;
        win_free(copy);
    } else {
        for (int y = 0; y < s->height; y++)
            memset(s->bits + (long)y * s->pitch, 0, (size_t)s->width);
    }
    if (win_sys.disp->flush)
        win_sys.disp->flush(0, 0, s->width, s->height);
    cursor_show(TRUE);
}
