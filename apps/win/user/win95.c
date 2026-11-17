/*
 * win95.c - Windows 95's USER for Win16 programs that WINN draws and
 * arranges itself: 3D edges and frame controls (DrawEdge, DrawFrameControl,
 * DrawState), the foreground and shell windows, tiled and cascaded windows,
 * menus' radio items and default items, and Windows 3.0's entry points
 * Windows 3.1 kept (OldExitWindows, OldSetDeskPattern).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

/* ---------------------------------------------------------------- windows */

HWND WINAPI GetForegroundWindow(void)
{
    wwnd *w = U.active ? user_toplevel(U.active) : NULL;
    return w ? w->h : 0;
}

BOOL WINAPI SetForegroundWindow(HWND h)
{
    wwnd *w = user_wnd(h);
    if (!w || w == U.desktop)
        return FALSE;
    user_switch_to(user_toplevel(w));
    return TRUE;
}

/* the shell's window: the first one of the program SYSTEM.INI names */
HWND WINAPI GetShellWindow(void)
{
    HTASK shell = task_shell();
    for (wwnd *w = U.desktop->child; shell && w; w = w->next)
        if (w->task == shell && !w->owner && user_is_visible(w))
            return w->h;
    return 0;
}

/* no animation: the window simply is where it goes */
BOOL WINAPI DrawAnimatedRects(HWND h, int ani, const RECT *from, const RECT *to)
{
    (void)h; (void)ani;
    return from && to;
}

/* side by side (a grid, MDITILE_HORIZONTAL: rows first) or cascaded */
UINT WINAPI TileWindowsN(HWND parent, UINT how, const RECT *rc, UINT count,
                         const HWND *kids, int cascade)
{
    wwnd *p = parent ? user_wnd(parent) : U.desktop;
    if (!p)
        return 0;
    if (!kids && p != U.desktop && p->cls && !lstrcmpi(p->cls->name, "MDICLIENT")) {
        UINT n = 0;
        for (wwnd *w = p->child; w; w = w->next)
            n += user_is_visible(w) && !(w->style & WS_MINIMIZE);
        SendMessage(p->h, cascade ? WM_MDICASCADE : WM_MDITILE, how, 0);
        return n;
    }
    HWND list[64];
    UINT n = 0;
    if (kids) {
        for (UINT i = 0; i < count && n < 64; i++)
            if (IsWindow(kids[i]))
                list[n++] = kids[i];
    } else {
        for (wwnd *w = p->child; w && n < 64; w = w->next)
            if (user_is_visible(w) && !(w->style & WS_MINIMIZE) && !w->owner &&
                (!(how & MDITILE_SKIPDISABLED) || !(w->style & WS_DISABLED)) &&
                (p != U.desktop || (w->style & WS_CAPTION) == WS_CAPTION))
                list[n++] = w->h;
    }
    if (!n)
        return 0;
    RECT area;
    if (rc)
        area = *rc;
    else if (p == U.desktop)
        SetRect(&area, 0, 0, win_sys.screen.width, win_sys.screen.height);
    else
        GetClientRect(p->h, &area);
    int aw = area.right - area.left, ah = area.bottom - area.top;
    if (cascade) {
        int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
        int ww = aw - step * (int)(n - 1), wh = ah - step * (int)(n - 1);
        if (ww < aw * 2 / 3) ww = aw * 2 / 3;
        if (wh < ah * 2 / 3) wh = ah * 2 / 3;
        for (UINT i = 0; i < n; i++) {     /* the first one on top */
            int k = (int)(n - 1 - i) % 8;
            MoveWindow(list[n - 1 - i], area.left + k * step, area.top + k * step, ww, wh, TRUE);
        }
        return n;
    }
    UINT cols = 1;
    while (cols * cols < n)
        cols++;
    UINT rows = (n + cols - 1) / cols;
    if (how & MDITILE_HORIZONTAL) {
        UINT t = cols;
        cols = rows;
        rows = t;
    }
    for (UINT i = 0; i < n; i++) {
        UINT c = i % cols, r = i / cols;
        UINT in_row = r == rows - 1 && n % cols ? n % cols : cols;
        int x0 = area.left + aw * (int)c / (int)in_row, x1 = area.left + aw * (int)(c + 1) / (int)in_row;
        int y0 = area.top + ah * (int)r / (int)rows, y1 = area.top + ah * (int)(r + 1) / (int)rows;
        MoveWindow(list[i], x0, y0, x1 - x0, y1 - y0, TRUE);
    }
    return n;
}

