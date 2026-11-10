/*
 * wnd.c - window classes, window tree, geometry, visibility, activation.
 *
 * Window and client rectangles are kept in screen coordinates; moving a
 * window moves its descendants. Siblings are ordered top to bottom. New
 * top-level windows go on top, new children at the bottom (so the first
 * control of a dialog is the first one GetWindow(GW_CHILD) returns, as in
 * Windows).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

wuser U;

/* ---------------------------------------------------------------- classes */

static wclass *classes;

static int class_is(const wclass *c, LPCSTR name)
{
    return IS_INTRESOURCE(name) ? c->atom == (ATOM)(uintptr_t)name : !lstrcmpi(c->name, name);
}

/* Application classes belong to the instance that registered them (each
 * task has its own copy of the program); global classes serve everyone.
 * The calling task's own class wins. */
wclass *user_find_class(LPCSTR name)
{
    if (!name)
        return NULL;
    HTASK me = task_current()->h;
    wclass *global = NULL;
    for (wclass *c = classes; c; c = c->next) {
        if (!class_is(c, name))
            continue;
        if (c->wc.hInstance == me && !(c->wc.style & CS_GLOBALCLASS))
            return c;
        if (!global && ((c->wc.style & CS_GLOBALCLASS) || !c->wc.hInstance))
            global = c;
    }
    return global;
}

/* classes of a finished task */
void user_unregister_task_classes(HTASK task)
{
    for (wclass **pp = &classes; *pp;) {
        wclass *c = *pp;
        if (c->wc.hInstance == task && task) {
            *pp = c->next;
            DeleteAtom(c->atom);
            win_free(c->name);
            win_free(c);
            continue;
        }
        pp = &c->next;
    }
}

ATOM WINAPI RegisterClass(const WNDCLASS *wc)
{
    if (!wc || !wc->lpszClassName || IS_INTRESOURCE(wc->lpszClassName))
        return 0;
    wclass *same = user_find_class(wc->lpszClassName);
    if (same && (same->wc.hInstance == wc->hInstance || (same->wc.style & CS_GLOBALCLASS)))
        return 0;
    wclass *c = (wclass *)win_alloc(sizeof(*c));
    if (!c)
        return 0;
    c->name = win_strdup(wc->lpszClassName);
    if (!c->name) {
        win_free(c);
        return 0;
    }
    c->atom = AddAtom(wc->lpszClassName);
    c->extra = wc->cbClsExtra > 0 ? (uint8_t *)win_alloc((size_t)wc->cbClsExtra) : NULL;
    if (!c->atom || (wc->cbClsExtra > 0 && !c->extra)) {
        if (c->atom)
            DeleteAtom(c->atom);
        win_free(c->name);
        win_free(c);
        return 0;
    }
    c->wc = *wc;
    c->wc.lpszClassName = c->name;
    c->next = classes;
    classes = c;
    return c->atom;
}

void user_register_builtin(LPCSTR name, WNDPROC proc, UINT style, int extra,
                           HCURSOR cur, HBRUSH bg)
{
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = style | CS_GLOBALCLASS;
    wc.lpfnWndProc = proc;
    wc.cbWndExtra = extra;
    wc.hCursor = cur;
    wc.hbrBackground = bg;
    wc.lpszClassName = name;
    RegisterClass(&wc);
}

/* An active window retains w->cls until WM_NCDESTROY finishes. */
static int class_has_window(wwnd *w, const wclass *c)
{
    for (; w; w = w->next) {
        if (w->cls == c || class_has_window(w->child, c))
            return 1;
    }
    return 0;
}

