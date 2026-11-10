/*
 * gdi_text.c - fonts and text output.
 *
 * The faces of Windows' raster font files WIN.INI's [fonts] names or
 * AddFontResource adds (fontfile.c), at their sizes, made larger by whole
 * numbers; else the 8x16 VGA glyphs (CP866), larger the same way. ANSI
 * text is Windows-1251 and is mapped to those glyphs; OEM_CHARSET fonts
 * take bytes as CP866. The proportional "System" face is made by trimming
 * each glyph's empty columns; the fixed faces use the full 8-pixel cell.
 * Bold is synthesized by a second pass one pixel to the right, italic by
 * slanting the rows.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "fontfile.h"

extern const uint8_t win_font8x16[4096];

#define CELL_H   16
#define ASCENT   13
#define DESCENT  3

/* Windows-1251 0x80..0xFF -> CP866 glyph. */
static const uint8_t ansi_to_oem[128] = {
    /* 80 */ '?', '?', '\'', '?', '"', 0xFA, '+', '+', '?', '%', '?', '<', '?', '?', '?', '?',
    /* 90 */ '?', '\'', '\'', '"', '"', 0xF9, '-', '-', '?', 'T', '?', '>', '?', '?', '?', '?',
    /* A0 */ 0xFF, 0xF6, 0xF7, 'J', 0xFD, '?', '|', 0x15, 0xF0, 'c', 0xF2, '<', 0xAA, '-', 'R', 0xF4,
    /* B0 */ 0xF8, '+', 'I', 'i', '?', 0xE6, 0x14, 0xFA, 0xF1, 0xFC, 0xF3, '>', 'j', 'S', 's', 0xF5,
    /* C0 */ 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    /* D0 */ 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    /* E0 */ 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    /* F0 */ 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
};

uint8_t win_ansi_to_oem(uint8_t c)
{
    return c < 0x80 ? c : ansi_to_oem[c - 0x80];
}

/* the first ANSI character drawn with the glyph; '?' for those none is */
uint8_t win_oem_to_ansi(uint8_t c)
{
    if (c < 0x80)
        return c;
    for (int i = 0; i < 128; i++)
        if (ansi_to_oem[i] == c)
            return (uint8_t)(0x80 + i);
    return '?';
}

/* Per glyph: first used column and used width (proportional face). */
static uint8_t glyph_left[256], glyph_width[256];

void gdi_text_init(void)
{
    for (int c = 0; c < 256; c++) {
        unsigned bits = 0;
        for (int y = 0; y < CELL_H; y++)
            bits |= win_font8x16[c * CELL_H + y];
        if (!bits) {
            glyph_left[c] = 0;
            glyph_width[c] = 4;         /* blank: space-sized */
            continue;
        }
        int l = 0, r = 7;
        while (!(bits & (1u << l))) l++;
        while (!(bits & (1u << r))) r--;
        glyph_left[c] = (uint8_t)l;
        glyph_width[c] = (uint8_t)(r - l + 1);
    }

    static const struct { int idx; int fixed; int oem; const char *face; } faces[] = {
        { SYSTEM_FONT, 0, 0, "System" },
        { ANSI_VAR_FONT, 0, 0, "MS Sans Serif" },
        { DEVICE_DEFAULT_FONT, 0, 0, "System" },
        { SYSTEM_FIXED_FONT, 1, 0, "Fixedsys" },
        { ANSI_FIXED_FONT, 1, 0, "Courier" },
        { OEM_FIXED_FONT, 1, 1, "Terminal" },
    };
    for (unsigned i = 0; i < sizeof(faces) / sizeof(faces[0]); i++) {
        LOGFONT lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = CELL_H;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = faces[i].oem ? OEM_CHARSET : ANSI_CHARSET;
        lf.lfPitchAndFamily = faces[i].fixed ? FIXED_PITCH : VARIABLE_PITCH;
        lstrcpyn(lf.lfFaceName, faces[i].face, LF_FACESIZE);
        HFONT h = CreateFontIndirect(&lf);
        win_sys.stock[faces[i].idx] = h;
        win_handle_set_stock(h);
    }
}

