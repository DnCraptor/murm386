/*
 * sysmet.c - system colours, metrics, DrawText.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

/* Windows 3.1 default scheme. */
static COLORREF sys_colors[COLOR_MAX] = {
    RGB(192, 192, 192),   /* SCROLLBAR */
    RGB(0, 128, 128),     /* BACKGROUND */
    RGB(0, 0, 128),       /* ACTIVECAPTION */
    RGB(255, 255, 255),   /* INACTIVECAPTION */
    RGB(255, 255, 255),   /* MENU */
    RGB(255, 255, 255),   /* WINDOW */
    RGB(0, 0, 0),         /* WINDOWFRAME */
    RGB(0, 0, 0),         /* MENUTEXT */
    RGB(0, 0, 0),         /* WINDOWTEXT */
    RGB(255, 255, 255),   /* CAPTIONTEXT */
    RGB(192, 192, 192),   /* ACTIVEBORDER */
    RGB(192, 192, 192),   /* INACTIVEBORDER */
    RGB(255, 255, 255),   /* APPWORKSPACE */
    RGB(0, 0, 128),       /* HIGHLIGHT */
    RGB(255, 255, 255),   /* HIGHLIGHTTEXT */
    RGB(192, 192, 192),   /* BTNFACE */
    RGB(128, 128, 128),   /* BTNSHADOW */
    RGB(128, 128, 128),   /* GRAYTEXT */
    RGB(0, 0, 0),         /* BTNTEXT */
    RGB(0, 0, 0),         /* INACTIVECAPTIONTEXT */
    RGB(255, 255, 255),   /* BTNHIGHLIGHT */
};

static HBRUSH sys_brushes[COLOR_MAX];

void user_init_sysmet(void)
{
    for (int i = 0; i < COLOR_MAX; i++) {
        sys_brushes[i] = CreateSolidBrush(sys_colors[i]);
        win_handle_set_stock(sys_brushes[i]);
    }
}

DWORD WINAPI GetSysColor(int i) { return (i >= 0 && i < COLOR_MAX) ? sys_colors[i] : 0; }

HBRUSH user_sys_brush(int i) { return (i >= 0 && i < COLOR_MAX) ? sys_brushes[i] : 0; }
HBRUSH WINAPI GetSysColorBrush(int i) { return user_sys_brush(i); }

void WINAPI SetSysColors(int n, const int *idx, const COLORREF *colors)
{
    for (int k = 0; k < n; k++) {
        int i = idx[k];
        if (i < 0 || i >= COLOR_MAX)
            continue;
        sys_colors[i] = colors[k];
        wbrush *b = (wbrush *)win_handle_get(sys_brushes[i], WOBJ_BRUSH);
        if (b)
            b->color = colors[k];
    }
    SendMessage(0xFFFF, WM_SYSCOLORCHANGE, 0, 0);
    InvalidateRect(0, NULL, TRUE);
    for (wwnd *w = U.desktop->child; w; w = w->next)
        w->ncpaint = 1;
}

int WINAPI GetSystemMetrics(int i)
{
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    switch (i) {
    case SM_CXSCREEN: return sw;
    case SM_CYSCREEN: return sh;
    case SM_CXVSCROLL: case SM_CYHSCROLL: return UM_SCROLL;
    case SM_CYCAPTION: return UM_CAPTION + 1;
    case SM_CXBORDER: case SM_CYBORDER: return UM_BORDER;
    case SM_CXDLGFRAME: case SM_CYDLGFRAME: return UM_FRAME;
    case SM_CYVTHUMB: case SM_CXHTHUMB: return UM_SCROLL;
    case SM_CXICON: case SM_CYICON: return UM_ICON;
    case SM_CXCURSOR: case SM_CYCURSOR: return 32;
    case SM_CYMENU: return 18;
    case SM_CXFULLSCREEN: return sw;
    case SM_CYFULLSCREEN: return sh - UM_CAPTION - 1;
    case SM_CXMIN: return 100;
    case SM_CYMIN: return UM_CAPTION + 2 * UM_FRAME + 1;
    case SM_CXSIZE: case SM_CYSIZE: return UM_CAPTION;
    case SM_CXFRAME: case SM_CYFRAME: return UM_FRAME;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    default: return 0;
    }
}

void WINAPI MessageBeep(UINT type) { (void)type; }

/* ---------------------------------------------------------------- DrawText */

/* Width of s[0..n) with '&' prefixes removed unless DT_NOPREFIX. */
static int seg_width(HDC dc, LPCSTR s, int n, UINT fmt, int tabw)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&' && !(fmt & DT_NOPREFIX)) {
            if (i + 1 < n && s[i + 1] == '&')
                i++;
            else
                continue;
            c = '&';
        }
        if (c == '\t' && (fmt & DT_EXPANDTABS)) {
            w = (w / tabw + 1) * tabw;
            continue;
        }
        SIZE sz;
        GetTextExtentPoint(dc, &c, 1, &sz);
        w += sz.cx;
    }
    return w;
}