BOOL WINAPI UnregisterClass(LPCSTR name, HINSTANCE inst)
{
    if (!name)
        return FALSE;
    for (wclass **pp = &classes; *pp; pp = &(*pp)->next) {
        wclass *c = *pp;
        if (c->wc.hInstance == inst &&
            (IS_INTRESOURCE(name) ? c->atom == (ATOM)(uintptr_t)name : !lstrcmpi(c->name, name))) {
            if (class_has_window(U.desktop, c))
                return FALSE;
            *pp = c->next;
            DeleteAtom(c->atom);
            win_free(c->name);
            win_free(c->extra);
            win_free(c);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL WINAPI GetClassInfo(HINSTANCE inst, LPCSTR name, LPWNDCLASS wc)
{
    (void)inst;
    wclass *c = user_find_class(name);
    if (!c)
        return FALSE;
    *wc = c->wc;
    return TRUE;
}

int WINAPI GetClassName(HWND h, LPSTR buf, int max)
{
    wwnd *w = user_wnd(h);
    if (!w || max <= 0)
        return 0;
    lstrcpyn(buf, w->cls->name, max);
    return lstrlen(buf);
}

/* the class's extra bytes at index, n of them: NULL if beyond */
static uint8_t *class_extra(wwnd *w, int index, int n)
{
    return w && index >= 0 && w->cls->extra && index + n <= w->cls->wc.cbClsExtra ?
           w->cls->extra + index : NULL;
}

/* a WORD of the class's: its extra bytes, or one of its fields */
static WORD *class_word(wwnd *w, int index)
{
    if (!w)
        return NULL;
    switch (index) {
    case GCW_HBRBACKGROUND: return &w->cls->wc.hbrBackground;
    case GCW_HCURSOR: return &w->cls->wc.hCursor;
    case GCW_HICON: return &w->cls->wc.hIcon;
    case GCW_HMODULE: return &w->cls->wc.hInstance;
    }
    return NULL;
}

LONG WINAPI GetClassLong(HWND h, int index)
{
    wwnd *w = user_wnd(h);
    uint8_t *p = class_extra(w, index, 4);
    if (p)
        return (LONG)(p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
    if (w && index == GCL_WNDPROC)
        return (LONG)(intptr_t)w->cls->wc.lpfnWndProc;
    return 0;
}

LONG WINAPI SetClassLong(HWND h, int index, LONG v)
{
    wwnd *w = user_wnd(h);
    LONG old = GetClassLong(h, index);
    uint8_t *p = class_extra(w, index, 4);
    if (p) {
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    } else if (w && index == GCL_WNDPROC) {     /* the class's windows made from now on */
        w->cls->wc.lpfnWndProc = (WNDPROC)(intptr_t)v;
    } else {
        return 0;
    }
    return old;
}

WORD WINAPI GetClassWord(HWND h, int index)
{
    wwnd *w = user_wnd(h);
    uint8_t *p = class_extra(w, index, 2);
    WORD *f = class_word(w, index);
    if (p)
        return (WORD)(p[0] | p[1] << 8);
    if (f)
        return *f;
    if (!w)
        return 0;
    switch (index) {
    case GCW_STYLE: return (WORD)w->cls->wc.style;
    case GCW_CBWNDEXTRA: return (WORD)w->cls->wc.cbWndExtra;
    case GCW_CBCLSEXTRA: return (WORD)w->cls->wc.cbClsExtra;
    case GCW_ATOM: return w->cls->atom;
    }
    return 0;
}

WORD WINAPI SetClassWord(HWND h, int index, WORD v)
{
    wwnd *w = user_wnd(h);
    WORD old = GetClassWord(h, index);
    uint8_t *p = class_extra(w, index, 2);
    WORD *f = class_word(w, index);
    if (p) {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
    } else if (f) {
        *f = v;
    } else if (w && index == GCW_STYLE) {
        w->cls->wc.style = v;
    } else {
        return 0;
    }
    return old;
}

/* ---------------------------------------------------------------- tree */

wwnd *user_wnd(HWND h)
{
    if (!h)
        return NULL;
    return (wwnd *)win_handle_get(h, WOBJ_WINDOW);
}

wwnd *user_toplevel(wwnd *w)
{
    while (w && w->parent && w->parent != U.desktop && (w->style & WS_CHILD))
        w = w->parent;
    return w;
}

static void unlink_wnd(wwnd *w)
{
    if (!w->parent)
        return;
    for (wwnd **pp = &w->parent->child; *pp; pp = &(*pp)->next)
        if (*pp == w) {
            *pp = w->next;
            break;
        }
    w->next = NULL;
}

static void link_top(wwnd *w)
{
    w->next = w->parent->child;
    w->parent->child = w;
}

static void link_bottom(wwnd *w)
{
    wwnd **pp = &w->parent->child;
    while (*pp)
        pp = &(*pp)->next;
    *pp = w;
    w->next = NULL;
}

static void link_after(wwnd *w, wwnd *after)
{
    w->next = after->next;
    after->next = w;
}

int user_is_visible(const wwnd *w)
{
    for (; w; w = w->parent)
        if (!(w->style & WS_VISIBLE))
            return 0;
    return 1;
}

BOOL WINAPI IsWindow(HWND h) { wwnd *w = user_wnd(h); return w && !w->destroying; }

BOOL WINAPI IsChild(HWND hp, HWND h)
{
    wwnd *p = user_wnd(hp), *w = user_wnd(h);
    if (!p || !w)
        return FALSE;
    while ((w->style & WS_CHILD) && w->parent) {
        w = w->parent;
        if (w == p)
            return TRUE;
    }
    return FALSE;
}

BOOL WINAPI IsWindowVisible(HWND h) { wwnd *w = user_wnd(h); return w && user_is_visible(w); }
BOOL WINAPI IsWindowEnabled(HWND h) { wwnd *w = user_wnd(h); return w && !(w->style & WS_DISABLED); }
HTASK WINAPI GetWindowTask(HWND h) { wwnd *w = user_wnd(h); return w ? w->task : 0; }
BOOL WINAPI IsIconic(HWND h) { wwnd *w = user_wnd(h); return w && (w->style & WS_MINIMIZE); }
BOOL WINAPI IsZoomed(HWND h) { wwnd *w = user_wnd(h); return w && (w->style & WS_MAXIMIZE); }

HWND WINAPI GetDesktopWindow(void) { return U.desktop ? U.desktop->h : 0; }

HWND WINAPI GetParent(HWND h)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    if (w->style & WS_CHILD)
        return w->parent ? w->parent->h : 0;
    return w->owner ? w->owner->h : 0;
}

HWND WINAPI SetParent(HWND h, HWND hp)
{
    wwnd *w = user_wnd(h), *p = hp ? user_wnd(hp) : U.desktop;
    if (!w || !p || w == U.desktop)
        return 0;
    HWND old = w->parent ? w->parent->h : 0;
    int vis = user_is_visible(w);
    if (vis)
        ShowWindow(h, SW_HIDE);
    RECT rc = w->rc;          /* keep screen position */
    unlink_wnd(w);
    w->parent = p;
    link_top(w);
    user_set_window_rect(w, &rc, 0);
    if (vis)
        ShowWindow(h, SW_SHOWNA);
    return old;
}

HWND WINAPI GetWindow(HWND h, UINT cmd)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    wwnd *r = NULL;
    switch (cmd) {
    case GW_CHILD: r = w->child; break;
    case GW_HWNDNEXT: r = w->next; break;
    case GW_HWNDFIRST: r = w->parent ? w->parent->child : NULL; break;
    case GW_HWNDLAST:
        r = w->parent ? w->parent->child : NULL;
        while (r && r->next)
            r = r->next;
        break;
    case GW_HWNDPREV:
        if (w->parent)
            for (wwnd *c = w->parent->child; c && c != w; c = c->next)
                r = c;
        break;
    case GW_OWNER: r = w->owner; break;
    }
    return r ? r->h : 0;
}

HWND WINAPI GetTopWindow(HWND h)
{
    wwnd *w = h ? user_wnd(h) : U.desktop;
    return w && w->child ? w->child->h : 0;
}

static int text_match(const char *a, const char *b) { return !lstrcmp(a ? a : "", b ? b : ""); }

HWND WINAPI FindWindow(LPCSTR klass, LPCSTR title)
{
    wclass *c = klass ? user_find_class(klass) : NULL;
    if (klass && !c)
        return 0;
    for (wwnd *w = U.desktop->child; w; w = w->next) {
        if (c && w->cls != c)
            continue;
        if (title && !text_match(w->text, title))
            continue;
        return w->h;
    }
    return 0;
}

HWND WINAPI GetNextWindow(HWND h, UINT cmd) { return GetWindow(h, cmd); }

/* a visible top-level window that has an owner or is a pop-up */
BOOL WINAPI AnyPopup(void)
{
    for (wwnd *w = U.desktop->child; w; w = w->next)
        if ((w->style & WS_VISIBLE) && (w->owner || (w->style & WS_POPUP)))
            return TRUE;
    return FALSE;
}

/* the caption the other way (invert), or as it is; how it was drawn */
BOOL WINAPI FlashWindow(HWND h, BOOL invert)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return FALSE;
    BOOL was = w->active_frame;
    SendMessage(h, WM_NCACTIVATE, invert ? !was : user_toplevel(w) == U.active, 0);
    return was;
}

BOOL WINAPI CloseWindow(HWND h)
{
    wwnd *w = user_wnd(h);
    if (!w || (w->style & WS_CHILD))
        return FALSE;
    ShowWindow(h, SW_MINIMIZE);
    return TRUE;
}

BOOL WINAPI OpenIcon(HWND h)
{
    if (!IsWindow(h))
        return FALSE;
    if (IsIconic(h))
        ShowWindow(h, SW_SHOWNORMAL);
    return TRUE;
}

/* its pop-ups (top-level windows it owns) shown or hidden */
void WINAPI ShowOwnedPopups(HWND h, BOOL show)
{
    wwnd *o = user_wnd(h);
    for (wwnd *w = U.desktop->child, *n; o && w; w = n) {
        n = w->next;
        if (w->owner == o && (w->style & WS_POPUP) && !(w->style & WS_VISIBLE) == !!show)
            ShowWindow(w->h, show ? SW_SHOWNA : SW_HIDE);
    }
}

/* the window it owns that was active last: the active one, if it is */
HWND WINAPI GetLastActivePopup(HWND h)
{
    for (wwnd *w = U.active ? U.active->owner : NULL; w; w = w->owner)
        if (w->h == h)
            return U.active->h;
    return h;
}

/* drawing is not held back: the window paints as it goes */
BOOL WINAPI LockWindowUpdate(HWND h) { (void)h; return TRUE; }

/* the positions of several windows at once: each set as it comes */
HDWP WINAPI BeginDeferWindowPos(int n) { (void)n; return 1; }

HDWP WINAPI DeferWindowPos(HDWP d, HWND h, HWND after, int x, int y, int cx, int cy,
                           UINT flags)
{
    return d && SetWindowPos(h, after, x, y, cx, cy, flags) ? d : 0;
}

BOOL WINAPI EndDeferWindowPos(HDWP d) { return d != 0; }

/* the system-modal window: noted, not enforced */
static HWND sys_modal;

HWND WINAPI SetSysModalWindow(HWND h)
{
    HWND old = GetSysModalWindow();
    sys_modal = h;
    return old;
}

HWND WINAPI GetSysModalWindow(void) { return IsWindow(sys_modal) ? sys_modal : 0; }

/* no Help program: HELP_QUIT (nothing to end) is all that succeeds */
BOOL WINAPI WinHelp(HWND h, LPCSTR file, UINT cmd, DWORD data)
{
    (void)h; (void)file; (void)data;
    return cmd == HELP_QUIT;
}

BOOL WINAPI EnumWindows(WNDENUMPROC proc, LPARAM lp)
{
    for (wwnd *w = U.desktop->child, *n; w; w = n) {
        n = w->next;
        if (!proc(w->h, lp))
            return FALSE;
    }
    return TRUE;
}

static BOOL enum_children(wwnd *p, WNDENUMPROC proc, LPARAM lp)
{
    for (wwnd *w = p->child, *n; w; w = n) {
        n = w->next;
        HWND h = w->h;
        if (!proc(h, lp))
            return FALSE;
        if (user_wnd(h) && !enum_children(w, proc, lp))
            return FALSE;
    }
    return TRUE;
}

BOOL WINAPI EnumChildWindows(HWND hp, WNDENUMPROC proc, LPARAM lp)
{
    wwnd *p = user_wnd(hp);
    return p ? enum_children(p, proc, lp) : FALSE;
}

/* ---------------------------------------------------------------- non-client geometry */

int user_has_caption(DWORD style) { return (style & WS_CAPTION) == WS_CAPTION; }

int user_frame_width(DWORD style, DWORD exstyle)
{
    if (style & WS_THICKFRAME)
        return UM_FRAME;
    if ((exstyle & WS_EX_DLGMODALFRAME) || ((style & WS_DLGFRAME) && !(style & WS_BORDER)))
        return UM_FRAME;
    if (style & (WS_BORDER | WS_DLGFRAME))
        return UM_BORDER;
    return 0;
}

static int own_icon(const wwnd *w) { return w->cls && !w->cls->wc.hIcon; }

/* own_icon: the class has no icon, so a minimized window draws its icon
 * itself: its client area is then the icon's place (as in Windows 3.x,
 * where the Clock keeps going as an icon) */
static void nc_calc(DWORD style, DWORD ex, int menu_h, int self_icon, const RECT *win, RECT *cl)
{
    *cl = *win;
    if (style & WS_MINIMIZE) {
        if (self_icon) {
            cl->left = win->left + (win->right - win->left - UM_ICON) / 2;
            cl->top = win->top + 2;
            cl->right = cl->left + UM_ICON;
            cl->bottom = cl->top + UM_ICON;
        } else {
            cl->right = cl->left;
            cl->bottom = cl->top;
        }
        return;
    }
    int f = user_frame_width(style, ex);
    InflateRect(cl, -f, -f);
    if (user_has_caption(style))
        cl->top += UM_CAPTION + 1;
    cl->top += menu_h;
    if (style & WS_VSCROLL)
        cl->right -= UM_SCROLL - 1;
    if (style & WS_HSCROLL)
        cl->bottom -= UM_SCROLL - 1;
    if (cl->right < cl->left) cl->right = cl->left;
    if (cl->bottom < cl->top) cl->bottom = cl->top;
}

void user_nc_rects(const wwnd *w, RECT *client)
{
    nc_calc(w->style, w->exstyle, user_menu_bar_height(w), own_icon(w), &w->rc, client);
}

void WINAPI AdjustWindowRectEx(LPRECT rc, DWORD style, BOOL menu, DWORD ex)
{
    if (menu && !(style & WS_CHILD))
        rc->top -= UM_MENU + 1;
    int f = user_frame_width(style, ex);
    InflateRect(rc, f, f);
    if (user_has_caption(style))
        rc->top -= UM_CAPTION + 1;
    if (style & WS_VSCROLL)
        rc->right += UM_SCROLL - 1;
    if (style & WS_HSCROLL)
        rc->bottom += UM_SCROLL - 1;
}

void WINAPI AdjustWindowRect(LPRECT rc, DWORD style, BOOL menu)
{
    AdjustWindowRectEx(rc, style, menu, 0);
}

void WINAPI GetWindowRect(HWND h, LPRECT rc)
{
    wwnd *w = user_wnd(h);
    if (w)
        *rc = w->rc;
    else
        SetRectEmpty(rc);
}

void WINAPI GetClientRect(HWND h, LPRECT rc)
{
    wwnd *w = user_wnd(h);
    SetRectEmpty(rc);
    if (w) {
        rc->right = w->client.right - w->client.left;
        rc->bottom = w->client.bottom - w->client.top;
    }
}

void WINAPI ClientToScreen(HWND h, LPPOINT pt)
{
    wwnd *w = user_wnd(h);
    if (w) {
        pt->x += w->client.left;
        pt->y += w->client.top;
    }
}

void WINAPI ScreenToClient(HWND h, LPPOINT pt)
{
    wwnd *w = user_wnd(h);
    if (w) {
        pt->x -= w->client.left;
        pt->y -= w->client.top;
    }
}

void WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT pts, UINT n)
{
    wwnd *a = user_wnd(from), *b = user_wnd(to);
    int dx = (a ? a->client.left : 0) - (b ? b->client.left : 0);
    int dy = (a ? a->client.top : 0) - (b ? b->client.top : 0);
    for (UINT i = 0; i < n; i++) {
        pts[i].x += dx;
        pts[i].y += dy;
    }
}