HFONT WINAPI CreateFontIndirect(const LOGFONT *lf)
{
    wfont *f = (wfont *)win_alloc(sizeof(*f));
    if (!f)
        return 0;
    HFONT h = win_handle_alloc(WOBJ_FONT, f);
    if (!h) {
        win_free(f);
        return 0;
    }
    f->lf = *lf;
    const char *n = lf->lfFaceName;
    f->fixed = (lf->lfPitchAndFamily & 3) == FIXED_PITCH ||
               !lstrcmpi(n, "Courier") || !lstrcmpi(n, "Courier New") ||
               !lstrcmpi(n, "Fixedsys") || !lstrcmpi(n, "Terminal");
    f->bold = lf->lfWeight >= 600;
    f->height = CELL_H;
    memset(&f->real, 0, sizeof(f->real));
    return h;
}

HFONT WINAPI CreateFont(int h, int w, int esc, int orient, int weight,
                        BYTE italic, BYTE underline, BYTE strike, BYTE charset,
                        BYTE outprec, BYTE clipprec, BYTE quality, BYTE pitch,
                        LPCSTR face)
{
    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = h; lf.lfWidth = w; lf.lfEscapement = esc; lf.lfOrientation = orient;
    lf.lfWeight = weight; lf.lfItalic = italic; lf.lfUnderline = underline;
    lf.lfStrikeOut = strike; lf.lfCharSet = charset; lf.lfOutPrecision = outprec;
    lf.lfClipPrecision = clipprec; lf.lfQuality = quality; lf.lfPitchAndFamily = pitch;
    if (face)
        lstrcpyn(lf.lfFaceName, face, LF_FACESIZE);
    return CreateFontIndirect(&lf);
}

const wfont *gdi_font(HFONT h)
{
    const wfont *f = (const wfont *)win_handle_get(h, WOBJ_FONT);
    return f ? f : (const wfont *)win_handle_get(win_sys.stock[SYSTEM_FONT], WOBJ_FONT);
}

static inline unsigned glyph_of(const wfont *f, unsigned char ch)
{
    if (ch < 0x80 || f->lf.lfCharSet == OEM_CHARSET)
        return ch;
    return ansi_to_oem[ch - 0x80];
}

/* ---------------------------------------------------------------- realizing */

/* face names that stand for each other: the first the second */
static const char *const alias[][2] = {
    { "Helv", "MS Sans Serif" }, { "Helvetica", "MS Sans Serif" },
    { "Arial", "MS Sans Serif" }, { "Tms Rmn", "MS Serif" }, { "Times", "MS Serif" },
    { "Times New Roman", "MS Serif" }, { "Courier New", "Courier" },
};

static const char *canonical(const char *name)
{
    for (unsigned i = 0; i < sizeof(alias) / sizeof(alias[0]); i++)
        if (!lstrcmpi(name, alias[i][0]))
            return alias[i][1];
    return name;
}

static int same_face(const char *a, const char *b)
{
    return !lstrcmpi(a, b) || !lstrcmpi(canonical(a), canonical(b));
}

/* the VGA glyphs' own names: theirs unless a font file has the face */
static int vga_name(const char *n)
{
    return !*n || !lstrcmpi(n, "System") || !lstrcmpi(n, "Fixedsys") || !lstrcmpi(n, "Terminal");
}

/* the font files' face nearest the font asked for at height key (pixels;
 * < 0 the characters' height, > 0 the cell's, 0 the default, 8 points),
 * and the whole number it is made larger by; NULL: the VGA glyphs' */
