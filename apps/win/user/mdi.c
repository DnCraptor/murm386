/*
 * mdi.c - the multiple document interface: the MDICLIENT class,
 * DefFrameProc, DefMDIChildProc and TranslateMDISysAccel (Windows 3.1
 * semantics).
 *
 * The client window keeps the list of children implicitly: they are its
 * child windows whose ids run from idFirstChild on, renumbered when one
 * goes, and listed in the frame's Window menu. One of them is active: its
 * caption follows the frame's, it has the focus while the frame has it.
 * A maximized child fills the client area with its caption still showing
 * (no merge into the frame's menu bar); the frame title then carries the
 * child's in brackets.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

#define MDI_MENU_MAX   9            /* children listed in the Window menu */

typedef struct mdiinfo {
    HWND   frame;
    HWND   active;
    HMENU  winmenu;
    UINT   first;
    int    count;
    int    menu_items;              /* items we appended (with the separator) */
    char  *title;                   /* the frame's own title */
} mdiinfo;

static mdiinfo *info(HWND client)
{
    wwnd *w = user_wnd(client);
    if (!w || !w->extra || w->cb_extra < (int)sizeof(mdiinfo *))
        return NULL;
    mdiinfo *m;
    memcpy(&m, w->extra, sizeof(m));
    return m;
}

static HWND client_of(HWND child)
{
    wwnd *w = user_wnd(child);
    return w && w->parent ? w->parent->h : 0;
}

/* the children in id order */
static int children(HWND client, mdiinfo *m, wwnd **out, int max)
{
    wwnd *c = user_wnd(client);
    int n = 0;
    for (UINT id = m->first; c && id < m->first + (UINT)m->count && n < max; id++)
        for (wwnd *t = c->child; t; t = t->next)
            if (t->id == id && !t->destroying) {
                out[n++] = t;
                break;
            }
    return n;
}

static int is_child(HWND client, mdiinfo *m, wwnd *t)
{
    return t && t->parent && t->parent->h == client && !t->destroying &&
           t->id >= m->first && t->id < m->first + (UINT)m->count;
}

/* ---------------------------------------------------------------- Window menu, title */

static void update_menu(HWND client, mdiinfo *m)
{
    if (!m->winmenu)
        return;
    int n = GetMenuItemCount(m->winmenu);
    for (int i = 0; i < m->menu_items && n > 0; i++)
        DeleteMenu(m->winmenu, (UINT)--n, MF_BYPOSITION);
    m->menu_items = 0;
    wwnd *list[MDI_MENU_MAX];
    int k = children(client, m, list, MDI_MENU_MAX);
    if (!k)
        return;
    AppendMenu(m->winmenu, MF_SEPARATOR, 0, NULL);
    m->menu_items = 1;
    for (int i = 0; i < k; i++) {
        char text[48];
        wsprintf(text, "&%d ", i + 1);
        int l = lstrlen(text);
        GetWindowText(list[i]->h, text + l, (int)sizeof(text) - l);
        AppendMenu(m->winmenu, MF_STRING | (list[i]->h == m->active ? MF_CHECKED : 0),
                   list[i]->id, text);
        m->menu_items++;
    }
}

static void update_title(mdiinfo *m)
{
    if (!m->frame || !m->title)
        return;
    wwnd *a = user_wnd(m->active);
    char text[128];
    if (a && (a->style & WS_MAXIMIZE)) {
        char ct[64];
        GetWindowText(m->active, ct, sizeof(ct));
        wsprintf(text, "%s - [%s]", m->title, ct);
    } else {
        lstrcpyn(text, m->title, sizeof(text));
    }
    wwnd *f = user_wnd(m->frame);
    if (f && lstrcmp(f->text ? f->text : "", text)) {
        win_free(f->text);
        f->text = win_strdup(text);
        if (user_is_visible(f))
            user_nc_paint(f);
    }
}

/* ---------------------------------------------------------------- activation */

static int frame_active(mdiinfo *m) { return m->frame && GetActiveWindow() == m->frame; }