/* ---------------------------------------------------------------- visibility */

static int clips_siblings(const wwnd *w)
{
    return !(w->style & WS_CHILD) || (w->style & WS_CLIPSIBLINGS);
}

/* A minimized icon has no rectangular background: only a self-painted
 * 32x32 client (e.g. Clock) must be protected against painting below it.
 * Ordinary icons are redrawn over the updated pixels after each GDI draw. */
static void subtract_window_coverage(wrgn *out, const wwnd *w)
{
    if (w->style & WS_MINIMIZE) {
        if (w->cls && !w->cls->wc.hIcon)
            wrgn_diff_rect(out, &w->client);
    } else {
        wrgn_diff_rect(out, &w->rc);
    }
}

/* Visible part of a window (or its client area) in screen coordinates. */
void user_vis_region(const wwnd *w, int client_only, wrgn *out)
{
    wrgn_clear(out);
    if (!user_is_visible(w))
        return;
    RECT r = client_only ? w->client : w->rc;
    wrgn_set_rect(out, &r);
    if (client_only && (w->style & WS_CLIPCHILDREN || w == U.desktop))
        for (const wwnd *c = w->child; c; c = c->next)
            if (c->style & WS_VISIBLE)
                subtract_window_coverage(out, c);
    for (const wwnd *cur = w; cur->parent && out->n; cur = cur->parent) {
        const wwnd *p = cur->parent;
        if (p != U.desktop)
            wrgn_and_rect(out, &p->client);
        if (clips_siblings(cur))
            for (const wwnd *s = p->child; s && s != cur; s = s->next)
                if (s->style & WS_VISIBLE)
                    subtract_window_coverage(out, s);
    }
}

