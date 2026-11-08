/*
 * ctl_edit.c - EDIT window class: single and multi-line text editing.
 *
 * Text is one buffer with CR LF line breaks, as in Windows. Multi-line
 * controls without ES_AUTOHSCROLL wrap at the right edge; line starts are
 * recomputed after each change (edit controls hold little text here).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

void *user_ctl_data(HWND h);
void  user_ctl_set_data(HWND h, void *p);

#define MARGIN 2

typedef struct {
    char *text;
    int   len, cap, limit;
    int   anchor, caret;        /* selection = [min, max) */
    int   xscroll;              /* single line: pixels scrolled */
    int   top;                  /* multi line: first visible line */
    int  *lines;                /* line start offsets */
    int   nlines, lines_cap;
    int   modified, focus, tracking;
    char  pwchar;
    int   lh;                   /* line height */
    int   goal_x;               /* column memory for up/down */
} wedit;

static char clipboard[1024];

static DWORD estyle(HWND h) { return (DWORD)GetWindowLong(h, GWL_STYLE); }
static int multi(HWND h) { return (estyle(h) & ES_MULTILINE) != 0; }

static void notify(HWND h, int code)
{
    wwnd *w = user_wnd(h);
    if (w && w->parent && w->parent != U.desktop)
        SendMessage(w->parent->h, WM_COMMAND, w->id, MAKELPARAM(h, code));
}

static int smin(wedit *e) { return e->anchor < e->caret ? e->anchor : e->caret; }
static int smax(wedit *e) { return e->anchor > e->caret ? e->anchor : e->caret; }

static void text_rect(HWND h, RECT *r)
{
    GetClientRect(h, r);
    InflateRect(r, -MARGIN, multi(h) ? 0 : 0);
    if (!multi(h)) {
        r->left = MARGIN;
        r->right -= 0;
    }
}

static HDC edit_dc(HWND h)
{
    HDC dc = GetDC(h);
    SelectObject(dc, user_ctl_font(h));
    return dc;
}

static int width_of(HDC dc, wedit *e, const char *s, int n)
{
    if (e->pwchar) {
        SIZE c;
        GetTextExtentPoint(dc, &e->pwchar, 1, &c);
        return c.cx * n;
    }
    SIZE sz;
    GetTextExtentPoint(dc, s, n, &sz);
    return sz.cx;
}

/* ---------------------------------------------------------------- lines */

static void push_line(wedit *e, int start)
{
    if (e->nlines == e->lines_cap) {
        int nc = e->lines_cap ? e->lines_cap * 2 : 16;
        int *n = (int *)win_realloc(e->lines, sizeof(int) * (size_t)nc);
        if (!n)
            return;
        e->lines = n;
        e->lines_cap = nc;
    }
    e->lines[e->nlines++] = start;
}

static void relayout(HWND h, wedit *e)
{
    e->nlines = 0;
    if (!multi(h)) {
        push_line(e, 0);
        return;
    }
    int wrap = !(estyle(h) & ES_AUTOHSCROLL);
    RECT tr;
    text_rect(h, &tr);
    int width = tr.right - tr.left;
    HDC dc = edit_dc(h);
    int i = 0;
    push_line(e, 0);
    while (i < e->len) {
        int start = e->lines[e->nlines - 1];
        int end = i;
        while (end < e->len && e->text[end] != '\r' && e->text[end] != '\n')
            end++;
        if (wrap && width > 8 && width_of(dc, e, e->text + start, end - start) > width) {
            /* break at the last space that fits, or inside the word */
            int brk = -1;
            for (int k = start; k < end; k++) {
                if (e->text[k] == ' ') {
                    if (width_of(dc, e, e->text + start, k + 1 - start) <= width + 8)
                        brk = k + 1;
                    else
                        break;
                }
            }
            if (brk <= start) {
                brk = start + 1;
                while (brk < end && width_of(dc, e, e->text + start, brk + 1 - start) <= width)
                    brk++;
            }
            push_line(e, brk);
            i = brk;
            continue;
        }
        i = end;
        if (i < e->len && e->text[i] == '\r') i++;
        if (i < e->len && e->text[i] == '\n') i++;
        if (i > end)
            push_line(e, i);
        else
            break;
    }
    ReleaseDC(h, dc);
}