static void activate(HWND client, mdiinfo *m, HWND child)
{
    wwnd *c = user_wnd(child);
    if (child && !is_child(client, m, c))
        return;
    HWND old = m->active;
    if (old == child) {
        if (c)
            user_bring_to_top(c);
        return;
    }
    wwnd *o = user_wnd(old);
    /* a maximized child hands the maximized state to the next one */
    int was_max = o && (o->style & WS_MAXIMIZE);
    m->active = child;
    if (o) {
        SendMessage(old, WM_NCACTIVATE, FALSE, 0);
        SendMessage(old, WM_MDIACTIVATE, FALSE, MAKELPARAM(child, old));
    }
    if (c) {
        user_bring_to_top(c);
        if (was_max && !(c->style & WS_MINIMIZE)) {
            ShowWindow(child, SW_SHOWMAXIMIZED);
            ShowWindow(old, SW_RESTORE);
            user_bring_to_top(c);
        }
        SendMessage(child, WM_NCACTIVATE, frame_active(m), 0);
        SendMessage(child, WM_MDIACTIVATE, TRUE, MAKELPARAM(child, old));
        if (frame_active(m))
            SetFocus(child);
    } else if (frame_active(m)) {
        SetFocus(client);
    }
    update_menu(client, m);
    update_title(m);
}

/* ---------------------------------------------------------------- arranging */

static void avail_rect(HWND client, mdiinfo *m, RECT *rc, int leave_icons)
{
    GetClientRect(client, rc);
    if (!leave_icons)
        return;
    wwnd *list[64];
    int n = children(client, m, list, 64);
    for (int i = 0; i < n; i++)
        if (list[i]->style & WS_MINIMIZE) {
            rc->bottom -= UM_ICONH + 4;
            break;
        }
    if (rc->bottom < rc->top + 40)
        rc->bottom = rc->top + 40;
}