int WINAPI MessageBoxEx(HWND h, LPCSTR text, LPCSTR caption, UINT type, WORD lang)
{
    (void)lang;
    return MessageBox(h, text, caption, type);
}

/* Windows 3.0's ExitWindows, without its arguments */
BOOL WINAPI OldExitWindows(void) { return ExitWindows(0, 0); }

/* WIN.INI's desktop pattern again */
void WINAPI OldSetDeskPattern(void)
{
    user_load_desktop();
    user_repaint_all();
}

BOOL WINAPI DestroyIcon32(HICON icon, UINT flags)
{
    (void)flags;
    return DestroyIcon(icon);
}

/* ---------------------------------------------------------------- menus */

BOOL WINAPI CheckMenuRadioItem(HMENU menu, UINT first, UINT last, UINT check, UINT flags)
{
    if (!menu || first > last)
        return FALSE;
    UINT by = flags & MF_BYPOSITION;
    for (UINT i = first; i <= last; i++)
        CheckMenuItem(menu, i, by | (i == check ? MF_CHECKED : MF_UNCHECKED));
    return TRUE;
}

/* the default items (shown as the others: WINN draws no bold item) */
static struct { HMENU menu; UINT id; } defaults[16];

UINT WINAPI GetMenuDefaultItem(HMENU menu, UINT by_pos, UINT flags)
{
    (void)flags;
    for (unsigned i = 0; i < 16; i++)
        if (defaults[i].menu == menu && IsMenu(menu)) {
            if (!by_pos)
                return defaults[i].id;
            for (int k = 0, n = GetMenuItemCount(menu); k < n; k++)
                if (GetMenuItemID(menu, k) == defaults[i].id)
                    return (UINT)k;
        }
    return (UINT)-1;
}

BOOL WINAPI SetMenuDefaultItem(HMENU menu, UINT item, UINT by_pos)
{
    if (!IsMenu(menu))
        return FALSE;
    UINT id = item;
    if (item != (UINT)-1 && by_pos) {
        if ((int)item >= GetMenuItemCount(menu))
            return FALSE;
        id = GetMenuItemID(menu, (int)item);
    }
    int slot = -1;
    for (int i = 0; i < 16; i++)
        if (defaults[i].menu == menu || (slot < 0 && (!defaults[i].menu || !IsMenu(defaults[i].menu))))
            slot = i;
    if (slot < 0)
        return FALSE;
    defaults[slot].menu = item == (UINT)-1 ? 0 : menu;
    defaults[slot].id = id;
    return TRUE;
}

/* ---------------------------------------------------------------- drawing */

static COLORREF color(int index)
{
    return GetSysColor(index);
}

static void hline(HDC dc, int x0, int x1, int y, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    RECT r = { x0, y, x1, y + 1 };
    if (x1 > x0)
        FillRect(dc, &r, b);
    DeleteObject(b);
}

static void vline(HDC dc, int x, int y0, int y1, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    RECT r = { x, y0, x + 1, y1 };
    if (y1 > y0)
        FillRect(dc, &r, b);
    DeleteObject(b);
}

static void line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, 1, c), old = SelectObject(dc, p);
    MoveTo(dc, x0, y0);
    LineTo(dc, x1, y1);
    SelectObject(dc, old);
    DeleteObject(p);
}

/* one ring of an edge: top-left in tl, bottom-right in br */
static void ring(HDC dc, RECT *r, UINT flags, COLORREF tl, COLORREF br)
{
    if (flags & BF_TOP)
        hline(dc, r->left, r->right, r->top, tl);
    if (flags & BF_LEFT)
        vline(dc, r->left, r->top, r->bottom, tl);
    if (flags & BF_BOTTOM)
        hline(dc, r->left, r->right, r->bottom - 1, br);
    if (flags & BF_RIGHT)
        vline(dc, r->right - 1, r->top, r->bottom, br);
    if (flags & BF_TOP) r->top++;
    if (flags & BF_LEFT) r->left++;
    if (flags & BF_BOTTOM) r->bottom--;
    if (flags & BF_RIGHT) r->right--;
}

