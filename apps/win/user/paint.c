/*
 * paint.c - update regions, window DCs, BeginPaint/EndPaint, scrolling.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

static wwnd *find_paint_in(wwnd *w, HTASK task)
{
    for (; w; w = w->next) {
        if (!(w->style & WS_VISIBLE) || (w->task && w->task != task))
            continue;
        if (w->update.n || w->ncpaint)
            return w;
        wwnd *c = find_paint_in(w->child, task);
        if (c)
            return c;
    }
    return NULL;
}

wwnd *user_find_paint(HTASK task)
{
    if (U.shutting_down || !U.desktop)
        return NULL;
    if (U.desktop->update.n || U.desktop->ncpaint)
        return U.desktop;
    return find_paint_in(U.desktop->child, task);
}

/* ---------------------------------------------------------------- DCs */

/* a window DC's visible area and origin, as the window is now */
static void dc_place(HDC h, wwnd *w, int whole, const wrgn *limit)
{
    wrgn vis;
    wrgn_init(&vis);
    user_vis_region(w, !whole, &vis);
    if (whole && !(w->style & WS_CLIPCHILDREN)) {
        /* window DC: frame plus client, children not excluded */
    }
    if (limit)
        wrgn_and(&vis, &vis, limit);
    gdi_set_dc_vis(h, &vis);
    wrgn_free(&vis);
    if (whole)
        gdi_set_dc_origin(h, w->rc.left, w->rc.top);
    else
        gdi_set_dc_origin(h, w->client.left, w->client.top);
    /* kept so as the windows change, unless kept to the area to paint */
    gdi_set_dc_follow(h, !limit, whole);
}

/* GDI's hook: a window DC used after the windows changed */
static void dc_follow(HDC h, HWND hwnd, int whole)
{
    wwnd *w = user_wnd(hwnd);
    if (w)
        dc_place(h, w, whole, NULL);
}

HDC user_get_dc(wwnd *w, int whole, const wrgn *limit)
{
    HDC h;
    int own = (w->cls->wc.style & CS_OWNDC) && !whole;
    gdi_dc_vis_hook = dc_follow;
    if (own && w->owndc) {
        h = w->owndc;
    } else {
        h = gdi_create_screen_dc(w->h);
        if (!h)
            return 0;
        if (own)
            w->owndc = h;
    }
    dc_place(h, w, whole, limit);
    if (!own)
        gdi_reset_dc(h);
    return h;
}

HDC WINAPI GetDC(HWND hwnd)
{
    wwnd *w = hwnd ? user_wnd(hwnd) : U.desktop;
    return w ? user_get_dc(w, 0, NULL) : 0;
}

HDC WINAPI GetWindowDC(HWND hwnd)
{
    wwnd *w = hwnd ? user_wnd(hwnd) : U.desktop;
    return w ? user_get_dc(w, 1, NULL) : 0;
}

int WINAPI ReleaseDC(HWND hwnd, HDC hdc)
{
    wwnd *w = user_wnd(hwnd);
    if (w && w->owndc == hdc) {
        /* kept: the whole of its visible area again (not just what
         * BeginPaint had to paint), as the window is when next used */
        gdi_set_dc_follow(hdc, 1, 0);
        win_sys.vis_gen++;
        return 1;
    }
    return DeleteDC(hdc) ? 1 : 0;
}

/* ---------------------------------------------------------------- paint */

HDC WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT ps)
{
    wwnd *w = user_wnd(hwnd);
    memset(ps, 0, sizeof(*ps));
    if (!w)
        return 0;
    user_caret_hide_for_paint(w);
    if (w->ncpaint) {
        w->ncpaint = 0;
        SendMessage(hwnd, WM_NCPAINT, 1, 0);
    }
    wrgn upd;
    wrgn_init(&upd);
    wrgn_copy(&upd, &w->update);
    wrgn_and_rect(&upd, &w->client);
    int erase = w->erase;
    wrgn_clear(&w->update);
    w->erase = 0;

    HDC dc = user_get_dc(w, 0, &upd);
    ps->hdc = dc;
    ps->rcPaint = upd.box;
    if (upd.n)
        OffsetRect(&ps->rcPaint, -w->client.left, -w->client.top);
    else
        SetRectEmpty(&ps->rcPaint);
    wrgn_free(&upd);
    ps->fErase = TRUE;
    if (erase && dc)
        ps->fErase = !SendMessage(hwnd, WM_ERASEBKGND, dc, 0);
    return dc;
}

void WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps)
{
    ReleaseDC(hwnd, ps->hdc);
    wwnd *w = user_wnd(hwnd);
    if (w)
        user_caret_restore(w);
}

/* ---------------------------------------------------------------- invalidation */

