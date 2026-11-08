/*
 * nc.c - non-client area: frames, caption, scroll bars, hit testing,
 * caption buttons, moving and sizing, system commands.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

/* ---------------------------------------------------------------- 3D helpers */

static void fill(HDC dc, int l, int t, int r, int b, int color)
{
    RECT rc = { l, t, r, b };
    FillRect(dc, &rc, user_sys_brush(color));
}

static void hline(HDC dc, int x0, int x1, int y, int color)
{
    fill(dc, x0, y, x1, y + 1, color);
}

static void vline(HDC dc, int x, int y0, int y1, int color)
{
    fill(dc, x, y0, x + 1, y1, color);
}

/* Windows 3.x button face inside rc (no black outline): white top-left,
 * 2-pixel grey shadow bottom-right; pressed: grey top-left only. */
void user_draw_3d(HDC dc, const RECT *rc, int pressed)
{
    int l = rc->left, t = rc->top, r = rc->right, b = rc->bottom;
    fill(dc, l, t, r, b, COLOR_BTNFACE);
    if (pressed) {
        hline(dc, l, r, t, COLOR_BTNSHADOW);
        vline(dc, l, t, b, COLOR_BTNSHADOW);
        return;
    }
    hline(dc, l, r - 1, t, COLOR_BTNHIGHLIGHT);
    vline(dc, l, t, b - 1, COLOR_BTNHIGHLIGHT);
    hline(dc, l + 1, r, b - 1, COLOR_BTNSHADOW);
    vline(dc, r - 1, t + 1, b, COLOR_BTNSHADOW);
    if (r - l > 8 && b - t > 8) {
        hline(dc, l + 2, r - 1, b - 2, COLOR_BTNSHADOW);
        vline(dc, r - 2, t + 2, b - 1, COLOR_BTNSHADOW);
    }
}

/* Solid triangle centred in rc. dir: 0 up, 1 down, 2 left, 3 right. */
void user_draw_arrow(HDC dc, const RECT *rc, int dir, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    int cx = (rc->left + rc->right) / 2, cy = (rc->top + rc->bottom) / 2;
    int n = ((rc->right - rc->left) < (rc->bottom - rc->top) ?
             (rc->right - rc->left) : (rc->bottom - rc->top)) / 4;
    if (n < 2) n = 2;
    for (int i = 0; i < n; i++) {
        RECT r;
        switch (dir) {
        case 0: SetRect(&r, cx - i, cy - n / 2 + i, cx + i + 1, cy - n / 2 + i + 1); break;
        case 1: SetRect(&r, cx - i, cy + n / 2 - i, cx + i + 1, cy + n / 2 - i + 1); break;
        case 2: SetRect(&r, cx - n / 2 + i, cy - i, cx - n / 2 + i + 1, cy + i + 1); break;
        default: SetRect(&r, cx + n / 2 - i, cy - i, cx + n / 2 - i + 1, cy + i + 1); break;
        }
        FillRect(dc, &r, br);
    }
    DeleteObject(br);
}

/* ---------------------------------------------------------------- geometry */

/* Rectangles in window coordinates (relative to w->rc). */
typedef struct {
    int  f;                 /* frame width */
    RECT caption;           /* caption bar, empty if none */
    RECT sysbox, minbox, maxbox;
    RECT menu;              /* menu bar, black line below it */
    RECT vbar, hbar, corner;
} ncgeo;

static void nc_geo(const wwnd *w, ncgeo *g)
{
    memset(g, 0, sizeof(*g));
    int ww = w->rc.right - w->rc.left, wh = w->rc.bottom - w->rc.top;
    g->f = user_frame_width(w->style, w->exstyle);
    int f = g->f;
    if (user_has_caption(w->style)) {
        SetRect(&g->caption, f, f, ww - f, f + UM_CAPTION);
        int s = UM_CAPTION;
        if (w->style & WS_SYSMENU)
            SetRect(&g->sysbox, f, f, f + s, f + s);
        int x = ww - f;
        if (w->style & WS_MAXIMIZEBOX) {
            SetRect(&g->maxbox, x - s, f, x, f + s);
            x -= s + 1;
        }
        if (w->style & WS_MINIMIZEBOX)
            SetRect(&g->minbox, x - s, f, x, f + s);
    }
    if (user_menu_bar_height(w)) {
        int top = f + (user_has_caption(w->style) ? UM_CAPTION + 1 : 0);
        SetRect(&g->menu, f, top, ww - f, top + UM_MENU);
    }
    RECT c = w->client;
    OffsetRect(&c, -w->rc.left, -w->rc.top);
    /* Bars share their outer lines with the frame and the caption line. */
    int vs = (w->style & WS_VSCROLL) != 0, hs = (w->style & WS_HSCROLL) != 0;
    if (vs)
        SetRect(&g->vbar, c.right, c.top - 1, ww - f + 1, hs ? c.bottom + 1 : wh - f + 1);
    if (hs)
        SetRect(&g->hbar, c.left - 1, c.bottom, vs ? c.right + 1 : ww - f + 1, wh - f + 1);
    if (vs && hs)
        SetRect(&g->corner, c.right + 1, c.bottom + 1, ww - f, wh - f);
}

