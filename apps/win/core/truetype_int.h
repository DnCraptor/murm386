/*
 * truetype_int.h - what truetype.c and the instructions' interpreter
 * (ttinterp.c) share: a font file's tables, a glyph's points.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WIN_TRUETYPE_INT_H
#define WIN_TRUETYPE_INT_H

#include <stdint.h>
#include "truetype.h"

struct ttfont {
    uint8_t *d;
    uint32_t n;
    uint32_t head, hhea, hmtx, maxp, loca, glyf, name, os2, post, cmap;
    uint32_t loca_len, glyf_len, hmtx_len;
    uint32_t sub;                   /* the character map's subtable (its offset) */
    int      kind;                  /* the map: 1 Unicode, 2 symbol, 3 Macintosh */
    int      loca_long, nglyphs, nhmetrics;
    ttinfo   info;
};

static inline unsigned tt_be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static inline int tt_bes16(const uint8_t *p) { return (int16_t)tt_be16(p); }
static inline uint32_t tt_be32(const uint8_t *p)
{
    return (uint32_t)tt_be16(p) << 16 | tt_be16(p + 2);
}

/* a table's place, 0 if there is none (or it is not all in the file) */
uint32_t tt_tab(const ttfont *f, const char *tag, uint32_t *len);

/* a glyph's bytes in glyf (0: none, an empty one) */
int tt_glyph_at(const ttfont *f, unsigned glyph, const uint8_t **g, const uint8_t **end);

/* its advance and left side bearing (hmtx, font units) */
void tt_hmetrics(const ttfont *f, unsigned glyph, int *advance, int *lsb);

/* a simple glyph's points (font units), on-curve flags (bit 0), contours'
 * last points, instructions; one block of win_alloc's (tt_simple_free) */
typedef struct {
    int       npts, ncont;
    int32_t  *x, *y;
    uint8_t  *on;
    uint16_t *ends;
    const uint8_t *ins;
    unsigned  nins;
    void     *mem;
} ttsimple;

/* g: its bytes (a simple glyph's: contours > 0); 0 if they are not sound */
int tt_simple(const uint8_t *g, const uint8_t *end, ttsimple *s);
void tt_simple_free(ttsimple *s);

/* contours (points in pixels, y up; on: bit 0) to the sink, as TrueType
 * joins them: lines between on-curve points, an off-curve one a curve's
 * control, two in a row an on-curve one between them */
void tt_emit(const float *px, const float *py, const uint8_t *on, const uint16_t *ends,
             int ncont, int npts, tt_sink sink, void *ctx);

#endif
