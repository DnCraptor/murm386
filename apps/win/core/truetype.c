/*
 * truetype.c - TrueType fonts: the tables a font file has (head, hhea,
 * hmtx, maxp, loca, glyf, cmap, name, OS/2, post), its characters' glyphs
 * (simple and composite ones, their quadratic curves made lines) drawn
 * black and white: a pixel set when its centre is inside the outline (the
 * non-zero winding rule), and the parts thinner than a pixel, which no
 * centre is inside, still a pixel (dropout control, rows and columns), as
 * Windows draws its TrueType fonts on a VGA. No hinting.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>

#include "wincore.h"
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

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static int bes16(const uint8_t *p) { return (int16_t)be16(p); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

/* Windows-1251 80h..FFh in Unicode */
static const uint16_t cp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};

static unsigned ansi_unicode(unsigned char c)
{
    if (c < 0x80)
        return c;
    if (c >= 0xC0)                  /* А..я */
        return 0x0410u + (c - 0xC0u);
    return cp1251[c - 0x80];
}

static char unicode_ansi(unsigned u)
{
    if (u < 0x80)
        return (char)u;
    if (u >= 0x0410 && u <= 0x044F)
        return (char)(0xC0u + (u - 0x0410u));
    for (unsigned i = 0; i < 0x40; i++)
        if (cp1251[i] == u)
            return (char)(0x80u + i);
    return '?';
}

int tt_is_ttf(const uint8_t *d, uint32_t n)
{
    return n >= 12 && (be32(d) == 0x00010000u || be32(d) == 0x74727565u);   /* 1.0, "true" */
}

/* a table's place, 0 if there is none (or it is not all in the file) */
static uint32_t table(const ttfont *f, const char *tag, uint32_t *len)
{
    unsigned count = be16(f->d + 4);
    for (unsigned i = 0; i < count && 12u + 16u * (i + 1) <= f->n; i++) {
        const uint8_t *r = f->d + 12 + 16 * i;
        if (memcmp(r, tag, 4))
            continue;
        uint32_t at = be32(r + 8), n = be32(r + 12);
        if (at > f->n || n > f->n - at)
            return 0;
        if (len)
            *len = n;
        return at;
    }
    return 0;
}

/* ---------------------------------------------------------------- characters */

static unsigned map_lookup(const ttfont *f, uint32_t code)
{
    const uint8_t *s = f->d + f->sub, *end = f->d + f->n;
    unsigned format = be16(s);
    if (format == 0) {
        return code < 256 && s + 6 + 256 <= end ? s[6 + code] : 0;
    }
    if (format == 6) {
        unsigned first = be16(s + 6), count = be16(s + 8);
        if (code < first || code >= first + count || s + 10 + 2 * count > end)
            return 0;
        return be16(s + 10 + 2 * (code - first));
    }
    if (format == 4) {
        unsigned segs = be16(s + 6) / 2;
        const uint8_t *ends = s + 14, *starts = ends + 2 * segs + 2;
        const uint8_t *deltas = starts + 2 * segs, *ranges = deltas + 2 * segs;
        if (code > 0xffff || ranges + 2 * segs > end)
            return 0;
        for (unsigned i = 0; i < segs; i++) {
            if (code > be16(ends + 2 * i))
                continue;
            unsigned start = be16(starts + 2 * i);
            if (code < start)
                return 0;
            unsigned range = be16(ranges + 2 * i), g;
            if (!range) {
                g = (code + be16(deltas + 2 * i)) & 0xffffu;
            } else {
                const uint8_t *p = ranges + 2 * i + range + 2 * (code - start);
                if (p + 2 > end)
                    return 0;
                g = be16(p);
                if (g)
                    g = (g + be16(deltas + 2 * i)) & 0xffffu;
            }
            return g;
        }
        return 0;
    }
    if (format == 12) {
        uint32_t groups = be32(s + 12);
        for (uint32_t i = 0; i < groups && s + 16 + 12 * (i + 1) <= end; i++) {
            const uint8_t *gr = s + 16 + 12 * i;
            if (code >= be32(gr) && code <= be32(gr + 4))
                return be32(gr + 8) + (code - be32(gr));
        }
    }
    return 0;
}

