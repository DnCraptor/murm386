/*
 * winhelp.c - WINHELP.EXE, Windows Help: Windows 3.1's help files
 * (hlpfile.c reads them), as programs open them with WinHelp or the user
 * with File Open.
 *
 * A topic: its paragraphs laid out in the window's width (their fonts,
 * indents, tabs, alignment, table rows, bitmaps), the non-scrolling region
 * above the rest; the jumps (green, underlined) go to another topic, the
 * pop-ups (dotted) show theirs in a small window, the macro ones run their
 * macros. The buttons: Contents, Search (the keywords, their topics), Back,
 * and << >> (BrowseButtons), with those the file makes (CreateButton).
 *
 * Its command line: a file, then -n number (a [MAP] context), -i context
 * (a string), -k keyword, -m macro, -p (the topic as a pop-up). A running
 * WinHelp is sent WinHelp's requests (WINHELP_N, the registered message
 * "WinnHelpRequest").
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "commdlg.h"
#include "hlpfile.h"

#define PT(v)       ((v) * 4 / 3)          /* points to pixels (96 a 72-point inch) */
#define MARGIN      8
#define LINK_COLOR  RGB(0, 128, 0)
#define BAR_H       28
#define MAXBUTTONS  12
#define HISTORY     40

#define IDM_OPEN    101
#define IDM_EXIT    102
#define IDM_COPY    103
#define IDM_HOWTO   104
#define IDM_ABOUT   105
#define IDB_FIRST   200                 /* the buttons */

/* ---------------------------------------------------------------- a topic */

enum { K_TEXT, K_TAB, K_BREAK, K_PARA_END, K_PARA, K_IMAGE, K_ROW, K_CELL, K_ROW_END };

typedef struct {
    uint8_t kind;
    uint8_t font;
    int16_t link;                       /* -1 none */
    int16_t index;                      /* K_PARA its format, K_ROW its row, K_CELL its column */
    uint16_t len;
    const char *text;                   /* K_TEXT */
    const hlp_bitmap *image;            /* K_IMAGE */
} piece;

typedef struct {
    int left, right, first, above, below, align;    /* align: 0 left, 1 right, 2 centred */
    int ntabs;
    int tabs[16];
} para_fmt;

typedef struct {
    int ncols;
    int x[16], w[16];
} row_fmt;

enum { L_JUMP, L_POPUP, L_MACRO };

typedef struct {
    uint8_t type, visible;
    DWORD hash;
    const char *text;                   /* a macro, or another file's name (NULL: this one) */
} link_t;

typedef struct {
    int page;
    piece *pc;
    int npc, cappc;
    para_fmt *pf;
    int npf, cappf;
    row_fmt *rf;
    int nrf, caprf;
    link_t *lk;
    int nlk, caplk;
    void **pool;                        /* what it allocated */
    int npool, cappool;
    int scroll_from;                    /* the first piece of the scrolling region */
    char title[96];
    DWORD prev, next;                   /* the browse sequence's topic offsets */
} topic;

/* laid out: the pieces as boxes */
typedef struct {
    int x, y, w, h, asc;
    int piece;
    uint16_t off, n;
} box;

typedef struct {
    box *b;
    int n, cap;
    int height;
} layout;

static HINSTANCE hinst;
static HWND hmain, hns, htopic, hpopup;
static hlp_file *H;
static topic T, P;                      /* the window's, the pop-up's */
static layout LNS, LS, LP;              /* the non-scrolling region, the rest, the pop-up */
static int scroll_y;
static HFONT *fonts;
static int *font_h, *font_asc, nfonts;
static DWORD history[HISTORY];
static char history_file[HISTORY][144];
static int nhistory;
static UINT request_msg;
static int browse_buttons;
static struct { char id[24]; char macro[160]; HWND h; int disabled; } buttons[MAXBUTTONS];
static int nbuttons;

static void *grow(void *p, int *cap, int need, size_t size)
{
    if (need <= *cap)
        return p;
    int ncap = *cap ? *cap * 2 : 32;
    while (ncap < need)
        ncap *= 2;
    void *q = realloc(p, size * (size_t)ncap);
    if (q)
        *cap = ncap;
    return q;
}

static void *keep(topic *t, void *p)
{
    void **pool = (void **)grow(t->pool, &t->cappool, t->npool + 1, sizeof(void *));
    if (!pool || !p) {
        if (pool)
            t->pool = pool;
        free(p);
        return NULL;
    }
    t->pool = pool;
    t->pool[t->npool++] = p;
    return p;
}

static void topic_free(topic *t)
{
    for (int i = 0; i < t->npool; i++)
        free(t->pool[i]);
    free(t->pool);
    free(t->pc);
    free(t->pf);
    free(t->rf);
    free(t->lk);
    memset(t, 0, sizeof(*t));
    t->page = -1;
}

static piece *add_piece(topic *t, int kind, int font, int link)
{
    piece *p = (piece *)grow(t->pc, &t->cappc, t->npc + 1, sizeof(piece));
    if (!p)
        return NULL;
    t->pc = p;
    p = &t->pc[t->npc++];
    memset(p, 0, sizeof(*p));
    p->kind = (uint8_t)kind;
    p->font = (uint8_t)font;
    p->link = (int16_t)link;
    return p;
}

static int add_link(topic *t, int type, int visible, DWORD hash, const char *text)
{
    link_t *l = (link_t *)grow(t->lk, &t->caplk, t->nlk + 1, sizeof(link_t));
    if (!l)
        return -1;
    t->lk = l;
    l = &t->lk[t->nlk];
    l->type = (uint8_t)type;
    l->visible = (uint8_t)visible;
    l->hash = hash;
    l->text = NULL;
    if (text) {
        char *s = (char *)keep(t, malloc(strlen(text) + 1));
        if (s) {
            strcpy(s, text);
            l->text = s;
        }
    }
    return t->nlk++;
}