BOOL WINAPI DrawEdge(HDC dc, LPRECT rc, UINT edge, UINT flags)
{
    if (!dc || !rc)
        return FALSE;
    RECT r = *rc;
    COLORREF hi = color(COLOR_BTNHIGHLIGHT), sh = color(COLOR_BTNSHADOW);
    COLORREF dk = color(COLOR_3DDKSHADOW), lt = color(COLOR_3DLIGHT);
    if (flags & BF_MONO) {
        hi = sh = dk = lt = color(COLOR_WINDOWFRAME);
    } else if (flags & BF_FLAT) {
        hi = lt = sh = dk = color(COLOR_BTNSHADOW);
    }
    if (flags & BF_DIAGONAL) {
        /* a line from corner to corner (which, by the sides named) */
        COLORREF c = edge & (BDR_RAISEDOUTER | BDR_RAISEDINNER) ? sh : hi;
        int down = (flags & (BF_TOP | BF_LEFT)) == (BF_TOP | BF_LEFT) ||
                   (flags & (BF_BOTTOM | BF_RIGHT)) == (BF_BOTTOM | BF_RIGHT);
        if (down)
            line(dc, r.left, r.top, r.right - 1, r.bottom - 1, c);
        else
            line(dc, r.left, r.bottom - 1, r.right - 1, r.top, c);
        return TRUE;
    }
    /* outer ring, then inner */
    if (edge & BDR_RAISEDOUTER)
        ring(dc, &r, flags, (flags & BF_SOFT) ? hi : lt, dk);
    else if (edge & BDR_SUNKENOUTER)
        ring(dc, &r, flags, sh, hi);
    if (edge & BDR_RAISEDINNER)
        ring(dc, &r, flags, (flags & BF_SOFT) ? lt : hi, sh);
    else if (edge & BDR_SUNKENINNER)
        ring(dc, &r, flags, dk, lt);
    if ((flags & BF_MIDDLE) && r.right > r.left && r.bottom > r.top)
        FillRect(dc, &r, user_sys_brush((flags & BF_MONO) ? COLOR_WINDOW : COLOR_BTNFACE));
    if (flags & BF_ADJUST)
        *rc = r;
    return TRUE;
}

static void check_mark(HDC dc, const RECT *r, COLORREF c)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    int x = r->left + w / 2 - 3, y = r->top + h / 2 - 1;
    for (int i = 0; i < 3; i++) {
        line(dc, x, y + i, x + 2, y + 2 + i, c);
        line(dc, x + 2, y + 2 + i, x + 7, y - 3 + i, c);
    }
}

static void caption_glyph(HDC dc, const RECT *r, UINT kind, COLORREF c)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    int s = (w < h ? w : h) / 2;
    if (s < 4)
        s = 4;
    int x = r->left + (w - s) / 2, y = r->top + (h - s) / 2;
    switch (kind) {
    case DFCS_CAPTIONCLOSE:
        line(dc, x, y, x + s, y + s, c);
        line(dc, x + 1, y, x + s + 1, y + s, c);
        line(dc, x + s - 1, y, x - 1, y + s, c);
        line(dc, x + s, y, x, y + s, c);
        break;
    case DFCS_CAPTIONMIN:
        hline(dc, x, x + s, y + s - 2, c);
        hline(dc, x, x + s, y + s - 1, c);
        break;
    case DFCS_CAPTIONMAX: {
        HBRUSH b = CreateSolidBrush(c);
        RECT f = { x, y, x + s + 1, y + s + 1 };
        FrameRect(dc, &f, b);
        hline(dc, x, x + s + 1, y + 1, c);
        DeleteObject(b);
        break;
    }
    case DFCS_CAPTIONRESTORE: {
        HBRUSH b = CreateSolidBrush(c);
        int t = s * 2 / 3;
        RECT back = { x + s - t, y, x + s + 1, y + t + 1 }, front = { x, y + s - t, x + t + 1, y + s + 1 };
        FrameRect(dc, &back, b);
        FillRect(dc, &front, user_sys_brush(COLOR_BTNFACE));
        FrameRect(dc, &front, b);
        hline(dc, front.left, front.right, front.top + 1, c);
        DeleteObject(b);
        break;
    }
    case DFCS_CAPTIONHELP: {
        RECT t = *r;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, c);
        DrawText(dc, "?", 1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    }
}