unsigned tt_glyph_index(const ttfont *f, unsigned char ch)
{
    unsigned g = 0;
    if (!f->sub)
        return 0;
    if (f->kind == 2) {                             /* symbol: F000h + the code */
        g = map_lookup(f, 0xF000u | ch);
        if (!g)
            g = map_lookup(f, ch);
    } else if (f->kind == 1) {
        g = map_lookup(f, ansi_unicode(ch));
        if (!g && ch >= 0x80)                       /* a font without Cyrillic: Latin-1 */
            g = map_lookup(f, ch);
    } else {
        g = map_lookup(f, ch);
    }
    return g < (unsigned)f->nglyphs ? g : 0;
}

int tt_advance(const ttfont *f, unsigned glyph)
{
    if (!f->hmtx || !f->nhmetrics)
        return f->info.em / 2;
    unsigned i = glyph < (unsigned)f->nhmetrics ? glyph : (unsigned)f->nhmetrics - 1;
    return 4u * i + 2 <= f->hmtx_len ? (int)be16(f->d + f->hmtx + 4 * i) : 0;
}

/* ---------------------------------------------------------------- the font */

/* a name of the name table's (1 family, 2 style, 4 full): the Windows
 * Unicode one (English first), else the Macintosh one */
static void name_of(const ttfont *f, unsigned id, char *out, int cap)
{
    out[0] = 0;
    if (!f->name)
        return;
    const uint8_t *t = f->d + f->name;
    unsigned count = be16(t + 2), strings = be16(t + 4);
    int best = -1, best_score = 0;
    for (unsigned i = 0; i < count && f->name + 6 + 12 * (i + 1) <= f->n; i++) {
        const uint8_t *r = t + 6 + 12 * i;
        if (be16(r + 6) != id)
            continue;
        unsigned plat = be16(r), enc = be16(r + 2), lang = be16(r + 4);
        int score = plat == 3 && (enc == 1 || enc == 0) ? (lang == 0x0409 ? 4 : 3) :
                    plat == 1 && enc == 0 ? 2 : 0;
        if (score > best_score) {
            best = (int)i;
            best_score = score;
        }
    }
    if (best < 0)
        return;
    const uint8_t *r = t + 6 + 12 * best;
    unsigned len = be16(r + 8);
    uint32_t at = f->name + strings + be16(r + 10);
    if (at > f->n || len > f->n - at)
        return;
    int k = 0;
    if (be16(r) == 3) {
        for (unsigned i = 0; i + 1 < len && k < cap - 1; i += 2)
            out[k++] = unicode_ansi(be16(f->d + at + i));
    } else {
        for (unsigned i = 0; i < len && k < cap - 1; i++)
            out[k++] = (char)f->d[at + i];
    }
    out[k] = 0;
}