/* a paragraph's or a table row's record (0x20, 0x23) into pieces */
static void parse_text(topic *t, const uint8_t *rec, DWORD size, int *font, int *link)
{
    DWORD len2 = hlp_u32(rec + 4), ld1 = hlp_u32(rec + 16);
    if (ld1 > size || ld1 < 0x15 || len2 > 0x20000ul)
        return;
    char *text = (char *)keep(t, malloc(len2 + 1));
    if (!text)
        return;
    hlp_expand(H, rec + ld1, size - ld1, (uint8_t *)text, len2);
    text[len2] = 0;
    const char *te = text + len2;
    const uint8_t *p = rec + 0x15, *end = rec + ld1;
    int table = rec[0x14] == 0x23, ncols = 1, row = -1;
    hlp_clong(&p);
    hlp_cushort(&p);
    if (table && p + 2 <= end) {
        row_fmt *r = (row_fmt *)grow(t->rf, &t->caprf, t->nrf + 1, sizeof(row_fmt));
        if (!r)
            return;
        t->rf = r;
        row = t->nrf++;
        r = &t->rf[row];
        ncols = *p++;
        int type = *p++;
        if (type == 0 || type == 2)
            p += 2;                     /* the table's least width */
        if (ncols > 16)
            ncols = 16;
        r->ncols = ncols;
        int x = MARGIN;
        for (int c = 0; c < ncols && p + 4 <= end; c++, p += 4) {
            int gap = (int16_t)hlp_u16(p), w = (int16_t)hlp_u16(p + 2);
            x += c ? PT(gap) / 2 : 0;
            r->x[c] = x;
            r->w[c] = PT(w) > 16 ? PT(w) : 16;
            x += r->w[c] + PT(gap) / 2;
        }
        add_piece(t, K_ROW, *font, -1)->index = (int16_t)row;
    }
    for (int col = 0; col < ncols && p < end; col++) {
        if (table) {
            if (p + 2 <= end && (int16_t)hlp_u16(p) == -1)
                break;
            int c = (int16_t)hlp_u16(p);
            piece *pc = add_piece(t, K_CELL, *font, -1);
            if (pc)
                pc->index = (int16_t)(c >= 0 && c < ncols ? c : col);
            p += 5;
        }
        if (p + 6 > end)
            break;
        p += 4;
        unsigned bits = hlp_u16(p);
        p += 2;
        para_fmt *f = (para_fmt *)grow(t->pf, &t->cappf, t->npf + 1, sizeof(para_fmt));
        if (!f)
            return;
        t->pf = f;
        f = &t->pf[t->npf];
        memset(f, 0, sizeof(*f));
        if (bits & 0x0001) hlp_clong(&p);
        if (bits & 0x0002) f->above = PT(hlp_cshort(&p));
        if (bits & 0x0004) f->below = PT(hlp_cshort(&p));
        if (bits & 0x0008) hlp_cshort(&p);
        if (bits & 0x0010) f->left = PT(hlp_cshort(&p));
        if (bits & 0x0020) f->right = PT(hlp_cshort(&p));
        if (bits & 0x0040) f->first = PT(hlp_cshort(&p));
        if (bits & 0x0100) p += 3;
        if (bits & 0x0200) {
            int n = hlp_cshort(&p);
            for (int i = 0; i < n && p < end; i++) {
                unsigned stop = hlp_cushort(&p);
                if (stop & 0x4000)
                    hlp_cushort(&p);
                if (f->ntabs < 16)
                    f->tabs[f->ntabs++] = PT((int)(stop & 0x3fff));
            }
        }
        f->align = (bits & 0x0400) ? 1 : (bits & 0x0800) ? 2 : 0;
        add_piece(t, K_PARA, *font, *link)->index = (int16_t)t->npf++;
        while (text < te && p < end) {
            size_t n = strlen(text);
            if (n) {
                piece *pc = add_piece(t, K_TEXT, *font, *link);
                if (pc) {
                    pc->text = text;
                    pc->len = (uint16_t)(n < 0xffff ? n : 0xffff);
                }
            }
            text += n + 1;
            unsigned c = *p;
            if (c == 0xff) {
                p++;
                break;
            }
            switch (c) {
            case 0x20: p += 5; break;
            case 0x21: p += 3; break;
            case 0x80:
                if (p + 3 <= end)
                    *font = hlp_u16(p + 1) < (unsigned)nfonts ? hlp_u16(p + 1) : 0;
                p += 3;
                break;
            case 0x81: add_piece(t, K_BREAK, *font, *link); p++; break;
            case 0x82: add_piece(t, K_PARA_END, *font, *link); p++; break;
            case 0x83: add_piece(t, K_TAB, *font, *link); p++; break;
            case 0x86: case 0x87: case 0x88: {
                int type = p + 1 < end ? p[1] : 0;
                p += 2;
                if (p >= end)
                    break;
                long n2 = hlp_clong(&p);
                const uint8_t *q = p;
                if (n2 < 0 || p + n2 > end)
                    n2 = end - p;
                if (type == 0x22)
                    hlp_cushort(&q);
                if ((type == 0x22 || type == 0x03) && q + 4 <= end) {
                    const hlp_bitmap *bm = NULL;
                    int how = (int16_t)hlp_u16(q);
                    if (how == 0) {
                        bm = hlp_bitmap_of(H, (int16_t)hlp_u16(q + 2));
                    } else if (how == 1) {
                        hlp_bitmap *b = (hlp_bitmap *)calloc(1, sizeof(hlp_bitmap));
                        if (b && hlp_picture(q + 2, (DWORD)(p + n2 - (q + 2)), b)) {
                            keep(t, b->info);
                            keep(t, b->bits);
                            bm = (const hlp_bitmap *)keep(t, b);
                        } else {
                            free(b);
                        }
                    }
                    piece *pc = bm ? add_piece(t, K_IMAGE, *font, *link) : NULL;
                    if (pc)
                        pc->image = bm;
                }
                p += n2;
                break;
            }
            case 0x89: *link = -1; p++; break;
            case 0x8B: case 0x8C: {
                piece *pc = add_piece(t, K_TEXT, *font, *link);
                if (pc) {
                    pc->text = c == 0x8B ? " " : "-";
                    pc->len = 1;
                }
                p++;
                break;
            }
            case 0xC8: case 0xCC: {
                unsigned n2 = p + 3 <= end ? hlp_u16(p + 1) : 0;
                char m[256];
                lstrcpyn(m, (const char *)p + 3, n2 + 1 < sizeof(m) ? (int)n2 + 1 : (int)sizeof(m));
                *link = add_link(t, L_MACRO, c == 0xC8, 0, m);
                p += 3 + n2;
                break;
            }
            case 0xE0: case 0xE1:       /* Windows 3.0's: a topic offset; as a hash, none */
                p += 5;
                break;
            case 0xE2: case 0xE3: case 0xE6: case 0xE7:
                if (p + 5 <= end)
                    *link = add_link(t, (c & 1) ? L_JUMP : L_POPUP, !(c & 4), hlp_u32(p + 1), NULL);
                p += 5;
                break;
            case 0xEA: case 0xEB: case 0xEE: case 0xEF: {
                unsigned n2 = p + 3 <= end ? hlp_u16(p + 1) : 0;
                if (p + 8 <= end) {
                    int type = p[3];
                    const char *s = (const char *)p + 8, *file = NULL;
                    if (type == 1)
                        s++;
                    if (type == 4 || type == 6)
                        file = s;
                    *link = add_link(t, (c & 4) ? L_JUMP : L_POPUP, !(c & 1), hlp_u32(p + 4), file);
                }
                p += 3 + n2;
                break;
            }
            default:
                p++;
                break;
            }
        }
    }
    if (table)
        add_piece(t, K_ROW_END, *font, -1);
}

/* the page's topic: its header (title, macros, browse sequence), its
 * records up to the next topic's */