/* Scroll bar parts in window coordinates. */
typedef struct { RECT bar, up, down, track, thumb; int vert, len, tpos; } sbgeo;

/* SB_CTL: a SCROLLBAR control keeps its state in vs and fills its window */
static wscroll *bar_info(wwnd *w, int bar) { return bar == SB_HORZ ? &w->hs : &w->vs; }

static int ctl_vert(const wwnd *w) { return (w->style & SBS_VERT) != 0; }

static void sb_geo(wwnd *w, int bar, sbgeo *s)
{
    memset(s, 0, sizeof(*s));
    if (bar == SB_CTL) {
        s->vert = ctl_vert(w);
        SetRect(&s->bar, 0, 0, w->rc.right - w->rc.left, w->rc.bottom - w->rc.top);
    } else {
        ncgeo g;
        nc_geo(w, &g);
        s->vert = bar == SB_VERT;
        s->bar = s->vert ? g.vbar : g.hbar;
    }
    RECT b = s->bar;
    int a = UM_SCROLL;
    if (s->vert) {
        if (b.bottom - b.top < 2 * a)
            a = (b.bottom - b.top) / 2;
        SetRect(&s->up, b.left, b.top, b.right, b.top + a);
        SetRect(&s->down, b.left, b.bottom - a, b.right, b.bottom);
        SetRect(&s->track, b.left, s->up.bottom - 1, b.right, s->down.top + 1);
        s->len = s->track.bottom - s->track.top;
    } else {
        if (b.right - b.left < 2 * a)
            a = (b.right - b.left) / 2;
        SetRect(&s->up, b.left, b.top, b.left + a, b.bottom);
        SetRect(&s->down, b.right - a, b.top, b.right, b.bottom);
        SetRect(&s->track, s->up.right - 1, b.top, s->down.left + 1, b.bottom);
        s->len = s->track.right - s->track.left;
    }
    wscroll *sc = bar_info(w, bar);
    int range = sc->max - sc->min;
    int room = s->len - UM_SCROLL;
    if (range <= 0 || room <= 0) {
        SetRectEmpty(&s->thumb);
        return;
    }
    s->tpos = (int)((long)(sc->pos - sc->min) * room / range);
    if (s->vert)
        SetRect(&s->thumb, b.left, s->track.top + s->tpos, b.right, s->track.top + s->tpos + UM_SCROLL);
    else
        SetRect(&s->thumb, s->track.left + s->tpos, b.top, s->track.left + s->tpos + UM_SCROLL, b.bottom);
}

static void draw_button_box(HDC dc, const RECT *r, int pressed)
{
    HBRUSH frame = user_sys_brush(COLOR_WINDOWFRAME);
    FrameRect(dc, r, frame);
    RECT in = *r;
    InflateRect(&in, -1, -1);
    user_draw_3d(dc, &in, pressed);
}

static void draw_scrollbar(HDC dc, wwnd *w, int bar, int pressed_part)
{
    sbgeo s;
    sb_geo(w, bar, &s);
    if (IsRectEmpty(&s.bar))
        return;
    /* track */
    RECT t = s.track;
    FrameRect(dc, &t, user_sys_brush(COLOR_WINDOWFRAME));
    InflateRect(&t, -1, -1);
    if (s.vert) InflateRect(&t, 0, 1); else InflateRect(&t, 1, 0);
    FillRect(dc, &t, user_sys_brush(COLOR_SCROLLBAR));
    /* arrows */
    draw_button_box(dc, &s.up, pressed_part == 1);
    draw_button_box(dc, &s.down, pressed_part == 2);
    RECT a = s.up, b = s.down;
    if (pressed_part == 1) OffsetRect(&a, 1, 1);
    if (pressed_part == 2) OffsetRect(&b, 1, 1);
    user_draw_arrow(dc, &a, s.vert ? 0 : 2, RGB(0, 0, 0));
    user_draw_arrow(dc, &b, s.vert ? 1 : 3, RGB(0, 0, 0));
    if (!IsRectEmpty(&s.thumb))
        draw_button_box(dc, &s.thumb, 0);
}

