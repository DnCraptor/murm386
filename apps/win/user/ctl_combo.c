/*
 * ctl_combo.c - COMBOBOX window class.
 *
 * CBS_SIMPLE: an edit field with a list always shown below it.
 * CBS_DROPDOWN: an edit field and a button that drops the list down.
 * CBS_DROPDOWNLIST: a selection field (no editing) and the button.
 *
 * The dropped list is a pop-up LISTBOX; while it is open the combo box runs
 * a small modal loop (like a menu) so a click anywhere else closes it.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

void *user_ctl_data(HWND h);
void  user_ctl_set_data(HWND h, void *p);

#define ID_EDIT  1001
#define ID_LIST  1000
#define FIELD_H  22

typedef struct {
    HWND edit, list;
    int  drop_h;          /* height of the dropped list */
    int  dropped;
    int  focus;
    int  pressed;         /* button drawn pressed */
} wcombo;

static WNDPROC edit_orig;

static int ctype(HWND h) { return (int)(GetWindowLong(h, GWL_STYLE) & 3); }

static void notify(HWND h, int code)
{
    wwnd *w = user_wnd(h);
    if (w && w->parent && w->parent != U.desktop)
        SendMessage(w->parent->h, WM_COMMAND, w->id, MAKELPARAM(h, code));
}

static void button_rect(HWND h, RECT *r)
{
    GetClientRect(h, r);
    r->bottom = FIELD_H;
    r->left = r->right - UM_SCROLL;
}

static void field_rect(HWND h, RECT *r)
{
    GetClientRect(h, r);
    r->bottom = FIELD_H;
    if (ctype(h) != CBS_SIMPLE)
        r->right -= UM_SCROLL + (ctype(h) == CBS_DROPDOWNLIST ? 0 : 2);
}

