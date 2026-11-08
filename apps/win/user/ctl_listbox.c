/*
 * ctl_listbox.c - LISTBOX window class.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

void *user_ctl_data(HWND h);
void  user_ctl_set_data(HWND h, void *p);

typedef struct {
    char  *s;
    DWORD  data;
    int    sel;
} lbitem;

typedef struct {
    lbitem *items;
    int     n, cap;
    int     top, cur, caret;
    int     ih;
    int     focus, tracking;
    int     has_vs;             /* created with WS_VSCROLL */
} wlist;

static DWORD lstyle(HWND h) { return (DWORD)GetWindowLong(h, GWL_STYLE); }
static int multisel(HWND h) { return (lstyle(h) & LBS_MULTIPLESEL) != 0; }

static void notify(HWND h, int code)
{
    if (!(lstyle(h) & LBS_NOTIFY) && code != LBN_ERRSPACE)
        return;
    wwnd *w = user_wnd(h);
    if (w && w->parent && w->parent != U.desktop)
        SendMessage(w->parent->h, WM_COMMAND, w->id, MAKELPARAM(h, code));
}

static int page(HWND h, wlist *l)
{
    RECT r;
    GetClientRect(h, &r);
    int n = r.bottom / l->ih;
    return n > 0 ? n : 1;
}

static void update_scroll(HWND h, wlist *l)
{
    if (!l->has_vs)
        return;
    int maxtop = l->n - page(h, l);
    if (maxtop < 0) maxtop = 0;
    if (l->top > maxtop) l->top = maxtop;
    SetScrollRange(h, SB_VERT, 0, maxtop > 0 ? maxtop : 0, FALSE);
    if (maxtop > 0)
        SetScrollPos(h, SB_VERT, l->top, TRUE);
}

static void item_rect(HWND h, wlist *l, int i, RECT *r)
{
    GetClientRect(h, r);
    r->top = (i - l->top) * l->ih;
    r->bottom = r->top + l->ih;
}

static void paint_item(HWND h, HDC dc, wlist *l, int i, HBRUSH bg, COLORREF fg, COLORREF bk)
{
    RECT r;
    item_rect(h, l, i, &r);
    int sel = i < l->n && (multisel(h) ? l->items[i].sel : i == l->cur);
    if (sel) {
        FillRect(dc, &r, user_sys_brush(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
    } else {
        FillRect(dc, &r, bg);
        SetTextColor(dc, fg);
        SetBkColor(dc, bk);
    }
    if (i < l->n) {
        SetBkMode(dc, TRANSPARENT);
        TextOut(dc, r.left + 2, r.top, l->items[i].s, lstrlen(l->items[i].s));
    }
    if (l->focus && i == l->caret)
        DrawFocusRect(dc, &r);
}

static void paint_all(HWND h, HDC dc, wlist *l)
{
    HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_LISTBOX);
    COLORREF fg = GetTextColor(dc), bk = GetBkColor(dc);
    SelectObject(dc, user_ctl_font(h));
    RECT rc;
    GetClientRect(h, &rc);
    int last = l->top + page(h, l);
    for (int i = l->top; i <= last; i++)
        paint_item(h, dc, l, i, bg, fg, bk);
    /* below the last row */
    RECT r = rc;
    r.top = (last + 1 - l->top) * l->ih;
    if (r.top < r.bottom)
        FillRect(dc, &r, bg);
}

static void redraw(HWND h)
{
    wlist *l = (wlist *)user_ctl_data(h);
    if (!l || !IsWindowVisible(h) || (lstyle(h) & LBS_NOREDRAW))
        return;
    HDC dc = GetDC(h);
    paint_all(h, dc, l);
    ReleaseDC(h, dc);
}

static void ensure_visible(HWND h, wlist *l, int i)
{
    int p = page(h, l);
    if (i < l->top) l->top = i;
    if (i >= l->top + p) l->top = i - p + 1;
    if (l->top < 0) l->top = 0;
    update_scroll(h, l);
}

static int insert(HWND h, wlist *l, int at, LPCSTR s)
{
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 16;
        lbitem *n = (lbitem *)win_realloc(l->items, sizeof(lbitem) * (size_t)nc);
        if (!n) {
            notify(h, LBN_ERRSPACE);
            return LB_ERRSPACE;
        }
        l->items = n;
        l->cap = nc;
    }
    if (at < 0 || at > l->n)
        at = l->n;
    memmove(&l->items[at + 1], &l->items[at], sizeof(lbitem) * (size_t)(l->n - at));
    l->items[at].s = win_strdup(s ? s : "");
    l->items[at].data = 0;
    l->items[at].sel = 0;
    l->n++;
    if (l->cur >= at) l->cur++;
    if (l->caret >= at && l->n > 1) l->caret++;
    update_scroll(h, l);
    redraw(h);
    return at;
}