ttfont *tt_open(uint8_t *data, uint32_t size)
{
    if (!data || !tt_is_ttf(data, size))
        return NULL;
    ttfont *f = (ttfont *)win_alloc(sizeof(*f));
    if (!f)
        return NULL;
    f->d = data;
    f->n = size;
    uint32_t len;
    f->head = table(f, "head", &len);
    if (len < 54)
        f->head = 0;
    f->hhea = table(f, "hhea", &len);
    if (len < 36)
        f->hhea = 0;
    f->maxp = table(f, "maxp", &len);
    if (len < 6)
        f->maxp = 0;
    f->loca = table(f, "loca", &f->loca_len);
    f->glyf = table(f, "glyf", &f->glyf_len);
    f->hmtx = table(f, "hmtx", &f->hmtx_len);
    f->cmap = table(f, "cmap", &len);
    f->name = table(f, "name", &len);
    f->os2 = table(f, "OS/2", &len);
    if (len < 78)
        f->os2 = 0;
    f->post = table(f, "post", &len);
    if (len < 16)
        f->post = 0;
    if (!f->head || !f->hhea || !f->maxp || !f->loca || !f->glyf || !f->cmap) {
        win_free(f);
        return NULL;
    }
    const uint8_t *d = f->d;
    f->nglyphs = (int)be16(d + f->maxp + 4);
    f->nhmetrics = (int)be16(d + f->hhea + 34);
    f->loca_long = bes16(d + f->head + 50) != 0;
    /* the character map: Unicode, the symbol one, the Macintosh one */
    unsigned maps = be16(d + f->cmap + 2);
    int best = 0;
    for (unsigned i = 0; i < maps && f->cmap + 4 + 8 * (i + 1) <= f->n; i++) {
        const uint8_t *r = d + f->cmap + 4 + 8 * i;
        unsigned plat = be16(r), enc = be16(r + 2);
        uint32_t at = f->cmap + be32(r + 4);
        if (at + 8 > f->n)
            continue;
        int score = plat == 3 && enc == 1 ? 5 : plat == 3 && enc == 10 ? 4 :
                    plat == 0 ? 3 : plat == 3 && enc == 0 ? 2 : plat == 1 && enc == 0 ? 1 : 0;
        unsigned format = be16(d + at);
        if (format != 0 && format != 4 && format != 6 && format != 12)
            score = 0;
        if (score > best) {
            best = score;
            f->sub = at;
            f->kind = score >= 3 ? 1 : score == 2 ? 2 : 3;
        }
    }
    ttinfo *in = &f->info;
    in->em = (int)be16(d + f->head + 18);
    if (in->em < 16 || in->em > 16384)
        in->em = 2048;
    int hasc = bes16(d + f->hhea + 4), hdesc = bes16(d + f->hhea + 6), gap = bes16(d + f->hhea + 8);
    in->maxw = (int)be16(d + f->hhea + 10);
    unsigned style = be16(d + f->head + 44);        /* macStyle: bold 1, italic 2 */
    in->weight = style & 1 ? 700 : 400;
    in->italic = (style & 2) != 0;
    in->ascent = hasc;
    in->descent = -hdesc;
    in->avgw = in->maxw / 2;
    int fam_class = 0, panose_family = 0, panose_serif = 0;
    if (f->os2) {
        const uint8_t *o = d + f->os2;
        in->avgw = bes16(o + 2);
        in->weight = (int)be16(o + 4);
        fam_class = (int)(be16(o + 30) >> 8);
        panose_family = o[32];
        panose_serif = o[33];
        if (be16(o + 62) & 1)
            in->italic = 1;
        in->ascent = (int)be16(o + 74);
        in->descent = (int)be16(o + 76);
    }
    if (in->ascent + in->descent <= 0) {
        in->ascent = in->em * 4 / 5;
        in->descent = in->em / 5;
    }
    int outside = gap - ((in->ascent + in->descent) - (hasc - hdesc));
    in->line_gap = outside > 0 ? outside : 0;
    in->fixed = f->post && be32(d + f->post + 12) != 0;
    in->symbol = f->kind == 2;
    in->family_class = in->fixed ? 0x30 :                       /* FF_MODERN */
        (fam_class >= 1 && fam_class <= 7 && fam_class != 6) ? 0x10 :   /* FF_ROMAN */
        fam_class == 8 ? 0x20 : fam_class == 10 ? 0x40 :        /* FF_SWISS, FF_SCRIPT */
        fam_class == 12 || fam_class == 9 ? 0x50 :              /* FF_DECORATIVE */
        panose_family == 2 ? (panose_serif >= 11 && panose_serif <= 13 ? 0x20 : 0x10) :
        panose_family == 3 ? 0x40 : panose_family == 4 || panose_family == 5 ? 0x50 : 0;
    name_of(f, 1, in->family, sizeof(in->family));
    name_of(f, 2, in->style, sizeof(in->style));
    name_of(f, 4, in->full, sizeof(in->full));
    name_of(f, 3, in->unique, sizeof(in->unique));
    if (!in->unique[0])
        lstrcpyn(in->unique, in->full, sizeof(in->unique));
    if (!in->family[0] || !f->sub) {
        win_free(f);
        return NULL;
    }
    return f;
}