static wface *pick(const LOGFONT *lf, int key, int *scale)
{
    wface *const *list;
    int n = font_faces(&list);
    int target = key < 0 ? -key : key, cell = key >= 0;
    if (!target)
        target = 13;
    int fam = lf->lfPitchAndFamily & 0xf0, fixed = (lf->lfPitchAndFamily & 3) == FIXED_PITCH;
    int oem = lf->lfCharSet == OEM_CHARSET;
    wface *best = NULL;
    long best_score = 0;
    for (int pass = 0; pass < 2 && !best; pass++) {
        if (pass && (vga_name(lf->lfFaceName) || (!fam && !fixed)))
            break;                      /* no face by its name: the family's */
        for (int i = 0; i < n; i++) {
            wface *f = list[i];
            if (!pass && !same_face(lf->lfFaceName, f->name))
                continue;
            if (pass && ((fam && (f->pitch_family & 0xf0) != fam) ||
                         (fixed && (f->pitch_family & 1)) || oem != (f->charset == OEM_CHARSET)))
                continue;
            int h = cell ? f->height : f->height - f->ileading;
            if (h <= 0)
                continue;
            int s = target >= 2 * h ? target / h : 1;
            if (s > 8)
                s = 8;
            int got = h * s;
            long score = got <= target ? (long)(target - got) * 4 : (long)(got - target) * 6 + 2;
            score += (lf->lfWeight >= 600) != (f->weight >= 600) ? 3 : 0;
            score += !lf->lfItalic && f->italic ? 100 : lf->lfItalic && !f->italic ? 3 : 0;
            if (!best || score < best_score) {
                best = f;
                best_score = score;
                *scale = s;
            }
        }
    }
    return best;
}

/* the font as the DC has it: its height in the DC's units on the device */
static const wreal *realize(const wdc *dc, const wfont *cf)
{
    wfont *f = (wfont *)cf;
    int key = f->lf.lfHeight;
    if (dc && dc->mapmode != MM_TEXT && key) {
        int d = gdi_ly_to_dy((wdc *)dc, key < 0 ? -key : key);
        d = d < 0 ? -d : d;
        key = key < 0 ? -(d ? d : 1) : (d ? d : 1);
    }
    if (f->real.height && f->real.key == key)
        return &f->real;
    wreal r;
    memset(&r, 0, sizeof(r));
    r.key = key;
    int scale = 1;
    wface *face = pick(&f->lf, key, &scale);
    if (face && font_data(face)) {
        face->used = 1;
        r.face = face;
        r.scale = scale;
        r.bold = f->lf.lfWeight >= 600 && face->weight < 600;
        r.italic = f->lf.lfItalic && !face->italic;
        r.fixed = !(face->pitch_family & 1);
        r.height = face->height * scale;
        r.ascent = face->ascent * scale;
        r.ileading = face->ileading * scale;
        r.eleading = face->eleading * scale;
        r.avgw = face->avgw * scale + r.bold;
        r.maxw = face->maxw * scale + r.bold;
    } else {
        int c = key > 0 ? key : key < 0 ? -key + 2 : CELL_H;
        scale = c >= 2 * CELL_H ? c / CELL_H : 1;
        if (scale > 8)
            scale = 8;
        r.scale = scale;
        r.bold = f->bold;
        r.italic = f->lf.lfItalic != 0;
        r.fixed = f->fixed;
        r.height = CELL_H * scale;
        r.ascent = ASCENT * scale;
        r.ileading = 2 * scale;
        r.avgw = (f->fixed ? 8 : glyph_width[glyph_of(f, 'x')] + 1) * scale + r.bold;
        r.maxw = (8 + (f->fixed ? 0 : 1)) * scale + r.bold;
    }
    f->real = r;
    return &f->real;
}

static int char_width(const wreal *r, const wfont *f, unsigned char ch)
{
    int w;
    if (!r->face) {
        w = r->fixed ? 8 : glyph_width[glyph_of(f, ch)] + 1;
    } else {
        const uint8_t *cols;
        if (!font_glyph(r->face, ch, &cols, &w))
            w = r->face->avgw;
    }
    return w * r->scale + r->bold;
}

