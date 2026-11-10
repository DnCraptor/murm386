/*
 * gdi_text.c - fonts and text output.
 *
 * One bitmap face, the 8x16 VGA glyphs (CP866). ANSI text is Windows-1251
 * and is mapped to those glyphs; OEM_CHARSET fonts take bytes as CP866.
 * The proportional "System" face is made by trimming each glyph's empty
 * columns; the fixed faces use the full 8-pixel cell. Bold is synthesized
 * by a second pass one pixel to the right.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

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

int gdi_char_width(const wfont *f, unsigned char ch)
{
    int w = f->fixed ? 8 : glyph_width[glyph_of(f, ch)] + 1;
    return w + (f->bold ? 1 : 0);
}

static int text_width(const wfont *f, LPCSTR s, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++)
        w += gdi_char_width(f, (unsigned char)s[i]);
    return w;
}

static wdc *text_dc(HDC h, const wfont **f)
{
    wdc *dc = gdi_dc(h);
    if (dc)
        *f = gdi_font(dc->font);
    return dc;
}

BOOL WINAPI GetTextExtentPoint(HDC h, LPCSTR s, int n, LPSIZE sz)
{
    const wfont *f;
    wdc *dc = text_dc(h, &f);
    if (!dc || !sz)
        return FALSE;
    if (n < 0)
        n = lstrlen(s);
    sz->cx = text_width(f, s, n);
    sz->cy = f->height;
    return TRUE;
}

DWORD WINAPI GetTextExtent(HDC h, LPCSTR s, int n)
{
    SIZE sz = { 0, 0 };
    GetTextExtentPoint(h, s, n, &sz);
    return (DWORD)(WORD)sz.cx | ((DWORD)(WORD)sz.cy << 16);
}

BOOL WINAPI GetTextMetrics(HDC h, LPTEXTMETRIC tm)
{
    const wfont *f;
    wdc *dc = text_dc(h, &f);
    if (!dc || !tm)
        return FALSE;
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = f->height;
    tm->tmAscent = ASCENT;
    tm->tmDescent = DESCENT;
    tm->tmInternalLeading = 2;
    tm->tmExternalLeading = 0;
    tm->tmAveCharWidth = f->fixed ? 8 + f->bold : gdi_char_width(f, 'x');
    tm->tmMaxCharWidth = 8 + f->bold + (f->fixed ? 0 : 1);
    tm->tmWeight = f->bold ? FW_BOLD : FW_NORMAL;
    tm->tmItalic = f->lf.lfItalic;
    tm->tmUnderlined = f->lf.lfUnderline;
    tm->tmStruckOut = f->lf.lfStrikeOut;
    tm->tmFirstChar = 0x20;
    tm->tmLastChar = 0xFF;
    tm->tmDefaultChar = '?';
    tm->tmBreakChar = ' ';
    /* bit 0 set means VARIABLE pitch (sic, Windows) */
    tm->tmPitchAndFamily = f->fixed ? 0 : 1;
    tm->tmCharSet = f->lf.lfCharSet;
    tm->tmOverhang = f->bold;
    tm->tmDigitizedAspectX = tm->tmDigitizedAspectY = 96;
    return TRUE;
}

int WINAPI GetTextFace(HDC h, int n, LPSTR face)
{
    const wfont *f;
    wdc *dc = text_dc(h, &f);
    if (!dc)
        return 0;
    if (face && n > 0)
        lstrcpyn(face, f->lf.lfFaceName, n);
    return lstrlen(f->lf.lfFaceName);
}

/*
 * Draw glyphs at surface position (x, y = top of the cell). dx: optional
 * advance per character. Returns the total advance.
 */