static void client_region(wwnd *w, const RECT *rc, wrgn *out)
{
    RECT r = w->client;
    if (rc) {
        r = *rc;
        OffsetRect(&r, w->client.left, w->client.top);
        IntersectRect(&r, &r, &w->client);
    }
    wrgn_set_rect(out, &r);
}

void WINAPI InvalidateRect(HWND hwnd, const RECT *rc, BOOL erase)
{
    if (!hwnd) {
        wrgn r;
        wrgn_init(&r);
        wrgn_set_rect(&r, &U.desktop->rc);
        user_invalidate_screen(&r);
        wrgn_free(&r);
        return;
    }
    wwnd *w = user_wnd(hwnd);
    if (!w)
        return;
    wrgn r;
    wrgn_init(&r);
    client_region(w, rc, &r);
    user_invalidate(w, &r, erase, 0);
    wrgn_free(&r);
}

void WINAPI InvalidateRgn(HWND hwnd, HRGN hr, BOOL erase)
{
    wwnd *w = user_wnd(hwnd);
    wrgn *g = gdi_region(hr);
    if (!w)
        return;
    if (!g) {
        InvalidateRect(hwnd, NULL, erase);
        return;
    }
    wrgn r;
    wrgn_init(&r);
    wrgn_copy(&r, g);
    wrgn_offset(&r, w->client.left, w->client.top);
    wrgn_and_rect(&r, &w->client);
    user_invalidate(w, &r, erase, 0);
    wrgn_free(&r);
}

void WINAPI ValidateRect(HWND hwnd, const RECT *rc)
{
    wwnd *w = user_wnd(hwnd);
    if (!w)
        return;
    if (!rc) {
        user_validate(w, NULL);
        return;
    }
    wrgn r;
    wrgn_init(&r);
    client_region(w, rc, &r);
    user_validate(w, &r);
    wrgn_free(&r);
}

void WINAPI ValidateRgn(HWND hwnd, HRGN hr)
{
    wwnd *w = user_wnd(hwnd);
    wrgn *g = gdi_region(hr);
    if (!w)
        return;
    if (!g) {
        user_validate(w, NULL);
        return;
    }
    wrgn r;
    wrgn_init(&r);
    wrgn_copy(&r, g);
    wrgn_offset(&r, w->client.left, w->client.top);
    user_validate(w, &r);
    wrgn_free(&r);
}

BOOL WINAPI GetUpdateRect(HWND hwnd, LPRECT rc, BOOL erase)
{
    wwnd *w = user_wnd(hwnd);
    if (!w)
        return FALSE;
    RECT b;
    if (!w->update.n || !IntersectRect(&b, &w->update.box, &w->client)) {
        if (rc)
            SetRectEmpty(rc);
        return FALSE;
    }
    if (rc) {
        *rc = b;
        OffsetRect(rc, -w->client.left, -w->client.top);
    }
    if (erase && w->erase) {
        HDC dc = user_get_dc(w, 0, &w->update);
        w->erase = !SendMessage(hwnd, WM_ERASEBKGND, dc, 0) ? 1 : 0;
        ReleaseDC(hwnd, dc);
    }
    return TRUE;
}

int WINAPI GetUpdateRgn(HWND hwnd, HRGN hr, BOOL erase)
{
    wwnd *w = user_wnd(hwnd);
    wrgn *g = gdi_region(hr);
    if (!w || !g)
        return ERROR;
    wrgn_copy(g, &w->update);
    wrgn_and_rect(g, &w->client);
    wrgn_offset(g, -w->client.left, -w->client.top);
    (void)erase;
    return wrgn_type(g);
}

/* the window's update region (client coordinates) out of the DC's clip */
int WINAPI ExcludeUpdateRgn(HDC hdc, HWND hwnd)
{
    wwnd *w = user_wnd(hwnd);
    if (!w)
        return ERROR;
    wrgn g;
    wrgn_init(&g);
    wrgn_copy(&g, &w->update);
    wrgn_and_rect(&g, &w->client);
    wrgn_offset(&g, -w->client.left, -w->client.top);
    for (int i = 0; i < g.n; i++)
        ExcludeClipRect(hdc, g.r[i].left, g.r[i].top, g.r[i].right, g.r[i].bottom);
    wrgn_free(&g);
    RECT rc;
    return GetClipBox(hdc, &rc);
}

BOOL WINAPI UpdateWindow(HWND hwnd)
{
    wwnd *w = user_wnd(hwnd);
    if (U.shutting_down || !w || !user_is_visible(w))
        return FALSE;
    if (w->update.n || w->ncpaint)
        SendMessage(hwnd, WM_PAINT, 0, 0);
    for (wwnd *c = w->child; c; c = c->next)
        if ((c->style & WS_VISIBLE) && (c->update.n || c->ncpaint))
            UpdateWindow(c->h);
    return TRUE;
}

