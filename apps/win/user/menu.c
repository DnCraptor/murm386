/*
 * menu.c - menus: objects, menu bar, popup menus, system menu, tracking,
 * accelerators.
 *
 * Tracking runs a modal loop like Windows: input messages are consumed by
 * the menu, everything else (paint, timers) is dispatched. Each open popup
 * is a top-level window of class "#32768" that only paints; hit testing
 * and selection are done here on screen coordinates.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

#define ITEM_H   18
#define SEP_H    8
#define CHECK_W  16
#define ARROW_W  16
#define BAR_PAD  8

/* the items a maximized MDI child puts into the frame's menu bar */
enum { MK_NONE, MK_CHILDSYS, MK_RESTORE };

typedef struct {
    UINT  flags;
    UINT  id;
    HMENU sub;
    char *text;
    RECT  rc;           /* bar: relative to the bar; popup: popup client */
    int   kind;         /* MK_*: its commands are system commands of target */
    HWND  target;
} mitem;

typedef struct {
    int    n, cap;
    mitem *it;
    int    popup;
    int    w, h;        /* popup size after layout */
    int    accel_x;     /* popup: column of the accelerator text */
} wmenu;

static wmenu *menu_get(HMENU h) { return (wmenu *)win_handle_get(h, WOBJ_MENU); }

/* ---------------------------------------------------------------- objects */

static HMENU menu_new(int popup)
{
    wmenu *m = (wmenu *)win_alloc(sizeof(*m));
    if (!m)
        return 0;
    m->popup = popup;
    HMENU h = win_handle_alloc(WOBJ_MENU, m);
    if (!h)
        win_free(m);
    return h;
}

HMENU WINAPI CreateMenu(void) { return menu_new(0); }
HMENU WINAPI CreatePopupMenu(void) { return menu_new(1); }

BOOL WINAPI IsMenu(HMENU h) { return menu_get(h) != NULL; }

/* Windows 2's one function for all: by MF_APPEND, MF_DELETE, MF_REMOVE,
 * MF_CHANGE, else an insertion */
BOOL WINAPI ChangeMenu(HMENU h, UINT pos, LPCSTR text, UINT id, UINT flags)
{
    UINT f = flags & ~(UINT)(MF_APPEND | MF_DELETE | MF_REMOVE | MF_CHANGE);
    if (flags & MF_APPEND)
        return AppendMenu(h, f, id, text);
    if (flags & MF_DELETE)
        return DeleteMenu(h, pos, f);
    if (flags & MF_REMOVE)
        return RemoveMenu(h, pos, f);
    if (flags & MF_CHANGE)
        return ModifyMenu(h, pos, f, id, text);
    return InsertMenu(h, pos, f, id, text);
}

DWORD WINAPI GetMenuCheckMarkDimensions(void) { return MAKELONG(CHECK_W, CHECK_W); }

/* bitmaps of its own for an item's check: taken, the check drawn as WINN's */
BOOL WINAPI SetMenuItemBitmaps(HMENU h, UINT pos, UINT flags, HBITMAP unchecked, HBITMAP checked)
{
    (void)pos; (void)flags; (void)unchecked; (void)checked;
    return IsMenu(h);
}

BOOL WINAPI DestroyMenu(HMENU h)
{
    wmenu *m = menu_get(h);
    if (!m)
        return FALSE;
    for (int i = 0; i < m->n; i++) {
        if ((m->it[i].flags & MF_POPUP) && !m->it[i].kind)   /* a child's system menu is its own */
            DestroyMenu(m->it[i].sub);
        win_free(m->it[i].text);
    }
    win_free(m->it);
    win_free(m);
    win_handle_free(h);
    return TRUE;
}

/* Item by command (searching submenus) or by position. */
static mitem *find_item(HMENU h, UINT key, UINT flags, wmenu **owner, int *index)
{
    wmenu *m = menu_get(h);
    if (!m)
        return NULL;
    if (flags & MF_BYPOSITION) {
        if (key >= (UINT)m->n)
            return NULL;
        if (owner) *owner = m;
        if (index) *index = (int)key;
        return &m->it[key];
    }
    for (int i = 0; i < m->n; i++) {
        if (!(m->it[i].flags & MF_POPUP) && m->it[i].id == key) {
            if (owner) *owner = m;
            if (index) *index = i;
            return &m->it[i];
        }
        if (m->it[i].flags & MF_POPUP) {
            mitem *r = find_item(m->it[i].sub, key, flags, owner, index);
            if (r)
                return r;
        }
    }
    return NULL;
}

static void set_item(mitem *it, UINT flags, UINT id, LPCSTR text)
{
    win_free(it->text);
    it->text = NULL;
    it->flags = flags & ~(UINT)(MF_BYPOSITION | MF_APPEND | MF_DELETE | MF_REMOVE | MF_CHANGE);
    if (flags & MF_POPUP) {
        it->sub = (HMENU)id;
        it->id = (UINT)-1;
    } else {
        it->sub = 0;
        it->id = id;
    }
    if (!(flags & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW)))
        it->text = win_strdup(text ? text : "");
}