void tt_close(ttfont *f)
{
    if (f) {
        win_free(f->d);
        win_free(f);
    }
}

const ttinfo *tt_info(const ttfont *f) { return &f->info; }

long tt_table(const ttfont *f, uint32_t tag, uint32_t offset, void *out, uint32_t size)
{
    uint32_t at = 0, len = f->n;
    if (tag) {
        char t[4] = { (char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag };
        unsigned count = be16(f->d + 4);
        at = 0;
        len = 0;
        for (unsigned i = 0; i < count && 12u + 16u * (i + 1) <= f->n; i++) {
            const uint8_t *r = f->d + 12 + 16 * i;
            if (!memcmp(r, t, 4)) {
                at = be32(r + 8);
                len = be32(r + 12);
            }
        }
        if (!len || at > f->n || len > f->n - at)
            return -1;
    }
    if (offset > len)
        return -1;
    uint32_t n = len - offset;
    if (!size)
        return (long)n;
    if (n > size)
        n = size;
    if (out)
        memcpy(out, f->d + at + offset, n);
    return (long)n;
}

void tt_metrics(const ttfont *f, ttmetrics *m)
{
    const uint8_t *d = f->d;
    const ttinfo *in = &f->info;
    uint32_t len = 0;
    memset(m, 0, sizeof(*m));
    m->xmin = bes16(d + f->head + 36);
    m->ymin = bes16(d + f->head + 38);
    m->xmax = bes16(d + f->head + 40);
    m->ymax = bes16(d + f->head + 42);
    m->min_ppem = (int)be16(d + f->head + 46);
    m->mac_ascent = bes16(d + f->hhea + 4);
    m->mac_descent = bes16(d + f->hhea + 6);
    m->mac_line_gap = bes16(d + f->hhea + 8);
    m->slope_rise = bes16(d + f->hhea + 18);
    m->slope_run = bes16(d + f->hhea + 20);
    /* without OS/2: what hhea and the em say */
    m->typo_ascender = in->ascent;
    m->typo_descender = -in->descent;
    m->typo_line_gap = in->line_gap;
    m->sub_x = m->sup_x = in->em * 2 / 3;
    m->sub_y = m->sup_y = in->em * 2 / 3;
    m->sub_yoff = in->em / 7;
    m->sup_yoff = in->em * 9 / 20;
    m->strike_size = in->em / 20;
    m->strike_pos = in->em / 4;
    m->cap_height = in->ascent * 7 / 10;
    m->x_height = in->ascent / 2;
    if (f->os2) {
        uint32_t at = table(f, "OS/2", &len);
        const uint8_t *o = d + at;
        m->fs_type = (int)be16(o + 8);
        m->sub_x = bes16(o + 10);
        m->sub_y = bes16(o + 12);
        m->sub_xoff = bes16(o + 14);
        m->sub_yoff = bes16(o + 16);
        m->sup_x = bes16(o + 18);
        m->sup_y = bes16(o + 20);
        m->sup_xoff = bes16(o + 22);
        m->sup_yoff = bes16(o + 24);
        m->strike_size = bes16(o + 26);
        m->strike_pos = bes16(o + 28);
        memcpy(m->panose, o + 32, 10);
        m->fs_selection = (int)be16(o + 62);
        m->typo_ascender = bes16(o + 68);
        m->typo_descender = bes16(o + 70);
        m->typo_line_gap = bes16(o + 72);
        if (be16(o) >= 2 && len >= 90) {
            m->x_height = bes16(o + 86);
            m->cap_height = bes16(o + 88);
        }
    }
    if (f->post) {
        int32_t angle = (int32_t)be32(d + f->post + 4);     /* 16.16 degrees */
        m->italic_angle = (int)((angle * 10 + (angle < 0 ? -32768 : 32768)) / 65536);
        m->under_pos = bes16(d + f->post + 8);
        m->under_size = bes16(d + f->post + 10);
    } else {
        m->under_pos = -in->em / 10;
        m->under_size = in->em / 20;
    }
}

int tt_kern(const ttfont *f, void (*pair)(void *ctx, unsigned left, unsigned right, int value),
            void *ctx)
{
    uint32_t len = 0, at = table(f, "kern", &len);
    if (!at || len < 4 || be16(f->d + at))             /* the Windows one: version 0 */
        return 0;
    const uint8_t *k = f->d + at, *end = k + len;
    unsigned tables = be16(k + 2);
    const uint8_t *t = k + 4;
    int count = 0;
    for (unsigned i = 0; i < tables && t + 6 <= end; i++) {
        unsigned size = be16(t + 2), cover = be16(t + 4);
        if (size < 6 || t + size > end)
            size = (unsigned)(end - t);
        /* format 0, horizontal, kerning (not minimums, not across) */
        if ((cover >> 8) == 0 && (cover & 7) == 1 && t + 14 <= end) {
            unsigned n = be16(t + 6);
            for (unsigned j = 0; j < n && t + 14 + 6 * (j + 1) <= end; j++) {
                const uint8_t *p = t + 14 + 6 * j;
                if (pair)
                    pair(ctx, be16(p), be16(p + 2), bes16(p + 4));
                count++;
            }
        }
        t += size;
    }
    return count;
}

/* ---------------------------------------------------------------- outlines */

typedef struct {
    float x0, y0, x1, y1;
} edge;

typedef struct {
    edge *e;
    int n, cap;
} edges;

static void add_edge(edges *es, float x0, float y0, float x1, float y1)
{
    if (x0 == x1 && y0 == y1)
        return;
    if (es->n == es->cap) {
        int cap = es->cap ? es->cap * 2 : 128;
        edge *n = (edge *)win_alloc(sizeof(edge) * (size_t)cap);
        if (!n)
            return;
        if (es->e) {
            memcpy(n, es->e, sizeof(edge) * (size_t)es->n);
            win_free(es->e);
        }
        es->e = n;
        es->cap = cap;
    }
    es->e[es->n++] = (edge){ x0, y0, x1, y1 };
}

/* a quadratic curve as lines, finer the larger it is */
static void add_curve(edges *es, float x0, float y0, float cx, float cy, float x1, float y1)
{
    float dx = x0 - 2 * cx + x1, dy = y0 - 2 * cy + y1;
    float dev = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    int n = 1;
    while (n < 16 && dev > 0.6f * (float)(n * n))
        n++;
    float px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1 - t;
        float x = u * u * x0 + 2 * u * t * cx + t * t * x1;
        float y = u * u * y0 + 2 * u * t * cy + t * t * y1;
        add_edge(es, px, py, x, y);
        px = x;
        py = y;
    }
}