static int draw_glyphs(wdc *dc, const wclip *c, const wfont *f, int x, int y,
                       LPCSTR s, int n, const int *dx, uint8_t color)
{
    int x0 = x;
    for (int i = 0; i < n; i++) {
        unsigned g = glyph_of(f, (unsigned char)s[i]);
        int adv = dx ? dx[i] : gdi_char_width(f, (unsigned char)s[i]);
        if (f->fixed)
            dib_glyph8(dc->surf, c, x, y, &win_font8x16[g * CELL_H], CELL_H, 8, 0, color, f->bold);
        else
            dib_glyph8(dc->surf, c, x, y, &win_font8x16[g * CELL_H], CELL_H,
                       glyph_width[g], glyph_left[g], color, f->bold);
        x += adv;
    }
    int w = x - x0;
    if (f->lf.lfUnderline || f->lf.lfStrikeOut) {
        wfill fl;
        memset(&fl, 0, sizeof(fl));
        fl.kind = WFILL_SOLID;
        fl.solid = color;
        if (f->lf.lfUnderline) {
            RECT r = { x0, y + ASCENT + 1, x0 + w, y + ASCENT + 2 };
            dib_fill_rect(dc->surf, c, &r, &fl, 0xF0);
        }
        if (f->lf.lfStrikeOut) {
            RECT r = { x0, y + ASCENT - 5, x0 + w, y + ASCENT - 4 };
            dib_fill_rect(dc->surf, c, &r, &fl, 0xF0);
        }
    }
    return w;
}

BOOL WINAPI ExtTextOut(HDC h, int x, int y, UINT opts, const RECT *rc,
                       LPCSTR s, UINT n, const int *dx)
{
    const wfont *f;
    wdc *dc = text_dc(h, &f);
    if (!dc)
        return FALSE;
    if (!s)
        n = 0;
    if (dc->align & TA_UPDATECP) {
        x = dc->cur.x;
        y = dc->cur.y;
    }
    int w = 0;
    if (dx)
        for (UINT i = 0; i < n; i++)
            w += dx[i];
    else
        w = text_width(f, s, (int)n);

    int lx = x, ly = y;
    switch (dc->align & TA_CENTER) {
    case TA_RIGHT:  lx -= w; break;
    case TA_CENTER: lx -= w / 2; break;
    default: break;
    }
    switch (dc->align & TA_BASELINE) {
    case TA_BOTTOM:   ly -= f->height; break;
    case TA_BASELINE: ly -= ASCENT; break;
    default: break;
    }
    if (dc->align & TA_UPDATECP) {
        if ((dc->align & TA_CENTER) == TA_RIGHT)
            dc->cur.x = x - w;
        else if ((dc->align & TA_CENTER) != TA_CENTER)
            dc->cur.x = x + w;
    }

    int sx = lx, sy = ly;
    gdi_lp_to_dp(dc, &sx, &sy);
    RECT tb = { sx, sy, sx + w + f->bold, sy + f->height };
    RECT orc = { 0, 0, 0, 0 };
    if (rc && (opts & (ETO_OPAQUE | ETO_CLIPPED))) {
        orc = *rc;
        gdi_lp_to_dp(dc, &orc.left, &orc.top);
        gdi_lp_to_dp(dc, &orc.right, &orc.bottom);
    }
    RECT bounds = tb;
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
        draw_glyphs(dc, c, f, sx, sy, s, (int)n, dx, gdi_color_index(dc, dc->text));
    gdi_end_draw(dc, &bounds);
    return TRUE;
}

BOOL WINAPI TextOut(HDC h, int x, int y, LPCSTR s, int n)
{
    if (n < 0)
        n = lstrlen(s);
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
    wdc *dc = text_dc(h, &f);
    if (!dc)
        return 0;
    if (n < 0)
        n = lstrlen(s);
    int avg = f->fixed ? 8 : gdi_char_width(f, 'x');
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
                pos += text_width(f, s + start, len);
            }
            if (i < n) {
                int nt = next_tab(pos, origin, ntabs, tabs, avg);
                if (draw && dc->bkmode == OPAQUE) {
                    RECT r = { pos, y, nt, y + f->height };
                    HBRUSH br = CreateSolidBrush(dc->bk);
                    FillRect(h, &r, br);
                    DeleteObject(br);
                }
                pos = nt;
            }
            start = i + 1;
        }
    }
    dc->align = align;
    return (LONG)((DWORD)(WORD)(pos - x) | ((DWORD)(WORD)f->height << 16));
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
