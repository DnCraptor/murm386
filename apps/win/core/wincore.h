/*
 * wincore.h - internal structures of the native Windows core.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINCORE_H
#define MURM_WINCORE_H

#include "windows.h"
#include "../ddi/winddi.h"

/* ---------------------------------------------------------------- handles */

enum {
    WOBJ_FREE = 0,
    WOBJ_PEN = OBJ_PEN,          /* 1 */
    WOBJ_BRUSH = OBJ_BRUSH,      /* 2 */
    WOBJ_FONT,
    WOBJ_BITMAP,
    WOBJ_REGION,
    WOBJ_PALETTE,
    WOBJ_DC,
    WOBJ_WINDOW,
    WOBJ_GLOBAL,
    WOBJ_LOCAL,
    WOBJ_CURSOR,
    WOBJ_MENU,
    WOBJ_CLASS,
    WOBJ_ACCEL,
};

HANDLE win_handle_alloc(int type, void *obj);
void  *win_handle_get(HANDLE h, int type);   /* NULL if h is not that type */
int    win_handle_type(HANDLE h);
void   win_handle_free(HANDLE h);
/* Stock objects are never freed by DeleteObject. */
void   win_handle_set_stock(HANDLE h);
int    win_handle_is_stock(HANDLE h);

void  *win_alloc(size_t n);    /* zeroed */
void  *win_realloc(void *p, size_t n);
void   win_free(void *p);
char  *win_strdup(const char *s);

/* ---------------------------------------------------------------- regions */

/* A region is a list of disjoint rectangles plus their bounding box. */
typedef struct wrgn {
    int   n, cap;
    RECT *r;
    RECT  box;
} wrgn;