static void add_update(wwnd *w, const wrgn *part, int erase)
{
    if (!part->n)
        return;
    wrgn_or(&w->update, &w->update, part);
    if (erase)
        w->erase = 1;
    /* does it reach outside the client area? */
    for (int i = 0; i < part->n && !w->ncpaint; i++) {
        RECT x;
        if (!IntersectRect(&x, &part->r[i], &w->client) || !EqualRect(&x, &part->r[i]))
            w->ncpaint = 1;
    }
}

static void inval_walk(wwnd *w, const wrgn *r, wrgn *tmp, wrgn *part)
{
    for (; w; w = w->next) {
        if (!(w->style & WS_VISIBLE))
            continue;
        RECT x;
        if (!IntersectRect(&x, &w->rc, &r->box))
            continue;
        user_vis_region(w, 0, tmp);
        wrgn_and(part, tmp, r);
        add_update(w, part, 1);
        inval_walk(w->child, r, tmp, part);
    }
}

/* Screen area exposed: everything visible there repaints. */
void user_invalidate_screen(const wrgn *r)
{
    if (U.shutting_down || !r->n || !U.desktop)
        return;
    wrgn tmp, part;
    wrgn_init(&tmp);
    wrgn_init(&part);
    user_vis_region(U.desktop, 0, &tmp);
    wrgn_and(&part, &tmp, r);
    add_update(U.desktop, &part, 1);
    inval_walk(U.desktop->child, r, &tmp, &part);
    wrgn_free(&tmp);
    wrgn_free(&part);
}

/* r in screen coordinates, already limited to what the caller wants. */
void user_invalidate(wwnd *w, const wrgn *r, int erase, int children)
{
    if (U.shutting_down)
        return;
    wrgn vis, part;
    wrgn_init(&vis);
    wrgn_init(&part);
    user_vis_region(w, 0, &vis);
    wrgn_and(&part, &vis, r);
    add_update(w, &part, erase);
    if (children || !(w->style & WS_CLIPCHILDREN))
        for (wwnd *c = w->child; c; c = c->next)
            if (c->style & WS_VISIBLE)
                user_invalidate(c, r, erase, children);
    wrgn_free(&vis);
    wrgn_free(&part);
}

void user_validate(wwnd *w, const wrgn *r)
{
    if (!r) {
        wrgn_clear(&w->update);
        w->erase = 0;
        w->ncpaint = 0;
        return;
    }
    wrgn_diff(&w->update, &w->update, r);
    if (!w->update.n)
        w->erase = 0;
}

/* ---------------------------------------------------------------- hit testing */

static wwnd *child_at(wwnd *p, int x, int y, wwnd *skip)
{
    POINT pt = { x, y };
    for (wwnd *c = p->child; c; c = c->next) {
        if (!(c->style & WS_VISIBLE) || c == skip || !PtInRect(&c->rc, pt))
            continue;
        return c;
    }
    return NULL;
}

/* Deepest visible window at a screen point; *hit gets its NCHITTEST.
 * HTTRANSPARENT windows (group boxes) pass the point to the next sibling. */
wwnd *user_window_from_point(int x, int y, int *hit)
{
    wwnd *p = U.desktop, *found = U.desktop;
    POINT pt = { x, y };
    wwnd *skip = NULL;
    for (;;) {
        wwnd *c = child_at(p, x, y, skip);
        if (!c)
            break;
        if (c->style & WS_DISABLED) {
            found = c;
            *hit = HTERROR;
            return found;
        }
        found = c;
        if (!PtInRect(&c->client, pt) || (c->style & WS_MINIMIZE))
            break;
        p = c;
        skip = NULL;
    }
    int ht = (int)SendMessage(found->h, WM_NCHITTEST, 0, MAKELPARAM(x, y));
    if (ht == HTTRANSPARENT && found->parent) {
        /* look again among the siblings below it */
        wwnd *par = found->parent;
        for (wwnd *s = found->next; s; s = s->next) {
            if ((s->style & WS_VISIBLE) && PtInRect(&s->rc, pt)) {
                found = s;
                ht = (int)SendMessage(s->h, WM_NCHITTEST, 0, MAKELPARAM(x, y));
                if (ht != HTTRANSPARENT)
                    break;
            }
        }
        if (ht == HTTRANSPARENT) {
            found = par;
            ht = (int)SendMessage(par->h, WM_NCHITTEST, 0, MAKELPARAM(x, y));
        }
    }
    *hit = ht;
    return found;
}

HWND WINAPI WindowFromPoint(POINT pt)
{
    int hit;
    wwnd *w = user_window_from_point(pt.x, pt.y, &hit);
    return w ? w->h : 0;
}

HWND WINAPI ChildWindowFromPoint(HWND hp, POINT pt)
{
    wwnd *p = user_wnd(hp);
    if (!p)
        return 0;
    POINT s = { pt.x + p->client.left, pt.y + p->client.top };
    if (!PtInRect(&p->rc, s))
        return 0;
    for (wwnd *c = p->child; c; c = c->next)
        if (PtInRect(&c->rc, s))
            return c->h;
    return hp;
}

/* ---------------------------------------------------------------- geometry changes */

static void offset_tree(wwnd *w, int dx, int dy)
{
    for (wwnd *c = w->child; c; c = c->next) {
        OffsetRect(&c->rc, dx, dy);
        OffsetRect(&c->client, dx, dy);
        wrgn_offset(&c->update, dx, dy);
        offset_tree(c, dx, dy);
    }
}

/* New window rectangle in screen coordinates; client follows. */
void user_set_window_rect(wwnd *w, const RECT *rc, UINT flags)
{
    (void)flags;
    RECT oc = w->client;
    w->rc = *rc;
    nc_calc(w->style, w->exstyle, user_menu_bar_height(w), own_icon(w), &w->rc, &w->client);
    int dx = w->client.left - oc.left, dy = w->client.top - oc.top;
    if (dx || dy)
        offset_tree(w, dx, dy);
    wrgn_clear(&w->update);
}

static void rect_to_parent(const wwnd *w, RECT *r)
{
    if (w->parent && w->parent != U.desktop)
        OffsetRect(r, -w->parent->client.left, -w->parent->client.top);
}

