/*
 * truetype.c - TrueType fonts: the tables a font file has (head, hhea,
 * hmtx, maxp, loca, glyf, cmap, name, OS/2, post, hdmx, kern), its
 * characters' glyphs (simple and composite ones, their quadratic curves
 * made lines) drawn black and white: a pixel set when its centre is inside
 * the outline (the non-zero winding rule), and, when the font's hints ask
 * for it (SCANCTRL), the parts thinner than a pixel still one (dropout
 * control: simple or smart, SCANTYPE), as Windows draws its TrueType fonts
 * on a VGA. The hints themselves: ttinterp.c.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>

#include "wincore.h"
#include "truetype.h"

#include "truetype_int.h"

#define be16 tt_be16
#define bes16 tt_bes16
#define be32 tt_be32
#define table tt_tab

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

uint32_t tt_tab(const ttfont *f, const char *tag, uint32_t *len)
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
    /* Cyrillic: А and а in the Unicode map, or 1251's in the symbol one */
    in->cyrillic = f->kind == 1 ? map_lookup(f, 0x0410) && map_lookup(f, 0x0430) :
                   f->kind == 2 ? map_lookup(f, 0xF0C0) || map_lookup(f, 0xC0) : 1;
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

/* ---------------------------------------------------------------- glyphs */

int tt_glyph_at(const ttfont *f, unsigned glyph, const uint8_t **g, const uint8_t **end)
{
    if (glyph >= (unsigned)f->nglyphs)
        return 0;
    uint32_t at, next;
    if (f->loca_long) {
        if (4u * glyph + 8 > f->loca_len)
            return 0;
        at = be32(f->d + f->loca + 4 * glyph);
        next = be32(f->d + f->loca + 4 * glyph + 4);
    } else {
        if (2u * glyph + 4 > f->loca_len)
            return 0;
        at = 2u * be16(f->d + f->loca + 2 * glyph);
        next = 2u * be16(f->d + f->loca + 2 * glyph + 2);
    }
    if (next <= at || next > f->glyf_len || next - at < 10)
        return 0;                   /* empty (a space) */
    *g = f->d + f->glyf + at;
    *end = f->d + f->glyf + next;
    return 1;
}

void tt_hmetrics(const ttfont *f, unsigned glyph, int *advance, int *lsb)
{
    *advance = tt_advance(f, glyph);
    *lsb = 0;
    if (!f->hmtx || !f->nhmetrics)
        return;
    uint32_t at = glyph < (unsigned)f->nhmetrics ? 4u * glyph + 2
                  : 4u * (unsigned)f->nhmetrics + 2u * (glyph - (unsigned)f->nhmetrics);
    if (at + 2 <= f->hmtx_len)
        *lsb = bes16(f->d + f->hmtx + at);
}

int tt_simple(const uint8_t *g, const uint8_t *end, ttsimple *s)
{
    memset(s, 0, sizeof(*s));
    int contours = bes16(g);
    const uint8_t *p = g + 10;
    if (contours <= 0 || p + 2 * contours + 2 > end)
        return 0;
    int points = (int)be16(p + 2 * (contours - 1)) + 1;
    unsigned ilen = be16(p + 2 * contours);
    if (points <= 0 || points > 0x7fff || p + 2 * contours + 2 + ilen > end)
        return 0;
    size_t sz = (sizeof(int32_t) * 2 + 1) * (size_t)points + sizeof(uint16_t) * (size_t)contours;
    uint8_t *mem = (uint8_t *)win_alloc(sz);
    if (!mem)
        return 0;
    s->mem = mem;
    s->x = (int32_t *)mem;
    s->y = s->x + points;
    s->ends = (uint16_t *)(s->y + points);
    s->on = (uint8_t *)(s->ends + contours);
    s->npts = points;
    s->ncont = contours;
    for (int c = 0; c < contours; c++) {
        s->ends[c] = (uint16_t)be16(p + 2 * c);
        if (s->ends[c] >= points || (c && s->ends[c] < s->ends[c - 1]))
            goto bad;
    }
    s->ins = p + 2 * contours + 2;
    s->nins = ilen;
    p = s->ins + ilen;
    uint8_t *flag = s->on;
    for (int i = 0; i < points;) {                  /* the flags, repeated */
        if (p >= end)
            goto bad;
        uint8_t fl = *p++;
        flag[i++] = fl;
        if (fl & 8) {
            if (p >= end)
                goto bad;
            for (unsigned r = *p++; r && i < points; r--)
                flag[i++] = fl;
        }
    }
    int32_t v = 0;
    for (int i = 0; i < points; i++) {              /* the x's, from one to the next */
        uint8_t fl = flag[i];
        if (fl & 2) {
            if (p >= end)
                goto bad;
            v += fl & 0x10 ? *p : -(int32_t)*p;
            p++;
        } else if (!(fl & 0x10)) {
            if (p + 2 > end)
                goto bad;
            v += bes16(p);
            p += 2;
        }
        s->x[i] = v;
    }
    v = 0;
    for (int i = 0; i < points; i++) {              /* the y's */
        uint8_t fl = flag[i];
        if (fl & 4) {
            if (p >= end)
                goto bad;
            v += fl & 0x20 ? *p : -(int32_t)*p;
            p++;
        } else if (!(fl & 0x20)) {
            if (p + 2 > end)
                goto bad;
            v += bes16(p);
            p += 2;
        }
        s->y[i] = v;
    }
    for (int i = 0; i < points; i++)
        flag[i] &= 1;                               /* on the curve */
    return 1;
bad:
    tt_simple_free(s);
    return 0;
}