int gdi_char_width(const wfont *f, unsigned char ch)
{
    return char_width(realize(NULL, f), f, ch);
}

static int text_width(const wreal *r, const wfont *f, LPCSTR s, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++)
        w += char_width(r, f, (unsigned char)s[i]);
    return w;
}

/* the DC, its font, and that realized for it */
static wdc *text_dc(HDC h, const wfont **f, const wreal **r)
{
    wdc *dc = gdi_dc(h);
    if (dc) {
        *f = gdi_font(dc->font);
        *r = realize(dc, *f);
    }
    return dc;
}

BOOL WINAPI GetTextExtentPoint(HDC h, LPCSTR s, int n, LPSIZE sz)
{
    const wfont *f;
    const wreal *r;
    wdc *dc = text_dc(h, &f, &r);
    if (!dc || !sz)
        return FALSE;
    if (n < 0)
        n = lstrlen(s);
    sz->cx = text_width(r, f, s, n) + dc->char_extra * n;
    sz->cy = r->height;
    if (dc->mapmode != MM_TEXT) {       /* in logical units */
        sz->cx = gdi_dx_to_lx(dc, sz->cx);
        sz->cy = gdi_dy_to_ly(dc, sz->cy);
        if (sz->cx < 0) sz->cx = -sz->cx;
        if (sz->cy < 0) sz->cy = -sz->cy;
    }
    return TRUE;
}

DWORD WINAPI GetTextExtent(HDC h, LPCSTR s, int n)
{
    SIZE sz = { 0, 0 };
    GetTextExtentPoint(h, s, n, &sz);
    return (DWORD)(WORD)sz.cx | ((DWORD)(WORD)sz.cy << 16);
}

/* a font's metrics, in pixels */
static void font_metrics(const wreal *r, const wfont *f, LPTEXTMETRIC tm)
{
    const wface *face = r->face;
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = r->height;
    tm->tmAscent = r->ascent;
    tm->tmDescent = r->height - r->ascent;
    tm->tmInternalLeading = r->ileading;
    tm->tmExternalLeading = r->eleading;
    tm->tmAveCharWidth = r->avgw;
    tm->tmMaxCharWidth = r->maxw;
    tm->tmWeight = r->bold || (!face && f->bold) ? FW_BOLD : face ? face->weight : FW_NORMAL;
    tm->tmItalic = (BYTE)(face ? face->italic || r->italic : f->lf.lfItalic);
    tm->tmUnderlined = f->lf.lfUnderline;
    tm->tmStruckOut = f->lf.lfStrikeOut;
    tm->tmFirstChar = (BYTE)(face ? face->first : 0x20);
    tm->tmLastChar = (BYTE)(face ? face->last : 0xFF);
    tm->tmDefaultChar = (BYTE)(face ? face->defchar : '?');
    tm->tmBreakChar = (BYTE)(face ? face->breakchar : ' ');
    /* bit 0 set means VARIABLE pitch (sic, Windows) */
    tm->tmPitchAndFamily = (BYTE)(face ? face->pitch_family : r->fixed ? 0 : 1);
    tm->tmCharSet = (BYTE)(face ? face->charset : f->lf.lfCharSet);
    tm->tmOverhang = r->bold;
    tm->tmDigitizedAspectX = tm->tmDigitizedAspectY = 96;
}

BOOL WINAPI GetTextMetrics(HDC h, LPTEXTMETRIC tm)
{
    const wfont *f;
    const wreal *r;
    wdc *dc = text_dc(h, &f, &r);
    if (!dc || !tm)
        return FALSE;
    font_metrics(r, f, tm);
    if (dc->mapmode != MM_TEXT) {       /* in logical units */
        int *v[] = { &tm->tmHeight, &tm->tmAscent, &tm->tmDescent, &tm->tmInternalLeading,
                     &tm->tmExternalLeading, &tm->tmAveCharWidth, &tm->tmMaxCharWidth,
                     &tm->tmOverhang };
        for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
            int l = i >= 5 ? gdi_dx_to_lx(dc, *v[i]) : gdi_dy_to_ly(dc, *v[i]);
            *v[i] = l < 0 ? -l : l;
        }
    }
    return TRUE;
}