BOOL WINAPI SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    wwnd *w = user_wnd(h);
    if (!w || w == U.desktop)
        return FALSE;
    RECT cur = w->rc;
    rect_to_parent(w, &cur);
    if (flags & SWP_NOMOVE) { x = cur.left; y = cur.top; }
    if (flags & SWP_NOSIZE) { cx = cur.right - cur.left; cy = cur.bottom - cur.top; }
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;

    WINDOWPOS wp = { h, after, x, y, cx, cy, flags };
    SendMessage(h, WM_WINDOWPOSCHANGING, 0, (LPARAM)(intptr_t)&wp);
    x = wp.x; y = wp.y; cx = wp.cx; cy = wp.cy;

    int was_vis = user_is_visible(w);
    wrgn old;
    wrgn_init(&old);
    if (was_vis)
        user_vis_region(w, 0, &old);

    /* geometry */
    RECT nr = { x, y, x + cx, y + cy };
    if (w->parent && w->parent != U.desktop)
        OffsetRect(&nr, w->parent->client.left, w->parent->client.top);
    int moved = nr.left != w->rc.left || nr.top != w->rc.top;
    int sized = (nr.right - nr.left) != (w->rc.right - w->rc.left) ||
                (nr.bottom - nr.top) != (w->rc.bottom - w->rc.top);
    int geo = moved || sized || (flags & SWP_FRAMECHANGED);
    if (geo)
        user_set_window_rect(w, &nr, flags);

    /* z-order */
    int zchg = 0;
    if (!(flags & SWP_NOZORDER) && w->parent) {
        wwnd *a = NULL;
        if (after == HWND_BOTTOM) {
            unlink_wnd(w);
            link_bottom(w);
            zchg = 1;
        } else if (after == HWND_TOP || after == HWND_TOPMOST || after == HWND_NOTOPMOST) {
            if (w->parent->child != w) {
                unlink_wnd(w);
                link_top(w);
                zchg = 1;
            }
        } else if ((a = user_wnd(after)) && a->parent == w->parent && a != w) {
            unlink_wnd(w);
            link_after(w, a);
            zchg = 1;
        }
    }

    if (flags & SWP_SHOWWINDOW)
        w->style |= WS_VISIBLE;
    if (flags & SWP_HIDEWINDOW)
        w->style &= ~WS_VISIBLE;
    int now_vis = user_is_visible(w);

    if (!(flags & SWP_NOREDRAW) && (geo || zchg || was_vis != now_vis)) {
        wrgn nw;
        wrgn_init(&nw);
        if (now_vis)
            user_vis_region(w, 0, &nw);
        /* others: what w no longer covers */
        wrgn exposed;
        wrgn_init(&exposed);
        wrgn_diff(&exposed, &old, &nw);
        /* Moving into minimized form uncovers the entire old window,
         * including the area inside the new icon's transparent bounds. */
        if (now_vis && (w->style & WS_MINIMIZE))
            wrgn_or(&exposed, &exposed, &nw);
        user_invalidate_screen(&exposed);
        /* w itself: everything after a move/size, newly uncovered otherwise */
        if (now_vis) {
            wrgn mine;
            wrgn_init(&mine);
            if (geo || !was_vis) {
                RECT all = w->rc;
                wrgn_set_rect(&mine, &all);
                user_invalidate(w, &mine, 1, 1);
            } else {
                wrgn_diff(&mine, &nw, &old);
                user_invalidate(w, &mine, 1, 1);
            }
            wrgn_free(&mine);
        }
        wrgn_free(&exposed);
        wrgn_free(&nw);
    }
    wrgn_free(&old);

    if (geo) {
        RECT c = w->client;
        rect_to_parent(w, &c);
        if (moved)
            SendMessage(h, WM_MOVE, 0, MAKELPARAM(c.left, c.top));
        if (sized || (flags & SWP_FRAMECHANGED)) {
            WPARAM t = (w->style & WS_MAXIMIZE) ? SIZE_MAXIMIZED :
                       (w->style & WS_MINIMIZE) ? SIZE_MINIMIZED : SIZE_RESTORED;
            SendMessage(h, WM_SIZE, t, MAKELPARAM(c.right - c.left, c.bottom - c.top));
        }
    }
    wp.x = x; wp.y = y; wp.cx = cx; wp.cy = cy;
    SendMessage(h, WM_WINDOWPOSCHANGED, 0, (LPARAM)(intptr_t)&wp);
    return TRUE;
}

BOOL WINAPI MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint)
{
    return SetWindowPos(h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE |
                        (repaint ? 0 : SWP_NOREDRAW));
}