/* the glyph's outline to the sink, its points through m (x' = m0 x + m2 y +
 * m4, y' = m1 x + m3 y + m5, in pixels, y up) */
static void outline(const ttfont *f, unsigned glyph, const float m[6], tt_sink sink, void *ctx,
                    int depth)
{
    if (glyph >= (unsigned)f->nglyphs || depth > 4)
        return;
    uint32_t at, next;
    if (f->loca_long) {
        if (4u * glyph + 8 > f->loca_len)
            return;
        at = be32(f->d + f->loca + 4 * glyph);
        next = be32(f->d + f->loca + 4 * glyph + 4);
    } else {
        if (2u * glyph + 4 > f->loca_len)
            return;
        at = 2u * be16(f->d + f->loca + 2 * glyph);
        next = 2u * be16(f->d + f->loca + 2 * glyph + 2);
    }
    if (next <= at || next > f->glyf_len || next - at < 10)
        return;                     /* empty (a space) */
    const uint8_t *g = f->d + f->glyf + at, *end = f->d + f->glyf + next;
    int contours = bes16(g);
    if (contours < 0) {             /* composite: its components, each moved */
        const uint8_t *p = g + 10;
        unsigned flags;
        do {
            if (p + 4 > end)
                return;
            flags = be16(p);
            unsigned part = be16(p + 2);
            p += 4;
            float dx = 0, dy = 0;
            if (flags & 1) {        /* ARG_1_AND_2_ARE_WORDS */
                if (p + 4 > end)
                    return;
                if (flags & 2) {
                    dx = (float)bes16(p);
                    dy = (float)bes16(p + 2);
                }
                p += 4;
            } else {
                if (p + 2 > end)
                    return;
                if (flags & 2) {
                    dx = (float)(int8_t)p[0];
                    dy = (float)(int8_t)p[1];
                }
                p += 2;
            }
            float a = 1, b = 0, c = 0, d = 1;
            if (flags & 8) {                        /* WE_HAVE_A_SCALE */
                if (p + 2 > end)
                    return;
                a = d = (float)bes16(p) / 16384.0f;
                p += 2;
            } else if (flags & 0x40) {              /* an x and a y scale */
                if (p + 4 > end)
                    return;
                a = (float)bes16(p) / 16384.0f;
                d = (float)bes16(p + 2) / 16384.0f;
                p += 4;
            } else if (flags & 0x80) {              /* two by two */
                if (p + 8 > end)
                    return;
                a = (float)bes16(p) / 16384.0f;
                b = (float)bes16(p + 2) / 16384.0f;
                c = (float)bes16(p + 4) / 16384.0f;
                d = (float)bes16(p + 6) / 16384.0f;
                p += 8;
            }
            /* the component's point (x, y): (a x + c y + dx, b x + d y + dy),
             * then through m */
            float n[6] = {
                m[0] * a + m[2] * b, m[1] * a + m[3] * b,
                m[0] * c + m[2] * d, m[1] * c + m[3] * d,
                m[0] * dx + m[2] * dy + m[4], m[1] * dx + m[3] * dy + m[5],
            };
            outline(f, part, n, sink, ctx, depth + 1);
        } while (flags & 0x20);                     /* MORE_COMPONENTS */
        return;
    }
    if (!contours)
        return;
    const uint8_t *p = g + 10;
    if (p + 2 * contours + 2 > end)
        return;
    int points = (int)be16(p + 2 * (contours - 1)) + 1;
    unsigned ilen = be16(p + 2 * contours);
    p += 2 * contours + 2 + ilen;
    if (points <= 0 || points > 0x7fff || p > end)
        return;
    uint8_t *flag = (uint8_t *)win_alloc((size_t)points);
    float *px = (float *)win_alloc(sizeof(float) * 2 * (size_t)points);
    if (!flag || !px) {
        win_free(flag);
        win_free(px);
        return;
    }
    float *py = px + points;
    for (int i = 0; i < points;) {                  /* the flags, repeated */
        if (p >= end)
            goto done;
        uint8_t fl = *p++;
        flag[i++] = fl;
        if (fl & 8) {
            if (p >= end)
                goto done;
            for (unsigned r = *p++; r && i < points; r--)
                flag[i++] = fl;
        }
    }
    int v = 0;
    for (int i = 0; i < points; i++) {              /* the x's, from one to the next */
        uint8_t fl = flag[i];
        if (fl & 2) {
            if (p >= end)
                goto done;
            v += fl & 0x10 ? *p : -(int)*p;
            p++;
        } else if (!(fl & 0x10)) {
            if (p + 2 > end)
                goto done;
            v += bes16(p);
            p += 2;
        }
        px[i] = (float)v;
    }
    v = 0;
    for (int i = 0; i < points; i++) {              /* the y's */
        uint8_t fl = flag[i];
        if (fl & 4) {
            if (p >= end)
                goto done;
            v += fl & 0x20 ? *p : -(int)*p;
            p++;
        } else if (!(fl & 0x20)) {
            if (p + 2 > end)
                goto done;
            v += bes16(p);
            p += 2;
        }
        py[i] = (float)v;
    }
    for (int i = 0; i < points; i++) {              /* through m */
        float x = px[i], y = py[i];
        px[i] = m[0] * x + m[2] * y + m[4];
        py[i] = m[1] * x + m[3] * y + m[5];
    }
    /* each contour: on-curve points joined by lines, an off-curve one
     * the control point of a curve (two in a row: on-curve between them) */
    int first = 0;
    for (int c = 0; c < contours; c++) {
        int last = (int)be16(g + 10 + 2 * c);
        if (last < first || last >= points)
            break;
        int count = last - first + 1;
        float sx, sy;                               /* where it starts: on the curve */
        int start;
        if (flag[first] & 1) {
            sx = px[first];
            sy = py[first];
            start = 1;
        } else if (flag[last] & 1) {
            sx = px[last];
            sy = py[last];
            start = 0;
            count--;
        } else {
            sx = (px[first] + px[last]) / 2;
            sy = (py[first] + py[last]) / 2;
            start = 0;
        }
        float cx = sx, cy = sy;                     /* the pen */
        sink(ctx, TT_MOVE, sx, sy, 0, 0);
        int ctrl = 0;
        float kx = 0, ky = 0;                       /* the pending control point */
        for (int k = start; k <= count; k++) {
            int i = first + (k < count ? k : 0);
            float x, y;
            int on;
            if (k == count) {                       /* back to the start */
                x = sx;
                y = sy;
                on = 1;
            } else {
                x = px[i];
                y = py[i];
                on = flag[i] & 1;
            }
            if (on) {
                if (ctrl)
                    sink(ctx, TT_QUAD, kx, ky, x, y);
                else
                    sink(ctx, TT_LINE, x, y, 0, 0);
                cx = x;
                cy = y;
                ctrl = 0;
            } else {
                if (ctrl) {                         /* the point between two controls */
                    float mx = (kx + x) / 2, my = (ky + y) / 2;
                    sink(ctx, TT_QUAD, kx, ky, mx, my);
                    cx = mx;
                    cy = my;
                }
                kx = x;
                ky = y;
                ctrl = 1;
            }
        }
        sink(ctx, TT_CLOSE, cx, cy, 0, 0);
        first = last + 1;
    }
done:
    win_free(flag);
    win_free(px);
}