/* the widths of the characters first..last (logical units) */
BOOL WINAPI GetCharWidth(HDC h, UINT first, UINT last, int *widths)
{
    const wfont *f;
    const wreal *r;
    wdc *dc = text_dc(h, &f, &r);
    if (!dc || !widths || first > last || last > 255)
        return FALSE;
    for (UINT c = first; c <= last; c++) {
        int w = char_width(r, f, (unsigned char)c);
        if (dc->mapmode != MM_TEXT) {
            w = gdi_dx_to_lx(dc, w);
            w = w < 0 ? -w : w;
        }
        widths[c - first] = w;
    }
    return TRUE;
}

/* EnumFonts, EnumFontFamilies: the stock fonts' faces (all of one size) and
 * the font files' (each of their sizes when one is named, the first of
 * each face else), the stock ones a font file has left out; what proc
 * returned last (0 stops) */
static int enum_fonts(LPCSTR face, FONTENUMPROC proc, LPARAM lp)
{
    static const int stock[] = { SYSTEM_FONT, ANSI_VAR_FONT, SYSTEM_FIXED_FONT,
                                 ANSI_FIXED_FONT, OEM_FIXED_FONT };
    wface *const *list;
    int n = font_faces(&list), r = 1;
    for (unsigned i = 0; r && i < sizeof(stock) / sizeof(stock[0]); i++) {
        const wfont *f = gdi_font(win_sys.stock[stock[i]]);
        if (face && *face && lstrcmpi(face, f->lf.lfFaceName))
            continue;
        int filed = 0;
        for (int k = 0; k < n && !filed; k++)
            filed = !lstrcmpi(list[k]->name, f->lf.lfFaceName);
        if (filed)
            continue;
        TEXTMETRIC tm;
        wreal vga;                      /* the VGA glyphs at their size */
        memset(&vga, 0, sizeof(vga));
        vga.scale = 1;
        vga.bold = f->bold;
        vga.fixed = f->fixed;
        vga.height = CELL_H;
        vga.ascent = ASCENT;
        vga.ileading = 2;
        vga.avgw = (f->fixed ? 8 : glyph_width[glyph_of(f, 'x')] + 1) + f->bold;
        vga.maxw = 8 + (f->fixed ? 0 : 1) + f->bold;
        font_metrics(&vga, f, &tm);
        r = proc(&f->lf, &tm, RASTER_FONTTYPE, lp);
    }
    for (int i = 0; r && i < n; i++) {
        wface *w = list[i];
        int seen = 0;
        if (face && *face) {
            if (lstrcmpi(face, w->name))
                continue;
        } else {
            for (int k = 0; k < i && !seen; k++)
                seen = !lstrcmpi(list[k]->name, w->name);
            if (seen)
                continue;
        }
        LOGFONT lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = w->height;
        lf.lfWidth = w->avgw;
        lf.lfWeight = w->weight;
        lf.lfItalic = (BYTE)w->italic;
        lf.lfCharSet = (BYTE)w->charset;
        lf.lfPitchAndFamily = (BYTE)((w->pitch_family & 0xf0) |
                                     ((w->pitch_family & 1) ? VARIABLE_PITCH : FIXED_PITCH));
        lstrcpyn(lf.lfFaceName, w->name, LF_FACESIZE);
        wfont tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.lf = lf;
        wreal real;
        memset(&real, 0, sizeof(real));
        real.face = w;
        real.scale = 1;
        real.fixed = !(w->pitch_family & 1);
        real.height = w->height;
        real.ascent = w->ascent;
        real.ileading = w->ileading;
        real.eleading = w->eleading;
        real.avgw = w->avgw;
        real.maxw = w->maxw;
        TEXTMETRIC tm;
        font_metrics(&real, &tmp, &tm);
        r = proc(&lf, &tm, RASTER_FONTTYPE, lp);
    }
    return r;
}