void tt_simple_free(ttsimple *s)
{
    win_free(s->mem);
    memset(s, 0, sizeof(*s));
}

void tt_emit(const float *px, const float *py, const uint8_t *on, const uint16_t *ends,
             int ncont, int npts, tt_sink sink, void *ctx)
{
    int first = 0;
    for (int c = 0; c < ncont; c++) {
        int last = ends[c];
        if (last < first || last >= npts)
            break;
        int count = last - first + 1;
        /* where it starts: an on-curve point, else the middle of the
         * last and the first */
        float sx, sy;
        int start;
        if (on[first] & 1) {
            sx = px[first];
            sy = py[first];
            start = 1;
        } else if (on[last] & 1) {
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
            int is_on;
            if (k == count) {                       /* back to the start */
                x = sx;
                y = sy;
                is_on = 1;
            } else {
                x = px[i];
                y = py[i];
                is_on = on[i] & 1;
            }
            if (is_on) {
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
}

/* the glyph's outline as it is (no hints) to the sink, its points through m
 * (x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5, in pixels, y up) */
static void outline(const ttfont *f, unsigned glyph, const float m[6], tt_sink sink, void *ctx,
                    int depth)
{
    const uint8_t *g, *end;
    if (depth > 4 || !tt_glyph_at(f, glyph, &g, &end))
        return;
    if (bes16(g) < 0) {             /* composite: its components, each moved */
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
    ttsimple s;
    if (!tt_simple(g, end, &s))
        return;
    float *px = (float *)win_alloc(sizeof(float) * 2 * (size_t)s.npts);
    if (px) {
        float *py = px + s.npts;
        for (int i = 0; i < s.npts; i++) {          /* through m */
            float x = (float)s.x[i], y = (float)s.y[i];
            px[i] = m[0] * x + m[2] * y + m[4];
            py[i] = m[1] * x + m[3] * y + m[5];
        }
        tt_emit(px, py, s.on, s.ends, s.ncont, s.npts, sink, ctx);
        win_free(px);
    }
    tt_simple_free(&s);
}

void tt_outline(const ttfont *f, unsigned glyph, const float m[4], tt_sink sink, void *ctx)
{
    const float m6[6] = { m[0], m[1], m[2], m[3], 0, 0 };
    outline(f, glyph, m6, sink, ctx, 0);
}

/* ---------------------------------------------------------------- scan conversion */

/*
 * The outline (26.6) made pixels as Windows' rasterizer makes them: the
 * coordinates fixed point (a pixel 4096 below 24 pixels to the em, else
 * 64), the pixels' centres at whole numbers; each contour cut into
 * profiles, its runs up and its runs down, each holding where it crosses
 * the scan lines it passes (the centres' rows: those on its ends too, a
 * turning point in both the profiles it ends and starts, a point within
 * a profile once); curves halved until a piece is flatter than a step,
 * then taken straight. A row: its rising crossings and its falling ones,
 * each in order, paired; the centres from one to the other set; a span
 * with none, between two neighbouring centres, a dropout. Then the
 * columns the same way, the outline turned: a centre on a horizontal edge
 * set, and the columns' dropouts. A dropout's pixel: simple, the left (the
 * lower) one; smart, the one nearer its middle; a stub (a stroke's end
 * between two scan lines) not, without stubs, unless the stroke reaches
 * half a pixel past the last line and is half a pixel wide.
 */

enum { ST_NONE, ST_UP, ST_DOWN };
enum { DROP_SIMPLE = 0, DROP_SIMPLE_NOSTUBS = 1, DROP_OFF = 2, DROP_SMART = 4, DROP_SMART_NOSTUBS = 5 };

typedef struct {
    int64_t *xs;                    /* its crossings: up, from its lowest line; down, from its highest */
    int      n, cap;
    int64_t  start;                 /* that line */
    uint8_t  up, ovr_top, ovr_bot;
    int      next;                  /* the next profile in its contour */
    int64_t  x;                     /* where it crosses the line swept */
} sprof;

typedef struct {
    int64_t x, y;
} spt;

typedef struct {
    sprof  *p;
    int     np, cap, cur, first;    /* first: the contour's first profile (-1) */
    int     state, fresh, joint;
    int64_t lastx, lasty, miny, maxy;
    int64_t P, half, step, jitter;
    int     bits;
    int     err;
} scan;

static int64_t sfloor(const scan *s, int64_t v) { return v & -s->P; }
static int64_t sceil(const scan *s, int64_t v) { return (v + s->P - 1) & -s->P; }
static int64_t strunc(const scan *s, int64_t v) { return v >> s->bits; }
static int64_t sfrac(const scan *s, int64_t v) { return v & (s->P - 1); }

static int64_t smuldiv(int64_t a, int64_t b, int64_t c, int round)
{
    int neg = (a < 0) ^ (b < 0) ^ (c < 0);
    uint64_t ua = (uint64_t)(a < 0 ? -a : a), ub = (uint64_t)(b < 0 ? -b : b);
    uint64_t uc = (uint64_t)(c < 0 ? -c : c);
    uint64_t q = uc ? (ua * ub + (round ? uc / 2 : 0)) / uc : 0x7fffffff;
    return neg ? -(int64_t)q : (int64_t)q;
}

static int slot(scan *s)
{
    if (s->np == s->cap) {
        int cap = s->cap ? s->cap * 2 : 32;
        sprof *p = (sprof *)win_alloc(sizeof(sprof) * (size_t)cap);
        if (!p) {
            s->err = 1;
            return -1;
        }
        if (s->p) {
            memcpy(p, s->p, sizeof(sprof) * (size_t)s->np);
            win_free(s->p);
        }
        s->p = p;
        s->cap = cap;
    }
    memset(&s->p[s->np], 0, sizeof(sprof));
    s->p[s->np].next = -1;
    return s->np++;
}

static void push_x(scan *s, int64_t x)
{
    sprof *p = &s->p[s->cur];
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 16;
        int64_t *n = (int64_t *)win_alloc(sizeof(int64_t) * (size_t)cap);
        if (!n) {
            s->err = 1;
            return;
        }
        if (p->xs) {
            memcpy(n, p->xs, sizeof(int64_t) * (size_t)p->n);
            win_free(p->xs);
        }
        p->xs = n;
        p->cap = cap;
    }
    p->xs[p->n++] = x;
}

static void pop_x(scan *s)
{
    if (s->p[s->cur].n)
        s->p[s->cur].n--;
}

static int top_ovr(const scan *s, int64_t y) { return y - sfloor(s, y) >= s->half; }
static int bot_ovr(const scan *s, int64_t y) { return sceil(s, y) - y >= s->half; }

static void new_profile(scan *s, int state, int overshoot)
{
    sprof *p = &s->p[s->cur];
    p->n = 0;
    p->start = 0;
    p->up = state == ST_UP;
    p->ovr_top = p->ovr_bot = 0;
    if (overshoot) {
        if (p->up)
            p->ovr_bot = 1;
        else
            p->ovr_top = 1;
    }
    p->next = -1;
    if (s->first < 0)
        s->first = s->cur;
    s->state = state;
    s->fresh = 1;
    s->joint = 0;
}

static void end_profile(scan *s, int overshoot)
{
    sprof *p = &s->p[s->cur];
    if (p->n > 0) {
        if (overshoot) {
            if (p->up)
                p->ovr_top = 1;
            else
                p->ovr_bot = 1;
        }
        int old = s->cur, n = slot(s);
        if (n < 0)
            return;
        s->cur = n;
        s->p[old].next = n;
    }
    s->joint = 0;
}

/* a rising line's crossings */
static void line_up(scan *s, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t miny,
                    int64_t maxy)
{
    int64_t dx = x2 - x1, dy = y2 - y1, e1, e2, f1, f2;
    if (dy <= 0 || y2 < miny || y1 > maxy)
        return;
    if (y1 < miny) {
        x1 += smuldiv(dx, miny - y1, dy, 1);
        e1 = strunc(s, miny);
        f1 = 0;
    } else {
        e1 = strunc(s, y1);
        f1 = sfrac(s, y1);
    }
    if (y2 > maxy) {
        e2 = strunc(s, maxy);
        f2 = 0;
    } else {
        e2 = strunc(s, y2);
        f2 = sfrac(s, y2);
    }
    if (f1 > 0) {
        if (e1 == e2)
            return;
        x1 += smuldiv(dx, s->P - f1, dy, 1);
        e1++;
    } else if (s->joint) {
        pop_x(s);
        s->joint = 0;
    }
    s->joint = f2 == 0;
    if (s->fresh) {
        s->p[s->cur].start = e1;
        s->fresh = 0;
    }
    int64_t ix, rx, ax = -dy;
    if (dx > 0) {
        ix = smuldiv(s->P, dx, dy, 0);
        rx = (s->P * dx) % dy;
        dx = 1;
    } else {
        ix = -smuldiv(s->P, -dx, dy, 0);
        rx = (s->P * -dx) % dy;
        dx = -1;
    }
    for (int64_t k = e2 - e1 + 1; k > 0 && !s->err; k--) {
        push_x(s, x1);
        x1 += ix;
        ax += rx;
        if (ax >= 0) {
            ax -= dy;
            x1 += dx;
        }
    }
}

static void line_down(scan *s, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t miny,
                      int64_t maxy)
{
    int fresh = s->fresh;
    line_up(s, x1, -y1, x2, -y2, -maxy, -miny);
    if (fresh && !s->fresh)
        s->p[s->cur].start = -s->p[s->cur].start;
}

static void split_conic(spt *b)
{
    int64_t a, c;
    b[4].x = b[2].x;
    a = b[0].x + b[1].x;
    c = b[1].x + b[2].x;
    b[3].x = c >> 1;
    b[2].x = (a + c) >> 2;
    b[1].x = a >> 1;
    b[4].y = b[2].y;
    a = b[0].y + b[1].y;
    c = b[1].y + b[2].y;
    b[3].y = c >> 1;
    b[2].y = (a + c) >> 2;
    b[1].y = a >> 1;
}

#define MAX_BEZIER 32

/* a rising conic's crossings: halved until a piece crossing a line is
 * flatter than a step, then that piece straight */
static void bezier_up(scan *s, spt *arc, spt *stack_end, int64_t miny, int64_t maxy)
{
    int64_t y1 = arc[2].y, y2 = arc[0].y, e, e2;
    if (y2 < miny || y1 > maxy)
        return;
    e2 = sfloor(s, y2);
    if (e2 > maxy)
        e2 = maxy;
    int64_t e0 = miny;
    if (y1 < miny) {
        e = miny;
    } else {
        e = sceil(s, y1);
        int64_t f1 = sfrac(s, y1);
        e0 = e;
        if (f1 == 0) {
            if (s->joint) {
                pop_x(s);
                s->joint = 0;
            }
            push_x(s, arc[2].x);
            e += s->P;
        }
    }
    if (s->fresh) {
        s->p[s->cur].start = strunc(s, e0);
        s->fresh = 0;
    }
    if (e2 < e)
        return;
    spt *start = arc;
    do {
        s->joint = 0;
        y2 = arc[0].y;
        if (y2 > e) {
            y1 = arc[2].y;
            if (y2 - y1 >= s->step && arc + 4 < stack_end) {
                split_conic(arc);
                arc += 2;
            } else {
                push_x(s, arc[2].x + (arc[0].x - arc[2].x) * (e - y1) / (y2 - y1));
                arc -= 2;
                e += s->P;
            }
        } else {
            if (y2 == e) {
                s->joint = 1;
                push_x(s, arc[0].x);
                e += s->P;
            }
            arc -= 2;
        }
    } while (arc >= start && e <= e2 && !s->err);
}

static void bezier_down(scan *s, spt *arc, spt *stack_end, int64_t miny, int64_t maxy)
{
    arc[0].y = -arc[0].y;
    arc[1].y = -arc[1].y;
    arc[2].y = -arc[2].y;
    int fresh = s->fresh;
    bezier_up(s, arc, stack_end, -maxy, -miny);
    if (fresh && !s->fresh)
        s->p[s->cur].start = -s->p[s->cur].start;
    arc[0].y = -arc[0].y;
}

static void line_to(scan *s, int64_t x, int64_t y)
{
    switch (s->state) {
    case ST_NONE:
        if (y > s->lasty)
            new_profile(s, ST_UP, bot_ovr(s, s->lasty));
        else if (y < s->lasty)
            new_profile(s, ST_DOWN, top_ovr(s, s->lasty));
        break;
    case ST_UP:
        if (y < s->lasty) {
            end_profile(s, top_ovr(s, s->lasty));
            new_profile(s, ST_DOWN, top_ovr(s, s->lasty));
        }
        break;
    default:
        if (y > s->lasty) {
            end_profile(s, bot_ovr(s, s->lasty));
            new_profile(s, ST_UP, bot_ovr(s, s->lasty));
        }
        break;
    }
    if (s->state == ST_UP)
        line_up(s, s->lastx, s->lasty, x, y, s->miny, s->maxy);
    else if (s->state == ST_DOWN)
        line_down(s, s->lastx, s->lasty, x, y, s->miny, s->maxy);
    s->lastx = x;
    s->lasty = y;
}

static void conic_to(scan *s, int64_t cx, int64_t cy, int64_t x, int64_t y)
{
    spt arcs[2 * MAX_BEZIER + 1], *arc = arcs, *end = arcs + 2 * MAX_BEZIER + 1;
    int64_t x3 = x, y3 = y;
    arc[2].x = s->lastx;
    arc[2].y = s->lasty;
    arc[1].x = cx;
    arc[1].y = cy;
    arc[0].x = x;
    arc[0].y = y;
    do {
        int64_t y1 = arc[2].y, y2 = arc[1].y, ya = arc[0].y, ymin, ymax;
        x3 = arc[0].x;
        y3 = ya;
        if (y1 <= ya) {
            ymin = y1;
            ymax = ya;
        } else {
            ymin = ya;
            ymax = y1;
        }
        if ((y2 < ymin || y2 > ymax) && arc + 4 < end) {    /* not one way: halved */
            split_conic(arc);
            arc += 2;
        } else if (y1 == ya) {                              /* flat */
            arc -= 2;
        } else {
            int st = y1 < ya ? ST_UP : ST_DOWN;
            if (s->state != st) {
                int o = st == ST_UP ? bot_ovr(s, y1) : top_ovr(s, y1);
                if (s->state != ST_NONE)
                    end_profile(s, o);
                new_profile(s, st, o);
            }
            if (st == ST_UP)
                bezier_up(s, arc, end, s->miny, s->maxy);
            else
                bezier_down(s, arc, end, s->miny, s->maxy);
            arc -= 2;
        }
    } while (arc >= arcs && !s->err);
    s->lastx = x3;
    s->lasty = y3;
}

/* the path (26.6, from the bitmap's corner) made profiles; flipped: x and
 * y swapped, for the columns */
typedef ttpath spath;

static void make_profiles(scan *s, const spath *pa, int flipped)
{
    int first = 0;
    s->cur = slot(s);
    for (int c = 0; c < pa->ncont && !s->err; c++) {
        int last = pa->ends[c];
        if (last < first || last >= pa->n)
            break;
        s->state = ST_NONE;
        s->first = -1;
#define SC(v) ((int64_t)(v) * (s->P >> 6) - s->half)
#define PT(i, X, Y) do { X = SC(pa->x[i]); Y = SC(pa->y[i]); \
                         if (flipped) { int64_t t_ = X; X = Y; Y = t_; } } while (0)
        int64_t sx, sy, lx, ly;
        PT(first, sx, sy);
        PT(last, lx, ly);
        int i = first, limit = last;
        if (!(pa->on[first] & 1)) {         /* starts off the curve */
            if (pa->on[last] & 1) {
                sx = lx;
                sy = ly;
                limit--;
            } else {
                sx = (sx + lx) / 2;
                sy = (sy + ly) / 2;
            }
            i--;
        }
        s->lastx = sx;
        s->lasty = sy;
        int closed = 0;
        while (i < limit && !s->err) {
            i++;
            int64_t x, y;
            PT(i, x, y);
            if (pa->on[i] & 1) {
                line_to(s, x, y);
                continue;
            }
            int64_t kx = x, ky = y;                 /* a control point */
            for (;;) {
                if (i < limit) {
                    i++;
                    PT(i, x, y);
                    if (pa->on[i] & 1) {
                        conic_to(s, kx, ky, x, y);
                        break;
                    }
                    conic_to(s, kx, ky, (kx + x) / 2, (ky + y) / 2);
                    kx = x;
                    ky = y;
                } else {
                    conic_to(s, kx, ky, sx, sy);
                    closed = 1;
                    break;
                }
            }
            if (closed)
                break;
        }
        if (!closed)
            line_to(s, sx, sy);
#undef PT
#undef SC
        /* the contour's last and first profiles joined on a line: its
         * point once */
        if (sfrac(s, s->lasty) == 0 && s->lasty >= s->miny && s->lasty <= s->maxy &&
            s->first >= 0 && s->p[s->first].up == s->p[s->cur].up)
            pop_x(s);
        int lastp = s->cur;
        int o = s->p[s->cur].n && s->p[s->cur].up ? top_ovr(s, s->lasty) : bot_ovr(s, s->lasty);
        end_profile(s, o);
        if (s->first >= 0)
            s->p[lastp].next = s->first;
        first = last + 1;
    }
}

static void scan_free(scan *s)
{
    for (int i = 0; i < s->np; i++)
        win_free(s->p[i].xs);
    win_free(s->p);
    memset(s, 0, sizeof(*s));
}

/* the bitmap: rows bottom up, (x, y) the pixel at column x, row y */
typedef struct {
    uint8_t *bits;
    int      w, h, pitch;
} sbmp;

static int sget(const sbmp *b, int x, int y)
{
    return x >= 0 && x < b->w && y >= 0 && y < b->h &&
           (b->bits[(b->h - 1 - y) * b->pitch + (x >> 3)] & (0x80u >> (x & 7)));
}

static void sset(sbmp *b, int x, int y)
{
    if (x >= 0 && x < b->w && y >= 0 && y < b->h)
        b->bits[(b->h - 1 - y) * b->pitch + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
}

/* a dropout's pixel on scan line y (flipped: column y), x1..x2 its span */
static void drop(const scan *s, sbmp *b, int flipped, int mode, int64_t y, int64_t x1, int64_t x2,
                 const sprof *left, int li, int ri, int last_of_left)
{
    int64_t e1 = sceil(s, x1), e2 = sfloor(s, x2), pxl = e1;
    int size = flipped ? b->h : b->w;
    if (e1 > e2) {
        if (e1 != e2 + s->P)
            return;
        switch (mode) {
        case DROP_SIMPLE:
            pxl = e2;
            break;
        case DROP_SMART:
            pxl = sfloor(s, (x1 + x2 + s->P * 63 / 64) >> 1);
            break;
        case DROP_SIMPLE_NOSTUBS:
        case DROP_SMART_NOSTUBS:
            /* a stub, at the stroke's top or bottom end */
            if (left->next == ri && last_of_left && !(left->ovr_top && x2 - x1 >= s->half))
                return;
            if (s->p[ri].next == li && left->start == y && !(left->ovr_bot && x2 - x1 >= s->half))
                return;
            pxl = mode == DROP_SIMPLE_NOSTUBS ? e2 : sfloor(s, (x1 + x2 + s->P * 63 / 64) >> 1);
            break;
        default:
            return;
        }
        if (pxl < 0)                        /* in the bitmap */
            pxl = e1;
        else if (strunc(s, pxl) >= size)
            pxl = e2;
        int64_t other = strunc(s, pxl == e1 ? e2 : e1);
        if (other >= 0 && other < size &&
            (flipped ? sget(b, (int)y, (int)other) : sget(b, (int)other, (int)y)))
            return;
    }
    int64_t p = strunc(s, pxl);
    if (flipped)
        sset(b, (int)y, (int)p);
    else
        sset(b, (int)p, (int)y);
}

static void sweep(scan *s, sbmp *b, int flipped, int mode)
{
    int lines = flipped ? b->w : b->h, size = flipped ? b->h : b->w;
    int *lst = (int *)win_alloc(sizeof(int) * (size_t)(2 * s->np + 2));
    int *pend = (int *)win_alloc(sizeof(int) * (size_t)(2 * s->np + 2));
    if (!lst || !pend) {
        win_free(lst);
        win_free(pend);
        return;
    }
    for (int y = 0; y < lines; y++) {
        int nl = 0, nr = 0;
        int *L = lst, *R = lst + s->np + 1;
        for (int i = 0; i < s->np; i++) {
            sprof *p = &s->p[i];
            if (!p->n)
                continue;
            int64_t lo = p->up ? p->start : p->start - p->n + 1, hi = lo + p->n - 1;
            if (y < lo || y > hi)
                continue;
            p->x = p->up ? p->xs[y - lo] : p->xs[p->start - y];
            int *lis = p->up ? L : R, *cnt = p->up ? &nl : &nr;
            int k = (*cnt)++;                       /* in order of x (and of making) */
            while (k > 0 && s->p[lis[k - 1]].x > p->x) {
                lis[k] = lis[k - 1];
                k--;
            }
            lis[k] = i;
        }
        int npend = 0;
        for (int k = 0; k < nl && k < nr; k++) {
            int64_t x1 = s->p[L[k]].x, x2 = s->p[R[k]].x;
            if (x1 > x2) {
                int64_t t = x1;
                x1 = x2;
                x2 = t;
            }
            int64_t f1 = sfloor(s, x1), c2 = sceil(s, x2);
            if (x2 - x1 <= s->P && f1 != x1 && c2 != x2 && (f1 > c2 || c2 == f1 + s->P)) {
                if (mode != DROP_OFF)               /* a dropout: after the spans */
                    pend[npend++] = k;
                continue;
            }
            if (!flipped) {                         /* the row's centres */
                int64_t e1 = sceil(s, x1), e2 = sfloor(s, x2);
                if (mode != DROP_OFF && x2 - x1 - s->P <= s->jitter && e1 != x1 && e2 != x2)
                    e2 = e1;
                int64_t a = strunc(s, e1), z = strunc(s, e2);
                if (z >= 0 && a < size) {
                    if (a < 0)
                        a = 0;
                    if (z >= size)
                        z = size - 1;
                    for (int64_t c = a; c <= z; c++)
                        sset(b, (int)c, y);
                }
            } else {                                /* a column: centres on its edges */
                int64_t e1 = sceil(s, x1), e2 = sfloor(s, x2);
                if (x1 == e1)
                    sset(b, y, (int)strunc(s, e1));
                if (x2 == e2)
                    sset(b, y, (int)strunc(s, e2));
            }
        }
        for (int j = 0; j < npend; j++) {
            int k = pend[j];
            const sprof *lp = &s->p[L[k]];
            int64_t lo = lp->up ? lp->start : lp->start - lp->n + 1, hi = lo + lp->n - 1;
            int64_t x1 = lp->x, x2 = s->p[R[k]].x;
            if (x1 > x2) {
                int64_t t = x1;
                x1 = x2;
                x2 = t;
            }
            /* the left profile's first line: its start as FreeType's is */
            sprof tmp = *lp;
            tmp.start = lo;
            drop(s, b, flipped, mode, y, x1, x2, &tmp, L[k], R[k], y == hi);
        }
    }
    win_free(lst);
    win_free(pend);
}

/* the path filled: its bitmap's box from the points' (control points too),
 * rounded so that a centre on its edge is in */
static uint8_t *fill_path(const spath *pa, int mode, int high, int *w, int *h, int *x0, int *top)
{
    *w = *h = *x0 = *top = 0;
    if (!pa->n)
        return NULL;
    int32_t xmin = pa->x[0], xmax = xmin, ymin = pa->y[0], ymax = ymin;
    for (int i = 1; i < pa->n; i++) {
        if (pa->x[i] < xmin) xmin = pa->x[i];
        if (pa->x[i] > xmax) xmax = pa->x[i];
        if (pa->y[i] < ymin) ymin = pa->y[i];
        if (pa->y[i] > ymax) ymax = pa->y[i];
    }
    int bx0 = (xmin >> 6) + (((xmin & 63) + 31) >> 6), bx1 = (xmax >> 6) + (((xmax & 63) + 32) >> 6);
    int by0 = (ymin >> 6) + (((ymin & 63) + 31) >> 6), by1 = (ymax >> 6) + (((ymax & 63) + 32) >> 6);
    if (bx0 == bx1) {
        if (((xmin & 63) + 31) % 64 - 31 + ((xmax & 63) + 32) % 64 - 32 < 0)
            bx0--;
        else
            bx1++;
    }
    if (by0 == by1) {
        if (((ymin & 63) + 31) % 64 - 31 + ((ymax & 63) + 32) % 64 - 32 < 0)
            by0--;
        else
            by1++;
    }
    sbmp b;
    b.w = bx1 - bx0;
    b.h = by1 - by0;
    if (b.w <= 0 || b.h <= 0 || b.w > 1024 || b.h > 1024)
        return NULL;
    b.pitch = (b.w + 7) / 8;
    b.bits = (uint8_t *)win_alloc((size_t)(b.pitch * b.h));
    spath q = *pa;
    q.x = (int32_t *)win_alloc(sizeof(int32_t) * 2 * (size_t)pa->n);
    if (!b.bits || !q.x) {
        win_free(b.bits);
        win_free(q.x);
        return NULL;
    }
    memset(b.bits, 0, (size_t)(b.pitch * b.h));
    q.y = q.x + pa->n;
    for (int i = 0; i < pa->n; i++) {       /* from the bitmap's corner */
        q.x[i] = pa->x[i] - bx0 * 64;
        q.y[i] = pa->y[i] - by0 * 64;
    }
    for (int flipped = 0; flipped < 2; flipped++) {
        scan s;
        memset(&s, 0, sizeof(s));
        s.bits = high ? 12 : 6;
        s.P = (int64_t)1 << s.bits;
        s.half = s.P >> 1;
        s.step = high ? 256 : 32;
        s.jitter = high ? 30 : 2;
        s.miny = 0;
        s.maxy = (int64_t)((flipped ? b.w : b.h) - 1) * s.P;
        make_profiles(&s, &q, flipped);
        if (!s.err)
            sweep(&s, &b, flipped, mode);
        scan_free(&s);
    }
    win_free(q.x);
    *w = b.w;
    *h = b.h;
    *x0 = bx0;
    *top = by1;
    return b.bits;
}

/* the sink's path kept: its points (26.6) and contours, as TrueType has them */
typedef struct {
    spath p;
    int   cap, ccap;
    int   start;                    /* the contour's first point */
} path_pen;

static void pen_point(path_pen *pp, float x, float y, int on)
{
    spath *p = &pp->p;
    if (p->n == pp->cap) {
        int cap = pp->cap ? pp->cap * 2 : 128;
        int32_t *nx = (int32_t *)win_alloc(sizeof(int32_t) * 2 * (size_t)cap);
        uint8_t *no = (uint8_t *)win_alloc((size_t)cap);
        if (!nx || !no) {
            win_free(nx);
            win_free(no);
            return;
        }
        if (p->n) {
            memcpy(nx, p->x, sizeof(int32_t) * (size_t)p->n);
            memcpy(nx + cap, p->y, sizeof(int32_t) * (size_t)p->n);
            memcpy(no, p->on, (size_t)p->n);
        }
        win_free(p->x);
        win_free(p->on);
        p->x = nx;
        p->y = nx + cap;
        p->on = no;
        pp->cap = cap;
    }
    p->x[p->n] = (int32_t)(x * 64.0f + (x < 0 ? -0.5f : 0.5f));
    p->y[p->n] = (int32_t)(y * 64.0f + (y < 0 ? -0.5f : 0.5f));
    p->on[p->n] = (uint8_t)on;
    p->n++;
}

static void path_sink(void *ctx, int op, float x0, float y0, float x1, float y1)
{
    path_pen *pp = (path_pen *)ctx;
    spath *p = &pp->p;
    switch (op) {
    case TT_MOVE:
        pp->start = p->n;
        pen_point(pp, x0, y0, 1);
        break;
    case TT_LINE:
        pen_point(pp, x0, y0, 1);
        break;
    case TT_QUAD:
        pen_point(pp, x0, y0, 0);
        pen_point(pp, x1, y1, 1);
        break;
    default:                                /* the contour: its last point its first's */
        if (p->n - pp->start > 1 && p->x[p->n - 1] == p->x[pp->start] &&
            p->y[p->n - 1] == p->y[pp->start] && (p->on[p->n - 1] & 1))
            p->n--;
        if (p->ncont == pp->ccap) {
            int cap = pp->ccap ? pp->ccap * 2 : 16;
            uint16_t *e = (uint16_t *)win_alloc(sizeof(uint16_t) * (size_t)cap);
            if (!e)
                return;
            if (p->ncont)
                memcpy(e, p->ends, sizeof(uint16_t) * (size_t)p->ncont);
            win_free(p->ends);
            p->ends = e;
            pp->ccap = cap;
        }
        if (p->n > pp->start)
            p->ends[p->ncont++] = (uint16_t)(p->n - 1);
        else
            p->n = pp->start;
        break;
    }
}

static void path_free(path_pen *pp)
{
    win_free(pp->p.x);
    win_free(pp->p.on);
    win_free(pp->p.ends);
    memset(pp, 0, sizeof(*pp));
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
    path_pen pp;
    memset(&pp, 0, sizeof(pp));
    tt_outline(f, glyph, m, path_sink, &pp);
    /* not hinted: what an unhinted outline gets, dropouts without stubs */
    float sc = m[0] < 0 ? -m[0] : m[0];
    uint8_t *bits = fill_path(&pp.p, DROP_SIMPLE_NOSTUBS, sc * (float)f->info.em < 24.0f, w, h,
                              x0, top);
    path_free(&pp);
    return bits;
}

uint8_t *tt_render_hinted(tthint *hint, unsigned glyph, int *w, int *h, int *x0, int *top,
                          int *advance)
{
    static const int modes[] = {
        [TT_DROPOUT_NONE] = DROP_OFF, [TT_DROPOUT_SIMPLE] = DROP_SIMPLE,
        [TT_DROPOUT_SIMPLE_NOSTUBS] = DROP_SIMPLE_NOSTUBS, [TT_DROPOUT_SMART] = DROP_SMART,
        [TT_DROPOUT_SMART_NOSTUBS] = DROP_SMART_NOSTUBS,
    };
    ttpath p;
    int dropout = 0;
    *w = *h = *x0 = *top = 0;
    if (!tt_hint_path(hint, glyph, &p, advance, &dropout))
        return NULL;
    uint8_t *bits = fill_path(&p, modes[dropout], tt_hint_ppem(hint) < 24, w, h, x0, top);
    tt_path_free(&p);
    return bits;
}

int tt_hdmx(const ttfont *f, int ppem, unsigned glyph)
{
    uint32_t len = 0, at = table(f, "hdmx", &len);
    if (!at || len < 8 || glyph >= (unsigned)f->nglyphs)
        return -1;
    const uint8_t *d = f->d + at;
    unsigned records = be16(d + 2);
    uint32_t size = be32(d + 4);
    if (size < 2 + (uint32_t)f->nglyphs || 8 + (uint64_t)records * size > len)
        return -1;
    for (unsigned i = 0; i < records; i++) {
        const uint8_t *r = d + 8 + i * size;
        if (r[0] == ppem)
            return r[2 + glyph];
    }
    return -1;
}