BOOL WINAPI RedrawWindow(HWND hwnd, const RECT *rc, HRGN hr, UINT flags)
{
    wwnd *w = hwnd ? user_wnd(hwnd) : U.desktop;
    if (!w)
        return FALSE;
    wrgn r;
    wrgn_init(&r);
    wrgn *g = gdi_region(hr);
    if (g) {
        wrgn_copy(&r, g);
        wrgn_offset(&r, w->client.left, w->client.top);
    } else if (rc) {
        client_region(w, rc, &r);
    } else {
        wrgn_set_rect(&r, (flags & RDW_FRAME) ? &w->rc : &w->client);
    }
    if (flags & RDW_INVALIDATE)
        user_invalidate(w, &r, (flags & RDW_ERASE) != 0, (flags & RDW_ALLCHILDREN) != 0);
    if (flags & RDW_FRAME)
        w->ncpaint = 1;
    if (flags & RDW_VALIDATE)
        user_validate(w, &r);
    wrgn_free(&r);
    if (flags & (RDW_UPDATENOW | RDW_ERASENOW))
        UpdateWindow(w->h);
    return TRUE;
}

/* ---------------------------------------------------------------- scrolling */

/* The scroll of ScrollWindow and ScrollWindowEx: the client area (or rc
 * of it) moved, what is left uncovered in exposed (client coordinates),
 * invalidated if asked (erased if asked), the children moved if asked */
static void scroll(wwnd *w, int dx, int dy, const RECT *rc, const RECT *clip, int children,
                   int invalidate, int erase, wrgn *exposed)
{
    HWND hwnd = w->h;
    RECT area = w->client;
    if (rc) {
        area = *rc;
        OffsetRect(&area, w->client.left, w->client.top);
        IntersectRect(&area, &area, &w->client);
    }
    RECT lim = area;
    if (clip) {
        RECT c = *clip;
        OffsetRect(&c, w->client.left, w->client.top);
        IntersectRect(&lim, &lim, &c);
    }
    user_caret_hide_for_paint(w);

    wrgn vis, shifted, dst;
    wrgn_init(&vis);
    wrgn_init(&shifted);
    wrgn_init(&dst);
    user_vis_region(w, 1, &vis);
    wrgn_and_rect(&vis, &area);
    wrgn_copy(&shifted, &vis);
    wrgn_offset(&shifted, dx, dy);
    wrgn_and(&dst, &vis, &shifted);
    wrgn_and_rect(&dst, &lim);

    if (dst.n) {
        HDC dc = user_get_dc(w, 0, &dst);
        int x = area.left - w->client.left, y = area.top - w->client.top;
        BitBlt(dc, x + dx, y + dy, area.right - area.left, area.bottom - area.top,
               dc, x, y, SRCCOPY);
        ReleaseDC(hwnd, dc);
    }
    /* pending update moves with the contents */
    if (w->update.n) {
        wrgn moved;
        wrgn_init(&moved);
        wrgn_copy(&moved, &w->update);
        wrgn_and_rect(&moved, &area);
        wrgn_diff_rect(&w->update, &area);
        wrgn_offset(&moved, dx, dy);
        wrgn_and_rect(&moved, &area);
        wrgn_or(&w->update, &w->update, &moved);
        wrgn_free(&moved);
    }
    /* what did not come from visible bits must be repainted */
    wrgn need;
    wrgn_init(&need);
    wrgn_set_rect(&need, &lim);
    wrgn_diff(&need, &need, &dst);
    if (invalidate)
        user_invalidate(w, &need, erase, 0);
    if (exposed) {
        wrgn_copy(exposed, &need);
        wrgn_offset(exposed, -w->client.left, -w->client.top);
    }
    wrgn_free(&need);

    if (children) {
        for (wwnd *c = w->child; c; c = c->next) {
            RECT r = c->rc;
            OffsetRect(&r, dx - w->client.left, dy - w->client.top);
            SetWindowPos(c->h, 0, r.left, r.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        }
    }
    wrgn_free(&vis);
    wrgn_free(&shifted);
    wrgn_free(&dst);
    user_caret_restore(w);
}

void WINAPI ScrollWindow(HWND hwnd, int dx, int dy, const RECT *rc, const RECT *clip)
{
    wwnd *w = user_wnd(hwnd);
    if (w && (dx || dy))
        scroll(w, dx, dy, rc, clip, !rc, 1, 1, NULL);
}

int WINAPI ScrollWindowEx(HWND hwnd, int dx, int dy, const RECT *rc, const RECT *clip,
                          HRGN update, LPRECT update_rc, UINT flags)
{
    wwnd *w = user_wnd(hwnd);
    if (!w)
        return ERROR;
    wrgn exposed;
    wrgn_init(&exposed);
    if (dx || dy)
        scroll(w, dx, dy, rc, clip, (flags & SW_SCROLLCHILDREN) != 0,
               (flags & SW_INVALIDATE) != 0, (flags & SW_ERASE) != 0, &exposed);
    wrgn *g = update ? gdi_region(update) : NULL;
    if (g)
        wrgn_copy(g, &exposed);
    if (update_rc)
        *update_rc = exposed.box;
    int type = wrgn_type(&exposed);
    wrgn_free(&exposed);
    return type;
}

/* a DC's bits moved within rc (its logical coordinates) clipped to clip;
 * what is left uncovered in update and update_rc */
BOOL WINAPI ScrollDC(HDC hdc, int dx, int dy, const RECT *rc, const RECT *clip, HRGN update,
                     LPRECT update_rc)
{
    RECT area, lim;
    if (rc)
        area = *rc;
    else
        GetClipBox(hdc, &area);
    lim = area;
    if (clip)
        IntersectRect(&lim, &lim, clip);
    RECT dst = area;
    OffsetRect(&dst, dx, dy);
    IntersectRect(&dst, &dst, &lim);
    if (!IsRectEmpty(&dst))
        BitBlt(hdc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, hdc,
               dst.left - dx, dst.top - dy, SRCCOPY);
    wrgn need;
    wrgn_init(&need);
    wrgn_set_rect(&need, &lim);
    wrgn_diff_rect(&need, &dst);
    wrgn *g = update ? gdi_region(update) : NULL;
    if (g)
        wrgn_copy(g, &need);
    if (update_rc)
        *update_rc = need.box;
    wrgn_free(&need);
    return TRUE;
}

/* ---------------------------------------------------------------- the rest of 3.1's */

HWND WINAPI WindowFromDC(HDC hdc)
{
    wdc *dc = gdi_dc(hdc);
    return dc && dc->is_screen && dc->hwnd && user_wnd(dc->hwnd) ? dc->hwnd : 0;
}

/* GetDC's with a choice: the whole window, a region to keep to or keep
 * out of (the client's coordinates; the window's with DCX_WINDOW) */
HDC WINAPI GetDCEx(HWND h, HRGN clip, DWORD flags)
{
    wwnd *w = h ? user_wnd(h) : U.desktop;
    if (!w)
        return 0;
    HDC dc = (flags & DCX_WINDOW) ? GetWindowDC(w->h) : GetDC(w->h);
    if (dc && clip && (flags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))) {
        if (flags & DCX_INTERSECTRGN) {
            SelectClipRgn(dc, clip);
        } else {
            RECT r;
            HRGN all;
            GetClipBox(dc, &r);
            all = CreateRectRgnIndirect(&r);
            CombineRgn(all, all, clip, RGN_DIFF);
            SelectClipRgn(dc, all);
            DeleteObject(all);
        }
    }
    return dc;
}