static int line_of(wedit *e, int pos)
{
    int l = 0;
    while (l + 1 < e->nlines && e->lines[l + 1] <= pos)
        l++;
    return l;
}

/* end of line l, without the line break */
static int line_end(wedit *e, int l)
{
    int end = l + 1 < e->nlines ? e->lines[l + 1] : e->len;
    while (end > e->lines[l] && (e->text[end - 1] == '\n' || e->text[end - 1] == '\r'))
        end--;
    return end;
}

static int visible_lines(HWND h, wedit *e)
{
    RECT r;
    GetClientRect(h, &r);
    int n = (r.bottom - r.top) / e->lh;
    return n > 0 ? n : 1;
}

/* ---------------------------------------------------------------- positions */

static void pos_to_xy(HWND h, HDC dc, wedit *e, int pos, int *x, int *y)
{
    RECT tr;
    text_rect(h, &tr);
    int l = line_of(e, pos);
    int s = e->lines[l];
    *x = tr.left + width_of(dc, e, e->text + s, pos - s) - (multi(h) ? 0 : e->xscroll);
    *y = multi(h) ? (l - e->top) * e->lh : (tr.bottom - tr.top - e->lh) / 2;
    if (!multi(h) && *y < 0)
        *y = 0;
}

static int xy_to_pos(HWND h, wedit *e, int x, int y)
{
    RECT tr;
    text_rect(h, &tr);
    int l = multi(h) ? e->top + (y < 0 ? -1 : y / e->lh) : 0;
    if (l < 0) l = 0;
    if (l >= e->nlines) l = e->nlines - 1;
    int s = e->lines[l], end = line_end(e, l);
    HDC dc = edit_dc(h);
    int px = x - tr.left + (multi(h) ? 0 : e->xscroll);
    int pos = s, acc = 0;
    while (pos < end) {
        int cw = width_of(dc, e, e->text + pos, 1);
        if (acc + cw / 2 > px)
            break;
        acc += cw;
        pos++;
    }
    ReleaseDC(h, dc);
    return pos;
}

/* ---------------------------------------------------------------- painting */

static void paint(HWND h, HDC dc, wedit *e)
{
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = user_ctl_color(h, dc, CTLCOLOR_EDIT);
    COLORREF fg = GetTextColor(dc), bk = GetBkColor(dc);
    FillRect(dc, &rc, bg);
    SelectObject(dc, user_ctl_font(h));
    SetBkMode(dc, OPAQUE);
    int s0 = smin(e), s1 = smax(e);
    int show_sel = e->focus || (estyle(h) & ES_NOHIDESEL);
    int first = multi(h) ? e->top : 0;
    int last = multi(h) ? e->top + visible_lines(h, e) : 0;
    if (!IsWindowEnabled(h))
        fg = GetSysColor(COLOR_GRAYTEXT);
    for (int l = first; l <= last && l < e->nlines; l++) {
        int s = e->lines[l], end = line_end(e, l);
        int x, y;
        pos_to_xy(h, dc, e, s, &x, &y);
        /* up to three runs: before, selected, after */
        int cuts[4] = { s, s, end, end };
        if (show_sel && s1 > s0) {
            cuts[1] = s0 < s ? s : (s0 > end ? end : s0);
            cuts[2] = s1 < s ? s : (s1 > end ? end : s1);
        } else {
            cuts[1] = cuts[2] = end;
        }
        for (int k = 0; k < 3; k++) {
            int a = cuts[k], b = cuts[k + 1];
            if (b <= a)
                continue;
            int sel = k == 1;
            SetTextColor(dc, sel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : fg);
            SetBkColor(dc, sel ? GetSysColor(COLOR_HIGHLIGHT) : bk);
            int xa = x + width_of(dc, e, e->text + s, a - s);
            if (e->pwchar) {
                char stars[128];
                int n = b - a > 127 ? 127 : b - a;
                memset(stars, e->pwchar, (size_t)n);
                TextOut(dc, xa, y, stars, n);
            } else {
                TextOut(dc, xa, y, e->text + a, b - a);
            }
        }
    }
}

static void redraw(HWND h)
{
    wedit *e = (wedit *)user_ctl_data(h);
    if (!e || !IsWindowVisible(h))
        return;
    HideCaret(h);
    HDC dc = GetDC(h);
    paint(h, dc, e);
    ReleaseDC(h, dc);
    ShowCaret(h);
}