/* ---------------------------------------------------------------- icons */

void user_draw_icon_id(HDC dc, int x, int y, int id);

static void paint_iconic(wwnd *w, HDC dc)
{
    int ww = w->rc.right - w->rc.left, wh = w->rc.bottom - w->rc.top;
    fill(dc, 0, 0, ww, wh, COLOR_BACKGROUND);
    int id = w->cls->wc.hIcon ? (int)(intptr_t)win_handle_get(w->cls->wc.hIcon, WOBJ_CURSOR) : 0;
    user_draw_icon_id(dc, (ww - UM_ICON) / 2, 2, id ? id : 101);
    RECT tr = { 0, UM_ICON + 4, ww, wh };
    int active = w == U.active;
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, GetSysColor(active ? COLOR_ACTIVECAPTION : COLOR_BACKGROUND));
    SetTextColor(dc, GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_CAPTIONTEXT));
    user_draw_text(dc, w->text, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
}

/* ---------------------------------------------------------------- painting */

static void paint_caption(HDC dc, wwnd *w, const ncgeo *g, int pressed)
{
    if (IsRectEmpty(&g->caption))
        return;
    int active = w->active_frame;
    RECT c = g->caption;
    FillRect(dc, &c, user_sys_brush(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
    hline(dc, c.left, c.right, c.bottom, COLOR_WINDOWFRAME);

    RECT text = c;
    if (!IsRectEmpty(&g->sysbox)) {
        RECT s = g->sysbox;
        fill(dc, s.left, s.top, s.right, s.bottom, COLOR_BTNFACE);
        vline(dc, s.right, s.top, s.bottom, COLOR_WINDOWFRAME);
        /* the "-" bar with its shadow */
        int cy = (s.top + s.bottom) / 2, x0 = s.left + 3, x1 = s.right - 3;
        fill(dc, x0 + 1, cy, x1 + 1, cy + 3, COLOR_BTNSHADOW);
        fill(dc, x0, cy - 1, x1, cy + 2, COLOR_WINDOWFRAME);
        fill(dc, x0 + 1, cy, x1 - 1, cy + 1, COLOR_BTNHIGHLIGHT);
        text.left = s.right + 1;
    }
    if (!IsRectEmpty(&g->maxbox)) {
        RECT m = g->maxbox;
        vline(dc, m.left - 1, m.top, m.bottom, COLOR_WINDOWFRAME);
        user_draw_3d(dc, &m, pressed == HTMAXBUTTON);
        RECT a = m;
        if (pressed == HTMAXBUTTON) OffsetRect(&a, 1, 1);
        if (w->style & WS_MAXIMIZE) {
            RECT u = a, d = a;
            u.bottom = (a.top + a.bottom) / 2 + 1;
            d.top = (a.top + a.bottom) / 2 - 1;
            user_draw_arrow(dc, &u, 0, RGB(0, 0, 0));
            user_draw_arrow(dc, &d, 1, RGB(0, 0, 0));
        } else {
            user_draw_arrow(dc, &a, 0, RGB(0, 0, 0));
        }
        text.right = m.left - 1;
    }
    if (!IsRectEmpty(&g->minbox)) {
        RECT m = g->minbox;
        vline(dc, m.left - 1, m.top, m.bottom, COLOR_WINDOWFRAME);
        user_draw_3d(dc, &m, pressed == HTMINBUTTON);
        RECT a = m;
        if (pressed == HTMINBUTTON) OffsetRect(&a, 1, 1);
        user_draw_arrow(dc, &a, 1, RGB(0, 0, 0));
        text.right = m.left - 1;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
    user_draw_text(dc, w->text, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void paint_frame(HDC dc, wwnd *w, const ncgeo *g)
{
    int ww = w->rc.right - w->rc.left, wh = w->rc.bottom - w->rc.top, f = g->f;
    if (!f)
        return;
    RECT all = { 0, 0, ww, wh };
    FrameRect(dc, &all, user_sys_brush(COLOR_WINDOWFRAME));
    if (f == 1)
        return;
    int active = w->active_frame;
    int band;
    if (w->style & WS_THICKFRAME)
        band = active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER;
    else
        band = active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION;
    fill(dc, 1, 1, ww - 1, f - 1, band);
    fill(dc, 1, wh - f + 1, ww - 1, wh - 1, band);
    fill(dc, 1, f - 1, f - 1, wh - f + 1, band);
    fill(dc, ww - f + 1, f - 1, ww - 1, wh - f + 1, band);
    RECT in = { f - 1, f - 1, ww - f + 1, wh - f + 1 };
    FrameRect(dc, &in, user_sys_brush(COLOR_WINDOWFRAME));
    if (w->style & WS_THICKFRAME) {
        /* sizing corner marks */
        int k = UM_CAPTION + f;
        hline(dc, 0, f, k, COLOR_WINDOWFRAME);
        hline(dc, ww - f, ww, k, COLOR_WINDOWFRAME);
        hline(dc, 0, f, wh - k - 1, COLOR_WINDOWFRAME);
        hline(dc, ww - f, ww, wh - k - 1, COLOR_WINDOWFRAME);
        vline(dc, k, 0, f, COLOR_WINDOWFRAME);
        vline(dc, k, wh - f, wh, COLOR_WINDOWFRAME);
        vline(dc, ww - k - 1, 0, f, COLOR_WINDOWFRAME);
        vline(dc, ww - k - 1, wh - f, wh, COLOR_WINDOWFRAME);
    }
}

static void nc_paint_ex(wwnd *w, int pressed)
{
    if (!user_is_visible(w))
        return;
    HDC dc = user_get_dc(w, 1, NULL);
    if (!dc)
        return;
    /* never touch the client area */
    RECT c = w->client;
    OffsetRect(&c, -w->rc.left, -w->rc.top);
    ExcludeClipRect(dc, c.left, c.top, c.right, c.bottom);
    if (w->style & WS_MINIMIZE) {
        paint_iconic(w, dc);
    } else {
        ncgeo g;
        nc_geo(w, &g);
        paint_frame(dc, w, &g);
        paint_caption(dc, w, &g, pressed);
        if (!IsRectEmpty(&g.menu)) {
            user_menu_paint_bar(w, dc, &g.menu);
            RECT l = { g.menu.left, g.menu.bottom, g.menu.right, g.menu.bottom + 1 };
            FillRect(dc, &l, user_sys_brush(COLOR_WINDOWFRAME));
        }
        if (w->style & WS_VSCROLL)
            draw_scrollbar(dc, w, SB_VERT, 0);
        if (w->style & WS_HSCROLL)
            draw_scrollbar(dc, w, SB_HORZ, 0);
        if (!IsRectEmpty(&g.corner))
            FillRect(dc, &g.corner, user_sys_brush(COLOR_BTNFACE));
    }
    ReleaseDC(w->h, dc);
}

void user_nc_paint(wwnd *w) { nc_paint_ex(w, 0); }

/* ---------------------------------------------------------------- hit test */

LRESULT user_nc_hittest(wwnd *w, int x, int y)
{
    POINT p = { x, y };
    if (!PtInRect(&w->rc, p))
        return HTNOWHERE;
    if (w->style & WS_MINIMIZE)
        return HTCAPTION;
    if (PtInRect(&w->client, p))
        return HTCLIENT;
    ncgeo g;
    nc_geo(w, &g);
    POINT q = { x - w->rc.left, y - w->rc.top };
    int ww = w->rc.right - w->rc.left, wh = w->rc.bottom - w->rc.top, f = g.f;

    if ((w->style & WS_THICKFRAME) && !(w->style & WS_MAXIMIZE)) {
        int k = UM_CAPTION + f;
        int left = q.x < f, right = q.x >= ww - f, top = q.y < f, bottom = q.y >= wh - f;
        if (left || right || top || bottom) {
            int nl = q.x < k, nr = q.x >= ww - k, nt = q.y < k, nb = q.y >= wh - k;
            if ((top && nl) || (left && nt)) return HTTOPLEFT;
            if ((top && nr) || (right && nt)) return HTTOPRIGHT;
            if ((bottom && nl) || (left && nb)) return HTBOTTOMLEFT;
            if ((bottom && nr) || (right && nb)) return HTBOTTOMRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (left) return HTLEFT;
            return HTRIGHT;
        }
    }
    if (PtInRect(&g.caption, q)) {
        if (PtInRect(&g.sysbox, q)) return HTSYSMENU;
        if (PtInRect(&g.maxbox, q)) return HTMAXBUTTON;
        if (PtInRect(&g.minbox, q)) return HTMINBUTTON;
        return HTCAPTION;
    }
    if (PtInRect(&g.menu, q)) return HTMENU;
    if (PtInRect(&g.vbar, q)) return HTVSCROLL;
    if (PtInRect(&g.hbar, q)) return HTHSCROLL;
    if (f && (q.x < f || q.y < f || q.x >= ww - f || q.y >= wh - f))
        return HTBORDER;
    return HTNOWHERE;
}

/* ---------------------------------------------------------------- tracking loops */

/* Next mouse or key message for a capture loop, screen coordinates.
 * Returns 0 when nothing arrived within the wait. */
static int track_msg(wwnd *w, MSG *m, POINT *pt)
{
    if (PeekMessage(m, 0, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE) ||
        PeekMessage(m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE) ||
        PeekMessage(m, 0, WM_NCMOUSEMOVE, WM_NCRBUTTONDBLCLK, PM_REMOVE)) {
        /* a release queued before the capture was taken arrives as a
         * non-client message: treat it like the client one */
        if (m->message >= WM_NCMOUSEMOVE && m->message <= WM_NCRBUTTONDBLCLK)
            m->message += WM_MOUSEMOVE - WM_NCMOUSEMOVE;
        pt->x = U.mouse_x;
        pt->y = U.mouse_y;
        (void)w;
        return 1;
    }
    if (win_sys.input)
        win_sys.input->idle();
    pt->x = U.mouse_x;
    pt->y = U.mouse_y;
    return 0;
}

static void xor_frame(HDC dc, const RECT *r, int t)
{
    PatBlt(dc, r->left, r->top, r->right - r->left, t, PATINVERT);
    PatBlt(dc, r->left, r->bottom - t, r->right - r->left, t, PATINVERT);
    PatBlt(dc, r->left, r->top + t, t, r->bottom - r->top - 2 * t, PATINVERT);
    PatBlt(dc, r->right - t, r->top + t, t, r->bottom - r->top - 2 * t, PATINVERT);
}

static HBRUSH gray_pattern(void)
{
    static const uint8_t bits[16] = { 0xAA, 0, 0x55, 0, 0xAA, 0, 0x55, 0,
                                      0xAA, 0, 0x55, 0, 0xAA, 0, 0x55, 0 };
    HBITMAP b = CreateBitmap(8, 8, 1, 1, bits);
    HBRUSH br = CreatePatternBrush(b);
    DeleteObject(b);
    return br;
}

/* SC_MOVE / SC_SIZE with an outline. dir: 0 move, else HT* edge code. */
static void move_size_loop(wwnd *w, int dir)
{
    RECT r = w->rc, start = w->rc;
    POINT p0 = { U.mouse_x, U.mouse_y };
    MINMAXINFO mm;
    memset(&mm, 0, sizeof(mm));
    mm.ptMinTrackSize.x = GetSystemMetrics(SM_CXMIN);
    mm.ptMinTrackSize.y = GetSystemMetrics(SM_CYMIN);
    mm.ptMaxTrackSize.x = 4096;
    mm.ptMaxTrackSize.y = 4096;
    SendMessage(w->h, WM_GETMINMAXINFO, 0, (LPARAM)(intptr_t)&mm);

    HDC dc = CreateDC("DISPLAY", NULL, NULL, NULL);
    HBRUSH br = gray_pattern();
    SelectObject(dc, br);
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkColor(dc, RGB(255, 255, 255));
    int t = (w->style & WS_THICKFRAME) ? UM_FRAME : 3;
    SetCapture(w->h);
    U.in_modal_move = 1;
    xor_frame(dc, &r, t);
    int done = 0, cancel = 0;
    while (!done) {
        MSG m;
        POINT pt;
        if (!track_msg(w, &m, &pt))
            continue;
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
            cancel = done = 1;
            continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
            done = 1;
            continue;
        }
        if (m.message == WM_LBUTTONUP) {
            done = 1;
            continue;
        }
        if (m.message != WM_MOUSEMOVE)
            continue;
        RECT n = start;
        int dx = pt.x - p0.x, dy = pt.y - p0.y;
        if (!dir) {
            OffsetRect(&n, dx, dy);
        } else {
            if (dir == HTLEFT || dir == HTTOPLEFT || dir == HTBOTTOMLEFT) n.left += dx;
            if (dir == HTRIGHT || dir == HTTOPRIGHT || dir == HTBOTTOMRIGHT) n.right += dx;
            if (dir == HTTOP || dir == HTTOPLEFT || dir == HTTOPRIGHT) n.top += dy;
            if (dir == HTBOTTOM || dir == HTBOTTOMLEFT || dir == HTBOTTOMRIGHT) n.bottom += dy;
            if (n.right - n.left < mm.ptMinTrackSize.x) {
                if (n.left != start.left) n.left = n.right - mm.ptMinTrackSize.x;
                else n.right = n.left + mm.ptMinTrackSize.x;
            }
            if (n.bottom - n.top < mm.ptMinTrackSize.y) {
                if (n.top != start.top) n.top = n.bottom - mm.ptMinTrackSize.y;
                else n.bottom = n.top + mm.ptMinTrackSize.y;
            }
        }
        if (!EqualRect(&n, &r)) {
            xor_frame(dc, &r, t);
            r = n;
            xor_frame(dc, &r, t);
        }
    }
    xor_frame(dc, &r, t);
    U.in_modal_move = 0;
    ReleaseCapture();
    DeleteDC(dc);
    DeleteObject(br);
    if (!cancel && !EqualRect(&r, &w->rc)) {
        if (w->parent && w->parent != U.desktop)
            OffsetRect(&r, -w->parent->client.left, -w->parent->client.top);
        SetWindowPos(w->h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* Caption button: pressed while the mouse is over it, acts on release. */
static int track_box(wwnd *w, int hit)
{
    ncgeo g;
    nc_geo(w, &g);
    RECT box = hit == HTMAXBUTTON ? g.maxbox : g.minbox;
    OffsetRect(&box, w->rc.left, w->rc.top);
    int in = 1;
    nc_paint_ex(w, hit);
    SetCapture(w->h);
    for (;;) {
        MSG m;
        POINT pt;
        if (!track_msg(w, &m, &pt))
            continue;
        if (m.message == WM_LBUTTONUP)
            break;
        if (m.message == WM_MOUSEMOVE) {
            int now = PtInRect(&box, pt);
            if (now != in) {
                in = now;
                nc_paint_ex(w, in ? hit : 0);
            }
        }
    }
    ReleaseCapture();
    nc_paint_ex(w, 0);
    return in;
}

/* ---------------------------------------------------------------- scroll bars */

void user_scroll_set(wwnd *w, int bar, int min, int max, int pos, int redraw)
{
    wscroll *s = bar_info(w, bar);
    s->min = min;
    s->max = max;
    if (pos < min) pos = min;
    if (pos > max) pos = max;
    s->pos = pos;
    if (bar == SB_CTL) {
        if (redraw && user_is_visible(w)) {
            HDC dc = user_get_dc(w, 1, NULL);
            draw_scrollbar(dc, w, SB_CTL, 0);
            ReleaseDC(w->h, dc);
        }
        return;
    }
    DWORD bit = bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
    DWORD want = min < max ? bit : 0;
    if ((w->style & bit) != want) {
        w->style = (w->style & ~bit) | want;
        RECT r = w->rc;
        if (w->parent && w->parent != U.desktop)
            OffsetRect(&r, -w->parent->client.left, -w->parent->client.top);
        SetWindowPos(w->h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return;
    }
    if (redraw && (w->style & bit) && user_is_visible(w)) {
        HDC dc = user_get_dc(w, 1, NULL);
        draw_scrollbar(dc, w, bar, 0);
        ReleaseDC(w->h, dc);
    }
}

int WINAPI SetScrollPos(HWND h, int bar, int pos, BOOL redraw)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return 0;
    wscroll *s = bar_info(w, bar);
    int old = s->pos;
    user_scroll_set(w, bar, s->min, s->max, pos, redraw);
    return old;
}

int WINAPI GetScrollPos(HWND h, int bar)
{
    wwnd *w = user_wnd(h);
    return w ? bar_info(w, bar)->pos : 0;
}

void WINAPI SetScrollRange(HWND h, int bar, int min, int max, BOOL redraw)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return;
    user_scroll_set(w, bar, min, max, bar_info(w, bar)->pos, redraw);
}

void WINAPI GetScrollRange(HWND h, int bar, LPINT min, LPINT max)
{
    wwnd *w = user_wnd(h);
    wscroll *s = w ? bar_info(w, bar) : NULL;
    if (min) *min = s ? s->min : 0;
    if (max) *max = s ? s->max : 0;
}

void WINAPI ShowScrollBar(HWND h, int bar, BOOL show)
{
    wwnd *w = user_wnd(h);
    if (!w)
        return;
    DWORD bits = bar == SB_VERT ? WS_VSCROLL : bar == SB_HORZ ? WS_HSCROLL :
                 bar == SB_BOTH ? (WS_VSCROLL | WS_HSCROLL) : 0;
    DWORD ns = show ? (w->style | bits) : (w->style & ~bits);
    if (ns == w->style)
        return;
    w->style = ns;
    RECT r = w->rc;
    if (w->parent && w->parent != U.desktop)
        OffsetRect(&r, -w->parent->client.left, -w->parent->client.top);
    SetWindowPos(h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void scroll_track(wwnd *w, int bar, int x, int y)
{
    sbgeo s;
    sb_geo(w, bar, &s);
    POINT q = { x - w->rc.left, y - w->rc.top };
    UINT msg = (bar == SB_CTL ? s.vert : bar == SB_VERT) ? WM_VSCROLL : WM_HSCROLL;
    /* the window itself, or for a control its parent with the control's
     * handle in HIWORD(lParam), as in Win16 */
    HWND to = w->h;
    WORD from = 0;
    if (bar == SB_CTL) {
        to = w->parent ? w->parent->h : w->h;
        from = w->h;
    }
    int code, part = 0;
    if (PtInRect(&s.up, q)) { code = SB_LINEUP; part = 1; }
    else if (PtInRect(&s.down, q)) { code = SB_LINEDOWN; part = 2; }
    else if (PtInRect(&s.thumb, q)) code = SB_THUMBTRACK;
    else if (s.vert ? q.y < s.thumb.top : q.x < s.thumb.left) code = SB_PAGEUP;
    else code = SB_PAGEDOWN;

    SetCapture(w->h);
    if (code == SB_THUMBTRACK) {
        int grab = s.vert ? q.y - s.thumb.top : q.x - s.thumb.left;
        wscroll *sc = bar_info(w, bar);
        int room = s.len - UM_SCROLL, pos = sc->pos;
        HDC dc = user_get_dc(w, 1, NULL);
        for (;;) {
            MSG m;
            POINT pt;
            if (!track_msg(w, &m, &pt))
                continue;
            if (m.message == WM_LBUTTONUP)
                break;
            if (m.message != WM_MOUSEMOVE || room <= 0)
                continue;
            int v = (s.vert ? pt.y - w->rc.top - s.track.top : pt.x - w->rc.left - s.track.left) - grab;
            if (v < 0) v = 0;
            if (v > room) v = room;
            int np = sc->min + (int)((long)v * (sc->max - sc->min) / room);
            if (np != pos) {
                pos = np;
                SendMessage(to, msg, SB_THUMBTRACK, MAKELPARAM(pos, from));
            }
        }
        ReleaseDC(w->h, dc);
        SendMessage(to, msg, SB_THUMBPOSITION, MAKELPARAM(pos, from));
    } else {
        HDC dc = user_get_dc(w, 1, NULL);
        if (part) {
            draw_scrollbar(dc, w, bar, part);
        }
        SendMessage(to, msg, (WPARAM)code, MAKELPARAM(0, from));
        uint32_t next = GetTickCount() + 400;
        for (;;) {
            MSG m;
            POINT pt;
            if (track_msg(w, &m, &pt) && m.message == WM_LBUTTONUP)
                break;
            if ((int32_t)(GetTickCount() - next) >= 0) {
                sbgeo s2;
                sb_geo(w, bar, &s2);
                POINT q2 = { pt.x - w->rc.left, pt.y - w->rc.top };
                int still = part == 1 ? PtInRect(&s2.up, q2) : part == 2 ? PtInRect(&s2.down, q2) :
                            PtInRect(&s2.track, q2) && !PtInRect(&s2.thumb, q2);
                if (still)
                    SendMessage(to, msg, (WPARAM)code, MAKELPARAM(0, from));
                next = GetTickCount() + 60;
            }
        }
        if (part)
            draw_scrollbar(dc, w, bar, 0);
        ReleaseDC(w->h, dc);
    }
    ReleaseCapture();
    SendMessage(to, msg, SB_ENDSCROLL, MAKELPARAM(0, from));
}

/* ---------------------------------------------------------------- NC mouse, sys commands */

void user_nc_button(wwnd *w, UINT msg, int hit, int x, int y)
{
    if (msg == WM_NCLBUTTONDBLCLK) {
        if (hit == HTSYSMENU && (w->style & WS_SYSMENU))
            SendMessage(w->h, WM_SYSCOMMAND, SC_CLOSE, MAKELPARAM(x, y));
        else if (hit == HTCAPTION && (w->style & WS_MINIMIZE))
            SendMessage(w->h, WM_SYSCOMMAND, SC_RESTORE, MAKELPARAM(x, y));
        else if (hit == HTCAPTION && (w->style & WS_MAXIMIZEBOX))
            SendMessage(w->h, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE,
                        MAKELPARAM(x, y));
        return;
    }
    if (msg != WM_NCLBUTTONDOWN)
        return;
    switch (hit) {
    case HTCAPTION:
        if (!(w->style & WS_MAXIMIZE))
            SendMessage(w->h, WM_SYSCOMMAND, SC_MOVE + HTCAPTION, MAKELPARAM(x, y));
        break;
    case HTMENU:
        user_menu_track_bar(w, x, y);
        break;
    case HTSYSMENU:
        user_menu_track_sys(w, 0);
        break;
    case HTMINBUTTON:
        if (track_box(w, hit))
            SendMessage(w->h, WM_SYSCOMMAND, SC_MINIMIZE, MAKELPARAM(x, y));
        break;
    case HTMAXBUTTON:
        if (track_box(w, hit))
            SendMessage(w->h, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE,
                        MAKELPARAM(x, y));
        break;
    case HTVSCROLL:
        scroll_track(w, SB_VERT, x, y);
        break;
    case HTHSCROLL:
        scroll_track(w, SB_HORZ, x, y);
        break;
    case HTLEFT: case HTRIGHT: case HTTOP: case HTBOTTOM:
    case HTTOPLEFT: case HTTOPRIGHT: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
        SendMessage(w->h, WM_SYSCOMMAND, SC_SIZE + (hit - HTLEFT + 1), MAKELPARAM(x, y));
        break;
    default:
        break;
    }
}

void user_syscommand(wwnd *w, UINT cmd, int x, int y)
{
    (void)x; (void)y;
    switch (cmd & 0xFFF0) {
    case SC_MOVE:
        move_size_loop(w, 0);
        break;
    case SC_SIZE:
        if (cmd & 0x0F)
            move_size_loop(w, (int)(cmd & 0x0F) - 1 + HTLEFT);
        break;
    case SC_MINIMIZE:
        ShowWindow(w->h, SW_MINIMIZE);
        break;
    case SC_MAXIMIZE:
        ShowWindow(w->h, SW_SHOWMAXIMIZED);
        break;
    case SC_RESTORE:
        if ((w->style & WS_MINIMIZE) && !SendMessage(w->h, WM_QUERYOPEN, 0, 0))
            break;
        ShowWindow(w->h, SW_RESTORE);
        break;
    case SC_CLOSE:
        SendMessage(w->h, WM_CLOSE, 0, 0);
        break;
    case SC_KEYMENU:
        user_menu_key(w, (int)(unsigned char)x);
        break;
    case SC_NEXTWINDOW: {
        /* bottom-most activatable top-level window comes to the front */
        wwnd *pick = NULL;
        for (wwnd *t = U.desktop->child; t; t = t->next)
            if (t != U.active && user_is_visible(t) && !(t->style & WS_DISABLED) && !t->owner)
                pick = t;
        if (pick)
            SetActiveWindow(pick->h);
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------- SCROLLBAR control */

void user_scroll_ctl_track(wwnd *w, int x, int y) { scroll_track(w, SB_CTL, x, y); }

LRESULT CALLBACK user_scrollbar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wwnd *w = user_wnd(h);
    switch (msg) {
    case WM_CREATE:
        w->vs.min = 0;
        w->vs.max = 100;
        w->vs.pos = 0;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        HDC dc = user_get_dc(w, 1, NULL);
        draw_scrollbar(dc, w, SB_CTL, 0);
        ReleaseDC(h, dc);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT p = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
        ClientToScreen(h, &p);
        scroll_track(w, SB_CTL, p.x, p.y);
        return 0;
    }
    case WM_GETDLGCODE:
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}