BOOL WINAPI InsertMenu(HMENU h, UINT pos, UINT flags, UINT id, LPCSTR text)
{
    wmenu *m = menu_get(h);
    if (!m)
        return FALSE;
    int at = m->n;
    if (pos != (UINT)-1) {
        if (flags & MF_BYPOSITION) {
            if (pos < (UINT)m->n)
                at = (int)pos;
        } else {
            wmenu *om;
            int idx;
            /* only items of this menu itself; otherwise append */
            if (find_item(h, pos, MF_BYCOMMAND, &om, &idx) && om == m)
                at = idx;
        }
    }
    if (m->n == m->cap) {
        int nc = m->cap ? m->cap * 2 : 8;
        mitem *n = (mitem *)win_realloc(m->it, sizeof(mitem) * (size_t)nc);
        if (!n)
            return FALSE;
        m->it = n;
        m->cap = nc;
    }
    memmove(&m->it[at + 1], &m->it[at], sizeof(mitem) * (size_t)(m->n - at));
    memset(&m->it[at], 0, sizeof(mitem));
    set_item(&m->it[at], flags, id, text);
    m->n++;
    if (flags & MF_POPUP) {
        wmenu *sub = menu_get((HMENU)id);
        if (sub)
            sub->popup = 1;
    }
    return TRUE;
}

BOOL WINAPI AppendMenu(HMENU h, UINT flags, UINT id, LPCSTR text)
{
    return InsertMenu(h, (UINT)-1, flags | MF_BYPOSITION, id, text);
}

BOOL WINAPI ModifyMenu(HMENU h, UINT pos, UINT flags, UINT id, LPCSTR text)
{
    mitem *it = find_item(h, pos, flags, NULL, NULL);
    if (!it)
        return FALSE;
    set_item(it, flags, id, text);
    return TRUE;
}

static BOOL remove_item(HMENU h, UINT pos, UINT flags, int destroy)
{
    wmenu *m;
    int idx;
    mitem *it = find_item(h, pos, flags, &m, &idx);
    if (!it)
        return FALSE;
    if (destroy && (it->flags & MF_POPUP))
        DestroyMenu(it->sub);
    win_free(it->text);
    memmove(&m->it[idx], &m->it[idx + 1], sizeof(mitem) * (size_t)(m->n - idx - 1));
    m->n--;
    return TRUE;
}

BOOL WINAPI DeleteMenu(HMENU h, UINT pos, UINT flags) { return remove_item(h, pos, flags, 1); }
BOOL WINAPI RemoveMenu(HMENU h, UINT pos, UINT flags) { return remove_item(h, pos, flags, 0); }

HMENU WINAPI GetSubMenu(HMENU h, int pos)
{
    wmenu *m = menu_get(h);
    if (!m || pos < 0 || pos >= m->n || !(m->it[pos].flags & MF_POPUP))
        return 0;
    return m->it[pos].sub;
}

int WINAPI GetMenuItemCount(HMENU h)
{
    wmenu *m = menu_get(h);
    return m ? m->n : -1;
}

UINT WINAPI GetMenuItemID(HMENU h, int pos)
{
    wmenu *m = menu_get(h);
    if (!m || pos < 0 || pos >= m->n)
        return (UINT)-1;
    return (m->it[pos].flags & MF_POPUP) ? (UINT)-1 : m->it[pos].id;
}

UINT WINAPI GetMenuState(HMENU h, UINT id, UINT flags)
{
    mitem *it = find_item(h, id, flags, NULL, NULL);
    if (!it)
        return (UINT)-1;
    if (it->flags & MF_POPUP)
        return (it->flags & 0xFF) | ((UINT)GetMenuItemCount(it->sub) << 8);
    return it->flags;
}

int WINAPI GetMenuString(HMENU h, UINT id, LPSTR buf, int max, UINT flags)
{
    mitem *it = find_item(h, id, flags, NULL, NULL);
    if (!it || !buf || max <= 0)
        return 0;
    lstrcpyn(buf, it->text ? it->text : "", max);
    return lstrlen(buf);
}

BOOL WINAPI CheckMenuItem(HMENU h, UINT id, UINT flags)
{
    mitem *it = find_item(h, id, flags, NULL, NULL);
    if (!it)
        return (BOOL)-1;
    BOOL old = (it->flags & MF_CHECKED) ? MF_CHECKED : MF_UNCHECKED;
    it->flags = (it->flags & ~(UINT)MF_CHECKED) | (flags & MF_CHECKED);
    return old;
}

BOOL WINAPI EnableMenuItem(HMENU h, UINT id, UINT flags)
{
    mitem *it = find_item(h, id, flags, NULL, NULL);
    if (!it)
        return (BOOL)-1;
    BOOL old = (BOOL)(it->flags & (MF_GRAYED | MF_DISABLED));
    it->flags = (it->flags & ~(UINT)(MF_GRAYED | MF_DISABLED)) | (flags & (MF_GRAYED | MF_DISABLED));
    return old;
}

/* ---------------------------------------------------------------- window menus */

int user_menu_bar_height(const wwnd *w)
{
    return (w->menu && !(w->style & WS_CHILD) && menu_get(w->menu)) ? UM_MENU + 1 : 0;
}

static void bar_rect(const wwnd *w, RECT *r)
{
    int f = user_frame_width(w->style, w->exstyle);
    int ww = w->rc.right - w->rc.left;
    int top = f + (user_has_caption(w->style) ? UM_CAPTION + 1 : 0);
    SetRect(r, f, top, ww - f, top + UM_MENU);
}