static void update_scrollbar(HWND h, wedit *e)
{
    if (!multi(h) || !(estyle(h) & WS_VSCROLL))
        return;
    int maxtop = e->nlines - visible_lines(h, e);
    if (maxtop < 0) maxtop = 0;
    SetScrollRange(h, SB_VERT, 0, maxtop > 0 ? maxtop : 1, FALSE);
    SetScrollPos(h, SB_VERT, e->top, TRUE);
}

/* Keep the caret visible; returns 1 if the view moved. */
static int scroll_to_caret(HWND h, wedit *e)
{
    int moved = 0;
    if (multi(h)) {
        int l = line_of(e, e->caret), vis = visible_lines(h, e);
        if (l < e->top) { e->top = l; moved = 1; }
        if (l >= e->top + vis) { e->top = l - vis + 1; moved = 1; }
        if (moved)
            update_scrollbar(h, e);
        return moved;
    }
    RECT tr;
    text_rect(h, &tr);
    HDC dc = edit_dc(h);
    int x = width_of(dc, e, e->text, e->caret);
    ReleaseDC(h, dc);
    int w = tr.right - tr.left;
    if (x - e->xscroll < 0) { e->xscroll = x - w / 4; moved = 1; }
    if (x - e->xscroll > w - 1) { e->xscroll = x - w + w / 4; moved = 1; }
    if (e->xscroll < 0) e->xscroll = 0;
    return moved;
}

static void place_caret(HWND h, wedit *e)
{
    if (!e->focus)
        return;
    HDC dc = edit_dc(h);
    int x, y;
    pos_to_xy(h, dc, e, e->caret, &x, &y);
    ReleaseDC(h, dc);
    SetCaretPos(x, y);
}

static void after_move(HWND h, wedit *e, int full)
{
    if (scroll_to_caret(h, e))
        full = 1;
    (void)full;
    redraw(h);
    place_caret(h, e);
}

/* ---------------------------------------------------------------- editing */

static int ensure_cap(wedit *e, int n)
{
    if (n + 1 <= e->cap)
        return 1;
    int nc = e->cap ? e->cap : 64;
    while (nc < n + 1)
        nc *= 2;
    char *t = (char *)win_realloc(e->text, (size_t)nc);
    if (!t)
        return 0;
    e->text = t;
    e->cap = nc;
    return 1;
}

/* Replace the selection with s (n bytes); single-line drops line breaks. */
static int replace_sel(HWND h, wedit *e, const char *s, int n)
{
    char buf[256];
    if (!multi(h)) {
        int k = 0;
        for (int i = 0; i < n && k < (int)sizeof(buf); i++)
            if (s[i] != '\r' && s[i] != '\n')
                buf[k++] = s[i];
        s = buf;
        n = k;
    }
    DWORD st = estyle(h);
    int a = smin(e), b = smax(e);
    int nl = e->len - (b - a) + n;
    if (nl > e->limit) {
        n -= nl - e->limit;
        if (n < 0) n = 0;
        nl = e->len - (b - a) + n;
        notify(h, EN_MAXTEXT);
    }
    if (!ensure_cap(e, nl))
        return 0;
    memmove(e->text + a + n, e->text + b, (size_t)(e->len - b));
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (st & ES_UPPERCASE) c = (char)(uintptr_t)AnsiUpper((LPSTR)(uintptr_t)(unsigned char)c);
        if (st & ES_LOWERCASE) c = (char)(uintptr_t)AnsiLower((LPSTR)(uintptr_t)(unsigned char)c);
        e->text[a + i] = c;
    }
    e->len = nl;
    e->text[e->len] = 0;
    e->anchor = e->caret = a + n;
    e->modified = 1;
    relayout(h, e);
    update_scrollbar(h, e);
    return 1;
}

static void changed(HWND h, wedit *e)
{
    notify(h, EN_UPDATE);
    after_move(h, e, 1);
    notify(h, EN_CHANGE);
}

static void set_text(HWND h, wedit *e, const char *s)
{
    e->anchor = 0;
    e->caret = e->len;
    e->xscroll = 0;
    e->top = 0;
    replace_sel(h, e, s ? s : "", s ? lstrlen(s) : 0);
    e->anchor = e->caret = 0;
    e->modified = 0;
    scroll_to_caret(h, e);
}