int WINAPI EnumFonts(HDC h, LPCSTR face, FONTENUMPROC proc, LPARAM lp)
{
    (void)h;
    return proc ? enum_fonts(face, proc, lp) : 0;
}

int WINAPI EnumFontFamilies(HDC h, LPCSTR family, FONTENUMPROC proc, LPARAM lp)
{
    (void)h;
    return proc ? enum_fonts(family, proc, lp) : 0;
}

/* the face's name: the font file's the font was realized with */
int WINAPI GetTextFace(HDC h, int n, LPSTR face)
{
    const wfont *f;
    const wreal *r;
    wdc *dc = text_dc(h, &f, &r);
    if (!dc)
        return 0;
    const char *name = r->face ? r->face->name : f->lf.lfFaceName;
    if (face && n > 0)
        lstrcpyn(face, name, n);
    return lstrlen(name);
}

/* one character's glyph at surface position (x, y = top of the cell) */
static void draw_char(wdc *dc, const wclip *c, const wreal *r, const wfont *f, int x, int y,
                      unsigned char ch, uint8_t color)
{
    int base = r->italic ? y + r->ascent : 0;
    if (!r->face) {
        unsigned g = glyph_of(f, ch);
        const uint8_t *src = &win_font8x16[g * CELL_H];
        int w = r->fixed ? 8 : glyph_width[g], left = r->fixed ? 0 : glyph_left[g];
        if (r->scale == 1 && !r->italic) {
            dib_glyph8(dc->surf, c, x, y, src, CELL_H, w, left, color, r->bold);
            return;
        }
        uint8_t rows[CELL_H];
        for (int yy = 0; yy < CELL_H; yy++) {
            unsigned b = (unsigned)src[yy] >> left, o = 0;
            for (int i = 0; i < w; i++)
                if (b >> i & 1)
                    o |= 0x80u >> i;
            rows[yy] = (uint8_t)o;
        }
        dib_glyph(dc->surf, c, x, y, rows, 1, w, CELL_H, r->scale, base, color);
        if (r->bold)
            dib_glyph(dc->surf, c, x + 1, y, rows, 1, w, CELL_H, r->scale, base, color);
        return;
    }
    const uint8_t *cols;
    int w;
    if (!font_glyph(r->face, ch, &cols, &w) || !w)
        return;
    int h = r->face->height, pitch = (w + 7) / 8;
    uint8_t small[256], *rows = pitch * h <= (int)sizeof(small) ? small
                                                               : (uint8_t *)win_alloc((size_t)(pitch * h));
    if (!rows)
        return;
    for (int k = 0; k < pitch; k++)     /* the font's columns: rows */
        for (int yy = 0; yy < h; yy++)
            rows[yy * pitch + k] = cols[k * h + yy];
    dib_glyph(dc->surf, c, x, y, rows, pitch, w, h, r->scale, base, color);
    if (r->bold)
        dib_glyph(dc->surf, c, x + 1, y, rows, pitch, w, h, r->scale, base, color);
    if (rows != small)
        win_free(rows);
}

/*
 * Draw glyphs at surface position (x, y = top of the cell). dx: optional
 * advance per character. Returns the total advance.
 */
