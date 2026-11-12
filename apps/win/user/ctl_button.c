/*
 * ctl_button.c - BUTTON and STATIC window classes.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

/* Built-in controls keep a pointer to their state in the window extra
 * bytes; the class reserves sizeof(void *). */
void *user_ctl_data(HWND h)
{
    wwnd *w = user_wnd(h);
    void *p = NULL;
    if (w && w->extra && w->cb_extra >= (int)sizeof(void *))
        memcpy(&p, w->extra, sizeof(p));
    return p;
}

void user_ctl_set_data(HWND h, void *p)
{
    wwnd *w = user_wnd(h);
    if (w && w->extra && w->cb_extra >= (int)sizeof(void *))
        memcpy(w->extra, &p, sizeof(p));
}

HFONT user_ctl_font(HWND h)
{
    HANDLE f = GetProp(h, MAKEINTATOM(1));
    return f ? f : GetStockObject(SYSTEM_FONT);
}

int user_ctl_line(HWND h)
{
    if (!GetProp(h, MAKEINTATOM(1)))
        return 16;
    HDC dc = GetDC(h);
    TEXTMETRIC tm;
    HGDIOBJ old = SelectObject(dc, user_ctl_font(h));
    int lh = GetTextMetrics(dc, &tm) && tm.tmHeight > 0 ? tm.tmHeight : 16;
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    return lh;
}

static void notify(HWND h, int code)
{
    wwnd *w = user_wnd(h);
    if (w && w->parent && w->parent != U.desktop)
        SendMessage(w->parent->h, WM_COMMAND, w->id, MAKELPARAM(h, code));
}

/* ---------------------------------------------------------------- BUTTON */

typedef struct {
    int check;          /* 0, 1, 2 */
    int pushed;         /* highlighted (mouse or space held) */
    int focus;
    int tracking;
} wbutton;

static int btype(HWND h) { return (int)(GetWindowLong(h, GWL_STYLE) & 0x0F); }

static int is_push(int t) { return t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON; }
static int is_check(int t)
{
    return t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_3STATE || t == BS_AUTO3STATE;
}
static int is_radio(int t) { return t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON; }

static void focus_rect_around(HDC dc, const RECT *text)
{
    RECT f = *text;
    InflateRect(&f, 1, 1);
    DrawFocusRect(dc, &f);
}

static void paint_push(HWND h, HDC dc, wbutton *b, const RECT *rc, int def)
{
    char text[128];
    GetWindowText(h, text, sizeof(text));
    HBRUSH frame = user_sys_brush(COLOR_WINDOWFRAME);
    RECT r = *rc;
    /* parent background shows in the rounded corners */
    HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_BTN);
    FillRect(dc, &r, bg);
    RECT o = { r.left + 1, r.top, r.right - 1, r.top + 1 };
    FillRect(dc, &o, frame);
    SetRect(&o, r.left + 1, r.bottom - 1, r.right - 1, r.bottom);
    FillRect(dc, &o, frame);
    SetRect(&o, r.left, r.top + 1, r.left + 1, r.bottom - 1);
    FillRect(dc, &o, frame);
    SetRect(&o, r.right - 1, r.top + 1, r.right, r.bottom - 1);
    FillRect(dc, &o, frame);
    InflateRect(&r, -1, -1);
    if (def) {
        FrameRect(dc, &r, frame);
        InflateRect(&r, -1, -1);
    }
    user_draw_3d(dc, &r, b->pushed);
    RECT t = r;
    if (b->pushed)
        OffsetRect(&t, 1, 1);
    SetBkMode(dc, TRANSPARENT);
    int enabled = IsWindowEnabled(h);
    SetTextColor(dc, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
    HGDIOBJ of = SelectObject(dc, user_ctl_font(h));
    RECT tr = t;
    user_draw_text(dc, text, -1, &tr, DT_CALCRECT | DT_SINGLELINE);
    int tw = tr.right - tr.left, th = tr.bottom - tr.top;
    OffsetRect(&tr, (t.left + t.right - tw) / 2 - tr.left, (t.top + t.bottom - th) / 2 - tr.top);
    user_draw_text(dc, text, -1, &tr, DT_SINGLELINE | DT_NOCLIP);
    if (b->focus)
        focus_rect_around(dc, &tr);
    SelectObject(dc, of);
}