static int topic_load(topic *t, int page)
{
    topic_free(t);
    if (!H || page < 0 || page >= H->npages)
        return 0;
    t->page = page;
    DWORD pos = H->pages[page].pos, scroll = HLP_NONE;
    int font = 0, link = -1;
    t->scroll_from = -1;
    for (int first = 1, guard = 0; pos != HLP_NONE && guard < 20000; first = 0, guard++) {
        uint8_t head[0x15];
        if (hlp_topic_read(H, pos, head, sizeof(head)) < sizeof(head))
            break;
        DWORD size = hlp_u32(head), next = hlp_u32(head + 12);
        int type = head[0x14];
        if (type == 2 && !first)
            break;
        if (size < 0x15 || size > 0x40000ul)
            break;
        uint8_t *rec = (uint8_t *)malloc(size);
        if (!rec || hlp_topic_read(H, pos, rec, size) != size) {
            free(rec);
            break;
        }
        if (type == 2) {
            DWORD len2 = hlp_u32(rec + 4), ld1 = hlp_u32(rec + 16);
            if (ld1 >= 0x15 + 28 && ld1 <= size) {
                t->prev = hlp_u32(rec + 0x19);
                t->next = hlp_u32(rec + 0x1D);
                DWORD ns = hlp_u32(rec + 0x25);
                scroll = hlp_u32(rec + 0x29);
                if (ns == HLP_NONE)
                    scroll = HLP_NONE;      /* all of it scrolls */
            }
            if (len2 < 0x8000 && ld1 <= size) {
                char *s = (char *)malloc(len2 + 1);
                if (s) {
                    hlp_expand(H, rec + ld1, size - ld1, (uint8_t *)s, len2);
                    s[len2] = 0;
                    lstrcpyn(t->title, s, sizeof(t->title));
                    free(s);
                }
            }
        } else if (type == 0x20 || type == 0x23) {
            if (scroll != HLP_NONE && pos == scroll)
                t->scroll_from = t->npc;
            parse_text(t, rec, size, &font, &link);
        }
        free(rec);
        if (next <= pos)
            break;
        pos = next;
    }
    if (scroll == HLP_NONE)
        t->scroll_from = 0;
    else if (t->scroll_from < 0)
        t->scroll_from = t->npc;        /* none of it scrolls */
    return 1;
}

/* ---------------------------------------------------------------- layout */

static void fonts_free(void)
{
    for (int i = 0; i < nfonts; i++)
        if (fonts[i])
            DeleteObject(fonts[i]);
    free(fonts);
    free(font_h);
    free(font_asc);
    fonts = NULL;
    font_h = font_asc = NULL;
    nfonts = 0;
}

static void fonts_make(void)
{
    fonts_free();
    int n = H && H->nfonts ? H->nfonts : 1;
    fonts = (HFONT *)calloc((size_t)n, sizeof(HFONT));
    font_h = (int *)calloc((size_t)n, sizeof(int));
    font_asc = (int *)calloc((size_t)n, sizeof(int));
    if (!fonts || !font_h || !font_asc)
        return;
    nfonts = n;
    HDC dc = GetDC(hmain);
    for (int i = 0; i < n; i++) {
        const hlp_font *f = H && H->nfonts ? &H->fonts[i] : NULL;
        int px = f ? f->half_points * 2 / 3 : 13;
        if (px < 8)
            px = 8;
        static const BYTE family[] = { FF_DONTCARE, FF_MODERN, FF_ROMAN, FF_SWISS, FF_SCRIPT,
                                       FF_DECORATIVE };
        fonts[i] = CreateFont(-px, 0, 0, 0, f && (f->attr & 1) ? FW_BOLD : FW_NORMAL,
                              f && (f->attr & 2), f && (f->attr & 4), f && (f->attr & 8),
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY,
                              (BYTE)((f && f->family <= 5 ? family[f->family] : FF_SWISS) |
                                     (f && f->family == 1 ? FIXED_PITCH : VARIABLE_PITCH)),
                              f ? f->face : "Helv");
        TEXTMETRIC tm;
        HGDIOBJ old = SelectObject(dc, fonts[i] ? fonts[i] : GetStockObject(SYSTEM_FONT));
        GetTextMetrics(dc, &tm);
        SelectObject(dc, old);
        font_h[i] = tm.tmHeight + tm.tmExternalLeading;
        font_asc[i] = tm.tmAscent;
    }
    ReleaseDC(hmain, dc);
}

static HFONT font_of(int f) { return f < nfonts && fonts[f] ? fonts[f] : (HFONT)GetStockObject(SYSTEM_FONT); }

typedef struct {
    layout *out;
    HDC dc;
    int L, R;                           /* the margins: the window's, or a cell's */
    int x, y, line_y, line_h, line_asc, line_first, line_right;
    const para_fmt *pf;
    int first_line;
    int font;
} lay;

static box *add_box(lay *s)
{
    box *b = (box *)grow(s->out->b, &s->out->cap, s->out->n + 1, sizeof(box));
    if (!b)
        return NULL;
    s->out->b = b;
    b = &s->out->b[s->out->n++];
    memset(b, 0, sizeof(*b));
    return b;
}

/* the line ends: its boxes on one baseline, aligned */
static void new_line(lay *s)
{
    layout *o = s->out;
    int asc = 0, h = 0;
    for (int i = s->line_first; i < o->n; i++) {
        if (o->b[i].asc > asc)
            asc = o->b[i].asc;
    }
    for (int i = s->line_first; i < o->n; i++) {
        int below = o->b[i].h - o->b[i].asc;
        if (asc + below > h)
            h = asc + below;
    }
    if (s->line_first == o->n) {        /* an empty line: the font's height */
        h = s->font < nfonts ? font_h[s->font] : 16;
    }
    int dx = 0, room = s->R - (s->pf ? s->pf->right : 0) - s->line_right;
    if (s->pf && s->pf->align == 1)
        dx = room;
    else if (s->pf && s->pf->align == 2)
        dx = room / 2;
    if (dx < 0)
        dx = 0;
    for (int i = s->line_first; i < o->n; i++) {
        o->b[i].y = s->line_y + asc - o->b[i].asc;
        o->b[i].x += dx;
    }
    s->y = s->line_y + h;
    s->line_y = s->y;
    s->line_first = o->n;
    s->first_line = 0;
    s->x = s->L + (s->pf ? s->pf->left : 0);
    s->line_right = s->x;
}

static void text_run(lay *s, const piece *pc, int index)
{
    const char *t = pc->text;
    int n = pc->len, i = 0;
    HGDIOBJ old = SelectObject(s->dc, font_of(pc->font));
    int right = s->R - (s->pf ? s->pf->right : 0);
    while (i < n) {
        int j = i;
        while (j < n && t[j] != ' ')    /* a word, its spaces after it */
            j++;
        int word_end = j;
        while (j < n && t[j] == ' ')
            j++;
        SIZE w, all;
        GetTextExtentPoint(s->dc, t + i, word_end - i, &w);
        GetTextExtentPoint(s->dc, t + i, j - i, &all);
        if (s->x + w.cx > right && s->line_first < s->out->n) {
            new_line(s);
        }
        box *b = add_box(s);
        if (!b)
            break;
        b->x = s->x;
        b->w = all.cx;
        b->h = pc->font < nfonts ? font_h[pc->font] : 16;
        b->asc = pc->font < nfonts ? font_asc[pc->font] : 12;
        b->piece = index;
        b->off = (uint16_t)i;
        b->n = (uint16_t)(j - i);
        s->x += all.cx;
        if (s->x - (all.cx - w.cx) > s->line_right)
            s->line_right = s->x - (all.cx - w.cx);
        i = j;
    }
    SelectObject(s->dc, old);
}