static void paint(HWND h, HDC dc, wcombo *c)
{
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_EDIT);
    if (ctype(h) == CBS_DROPDOWNLIST) {
        RECT f;
        field_rect(h, &f);
        f.right += 1;
        FillRect(dc, &f, bg);
        FrameRect(dc, &f, user_sys_brush(COLOR_WINDOWFRAME));
        char text[128] = "";
        int sel = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
        if (sel >= 0)
            SendMessage(c->list, LB_GETTEXT, (WPARAM)sel, (LPARAM)(intptr_t)text);
        RECT t = f;
        InflateRect(&t, -2, -2);
        SelectObject(dc, user_ctl_font(h));
        if (c->focus) {
            FillRect(dc, &t, user_sys_brush(COLOR_HIGHLIGHT));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else if (!IsWindowEnabled(h)) {
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        }
        SetBkMode(dc, TRANSPARENT);
        t.left += 1;
        user_draw_text(dc, text, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (c->focus) {
            RECT fr = f;
            InflateRect(&fr, -2, -2);
            DrawFocusRect(dc, &fr);
        }
    } else if (ctype(h) == CBS_DROPDOWN) {
        /* gap between the edit and the button */
        RECT g = { rc.right - UM_SCROLL - 2, 0, rc.right - UM_SCROLL, FIELD_H };
        FillRect(dc, &g, user_ctl_color(h, dc, CTLCOLOR_DLG));
    }
    if (ctype(h) != CBS_SIMPLE) {
        RECT b;
        button_rect(h, &b);
        FrameRect(dc, &b, user_sys_brush(COLOR_WINDOWFRAME));
        RECT in = b;
        InflateRect(&in, -1, -1);
        user_draw_3d(dc, &in, c->pressed);
        if (c->pressed)
            OffsetRect(&in, 1, 1);
        user_draw_arrow(dc, &in, 1, GetSysColor(IsWindowEnabled(h) ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
    }
}

static void redraw(HWND h)
{
    wcombo *c = (wcombo *)user_ctl_data(h);
    if (!c || !IsWindowVisible(h))
        return;
    HDC dc = GetDC(h);
    paint(h, dc, c);
    ReleaseDC(h, dc);
}

/* The selection's text goes into the edit field. */
static void sync_edit(wcombo *c)
{
    if (!c->edit)
        return;
    char text[256] = "";
    int sel = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
    if (sel >= 0)
        SendMessage(c->list, LB_GETTEXT, (WPARAM)sel, (LPARAM)(intptr_t)text);
    SetWindowText(c->edit, text);
    SendMessage(c->edit, EM_SETSEL, 0, MAKELPARAM(0, -1));
}

static void select_index(HWND h, wcombo *c, int i, int notify_change)
{
    int n = (int)SendMessage(c->list, LB_GETCOUNT, 0, 0);
    if (!n)
        return;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    int old = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
    SendMessage(c->list, LB_SETCURSEL, (WPARAM)i, 0);
    sync_edit(c);
    redraw(h);
    if (notify_change && old != i)
        notify(h, CBN_SELCHANGE);
}

/* ---------------------------------------------------------------- drop-down */

static void list_item_at(wcombo *c, int sx, int sy, int *index)
{
    wwnd *l = user_wnd(c->list);
    *index = -1;
    POINT p = { sx, sy };
    if (!PtInRect(&l->client, p))
        return;
    int top = (int)SendMessage(c->list, LB_GETTOPINDEX, 0, 0);
    int i = top + (sy - l->client.top) / 16;
    if (i < (int)SendMessage(c->list, LB_GETCOUNT, 0, 0))
        *index = i;
}

static void close_up(HWND h, wcombo *c, int ok, int start_sel)
{
    ShowWindow(c->list, SW_HIDE);
    c->dropped = 0;
    c->pressed = 0;
    redraw(h);
    if (ok) {
        sync_edit(c);
        notify(h, CBN_SELENDOK);
        if ((int)SendMessage(c->list, LB_GETCURSEL, 0, 0) != start_sel)
            notify(h, CBN_SELCHANGE);
    } else {
        SendMessage(c->list, LB_SETCURSEL, (WPARAM)start_sel, 0);
        notify(h, CBN_SELENDCANCEL);
    }
    notify(h, CBN_CLOSEUP);
}

static void drop_down(HWND h, wcombo *c, int by_mouse)
{
    if (c->dropped || ctype(h) == CBS_SIMPLE)
        return;
    notify(h, CBN_DROPDOWN);
    wwnd *w = user_wnd(h);
    int n = (int)SendMessage(c->list, LB_GETCOUNT, 0, 0);
    int lh = n * 16 + 2;
    int max_h = c->drop_h - FIELD_H;
    if (max_h < 18) max_h = 18;
    if (lh > max_h) lh = max_h;
    if (lh < 18) lh = 18;
    int x = w->rc.left, y = w->rc.top + FIELD_H;
    int lw = w->rc.right - w->rc.left;
    if (y + lh > win_sys.screen.height)
        y = w->rc.top - lh;
    SetWindowPos(c->list, HWND_TOP, x, y, lw, lh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(c->list);
    c->dropped = 1;
    c->pressed = by_mouse;
    redraw(h);

    int start = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
    int dragging = by_mouse;
    SetCapture(h);
    for (;;) {
        MSG m;
        if (!PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            if (win_sys.input)
                win_sys.input->idle();
            continue;
        }
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            close_up(h, c, 0, start);
            break;
        }
        int mouse = (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST) ||
                    (m.message >= WM_NCMOUSEMOVE && m.message <= WM_NCMBUTTONDBLCLK);
        if (mouse) {
            UINT mm = m.message >= WM_MOUSEFIRST ? m.message
                                                  : m.message + WM_MOUSEMOVE - WM_NCMOUSEMOVE;
            int sx = U.mouse_x, sy = U.mouse_y, i;
            POINT p = { sx, sy };
            wwnd *lw2 = user_wnd(c->list);
            list_item_at(c, sx, sy, &i);
            if (mm == WM_MOUSEMOVE) {
                if (i >= 0 && (dragging || (U.buttons & 1)))
                    SendMessage(c->list, LB_SETCURSEL, (WPARAM)i, 0);
                continue;
            }
            if (mm == WM_LBUTTONDOWN || mm == WM_LBUTTONDBLCLK) {
                if (PtInRect(&lw2->rc, p) && !PtInRect(&lw2->client, p)) {
                    /* the list's scroll bar */
                    ReleaseCapture();
                    SendMessage(c->list, WM_NCLBUTTONDOWN,
                                SendMessage(c->list, WM_NCHITTEST, 0, MAKELPARAM(sx, sy)),
                                MAKELPARAM(sx, sy));
                    SetCapture(h);
                    continue;
                }
                if (i >= 0) {
                    SendMessage(c->list, LB_SETCURSEL, (WPARAM)i, 0);
                    dragging = 1;
                    continue;
                }
                close_up(h, c, 0, start);
                break;
            }
            if (mm == WM_LBUTTONUP) {
                if (i >= 0) {
                    close_up(h, c, 1, start);
                    break;
                }
                dragging = 0;
                if (c->pressed) {
                    c->pressed = 0;
                    redraw(h);
                }
            }
            continue;
        }
        if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
            int cur = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
            int n2 = (int)SendMessage(c->list, LB_GETCOUNT, 0, 0);
            switch (m.wParam) {
            case VK_UP: if (cur > 0) SendMessage(c->list, LB_SETCURSEL, (WPARAM)(cur - 1), 0); break;
            case VK_DOWN:
                if ((GetKeyState(VK_MENU) & 0x8000)) { close_up(h, c, 1, start); goto done; }
                if (cur < n2 - 1) SendMessage(c->list, LB_SETCURSEL, (WPARAM)(cur + 1), 0);
                break;
            case VK_RETURN: case VK_F4: case VK_TAB:
                close_up(h, c, 1, start);
                goto done;
            case VK_ESCAPE:
                close_up(h, c, 0, start);
                goto done;
            default: break;
            }
            continue;
        }
        if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST)
            continue;
        DispatchMessage(&m);
        if (!IsWindow(h))
            return;
    }
done:
    ReleaseCapture();
}

/* ---------------------------------------------------------------- edit subclass */

static LRESULT CALLBACK combo_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND combo = GetParent(h);
    if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN || wp == VK_F4))
        return SendMessage(combo, WM_KEYDOWN, wp, lp);
    if (msg == WM_SYSKEYDOWN && (wp == VK_UP || wp == VK_DOWN))
        return SendMessage(combo, WM_KEYDOWN, VK_F4, lp);
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        LRESULT r = CallWindowProc(edit_orig, h, msg, wp, lp);
        HWND other = (HWND)wp;
        if (other != combo && !IsChild(combo, other))
            SendMessage(combo, msg == WM_SETFOCUS ? WM_USER + 0x100 : WM_USER + 0x101, 0, 0);
        return r;
    }
    return CallWindowProc(edit_orig, h, msg, wp, lp);
}

