/*
 * truetype.h - TrueType fonts (.TTF; .FOT files naming them): a font file
 * read and its glyphs drawn at any size, black and white as Windows 3.1
 * draws them on a VGA (no smoothing, hinted, dropout control), its
 * characters Windows-1251 (the ANSI WINN has) through the font's Unicode
 * character map (or its symbol one, F000h + the character).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WIN_TRUETYPE_H
#define WIN_TRUETYPE_H

#include <stdint.h>

typedef struct ttfont ttfont;

/* what the font says of itself */
typedef struct {
    char     family[32];            /* its name table's family (Windows-1251) */
    char     full[64];              /* its full name */
    char     style[32];             /* its subfamily: "Regular", "Bold" ... */
    char     unique[64];            /* its unique name (the full one without) */
    int      em;                    /* units per em */
    int      ascent, descent;       /* OS/2's usWinAscent, usWinDescent (units) */
    int      line_gap;              /* what hhea's line gap leaves outside them */
    int      avgw, maxw;            /* OS/2's average width, hhea's largest advance */
    int      weight, italic, fixed, symbol;
    int      family_class;          /* FF_ROMAN, FF_SWISS ... */
    int      cyrillic;              /* its map has Windows-1251's letters */
} ttinfo;

/* the rest of what it says, in font units (OUTLINETEXTMETRIC's) */
typedef struct {
    uint8_t  panose[10];
    int      fs_type, fs_selection;
    int      slope_rise, slope_run;     /* the caret's */
    int      italic_angle;              /* tenths of a degree */
    int      typo_ascender, typo_descender, typo_line_gap;
    int      cap_height, x_height;
    int      xmin, ymin, xmax, ymax;    /* every glyph's box */
    int      mac_ascent, mac_descent, mac_line_gap;
    int      min_ppem;
    int      sub_x, sub_y, sub_xoff, sub_yoff, sup_x, sup_y, sup_xoff, sup_yoff;
    int      strike_size, strike_pos, under_size, under_pos;
} ttmetrics;

/* a font file's bytes made a font (the bytes kept; NULL: not TrueType) */
ttfont *tt_open(uint8_t *data, uint32_t size);
void tt_close(ttfont *f);           /* the bytes freed too */
const ttinfo *tt_info(const ttfont *f);
void tt_metrics(const ttfont *f, ttmetrics *m);
/* is it a TrueType file (its first bytes) */
int tt_is_ttf(const uint8_t *d, uint32_t n);

/* a character's glyph index (0: the missing one) */
unsigned tt_glyph_index(const ttfont *f, unsigned char ch);
/* its advance width in font units */
int tt_advance(const ttfont *f, unsigned glyph);

/* a glyph's outline, its points (x, y) made (m0 x + m2 y, m1 x + m3 y),
 * pixels, y up: each contour a TT_MOVE to its start, then TT_LINE to a
 * point, TT_QUAD through a control point (x0, y0) to (x1, y1), back to the
 * start, and TT_CLOSE */
enum { TT_MOVE, TT_LINE, TT_QUAD, TT_CLOSE };
typedef void (*tt_sink)(void *ctx, int op, float x0, float y0, float x1, float y1);
void tt_outline(const ttfont *f, unsigned glyph, const float m[4], tt_sink sink, void *ctx);

/* a glyph drawn at scale (pixels per font unit): its rows, MSB the
 * leftmost pixel, (w + 7) / 8 bytes each (win_alloc's, the caller's to
 * free; NULL for an empty one); w, h; x0 its left from the origin, top
 * its top above the baseline (pixels) */
uint8_t *tt_render(const ttfont *f, unsigned glyph, float scale, int *w, int *h, int *x0,
                   int *top);
/* the same through a matrix (as tt_outline's) */
uint8_t *tt_render_m(const ttfont *f, unsigned glyph, const float m[4], int *w, int *h,
                     int *x0, int *top);

/* dropout control (SCANTYPE): none, simple (the left or lower pixel),
 * smart (the one nearest the span's middle); each with the stubs (a
 * stroke's end between two scan lines) or without */
enum {
    TT_DROPOUT_NONE, TT_DROPOUT_SIMPLE, TT_DROPOUT_SIMPLE_NOSTUBS, TT_DROPOUT_SMART,
    TT_DROPOUT_SMART_NOSTUBS
};

/* the font's hints (ttinterp.c) at a size: its instructions (fpgm, prep)
 * run for ppem pixels to the em; NULL if there is no memory */
typedef struct tthint tthint;
tthint *tt_hint_new(const ttfont *f, int ppem);
void tt_hint_free(tthint *h);
/* a glyph hinted: its outline (pixels, y up, its origin at 0) to the sink,
 * its advance (whole pixels), the dropout control its hints ask for;
 * 0 if it has none to draw (sink unused) */
int tt_hint_glyph(tthint *h, unsigned glyph, tt_sink sink, void *ctx, int *advance,
                  int *dropout);
/* a glyph's path: its points (26.6, y up), on the curve (bit 0), its
 * contours' last points; one win_alloc block (tt_path_free) */
typedef struct {
    int       n, ncont;
    int32_t  *x, *y;
    uint8_t  *on;
    uint16_t *ends;
} ttpath;

/* the glyph hinted (ttinterp.c): its path from its origin, its advance
 * (pixels), the dropout control its hints ask for; 0 if it cannot be */
int tt_hint_path(tthint *h, unsigned glyph, ttpath *out, int *advance, int *dropout);
int tt_hint_ppem(const tthint *h);
void tt_path_free(ttpath *p);

/* the glyph drawn hinted (as tt_render), its advance */
uint8_t *tt_render_hinted(tthint *h, unsigned glyph, int *w, int *hh, int *x0, int *top,
                          int *advance);
/* the hdmx table's advance (pixels) of the glyph at ppem; -1 if none */
int tt_hdmx(const ttfont *f, int ppem, unsigned glyph);

/* the kerning table's pairs (format 0's, horizontal): each its glyphs and
 * value (font units) to pair(); their number */
int tt_kern(const ttfont *f, void (*pair)(void *ctx, unsigned left, unsigned right, int value),
            void *ctx);

/* a table's bytes (tag "cmap" ...; 0 the whole file) from offset, at most
 * size of them (size 0: how many there are); -1 if there is none */
long tt_table(const ttfont *f, uint32_t tag, uint32_t offset, void *out, uint32_t size);

#endif