void tt_outline(const ttfont *f, unsigned glyph, const float m[4], tt_sink sink, void *ctx)
{
    const float m6[6] = { m[0], m[1], m[2], m[3], 0, 0 };
    outline(f, glyph, m6, sink, ctx, 0);
}

/* the outline as edges, the curves as lines */
typedef struct {
    edges es;
    float px, py;
} edge_pen;

static void edge_sink(void *ctx, int op, float x0, float y0, float x1, float y1)
{
    edge_pen *p = (edge_pen *)ctx;
    if (op == TT_LINE) {
        add_edge(&p->es, p->px, p->py, x0, y0);
    } else if (op == TT_QUAD) {
        add_curve(&p->es, p->px, p->py, x0, y0, x1, y1);
        x0 = x1;
        y0 = y1;
    } else if (op == TT_CLOSE) {
        return;
    }
    p->px = x0;
    p->py = y0;
}

/* ---------------------------------------------------------------- drawing */

static int ifloor(float v) { int i = (int)v; return (float)i > v ? i - 1 : i; }
static int iceil(float v) { int i = (int)v; return (float)i < v ? i + 1 : i; }

typedef struct {
    float at;
    int dir;
} cross;

static void sort_crossings(cross *c, int n)
{
    for (int i = 1; i < n; i++) {                   /* few: insertion */
        cross k = c[i];
        int j = i - 1;
        while (j >= 0 && c[j].at > k.at) {
            c[j + 1] = c[j];
            j--;
        }
        c[j + 1] = k;
    }
}