/* the brush a control's parent gives it (WM_CTLCOLOR), DefWindowProc's
 * when it has none */
HBRUSH WINAPI GetControlBrush(HWND h, HDC hdc, UINT type)
{
    HWND parent = GetParent(h);
    HBRUSH b = parent ? (HBRUSH)SendMessage(parent, WM_CTLCOLOR, (WPARAM)hdc,
                                            MAKELPARAM(h, type)) : 0;
    if (!b)
        b = (HBRUSH)DefWindowProc(h, WM_CTLCOLOR, (WPARAM)hdc, MAKELPARAM(h, type));
    return b;
}

/* a brush, or a CTLCOLOR_ type for the control's (CTLCOLOR_MAX at most) */
static HBRUSH paint_brush(HWND parent, HWND h, HDC hdc, HBRUSH brush)
{
    (void)parent;
    return brush <= (HBRUSH)CTLCOLOR_MAX ? GetControlBrush(h, hdc, (UINT)brush) : brush;
}

void WINAPI PaintRect(HWND parent, HWND h, HDC hdc, HBRUSH brush, const RECT *rc)
{
    HBRUSH b = paint_brush(parent, h, hdc, brush);
    if (b && rc)
        FillRect(hdc, rc, b);
}

void WINAPI FillWindow(HWND parent, HWND h, HDC hdc, HBRUSH brush)
{
    RECT r;
    GetClientRect(h, &r);
    PaintRect(parent, h, hdc, brush, &r);
}

void WINAPI RepaintScreen(void)
{
    if (U.desktop)
        RedrawWindow(U.desktop->h, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
