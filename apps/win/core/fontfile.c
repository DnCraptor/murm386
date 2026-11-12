/*
 * fontfile.c - Windows' fonts: raster ones, .FON files (an NE file's
 * RT_FONT resources) and .FNT files, versions 2 and 3; TrueType ones,
 * .TTF files and the .FOT files that name them (truetype.c draws them).
 * The ones WIN.INI's [fonts] names (in the Windows directory, as Windows
 * has them), and those AddFontResource adds; each face's sizes (a
 * TrueType one: any) known from the start, its glyphs read when it is
 * first drawn.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>

#include "wincore.h"
#include "fontfile.h"
#include "truetype.h"

#define MAX_FACES 64

static wface *faces[MAX_FACES];
static int nfaces, scanned;

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }

/* the header of a font resource (FNT): a face of it, or NULL (a vector
 * font, not one) */
static wface *face_of(const uint8_t *d, uint32_t n, const char *path, int nth)
{
    if (n < 118)
        return NULL;
    unsigned ver = u16(d), type = u16(d + 66);
    if ((ver != 0x200 && ver != 0x300) || (type & 1) || (ver == 0x300 && n < 148))
        return NULL;
    unsigned height = u16(d + 88), first = d[95], last = d[96];
    if (!height || height > 255 || last < first)
        return NULL;
    uint32_t table = ver == 0x300 ? 148 : 118, entry = ver == 0x300 ? 6 : 4;
    if (table + entry * (last - first + 2) > n)
        return NULL;
    wface *f = (wface *)win_alloc(sizeof(*f));
    if (!f)
        return NULL;
    lstrcpyn(f->file, path, sizeof(f->file));
    f->nth = nth;
    uint32_t face = u32(d + 105);
    if (face && face < n) {
        uint32_t k = 0;
        while (face + k < n && d[face + k] && k + 1 < sizeof(f->name)) {
            f->name[k] = (char)d[face + k];
            k++;
        }
        f->name[k] = 0;
    }
    f->v3 = ver == 0x300;
    f->points = (int)u16(d + 68);
    f->height = (int)height;
    f->ascent = (int)u16(d + 74);
    f->ileading = (int)u16(d + 76);
    f->eleading = (int)u16(d + 78);
    f->italic = d[80];
    f->weight = (int)u16(d + 83);
    f->charset = d[85];
    f->pixwidth = (int)u16(d + 86);
    f->pitch_family = d[90];
    f->avgw = (int)u16(d + 91);
    f->maxw = (int)u16(d + 93);
    f->first = (int)first;
    f->last = (int)last;
    f->defchar = (int)(first + d[97]);
    f->breakchar = (int)(first + d[98]);
    return f;
}

/* an NE file's RT_FONT resources: the nth's place in it */
static int ne_font(const uint8_t *d, uint32_t n, int nth, uint32_t *at, uint32_t *len)
{
    if (n < 0x40 || u16(d) != 0x5a4d)
        return 0;
    uint32_t ne = u32(d + 0x3c);
    if (ne + 0x40 > n || u16(d + ne) != 0x454e)
        return 0;
    uint32_t rt = ne + u16(d + ne + 0x24);
    if (rt + 2 > n)
        return 0;
    unsigned shift = u16(d + rt);
    uint32_t p = rt + 2;
    while (p + 8 <= n && u16(d + p)) {
        unsigned type = u16(d + p), count = u16(d + p + 2);
        p += 8;
        for (unsigned i = 0; i < count && p + 12 <= n; i++, p += 12)
            if (type == 0x8008 && nth-- == 0) {
                *at = (uint32_t)u16(d + p) << shift;
                *len = (uint32_t)u16(d + p + 2) << shift;
                if (*at >= n)
                    return 0;
                if (*len > n - *at)     /* the last one's alignment: past the end */
                    *len = n - *at;
                return 1;
            }
    }
    return 0;
}

/* a face of the same name, size, weight and character set there already
 * (the same file under another name, another copy of it) */