static void select_one(HWND h, wlist *l, int i, int notify_change)
{
    if (i < 0 || i >= l->n)
        return;
    int changed = l->cur != i;
    l->cur = i;
    l->caret = i;
    ensure_visible(h, l, i);
    redraw(h);
    if (changed && notify_change)
        notify(h, LBN_SELCHANGE);
}

LRESULT CALLBACK user_listbox_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wlist *l = (wlist *)user_ctl_data(h);
    switch (msg) {
    case WM_CREATE:
        l = (wlist *)win_alloc(sizeof(*l));
        if (!l)
            return -1;
        user_ctl_set_data(h, l);
        l->cur = -1;
        l->ih = 16;
        l->has_vs = (lstyle(h) & WS_VSCROLL) != 0;
        return 0;
    case WM_NCDESTROY:
        if (l) {
            for (int i = 0; i < l->n; i++)
                win_free(l->items[i].s);
            win_free(l->items);
            win_free(l);
        }
        user_ctl_set_data(h, NULL);
        RemoveProp(h, MAKEINTATOM(1));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (l)
            paint_all(h, dc, l);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        if (l)
            update_scroll(h, l);
        return 0;
    case WM_SETFONT:
        SetProp(h, MAKEINTATOM(1), (HANDLE)wp);
        if (lp)
            InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return GetProp(h, MAKEINTATOM(1));
    case WM_SETREDRAW: {
        wwnd *w = user_wnd(h);
        if (wp) w->style &= ~(DWORD)LBS_NOREDRAW; else w->style |= LBS_NOREDRAW;
        if (wp) InvalidateRect(h, NULL, TRUE);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
        if (l) {
            l->focus = 1;
            if (l->caret < 0) l->caret = 0;
            redraw(h);
            notify(h, LBN_SETFOCUS);
        }
        return 0;
    case WM_KILLFOCUS:
        if (l) {
            l->focus = 0;
            redraw(h);
            notify(h, LBN_KILLFOCUS);
        }
        return 0;
    case WM_ENABLE:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        if (!l)
            return 0;
        if (GetFocus() != h)
            SetFocus(h);
        int i = l->top + (int16_t)HIWORD(lp) / l->ih;
        if (i >= l->n)
            return 0;
        if (multisel(h)) {
            l->items[i].sel ^= 1;
            l->caret = i;
            redraw(h);
            notify(h, LBN_SELCHANGE);
        } else {
            select_one(h, l, i, 1);
            SetCapture(h);
            l->tracking = 1;
        }
        if (msg == WM_LBUTTONDBLCLK)
            notify(h, LBN_DBLCLK);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (l && l->tracking) {
            int y = (int16_t)HIWORD(lp);
            int i = l->top + (y < 0 ? -1 : y / l->ih);
            if (i < 0) i = 0;
            if (i >= l->n) i = l->n - 1;
            select_one(h, l, i, 1);
        }
        return 0;
    case WM_LBUTTONUP:
        if (l && l->tracking) {
            l->tracking = 0;
            ReleaseCapture();
        }
        return 0;
    case WM_KEYDOWN: {
        if (!l || !l->n)
            return 0;
        int i = l->caret < 0 ? 0 : l->caret, p = page(h, l);
        switch (wp) {
        case VK_UP: i--; break;
        case VK_DOWN: i++; break;
        case VK_PRIOR: i -= p - 1; break;
        case VK_NEXT: i += p - 1; break;
        case VK_HOME: i = 0; break;
        case VK_END: i = l->n - 1; break;
        case VK_SPACE:
            if (multisel(h)) {
                l->items[i].sel ^= 1;
                redraw(h);
                notify(h, LBN_SELCHANGE);
            }
            return 0;
        default:
            return 0;
        }
        if (i < 0) i = 0;
        if (i >= l->n) i = l->n - 1;
        if (multisel(h)) {
            l->caret = i;
            ensure_visible(h, l, i);
            redraw(h);
        } else {
            select_one(h, l, i, 1);
        }
        return 0;
    }
    case WM_CHAR: {
        if (!l || !l->n || wp < 32)
            return 0;
        char c = (char)wp;
        int start = l->caret < 0 ? 0 : l->caret;
        for (int k = 1; k <= l->n; k++) {
            int i = (start + k) % l->n;
            char a[2] = { l->items[i].s[0], 0 }, b[2] = { c, 0 };
            if (!lstrcmpi(a, b)) {
                if (multisel(h)) {
                    l->caret = i;
                    ensure_visible(h, l, i);
                    redraw(h);
                } else {
                    select_one(h, l, i, 1);
                }
                break;
            }
        }
        return 0;
    }
    case WM_VSCROLL: {
        if (!l)
            return 0;
        int p = page(h, l), maxtop = l->n - p, t = l->top;
        if (maxtop < 0) maxtop = 0;
        switch (wp) {
        case SB_LINEUP: t--; break;
        case SB_LINEDOWN: t++; break;
        case SB_PAGEUP: t -= p - 1; break;
        case SB_PAGEDOWN: t += p - 1; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: t = (int16_t)LOWORD(lp); break;
        case SB_TOP: t = 0; break;
        case SB_BOTTOM: t = maxtop; break;
        default: return 0;
        }
        if (t > maxtop) t = maxtop;
        if (t < 0) t = 0;
        if (t != l->top) {
            l->top = t;
            update_scroll(h, l);
            redraw(h);
        }
        return 0;
    }
    case LB_ADDSTRING: {
        if (!l)
            return LB_ERR;
        LPCSTR s = (LPCSTR)(intptr_t)lp;
        int at = l->n;
        if (lstyle(h) & LBS_SORT)
            for (at = 0; at < l->n && lstrcmpi(l->items[at].s, s) <= 0; at++)
                ;
        return insert(h, l, at, s);
    }
    case LB_INSERTSTRING:
        return l ? insert(h, l, (int)(int16_t)wp, (LPCSTR)(intptr_t)lp) : LB_ERR;
    case LB_DELETESTRING: {
        int i = (int)wp;
        if (!l || i < 0 || i >= l->n)
            return LB_ERR;
        win_free(l->items[i].s);
        memmove(&l->items[i], &l->items[i + 1], sizeof(lbitem) * (size_t)(l->n - i - 1));
        l->n--;
        if (l->cur == i) l->cur = -1;
        else if (l->cur > i) l->cur--;
        if (l->caret >= l->n) l->caret = l->n - 1;
        update_scroll(h, l);
        InvalidateRect(h, NULL, TRUE);
        return l->n;
    }
    case LB_RESETCONTENT:
        if (l) {
            for (int i = 0; i < l->n; i++)
                win_free(l->items[i].s);
            l->n = 0;
            l->cur = -1;
            l->caret = 0;
            l->top = 0;
            update_scroll(h, l);
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case LB_SETCURSEL:
        if (!l || multisel(h))
            return LB_ERR;
        if ((int)(int16_t)wp < 0 || (int)wp >= l->n) {
            l->cur = -1;
            redraw(h);
            return LB_ERR;
        }
        select_one(h, l, (int)wp, 0);
        return (LRESULT)wp;
    case LB_GETCURSEL:
        return l ? (multisel(h) ? l->caret : l->cur) : LB_ERR;
    case LB_GETTEXT:
        if (!l || (int)wp < 0 || (int)wp >= l->n)
            return LB_ERR;
        lstrcpy((LPSTR)(intptr_t)lp, l->items[wp].s);
        return lstrlen(l->items[wp].s);
    case LB_GETTEXTLEN:
        return (l && (int)wp >= 0 && (int)wp < l->n) ? lstrlen(l->items[wp].s) : LB_ERR;
    case LB_GETCOUNT:
        return l ? l->n : 0;
    case LB_SETTOPINDEX:
        if (l) {
            l->top = (int)wp;
            update_scroll(h, l);
            redraw(h);
        }
        return 0;
    case LB_GETTOPINDEX:
        return l ? l->top : 0;
    case LB_GETITEMDATA:
        return (l && (int)wp >= 0 && (int)wp < l->n) ? (LRESULT)l->items[wp].data : LB_ERR;
    case LB_SETITEMDATA:
        if (!l || (int)wp < 0 || (int)wp >= l->n)
            return LB_ERR;
        l->items[wp].data = (DWORD)lp;
        return 0;
    case LB_SETSEL: {
        if (!l || !multisel(h))
            return LB_ERR;
        int i = (int)(int16_t)LOWORD(lp);
        for (int k = 0; k < l->n; k++)
            if (i == -1 || k == i)
                l->items[k].sel = wp != 0;
        redraw(h);
        return 0;
    }
    case LB_GETSEL:
        if (!l || (int)wp < 0 || (int)wp >= l->n)
            return LB_ERR;
        return multisel(h) ? l->items[wp].sel : (int)wp == l->cur;
    case LB_GETSELCOUNT: {
        if (!l || !multisel(h))
            return LB_ERR;
        int c = 0;
        for (int k = 0; k < l->n; k++)
            c += l->items[k].sel;
        return c;
    }
    case LB_FINDSTRING: {
        if (!l || !l->n)
            return LB_ERR;
        LPCSTR s = (LPCSTR)(intptr_t)lp;
        int n = lstrlen(s), start = (int)(int16_t)wp;
        for (int k = 1; k <= l->n; k++) {
            int i = (start + k + l->n) % l->n;
            int m = 0;
            while (m < n) {
                char a[2] = { l->items[i].s[m], 0 }, b[2] = { s[m], 0 };
                if (!a[0] || lstrcmpi(a, b))
                    break;
                m++;
            }
            if (m == n)
                return i;
        }
        return LB_ERR;
    }
    case LB_GETITEMRECT:
        if (!l || (int)wp < 0 || (int)wp >= l->n)
            return LB_ERR;
        item_rect(h, l, (int)wp, (RECT *)(intptr_t)lp);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}