static void lay_out(const topic *t, int from, int to, int width, HDC dc, layout *out)
{
    out->n = 0;
    out->height = 0;
    lay s;
    memset(&s, 0, sizeof(s));
    s.out = out;
    s.dc = dc;
    s.L = MARGIN;
    s.R = width - MARGIN;
    s.x = s.L;
    s.y = s.line_y = MARGIN / 2;
    s.line_right = s.x;
    int row_y = 0, row_max = 0, saved_L = s.L, saved_R = s.R;
    for (int i = from; i < to; i++) {
        const piece *pc = &t->pc[i];
        s.font = pc->font;
        switch (pc->kind) {
        case K_PARA:
            if (s.line_first < out->n)
                new_line(&s);
            s.pf = pc->index >= 0 && pc->index < t->npf ? &t->pf[pc->index] : NULL;
            s.line_y += s.pf ? s.pf->above : 0;
            s.x = s.L + (s.pf ? s.pf->left + s.pf->first : 0);
            if (s.x < s.L)
                s.x = s.L;
            s.line_right = s.x;
            break;
        case K_TEXT:
            text_run(&s, pc, i);
            break;
        case K_TAB: {
            int base = s.L, pos = s.x - base, stop = 0;
            for (int k = 0; s.pf && k < s.pf->ntabs; k++)
                if (s.pf->tabs[k] > pos) {
                    stop = s.pf->tabs[k];
                    break;
                }
            if (!stop)
                stop = (pos / PT(36) + 1) * PT(36);
            s.x = base + stop;
            break;
        }
        case K_BREAK:
            new_line(&s);
            break;
        case K_PARA_END:
            new_line(&s);
            s.line_y += s.pf ? s.pf->below : 0;
            s.y = s.line_y;
            s.x = s.L + (s.pf ? s.pf->left + s.pf->first : 0);
            if (s.x < s.L)
                s.x = s.L;
            s.line_right = s.x;
            break;
        case K_IMAGE: {
            if (!pc->image)
                break;
            int right = s.R - (s.pf ? s.pf->right : 0);
            if (s.x + pc->image->w > right && s.line_first < out->n)
                new_line(&s);
            box *b = add_box(&s);
            if (!b)
                break;
            b->x = s.x;
            b->w = pc->image->w;
            b->h = pc->image->h;
            b->asc = pc->image->h;
            b->piece = i;
            s.x += pc->image->w;
            if (s.x > s.line_right)
                s.line_right = s.x;
            break;
        }
        case K_ROW:
            if (s.line_first < out->n)
                new_line(&s);
            row_y = row_max = s.line_y;
            saved_L = s.L;
            saved_R = s.R;
            break;
        case K_CELL: {
            if (s.line_first < out->n)
                new_line(&s);
            if (s.line_y > row_max)
                row_max = s.line_y;
            const row_fmt *r = NULL;
            for (int k = i; k >= from; k--)
                if (t->pc[k].kind == K_ROW) {
                    r = &t->rf[t->pc[k].index];
                    break;
                }
            int c = pc->index;
            if (r && c >= 0 && c < r->ncols) {
                s.L = r->x[c];
                s.R = r->x[c] + r->w[c];
            }
            s.line_y = s.y = row_y;
            s.x = s.L;
            s.line_right = s.x;
            break;
        }
        case K_ROW_END:
            if (s.line_first < out->n)
                new_line(&s);
            if (s.line_y > row_max)
                row_max = s.line_y;
            s.L = saved_L;
            s.R = saved_R;
            s.line_y = s.y = row_max;
            s.x = s.L;
            s.line_right = s.x;
            break;
        }
    }
    if (s.line_first < out->n)
        new_line(&s);
    out->height = s.line_y + MARGIN / 2;
}

/* ---------------------------------------------------------------- drawing */

static void draw(HDC dc, const topic *t, const layout *l, int dy, const RECT *clip)
{
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < l->n; i++) {
        const box *b = &l->b[i];
        int y = b->y - dy;
        if (y + b->h < clip->top || y > clip->bottom)
            continue;
        const piece *pc = &t->pc[b->piece];
        if (pc->kind == K_IMAGE) {
            const hlp_bitmap *bm = pc->image;
            SetDIBitsToDevice(dc, b->x, y, (UINT)bm->w, (UINT)bm->h, 0, 0, 0, (UINT)bm->h,
                              bm->bits, bm->info, DIB_RGB_COLORS);
            continue;
        }
        const link_t *lk = pc->link >= 0 && pc->link < t->nlk ? &t->lk[pc->link] : NULL;
        int visible = lk && lk->visible;
        char buf[256];
        int n = b->n < (int)sizeof(buf) ? b->n : (int)sizeof(buf);
        for (int k = 0; k < n; k++) {
            unsigned char c = (unsigned char)pc->text[b->off + k];
            buf[k] = c == 0xA0 ? ' ' : (char)c;
        }
        SelectObject(dc, font_of(pc->font));
        COLORREF col = H && pc->font < H->nfonts ? H->fonts[pc->font].color : RGB(0, 0, 0);
        SetTextColor(dc, visible ? LINK_COLOR : col);
        TextOut(dc, b->x, y, buf, n);
        if (visible) {                  /* a jump underlined, a pop-up dotted */
            int uy = y + b->asc + 1, w = b->w;
            while (n && buf[n - 1] == ' ') {
                SIZE sp;
                GetTextExtentPoint(dc, " ", 1, &sp);
                w -= sp.cx;
                n--;
            }
            for (int x = 0; x < w; x++)
                if (lk->type != L_POPUP || !(x & 1))
                    SetPixel(dc, b->x + x, uy, LINK_COLOR);
        }
    }
}

