/*
 * fontfile.h - Windows' raster fonts (.FON, .FNT) as faces of fixed sizes.
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
} wface;

/* all the faces (WIN.INI's [fonts] read the first time) */
int font_faces(wface *const **list);
/* a character's glyph: its columns, 8 pixels each, the face's height in
 * bytes one after the other (the leftmost pixel bit 7); its width */
int font_glyph(wface *f, unsigned ch, const uint8_t **cols, int *width);
const uint8_t *font_data(wface *f);

#endif