static void icon_arrange(HWND client, mdiinfo *m)
{
    RECT rc;
    GetClientRect(client, &rc);
    wwnd *list[64];
    int n = children(client, m, list, 64), k = 0;
    int per_row = (rc.right - 4) / 76;
    if (per_row < 1)
        per_row = 1;
    for (int i = 0; i < n; i++) {
        if (!(list[i]->style & WS_MINIMIZE))
            continue;
        int x = 4 + (k % per_row) * 76, y = rc.bottom - UM_ICONH - 4 - (k / per_row) * (UM_ICONH + 4);
        SetWindowPos(list[i]->h, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        k++;
    }
}

static void restore_max(wwnd **list, int n)
{
    for (int i = 0; i < n; i++)
        if (list[i]->style & WS_MAXIMIZE)
            ShowWindow(list[i]->h, SW_RESTORE);
}

static void cascade(HWND client, mdiinfo *m)
{
    wwnd *list[64];
    int n = children(client, m, list, 64);
    restore_max(list, n);
    RECT rc;
    avail_rect(client, m, &rc, 1);
    /* bottom of the z-order first, so the active one ends on top, last */
    wwnd *order[64];
    int k = 0;
    wwnd *c = user_wnd(client);
    for (wwnd *t = c ? c->child : NULL; t; t = t->next)
        if (is_child(client, m, t) && !(t->style & WS_MINIMIZE) && user_is_visible(t))
            order[k++] = t;
    int d = UM_CAPTION + UM_FRAME;
    int cx = rc.right - rc.left - d * (k - 1), cy = rc.bottom - rc.top - d * (k - 1);
    if (cx < (rc.right - rc.left) * 2 / 3) cx = (rc.right - rc.left) * 2 / 3;
    if (cy < (rc.bottom - rc.top) * 2 / 3) cy = (rc.bottom - rc.top) * 2 / 3;
    for (int i = 0; i < k; i++) {
        int x = rc.left + (k - 1 - i) * d, y = rc.top + (k - 1 - i) * d;
        if (x + cx > rc.right) x = rc.left;
        if (y + cy > rc.bottom) y = rc.top;
        SetWindowPos(order[i]->h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void tile(HWND client, mdiinfo *m, UINT how)
{
    wwnd *list[64];
    int n = children(client, m, list, 64);
    restore_max(list, n);
    wwnd *t[64];
    int k = 0;
    for (int i = 0; i < n; i++)
        if (!(list[i]->style & WS_MINIMIZE) && user_is_visible(list[i]) &&
            !((how & MDITILE_SKIPDISABLED) && (list[i]->style & WS_DISABLED)))
            t[k++] = list[i];
    if (!k)
        return;
    RECT rc;
    avail_rect(client, m, &rc, 1);
    /* columns of windows; the last column takes the remainder */
    int rows = 1;
    while ((rows + 1) * (rows + 1) <= k)
        rows++;
    int cols = k / rows;
    int horz = (how & MDITILE_HORIZONTAL) != 0;
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    int i = 0;
    for (int c = 0; c < cols; c++) {
        int r_here = c == cols - 1 ? k - i : rows;
        for (int r = 0; r < r_here; r++, i++) {
            int x, y, cx, cy;
            if (!horz) {
                x = rc.left + c * W / cols;  cx = rc.left + (c + 1) * W / cols - x;
                y = rc.top + r * H / r_here; cy = rc.top + (r + 1) * H / r_here - y;
            } else {
                y = rc.top + c * H / cols;   cy = rc.top + (c + 1) * H / cols - y;
                x = rc.left + r * W / r_here; cx = rc.left + (r + 1) * W / r_here - x;
            }
            SetWindowPos(t[i]->h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

/* ---------------------------------------------------------------- create / destroy */

static HWND mdi_create(HWND client, mdiinfo *m, MDICREATESTRUCT *cs)
{
    DWORD style = (DWORD)cs->style;
    wwnd *cw = user_wnd(client);
    if (!(cw && (cw->style & MDIS_ALLCHILDSTYLES)))
        style |= WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE;
    style &= ~(WS_POPUP | WS_MAXIMIZE | WS_MINIMIZE);
    int x = cs->x, y = cs->y, cx = cs->cx, cy = cs->cy;
    RECT rc;
    GetClientRect(client, &rc);
    int d = UM_CAPTION + UM_FRAME, pos = m->count % 6;
    if (x == CW_USEDEFAULT) { x = pos * d; y = pos * d; }
    if (cx == CW_USEDEFAULT) {
        cx = rc.right * 3 / 4;
        cy = rc.bottom * 3 / 4;
    }
    UINT id = m->first + (UINT)m->count;
    m->count++;                     /* the id is in range during creation */
    HWND h = CreateWindowEx(0, cs->szClass, cs->szTitle, style, x, y, cx, cy, client,
                            (HMENU)(uintptr_t)id, cs->hOwner, (LPVOID)cs);
    if (!h) {
        m->count--;
        return 0;
    }
    if (cs->style & WS_MINIMIZE)
        ShowWindow(h, SW_SHOWMINNOACTIVE);
    activate(client, m, h);
    wwnd *a = user_wnd(m->active);
    if (((cs->style & WS_MAXIMIZE) || (a && a != user_wnd(h) && (a->style & WS_MAXIMIZE))) &&
        !(cs->style & WS_MINIMIZE))
        ShowWindow(h, SW_SHOWMAXIMIZED);
    update_menu(client, m);
    update_title(m);
    return h;
}

static void mdi_destroy(HWND client, mdiinfo *m, HWND child)
{
    wwnd *c = user_wnd(child);
    if (!is_child(client, m, c))
        return;
    if (m->active == child) {
        /* the next one in the z-order takes over */
        wwnd *next = NULL;
        wwnd *cl = user_wnd(client);
        for (wwnd *t = cl->child; t; t = t->next)
            if (t != c && is_child(client, m, t) && user_is_visible(t)) {
                next = t;
                break;
            }
        int was_max = (c->style & WS_MAXIMIZE) != 0;
        activate(client, m, next ? next->h : 0);
        if (was_max && next && !(next->style & WS_MINIMIZE))
            ShowWindow(next->h, SW_SHOWMAXIMIZED);
    }
    UINT id = c->id;
    DestroyWindow(child);
    /* renumber the ones above it */
    wwnd *cl = user_wnd(client);
    for (UINT k = id + 1; cl && k < m->first + (UINT)m->count; k++)
        for (wwnd *t = cl->child; t; t = t->next)
            if (t->id == k) {
                t->id = k - 1;
                break;
            }
    m->count--;
    update_menu(client, m);
    update_title(m);
}

static void mdi_next(HWND client, mdiinfo *m, HWND from, int prev)
{
    wwnd *cl = user_wnd(client);
    wwnd *cur = user_wnd(from ? from : m->active);
    if (!cl || !cur)
        return;
    wwnd *pick = NULL;
    if (!prev) {                    /* the one below; the current goes to the bottom */
        for (wwnd *t = cur->next; t && !pick; t = t->next)
            if (is_child(client, m, t) && user_is_visible(t))
                pick = t;
        if (!pick)
            return;
        activate(client, m, pick->h);
        SetWindowPos(cur->h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {                        /* the bottom-most one */
        for (wwnd *t = cl->child; t; t = t->next)
            if (t != cur && is_child(client, m, t) && user_is_visible(t))
                pick = t;
        if (pick)
            activate(client, m, pick->h);
    }
}

/* ---------------------------------------------------------------- MDICLIENT */

LRESULT CALLBACK user_mdiclient_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wwnd *w = user_wnd(h);
    mdiinfo *m = info(h);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)(intptr_t)lp;
        CLIENTCREATESTRUCT *cc = (CLIENTCREATESTRUCT *)cs->lpCreateParams;
        m = (mdiinfo *)win_alloc(sizeof(mdiinfo));
        if (!m || !w->extra)
            return -1;
        memcpy(w->extra, &m, sizeof(m));
        m->frame = w->parent ? w->parent->h : 0;
        if (cc) {
            m->winmenu = cc->hWindowMenu;
            m->first = cc->idFirstChild;
        }
        if (!m->first)
            m->first = 0xFF00;
        char t[96];
        GetWindowText(m->frame, t, sizeof(t));
        m->title = win_strdup(t);
        return 0;
    }
    case WM_DESTROY:
        if (m) {
            win_free(m->title);
            win_free(m);
            memset(w->extra, 0, sizeof(m));
        }
        return 0;
    }
    if (!m)
        return DefWindowProc(h, msg, wp, lp);
    switch (msg) {
    case WM_MDICREATE:
        return (LRESULT)mdi_create(h, m, (MDICREATESTRUCT *)(intptr_t)lp);
    case WM_MDIDESTROY:
        mdi_destroy(h, m, (HWND)wp);
        return 0;
    case WM_MDIACTIVATE: {
        wwnd *c = user_wnd((HWND)wp);
        if (c && (c->style & WS_MINIMIZE) && m->active == (HWND)wp)
            return 0;
        activate(h, m, (HWND)wp);
        return 0;
    }
    case WM_MDINEXT:
        mdi_next(h, m, (HWND)wp, lp != 0);
        return 0;
    case WM_MDIMAXIMIZE:
        ShowWindow((HWND)wp, SW_SHOWMAXIMIZED);
        update_title(m);
        return 0;
    case WM_MDIRESTORE:
        ShowWindow((HWND)wp, SW_RESTORE);
        update_title(m);
        return 0;
    case WM_MDITILE:
        tile(h, m, (UINT)wp);
        return TRUE;
    case WM_MDICASCADE:
        cascade(h, m);
        return TRUE;
    case WM_MDIICONARRANGE:
        icon_arrange(h, m);
        return 0;
    case WM_MDIGETACTIVE: {
        wwnd *a = user_wnd(m->active);
        return (LRESULT)MAKELONG(m->active, a && (a->style & WS_MAXIMIZE) ? 1 : 0);
    }
    case WM_MDISETMENU: {
        HMENU old = GetMenu(m->frame);
        if (HIWORD(lp)) {
            /* the old Window menu loses our items */
            int n = GetMenuItemCount(m->winmenu);
            for (int i = 0; m->winmenu && i < m->menu_items && n > 0; i++)
                DeleteMenu(m->winmenu, (UINT)--n, MF_BYPOSITION);
            m->menu_items = 0;
            m->winmenu = (HMENU)HIWORD(lp);
        }
        if (LOWORD(lp))
            SetMenu(m->frame, (HMENU)LOWORD(lp));
        update_menu(h, m);
        if (wp)
            DrawMenuBar(m->frame);
        return (LRESULT)old;
    }
    case WM_SETFOCUS:
        if (m->active && IsWindow(m->active) && !IsIconic(m->active))
            SetFocus(m->active);
        return 0;
    case WM_SIZE: {
        wwnd *a = user_wnd(m->active);
        if (a && (a->style & WS_MAXIMIZE))
            ShowWindow(m->active, SW_SHOWMAXIMIZED);   /* follow the new size */
        icon_arrange(h, m);
        return 0;
    }
    case WM_PARENTNOTIFY:
        /* a click in a child: it becomes the active one */
        if (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN) {
            POINT p = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
            for (wwnd *t = w->child; t; t = t->next) {
                RECT r = t->rc;
                OffsetRect(&r, -w->client.left, -w->client.top);
                if (is_child(h, m, t) && user_is_visible(t) && PtInRect(&r, p)) {
                    activate(h, m, t->h);
                    break;
                }
            }
        }
        return 0;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, user_sys_brush(COLOR_APPWORKSPACE));
        return 1;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- frame and child defaults */

LRESULT WINAPI DefFrameProc(HWND h, HWND client, UINT msg, WPARAM wp, LPARAM lp)
{
    mdiinfo *m = client ? info(client) : NULL;
    if (m) {
        switch (msg) {
        case WM_COMMAND:
            if (wp >= m->first && wp < m->first + (UINT)m->count && !HIWORD(lp)) {
                wwnd *list[64];
                int n = children(client, m, list, 64);
                for (int i = 0; i < n; i++)
                    if (list[i]->id == wp) {
                        if (list[i]->style & WS_MINIMIZE)
                            ShowWindow(list[i]->h, SW_RESTORE);
                        activate(client, m, list[i]->h);
                        break;
                    }
                return 0;
            }
            break;
        case WM_SETFOCUS:
            SetFocus(client);
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED)
                MoveWindow(client, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_NCACTIVATE: {
            LRESULT r = DefWindowProc(h, msg, wp, lp);
            if (m->active && IsWindow(m->active))
                SendMessage(m->active, WM_NCACTIVATE, wp, 0);
            return r;
        }
        case WM_SETTEXT: {
            win_free(m->title);
            m->title = win_strdup(lp ? (LPCSTR)(intptr_t)lp : "");
            LRESULT r = DefWindowProc(h, msg, wp, lp);
            update_title(m);
            return r;
        }
        }
    }
    return DefWindowProc(h, msg, wp, lp);
}

LRESULT WINAPI DefMDIChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND client = client_of(h);
    mdiinfo *m = client ? info(client) : NULL;
    if (m) {
        switch (msg) {
        case WM_CHILDACTIVATE:
            activate(client, m, h);
            return 0;
        case WM_SETFOCUS:
            if (m->active != h)
                activate(client, m, h);
            break;
        case WM_CLOSE:
            SendMessage(client, WM_MDIDESTROY, h, 0);
            return 0;
        case WM_MDIACTIVATE:
            return 0;
        case WM_SYSCOMMAND:
            switch (wp & 0xFFF0) {
            case SC_NEXTWINDOW:
                SendMessage(client, WM_MDINEXT, h, 0);
                return 0;
            case SC_PREVWINDOW:
                SendMessage(client, WM_MDINEXT, h, 1);
                return 0;
            case SC_MAXIMIZE:
            case SC_RESTORE:
                DefWindowProc(h, msg, wp, lp);
                activate(client, m, h);
                update_title(m);
                return 0;
            case SC_MINIMIZE: {
                DefWindowProc(h, msg, wp, lp);
                icon_arrange(client, m);
                /* the next open child takes over, if there is one */
                if (m->active == h) {
                    wwnd *cl = user_wnd(client);
                    for (wwnd *t = cl ? cl->child : NULL; t; t = t->next)
                        if (t->h != h && is_child(client, m, t) && user_is_visible(t) &&
                            !(t->style & WS_MINIMIZE)) {
                            activate(client, m, t->h);
                            break;
                        }
                }
                update_title(m);
                return 0;
            }
            }
            break;
        case WM_SETTEXT: {
            LRESULT r = DefWindowProc(h, msg, wp, lp);
            update_menu(client, m);
            update_title(m);
            return r;
        }
        case WM_NCACTIVATE:
            /* the caption shows the child as active while the frame is */
            if (wp && (m->active != h || !frame_active(m)))
                wp = FALSE;
            break;
        }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* Ctrl+F4 closes the active child, Ctrl+F6 / Ctrl+Shift+F6 cycle. */
BOOL WINAPI TranslateMDISysAccel(HWND client, LPMSG msg)
{
    mdiinfo *m = info(client);
    if (!m || !m->active || !msg || (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN))
        return FALSE;
    if (GetKeyState(VK_CONTROL) >= 0)
        return FALSE;
    UINT cmd;
    if (msg->wParam == VK_F4)
        cmd = SC_CLOSE;
    else if (msg->wParam == VK_F6)
        cmd = GetKeyState(VK_SHIFT) < 0 ? SC_PREVWINDOW : SC_NEXTWINDOW;
    else
        return FALSE;
    SendMessage(m->active, WM_SYSCOMMAND, cmd, 0);
    return TRUE;
}