static int draw_glyphs(wdc *dc, const wclip *c, const wreal *r, const wfont *f, int x, int y,
                       LPCSTR s, int n, const int *dx, uint8_t color)
{
    int x0 = x;
    for (int i = 0; i < n; i++) {
        int adv = dx ? dx[i] : char_width(r, f, (unsigned char)s[i]) + dc->char_extra;
        draw_char(dc, c, r, f, x, y, (unsigned char)s[i], color);
        x += adv;
    }
    int w = x - x0;
    if (f->lf.lfUnderline || f->lf.lfStrikeOut) {
        wfill fl;
        memset(&fl, 0, sizeof(fl));
        fl.kind = WFILL_SOLID;
        fl.solid = color;
        if (f->lf.lfUnderline) {
            RECT u = { x0, y + r->ascent + r->scale, x0 + w, y + r->ascent + 2 * r->scale };
            dib_fill_rect(dc->surf, c, &u, &fl, 0xF0);
        }
        if (f->lf.lfStrikeOut) {
            int t = y + r->ascent - (r->ascent - r->ileading) / 2;
            RECT so = { x0, t, x0 + w, t + r->scale };
            dib_fill_rect(dc->surf, c, &so, &fl, 0xF0);
        }
    }
    return w;
}

BOOL WINAPI ExtTextOut(HDC h, int x, int y, UINT opts, const RECT *rc,
                       LPCSTR s, UINT n, const int *dx)
{
    const wfont *f;
    const wreal *r;
    wdc *dc = text_dc(h, &f, &r);
    if (!dc)
        return FALSE;
    if (!s)
        n = 0;
    if (dc->meta)
        return mf_text(dc, x, y, opts, rc, s, (int)n, dx, 1);
    if (dc->align & TA_UPDATECP) {
        x = dc->cur.x;
        y = dc->cur.y;
    }
    int w = 0;
    if (dx)
        for (UINT i = 0; i < n; i++)
            w += dx[i];
    else
        w = text_width(r, f, s, (int)n) + dc->char_extra * (int)n;

    /* the reference point on the device, the text placed from it in pixels
     * (the glyphs are the realized font's size) */
    int sx = x, sy = y;
    gdi_lp_to_dp(dc, &sx, &sy);
    switch (dc->align & TA_CENTER) {
    case TA_RIGHT:  sx -= w; break;
    case TA_CENTER: sx -= w / 2; break;
    default: break;
    }
    switch (dc->align & TA_BASELINE) {
    case TA_BOTTOM:   sy -= r->height; break;
    case TA_BASELINE: sy -= r->ascent; break;
    default: break;
    }
    if (dc->align & TA_UPDATECP) {
        int lw = dc->mapmode == MM_TEXT ? w : gdi_dx_to_lx(dc, w);
        if ((dc->align & TA_CENTER) == TA_RIGHT)
            dc->cur.x = x - lw;
        else if ((dc->align & TA_CENTER) != TA_CENTER)
            dc->cur.x = x + lw;
    }

    int lean = r->italic ? r->height / 3 + 1 : 0;
    RECT tb = { sx, sy, sx + w + r->bold, sy + r->height };
    RECT orc = { 0, 0, 0, 0 };
    if (rc && (opts & (ETO_OPAQUE | ETO_CLIPPED))) {
        orc = *rc;
        gdi_lp_to_dp(dc, &orc.left, &orc.top);
        gdi_lp_to_dp(dc, &orc.right, &orc.bottom);
    }
    RECT bounds = tb;
    bounds.left -= lean;
    bounds.right += lean;
    if (rc && (opts & ETO_OPAQUE))
        UnionRect(&bounds, &bounds, &orc);
    if (rc && (opts & ETO_CLIPPED))
        IntersectRect(&bounds, &bounds, &orc);
    /* clip list, optionally narrowed to rc */
    wclip cl;
    const wclip *c = gdi_dc_clip(dc, &cl);
    RECT local[32];
    wclip lc = { local, 0 };
    if (rc && (opts & ETO_CLIPPED)) {
        for (int i = 0; i < c->n && lc.n < 32; i++)
            if (IntersectRect(&local[lc.n], &c->r[i], &orc))
                lc.n++;
        c = &lc;
    }

    gdi_begin_draw(dc, &bounds);
    wfill bkf;
    memset(&bkf, 0, sizeof(bkf));
    bkf.kind = WFILL_SOLID;
    bkf.solid = gdi_color_index(dc, dc->bk);
    if (rc && (opts & ETO_OPAQUE)) {
        wclip full;
        dib_fill_rect(dc->surf, gdi_dc_clip(dc, &full), &orc, &bkf, 0xF0);
    }
    if (dc->bkmode == OPAQUE && n)
        dib_fill_rect(dc->surf, c, &tb, &bkf, 0xF0);
    if (n)
        draw_glyphs(dc, c, r, f, sx, sy, s, (int)n, dx, gdi_color_index(dc, dc->text));
    gdi_end_draw(dc, &bounds);
    return TRUE;
}