/* ---------------------------------------------------------------- window procedure */

LRESULT CALLBACK user_combo_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wcombo *c = (wcombo *)user_ctl_data(h);
    switch (msg) {
    case WM_CREATE: {
        c = (wcombo *)win_alloc(sizeof(*c));
        if (!c)
            return -1;
        user_ctl_set_data(h, c);
        wwnd *w = user_wnd(h);
        int t = ctype(h);
        DWORD st = w->style;
        /* WS_VSCROLL is for the list; the combo box itself has no scroll bars */
        if (st & (WS_VSCROLL | WS_HSCROLL)) {
            RECT wr = w->rc;
            w->style &= ~(DWORD)(WS_VSCROLL | WS_HSCROLL);
            user_set_window_rect(w, &wr, 0);
        }
        RECT rc;
        GetClientRect(h, &rc);
        c->drop_h = rc.bottom;
        if (t != CBS_DROPDOWNLIST) {
            RECT f;
            field_rect(h, &f);
            c->edit = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER |
                                   ((st & CBS_AUTOHSCROLL) ? ES_AUTOHSCROLL : 0),
                                   0, 0, f.right, FIELD_H, h, (HMENU)ID_EDIT, w->inst, NULL);
            if (!edit_orig)
                edit_orig = (WNDPROC)(intptr_t)GetWindowLong(c->edit, GWL_WNDPROC);
            SetWindowLong(c->edit, GWL_WNDPROC, (LONG)(intptr_t)combo_edit_proc);
        }
        DWORD ls = WS_BORDER | WS_VSCROLL | ((st & CBS_SORT) ? LBS_SORT : 0);
        if (t == CBS_SIMPLE) {
            c->list = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | ls, 0, FIELD_H + 2,
                                   rc.right, rc.bottom - FIELD_H - 2, h, (HMENU)ID_LIST, w->inst, NULL);
        } else {
            c->list = CreateWindowEx(0, "LISTBOX", "", WS_POPUP | ls, 0, 0, rc.right, 100,
                                     h, 0, w->inst, NULL);
            /* the combo box itself is only as tall as its field */
            RECT r = w->rc;
            if (w->parent && w->parent != U.desktop)
                OffsetRect(&r, -w->parent->client.left, -w->parent->client.top);
            SetWindowPos(h, 0, r.left, r.top, r.right - r.left, FIELD_H,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        }
        return c->list ? 0 : -1;
    }
    case WM_NCDESTROY:
        if (c) {
            if (c->list && !(GetWindowLong(c->list, GWL_STYLE) & WS_CHILD))
                DestroyWindow(c->list);
            win_free(c);
        }
        user_ctl_set_data(h, NULL);
        RemoveProp(h, MAKEINTATOM(1));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (c)
            paint(h, dc, c);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFONT:
        SetProp(h, MAKEINTATOM(1), (HANDLE)wp);
        if (c && c->edit) SendMessage(c->edit, WM_SETFONT, wp, lp);
        if (c && c->list) SendMessage(c->list, WM_SETFONT, wp, lp);
        return 0;
    case WM_GETFONT:
        return GetProp(h, MAKEINTATOM(1));
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
        if (!c)
            return 0;
        if (c->edit) {
            SetFocus(c->edit);
            return 0;
        }
        c->focus = 1;
        redraw(h);
        notify(h, CBN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        if (c && !c->edit) {
            c->focus = 0;
            redraw(h);
            notify(h, CBN_KILLFOCUS);
        }
        return 0;
    case WM_USER + 0x100:           /* the edit got focus from outside */
        notify(h, CBN_SETFOCUS);
        return 0;
    case WM_USER + 0x101:
        notify(h, CBN_KILLFOCUS);
        return 0;
    case WM_ENABLE:
        if (c && c->edit) EnableWindow(c->edit, (BOOL)wp);
        if (c && c->list) EnableWindow(c->list, (BOOL)wp);
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_COMMAND:
        if (!c)
            return 0;
        if (LOWORD(wp) == ID_EDIT || (HWND)LOWORD(lp) == c->edit) {
            if (HIWORD(lp) == EN_CHANGE) notify(h, CBN_EDITCHANGE);
            if (HIWORD(lp) == EN_UPDATE) notify(h, CBN_EDITUPDATE);
        } else if ((HWND)LOWORD(lp) == c->list) {
            if (HIWORD(lp) == LBN_SELCHANGE) {
                sync_edit(c);
                notify(h, CBN_SELCHANGE);
            } else if (HIWORD(lp) == LBN_DBLCLK) {
                notify(h, CBN_DBLCLK);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!c)
            return 0;
        if (GetFocus() != h && (!c->edit || GetFocus() != c->edit))
            SetFocus(h);
        if (ctype(h) != CBS_SIMPLE)
            drop_down(h, c, 1);
        return 0;
    case WM_KEYDOWN:
        if (!c)
            return 0;
        switch (wp) {
        case VK_F4:
            drop_down(h, c, 0);
            return 0;
        case VK_UP:
        case VK_DOWN: {
            if (GetKeyState(VK_MENU) & 0x8000) {
                drop_down(h, c, 0);
                return 0;
            }
            int cur = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
            select_index(h, c, cur < 0 ? 0 : cur + (wp == VK_DOWN ? 1 : -1), 1);
            return 0;
        }
        case VK_HOME: if (!c->edit) select_index(h, c, 0, 1); return 0;
        case VK_END: if (!c->edit) select_index(h, c, 1 << 30, 1); return 0;
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (c && (wp == VK_DOWN || wp == VK_UP)) {
            drop_down(h, c, 0);
            return 0;
        }
        break;
    case WM_CHAR:
        if (c && !c->edit && wp >= ' ') {
            char s[2] = { (char)wp, 0 };
            int cur = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
            int i = (int)SendMessage(c->list, LB_FINDSTRING, (WPARAM)cur, (LPARAM)(intptr_t)s);
            if (i >= 0)
                select_index(h, c, i, 1);
        }
        return 0;

    /* ---- messages */
    case CB_ADDSTRING:
        return c ? SendMessage(c->list, LB_ADDSTRING, wp, lp) : CB_ERR;
    case CB_INSERTSTRING:
        return c ? SendMessage(c->list, LB_INSERTSTRING, wp, lp) : CB_ERR;
    case CB_DELETESTRING:
        return c ? SendMessage(c->list, LB_DELETESTRING, wp, lp) : CB_ERR;
    case CB_RESETCONTENT:
        if (c) {
            SendMessage(c->list, LB_RESETCONTENT, 0, 0);
            if (c->edit) SetWindowText(c->edit, "");
            redraw(h);
        }
        return CB_OKAY;
    case CB_GETCOUNT:
        return c ? SendMessage(c->list, LB_GETCOUNT, 0, 0) : 0;
    case CB_GETCURSEL:
        return c ? SendMessage(c->list, LB_GETCURSEL, 0, 0) : CB_ERR;
    case CB_SETCURSEL: {
        if (!c)
            return CB_ERR;
        LRESULT r = SendMessage(c->list, LB_SETCURSEL, wp, 0);
        sync_edit(c);
        redraw(h);
        return r;
    }
    case CB_GETLBTEXT:
        return c ? SendMessage(c->list, LB_GETTEXT, wp, lp) : CB_ERR;
    case CB_GETLBTEXTLEN:
        return c ? SendMessage(c->list, LB_GETTEXTLEN, wp, lp) : CB_ERR;
    case CB_FINDSTRING:
    case CB_FINDSTRINGEXACT:
        return c ? SendMessage(c->list, LB_FINDSTRING, wp, lp) : CB_ERR;
    case CB_SELECTSTRING: {
        if (!c)
            return CB_ERR;
        int i = (int)SendMessage(c->list, LB_FINDSTRING, wp, lp);
        if (i >= 0)
            select_index(h, c, i, 0);
        return i;
    }
    case CB_GETITEMDATA:
        return c ? SendMessage(c->list, LB_GETITEMDATA, wp, lp) : CB_ERR;
    case CB_SETITEMDATA:
        return c ? SendMessage(c->list, LB_SETITEMDATA, wp, lp) : CB_ERR;
    case CB_SHOWDROPDOWN:
        if (c && wp)
            drop_down(h, c, 0);
        return TRUE;
    case CB_GETDROPPEDSTATE:
        return c ? c->dropped : 0;
    case CB_LIMITTEXT:
        if (c && c->edit) SendMessage(c->edit, EM_LIMITTEXT, wp, 0);
        return TRUE;
    case CB_GETEDITSEL:
        return c && c->edit ? SendMessage(c->edit, EM_GETSEL, 0, 0) : CB_ERR;
    case CB_SETEDITSEL:
        if (c && c->edit) SendMessage(c->edit, EM_SETSEL, 0, lp);
        return TRUE;
    case CB_GETDROPPEDCONTROLRECT:
        if (c && lp) {
            RECT *r = (RECT *)(intptr_t)lp;
            GetWindowRect(h, r);
            r->bottom = r->top + c->drop_h;
        }
        return CB_OKAY;
    case WM_SETTEXT:
        if (c && c->edit)
            return SendMessage(c->edit, WM_SETTEXT, wp, lp);
        if (c) {
            int i = (int)SendMessage(c->list, LB_FINDSTRING, (WPARAM)-1, lp);
            if (i >= 0)
                select_index(h, c, i, 0);
        }
        return TRUE;
    case WM_GETTEXT:
        if (c && c->edit)
            return SendMessage(c->edit, WM_GETTEXT, wp, lp);
        if (c && wp) {
            char buf[256] = "";
            int i = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
            if (i >= 0)
                SendMessage(c->list, LB_GETTEXT, (WPARAM)i, (LPARAM)(intptr_t)buf);
            lstrcpyn((LPSTR)(intptr_t)lp, buf, (int)wp);
            return lstrlen((LPSTR)(intptr_t)lp);
        }
        return 0;
    case WM_GETTEXTLENGTH:
        if (c && c->edit)
            return SendMessage(c->edit, WM_GETTEXTLENGTH, 0, 0);
        if (c) {
            int i = (int)SendMessage(c->list, LB_GETCURSEL, 0, 0);
            return i >= 0 ? SendMessage(c->list, LB_GETTEXTLEN, (WPARAM)i, 0) : 0;
        }
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}