static void copy_sel(wedit *e)
{
    int a = smin(e), b = smax(e);
    int n = b - a < (int)sizeof(clipboard) - 1 ? b - a : (int)sizeof(clipboard) - 1;
    memcpy(clipboard, e->text + a, (size_t)n);
    clipboard[n] = 0;
}

static int is_word(char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; }

static void key_down(HWND h, wedit *e, int vk)
{
    int shift = GetKeyState(VK_SHIFT) < 0;
    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int ro = (estyle(h) & ES_READONLY) != 0;
    int p = e->caret, l = line_of(e, p);
    int keep_goal = 0;
    switch (vk) {
    case VK_LEFT:
        if (!shift && smin(e) != smax(e)) p = smin(e);
        else if (p > 0) {
            p--;
            if (p > 0 && e->text[p] == '\n' && e->text[p - 1] == '\r') p--;
            if (ctrl) while (p > 0 && !(is_word(e->text[p]) && !is_word(e->text[p - 1]))) p--;
        }
        break;
    case VK_RIGHT:
        if (!shift && smin(e) != smax(e)) p = smax(e);
        else if (p < e->len) {
            if (e->text[p] == '\r' && p + 1 < e->len && e->text[p + 1] == '\n') p++;
            p++;
            if (ctrl) while (p < e->len && !(is_word(e->text[p]) && !is_word(e->text[p - 1]))) p++;
        }
        break;
    case VK_HOME:
        p = ctrl ? 0 : e->lines[l];
        break;
    case VK_END:
        p = ctrl ? e->len : line_end(e, l);
        break;
    case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: {
        if (!multi(h))
            return;
        HDC dc = edit_dc(h);
        int x, y;
        pos_to_xy(h, dc, e, p, &x, &y);
        ReleaseDC(h, dc);
        if (e->goal_x < 0)
            e->goal_x = x;
        int step = (vk == VK_UP || vk == VK_DOWN) ? 1 : visible_lines(h, e);
        int nl = (vk == VK_UP || vk == VK_PRIOR) ? l - step : l + step;
        if (nl < 0) nl = 0;
        if (nl >= e->nlines) nl = e->nlines - 1;
        if (vk == VK_PRIOR || vk == VK_NEXT) {
            e->top += nl - l;
            if (e->top < 0) e->top = 0;
        }
        p = xy_to_pos(h, e, e->goal_x, (nl - e->top) * e->lh);
        keep_goal = 1;
        break;
    }
    case VK_DELETE:
        if (ro)
            return;
        if (shift && smin(e) != smax(e)) {
            copy_sel(e);
        } else if (smin(e) == smax(e)) {
            if (p >= e->len)
                return;
            e->anchor = p;
            e->caret = p + ((e->text[p] == '\r' && p + 1 < e->len && e->text[p + 1] == '\n') ? 2 : 1);
        }
        replace_sel(h, e, "", 0);
        changed(h, e);
        return;
    case VK_INSERT:
        if (ctrl)
            copy_sel(e);
        else if (shift && !ro) {
            replace_sel(h, e, clipboard, lstrlen(clipboard));
            changed(h, e);
        }
        return;
    default:
        return;
    }
    if (!keep_goal)
        e->goal_x = -1;
    e->caret = p;
    if (!shift)
        e->anchor = p;
    after_move(h, e, 0);
}

static void char_in(HWND h, wedit *e, unsigned char c)
{
    int ro = (estyle(h) & ES_READONLY) != 0;
    switch (c) {
    case 3:             /* ^C */
        copy_sel(e);
        return;
    case 24:            /* ^X */
        if (ro) return;
        copy_sel(e);
        replace_sel(h, e, "", 0);
        changed(h, e);
        return;
    case 22:            /* ^V */
        if (ro) return;
        replace_sel(h, e, clipboard, lstrlen(clipboard));
        changed(h, e);
        return;
    case 8:
        if (ro) return;
        if (smin(e) == smax(e)) {
            if (e->caret == 0)
                return;
            e->anchor = e->caret - 1;
            if (e->anchor > 0 && e->text[e->anchor] == '\n' && e->text[e->anchor - 1] == '\r')
                e->anchor--;
        }
        replace_sel(h, e, "", 0);
        changed(h, e);
        return;
    case 13:
    case 10:
        if (!multi(h) || ro)
            return;
        replace_sel(h, e, "\r\n", 2);
        changed(h, e);
        return;
    case 9:
        if (!multi(h) || ro)
            return;
        replace_sel(h, e, "\t", 1);
        changed(h, e);
        return;
    default:
        if (c < 32 || ro)
            return;
        {
            char s = (char)c;
            replace_sel(h, e, &s, 1);
        }
        e->goal_x = -1;
        changed(h, e);
        return;
    }
}