static void paint_check(HWND h, HDC dc, wbutton *b, const RECT *rc, int t)
{
    char text[128];
    GetWindowText(h, text, sizeof(text));
    HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_BTN);
    FillRect(dc, rc, bg);
    int lefttext = (GetWindowLong(h, GWL_STYLE) & BS_LEFTTEXT) != 0;
    int box = 12;
    int by = (rc->top + rc->bottom - box) / 2;
    int bx = lefttext ? rc->right - box : rc->left;
    RECT br = { bx, by, bx + box, by + box };
    HGDIOBJ of = SelectObject(dc, user_ctl_font(h));

    if (is_radio(t)) {
        HGDIOBJ ob = SelectObject(dc, GetStockObject(WHITE_BRUSH));
        HGDIOBJ op = SelectObject(dc, GetStockObject(BLACK_PEN));
        Ellipse(dc, br.left, br.top, br.right, br.bottom);
        if (b->pushed)
            Ellipse(dc, br.left + 1, br.top + 1, br.right - 1, br.bottom - 1);
        if (b->check) {
            SelectObject(dc, GetStockObject(BLACK_BRUSH));
            Ellipse(dc, br.left + 3, br.top + 3, br.right - 3, br.bottom - 3);
        }
        SelectObject(dc, ob);
        SelectObject(dc, op);
    } else {
        FillRect(dc, &br, b->check == 2 ? user_sys_brush(COLOR_BTNFACE) : GetStockObject(WHITE_BRUSH));
        FrameRect(dc, &br, GetStockObject(BLACK_BRUSH));
        if (b->pushed) {
            RECT in = br;
            InflateRect(&in, -1, -1);
            FrameRect(dc, &in, GetStockObject(BLACK_BRUSH));
        }
        if (b->check) {
            HGDIOBJ op = SelectObject(dc, GetStockObject(BLACK_PEN));
            MoveTo(dc, br.left, br.top);
            LineTo(dc, br.right, br.bottom);
            MoveTo(dc, br.right - 1, br.top);
            LineTo(dc, br.left - 1, br.bottom);
            SelectObject(dc, op);
        }
    }
    RECT tr = *rc;
    if (lefttext)
        tr.right = bx - 4;
    else
        tr.left = bx + box + 4;
    RECT cr = tr;
    user_draw_text(dc, text, -1, &cr, DT_CALCRECT | DT_SINGLELINE);
    int th = cr.bottom - cr.top;
    OffsetRect(&cr, lefttext ? tr.right - cr.right : 0, (rc->top + rc->bottom - th) / 2 - cr.top);
    SetBkMode(dc, TRANSPARENT);
    if (!IsWindowEnabled(h))
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    user_draw_text(dc, text, -1, &cr, DT_SINGLELINE | DT_NOCLIP);
    if (b->focus)
        focus_rect_around(dc, &cr);
    SelectObject(dc, of);
}

static void paint_group(HWND h, HDC dc, const RECT *rc)
{
    char text[128];
    GetWindowText(h, text, sizeof(text));
    HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_BTN);
    HGDIOBJ of = SelectObject(dc, user_ctl_font(h));
    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);
    RECT fr = *rc;
    fr.top += tm.tmHeight / 2;
    FrameRect(dc, &fr, user_sys_brush(COLOR_WINDOWFRAME));
    if (text[0]) {
        RECT tr = { rc->left + 8, rc->top, rc->right - 8, rc->top + tm.tmHeight };
        RECT cr = tr;
        user_draw_text(dc, text, -1, &cr, DT_CALCRECT | DT_SINGLELINE);
        cr.left -= 2;
        cr.right += 2;
        FillRect(dc, &cr, bg);
        SetBkMode(dc, TRANSPARENT);
        if (!IsWindowEnabled(h))
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        user_draw_text(dc, text, -1, &tr, DT_SINGLELINE);
    }
    SelectObject(dc, of);
}

static void button_paint(HWND h, HDC dc, wbutton *b)
{
    RECT rc;
    GetClientRect(h, &rc);
    int t = btype(h);
    if (t == BS_OWNERDRAW) {
        wwnd *w = user_wnd(h);
        DRAWITEMSTRUCT di = { 4 /* ODT_BUTTON */, w->id, 0, 1 /* ODA_DRAWENTIRE */,
                              (UINT)((b->pushed ? 1 : 0) | (b->focus ? 0x10 : 0) |
                                     (IsWindowEnabled(h) ? 0 : 4)),
                              h, dc, rc, 0 };
        SendMessage(w->parent->h, WM_DRAWITEM, w->id, (LPARAM)(intptr_t)&di);
    } else if (is_push(t) || t == BS_USERBUTTON) {
        paint_push(h, dc, b, &rc, t == BS_DEFPUSHBUTTON);
    } else if (t == BS_GROUPBOX) {
        paint_group(h, dc, &rc);
    } else {
        paint_check(h, dc, b, &rc, t);
    }
}

static void button_redraw(HWND h)
{
    wbutton *b = (wbutton *)user_ctl_data(h);
    if (!b || !IsWindowVisible(h))
        return;
    HDC dc = GetDC(h);
    button_paint(h, dc, b);
    ReleaseDC(h, dc);
}