/* Owned windows stay above their owner. */
void user_bring_to_top(wwnd *w)
{
    if (!w || !w->parent)
        return;
    SetWindowPos(w->h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (w->style & WS_CHILD)
        return;
    for (int again = 1; again;) {
        again = 0;
        for (wwnd *o = U.desktop->child; o; o = o->next) {
            if (o->owner != w || o == U.desktop->child)
                continue;
            /* o must be above w */
            int above = 0;
            for (wwnd *s = U.desktop->child; s && s != w; s = s->next)
                if (s == o)
                    above = 1;
            if (!above) {
                SetWindowPos(o->h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                again = 1;
                break;
            }
        }
    }
}

/* The unowned top-level window a window belongs with (itself, or the owner
 * of its owner ...): what the switching lists show. */
wwnd *user_root_owner(wwnd *w)
{
    while (w && w->owner)
        w = w->owner;
    return w;
}

/* The window that takes the activation for w: w, or while a modal dialog
 * (a MessageBox) has disabled it, that dialog, or the one that disabled
 * the dialog in turn (Windows: GetLastActivePopup). */
wwnd *user_enabled_popup(wwnd *w)
{
    while (w && (w->style & WS_DISABLED)) {
        wwnd *p = NULL;
        for (wwnd *t = U.desktop->child; t && !p; t = t->next)
            if (t->owner == w && user_is_visible(t))
                p = t;
        if (!p)
            break;
        w = p;
    }
    return w;
}

/* Switching to an application (Alt+Tab, Alt+Esc, a click on its disabled
 * window): its window comes up with the windows it owns over it, the
 * dialog it waits for, if any, is activated, and a minimized one is
 * restored. */
void user_switch_to(wwnd *w)
{
    w = user_root_owner(w);
    if (!w || !IsWindow(w->h))
        return;
    user_bring_to_top(w);
    SetActiveWindow(user_enabled_popup(w)->h);
    if ((w->style & WS_MINIMIZE) && IsWindow(w->h))
        SendMessage(w->h, WM_SYSCOMMAND, SC_RESTORE, 0);
}

BOOL WINAPI BringWindowToTop(HWND h)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return FALSE;
    if (!(w->style & WS_CHILD))
        SetActiveWindow(h);
    else
        user_bring_to_top(w);
    return TRUE;
}

/* ---------------------------------------------------------------- activation and focus */

HWND WINAPI GetActiveWindow(void) { return U.active ? U.active->h : 0; }
HWND WINAPI GetFocus(void) { return U.focus ? U.focus->h : 0; }

static int can_activate(wwnd *w)
{
    return w && w != U.desktop && !(w->style & WS_CHILD) && user_is_visible(w) &&
           !(w->style & WS_DISABLED);
}

HWND WINAPI SetActiveWindow(HWND h)
{
    wwnd *w = user_wnd(h);
    HWND old = U.active ? U.active->h : 0;
    if (w)
        w = user_toplevel(w);
    if (w == U.active) {
        if (w)
            user_bring_to_top(w);
        return old;
    }
    wwnd *o = U.active;
    if (o) {
        SendMessage(o->h, WM_NCACTIVATE, FALSE, 0);
        SendMessage(o->h, WM_ACTIVATE, WA_INACTIVE, MAKELPARAM(w ? w->h : 0, 0));
    }
    U.active = w;
    if (U.focus && (!w || user_toplevel(U.focus) != w)) {
        wwnd *f = U.focus;
        U.focus = NULL;
        SendMessage(f->h, WM_KILLFOCUS, 0, 0);
    }
    if (w) {
        user_bring_to_top(w);
        SendMessage(w->h, WM_NCACTIVATE, TRUE, 0);
        SendMessage(w->h, WM_ACTIVATE, WA_ACTIVE, MAKELPARAM(old, (w->style & WS_MINIMIZE) ? 1 : 0));
    }
    return old;
}

/* After the active window went away: next visible top-level window. */
static void activate_other(wwnd *gone)
{
    wwnd *cand = NULL;
    if (gone && gone->owner && can_activate(gone->owner))
        cand = gone->owner;
    /* an open window first; when only icons are left, a window just
     * minimized stays active as its icon */
    for (wwnd *t = U.desktop->child; !cand && t; t = t->next)
        if (t != gone && can_activate(t) && !(t->style & WS_MINIMIZE))
            cand = t;
    int alive = gone && !gone->destroying && IsWindow(gone->h);
    if (!cand && alive && can_activate(gone))
        return;
    for (wwnd *t = U.desktop->child; !cand && t; t = t->next)
        if (t != gone && can_activate(t))
            cand = t;
    /* a window still there hears that it lost the activation */
    if (U.active == gone && !alive)
        U.active = NULL;
    SetActiveWindow(cand ? cand->h : 0);
}

HWND WINAPI SetFocus(HWND h)
{
    wwnd *w = user_wnd(h);
    HWND old = U.focus ? U.focus->h : 0;
    if (w == U.focus)
        return old;
    if (w) {
        wwnd *top = user_toplevel(w);
        if (top != U.active)
            SetActiveWindow(top->h);
        if (U.focus == w)
            return old;
    }
    wwnd *o = U.focus;
    U.focus = w;
    if (o && IsWindow(o->h))
        SendMessage(o->h, WM_KILLFOCUS, w ? w->h : 0, 0);
    if (w && U.focus == w)
        SendMessage(w->h, WM_SETFOCUS, old, 0);
    if (w && U.focus == w)
        user_dlg_focus_changed(w);
    return old;
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return FALSE;
    BOOL was_disabled = (w->style & WS_DISABLED) != 0;
    if (enable && was_disabled) {
        w->style &= ~WS_DISABLED;
        SendMessage(h, WM_ENABLE, TRUE, 0);
    } else if (!enable && !was_disabled) {
        SendMessage(h, WM_CANCELMODE, 0, 0);
        w->style |= WS_DISABLED;
        if (U.focus && (U.focus == w || IsChild(h, U.focus->h)))
            SetFocus(0);
        if (U.capture == w)
            ReleaseCapture();
        SendMessage(h, WM_ENABLE, FALSE, 0);
    }
    return was_disabled;
}

HWND WINAPI SetCapture(HWND h)
{
    HWND old = U.capture ? U.capture->h : 0;
    U.capture = user_wnd(h);
    return old;
}

void WINAPI ReleaseCapture(void) { U.capture = NULL; }
HWND WINAPI GetCapture(void) { return U.capture ? U.capture->h : 0; }

/* ---------------------------------------------------------------- show */

/* Width of a minimized window: the standard one, or wider when a word of
 * the title would not fit (titles wrap at spaces, never inside a word). */
static int icon_width(wwnd *w)
{
    int width = UM_ICONW;
    const char *t = w->text;
    if (!t)
        return width;
    HDC dc = GetDC(0);
    while (*t) {
        while (*t == ' ')
            t++;
        int n = 0;
        while (t[n] && t[n] != ' ')
            n++;
        if (n) {
            SIZE sz;
            GetTextExtentPoint(dc, t, n, &sz);
            if (sz.cx + 4 > width)
                width = sz.cx + 4;
        }
        t += n;
    }
    ReleaseDC(0, dc);
    return width > 3 * UM_ICONW ? 3 * UM_ICONW : width;
}

/* Where a window being minimized goes: the first place in the rows at the
 * bottom of the screen (of the parent's client area for a child) where it
 * covers no other icon. Icons only overlap when moved there by hand. */
void user_icon_slot(wwnd *w, RECT *r)
{
    wwnd *p = (w->style & WS_CHILD) && w->parent ? w->parent : U.desktop;
    int ox = 0, oy = 0;
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    if (p != U.desktop) {
        ox = p->client.left;
        oy = p->client.top;
        sw = p->client.right - p->client.left;
        sh = p->client.bottom - p->client.top;
    }
    int width = icon_width(w);
    int dx = UM_ICONW + UM_ICONGAP, dy = UM_ICONH + UM_ICONGAP;
    int per_row = (sw - UM_ICONGAP) / dx;
    if (per_row < 1)
        per_row = 1;
    for (int k = 0; k < 256; k++) {
        /* centred on its place in the grid, kept inside the area */
        int cx = UM_ICONGAP + (k % per_row) * dx + UM_ICONW / 2;
        r->left = cx - width / 2;
        if (r->left < UM_ICONGAP / 2)
            r->left = UM_ICONGAP / 2;
        if (r->left + width > sw)
            r->left = sw - width;
        r->top = sh - dy - (k / per_row) * dy;
        r->right = r->left + width;
        r->bottom = r->top + UM_ICONH;
        int taken = 0;
        for (wwnd *t = p->child; t && !taken; t = t->next) {
            if (t == w || !(t->style & WS_MINIMIZE) || !(t->style & WS_VISIBLE))
                continue;
            RECT o = t->rc, x;
            OffsetRect(&o, -ox, -oy);
            InflateRect(&o, UM_ICONGAP / 2, UM_ICONGAP / 2);
            taken = IntersectRect(&x, &o, r);
        }
        if (!taken)
            return;
    }
}

BOOL WINAPI ShowWindow(HWND h, int cmd)
{
    wwnd *w = user_wnd(h);
    if (!w || w == U.desktop)
        return FALSE;
    BOOL was = (w->style & WS_VISIBLE) != 0;
    UINT f = SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE;
    int activate = 0;
    switch (cmd) {
    case SW_HIDE:
        if (!was)
            return FALSE;
        SendMessage(h, WM_SHOWWINDOW, FALSE, 0);
        SetWindowPos(h, 0, 0, 0, 0, 0, f | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        if (U.focus && (U.focus == w || IsChild(h, U.focus->h)))
            SetFocus(w->parent && w->parent != U.desktop ? w->parent->h : 0);
        if (U.active == w)
            activate_other(w);
        return was;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:
    case SW_SHOWMINNOACTIVE:
        if (!(w->style & WS_MINIMIZE)) {
            if (!(w->style & WS_MAXIMIZE)) {
                w->normal = w->rc;
                rect_to_parent(w, &w->normal);
            }
            RECT r;
            user_icon_slot(w, &r);
            w->style |= WS_MINIMIZE;
            w->style &= ~WS_MAXIMIZE;
            SetWindowPos(h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (!was)
            SendMessage(h, WM_SHOWWINDOW, TRUE, 0);
        SetWindowPos(h, 0, 0, 0, 0, 0, f | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (U.active == w && cmd == SW_MINIMIZE)
            activate_other(w);
        return was;
    case SW_SHOWMAXIMIZED: {
        if (!(w->style & (WS_MAXIMIZE | WS_MINIMIZE))) {
            w->normal = w->rc;
            rect_to_parent(w, &w->normal);
        }
        w->style |= WS_MAXIMIZE;
        w->style &= ~WS_MINIMIZE;
        RECT r = { 0, 0, win_sys.screen.width, win_sys.screen.height };
        if (w->style & WS_CHILD)
            GetClientRect(w->parent->h, &r);
        int fw = user_frame_width(w->style, w->exstyle);
        InflateRect(&r, fw, fw);
        MINMAXINFO mm;
        memset(&mm, 0, sizeof(mm));
        mm.ptMaxSize.x = r.right - r.left; mm.ptMaxSize.y = r.bottom - r.top;
        mm.ptMaxPosition.x = r.left; mm.ptMaxPosition.y = r.top;
        mm.ptMaxTrackSize = mm.ptMaxSize;
        SendMessage(h, WM_GETMINMAXINFO, 0, (LPARAM)(intptr_t)&mm);
        if (!was)
            SendMessage(h, WM_SHOWWINDOW, TRUE, 0);
        SetWindowPos(h, 0, mm.ptMaxPosition.x, mm.ptMaxPosition.y, mm.ptMaxSize.x, mm.ptMaxSize.y,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        activate = 1;
        break;
    }
    case SW_RESTORE:
    case SW_SHOWNORMAL:
        if (w->style & (WS_MAXIMIZE | WS_MINIMIZE)) {
            w->style &= ~(WS_MAXIMIZE | WS_MINIMIZE);
            RECT r = w->normal;
            if (!was)
                SendMessage(h, WM_SHOWWINDOW, TRUE, 0);
            SetWindowPos(h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            activate = 1;
            break;
        }
        /* fall through */
    case SW_SHOW:
    case SW_SHOWNOACTIVATE:
    case SW_SHOWNA:
        if (!was) {
            SendMessage(h, WM_SHOWWINDOW, TRUE, 0);
            SetWindowPos(h, 0, 0, 0, 0, 0, f | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        activate = cmd == SW_SHOW || cmd == SW_SHOWNORMAL || cmd == SW_RESTORE;
        break;
    default:
        return was;
    }
    if (activate && !(w->style & WS_CHILD))
        SetActiveWindow(h);
    return was;
}

/* ---------------------------------------------------------------- create / destroy */

static int cascade;

HWND WINAPI CreateWindowEx(DWORD ex, LPCSTR klass, LPCSTR title, DWORD style,
                           int x, int y, int cx, int cy, HWND hparent, HMENU menu,
                           HINSTANCE inst, LPVOID param)
{
    wclass *c = user_find_class(klass);
    if (!c)
        return 0;
    wwnd *parent = NULL, *owner = NULL;
    if (style & WS_CHILD) {
        parent = user_wnd(hparent);
        if (!parent)
            return 0;
    } else {
        parent = U.desktop;
        owner = hparent ? user_toplevel(user_wnd(hparent)) : NULL;
    }

    wwnd *w = (wwnd *)win_alloc(sizeof(*w));
    if (!w)
        return 0;
    HWND h = win_handle_alloc(WOBJ_WINDOW, w);
    if (!h) {
        win_free(w);
        return 0;
    }
    w->h = h;
    w->cls = c;
    w->proc = c->wc.lpfnWndProc;
    w->exstyle = ex;
    w->parent = parent;
    w->owner = owner;
    w->inst = inst;
    w->id = (style & WS_CHILD) ? (UINT)menu : 0;
    w->menu = (style & WS_CHILD) ? 0 : menu;
    w->task = (style & WS_CHILD) ? parent->task : task_current()->h;
    w->cb_extra = c->wc.cbWndExtra;
    if (w->cb_extra > 0) {
        w->extra = (uint8_t *)win_alloc((size_t)w->cb_extra);
        if (!w->extra) {
            win_handle_free(h);
            win_free(w);
            return 0;
        }
    }
    wrgn_init(&w->update);
    w->vs.max = w->hs.max = 100;

    if (!(style & (WS_CHILD | WS_POPUP)))
        style |= WS_CAPTION | WS_CLIPSIBLINGS;    /* overlapped windows */
    DWORD want_vis = style & WS_VISIBLE;
    w->style = style & ~WS_VISIBLE;

    /* default size and position for overlapped windows */
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    if (x == CW_USEDEFAULT) {
        x = 16 + (cascade % 8) * 22;
        y = 16 + (cascade % 8) * 22;
        cascade++;
    }
    if (cx == CW_USEDEFAULT) {
        cx = sw * 3 / 4;
        cy = sh * 3 / 4;
    }
    RECT rc = { x, y, x + cx, y + cy };
    if (parent != U.desktop)
        OffsetRect(&rc, parent->client.left, parent->client.top);
    w->rc = rc;
    nc_calc(w->style, w->exstyle, user_menu_bar_height(w), own_icon(w), &w->rc, &w->client);
    w->text = win_strdup(title ? title : "");
    if (!w->text) {
        wrgn_free(&w->update);
        win_free(w->extra);
        win_handle_free(h);
        win_free(w);
        return 0;
    }

    if (parent && (style & WS_CHILD))
        link_bottom(w);
    else
        link_top(w);

    CREATESTRUCT cs = { param, inst, menu, hparent, cy, cx, y, x, (LONG)style, title, klass, ex };
    if (!SendMessage(h, WM_NCCREATE, 0, (LPARAM)(intptr_t)&cs)) {
        DestroyWindow(h);
        return 0;
    }
    if (SendMessage(h, WM_CREATE, 0, (LPARAM)(intptr_t)&cs) == -1) {
        DestroyWindow(h);
        return 0;
    }
    if (!IsWindow(h))
        return 0;
    {
        RECT cl = w->client;
        rect_to_parent(w, &cl);
        SendMessage(h, WM_SIZE, SIZE_RESTORED, MAKELPARAM(cl.right - cl.left, cl.bottom - cl.top));
        SendMessage(h, WM_MOVE, 0, MAKELPARAM(cl.left, cl.top));
    }
    if ((style & WS_CHILD) && !(ex & WS_EX_NOPARENTNOTIFY))
        SendMessage(parent->h, WM_PARENTNOTIFY, WM_CREATE, MAKELPARAM(h, w->id));
    if (!IsWindow(h))
        return 0;
    if (want_vis) {
        int cmd = (style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED :
                  (style & WS_MINIMIZE) ? SW_SHOWMINIMIZED : SW_SHOW;
        w->style &= ~(WS_MAXIMIZE | WS_MINIMIZE);
        ShowWindow(h, cmd);
    }
    return h;
}

HWND WINAPI CreateWindow(LPCSTR klass, LPCSTR title, DWORD style, int x, int y, int cx, int cy,
                         HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    return CreateWindowEx(0, klass, title, style, x, y, cx, cy, parent, menu, inst, param);
}

static void send_destroy(wwnd *w)
{
    w->destroying = 1;
    SendMessage(w->h, WM_DESTROY, 0, 0);
    for (wwnd *c = w->child; c; c = c->next)
        send_destroy(c);
}

static void free_tree(wwnd *w)
{
    while (w->child) {
        wwnd *c = w->child;
        w->child = c->next;
        c->parent = NULL;
        free_tree(c);
    }
    HWND h = w->h;
    SendMessage(h, WM_NCDESTROY, 0, 0);
    if (U.focus == w) U.focus = NULL;
    if (U.active == w) U.active = NULL;
    if (U.capture == w) U.capture = NULL;
    user_caret_window_gone(h);
    user_kill_window_timers(h);
    user_purge_window(h);
    while (w->props) {
        wprop *p = w->props;
        w->props = p->next;
        DeleteAtom(p->atom);
        win_free(p);
    }
    user_menu_destroy_window(w);
    if (w->owndc)
        DeleteDC(w->owndc);
    wrgn_free(&w->update);
    win_free(w->text);
    win_free(w->extra);
    win_handle_free(h);
    win_free(w);
}

BOOL WINAPI DestroyWindow(HWND h)
{
    wwnd *w = (wwnd *)win_handle_get(h, WOBJ_WINDOW);
    if (!w || w == U.desktop || w->destroying)
        return FALSE;

    /* Preserve the hourglass during the potentially slow WM_DESTROY and
     * resource release of a top-level application window. Normal dialogs
     * and transient child controls must not flash the wait cursor. */
    int wait_close = !(w->style & WS_CHILD) && !w->owner && !w->dialog && w->task;
    if (wait_close)
        cursor_wait_begin();

    /* owned popups go first */
    if (!(w->style & WS_CHILD))
        for (wwnd *t = U.desktop->child, *n; t; t = n) {
            n = t->next;
            if (t->owner == w) {
                DestroyWindow(t->h);
                n = U.desktop->child;          /* list changed */
            }
        }

    int was_active = U.active == w;
    if (w->style & WS_VISIBLE)
        SetWindowPos(h, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
    if (U.focus && (U.focus == w || IsChild(h, U.focus->h)))
        U.focus = NULL;
    if ((w->style & WS_CHILD) && !(w->exstyle & WS_EX_NOPARENTNOTIFY) && w->parent)
        SendMessage(w->parent->h, WM_PARENTNOTIFY, WM_DESTROY, MAKELPARAM(h, w->id));

    send_destroy(w);
    unlink_wnd(w);
    w->parent = NULL;
    free_tree(w);
    if (was_active)
        activate_other(NULL);
    if (wait_close)
        cursor_wait_end();
    return TRUE;
}

/* ---------------------------------------------------------------- long / word / text */

LONG WINAPI GetWindowLong(HWND h, int index)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    switch (index) {
    case GWL_WNDPROC: return (LONG)(intptr_t)w->proc;
    case GWL_STYLE: return (LONG)w->style;
    case GWL_EXSTYLE: return (LONG)w->exstyle;
    default:
        if (index >= 0 && index + 4 <= w->cb_extra) {
            LONG v;
            memcpy(&v, w->extra + index, 4);
            return v;
        }
        return 0;
    }
}

LONG WINAPI SetWindowLong(HWND h, int index, LONG value)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    LONG old = GetWindowLong(h, index);
    switch (index) {
    case GWL_WNDPROC: w->proc = (WNDPROC)(intptr_t)value; break;
    case GWL_STYLE: w->style = (DWORD)value; break;
    case GWL_EXSTYLE: w->exstyle = (DWORD)value; break;
    default:
        if (index >= 0 && index + 4 <= w->cb_extra)
            memcpy(w->extra + index, &value, 4);
        break;
    }
    return old;
}

WORD WINAPI GetWindowWord(HWND h, int index)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    switch (index) {
    case GWW_ID: return (WORD)w->id;
    case GWW_HINSTANCE: return w->inst;
    case GWW_HWNDPARENT: return GetParent(h);
    default:
        if (index >= 0 && index + 2 <= w->cb_extra) {
            WORD v;
            memcpy(&v, w->extra + index, 2);
            return v;
        }
        return 0;
    }
}

WORD WINAPI SetWindowWord(HWND h, int index, WORD value)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    WORD old = GetWindowWord(h, index);
    switch (index) {
    case GWW_ID: w->id = value; break;
    case GWW_HINSTANCE: w->inst = value; break;
    default:
        if (index >= 0 && index + 2 <= w->cb_extra)
            memcpy(w->extra + index, &value, 2);
        break;
    }
    return old;
}

int WINAPI GetDlgCtrlID(HWND h) { wwnd *w = user_wnd(h); return w ? (int)w->id : 0; }

int WINAPI GetWindowText(HWND h, LPSTR buf, int max)
{
    if (!buf || max <= 0)
        return 0;
    buf[0] = 0;
    return (int)SendMessage(h, WM_GETTEXT, (WPARAM)max, (LPARAM)(intptr_t)buf);
}

int WINAPI GetWindowTextLength(HWND h)
{
    return (int)SendMessage(h, WM_GETTEXTLENGTH, 0, 0);
}

void WINAPI SetWindowText(HWND h, LPCSTR text)
{
    SendMessage(h, WM_SETTEXT, 0, (LPARAM)(intptr_t)(text ? text : ""));
}

/* ---------------------------------------------------------------- properties */

BOOL WINAPI SetProp(HWND h, LPCSTR name, HANDLE data)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return FALSE;
    ATOM a = IS_INTRESOURCE(name) ? (ATOM)(uintptr_t)name : AddAtom(name);
    for (wprop *p = w->props; p; p = p->next)
        if (p->atom == a) {
            p->data = data;
            if (!IS_INTRESOURCE(name))
                DeleteAtom(a);
            return TRUE;
        }
    wprop *p = (wprop *)win_alloc(sizeof(*p));
    if (!p)
        return FALSE;
    p->atom = a;
    p->data = data;
    p->next = w->props;
    w->props = p;
    return TRUE;
}

static wprop **find_prop(wwnd *w, LPCSTR name)
{
    ATOM a = IS_INTRESOURCE(name) ? (ATOM)(uintptr_t)name : FindAtom(name);
    if (!a)
        return NULL;
    for (wprop **pp = &w->props; *pp; pp = &(*pp)->next)
        if ((*pp)->atom == a)
            return pp;
    return NULL;
}

HANDLE WINAPI GetProp(HWND h, LPCSTR name)
{
    wwnd *w = user_wnd(h);
    wprop **pp = w ? find_prop(w, name) : NULL;
    return pp ? (*pp)->data : 0;
}

HANDLE WINAPI RemoveProp(HWND h, LPCSTR name)
{
    wwnd *w = user_wnd(h);
    wprop **pp = w ? find_prop(w, name) : NULL;
    if (!pp)
        return 0;
    wprop *p = *pp;
    HANDLE d = p->data;
    *pp = p->next;
    DeleteAtom(p->atom);
    win_free(p);
    return d;
}

/* ---------------------------------------------------------------- desktop */

void user_init_windows(void)
{
    gdi_overlay_hook = user_overlay_icons;
    user_register_builtin("#32769", user_desktop_proc, CS_DBLCLKS, 0, 0, 0);
    wclass *c = user_find_class("#32769");
    wwnd *d = (wwnd *)win_alloc(sizeof(*d));
    d->h = win_handle_alloc(WOBJ_WINDOW, d);
    d->cls = c;
    d->proc = c->wc.lpfnWndProc;
    d->style = WS_VISIBLE | WS_CLIPCHILDREN | WS_POPUP;
    SetRect(&d->rc, 0, 0, win_sys.screen.width, win_sys.screen.height);
    d->client = d->rc;
    d->text = win_strdup("");
    wrgn_init(&d->update);
    U.desktop = d;
    wrgn r;
    wrgn_init(&r);
    wrgn_set_rect(&r, &d->rc);
    add_update(d, &r, 1);
    wrgn_free(&r);
}