BOOL WINAPI DrawFrameControl(HDC dc, LPRECT rc, UINT type, UINT state)
{
    if (!dc || !rc)
        return FALSE;
    RECT r = *rc;
    UINT kind = state & 0xff;
    int pushed = (state & DFCS_PUSHED) != 0, flat = (state & (DFCS_FLAT | DFCS_MONO)) != 0;
    COLORREF ink = color((state & DFCS_INACTIVE) ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
    switch (type) {
    case DFC_BUTTON:
        if (kind & (DFCS_BUTTONRADIO | DFCS_BUTTONRADIOIMAGE | DFCS_BUTTONRADIOMASK)) {
            int s = (r.right - r.left < r.bottom - r.top ? r.right - r.left : r.bottom - r.top);
            RECT c = { r.left, r.top, r.left + s, r.top + s };
            HBRUSH face = user_sys_brush(pushed || (state & DFCS_INACTIVE) ? COLOR_BTNFACE : COLOR_WINDOW);
            HPEN pen = CreatePen(PS_SOLID, 1, color(COLOR_BTNSHADOW));
            HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, face);
            Ellipse(dc, c.left, c.top, c.right, c.bottom);
            SelectObject(dc, op);
            DeleteObject(pen);
            if (state & DFCS_CHECKED) {
                HBRUSH dot = CreateSolidBrush(ink);
                HPEN np = GetStockObject(NULL_PEN);
                SelectObject(dc, dot);
                op = SelectObject(dc, np);
                Ellipse(dc, c.left + s / 3, c.top + s / 3, c.right - s / 3 + 1, c.bottom - s / 3 + 1);
                SelectObject(dc, op);
                SelectObject(dc, ob);
                DeleteObject(dot);
            } else {
                SelectObject(dc, ob);
            }
            return TRUE;
        }
        if (kind & DFCS_BUTTONPUSH) {
            DrawEdge(dc, &r, pushed ? EDGE_SUNKEN : EDGE_RAISED,
                     BF_RECT | BF_MIDDLE | BF_SOFT | (flat ? BF_FLAT : 0) | BF_ADJUST);
            if (state & DFCS_ADJUSTRECT)
                *rc = r;
            return TRUE;
        }
        /* a check box (3-state: its checked state grey) */
        DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT | BF_ADJUST | (flat ? BF_FLAT : 0));
        FillRect(dc, &r, user_sys_brush(pushed || (state & DFCS_INACTIVE) ||
                                        ((kind & DFCS_BUTTON3STATE) && (state & DFCS_CHECKED)) ?
                                        COLOR_BTNFACE : COLOR_WINDOW));
        if (state & DFCS_CHECKED)
            check_mark(dc, &r, (kind & DFCS_BUTTON3STATE) ? color(COLOR_BTNSHADOW) : ink);
        return TRUE;
    case DFC_CAPTION:
    case DFC_SCROLL:
        if (type == DFC_SCROLL && kind == DFCS_SCROLLSIZEGRIP) {
            FillRect(dc, &r, user_sys_brush(COLOR_BTNFACE));
            for (int i = 1; i < 12; i += 4) {
                line(dc, r.right - 1 - i, r.bottom - 1, r.right - 1, r.bottom - 1 - i,
                     color(COLOR_BTNHIGHLIGHT));
                line(dc, r.right - 2 - i, r.bottom - 1, r.right - 1, r.bottom - 2 - i,
                     color(COLOR_BTNSHADOW));
                line(dc, r.right - 3 - i, r.bottom - 1, r.right - 1, r.bottom - 3 - i,
                     color(COLOR_BTNSHADOW));
            }
            return TRUE;
        }
        DrawEdge(dc, &r, pushed ? EDGE_SUNKEN : EDGE_RAISED,
                 BF_RECT | BF_MIDDLE | BF_SOFT | (flat ? BF_FLAT : 0) | BF_ADJUST);
        if (pushed)
            OffsetRect(&r, 1, 1);
        if (type == DFC_CAPTION) {
            caption_glyph(dc, &r, kind, ink);
        } else {
            static const int dirs[6] = { 0, 1, 2, 3, 1, 1 };
            RECT a = r;
            InflateRect(&a, -(a.right - a.left) / 4, -(a.bottom - a.top) / 4);
            user_draw_arrow(dc, &a, kind < 6 ? dirs[kind] : 1, ink);
        }
        return TRUE;
    case DFC_MENU: {
        COLORREF c = color(COLOR_MENUTEXT);
        FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
        if (kind == DFCS_MENUCHECK) {
            check_mark(dc, &r, c);
        } else if (kind == DFCS_MENUBULLET) {
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            HBRUSH dot = CreateSolidBrush(c);
            RECT d = { cx - 2, cy - 2, cx + 3, cy + 3 };
            FillRect(dc, &d, dot);
            DeleteObject(dot);
        } else {
            RECT a = r;
            InflateRect(&a, -(a.right - a.left) / 4, -(a.bottom - a.top) / 4);
            user_draw_arrow(dc, &a, 3, c);
        }
        return TRUE;
    }
    }
    return FALSE;
}