void wrgn_init(wrgn *g);
void wrgn_clear(wrgn *g);                         /* empty, keeps storage */
void wrgn_free(wrgn *g);
void wrgn_set_rect(wrgn *g, const RECT *rc);
void wrgn_copy(wrgn *d, const wrgn *s);
void wrgn_and(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_or(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_diff(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_xor(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_and_rect(wrgn *g, const RECT *rc);      /* in place */
void wrgn_diff_rect(wrgn *g, const RECT *rc);     /* in place */
void wrgn_or_rect(wrgn *g, const RECT *rc);       /* in place */
void wrgn_offset(wrgn *g, int dx, int dy);
int  wrgn_type(const wrgn *g);
int  wrgn_pt_in(const wrgn *g, int x, int y);
int  wrgn_rect_in(const wrgn *g, const RECT *rc);
int  wrgn_equal(const wrgn *a, const wrgn *b);

/* ---------------------------------------------------------------- raster */

typedef win_surface wsurf;

/* What fills an area: a brush realized for one DC. */
enum { WFILL_NONE, WFILL_SOLID, WFILL_PATTERN };
typedef struct wfill {
    int     kind;
    uint8_t solid;          /* WFILL_SOLID: palette index */
    uint8_t pat[64];        /* WFILL_PATTERN: 8x8 indices */
    uint8_t pat_mask[64];   /* 1 = pixel drawn (hatch with TRANSPARENT bk) */
    int     masked;
    int     org_x, org_y;   /* brush origin, device coordinates */
} wfill;

/* All raster operations clip against a list of device rectangles. */
typedef struct wclip {
    const RECT *r;
    int         n;
} wclip;

/* rop2: R2_* ; rop3: the ROP3 index byte (high word of a raster op). */
int  dib_rop2_to_rop3(int rop2);          /* pen/brush as P, ignores S */
void dib_fill_rect(wsurf *s, const wclip *c, const RECT *rc, const wfill *f, int rop3);
void dib_set_pixel(wsurf *s, const wclip *c, int x, int y, uint8_t color, int rop2);
uint8_t dib_get_pixel(const wsurf *s, int x, int y);
void dib_hline(wsurf *s, const wclip *c, int x0, int x1, int y, uint8_t color, int rop2);
void dib_line(wsurf *s, const wclip *c, int x0, int y0, int x1, int y1,
              uint8_t color, int rop2, int style, int skip_last);
/* src may equal dst (overlap handled). For 1 <-> 8 bpp conversions:
 * mono->color: 1 bits become bk, 0 bits become fg (Windows rule);
 * color->mono: pixels equal to bk become 1. */
void dib_blit(wsurf *d, const wclip *c, int dx, int dy, int w, int h,
              const wsurf *s, int sx, int sy, const wfill *pat, int rop3,
              uint8_t fg, uint8_t bk);
void dib_stretch(wsurf *d, const wclip *c, int dx, int dy, int dw, int dh,
                 const wsurf *s, int sx, int sy, int sw, int sh,
                 const wfill *pat, int rop3, uint8_t fg, uint8_t bk);
/* Draw the 1 bits of a glyph (LSB = leftmost, one byte per row, width<=8)
 * in color; bold adds a second pass one pixel to the right. */
void dib_glyph8(wsurf *s, const wclip *c, int x, int y, const uint8_t *rows,
                int h, int w, int skip_left, uint8_t color, int bold);

/* ---------------------------------------------------------------- GDI */

typedef struct wpen {
    int      style, width;
    COLORREF color;
} wpen;

enum { WBR_NULL, WBR_SOLID, WBR_HATCH, WBR_PATTERN };
typedef struct wbrush {
    int      style;
    COLORREF color;
    int      hatch;
    HBITMAP  pattern;       /* WBR_PATTERN (copied 8x8) */
    uint8_t  pat8[64];      /* pattern indices (8bpp source) */
    uint8_t  patmono[8];    /* pattern rows (1bpp source), LSB unused */
    int      pat_is_mono;
} wbrush;

typedef struct wfont {
    LOGFONT  lf;
    int      fixed;         /* fixed pitch */
    int      bold;
    int      height;        /* realized cell height */
} wfont;

typedef struct wbitmap {
    wsurf    s;
    int      selected;      /* selected into a memory DC */
} wbitmap;

/* Bitmap row size as Win16 reports it (rows padded to 16 bits). */
int      gdi_bitmap_pitch(int w, int bpp);

typedef struct wpalette {
    int          n;
    PALETTEENTRY e[256];
    uint8_t      map[256];  /* logical index -> system palette index */
    int          realized;
} wpalette;

struct wwnd;

typedef struct wdc {
    wsurf   *surf;          /* screen surface or bitmap surface */
    int      is_screen;
    HBITMAP  bitmap;        /* memory DC */
    HWND     hwnd;          /* window DC (USER) */
    int      org_x, org_y;  /* device origin inside the surface */
    int      vp_x, vp_y, win_x, win_y;
    wrgn     vis;           /* visible area, surface coordinates (USER) */
    int      has_clip;
    wrgn     clip;          /* app clip region, surface coordinates */
    wrgn     eff;           /* vis & clip, rebuilt when dirty */
    int      eff_dirty;
    HPEN     pen;
    HBRUSH   brush;
    HFONT    font;
    HPALETTE pal;
    COLORREF text, bk;
    int      bkmode, rop2, polyfill;
    UINT     align;
    int      brush_x, brush_y;
    POINT    cur;
    int      save_level;
    struct wdc *saved;
} wdc;

/* Screen and system state shared by GDI and USER. */
typedef struct wsys {
    const win_display_driver *disp;
    const win_input_driver   *input;
    wsurf     screen;
    int       screen_ok;
    PALETTEENTRY syspal[256];
    HGDIOBJ   stock[17];
    /* software cursor */
    int       cur_x, cur_y, cur_hot_x, cur_hot_y;
    int       cur_shown;        /* ShowCursor counter >= 0 */
    int       cur_drawn;        /* on screen now */
    int       cur_excluded;
    const uint8_t *cur_and, *cur_xor;   /* 32x32 1bpp masks, MSB left */
    uint8_t   cur_save[32 * 32];
    RECT      cur_rect;
} wsys;

extern wsys win_sys;

/* winsys.c */
int      win_init(const char *display, const char *input, int want_w, int want_h, int want_bpp);
void     win_shutdown(void);
int      user_init(void);                        /* user/dialog.c */

/* gdi.c */
int      gdi_init(void);
void     gdi_shutdown(void);
uint8_t  gdi_color_index(wdc *dc, COLORREF c);
wdc     *gdi_dc(HDC h);
HDC      gdi_create_screen_dc(HWND hwnd);
void     gdi_reset_dc(HDC h);                           /* GetDC defaults */
void     gdi_set_dc_origin(HDC h, int x, int y);
void     gdi_set_dc_vis(HDC h, const wrgn *vis);          /* surface coords */
const wclip *gdi_dc_clip(wdc *dc, wclip *tmp);
void     gdi_lp_to_dp(wdc *dc, int *x, int *y);
void     gdi_realize_brush(wdc *dc, HBRUSH br, wfill *f);
wrgn    *gdi_region(HRGN h);
void     gdi_begin_draw(wdc *dc, const RECT *dev_bounds);   /* cursor exclusion */
void     gdi_end_draw(wdc *dc, const RECT *dev_bounds);     /* flush, cursor */
void     gdi_text_init(void);
const wfont *gdi_font(HFONT h);
int      gdi_char_width(const wfont *f, unsigned char ch);

/* cursor.c */
void     cursor_init(void);
void     cursor_move(int x, int y);
void     cursor_exclude(const RECT *rc);
void     cursor_unexclude(void);
void     cursor_set_shape(const uint8_t *and_mask, const uint8_t *xor_mask, int hx, int hy);
int      cursor_show(int show);                 /* ShowCursor counter */
enum { WCUR_ARROW, WCUR_IBEAM, WCUR_WAIT, WCUR_CROSS, WCUR_SIZEWE, WCUR_SIZENS, WCUR_COUNT };
void     cursor_select_builtin(int id);

#endif /* MURM_WINCORE_H */