static void reframe(wwnd *w)
{
    RECT r = w->rc;
    if (w->parent && w->parent != U.desktop)
        OffsetRect(&r, -w->parent->client.left, -w->parent->client.top);
    SetWindowPos(w->h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

HMENU WINAPI GetMenu(HWND h)
{
    wwnd *w = user_wnd(h);
    return w && !(w->style & WS_CHILD) ? w->menu : 0;
}

BOOL WINAPI SetMenu(HWND h, HMENU menu)
{
    wwnd *w = user_wnd(h);
    if (!w || (w->style & WS_CHILD))
        return FALSE;
    w->menu = menu;
    reframe(w);
    return TRUE;
}

void WINAPI DrawMenuBar(HWND h)
{
    wwnd *w = user_wnd(h);
    if (w && user_menu_bar_height(w))
        user_nc_paint(w);
}

void user_menu_destroy_window(wwnd *w)
{
    if (w->menu && !(w->style & WS_CHILD))
        DestroyMenu(w->menu);
    w->menu = 0;
    if (w->sysmenu)
        DestroyMenu(w->sysmenu);
    w->sysmenu = 0;
}

static HMENU default_sysmenu(void)
{
    HMENU m = CreatePopupMenu();
    AppendMenu(m, MF_STRING, SC_RESTORE, "&Restore");
    AppendMenu(m, MF_STRING, SC_MOVE, "&Move");
    AppendMenu(m, MF_STRING, SC_SIZE, "&Size");
    AppendMenu(m, MF_STRING, SC_MINIMIZE, "Mi&nimize");
    AppendMenu(m, MF_STRING, SC_MAXIMIZE, "Ma&ximize");
    AppendMenu(m, MF_SEPARATOR, 0, NULL);
    AppendMenu(m, MF_STRING, SC_CLOSE, "&Close\tAlt+F4");
    return m;
}

HMENU WINAPI GetSystemMenu(HWND h, BOOL revert)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    if (revert) {
        if (w->sysmenu)
            DestroyMenu(w->sysmenu);
        w->sysmenu = 0;
        return 0;
    }
    if (!w->sysmenu)
        w->sysmenu = default_sysmenu();
    return w->sysmenu;
}

/* A maximized MDI child's system menu at the left of the frame's menu bar,
 * its restore button at the right (child 0: taken away again). */
void user_menu_mdi_merge(HMENU bar, HWND child)
{
    wmenu *m = menu_get(bar);
    if (!m)
        return;
    for (int i = m->n - 1; i >= 0; i--)
        if (m->it[i].kind) {
            win_free(m->it[i].text);
            memmove(&m->it[i], &m->it[i + 1], sizeof(mitem) * (size_t)(m->n - i - 1));
            m->n--;
        }
    if (!child)
        return;
    HMENU sm = GetSystemMenu(child, FALSE);
    if (!InsertMenu(bar, 0, MF_BYPOSITION | MF_POPUP, (UINT)sm, ""))
        return;
    m->it[0].kind = MK_CHILDSYS;
    m->it[0].target = child;
    if (AppendMenu(bar, MF_STRING, SC_RESTORE, "")) {
        m->it[m->n - 1].kind = MK_RESTORE;
        m->it[m->n - 1].target = child;
    }
}

/* State of the system menu items follows the window. */
static void sysmenu_update(wwnd *w, HMENU m)
{
    DWORD s = w->style;
    int mn = (s & WS_MINIMIZE) != 0, mx = (s & WS_MAXIMIZE) != 0;
    EnableMenuItem(m, SC_RESTORE, (mn || mx) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, SC_MOVE, !mx ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, SC_SIZE, ((s & WS_THICKFRAME) && !mn && !mx) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, SC_MINIMIZE, ((s & WS_MINIMIZEBOX) && !mn) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, SC_MAXIMIZE, ((s & WS_MAXIMIZEBOX) && !mx) ? MF_ENABLED : MF_GRAYED);
}

/* ---------------------------------------------------------------- layout and drawing */

/* Text before and after the tab, mnemonic '&' not counted. */
static int text_part_width(HDC dc, const char *s, int n)
{
    RECT r = { 0, 0, 1000, 100 };
    user_draw_text(dc, s, n, &r, DT_SINGLELINE | DT_CALCRECT);
    return r.right - r.left;
}

static void split_tab(const char *s, int *left_n, const char **right)
{
    const char *t = s;
    while (*t && *t != '\t')
        t++;
    *left_n = (int)(t - s);
    *right = *t ? t + 1 : NULL;
}

static void layout_bar(wmenu *m, int width)
{
    HDC dc = GetDC(0);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    int x = 0, help = -1;
    for (int i = 0; i < m->n; i++) {
        mitem *it = &m->it[i];
        int ln;
        const char *r;
        split_tab(it->text ? it->text : "", &ln, &r);
        int w = text_part_width(dc, it->text ? it->text : "", ln) + 2 * BAR_PAD;
        if (it->flags & MF_SEPARATOR)
            w = BAR_PAD;
        if (it->kind)
            w = UM_MENU + 2;
        if ((it->flags & MF_HELP) && help < 0)
            help = i;
        SetRect(&it->rc, x, 0, x + w, UM_MENU);
        x += w;
    }
    /* the restore button keeps to the right edge, the rest of a help
     * item's run goes just left of it */
    int right = width;
    if (m->n && m->it[m->n - 1].kind == MK_RESTORE) {
        mitem *r = &m->it[m->n - 1];
        int w = r->rc.right - r->rc.left;
        SetRect(&r->rc, width - w, 0, width, UM_MENU);
        right = width - w;
        x -= w;
    }
    if (help >= 0 && x < right) {
        int shift = right - x;
        for (int i = help; i < m->n && !m->it[i].kind; i++)
            OffsetRect(&m->it[i].rc, shift, 0);
    }
    ReleaseDC(0, dc);
}

static void layout_popup(wmenu *m)
{
    HDC dc = GetDC(0);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    int tw = 0, aw = 0, y = 1;
    for (int i = 0; i < m->n; i++) {
        mitem *it = &m->it[i];
        if (it->flags & MF_SEPARATOR)
            continue;
        int ln;
        const char *r;
        split_tab(it->text ? it->text : "", &ln, &r);
        int a = text_part_width(dc, it->text ? it->text : "", ln);
        if (a > tw) tw = a;
        if (r) {
            int b = text_part_width(dc, r, -1);
            if (b > aw) aw = b;
        }
    }
    m->accel_x = 1 + CHECK_W + tw + (aw ? 16 : 0);
    int width = m->accel_x + aw + ARROW_W;
    for (int i = 0; i < m->n; i++) {
        mitem *it = &m->it[i];
        int h = (it->flags & MF_SEPARATOR) ? SEP_H : ITEM_H;
        SetRect(&it->rc, 1, y, 1 + width, y + h);
        y += h;
    }
    m->w = width + 2;
    m->h = y + 1;
    ReleaseDC(0, dc);
}

static int item_disabled(const mitem *it) { return (it->flags & (MF_GRAYED | MF_DISABLED)) != 0; }

static void draw_item(HDC dc, wmenu *m, int i, int selected, int ox, int oy)
{
    mitem *it = &m->it[i];
    RECT r = it->rc;
    OffsetRect(&r, ox, oy);
    if (it->flags & MF_SEPARATOR) {
        FillRect(dc, &r, user_sys_brush(COLOR_MENU));
        if (m->popup) {
            RECT l = { r.left, (r.top + r.bottom) / 2, r.right, (r.top + r.bottom) / 2 + 1 };
            FillRect(dc, &l, user_sys_brush(COLOR_MENUTEXT));
        }
        return;
    }
    if (it->kind == MK_CHILDSYS) {          /* the child's system menu box */
        FillRect(dc, &r, user_sys_brush(COLOR_MENU));
        RECT b = { r.left + 1, r.top + 1, r.left + 1 + UM_MENU - 2, r.bottom - 1 };
        FillRect(dc, &b, user_sys_brush(selected ? COLOR_HIGHLIGHT : COLOR_BTNFACE));
        FrameRect(dc, &b, user_sys_brush(COLOR_WINDOWFRAME));
        int cy = (b.top + b.bottom) / 2, x0 = b.left + 4, x1 = b.right - 4;
        RECT sh = { x0 + 1, cy, x1 + 1, cy + 2 }, bar = { x0, cy - 1, x1, cy + 1 };
        FillRect(dc, &sh, user_sys_brush(COLOR_BTNSHADOW));
        FillRect(dc, &bar, user_sys_brush(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWFRAME));
        return;
    }
    if (it->kind == MK_RESTORE) {           /* the child's restore button */
        FillRect(dc, &r, user_sys_brush(COLOR_MENU));
        RECT b = { r.right - UM_MENU, r.top + 1, r.right - 1, r.bottom - 1 };
        user_draw_3d(dc, &b, selected);
        if (selected)
            OffsetRect(&b, 1, 1);
        RECT u = b, d = b;
        u.bottom = (b.top + b.bottom) / 2 + 1;
        d.top = (b.top + b.bottom) / 2 - 1;
        user_draw_arrow(dc, &u, 0, RGB(0, 0, 0));
        user_draw_arrow(dc, &d, 1, RGB(0, 0, 0));
        return;
    }
    FillRect(dc, &r, user_sys_brush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
    SetBkMode(dc, TRANSPARENT);
    COLORREF tc = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT);
    if (item_disabled(it))
        tc = GetSysColor(COLOR_GRAYTEXT);
    SetTextColor(dc, tc);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    const char *text = it->text ? it->text : "";
    int ln;
    const char *acc;
    split_tab(text, &ln, &acc);
    if (!m->popup) {
        user_draw_text(dc, text, ln, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        return;
    }
    RECT tr = r;
    tr.left += CHECK_W;
    user_draw_text(dc, text, ln, &tr, DT_SINGLELINE | DT_VCENTER);
    if (acc) {
        RECT ar = r;
        ar.left = ox + m->accel_x;
        user_draw_text(dc, acc, -1, &ar, DT_SINGLELINE | DT_VCENTER);
    }
    if (it->flags & MF_CHECKED) {
        HPEN p = CreatePen(PS_SOLID, 2, tc);
        HGDIOBJ op = SelectObject(dc, p);
        int cx = r.left + 4, cy = (r.top + r.bottom) / 2;
        MoveTo(dc, cx, cy);
        LineTo(dc, cx + 3, cy + 3);
        LineTo(dc, cx + 9, cy - 4);
        SelectObject(dc, op);
        DeleteObject(p);
    }
    if (it->flags & MF_POPUP) {
        RECT ar = { r.right - ARROW_W, r.top, r.right, r.bottom };
        user_draw_arrow(dc, &ar, 3, tc);
    }
}

/* Bar inside the window DC; the black line below is drawn by nc.c. */
void user_menu_paint_bar(wwnd *w, HDC dc, const RECT *bar)
{
    wmenu *m = menu_get(w->menu);
    FillRect(dc, bar, user_sys_brush(COLOR_MENU));
    if (!m)
        return;
    layout_bar(m, bar->right - bar->left);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, bar->left, bar->top, bar->right, bar->bottom);
    for (int i = 0; i < m->n; i++)
        draw_item(dc, m, i, (m->it[i].flags & MF_HILITE) != 0, bar->left, bar->top);
    RestoreDC(dc, saved);
}

BOOL WINAPI HiliteMenuItem(HWND h, HMENU menu, UINT id, UINT flags)
{
    mitem *it = find_item(menu, id, flags, NULL, NULL);
    if (!it)
        return FALSE;
    it->flags = (it->flags & ~(UINT)MF_HILITE) | (flags & MF_HILITE);
    DrawMenuBar(h);
    return TRUE;
}

/* ---------------------------------------------------------------- tracking */

typedef struct {
    HMENU menu;
    wmenu *m;
    HWND  wnd;          /* popup window, 0 for the bar */
    int   sel;
    RECT  org;          /* screen origin of item coordinates (left/top) */
} mlevel;

#define MAX_LEVELS 8

static struct {
    mlevel lv[MAX_LEVELS];
    int    depth;
    wwnd  *owner;
    int    is_sys;      /* items are system commands */
    int    done;
    UINT   cmd;
    int    have_cmd;
    HWND   target;      /* a merged MDI child item: its system command */
    int    mouse_down_outside;
} T;

LRESULT CALLBACK user_menu_popup_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        for (int l = 0; l < T.depth; l++) {
            if (T.lv[l].wnd != h)
                continue;
            wmenu *m = T.lv[l].m;
            RECT r = { 0, 0, m->w, m->h };
            FrameRect(dc, &r, user_sys_brush(COLOR_WINDOWFRAME));
            for (int i = 0; i < m->n; i++)
                draw_item(dc, m, i, i == T.lv[l].sel, 0, 0);
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void redraw_item(int l, int i)
{
    mlevel *v = &T.lv[l];
    if (i < 0 || i >= v->m->n)
        return;
    if (v->wnd) {
        HDC dc = GetDC(v->wnd);
        draw_item(dc, v->m, i, i == v->sel, 0, 0);
        ReleaseDC(v->wnd, dc);
    } else {
        HDC dc = GetWindowDC(T.owner->h);
        RECT b;
        bar_rect(T.owner, &b);
        draw_item(dc, v->m, i, i == v->sel, b.left, b.top);
        ReleaseDC(T.owner->h, dc);
    }
}

static void menu_select_msg(int l)
{
    mlevel *v = &T.lv[l];
    if (v->sel < 0) {
        SendMessage(T.owner->h, WM_MENUSELECT, 0, MAKELPARAM(0, v->menu));
        return;
    }
    mitem *it = &v->m->it[v->sel];
    UINT fl = (it->flags & 0xFF) | MF_HILITE | (T.is_sys ? MF_SYSMENU : 0);
    WPARAM id = (it->flags & MF_POPUP) ? it->sub : it->id;
    SendMessage(T.owner->h, WM_MENUSELECT, id, MAKELPARAM(fl, v->menu));
}

static void set_sel(int l, int i)
{
    mlevel *v = &T.lv[l];
    if (v->sel == i)
        return;
    int old = v->sel;
    v->sel = i;
    redraw_item(l, old);
    redraw_item(l, i);
    menu_select_msg(l);
}

static void close_above(int l)
{
    while (T.depth > l + 1) {
        mlevel *v = &T.lv[--T.depth];
        if (v->wnd)
            DestroyWindow(v->wnd);
        v->wnd = 0;
    }
}

static int open_popup(HMENU menu, int x, int y, int alt_x, int index)
{
    wmenu *m = menu_get(menu);
    if (!m || T.depth >= MAX_LEVELS)
        return 0;
    SendMessage(T.owner->h, WM_INITMENUPOPUP, menu, MAKELPARAM(index, T.is_sys));
    layout_popup(m);
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    if (x + m->w > sw)
        x = alt_x - m->w >= 0 ? alt_x - m->w : sw - m->w;
    if (y + m->h > sh)
        y = sh - m->h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    mlevel *v = &T.lv[T.depth++];
    v->menu = menu;
    v->m = m;
    v->sel = -1;
    SetRect(&v->org, x, y, x, y);
    v->wnd = CreateWindowEx(0, "#32768", "", WS_POPUP, x, y, m->w, m->h, T.owner->h, 0, 0, NULL);
    if (v->wnd) {
        SetWindowPos(v->wnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(v->wnd);
    }
    return 1;
}

/* Open the submenu of level l's selected item. */
static void open_sub(int l, int select_first)
{
    mlevel *v = &T.lv[l];
    if (v->sel < 0)
        return;
    mitem *it = &v->m->it[v->sel];
    if (!(it->flags & MF_POPUP) || item_disabled(it))
        return;
    if (it->kind == MK_CHILDSYS) {
        wwnd *c = user_wnd(it->target);
        if (!c)
            return;
        sysmenu_update(c, it->sub);
    }
    close_above(l);
    int x, y, alt_x;
    if (!v->wnd) {          /* below the bar item */
        x = v->org.left + it->rc.left;
        y = v->org.top + it->rc.bottom + 1;
        alt_x = v->org.left + it->rc.right;
    } else {                /* right of the popup item */
        x = v->org.left + it->rc.right - 2;
        y = v->org.top + it->rc.top - 1;
        alt_x = v->org.left + 2;
    }
    if (open_popup(it->sub, x, y, alt_x, v->sel) && select_first) {
        mlevel *n = &T.lv[T.depth - 1];
        for (int i = 0; i < n->m->n; i++)
            if (!(n->m->it[i].flags & MF_SEPARATOR)) {
                set_sel(T.depth - 1, i);
                break;
            }
    }
}

static int hit_level(int x, int y, int *item)
{
    for (int l = T.depth - 1; l >= 0; l--) {
        mlevel *v = &T.lv[l];
        POINT p = { x - v->org.left, y - v->org.top };
        if (v->wnd) {
            RECT r = { 0, 0, v->m->w, v->m->h };
            if (!PtInRect(&r, p))
                continue;
        }
        for (int i = 0; i < v->m->n; i++)
            if (PtInRect(&v->m->it[i].rc, p) && !(v->m->it[i].flags & MF_SEPARATOR)) {
                *item = i;
                return l;
            }
        if (v->wnd) {
            *item = -1;
            return l;
        }
    }
    return -1;
}

static void choose(int l, int i)
{
    mitem *it = &T.lv[l].m->it[i];
    if (it->flags & MF_POPUP) {
        open_sub(l, 1);
        return;
    }
    if (item_disabled(it))
        return;
    T.cmd = it->id;
    T.have_cmd = 1;
    T.done = 1;
    /* under a merged child item of the bar: the child's system command */
    mlevel *b = &T.lv[0];
    if (!b->wnd && b->sel >= 0 && b->m->it[b->sel].kind)
        T.target = b->m->it[b->sel].target;
}

static void on_mouse(UINT msg)
{
    int item, l = hit_level(U.mouse_x, U.mouse_y, &item);
    int down = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK ||
               msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK;
    int up = msg == WM_LBUTTONUP || msg == WM_RBUTTONUP;
    if (l < 0) {
        if (down)
            T.done = 1;          /* click elsewhere closes the menu */
        else if (T.depth && T.lv[T.depth - 1].wnd && !up)
            set_sel(T.depth - 1, -1);
        return;
    }
    if (item < 0) {
        if (T.lv[l].wnd && l == T.depth - 1)
            set_sel(l, -1);
        return;
    }
    mlevel *v = &T.lv[l];
    int is_bar = !v->wnd;
    if (v->sel != item) {
        int was_open = T.depth > l + 1;
        close_above(l);
        set_sel(l, item);
        /* a bar item opens its popup when one was open or on a press,
         * a popup item with a submenu opens it at once */
        if (!is_bar || down || was_open || (U.buttons & 1))
            open_sub(l, 0);
    } else if (down && is_bar && T.depth > l + 1) {
        return;
    } else if ((down || (U.buttons & 1)) && T.depth == l + 1) {
        open_sub(l, 0);
    }
    if (up && !is_bar)
        choose(l, item);
    else if (up && is_bar && v->sel == item && !(v->m->it[item].flags & MF_POPUP))
        choose(l, item);            /* a command right on the bar */
}

static int mnemonic_of(const char *s)
{
    for (; s && *s; s++)
        if (s[0] == '&' && s[1] && s[1] != '&') {
            char c[2] = { s[1], 0 };
            AnsiUpper(c);
            return (unsigned char)c[0];
        } else if (s[0] == '&') {
            s++;
        }
    return 0;
}

static int find_mnemonic(mlevel *v, int ch)
{
    char c[2] = { (char)ch, 0 };
    AnsiUpper(c);
    for (int i = 0; i < v->m->n; i++)
        if (mnemonic_of(v->m->it[i].text) == (unsigned char)c[0])
            return i;
    return -1;
}

static int next_item(mlevel *v, int from, int dir)
{
    int n = v->m->n;
    if (!n)
        return -1;
    int i = from;
    for (int k = 0; k < n; k++) {
        i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
        if (!(v->m->it[i].flags & MF_SEPARATOR))
            return i;
    }
    return -1;
}

static void bar_move(int dir)
{
    mlevel *b = &T.lv[0];
    int was_open = T.depth > 1;
    close_above(0);
    set_sel(0, next_item(b, b->sel, dir));
    if (was_open)
        open_sub(0, 1);
}

static void on_key(int vk)
{
    mlevel *top = &T.lv[T.depth - 1];
    int has_bar = !T.lv[0].wnd;
    switch (vk) {
    case VK_ESCAPE:
        if (T.depth > 1) {
            close_above(T.depth - 2);
            if (T.depth == 1 && !has_bar)
                T.done = 1;
        } else {
            T.done = 1;
        }
        return;
    case VK_MENU:
    case VK_F10:
        T.done = 1;
        return;
    case VK_UP:
    case VK_DOWN:
        if (top->wnd)
            set_sel(T.depth - 1, next_item(top, top->sel, vk == VK_DOWN ? 1 : -1));
        else if (vk == VK_DOWN)
            open_sub(0, 1);
        return;
    case VK_LEFT:
        if (top->wnd && T.depth > (has_bar ? 2 : 1))
            close_above(T.depth - 2);
        else if (has_bar)
            bar_move(-1);
        return;
    case VK_RIGHT:
        if (top->wnd && top->sel >= 0 && (top->m->it[top->sel].flags & MF_POPUP))
            open_sub(T.depth - 1, 1);
        else if (has_bar)
            bar_move(1);
        return;
    case VK_RETURN:
        if (top->sel >= 0)
            choose(T.depth - 1, top->sel);
        return;
    default: {
        unsigned char ch;
        if (!user_vk_to_char(vk, 0, &ch))
            return;
        int i = find_mnemonic(top, ch);
        if (i >= 0) {
            set_sel(T.depth - 1, i);
            choose(T.depth - 1, i);
        }
        return;
    }
    }
}

/* level 0 must be set up by the caller */
static void track_loop(void)
{
    T.done = 0;
    T.have_cmd = 0;
    SetCapture(T.owner->h);
    SendMessage(T.owner->h, WM_INITMENU, T.lv[0].menu, 0);
    int first_up = (U.buttons & 1) != 0;
    while (!T.done) {
        MSG m;
        if (!user_peek(&m, 0, 0, 0, PM_REMOVE)) {
            if (win_sys.input)
                win_sys.input->idle();
            continue;
        }
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (CallMsgFilter(&m, MSGF_MENU))
            continue;
        int mouse = (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST) ||
                    (m.message >= WM_NCMOUSEMOVE && m.message <= WM_NCMBUTTONDBLCLK);
        if (mouse) {
            UINT mm = m.message >= WM_MOUSEFIRST ? m.message
                                                  : m.message + WM_MOUSEMOVE - WM_NCMOUSEMOVE;
            if (first_up && mm == WM_LBUTTONUP) {
                first_up = 0;
                int item, l = hit_level(U.mouse_x, U.mouse_y, &item);
                if (l == 0 && !T.lv[0].wnd && item >= 0 && item == T.lv[0].sel &&
                    !(T.lv[0].m->it[item].flags & MF_POPUP)) {
                    choose(0, item);  /* a command right on the bar */
                    continue;
                }
                if (l < 0 || !T.lv[l].wnd)
                    continue;      /* release on the bar keeps the menu open */
            }
            on_mouse(mm);
            continue;
        }
        if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
            on_key((int)m.wParam);
            continue;
        }
        if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST)
            continue;
        DispatchMessage(&m);
        if (!IsWindow(T.owner->h))
            break;
    }
    ReleaseCapture();
    close_above(0);
    if (T.lv[0].wnd)
        DestroyWindow(T.lv[0].wnd);
    if (IsWindow(T.owner->h)) {
        if (!T.lv[0].wnd && T.lv[0].sel >= 0) {
            int s = T.lv[0].sel;
            T.lv[0].sel = -1;
            redraw_item(0, s);
        }
        SendMessage(T.owner->h, WM_MENUSELECT, 0, MAKELPARAM(0xFFFF, 0));
        if (T.have_cmd && T.target && IsWindow(T.target))
            PostMessage(T.target, WM_SYSCOMMAND, T.cmd, 0);
        else if (T.have_cmd)
            PostMessage(T.owner->h, T.is_sys ? WM_SYSCOMMAND : WM_COMMAND, T.cmd, 0);
    }
    T.depth = 0;
}

static int start_bar(wwnd *w)
{
    wmenu *m = menu_get(w->menu);
    if (!m || !m->n || T.depth)
        return 0;
    memset(&T, 0, sizeof(T));
    T.owner = w;
    RECT b;
    bar_rect(w, &b);
    layout_bar(m, b.right - b.left);
    T.lv[0].menu = w->menu;
    T.lv[0].m = m;
    T.lv[0].sel = -1;
    SetRect(&T.lv[0].org, w->rc.left + b.left, w->rc.top + b.top, 0, 0);
    T.depth = 1;
    return 1;
}

void user_menu_track_bar(wwnd *w, int x, int y)
{
    (void)x; (void)y;
    if (!start_bar(w))
        return;
    int item, l = hit_level(U.mouse_x, U.mouse_y, &item);
    if (l == 0 && item >= 0) {
        set_sel(0, item);
        open_sub(0, 0);
    }
    track_loop();
}

/* Keyboard entry: Alt alone or F10 (ch 0), Alt+letter. */
void user_menu_key(wwnd *w, int ch)
{
    if (ch == ' ' || (ch == '-' && (w->style & WS_CHILD))) {
        user_menu_track_sys(w, 1);      /* Alt+-: an MDI child's own */
        return;
    }
    if (ch == '-') {
        wmenu *bm = menu_get(w->menu);
        if (bm && bm->n && bm->it[0].kind == MK_CHILDSYS && start_bar(w)) {
            set_sel(0, 0);
            open_sub(0, 1);
            track_loop();
            return;
        }
    }
    if (!start_bar(w)) {
        if (!ch && (w->style & WS_SYSMENU))
            user_menu_track_sys(w, 1);
        return;
    }
    if (ch) {
        int i = find_mnemonic(&T.lv[0], ch);
        if (i < 0) {
            T.depth = 0;
            MessageBeep(0);
            return;
        }
        set_sel(0, i);
        if (T.lv[0].m->it[i].flags & MF_POPUP) {
            open_sub(0, 1);
        } else {
            choose(0, i);
            T.depth = 1;
        }
        if (T.done) {           /* a command right on the bar */
            T.depth = 1;
            T.done = 0;
            UINT cmd = T.cmd;
            T.lv[0].sel = -1;
            redraw_item(0, i);
            T.depth = 0;
            PostMessage(w->h, WM_COMMAND, cmd, 0);
            return;
        }
    } else {
        set_sel(0, next_item(&T.lv[0], -1, 1));
    }
    track_loop();
}

void user_menu_track_sys(wwnd *w, int by_key)
{
    if (T.depth || !(w->style & WS_SYSMENU))
        return;
    HMENU sm = GetSystemMenu(w->h, FALSE);
    sysmenu_update(w, sm);
    memset(&T, 0, sizeof(T));
    T.owner = w;
    T.is_sys = 1;
    int f = user_frame_width(w->style, w->exstyle);
    int x = w->rc.left + f, y = w->rc.top + f + UM_CAPTION + 1;
    if (w->style & WS_MINIMIZE) {
        wmenu *m = menu_get(sm);
        layout_popup(m);
        y = w->rc.top - m->h;
    }
    if (!open_popup(sm, x, y, x, 0))
        return;
    if (by_key)
        set_sel(0, next_item(&T.lv[0], -1, 1));
    track_loop();
}

BOOL WINAPI TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved,
                           HWND h, const RECT *rc)
{
    (void)reserved; (void)rc;
    wwnd *w = user_wnd(h);
    wmenu *m = menu_get(menu);
    if (!w || !m || T.depth)
        return FALSE;
    memset(&T, 0, sizeof(T));
    T.owner = w;
    layout_popup(m);
    if (flags & TPM_CENTERALIGN) x -= m->w / 2;
    else if (flags & TPM_RIGHTALIGN) x -= m->w;
    if (!open_popup(menu, x, y, x, 0))
        return FALSE;
    track_loop();
    return TRUE;
}