/* text, an icon or a bitmap, as it is or disabled (embossed: a light copy
 * one pixel down and right, the shadow over it) */
BOOL WINAPI DrawStateN(HDC dc, HBRUSH br, UINT type, LPCSTR text, int n, HANDLE image,
                       int x, int y, int cx, int cy, UINT flags)
{
    (void)br;
    if (!dc)
        return FALSE;
    if (type == DST_TEXT || type == DST_PREFIXTEXT) {
        if (!text)
            return FALSE;
        if (n < 0)
            n = lstrlen(text);
        UINT fmt = DT_LEFT | DT_TOP | DT_SINGLELINE | (type == DST_TEXT ? DT_NOPREFIX : 0);
        if (!cx || !cy) {
            RECT m = { 0, 0, 0, 0 };
            DrawText(dc, text, n, &m, fmt | DT_CALCRECT);
            if (!cx) cx = m.right;
            if (!cy) cy = m.bottom;
        }
        RECT r = { x, y, x + cx, y + cy };
        int mode = SetBkMode(dc, TRANSPARENT);
        COLORREF old = GetTextColor(dc);
        if (flags & DSS_DISABLED) {
            RECT s = r;
            OffsetRect(&s, 1, 1);
            SetTextColor(dc, color(COLOR_BTNHIGHLIGHT));
            DrawText(dc, text, n, &s, fmt);
            SetTextColor(dc, color(COLOR_BTNSHADOW));
        } else if (flags & DSS_UNION) {
            SetTextColor(dc, color(COLOR_GRAYTEXT));
        }
        DrawText(dc, text, n, &r, fmt);
        SetTextColor(dc, old);
        SetBkMode(dc, mode);
        return TRUE;
    }
    if (type == DST_ICON)
        return image && (cx && cy ? DrawIconEx(dc, x, y, (HICON)image, cx, cy, 0, 0, DI_NORMAL) :
                         DrawIcon(dc, x, y, (HICON)image));
    if (type == DST_BITMAP) {
        BITMAP bm;
        if (!image || !GetObject(image, sizeof(bm), &bm))
            return FALSE;
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, image);
        if (!cx) cx = bm.bmWidth;
        if (!cy) cy = bm.bmHeight;
        BitBlt(dc, x, y, cx, cy, mem, 0, 0, (flags & DSS_DISABLED) ? MERGEPAINT : SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        return TRUE;
    }
    return FALSE;                       /* DST_COMPLEX: the program's procedure */
}
