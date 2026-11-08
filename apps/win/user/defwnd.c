/*
 * defwnd.c - DefWindowProc and the desktop window.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

static HBRUSH class_brush(wwnd *w)
{
    HBRUSH b = w->cls->wc.hbrBackground;
    if (b && b <= COLOR_MAX)            /* (HBRUSH)(COLOR_xxx + 1) */
        return user_sys_brush(b - 1);
    return b;
}

static int size_cursor(int hit)
{
    switch (hit) {
    case HTLEFT: case HTRIGHT: return WCUR_SIZEWE;
    case HTTOP: case HTBOTTOM: return WCUR_SIZENS;
    case HTTOPLEFT: case HTBOTTOMRIGHT: return WCUR_SIZENS;   /* no diagonal shapes yet */
    case HTTOPRIGHT: case HTBOTTOMLEFT: return WCUR_SIZEWE;
    default: return -1;
    }
}

LRESULT WINAPI DefWindowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    switch (msg) {
    case WM_NCCREATE:
        return TRUE;
    case WM_NCHITTEST:
        return user_nc_hittest(w, (int16_t)LOWORD(lp), (int16_t)HIWORD(lp));
    case WM_NCPAINT:
        user_nc_paint(w);
        return 0;
    case WM_NCACTIVATE:
        w->active_frame = wp != 0;
        user_nc_paint(w);
        return TRUE;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        user_nc_button(w, msg, (int)wp, (int16_t)LOWORD(lp), (int16_t)HIWORD(lp));
        return 0;
    case WM_SETCURSOR: {
        if ((w->style & WS_CHILD) && w->parent &&
            SendMessage(w->parent->h, WM_SETCURSOR, wp, lp))
            return TRUE;
        int hit = (int16_t)LOWORD(lp);
        if (hit == HTCLIENT && w->cls->wc.hCursor) {
            SetCursor(w->cls->wc.hCursor);
            return TRUE;
        }
        int sc = size_cursor(hit);
        cursor_select_builtin(sc >= 0 ? sc : WCUR_ARROW);
        return hit == HTCLIENT ? FALSE : TRUE;
    }
    case WM_MOUSEACTIVATE:
        if ((w->style & WS_CHILD) && w->parent) {
            LRESULT r = SendMessage(w->parent->h, WM_MOUSEACTIVATE, wp, lp);
            if (r)
                return r;
        }
        return MA_ACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && !(w->style & WS_MINIMIZE))
            SetFocus(h);
        return 0;
    case WM_ERASEBKGND: {
        HBRUSH b = class_brush(w);
        if (!b)
            return 0;
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_SYSCOMMAND:
        user_syscommand(w, (UINT)wp, (int16_t)LOWORD(lp), (int16_t)HIWORD(lp));
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR: {
        /* keys of child windows are handled by the top-level window */
        if ((w->style & WS_CHILD) && w->parent && w->parent != U.desktop)
            return SendMessage(w->parent->h, msg, wp, lp);
        if (msg == WM_SYSKEYDOWN) {
            w->alt_tap = wp == VK_MENU;
            if (wp == VK_F4 && (lp & (1L << 29)))
                PostMessage(h, WM_SYSCOMMAND, SC_CLOSE, 0);
            else if (wp == VK_TAB && (lp & (1L << 29)))
                user_alt_tab(w);
            else if (wp == VK_ESCAPE && (lp & (1L << 29)))
                PostMessage(h, WM_SYSCOMMAND, SC_NEXTWINDOW, 0);
            else if (wp == VK_F10)
                SendMessage(h, WM_SYSCOMMAND, SC_KEYMENU, 0);
        } else if (msg == WM_SYSKEYUP) {
            if (wp == VK_MENU && w->alt_tap)
                SendMessage(h, WM_SYSCOMMAND, SC_KEYMENU, 0);
            w->alt_tap = 0;
        } else if (wp == '\r' && (w->style & WS_MINIMIZE)) {
            /* Enter on the active icon restores the window */
            w->alt_tap = 0;
            PostMessage(h, WM_SYSCOMMAND, SC_RESTORE, 0);
        } else if (wp == ' ' || wp > ' ') {
            w->alt_tap = 0;
            SendMessage(h, WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)(unsigned char)wp);
        }
        return 0;
    }
    case WM_SETTEXT:
        win_free(w->text);
        w->text = win_strdup(lp ? (LPCSTR)(intptr_t)lp : "");
        if (user_has_caption(w->style) || (w->style & WS_MINIMIZE))
            user_nc_paint(w);
        return TRUE;
    case WM_GETTEXT: {
        if (!wp || !lp)
            return 0;
        lstrcpyn((LPSTR)(intptr_t)lp, w->text ? w->text : "", (int)wp);
        return lstrlen((LPSTR)(intptr_t)lp);
    }
    case WM_GETTEXTLENGTH:
        return lstrlen(w->text);
    case WM_CTLCOLOR: {
        HDC dc = (HDC)wp;
        int type = HIWORD(lp);
        if (type == CTLCOLOR_SCROLLBAR) {
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, RGB(0, 0, 0));
            return user_sys_brush(COLOR_SCROLLBAR);
        }
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return user_sys_brush(COLOR_WINDOW);
    }
    case WM_CANCELMODE:
        if (U.capture == w)
            ReleaseCapture();
        return 0;
    case WM_QUERYOPEN:
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_GETFONT:
        return 0;
    default:
        return 0;
    }
}

LRESULT CALLBACK user_desktop_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH pat = user_desktop_pattern();
        if (pat) {
            /* the pattern's set bits in black, the rest in the desktop colour */
            SetTextColor((HDC)wp, GetSysColor(COLOR_BACKGROUND));
            SetBkColor((HDC)wp, RGB(0, 0, 0));
            FillRect((HDC)wp, &rc, pat);
        } else {
            FillRect((HDC)wp, &rc, user_sys_brush(COLOR_BACKGROUND));
        }
        return 1;
    }
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_NCPAINT:
        return 0;
    case WM_LBUTTONDBLCLK:          /* a double click on the desktop: the Task List */
        PostMessage(h, WM_SYSCOMMAND, SC_TASKLIST, 0);
        return 0;
    default:
        return DefWindowProc(h, msg, wp, lp);
    }
}

/* Ask the parent for colours: WM_CTLCOLOR(hdc, MAKELONG(hwnd, type)). */
HBRUSH user_ctl_color(HWND h, HDC dc, int type)
{
    wwnd *w = user_wnd(h);
    HWND p = (w && w->parent && w->parent != U.desktop) ? w->parent->h : h;
    HBRUSH b = (HBRUSH)SendMessage(p, WM_CTLCOLOR, dc, MAKELPARAM(h, type));
    if (!b)
        b = (HBRUSH)DefWindowProc(p, WM_CTLCOLOR, dc, MAKELPARAM(h, type));
    return b;
}
