/*
 * fontfile.h - Windows' fonts: raster ones (.FON, .FNT) as faces of fixed
 * sizes, TrueType ones (.TTF, .FOT) as faces of any size.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WIN_FONTFILE_H
#define WIN_FONTFILE_H

#include <stdint.h>

/* a face at one size: the font file's, and what its header says */
typedef struct wface {
    char     name[LF_FACESIZE];
    char     file[128];
    int      nth;                   /* its RT_FONT resource; -1: a .FNT */
    int      v3;
    int      points, height, ascent, ileading, eleading;
    int      italic, weight, charset, pixwidth, pitch_family;
    int      avgw, maxw, first, last, defchar, breakchar;
    int      used;                  /* realized: not removed */
    int      bad;
    uint8_t *data;                  /* the resource, once drawn */
    uint32_t size;
    int      tt;                    /* TrueType (truetype.h): any size, height 0 */
    int      cyrillic;              /* has Windows-1251's letters (a raster one: as is) */
    struct ttfont *ttf;             /* its font, once drawn */
} wface;

/* a TrueType face at one height: its metrics (pixels) and the glyphs
 * drawn so far (fontfile.c keeps a few of them, the latest used) */
typedef struct {
    int      height, ascent, ileading, eleading, avgw, maxw;
} wttsize;

/* all the faces (WIN.INI's [fonts] read the first time) */
int font_faces(wface *const **list);
/* a character's glyph: its columns, 8 pixels each, the face's height in
 * bytes one after the other (the leftmost pixel bit 7); its width */
int font_glyph(wface *f, unsigned ch, const uint8_t **cols, int *width);
const uint8_t *font_data(wface *f);

/* a TrueType face at height key (pixels: < 0 the characters', > 0 the
 * cell's, 0 the default) - its metrics; NULL if the font cannot be read */
const wttsize *font_tt_size(wface *f, int key);
/* a character's advance at that height (pixels) */
int font_tt_width(wface *f, int key, unsigned char ch);
/* its glyph: rows, MSB the leftmost pixel, (w + 7) / 8 bytes each; its
 * left from the origin and top above the baseline; 0 if it has none */
int font_tt_glyph(wface *f, int key, unsigned char ch, const uint8_t **rows, int *w, int *h,
                  int *x0, int *top);
/* the face's font (TrueType), read the first time */
struct ttfont *font_tt(wface *f);
/* is there a TrueType face */
int font_tt_any(void);
/* CreateScalableFontResource's: a .FOT file naming a TrueType one (path
 * its directory, or NULL); 0 if that is not TrueType or fot not written */
int font_make_fot(const char *fot, const char *ttf, const char *path);
/* the size's scale (pixels per font unit; 0: none) */
float font_tt_scale(wface *f, int key);
/* the size's hints (NULL: none) */
struct tthint *font_tt_hint(wface *f, int key);
/* WINNTRAC.LOG's line on them: files read, sizes, glyphs drawn, times */
void font_tt_stats(char *line);

#endif