/* ---------------------------------------------------------------- window procedure */

LRESULT CALLBACK user_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wedit *e = (wedit *)user_ctl_data(h);
    switch (msg) {
    case WM_CREATE: {
        e = (wedit *)win_alloc(sizeof(*e));
        if (!e)
            return -1;
        user_ctl_set_data(h, e);
        e->limit = 30000;
        e->goal_x = -1;
        e->lh = 16;
        if (estyle(h) & ES_PASSWORD)
            e->pwchar = '*';
        ensure_cap(e, 0);
        e->text[0] = 0;
        CREATESTRUCT *cs = (CREATESTRUCT *)(intptr_t)lp;
        set_text(h, e, cs ? cs->lpszName : "");
        return 0;
    }
    case WM_NCDESTROY:
        if (e) {
            win_free(e->text);
            win_free(e->lines);
            win_free(e);
        }
        user_ctl_set_data(h, NULL);
        RemoveProp(h, MAKEINTATOM(1));
        return 0;
    case WM_SIZE:
        if (e) {
            relayout(h, e);
            update_scrollbar(h, e);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (e)
            paint(h, dc, e);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFONT:
        SetProp(h, MAKEINTATOM(1), (HANDLE)wp);
        if (e) {
            relayout(h, e);
            if (lp)
                InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case WM_GETFONT:
        return GetProp(h, MAKEINTATOM(1));
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS |
               (multi(h) && (estyle(h) & ES_WANTRETURN) ? DLGC_WANTALLKEYS : 0);
    case WM_SETFOCUS:
        if (!e)
            return 0;
        e->focus = 1;
        CreateCaret(h, 0, 1, e->lh);
        place_caret(h, e);
        ShowCaret(h);
        redraw(h);
        notify(h, EN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        if (!e)
            return 0;
        e->focus = 0;
        DestroyCaret();
        redraw(h);
        notify(h, EN_KILLFOCUS);
        return 0;
    case WM_ENABLE:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_KEYDOWN:
        if (e)
            key_down(h, e, (int)wp);
        return 0;
    case WM_CHAR:
        if (e)
            char_in(h, e, (unsigned char)wp);
        return 0;
    case WM_LBUTTONDOWN:
        if (!e)
            return 0;
        if (GetFocus() != h)
            SetFocus(h);
        e->caret = xy_to_pos(h, e, (int16_t)LOWORD(lp), (int16_t)HIWORD(lp));
        if (!(wp & MK_SHIFT))
            e->anchor = e->caret;
        e->tracking = 1;
        e->goal_x = -1;
        SetCapture(h);
        after_move(h, e, 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (!e)
            return 0;
        {
            int p = xy_to_pos(h, e, (int16_t)LOWORD(lp), (int16_t)HIWORD(lp));
            int a = p, b = p;
            while (a > 0 && is_word(e->text[a - 1])) a--;
            while (b < e->len && is_word(e->text[b])) b++;
            e->anchor = a;
            e->caret = b;
            after_move(h, e, 0);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (e && e->tracking) {
            int p = xy_to_pos(h, e, (int16_t)LOWORD(lp), (int16_t)HIWORD(lp));
            if (p != e->caret) {
                e->caret = p;
                after_move(h, e, 0);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (e && e->tracking) {
            e->tracking = 0;
            ReleaseCapture();
        }
        return 0;
    case WM_VSCROLL: {
        if (!e || !multi(h))
            return 0;
        int vis = visible_lines(h, e), maxtop = e->nlines - vis;
        if (maxtop < 0) maxtop = 0;
        int t = e->top;
        switch (wp) {
        case SB_LINEUP: t--; break;
        case SB_LINEDOWN: t++; break;
        case SB_PAGEUP: t -= vis; break;
        case SB_PAGEDOWN: t += vis; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: t = (int16_t)LOWORD(lp); break;
        case SB_TOP: t = 0; break;
        case SB_BOTTOM: t = maxtop; break;
        default: return 0;
        }
        if (t > maxtop) t = maxtop;
        if (t < 0) t = 0;
        if (t != e->top) {
            e->top = t;
            update_scrollbar(h, e);
            redraw(h);
            place_caret(h, e);
        }
        return 0;
    }
    case WM_SETTEXT:
        if (!e)
            return 0;
        set_text(h, e, (LPCSTR)(intptr_t)lp);
        redraw(h);
        place_caret(h, e);
        notify(h, EN_CHANGE);
        return TRUE;
    case WM_GETTEXT:
        if (!e || !wp)
            return 0;
        lstrcpyn((LPSTR)(intptr_t)lp, e->text, (int)wp);
        return lstrlen((LPSTR)(intptr_t)lp);
    case WM_GETTEXTLENGTH:
        return e ? e->len : 0;
    case EM_GETSEL:
        return e ? MAKELONG(smin(e), smax(e)) : 0;
    case EM_SETSEL: {
        if (!e)
            return 0;
        int a = (int16_t)LOWORD(lp), b = (int16_t)HIWORD(lp);
        if (a < 0) { a = e->len; b = e->len; }
        if (b < 0 || b > e->len) b = e->len;
        if (a > e->len) a = e->len;
        e->anchor = a;
        e->caret = b;
        after_move(h, e, 0);
        return 0;
    }
    case EM_REPLACESEL:
        if (e) {
            LPCSTR s = (LPCSTR)(intptr_t)lp;
            replace_sel(h, e, s ? s : "", s ? lstrlen(s) : 0);
            changed(h, e);
        }
        return 0;
    case EM_LIMITTEXT:
        if (e)
            e->limit = wp ? (int)wp : 30000;
        return 0;
    case EM_SETREADONLY: {
        wwnd *w = user_wnd(h);
        if (wp) w->style |= ES_READONLY; else w->style &= ~(DWORD)ES_READONLY;
        return TRUE;
    }
    case EM_GETMODIFY:
        return e ? e->modified : 0;
    case EM_SETMODIFY:
        if (e) e->modified = wp != 0;
        return 0;
    case EM_GETLINECOUNT:
        return e ? e->nlines : 1;
    case EM_LINEINDEX: {
        if (!e) return -1;
        int l = (int)wp == -1 || wp == 0xFFFF ? line_of(e, e->caret) : (int)wp;
        return l < e->nlines ? e->lines[l] : -1;
    }
    case EM_LINELENGTH: {
        if (!e) return 0;
        int l = (int)wp == -1 || wp == 0xFFFF ? line_of(e, e->caret) : line_of(e, (int)wp);
        return line_end(e, l) - e->lines[l];
    }
    case EM_LINEFROMCHAR:
        if (!e) return 0;
        return line_of(e, ((int)wp == -1 || wp == 0xFFFF) ? e->caret : (int)wp);
    case EM_GETLINE: {
        if (!e || (int)wp >= e->nlines) return 0;
        char *buf = (char *)(intptr_t)lp;
        int max = (uint8_t)buf[0] | ((uint8_t)buf[1] << 8);
        int n = line_end(e, (int)wp) - e->lines[wp];
        if (n > max) n = max;
        memcpy(buf, e->text + e->lines[wp], (size_t)n);
        return n;
    }
    case EM_SETPASSWORDCHAR:
        if (e) {
            e->pwchar = (char)wp;
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case EM_LINESCROLL:
        if (e && multi(h)) {
            e->top += (int16_t)LOWORD(lp);
            if (e->top > e->nlines - 1) e->top = e->nlines - 1;
            if (e->top < 0) e->top = 0;
            update_scrollbar(h, e);
            redraw(h);
            place_caret(h, e);
        }
        return 0;
    case EM_GETRECT:
        if (lp)
            text_rect(h, (RECT *)(intptr_t)lp);
        return 0;
    case EM_CANUNDO:
        return FALSE;
    case EM_UNDO:
        return FALSE;
    }
    return DefWindowProc(h, msg, wp, lp);
}