/* ---------------------------------------------------------------- accelerators */

typedef struct {
    int     n;
    ACCEL_N a[1];
} waccel;

HACCEL WINAPI CreateAcceleratorTableN(const ACCEL_N *a, int n)
{
    if (!a || n <= 0)
        return 0;
    waccel *t = (waccel *)win_alloc(sizeof(waccel) + sizeof(ACCEL_N) * (size_t)(n - 1));
    if (!t)
        return 0;
    t->n = n;
    memcpy(t->a, a, sizeof(ACCEL_N) * (size_t)n);
    HACCEL h = win_handle_alloc(WOBJ_ACCEL, t);
    if (!h)
        win_free(t);
    return h;
}

BOOL WINAPI DestroyAcceleratorTableN(HACCEL h)
{
    waccel *t = (waccel *)win_handle_get(h, WOBJ_ACCEL);
    if (!t)
        return FALSE;
    win_free(t);
    win_handle_free(h);
    return TRUE;
}

int WINAPI TranslateAccelerator(HWND h, HACCEL ha, LPMSG msg)
{
    waccel *t = (waccel *)win_handle_get(ha, WOBJ_ACCEL);
    if (!t || !h)
        return 0;
    int key = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
    int chr = msg->message == WM_CHAR || msg->message == WM_SYSCHAR;
    if (!key && !chr)
        return 0;
    BYTE mods = (BYTE)(((GetKeyState(VK_SHIFT) & 0x8000) ? FSHIFT : 0) |
                       ((GetKeyState(VK_CONTROL) & 0x8000) ? FCONTROL : 0) |
                       ((GetKeyState(VK_MENU) & 0x8000) ? FALT : 0));
    for (int i = 0; i < t->n; i++) {
        const ACCEL_N *a = &t->a[i];
        if (a->fVirt & FVIRTKEY) {
            if (!key || a->key != (WORD)msg->wParam ||
                (a->fVirt & (FSHIFT | FCONTROL | FALT)) != mods)
                continue;
        } else {
            if (!chr || a->key != (WORD)(msg->wParam & 0xFF) ||
                ((a->fVirt & FALT) != 0) != (msg->message == WM_SYSCHAR))
                continue;
        }
        HMENU menu = GetMenu(h);
        UINT st = menu ? GetMenuState(menu, a->cmd, MF_BYCOMMAND) : (UINT)-1;
        if (st != (UINT)-1 && (st & (MF_GRAYED | MF_DISABLED)))
            return 1;            /* eaten, but no command for a disabled item */
        SendMessage(h, WM_COMMAND, a->cmd, MAKELPARAM(0, 1));
        return 1;
    }
    return 0;
}

/* the menu being tracked closes, nothing chosen */
void WINAPI EndMenu(void)
{
    if (T.depth)
        T.done = 1;
}

/* a window's system menu replaced by another */
BOOL WINAPI SetSystemMenu(HWND h, HMENU menu)
{
    wwnd *w = user_wnd(h);
    if (!w || !menu_get(menu))
        return FALSE;
    if (w->sysmenu && w->sysmenu != menu)
        DestroyMenu(w->sysmenu);
    w->sysmenu = menu;
    return TRUE;
}