BOOL WINAPI TextOut(HDC h, int x, int y, LPCSTR s, int n)
{
    if (n < 0)
        n = lstrlen(s);
    wdc *dc = gdi_dc(h);
    if (dc && dc->meta)
        return mf_text(dc, x, y, 0, NULL, s, n, NULL, 0);
    return ExtTextOut(h, x, y, 0, NULL, s, (UINT)n, NULL);
}

/* Tab stops: positions relative to origin; none = every 8 average chars;
 * one = that interval. Returns extent as MAKELONG(cx, cy). */
static int next_tab(int pos, int origin, int ntabs, const int *tabs, int avg)
{
    int rel = pos - origin;
    if (ntabs <= 0 || !tabs) {
        int step = 8 * avg;
        return origin + (rel / step + 1) * step;
    }
    if (ntabs == 1) {
        int step = tabs[0] > 0 ? tabs[0] : 8 * avg;
        return origin + (rel / step + 1) * step;
    }
    for (int i = 0; i < ntabs; i++)
        if (tabs[i] > rel)
            return origin + tabs[i];
    return pos + gdi_char_width(gdi_font(0), ' ');
}

static LONG tabbed(HDC h, int x, int y, LPCSTR s, int n, int ntabs,
                   const int *tabs, int origin, int draw)
{
    const wfont *f;
    const wreal *r;
    wdc *dc = text_dc(h, &f, &r);
    if (!dc)
        return 0;
    if (n < 0)
        n = lstrlen(s);
    int avg = r->fixed ? 8 * r->scale : char_width(r, f, 'x');
    int pos = x, start = 0;
    UINT align = dc->align;
    if (draw)
        dc->align &= ~(UINT)(TA_UPDATECP | TA_CENTER);
    for (int i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            int len = i - start;
            if (len > 0) {
                if (draw)
                    ExtTextOut(h, pos, y, 0, NULL, s + start, (UINT)len, NULL);
                pos += text_width(r, f, s + start, len) + dc->char_extra * len;
            }
            if (i < n) {
                int nt = next_tab(pos, origin, ntabs, tabs, avg);
                if (draw && dc->bkmode == OPAQUE) {
                    RECT bk = { pos, y, nt, y + r->height };
                    HBRUSH br = CreateSolidBrush(dc->bk);
                    FillRect(h, &bk, br);
                    DeleteObject(br);
                }
                pos = nt;
            }
            start = i + 1;
        }
    }
    dc->align = align;
    return (LONG)((DWORD)(WORD)(pos - x) | ((DWORD)(WORD)r->height << 16));
}

LONG WINAPI TabbedTextOut(HDC h, int x, int y, LPCSTR s, int n,
                          int ntabs, const int *tabs, int origin)
{
    return tabbed(h, x, y, s, n, ntabs, tabs, origin, 1);
}

DWORD WINAPI GetTabbedTextExtent(HDC h, LPCSTR s, int n, int ntabs, const int *tabs)
{
    return (DWORD)tabbed(h, 0, 0, s, n, ntabs, tabs, 0, 0);
}