static void set_px(uint8_t *bits, int pitch, int w, int h, int x, int y)
{
    if (x >= 0 && x < w && y >= 0 && y < h)
        bits[y * pitch + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
}

uint8_t *tt_render(const ttfont *f, unsigned glyph, float scale, int *w, int *h, int *x0,
                   int *top)
{
    const float m[4] = { scale, 0, 0, scale };
    return tt_render_m(f, glyph, m, w, h, x0, top);
}

uint8_t *tt_render_m(const ttfont *f, unsigned glyph, const float m[4], int *w, int *h,
                     int *x0, int *top)
{
    edge_pen pen = { { NULL, 0, 0 }, 0, 0 };
    *w = *h = *x0 = *top = 0;
    tt_outline(f, glyph, m, edge_sink, &pen);
    edges es = pen.es;
    if (!es.n) {
        win_free(es.e);
        return NULL;
    }
    float xmin = es.e[0].x0, xmax = xmin, ymin = es.e[0].y0, ymax = ymin;
    for (int i = 0; i < es.n; i++) {
        const edge *e = &es.e[i];
        float xs[2] = { e->x0, e->x1 }, ys[2] = { e->y0, e->y1 };
        for (int k = 0; k < 2; k++) {
            if (xs[k] < xmin) xmin = xs[k];
            if (xs[k] > xmax) xmax = xs[k];
            if (ys[k] < ymin) ymin = ys[k];
            if (ys[k] > ymax) ymax = ys[k];
        }
    }
    int left = ifloor(xmin), right = iceil(xmax), bottom = ifloor(ymin), up = iceil(ymax);
    if (right <= left)
        right = left + 1;
    if (up <= bottom)
        up = bottom + 1;
    int bw = right - left, bh = up - bottom, pitch = (bw + 7) / 8;
    if (bw > 1024 || bh > 1024) {
        win_free(es.e);
        return NULL;
    }
    uint8_t *bits = (uint8_t *)win_alloc((size_t)(pitch * bh));
    cross *cr = (cross *)win_alloc(sizeof(cross) * (size_t)es.n);
    if (!bits || !cr) {
        win_free(bits);
        win_free(cr);
        win_free(es.e);
        return NULL;
    }
    /* rows: the pixels whose centres are inside, and one for a span
     * between two centres */
    for (int r = 0; r < bh; r++) {
        float yc = (float)up - (float)r - 0.5f;
        int n = 0;
        for (int i = 0; i < es.n; i++) {
            const edge *e = &es.e[i];
            if ((e->y0 <= yc && yc < e->y1) || (e->y1 <= yc && yc < e->y0)) {
                cr[n].at = e->x0 + (yc - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0) - (float)left;
                cr[n].dir = e->y1 > e->y0 ? 1 : -1;
                n++;
            }
        }
        sort_crossings(cr, n);
        int wind = 0;
        float from = 0;
        for (int i = 0; i < n; i++) {
            int was = wind;
            wind += cr[i].dir;
            if (!was && wind)
                from = cr[i].at;
            else if (was && !wind) {
                int c0 = iceil(from - 0.5f), c1 = iceil(cr[i].at - 0.5f) - 1;
                if (c1 < c0)                        /* thinner than a pixel */
                    set_px(bits, pitch, bw, bh, ifloor((from + cr[i].at) / 2), r);
                for (int c = c0; c <= c1; c++)
                    set_px(bits, pitch, bw, bh, c, r);
            }
        }
    }
    /* columns: only the spans thinner than a pixel (horizontal strokes
     * between two rows' centres) */
    for (int c = 0; c < bw; c++) {
        float xc = (float)left + (float)c + 0.5f;
        int n = 0;
        for (int i = 0; i < es.n; i++) {
            const edge *e = &es.e[i];
            if ((e->x0 <= xc && xc < e->x1) || (e->x1 <= xc && xc < e->x0)) {
                cr[n].at = (float)up - (e->y0 + (xc - e->x0) * (e->y1 - e->y0) / (e->x1 - e->x0));
                cr[n].dir = e->x1 > e->x0 ? 1 : -1;
                n++;
            }
        }
        sort_crossings(cr, n);
        int wind = 0;
        float from = 0;
        for (int i = 0; i < n; i++) {
            int was = wind;
            wind += cr[i].dir;
            if (!was && wind)
                from = cr[i].at;
            else if (was && !wind && iceil(cr[i].at - 0.5f) - 1 < iceil(from - 0.5f))
                set_px(bits, pitch, bw, bh, c, ifloor((from + cr[i].at) / 2));
        }
    }
    win_free(cr);
    win_free(es.e);
    *w = bw;
    *h = bh;
    *x0 = left;
    *top = up;
    return bits;
}