static void draw_seg(HDC dc, int x, int y, LPCSTR s, int n, UINT fmt, int tabw, int lh)
{
    int x0 = x;
    char buf[256];
    int k = 0, ul_x = -1, ul_w = 0;
    for (int i = 0; i <= n; i++) {
        int flush = i == n;
        char c = i < n ? s[i] : 0;
        if (!flush && c == '&' && !(fmt & DT_NOPREFIX)) {
            if (i + 1 < n && s[i + 1] == '&') {
                i++;
            } else {
                if (i + 1 < n) {
                    /* underline the next character */
                    SIZE pre, ch;
                    GetTextExtentPoint(dc, buf, k, &pre);
                    GetTextExtentPoint(dc, &s[i + 1], 1, &ch);
                    ul_x = x + pre.cx;
                    ul_w = ch.cx - 1;
                }
                continue;
            }
        }
        if (!flush && c == '\t' && (fmt & DT_EXPANDTABS))
            flush = 1;
        if (!flush && k < (int)sizeof(buf)) {
            buf[k++] = c;
            continue;
        }
        if (k) {
            TextOut(dc, x, y, buf, k);
            SIZE sz;
            GetTextExtentPoint(dc, buf, k, &sz);
            x += sz.cx;
            k = 0;
        }
        if (i < n && c == '\t')
            x = x0 + ((x - x0) / tabw + 1) * tabw;
        else if (i < n)
            buf[k++] = c;
    }
    if (ul_x >= 0 && ul_w > 0) {
        HPEN p = CreatePen(PS_SOLID, 1, GetTextColor(dc));
        HGDIOBJ o = SelectObject(dc, p);
        TEXTMETRIC tm;
        GetTextMetrics(dc, &tm);
        MoveTo(dc, ul_x, y + tm.tmAscent + 1);
        LineTo(dc, ul_x + ul_w, y + tm.tmAscent + 1);
        SelectObject(dc, o);
        DeleteObject(p);
    }
    (void)lh;
}

int user_draw_text(HDC dc, LPCSTR s, int n, RECT *rc, UINT fmt)
{
    if (n < 0)
        n = lstrlen(s);
    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);
    int lh = tm.tmHeight + tm.tmExternalLeading;
    int tabw = 8 * tm.tmAveCharWidth;
    int width = rc->right - rc->left;
    int y = rc->top, maxw = 0;

    if (fmt & DT_SINGLELINE) {
        int w = seg_width(dc, s, n, fmt, tabw);
        if (fmt & DT_CALCRECT) {
            rc->right = rc->left + w;
            rc->bottom = rc->top + lh;
            return lh;
        }
        if (fmt & DT_VCENTER)
            y = rc->top + (rc->bottom - rc->top - lh) / 2;
        else if (fmt & DT_BOTTOM)
            y = rc->bottom - lh;
        int x = rc->left;
        if (fmt & DT_CENTER) x = rc->left + (width - w) / 2;
        else if (fmt & DT_RIGHT) x = rc->right - w;
        int saved = 0;
        if (!(fmt & DT_NOCLIP)) {
            saved = SaveDC(dc);
            IntersectClipRect(dc, rc->left, rc->top, rc->right, rc->bottom);
        }
        draw_seg(dc, x, y, s, n, fmt, tabw, lh);
        if (saved)
            RestoreDC(dc, saved);
        return lh;
    }

    int saved = 0;
    if (!(fmt & (DT_NOCLIP | DT_CALCRECT))) {
        saved = SaveDC(dc);
        IntersectClipRect(dc, rc->left, rc->top, rc->right, rc->bottom);
    }
    int i = 0;
    while (i < n || (i == 0 && n == 0)) {
        /* one line: up to newline, or word-wrapped to the width */
        int end = i;
        while (end < n && s[end] != '\n' && s[end] != '\r')
            end++;
        int line_end = end;
        if ((fmt & DT_WORDBREAK) && seg_width(dc, s + i, end - i, fmt, tabw) > width) {
            int brk = -1;
            for (int k = i; k < end; k++) {
                if (s[k] == ' ' || s[k] == '\t') {
                    if (seg_width(dc, s + i, k - i, fmt, tabw) <= width)
                        brk = k;
                    else
                        break;
                }
            }
            if (brk < 0) {          /* no space fits: cut the word */
                brk = i + 1;
                while (brk < end && seg_width(dc, s + i, brk + 1 - i, fmt, tabw) <= width)
                    brk++;
            }
            line_end = brk;
        }
        int w = seg_width(dc, s + i, line_end - i, fmt, tabw);
        if (w > maxw)
            maxw = w;
        if (!(fmt & DT_CALCRECT)) {
            int x = rc->left;
            if (fmt & DT_CENTER) x = rc->left + (width - w) / 2;
            else if (fmt & DT_RIGHT) x = rc->right - w;
            draw_seg(dc, x, y, s + i, line_end - i, fmt, tabw, lh);
        }
        y += lh;
        i = line_end;
        if (i < n && (s[i] == ' ' || s[i] == '\t') && line_end != end)
            while (i < n && s[i] == ' ')
                i++;
        if (i < n && s[i] == '\r')
            i++;
        if (i < n && s[i] == '\n')
            i++;
        if (n == 0)
            break;
    }
    if (saved)
        RestoreDC(dc, saved);
    if (fmt & DT_CALCRECT) {
        rc->right = rc->left + maxw;
        rc->bottom = y;
    }
    return y - rc->top;
}

int WINAPI DrawText(HDC dc, LPCSTR s, int n, LPRECT rc, UINT fmt)
{
    return user_draw_text(dc, s, n, rc, fmt);
}