static void button_click(HWND h, wbutton *b)
{
    int t = btype(h);
    if (t == BS_AUTOCHECKBOX)
        b->check = !b->check;
    else if (t == BS_AUTO3STATE)
        b->check = (b->check + 1) % 3;
    else if (t == BS_AUTORADIOBUTTON) {
        /* uncheck the other auto radio buttons of the group */
        wwnd *w = user_wnd(h);
        HWND p = w->parent->h;
        HWND first = h;
        for (HWND s = h; s; s = GetWindow(s, GW_HWNDPREV)) {
            first = s;
            if (GetWindowLong(s, GWL_STYLE) & WS_GROUP)
                break;
        }
        for (HWND s = first; s; s = GetWindow(s, GW_HWNDNEXT)) {
            if (s != first && (GetWindowLong(s, GWL_STYLE) & WS_GROUP))
                break;
            if (s != h && (GetWindowLong(s, GWL_STYLE) & 0x0F) == BS_AUTORADIOBUTTON)
                SendMessage(s, BM_SETCHECK, 0, 0);
        }
        (void)p;
        b->check = 1;
    }
    button_redraw(h);
    notify(h, BN_CLICKED);
}

LRESULT CALLBACK user_button_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wbutton *b = (wbutton *)user_ctl_data(h);
    int t = btype(h);
    switch (msg) {
    case WM_CREATE:
        b = (wbutton *)win_alloc(sizeof(*b));
        user_ctl_set_data(h, b);
        return b ? 0 : -1;
    case WM_NCDESTROY:
        win_free(b);
        user_ctl_set_data(h, NULL);
        RemoveProp(h, MAKEINTATOM(1));
        return 0;
    case WM_NCHITTEST:
        if (t == BS_GROUPBOX)
            return HTTRANSPARENT;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (b)
            button_paint(h, dc, b);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ENABLE:
    case WM_SETTEXT: {
        LRESULT r = msg == WM_SETTEXT ? DefWindowProc(h, msg, wp, lp) : 0;
        button_redraw(h);
        return msg == WM_SETTEXT ? r : 0;
    }
    case WM_SETFONT:
        SetProp(h, MAKEINTATOM(1), (HANDLE)wp);
        if (lp)
            button_redraw(h);
        return 0;
    case WM_GETFONT:
        return GetProp(h, MAKEINTATOM(1));
    case WM_GETDLGCODE:
        if (t == BS_DEFPUSHBUTTON) return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
        if (t == BS_PUSHBUTTON) return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
        if (is_radio(t)) return DLGC_BUTTON | DLGC_RADIOBUTTON;
        if (t == BS_GROUPBOX) return DLGC_STATIC;
        return DLGC_BUTTON;
    case WM_SETFOCUS:
        if (b && t != BS_GROUPBOX) {
            b->focus = 1;
            button_redraw(h);
        }
        return 0;
    case WM_KILLFOCUS:
        if (b) {
            b->focus = 0;
            if (b->pushed && !b->tracking)
                b->pushed = 0;
            button_redraw(h);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!b || t == BS_GROUPBOX)
            return 0;
        SetCapture(h);
        b->tracking = 1;
        b->pushed = 1;
        if (GetFocus() != h)
            SetFocus(h);
        button_redraw(h);
        if (msg == WM_LBUTTONDBLCLK && (t == BS_USERBUTTON || is_radio(t)))
            notify(h, BN_DOUBLECLICKED);
        return 0;
    case WM_MOUSEMOVE:
        if (b && b->tracking) {
            RECT rc;
            GetClientRect(h, &rc);
            POINT p = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
            int in = PtInRect(&rc, p);
            if (in != b->pushed) {
                b->pushed = in;
                button_redraw(h);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (b && b->tracking) {
            b->tracking = 0;
            ReleaseCapture();
            if (b->pushed) {
                b->pushed = 0;
                button_click(h, b);
            }
        }
        return 0;
    case WM_CANCELMODE:
        if (b && b->tracking) {
            b->tracking = 0;
            b->pushed = 0;
            ReleaseCapture();
            button_redraw(h);
        }
        return 0;
    case WM_KEYDOWN:
        if (b && wp == VK_SPACE && !b->pushed) {
            b->pushed = 1;
            button_redraw(h);
        }
        return 0;
    case WM_KEYUP:
        if (b && wp == VK_SPACE && b->pushed) {
            b->pushed = 0;
            button_click(h, b);
        }
        return 0;
    case WM_CHAR:
        return 0;
    case BM_GETCHECK:
        return b ? b->check : 0;
    case BM_SETCHECK:
        if (b && (is_check(t) || is_radio(t)) && b->check != (int)wp) {
            b->check = (int)wp;
            button_redraw(h);
        }
        return 0;
    case BM_GETSTATE:
        return b ? (b->check | (b->pushed ? 4 : 0) | (b->focus ? 8 : 0)) : 0;
    case BM_SETSTATE:
        if (b && b->pushed != (wp != 0)) {
            b->pushed = wp != 0;
            button_redraw(h);
        }
        return 0;
    case BM_SETSTYLE: {
        wwnd *w = user_wnd(h);
        w->style = (w->style & ~0x0FUL) | (wp & 0x0F);
        if (lp)
            button_redraw(h);
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- STATIC */

LRESULT CALLBACK user_static_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    DWORD style = (DWORD)GetWindowLong(h, GWL_STYLE);
    int t = (int)(style & 0x0F);
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_GETDLGCODE:
        return DLGC_STATIC;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProc(h, msg, wp, lp);
        InvalidateRect(h, NULL, TRUE);
        return r;
    }
    case WM_ENABLE:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_SETFONT:
        SetProp(h, MAKEINTATOM(1), (HANDLE)wp);
        if (lp)
            InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return GetProp(h, MAKEINTATOM(1));
    case WM_CREATE:
        /* an icon's: its text the icon (#n the system's or the module's
         * n-th, \001 and four hex digits a handle, else a name) */
        if (t == SS_ICON) {
            const char *s = ((const CREATESTRUCT *)lp)->lpszName;
            HINSTANCE inst = ((const CREATESTRUCT *)lp)->hInstance;
            HICON ic = 0;
            if (s && s[0] == '\001') {
                unsigned v = 0;
                for (int i = 1; i < 5 && s[i]; i++)
                    v = v * 16 + (unsigned)(s[i] <= '9' ? s[i] - '0' : (s[i] | 0x20) - 'a' + 10);
                ic = (HICON)(uintptr_t)v;
            } else if (s && s[0] == '#') {
                int id = 0;
                for (const char *p = s + 1; *p >= '0' && *p <= '9'; p++)
                    id = id * 10 + (*p - '0');
                ic = LoadIcon(id >= 32512 ? 0 : inst, MAKEINTRESOURCE(id ? id : 32512));
            } else if (s && s[0]) {
                ic = LoadIcon(inst, s);
            }
            SetProp(h, MAKEINTATOM(2), ic ? ic : LoadIcon(0, IDI_APPLICATION));
        }
        return 0;
    case STM_SETICON: {                 /* the icon drawn, the one before */
        HANDLE was = GetProp(h, MAKEINTATOM(2));
        SetProp(h, MAKEINTATOM(2), (HANDLE)wp);
        InvalidateRect(h, NULL, TRUE);
        return (LRESULT)(uintptr_t)was;
    }
    case STM_GETICON:
        return (LRESULT)(uintptr_t)GetProp(h, MAKEINTATOM(2));
    case WM_NCDESTROY:
        RemoveProp(h, MAKEINTATOM(1));
        RemoveProp(h, MAKEINTATOM(2));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        switch (t) {
        case SS_BLACKRECT: FillRect(dc, &rc, user_sys_brush(COLOR_WINDOWFRAME)); break;
        case SS_GRAYRECT: FillRect(dc, &rc, user_sys_brush(COLOR_BACKGROUND)); break;
        case SS_WHITERECT: FillRect(dc, &rc, user_sys_brush(COLOR_WINDOW)); break;
        case SS_BLACKFRAME: FrameRect(dc, &rc, user_sys_brush(COLOR_WINDOWFRAME)); break;
        case SS_GRAYFRAME: FrameRect(dc, &rc, user_sys_brush(COLOR_BACKGROUND)); break;
        case SS_WHITEFRAME: FrameRect(dc, &rc, user_sys_brush(COLOR_WINDOW)); break;
        case SS_ICON: {
            HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_STATIC);
            FillRect(dc, &rc, bg);
            DrawIcon(dc, 0, 0, (HICON)GetProp(h, MAKEINTATOM(2)));
            break;
        }
        default: {
            char text[512];
            GetWindowText(h, text, sizeof(text));
            HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_STATIC);
            FillRect(dc, &rc, bg);
            HGDIOBJ of = SelectObject(dc, user_ctl_font(h));
            SetBkMode(dc, TRANSPARENT);
            if (!IsWindowEnabled(h))
                SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            UINT fmt = (style & SS_NOPREFIX) ? DT_NOPREFIX : 0;
            if (t == SS_CENTER) fmt |= DT_CENTER | DT_WORDBREAK;
            else if (t == SS_RIGHT) fmt |= DT_RIGHT | DT_WORDBREAK;
            else if (t == SS_LEFT) fmt |= DT_WORDBREAK | DT_EXPANDTABS;
            else fmt |= DT_SINGLELINE;
            user_draw_text(dc, text, -1, &rc, fmt);
            SelectObject(dc, of);
            break;
        }
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}