/* the link at a point of a layout, -1 for none */
static int link_at(const topic *t, const layout *l, int x, int y)
{
    for (int i = 0; i < l->n; i++) {
        const box *b = &l->b[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            int k = t->pc[b->piece].link;
            return k >= 0 && k < t->nlk ? k : -1;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- navigation */

static void relayout(void);
static void run_macros(const char *m);
static void show_popup(int page, int x, int y);
static void search_dialog(const char *word);

static void set_title(void)
{
    char s[128];
    if (H)
        wsprintf(s, "%s", H->title);
    else
        lstrcpy(s, "Windows Help");
    SetWindowText(hmain, s);
}

static void update_buttons(void)
{
    for (int i = 0; i < nbuttons; i++) {
        int on = !buttons[i].disabled && H;
        if (!lstrcmpi(buttons[i].id, "btn_back"))
            on = on && nhistory > 1;
        else if (!lstrcmpi(buttons[i].id, "btn_prev"))
            on = on && T.prev != HLP_NONE && hlp_page_of(H, T.prev) >= 0;
        else if (!lstrcmpi(buttons[i].id, "btn_next"))
            on = on && T.next != HLP_NONE && hlp_page_of(H, T.next) >= 0;
        EnableWindow(buttons[i].h, on);
    }
}

static void show_page(int page, int remember)
{
    if (!H || page < 0 || page >= H->npages)
        return;
    topic_load(&T, page);
    scroll_y = 0;
    if (remember) {
        if (nhistory == HISTORY) {
            memmove(history, history + 1, sizeof(history[0]) * (HISTORY - 1));
            memmove(history_file, history_file + 1, sizeof(history_file[0]) * (HISTORY - 1));
            nhistory--;
        }
        history[nhistory] = H->pages[page].offset;
        lstrcpyn(history_file[nhistory], H->path, sizeof(history_file[0]));
        nhistory++;
    }
    relayout();
    update_buttons();
}

static void show_offset(DWORD offset)
{
    if (H)
        show_page(hlp_page_of(H, offset), 1);
}

static void buttons_reset(void);

/* another file (its name as WinHelp has it); 1 when it is open */
static int open_file(const char *name)
{
    if (H && !lstrcmpi(H->path, name))
        return 1;
    hlp_file *h = hlp_open(name);
    if (!h) {
        char msg[200];
        wsprintf(msg, "Cannot open the Help file %s.", name);
        MessageBox(hmain, msg, "Help", MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    if (hpopup)
        DestroyWindow(hpopup);
    topic_free(&T);
    hlp_close(H);
    H = h;
    fonts_make();
    set_title();
    buttons_reset();
    if (H->config)
        for (const char *m = H->config; m < H->config + H->config_len && *m; m += strlen(m) + 1)
            run_macros(m);
    return 1;
}

static void contents(void)
{
    if (!H)
        return;
    int p = hlp_page_of(H, H->contents);
    show_page(p >= 0 ? p : 0, 1);
}

static void back(void)
{
    if (nhistory < 2)
        return;
    nhistory--;
    DWORD off = history[nhistory - 1];
    char file[144];
    lstrcpy(file, history_file[nhistory - 1]);
    if (open_file(file))
        show_page(hlp_page_of(H, off), 0);
}

/* a jump's or a pop-up's: in this file or another */
static void follow(const link_t *l, int x, int y)
{
    if (l->type == L_MACRO) {
        run_macros(l->text);
        return;
    }
    if (l->text && l->text[0] && !(l->type == L_POPUP)) {
        char file[144];
        lstrcpyn(file, l->text, sizeof(file));
        if (!open_file(file))
            return;
    }
    DWORD off = H ? hlp_context(H, l->hash) : HLP_NONE;
    int page = off != HLP_NONE ? hlp_page_of(H, off) : -1;
    if (page < 0) {
        MessageBox(hmain, "The topic does not exist.", "Help", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (l->type == L_POPUP)
        show_popup(page, x, y);
    else
        show_page(page, 1);
}

/* ---------------------------------------------------------------- macros */

/* the next argument (a quoted string, `string', a number) into out */
static const char *macro_arg(const char *s, char *out, int cap)
{
    int n = 0;
    while (*s == ' ' || *s == ',')
        s++;
    if (*s == '"' || *s == '`') {
        char close = *s == '"' ? '"' : '\'';
        s++;
        while (*s && *s != close) {
            if (n < cap - 1)
                out[n++] = *s;
            s++;
        }
        if (*s)
            s++;
    } else {
        while (*s && *s != ',' && *s != ')') {
            if (n < cap - 1)
                out[n++] = *s;
            s++;
        }
        while (n && out[n - 1] == ' ')
            n--;
    }
    out[n] = 0;
    return s;
}

static long number(const char *s)
{
    long v = 0;
    int neg = *s == '-';
    if (neg)
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        for (s += 2;; s++) {
            int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                    *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
            if (d < 0)
                break;
            v = v * 16 + d;
        }
    else
        for (; *s >= '0' && *s <= '9'; s++)
            v = v * 10 + (*s - '0');
    return neg ? -v : v;
}

static int button_find(const char *id)
{
    for (int i = 0; i < nbuttons; i++)
        if (!lstrcmpi(buttons[i].id, id))
            return i;
    return -1;
}

static void buttons_layout(void);

static void button_add(const char *id, const char *label, const char *macro)
{
    int i = button_find(id);
    if (i < 0) {
        if (nbuttons == MAXBUTTONS)
            return;
        i = nbuttons++;
        lstrcpyn(buttons[i].id, id, sizeof(buttons[i].id));
        buttons[i].h = CreateWindow("BUTTON", label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    0, 0, 10, 10, hmain, (HMENU)(UINT)(IDB_FIRST + i), hinst, NULL);
    } else {
        SetWindowText(buttons[i].h, label);
    }
    lstrcpyn(buttons[i].macro, macro, sizeof(buttons[i].macro));
    buttons[i].disabled = 0;
    buttons_layout();
}

static void buttons_reset(void)
{
    for (int i = 0; i < nbuttons; i++)
        DestroyWindow(buttons[i].h);
    nbuttons = 0;
    browse_buttons = 0;
    button_add("btn_contents", "&Contents", "Contents()");
    button_add("btn_search", "&Search", "Search()");
    button_add("btn_back", "&Back", "Back()");
}

static void file_and(const char *arg, char *file)
{
    /* "file>window" or "" (this one) */
    lstrcpyn(file, arg, 144);
    for (char *p = file; *p; p++)
        if (*p == '>') {
            *p = 0;
            break;
        }
}

static void run_macro(const char *name, const char *args)
{
    char a[4][160];
    const char *s = args;
    for (int i = 0; i < 4; i++)
        s = macro_arg(s, a[i], sizeof(a[i]));
    char file[144];
    file_and(a[0], file);
    if (!lstrcmpi(name, "Contents"))
        contents();
    else if (!lstrcmpi(name, "Search"))
        search_dialog(NULL);
    else if (!lstrcmpi(name, "Back"))
        back();
    else if (!lstrcmpi(name, "Prev"))
        show_offset(T.prev);
    else if (!lstrcmpi(name, "Next"))
        show_offset(T.next);
    else if (!lstrcmpi(name, "Exit"))
        PostMessage(hmain, WM_CLOSE, 0, 0);
    else if (!lstrcmpi(name, "BrowseButtons")) {
        if (!browse_buttons) {
            browse_buttons = 1;
            button_add("btn_prev", "<<", "Prev()");
            button_add("btn_next", ">>", "Next()");
        }
    } else if (!lstrcmpi(name, "CreateButton") || !lstrcmpi(name, "CB"))
        button_add(a[0], a[1], a[2]);
    else if (!lstrcmpi(name, "ChangeButtonBinding") || !lstrcmpi(name, "CBB")) {
        int i = button_find(a[0]);
        if (i >= 0)
            lstrcpyn(buttons[i].macro, a[1], sizeof(buttons[i].macro));
    } else if (!lstrcmpi(name, "DisableButton") || !lstrcmpi(name, "DB") ||
               !lstrcmpi(name, "EnableButton") || !lstrcmpi(name, "EB")) {
        int i = button_find(a[0]);
        if (i >= 0)
            buttons[i].disabled = name[0] == 'D' || name[0] == 'd';
    } else if (!lstrcmpi(name, "DestroyButton")) {
        int i = button_find(a[0]);
        if (i >= 0) {
            DestroyWindow(buttons[i].h);
            for (; i < nbuttons - 1; i++)
                buttons[i] = buttons[i + 1];
            nbuttons--;
            buttons_layout();
        }
    } else if (!lstrcmpi(name, "JumpId") || !lstrcmpi(name, "JI") ||
               !lstrcmpi(name, "PopupId") || !lstrcmpi(name, "PI")) {
        if (file[0] && !open_file(file))
            return;
        DWORD off = H ? hlp_context(H, hlp_hash(a[1])) : HLP_NONE;
        if (off == HLP_NONE)
            return;
        if (name[0] == 'P' || name[0] == 'p') {
            POINT pt;
            GetCursorPos(&pt);
            show_popup(hlp_page_of(H, off), pt.x, pt.y);
        } else {
            show_offset(off);
        }
    } else if (!lstrcmpi(name, "JumpContext") || !lstrcmpi(name, "JC") ||
               !lstrcmpi(name, "PopupContext") || !lstrcmpi(name, "PC")) {
        if (file[0] && !open_file(file))
            return;
        DWORD off = H ? hlp_context_number(H, (DWORD)number(a[1])) : HLP_NONE;
        if (off == HLP_NONE)
            return;
        if (name[0] == 'P' || name[0] == 'p') {
            POINT pt;
            GetCursorPos(&pt);
            show_popup(hlp_page_of(H, off), pt.x, pt.y);
        } else {
            show_offset(off);
        }
    } else if (!lstrcmpi(name, "JumpContents")) {
        if (!file[0] || open_file(file))
            contents();
    } else if (!lstrcmpi(name, "JumpKeyword") || !lstrcmpi(name, "JK")) {
        if (!file[0] || open_file(file))
            search_dialog(a[1]);
    } else if (!lstrcmpi(name, "HelpOn")) {
        if (open_file("WINHELP.HLP"))
            contents();
    } else if (!lstrcmpi(name, "ExecProgram") || !lstrcmpi(name, "EP")) {
        WinExec(a[0], a[1][0] ? (UINT)number(a[1]) : SW_SHOWNORMAL);
    }
    /* the rest (menus, bookmarks, annotations, DLL calls ...): nothing */
}

/* "Name(args);Name2(args)" (or ':' between them) */
static void run_macros(const char *m)
{
    if (!m)
        return;
    char copy[512];
    lstrcpyn(copy, m, sizeof(copy));
    char *s = copy;
    while (*s) {
        while (*s == ' ' || *s == ';' || *s == ':')
            s++;
        if (!*s)
            break;
        char *name = s;
        while (*s && *s != '(' && *s != ';' && *s != ':' && *s != ' ')
            s++;
        char *args = (char *)"";
        if (*s == ' ' || *s == '(') {
            char *n_end = s;
            while (*s == ' ')
                s++;
            if (*s == '(') {
                *n_end = 0;
                s++;
                args = s;
                int q = 0;
                while (*s && (q || *s != ')')) {
                    if (*s == '"')
                        q = !q;
                    s++;
                }
                if (*s)
                    *s++ = 0;
            } else {
                *n_end = 0;
            }
        } else if (*s) {
            *s++ = 0;
        }
        run_macro(name, args);
    }
}

/* ---------------------------------------------------------------- windows */

static void buttons_layout(void)
{
    if (!hmain)
        return;
    HDC dc = GetDC(hmain);
    int x = 4;
    for (int i = 0; i < nbuttons; i++) {
        char label[64];
        GetWindowText(buttons[i].h, label, sizeof(label));
        SIZE sz;
        GetTextExtentPoint(dc, label, lstrlen(label), &sz);
        int w = sz.cx + 16 < 60 ? 60 : sz.cx + 16;
        MoveWindow(buttons[i].h, x, 3, w, BAR_H - 6, TRUE);
        x += w + 2;
    }
    ReleaseDC(hmain, dc);
}

static void relayout(void)
{
    if (!htopic)
        return;
    RECT rc;
    GetClientRect(hmain, &rc);
    int w = rc.right, top = BAR_H;
    HDC dc = GetDC(hmain);
    int sw = GetSystemMetrics(SM_CXVSCROLL);
    lay_out(&T, 0, T.scroll_from, w, dc, &LNS);
    int ns_h = T.scroll_from > 0 ? LNS.height : 0;
    lay_out(&T, T.scroll_from, T.npc, w - sw, dc, &LS);
    ReleaseDC(hmain, dc);
    MoveWindow(hns, 0, top, w, ns_h, TRUE);
    ShowWindow(hns, ns_h ? SW_SHOW : SW_HIDE);
    MoveWindow(htopic, 0, top + ns_h, w, rc.bottom - top - ns_h > 0 ? rc.bottom - top - ns_h : 0, TRUE);
    RECT tr;
    GetClientRect(htopic, &tr);
    int max = LS.height - tr.bottom;
    if (max < 0)
        max = 0;
    if (scroll_y > max)
        scroll_y = max;
    SetScrollRange(htopic, SB_VERT, 0, max, FALSE);
    SetScrollPos(htopic, SB_VERT, scroll_y, TRUE);
    InvalidateRect(hns, NULL, TRUE);
    InvalidateRect(htopic, NULL, TRUE);
}

static void scroll_to(int y)
{
    RECT tr;
    GetClientRect(htopic, &tr);
    int max = LS.height - tr.bottom;
    if (max < 0)
        max = 0;
    if (y > max)
        y = max;
    if (y < 0)
        y = 0;
    if (y == scroll_y)
        return;
    scroll_y = y;
    SetScrollPos(htopic, SB_VERT, y, TRUE);
    InvalidateRect(htopic, NULL, TRUE);
}

static LRESULT CALLBACK TopicProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int region_ns = h == hns;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        draw(dc, &T, region_ns ? &LNS : &LS, region_ns ? 0 : scroll_y, &ps.rcPaint);
        if (region_ns) {
            RECT r;
            GetClientRect(h, &r);
            MoveToEx(dc, 0, r.bottom - 1, NULL);
            LineTo(dc, r.right, r.bottom - 1);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (int16_t)LOWORD(lp), y = (int16_t)HIWORD(lp);
        int k = link_at(&T, region_ns ? &LNS : &LS, x, y + (region_ns ? 0 : scroll_y));
        SetFocus(htopic);
        if (k >= 0) {
            POINT pt = { x, y };
            ClientToScreen(h, &pt);
            link_t l = T.lk[k];
            char text[256];
            if (l.text) {               /* the topic it is in goes */
                lstrcpyn(text, l.text, sizeof(text));
                l.text = text;
            }
            follow(&l, pt.x, pt.y);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        static const struct { UINT vk, code; } keys[] = {
            { VK_UP, SB_LINEUP }, { VK_DOWN, SB_LINEDOWN }, { VK_PRIOR, SB_PAGEUP },
            { VK_NEXT, SB_PAGEDOWN }, { VK_HOME, SB_TOP }, { VK_END, SB_BOTTOM },
        };
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
            if (wp == keys[i].vk) {
                SendMessage(htopic, WM_VSCROLL, keys[i].code, 0);
                return 0;
            }
        break;
    }
    case WM_VSCROLL: {
        RECT r;
        GetClientRect(htopic, &r);
        int line = 16, y = scroll_y;
        switch (LOWORD(wp)) {
        case SB_LINEUP: y -= line; break;
        case SB_LINEDOWN: y += line; break;
        case SB_PAGEUP: y -= r.bottom - line; break;
        case SB_PAGEDOWN: y += r.bottom - line; break;
        case SB_TOP: y = 0; break;
        case SB_BOTTOM: y = LS.height; break;
        case SB_THUMBPOSITION: case SB_THUMBTRACK: y = (int16_t)HIWORD(wp); break;
        default: return 0;
        }
        scroll_to(y);
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- the pop-up */

static LRESULT CALLBACK PopupProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        draw(dc, &P, &LP, 0, &ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (int16_t)LOWORD(lp), y = (int16_t)HIWORD(lp);
        int k = link_at(&P, &LP, x, y);
        link_t l;
        char text[256];
        if (k >= 0) {
            l = P.lk[k];
            if (l.text) {
                lstrcpyn(text, l.text, sizeof(text));
                l.text = text;
            }
        }
        POINT pt = { x, y };
        ClientToScreen(h, &pt);
        DestroyWindow(h);
        if (k >= 0)
            follow(&l, pt.x, pt.y);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_KILLFOCUS:
        if (msg == WM_KILLFOCUS && (HWND)wp == h)
            break;
        PostMessage(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_DESTROY:
        if (hpopup == h)
            hpopup = 0;
        topic_free(&P);
        LP.n = 0;
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void show_popup(int page, int x, int y)
{
    if (!H || page < 0)
        return;
    if (hpopup)
        DestroyWindow(hpopup);
    if (!topic_load(&P, page))
        return;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int w = sw * 2 / 3;
    HDC dc = GetDC(hmain);
    lay_out(&P, 0, P.npc, w, dc, &LP);
    ReleaseDC(hmain, dc);
    int right = 0;
    for (int i = 0; i < LP.n; i++)
        if (LP.b[i].x + LP.b[i].w > right)
            right = LP.b[i].x + LP.b[i].w;
    w = right + MARGIN + 2 < w ? right + MARGIN + 2 : w;
    if (w < 60)
        w = 60;
    int h = LP.height + 2;
    if (h > sh - 8)
        h = sh - 8;
    x -= w / 2;
    y += 8;
    if (x + w > sw)
        x = sw - w;
    if (x < 0)
        x = 0;
    if (y + h > sh)
        y = y - h - 16 > 0 ? y - h - 16 : sh - h;
    hpopup = CreateWindow("WinHelpPopup", "", WS_POPUP | WS_BORDER | WS_VISIBLE, x, y, w, h,
                          hmain, 0, hinst, NULL);
    if (hpopup)
        SetFocus(hpopup);
}

/* ---------------------------------------------------------------- Search */

#define IDC_WORD   301
#define IDC_WORDS  302
#define IDC_SHOW   303
#define IDC_TOPICS 304
#define IDC_GOTO   305

static hlp_keyword *kw;
static int nkw;
static const char *search_word;
static DWORD search_go = HLP_NONE;

static void search_show(HWND dlg)
{
    HWND words = GetDlgItem(dlg, IDC_WORDS), topics = GetDlgItem(dlg, IDC_TOPICS);
    int i = (int)SendMessage(words, LB_GETCURSEL, 0, 0);
    SendMessage(topics, LB_RESETCONTENT, 0, 0);
    if (i < 0 || i >= nkw)
        return;
    DWORD offs[64];
    int n = hlp_keyword_topics(H, &kw[i], offs, 64);
    for (int k = 0; k < n; k++) {
        char title[96];
        hlp_page_title(H, hlp_page_of(H, offs[k]), title, sizeof(title));
        if (!title[0])
            lstrcpy(title, "(untitled)");
        int at = (int)SendMessage(topics, LB_ADDSTRING, 0, (LPARAM)(LPSTR)title);
        if (at >= 0)
            SendMessage(topics, LB_SETITEMDATA, (WPARAM)at, (LPARAM)offs[k]);
    }
    SendMessage(topics, LB_SETCURSEL, 0, 0);
}

static BOOL CALLBACK SearchProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        HWND words = GetDlgItem(dlg, IDC_WORDS);
        for (int i = 0; i < nkw; i++)
            SendMessage(words, LB_ADDSTRING, 0, (LPARAM)(LPSTR)kw[i].word);
        if (search_word && search_word[0]) {
            SetDlgItemText(dlg, IDC_WORD, search_word);
            int i = (int)SendMessage(words, LB_FINDSTRING, (WPARAM)-1, (LPARAM)(LPSTR)search_word);
            SendMessage(words, LB_SETCURSEL, (WPARAM)(i >= 0 ? i : 0), 0);
            search_show(dlg);
        } else {
            SendMessage(words, LB_SETCURSEL, 0, 0);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_WORD:
            if (HIWORD(lp) == EN_CHANGE) {
                char s[80];
                GetDlgItemText(dlg, IDC_WORD, s, sizeof(s));
                int i = (int)SendDlgItemMessage(dlg, IDC_WORDS, LB_FINDSTRING, (WPARAM)-1,
                                                (LPARAM)(LPSTR)s);
                if (i >= 0)
                    SendDlgItemMessage(dlg, IDC_WORDS, LB_SETCURSEL, (WPARAM)i, 0);
            }
            return TRUE;
        case IDC_WORDS:
            if (HIWORD(lp) == LBN_DBLCLK)
                search_show(dlg);
            return TRUE;
        case IDC_SHOW:
            search_show(dlg);
            return TRUE;
        case IDC_TOPICS:
            if (HIWORD(lp) != LBN_DBLCLK)
                return TRUE;
            /* fall through */
        case IDC_GOTO:
        case IDOK: {
            HWND topics = GetDlgItem(dlg, IDC_TOPICS);
            int i = (int)SendMessage(topics, LB_GETCURSEL, 0, 0);
            if (i < 0) {
                search_show(dlg);
                return TRUE;
            }
            search_go = (DWORD)SendMessage(topics, LB_GETITEMDATA, (WPARAM)i, 0);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void search_dialog(const char *word)
{
    static const DLGTEMPLATE_N tpl = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 9, 20, 20, 236, 170,
        NULL, NULL, "Search", 0, NULL,
    };
    static const DLGITEM_N items[] = {
        { 6, 4, 180, 8, -1, SS_LEFT | WS_VISIBLE, "STATIC",
          "Type a &word, or select one from the list." },
        { 6, 14, 160, 12, IDC_WORD, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE,
          "EDIT", "" },
        { 6, 28, 160, 60, IDC_WORDS, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE,
          "LISTBOX", "" },
        { 172, 14, 58, 14, IDC_SHOW, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON",
          "&Show Topics" },
        { 172, 32, 58, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
        { 6, 92, 180, 8, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Select a &topic, then choose Go To." },
        { 6, 102, 160, 62, IDC_TOPICS, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE,
          "LISTBOX", "" },
        { 172, 102, 58, 14, IDC_GOTO, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Go To" },
        { 0, 0, 0, 0, -1, 0, "STATIC", "" },
    };
    if (!H)
        return;
    nkw = hlp_keywords(H, &kw);
    if (!nkw) {
        free(kw);
        kw = NULL;
        MessageBox(hmain, "This Help file has no keywords.", "Search", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (word && word[0]) {              /* one keyword, one topic: there at once */
        for (int i = 0; i < nkw; i++)
            if (!lstrcmpi(kw[i].word, word) && kw[i].count == 1) {
                DWORD off;
                if (hlp_keyword_topics(H, &kw[i], &off, 1) == 1) {
                    free(kw);
                    kw = NULL;
                    show_offset(off);
                    return;
                }
            }
    }
    search_word = word;
    search_go = HLP_NONE;
    int r = DialogBoxIndirectN(hinst, &tpl, items, hmain, SearchProc);
    free(kw);
    kw = NULL;
    nkw = 0;
    if (r == IDOK && search_go != HLP_NONE)
        show_offset(search_go);
}

/* ---------------------------------------------------------------- requests */

/* "file -n 12", "file -i context", "file -k keyword", "file -m macro",
 * "-p" before them: the topic as a pop-up */
static void request(const char *cmd)
{
    char file[144];
    int n = 0, popup = 0;
    while (*cmd == ' ')
        cmd++;
    while (*cmd && *cmd != ' ' && n < (int)sizeof(file) - 1)
        file[n++] = *cmd++;
    file[n] = 0;
    if (!file[0] || !open_file(file))
        return;
    while (*cmd == ' ')
        cmd++;
    if (cmd[0] == '-' && (cmd[1] == 'p' || cmd[1] == 'P')) {
        popup = 1;
        cmd += 2;
        while (*cmd == ' ')
            cmd++;
    }
    char opt = cmd[0] == '-' ? (char)(cmd[1] | 0x20) : 0;
    const char *arg = opt ? cmd + 2 : cmd;
    while (*arg == ' ')
        arg++;
    DWORD off = HLP_NONE;
    if (opt == 'n')
        off = hlp_context_number(H, (DWORD)number(arg));
    else if (opt == 'i')
        off = hlp_context(H, hlp_hash(arg));
    else if (opt == 'k') {
        contents();
        search_dialog(arg);
        return;
    } else if (opt == 'm') {
        contents();
        run_macros(arg);
        return;
    }
    if (off == HLP_NONE) {
        contents();
        return;
    }
    if (popup) {
        POINT pt;
        GetCursorPos(&pt);
        if (!T.npc)
            contents();
        show_popup(hlp_page_of(H, off), pt.x, pt.y);
    } else {
        show_offset(off);
    }
}

static void copy_topic(void)
{
    DWORD n = 0;
    for (int i = 0; i < T.npc; i++)
        n += T.pc[i].kind == K_TEXT ? T.pc[i].len : 2;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n + 1);
    char *s = g ? (char *)GlobalLock(g) : NULL;
    if (!s)
        return;
    DWORD k = 0;
    for (int i = 0; i < T.npc; i++) {
        const piece *pc = &T.pc[i];
        if (pc->kind == K_TEXT) {
            for (int j = 0; j < pc->len; j++)
                s[k++] = (unsigned char)pc->text[j] == 0xA0 ? ' ' : pc->text[j];
        } else if (pc->kind == K_BREAK || pc->kind == K_PARA_END) {
            s[k++] = '\r';
            s[k++] = '\n';
        } else if (pc->kind == K_TAB) {
            s[k++] = '\t';
        }
    }
    s[k] = 0;
    GlobalUnlock(g);
    if (OpenClipboard(hmain)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, g);
        CloseClipboard();
    } else {
        GlobalFree(g);
    }
}

static void file_open(void)
{
    char name[144];
    OFSTRUCT of;
    (void)of;
    name[0] = 0;
    char filter[] = "Help Files (*.HLP)\0*.hlp\0All Files (*.*)\0*.*\0";
    OPENFILENAME ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hmain;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = name;
    ofn.nMaxFile = sizeof(name);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Open";
    if (GetOpenFileName(&ofn) && open_file(name)) {
        nhistory = 0;
        contents();
    }
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == request_msg && request_msg && lp) {
        const WINHELP_N *r = (const WINHELP_N *)(uintptr_t)lp;
        char cmd[400];
        switch (r->cmd) {
        case HELP_CONTEXT: wsprintf(cmd, "%s -n %lu", r->file, (unsigned long)r->data); break;
        case HELP_CONTEXTPOPUP: wsprintf(cmd, "%s -p -n %lu", r->file, (unsigned long)r->data); break;
        case HELP_KEY: case HELP_PARTIALKEY: wsprintf(cmd, "%s -k %s", r->file, r->text); break;
        case HELP_COMMAND: wsprintf(cmd, "%s -m %s", r->file, r->text); break;
        case HELP_HELPONHELP: lstrcpy(cmd, "WINHELP.HLP"); break;
        default: lstrcpyn(cmd, r->file, sizeof(cmd)); break;
        }
        if (IsIconic(h))
            ShowWindow(h, SW_RESTORE);
        SetActiveWindow(h);
        request(cmd);
        return TRUE;
    }
    switch (msg) {
    case WM_CREATE:
        hmain = h;
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            relayout();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) >= IDB_FIRST && LOWORD(wp) < IDB_FIRST + nbuttons) {
            char m[160];
            lstrcpy(m, buttons[LOWORD(wp) - IDB_FIRST].macro);
            run_macros(m);
            SetFocus(htopic);
            return 0;
        }
        switch (LOWORD(wp)) {
        case IDM_OPEN: file_open(); return 0;
        case IDM_EXIT: DestroyWindow(h); return 0;
        case IDM_COPY: copy_topic(); return 0;
        case IDM_HOWTO:
            if (open_file("WINHELP.HLP"))
                contents();
            return 0;
        case IDM_ABOUT:
            MessageBox(h, "Windows Help\nfor the native Windows 3.x API of murm386.",
                       "About Help", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_SETFOCUS:
        SetFocus(htopic);
        return 0;
    case WM_DESTROY:
        if (hpopup)
            DestroyWindow(hpopup);
        topic_free(&T);
        fonts_free();
        hlp_close(H);
        H = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev;
    hinst = inst;
    request_msg = RegisterWindowMessage(WINHELP_MESSAGE_N);
    T.page = P.page = -1;
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WINHELP_CLASS_N;
    RegisterClass(&wc);
    wc.lpfnWndProc = TopicProc;
    wc.hIcon = 0;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "WinHelpTopic";
    RegisterClass(&wc);
    wc.lpfnWndProc = PopupProc;
    wc.lpszClassName = "WinHelpPopup";
    RegisterClass(&wc);

    HMENU file = CreatePopupMenu();
    AppendMenu(file, MF_STRING, IDM_OPEN, "&Open...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT, "E&xit");
    HMENU edit = CreatePopupMenu();
    AppendMenu(edit, MF_STRING, IDM_COPY, "&Copy");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_HOWTO, "&How to Use Help");
    AppendMenu(help, MF_SEPARATOR, 0, NULL);
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Help...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)file, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)edit, "&Edit");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindow(WINHELP_CLASS_N, "Windows Help", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          sw / 8, sh / 12, sw * 3 / 4, sh * 5 / 6, 0, bar, inst, NULL);
    if (!h)
        return 1;
    hns = CreateWindow("WinHelpTopic", NULL, WS_CHILD, 0, 0, 0, 0, h, (HMENU)1, inst, NULL);
    htopic = CreateWindow("WinHelpTopic", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0,
                          h, (HMENU)2, inst, NULL);
    fonts_make();
    buttons_reset();
    update_buttons();
    ShowWindow(h, show);
    UpdateWindow(h);
    if (cmd && cmd[0])
        request(cmd);

    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