static int known(const wface *f)
{
    for (int i = 0; i < nfaces; i++) {
        const wface *k = faces[i];
        if (!lstrcmpi(k->name, f->name) && k->height == f->height && k->weight == f->weight &&
            k->italic == f->italic && k->charset == f->charset && k->tt == f->tt)
            return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- TrueType */

/* a TrueType file's face (its bytes read, kept until it is drawn) */
static wface *tt_face(const char *path, uint8_t *d, uint32_t n)
{
    ttfont *t = tt_open(d, n);
    if (!t) {
        win_free(d);
        return NULL;
    }
    const ttinfo *in = tt_info(t);
    wface *f = (wface *)win_alloc(sizeof(*f));
    if (!f) {
        tt_close(t);
        return NULL;
    }
    lstrcpyn(f->name, in->family, sizeof(f->name));
    lstrcpyn(f->file, path, sizeof(f->file));
    f->nth = -1;
    f->tt = 1;
    f->weight = in->weight;
    f->italic = in->italic;
    f->charset = in->symbol ? SYMBOL_CHARSET : ANSI_CHARSET;
    /* bit 0 variable pitch (sic), 2 a vector font, 4 TrueType */
    f->pitch_family = in->family_class | (in->fixed ? 0 : 1) | 2 | 4;
    f->first = 0x20;
    f->last = 0xff;
    f->defchar = 0x1f;
    f->breakchar = 0x20;
    tt_close(t);                    /* d too: read again when it is drawn */
    return f;
}

/* a .FOT file's TrueType file: the name in it that ends ".TTF" (where it
 * says, else in the Windows or the system directory); 0 if none */
static int fot_target(const uint8_t *d, uint32_t n, char *path, int cap)
{
    for (uint32_t i = 4; i + 4 <= n; i++) {
        if (d[i] != '.' || (d[i + 1] | 0x20) != 't' || (d[i + 2] | 0x20) != 't' ||
            (d[i + 3] | 0x20) != 'f' || (i + 4 < n && d[i + 4] > ' '))
            continue;
        uint32_t s = i;
        while (s > 0 && d[s - 1] > ' ' && d[s - 1] < 0x7f && i + 4 - s < 120)
            s--;
        char name[128];
        uint32_t len = i + 4 - s;
        memcpy(name, d + s, len);
        name[len] = 0;
        if (win_file_attr(name, 0, 0) >= 0) {
            lstrcpyn(path, name, cap);
            return 1;
        }
        const char *base = name;                    /* its name alone */
        for (const char *c = name; *c; c++)
            if (*c == '\\' || *c == '/' || *c == ':')
                base = c + 1;
        win_find_path(base, path, cap);
        return 1;
    }
    return 0;
}

/* the face of a TrueType file, or of the one a .FOT file names; 1 if
 * there is one (d is the file's, freed or kept) */
static int add_tt(const char *path, uint8_t *d, uint32_t n)
{
    char ttf[128];
    if (!tt_is_ttf(d, n)) {
        if (!fot_target(d, n, ttf, sizeof(ttf))) {
            win_free(d);
            return 0;
        }
        win_free(d);
        d = win_file_load(ttf, &n, 0x1000000u);
        path = ttf;
        if (!d || !tt_is_ttf(d, n)) {
            win_free(d);
            return 0;
        }
    }
    wface *f = tt_face(path, d, n);     /* frees d */
    if (!f)
        return 0;
    if (nfaces < MAX_FACES && !known(f))
        faces[nfaces++] = f;
    else
        win_free(f);
    return 1;
}

/* a .FOT file: an NE file with "FONTRES" in it and a TrueType file's name */
static int is_fot(const uint8_t *d, uint32_t n)
{
    char ttf[128];
    if (n < 0x40 || u16(d) != 0x5a4d)
        return 0;
    for (uint32_t i = 0; i + 7 <= n; i++)
        if (!memcmp(d + i, "FONTRES", 7))
            return fot_target(d, n, ttf, sizeof(ttf));
    return 0;
}

struct ttfont *font_tt(wface *f)
{
    if (!f->tt)
        return NULL;
    if (f->ttf || f->bad)
        return f->ttf;
    uint32_t n = 0;
    uint8_t *d = win_file_load(f->file, &n, 0x1000000u);
    f->ttf = d ? tt_open(d, n) : NULL;
    if (!f->ttf) {
        win_free(d);
        f->bad = 1;
    }
    return f->ttf;
}

int font_tt_any(void)
{
    wface *const *list;
    int n = font_faces(&list);
    for (int i = 0; i < n; i++)
        if (list[i]->tt)
            return 1;
    return 0;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }

/* the .FOT file: an NE library, "FONTRES 100,96,96 : face" its
 * non-resident name, one RT_FONT (a font header without glyphs, its
 * device name the TrueType file) */
int font_make_fot(const char *fot, const char *ttf, const char *path)
{
    char full[160];
    if (!fot || !ttf || !*fot || !*ttf)
        return 0;
    full[0] = 0;
    if (path && *path) {
        lstrcpyn(full, path, sizeof(full) - 16);
        int n = lstrlen(full);
        if (full[n - 1] != '\\' && full[n - 1] != '/' && full[n - 1] != ':')
            lstrcpy(full + n, "\\");
    }
    if (lstrlen(full) + lstrlen(ttf) >= (int)sizeof(full))
        return 0;
    lstrcpy(full + lstrlen(full), ttf);
    uint32_t n = 0;
    uint8_t *d = win_file_load(full, &n, 0x1000000u);
    if (!d && full[0] && full[1] != ':' && full[0] != '\\') {
        char found[160];                /* not here: the Windows, the system directory's */
        win_find_path(full, found, sizeof(found));
        d = win_file_load(found, &n, 0x1000000u);
    }
    struct ttfont *t = d ? tt_open(d, n) : NULL;
    if (!t) {
        win_free(d);
        return 0;
    }
    char face[LF_FACESIZE];
    lstrcpyn(face, tt_info(t)->family, sizeof(face));
    tt_close(t);
    /* the module's name: the .FOT file's, upper case, 8 letters at most */
    char module[9];
    const char *base = fot;
    for (const char *c = fot; *c; c++)
        if (*c == '\\' || *c == '/' || *c == ':')
            base = c + 1;
    int m = 0;
    for (; base[m] && base[m] != '.' && m < 8; m++)
        module[m] = (char)(base[m] >= 'a' && base[m] <= 'z' ? base[m] - 32 : base[m]);
    module[m] = 0;
    char desc[96];
    lstrcpy(desc, "FONTRES 100,96,96 : ");
    lstrcpyn(desc + lstrlen(desc), face, LF_FACESIZE);
    int devlen = lstrlen(full) + 1, facelen = lstrlen(face) + 1, desclen = lstrlen(desc);
    uint32_t fontlen = 118u + (uint32_t)(devlen + facelen);
    enum { MZ = 0x80, NE = 0x40, RES = 24, SHIFT = 4 };
    uint32_t resname = (uint32_t)(1 + m + 2 + 1), entry = 2;
    uint32_t nonres_at = MZ + NE + RES + resname + entry;
    uint32_t font_at = (nonres_at + 1u + (uint32_t)desclen + 3u + 15u) & ~15u;
    uint32_t total = (font_at + fontlen + 15u) & ~15u;
    uint8_t *b = (uint8_t *)win_alloc(total);
    if (!b)
        return 0;
    memset(b, 0, total);
    put16(b, 0x5a4d);                               /* MZ */
    put16(b + 2, 0x80);
    put16(b + 4, 1);
    put16(b + 8, 4);
    put16(b + 0x18, 0x40);
    put32(b + 0x3c, MZ);
    uint8_t *ne = b + MZ;
    put16(ne, 0x454e);                              /* NE */
    ne[2] = 5;
    ne[3] = 10;
    put16(ne + 0x04, NE + RES + resname);           /* the entry table: empty */
    put16(ne + 0x06, entry);
    put16(ne + 0x0c, 0x8000);                       /* a library */
    put16(ne + 0x20, (unsigned)(1 + desclen + 3));
    put16(ne + 0x22, NE);                           /* no segments */
    put16(ne + 0x24, NE);                           /* the resources */
    put16(ne + 0x26, NE + RES);
    put16(ne + 0x28, NE + RES + resname + entry);
    put16(ne + 0x2a, NE + RES + resname + entry);
    put32(ne + 0x2c, nonres_at);
    put16(ne + 0x32, SHIFT);
    ne[0x36] = 2;                                   /* Windows */
    put16(ne + 0x3e, 0x30a);
    uint8_t *res = ne + NE;
    put16(res, SHIFT);
    put16(res + 2, 0x8008);                         /* RT_FONT, one */
    put16(res + 4, 1);
    put16(res + 10, (unsigned)(font_at >> SHIFT));
    put16(res + 12, (unsigned)((fontlen + 15u) >> SHIFT));
    put16(res + 14, 0x0c30);
    put16(res + 16, 0x8001);
    uint8_t *rn = res + RES;
    rn[0] = (uint8_t)m;
    memcpy(rn + 1, module, (size_t)m);
    uint8_t *nr = b + nonres_at;
    nr[0] = (uint8_t)desclen;
    memcpy(nr + 1, desc, (size_t)desclen);
    uint8_t *fh = b + font_at;
    put16(fh, 0x200);                               /* dfVersion */
    put32(fh + 2, fontlen);                         /* dfSize */
    fh[66] = 3;                                     /* dfType: an outline font's */
    put16(fh + 68, 12);                             /* dfPoints */
    put16(fh + 70, 96);
    put16(fh + 72, 96);
    put32(fh + 101, 118);                           /* dfDevice: the TrueType file */
    put32(fh + 105, 118u + (uint32_t)devlen);       /* dfFace */
    memcpy(fh + 118, full, (size_t)devlen);
    memcpy(fh + 118 + devlen, face, (size_t)facelen);
    int ok = win_file_save(fot, b, total);
    win_free(b);
    return ok;
}

/* the sizes drawn, the latest used first */
#define TT_SIZES 12

typedef struct ttsize {
    wface   *face;
    int      key;
    float    scale;
    wttsize  m;
    struct {
        uint8_t  done;
        int16_t  adv, w, h, x0, top;
        uint8_t *bits;
    } g[256];
} ttsize;

static ttsize *sizes[TT_SIZES];

static int round_px(float v) { return (int)(v + 0.5f); }

static ttsize *tt_size(wface *f, int key)
{
    for (int i = 0; i < TT_SIZES && sizes[i]; i++)
        if (sizes[i]->face == f && sizes[i]->key == key) {
            ttsize *s = sizes[i];               /* to the front */
            memmove(&sizes[1], &sizes[0], sizeof(sizes[0]) * (size_t)i);
            sizes[0] = s;
            return s;
        }
    struct ttfont *t = font_tt(f);
    if (!t)
        return NULL;
    const ttinfo *in = tt_info(t);
    ttsize *s = (ttsize *)win_alloc(sizeof(*s));
    if (!s)
        return NULL;
    int cell = in->ascent + in->descent;
    float px = key < 0 ? (float)-key : key > 0 ? (float)key : 16.0f;
    if (px > 400)
        px = 400;
    s->scale = key < 0 ? px / (float)in->em : px / (float)cell;
    s->face = f;
    s->key = key;
    s->m.ascent = round_px((float)in->ascent * s->scale);
    s->m.height = s->m.ascent + round_px((float)in->descent * s->scale);
    if (s->m.height < 1)
        s->m.height = 1;
    s->m.ileading = s->m.height - round_px((float)in->em * s->scale);
    if (s->m.ileading < 0)
        s->m.ileading = 0;
    s->m.eleading = round_px((float)in->line_gap * s->scale);
    s->m.avgw = round_px((float)in->avgw * s->scale);
    s->m.maxw = round_px((float)in->maxw * s->scale);
    if (s->m.avgw < 1)
        s->m.avgw = 1;
    if (s->m.maxw < s->m.avgw)
        s->m.maxw = s->m.avgw;
    ttsize *old = sizes[TT_SIZES - 1];              /* the least used, gone */
    if (old) {
        for (int c = 0; c < 256; c++)
            win_free(old->g[c].bits);
        win_free(old);
    }
    memmove(&sizes[1], &sizes[0], sizeof(sizes[0]) * (TT_SIZES - 1));
    sizes[0] = s;
    return s;
}

/* the character's glyph at the size, drawn the first time */
static int tt_char(ttsize *s, unsigned char ch)
{
    if (s->g[ch].done)
        return 1;
    struct ttfont *t = font_tt(s->face);
    if (!t)
        return 0;
    unsigned gi = tt_glyph_index(t, ch);
    int w, h, x0, top;
    s->g[ch].bits = tt_render(t, gi, s->scale, &w, &h, &x0, &top);
    s->g[ch].w = (int16_t)w;
    s->g[ch].h = (int16_t)h;
    s->g[ch].x0 = (int16_t)x0;
    s->g[ch].top = (int16_t)top;
    s->g[ch].adv = (int16_t)round_px((float)tt_advance(t, gi) * s->scale);
    s->g[ch].done = 1;
    return 1;
}

const wttsize *font_tt_size(wface *f, int key)
{
    ttsize *s = tt_size(f, key);
    return s ? &s->m : NULL;
}

float font_tt_scale(wface *f, int key)
{
    ttsize *s = tt_size(f, key);
    return s ? s->scale : 0.0f;
}

int font_tt_width(wface *f, int key, unsigned char ch)
{
    ttsize *s = tt_size(f, key);
    return s && tt_char(s, ch) ? s->g[ch].adv : 0;
}

int font_tt_glyph(wface *f, int key, unsigned char ch, const uint8_t **rows, int *w, int *h,
                  int *x0, int *top)
{
    ttsize *s = tt_size(f, key);
    if (!s || !tt_char(s, ch) || !s->g[ch].bits)
        return 0;
    *rows = s->g[ch].bits;
    *w = s->g[ch].w;
    *h = s->g[ch].h;
    *x0 = s->g[ch].x0;
    *top = s->g[ch].top;
    return 1;
}

/* the font file's faces into the list: their number */
static int add_file(const char *path)
{
    uint32_t n = 0;
    uint8_t *d = win_file_load(path, &n, 0x1000000u);
    int added = 0;
    if (!d)
        return 0;
    if (tt_is_ttf(d, n) || is_fot(d, n))                    /* TrueType */
        return add_tt(path, d, n);
    if (n >= 2 && (u16(d) == 0x200 || u16(d) == 0x300)) {      /* a .FNT */
        wface *f = face_of(d, n, path, -1);
        if (f)
            added++;
        if (f && nfaces < MAX_FACES && !known(f))
            faces[nfaces++] = f;
        else
            win_free(f);
    } else {
        uint32_t at, len;
        for (int i = 0; nfaces < MAX_FACES && ne_font(d, n, i, &at, &len); i++) {
            wface *f = face_of(d + at, len, path, i);
            if (f)
                added++;
            if (f && !known(f))
                faces[nfaces++] = f;
            else
                win_free(f);
        }
    }
    win_free(d);
    return added;
}

/* WIN.INI's [fonts]: "MS Sans Serif 8,10,12,14,18,24 (VGA res)=SSERIFE.FON" */
static void scan(void)
{
    if (scanned)
        return;
    scanned = 1;
    char keys[1024];
    int n = GetProfileString("fonts", NULL, "", keys, sizeof(keys));
    for (int k = 0; k < n && keys[k]; k += lstrlen(keys + k) + 1) {
        char file[80], path[128];
        if (GetProfileString("fonts", keys + k, "", file, sizeof(file)) <= 0)
            continue;
        win_find_path(file, path, sizeof(path));      /* Windows has them in SYSTEM */
        add_file(path);
    }
}

int font_faces(wface *const **list)
{
    scan();
    *list = faces;
    return nfaces;
}

/* a font file's name: as given if there is one, else the Windows
 * directory's or the system one's */
static void font_path(const char *name, char *path, int size)
{
    uint32_t n = 0;
    uint8_t *d = win_file_load(name, &n, 4);
    if (d) {
        win_free(d);
        lstrcpyn(path, name, size);
    } else {
        win_find_path(name, path, size);
    }
}

int WINAPI AddFontResource(LPCSTR file)
{
    char path[128];
    if (!file || !*file)
        return 0;
    scan();
    font_path(file, path, sizeof(path));
    for (int i = 0; i < nfaces; i++)    /* added already: its count */
        if (!lstrcmpi(faces[i]->file, path)) {
            int c = 0;
            for (int j = 0; j < nfaces; j++)
                c += !lstrcmpi(faces[j]->file, path);
            return c;
        }
    return add_file(path);
}

BOOL WINAPI RemoveFontResource(LPCSTR file)
{
    char path[128];
    if (!file || !*file)
        return FALSE;
    scan();
    font_path(file, path, sizeof(path));
    int k = 0, gone = 0;
    for (int i = 0; i < nfaces; i++) {
        wface *f = faces[i];
        if (!lstrcmpi(f->file, path) && !f->used) {
            win_free(f->data);
            tt_close(f->ttf);
            win_free(f);
            gone++;
        } else {
            faces[k++] = f;
        }
    }
    nfaces = k;
    return gone > 0;
}

const uint8_t *font_data(wface *f)
{
    if (f->data || f->bad)
        return f->data;
    uint32_t n = 0, at = 0, len;
    uint8_t *d = win_file_load(f->file, &n, 0x100000u);
    if (!d) {
        f->bad = 1;
        return NULL;
    }
    len = n;
    if (f->nth >= 0 && !ne_font(d, n, f->nth, &at, &len))
        len = 0;
    f->data = len ? (uint8_t *)win_alloc(len) : NULL;
    if (f->data) {
        memcpy(f->data, d + at, len);
        f->size = len;
    } else {
        f->bad = 1;
    }
    win_free(d);
    return f->data;
}

int font_glyph(wface *f, unsigned ch, const uint8_t **cols, int *width)
{
    const uint8_t *d = font_data(f);
    if (!d)
        return 0;
    if ((int)ch < f->first || (int)ch > f->last)
        ch = (unsigned)f->defchar;
    if ((int)ch < f->first || (int)ch > f->last)
        return 0;
    uint32_t e = f->v3 ? 148 + 6 * (ch - (unsigned)f->first) : 118 + 4 * (ch - (unsigned)f->first);
    unsigned w = u16(d + e);
    uint32_t off = f->v3 ? u32(d + e + 2) : u16(d + e + 2);
    uint32_t bytes = (w + 7) / 8 * (uint32_t)f->height;
    if (off > f->size || bytes > f->size - off)
        return 0;
    *cols = d + off;
    *width = (int)w;
    return 1;
}
