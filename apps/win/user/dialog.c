/*
 * dialog.c - dialog manager, MessageBox, USER start-up.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

void *user_ctl_data(HWND h);
void  user_ctl_set_data(HWND h, void *p);

/* Win16 dialog extra bytes: DWL_MSGRESULT, DWL_DLGPROC, DWL_USER, then
 * our state pointer. */
#define DLG_STATE_OFS  12
#define DLG_EXTRA      (12 + (int)sizeof(void *))

typedef struct {
    DLGPROC proc;
    int     ended, result;
    int     defid;
    HWND    focus;          /* restored on activation */
    HFONT   font;
} wdlg;

static wdlg *dlg_state(HWND h)
{
    wwnd *w = user_wnd(h);
    wdlg *d = NULL;
    if (w && w->extra && w->cb_extra >= DLG_EXTRA)
        memcpy(&d, w->extra + DLG_STATE_OFS, sizeof(d));
    return d;
}

#define DLG_BASE_X 8
#define DLG_BASE_Y 16

DWORD WINAPI GetDialogBaseUnits(void) { return (DWORD)MAKELONG(DLG_BASE_X, DLG_BASE_Y); }

static int du_x(int v) { return v * DLG_BASE_X / 4; }
static int du_y(int v) { return v * DLG_BASE_Y / 8; }

/* ---------------------------------------------------------------- navigation */

static int ctl_code(HWND c, MSG *m)
{
    return (int)SendMessage(c, WM_GETDLGCODE, 0, (LPARAM)(intptr_t)m);
}

static int tabbable(HWND c)
{
    DWORD s = (DWORD)GetWindowLong(c, GWL_STYLE);
    return (s & WS_TABSTOP) && (s & WS_VISIBLE) && !(s & WS_DISABLED);
}

/* the direct child of dlg that contains c */
static HWND dlg_child(HWND dlg, HWND c)
{
    while (c && GetParent(c) != dlg) {
        c = GetParent(c);
        if (c && !(GetWindowLong(c, GWL_STYLE) & WS_CHILD))
            return 0;
    }
    return c;
}

HWND WINAPI GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL prev)
{
    HWND first = GetWindow(dlg, GW_CHILD);
    if (!first)
        return 0;
    ctl = ctl ? dlg_child(dlg, ctl) : 0;
    HWND c = ctl ? ctl : (prev ? first : GetWindow(first, GW_HWNDLAST));
    for (int guard = 0; guard < 512; guard++) {
        if (prev) {
            c = GetWindow(c, GW_HWNDPREV);
            if (!c)
                c = GetWindow(first, GW_HWNDLAST);
        } else {
            c = GetWindow(c, GW_HWNDNEXT);
            if (!c)
                c = first;
        }
        if (tabbable(c) || c == ctl)
            return c;
    }
    return ctl;
}

HWND WINAPI GetNextDlgGroupItem(HWND dlg, HWND ctl, BOOL prev)
{
    ctl = dlg_child(dlg, ctl);
    if (!ctl)
        return 0;
    /* group: from the WS_GROUP item at or before ctl to before the next one */
    HWND start = ctl;
    while (!(GetWindowLong(start, GWL_STYLE) & WS_GROUP)) {
        HWND p = GetWindow(start, GW_HWNDPREV);
        if (!p)
            break;
        start = p;
    }
    HWND c = ctl;
    for (int guard = 0; guard < 512; guard++) {
        if (prev) {
            if (c == start || (GetWindowLong(c, GWL_STYLE) & WS_GROUP)) {
                /* wrap to the last item of the group */
                HWND n = start, last = start;
                while ((n = GetWindow(n, GW_HWNDNEXT)) && !(GetWindowLong(n, GWL_STYLE) & WS_GROUP))
                    last = n;
                c = last;
            } else {
                c = GetWindow(c, GW_HWNDPREV);
            }
        } else {
            HWND n = GetWindow(c, GW_HWNDNEXT);
            c = (!n || (GetWindowLong(n, GWL_STYLE) & WS_GROUP)) ? start : n;
        }
        DWORD s = (DWORD)GetWindowLong(c, GWL_STYLE);
        if (c == ctl || ((s & WS_VISIBLE) && !(s & WS_DISABLED)))
            return c;
    }
    return ctl;
}

static void set_default_button(HWND dlg, HWND focus)
{
    wdlg *d = dlg_state(dlg);
    HWND def = d && d->defid ? GetDlgItem(dlg, d->defid) : 0;
    int fcode = focus ? ctl_code(focus, NULL) : 0;
    HWND want = (fcode & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) ? focus : def;
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        int code = ctl_code(c, NULL);
        if ((code & DLGC_DEFPUSHBUTTON) && c != want)
            SendMessage(c, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
        else if ((code & DLGC_UNDEFPUSHBUTTON) && c == want)
            SendMessage(c, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
    }
}

/* Focus moved to w (by SetFocus, a mouse click, the application): the
 * default push button follows it, as the dialog manager does it, so that
 * Enter always presses the button drawn as the default one. */
void user_dlg_focus_changed(wwnd *w)
{
    wwnd *p = w;
    while (p && !p->dialog)
        p = (p->style & WS_CHILD) ? p->parent : NULL;
    if (!p || p == w)
        return;
    set_default_button(p->h, w->h);
}

static void dlg_focus(HWND dlg, HWND c)
{
    if (!c)
        return;
    if (ctl_code(c, NULL) & DLGC_HASSETSEL)
        SendMessage(c, EM_SETSEL, 0, MAKELPARAM(0, -1));
    SetFocus(c);
    set_default_button(dlg, c);
    wdlg *d = dlg_state(dlg);
    if (d)
        d->focus = c;
}

/* ---------------------------------------------------------------- dialog procedure */

static int direct_result(UINT msg)
{
    return msg == WM_CTLCOLOR || msg == WM_INITDIALOG || msg == WM_GETDLGCODE;
}

LRESULT WINAPI DefDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wdlg *d = dlg_state(h);
    switch (msg) {
    case WM_ERASEBKGND: {
        HBRUSH b = (HBRUSH)SendMessage(h, WM_CTLCOLOR, wp, MAKELPARAM(h, CTLCOLOR_DLG));
        if (!b)
            b = user_sys_brush(COLOR_WINDOW);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, b);
        return 1;
    }
    case WM_CLOSE:
        PostMessage(h, WM_COMMAND, IDCANCEL, MAKELPARAM(GetDlgItem(h, IDCANCEL), 0));
        return 0;
    case WM_ACTIVATE:
        if (d && LOWORD(wp) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(h, f))
                d->focus = f;
        } else if (d) {
            HWND f = d->focus && IsWindow(d->focus) ? d->focus : GetNextDlgTabItem(h, 0, FALSE);
            if (f)
                dlg_focus(h, f);
            else
                SetFocus(h);
        }
        return 0;
    case WM_SETFOCUS:
        if (d) {
            HWND f = d->focus && IsWindow(d->focus) ? d->focus : GetNextDlgTabItem(h, 0, FALSE);
            if (f && f != h)
                dlg_focus(h, f);
        }
        return 0;
    case WM_NEXTDLGCTL: {
        HWND c = lp ? (HWND)wp : GetNextDlgTabItem(h, GetFocus(), wp != 0);
        dlg_focus(h, c);
        return 0;
    }
    case DM_GETDEFID:
        return d && d->defid ? MAKELONG(d->defid, DC_HASDEFID) : 0;
    case DM_SETDEFID:
        if (d) {
            d->defid = (int)wp;
            set_default_button(h, GetFocus());
        }
        return TRUE;
    case WM_NCDESTROY:
        if (d) {
            win_free(d);
            wwnd *w = user_wnd(h);
            memset(w->extra + DLG_STATE_OFS, 0, sizeof(void *));
        }
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

LRESULT CALLBACK user_dialog_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    wdlg *d = dlg_state(h);
    if (d && d->proc && msg != WM_NCDESTROY) {
        BOOL r = d->proc(h, msg, wp, lp);
        if (direct_result(msg) && r)
            return r;
        if (r)
            return GetWindowLong(h, DWL_MSGRESULT);
    } else if (d && d->proc && msg == WM_NCDESTROY) {
        d->proc(h, msg, wp, lp);
    }
    return DefDlgProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- creation */

static LPCSTR ctl_class(LPCSTR k)
{
    if (!IS_INTRESOURCE(k))
        return k;
    switch ((uintptr_t)k) {
    case 0x80: return "BUTTON";
    case 0x81: return "EDIT";
    case 0x82: return "STATIC";
    case 0x83: return "LISTBOX";
    case 0x84: return "SCROLLBAR";
    case 0x85: return "COMBOBOX";
    default: return "STATIC";
    }
}

/* dialog units to pixels, as the dialog manager places its controls */
BOOL WINAPI MapDialogRect(HWND h, LPRECT rc)
{
    (void)h;
    rc->left = du_x(rc->left);
    rc->right = du_x(rc->right);
    rc->top = du_y(rc->top);
    rc->bottom = du_y(rc->bottom);
    return TRUE;
}

HWND WINAPI CreateDialogIndirectN(HINSTANCE inst, const DLGTEMPLATE_N *t,
                                  const DLGITEM_N *items, HWND owner, DLGPROC proc)
{
    DWORD style = t->style & ~WS_VISIBLE;
    DWORD ex = (t->style & DS_MODALFRAME) ? WS_EX_DLGMODALFRAME : 0;
    if (t->caption && !(style & WS_CHILD))
        style |= WS_CAPTION;
    RECT rc = { 0, 0, du_x(t->cx), du_y(t->cy) };
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int x = du_x(t->x) + rc.left, y = du_y(t->y) + rc.top;
    wwnd *ow = user_wnd(owner);
    if (ow && !(style & WS_CHILD) && !(t->style & DS_ABSALIGN)) {
        x += ow->client.left;
        y += ow->client.top;
    }
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    if (!(style & WS_CHILD)) {
        if (x + ww > sw) x = sw - ww;
        if (y + wh > sh) y = sh - wh;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    LPCSTR klass = t->klass ? t->klass : "#32770";
    HWND h = CreateWindowEx(ex, klass, t->caption ? t->caption : "", style,
                            x, y, ww, wh, owner, 0, inst, NULL);
    if (!h)
        return 0;
    wwnd *w = user_wnd(h);
    wdlg *d = (wdlg *)win_alloc(sizeof(*d));
    if (!d || !w->extra || w->cb_extra < DLG_EXTRA) {
        win_free(d);
        DestroyWindow(h);
        return 0;
    }
    d->proc = proc;
    d->defid = IDOK;
    memcpy(w->extra + DLG_STATE_OFS, &d, sizeof(d));
    w->dialog = 1;

    HWND first_tab = 0;
    for (int i = 0; i < t->cdit; i++) {
        const DLGITEM_N *it = &items[i];
        LPCSTR k = ctl_class(it->klass);
        DWORD cs = it->style | WS_CHILD;
        DWORD cex = WS_EX_NOPARENTNOTIFY;
        HWND c = CreateWindowEx(cex, k, it->text ? it->text : "", cs,
                                du_x(it->x), du_y(it->y), du_x(it->cx), du_y(it->cy),
                                h, (HMENU)it->id, inst, NULL);
        if (!c)
            continue;
        if (!lstrcmpi(k, "BUTTON") && (cs & 0x0F) == BS_DEFPUSHBUTTON)
            d->defid = it->id;
        if (!first_tab && tabbable(c))
            first_tab = c;
    }
    if (proc && proc(h, WM_INITDIALOG, first_tab, 0) && first_tab)
        dlg_focus(h, first_tab);
    else if (first_tab && !d->focus)
        d->focus = first_tab;
    if (t->style & WS_VISIBLE)
        ShowWindow(h, SW_SHOWNORMAL);
    return h;
}

BOOL WINAPI EndDialog(HWND h, int result)
{
    wdlg *d = dlg_state(h);
    if (!d)
        return FALSE;
    d->ended = 1;
    d->result = result;
    return TRUE;
}

int WINAPI DialogBoxIndirectN(HINSTANCE inst, const DLGTEMPLATE_N *t,
                              const DLGITEM_N *items, HWND owner, DLGPROC proc)
{
    DLGTEMPLATE_N tt = *t;
    tt.style &= ~WS_VISIBLE;
    HWND h = CreateDialogIndirectN(inst, &tt, items, owner, proc);
    if (!h)
        return -1;
    wwnd *ow = owner ? user_toplevel(user_wnd(owner)) : NULL;
    int owner_was_enabled = ow && IsWindowEnabled(ow->h);
    wdlg *d = dlg_state(h);
    if (!d->ended) {
        ShowWindow(h, SW_SHOWNORMAL);
        if (ow)
            EnableWindow(ow->h, FALSE);
    }
    while (IsWindow(h) && !d->ended) {
        MSG m;
        if (!GetMessage(&m, 0, 0, 0)) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (!IsDialogMessage(h, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    int result = IsWindow(h) && d ? d->result : -1;
    if (ow && owner_was_enabled)
        EnableWindow(ow->h, TRUE);
    if (IsWindow(h))
        DestroyWindow(h);
    if (ow && IsWindow(ow->h))
        SetActiveWindow(ow->h);
    return result;
}

/* ---------------------------------------------------------------- IsDialogMessage */

static HWND find_mnemonic(HWND dlg, unsigned char ch)
{
    char up[2] = { (char)ch, 0 };
    AnsiUpper(up);
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!(GetWindowLong(c, GWL_STYLE) & WS_VISIBLE) || (GetWindowLong(c, GWL_STYLE) & WS_DISABLED))
            continue;
        char text[128];
        GetWindowText(c, text, sizeof(text));
        for (char *p = text; *p; p++) {
            if (p[0] == '&' && p[1] && p[1] != '&') {
                char m[2] = { p[1], 0 };
                AnsiUpper(m);
                if (m[0] == up[0])
                    return c;
                break;
            }
            if (p[0] == '&' && p[1] == '&')
                p++;
        }
    }
    return 0;
}

static void click_button(HWND c)
{
    SendMessage(c, BM_SETSTATE, TRUE, 0);
    SendMessage(c, BM_SETSTATE, FALSE, 0);
    wwnd *w = user_wnd(c);
    int t = (int)(w->style & 0x0F);
    if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE || t == BS_AUTORADIOBUTTON) {
        /* let the button do its own toggling */
        SendMessage(c, WM_KEYDOWN, VK_SPACE, 0);
        SendMessage(c, WM_KEYUP, VK_SPACE, 0);
        return;
    }
    SendMessage(w->parent->h, WM_COMMAND, w->id, MAKELPARAM(c, BN_CLICKED));
}

BOOL WINAPI IsDialogMessage(HWND dlg, LPMSG m)
{
    if (!m->hwnd || (m->hwnd != dlg && !IsChild(dlg, m->hwnd)))
        return FALSE;
    HWND f = m->hwnd;
    int code = (f != dlg) ? ctl_code(f, m) : 0;

    if (m->message == WM_KEYDOWN && !(code & DLGC_WANTALLKEYS)) {
        switch (m->wParam) {
        case VK_TAB: {
            if (code & DLGC_WANTTAB)
                break;
            HWND n = GetNextDlgTabItem(dlg, f == dlg ? 0 : f, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            /* entering a radio group lands on its checked button */
            if (n && (ctl_code(n, NULL) & DLGC_RADIOBUTTON) && !SendMessage(n, BM_GETCHECK, 0, 0)) {
                HWND r = n;
                do {
                    r = GetNextDlgGroupItem(dlg, r, FALSE);
                    if (r && (ctl_code(r, NULL) & DLGC_RADIOBUTTON) && SendMessage(r, BM_GETCHECK, 0, 0)) {
                        n = r;
                        break;
                    }
                } while (r && r != n);
            }
            dlg_focus(dlg, n);
            return TRUE;
        }
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
            if (code & DLGC_WANTARROWS)
                break;
            if (f == dlg)
                return TRUE;
            HWND n = GetNextDlgGroupItem(dlg, f, m->wParam == VK_LEFT || m->wParam == VK_UP);
            if (n && n != f) {
                dlg_focus(dlg, n);
                if ((ctl_code(n, NULL) & DLGC_RADIOBUTTON) &&
                    (GetWindowLong(n, GWL_STYLE) & 0x0F) == BS_AUTORADIOBUTTON)
                    click_button(n);
            }
            return TRUE;
        }
        case VK_RETURN: {
            if (code & DLGC_DEFPUSHBUTTON) {
                click_button(f);
                return TRUE;
            }
            DWORD def = (DWORD)SendMessage(dlg, DM_GETDEFID, 0, 0);
            int id = HIWORD(def) == DC_HASDEFID ? LOWORD(def) : IDOK;
            HWND b = GetDlgItem(dlg, id);
            if (!b || IsWindowEnabled(b))
                SendMessage(dlg, WM_COMMAND, (WPARAM)id, MAKELPARAM(b, BN_CLICKED));
            return TRUE;
        }
        case VK_ESCAPE: {
            HWND b = GetDlgItem(dlg, IDCANCEL);
            if (!b || IsWindowEnabled(b))
                SendMessage(dlg, WM_COMMAND, IDCANCEL, MAKELPARAM(b, BN_CLICKED));
            return TRUE;
        }
        default:
            break;
        }
    }
    if (m->message == WM_SYSCHAR || (m->message == WM_CHAR && !(code & DLGC_WANTCHARS))) {
        HWND c = find_mnemonic(dlg, (unsigned char)m->wParam);
        if (c) {
            int cc = ctl_code(c, NULL);
            if (cc & DLGC_STATIC) {
                HWND n = GetNextDlgTabItem(dlg, c, FALSE);
                dlg_focus(dlg, n);
            } else if (cc & DLGC_BUTTON) {
                dlg_focus(dlg, c);
                click_button(c);
            } else {
                dlg_focus(dlg, c);
            }
            return TRUE;
        }
        /* Alt and a key that is no control's: the window's menu, as in
         * Windows (dispatched below) */
    }
    TranslateMessage(m);
    DispatchMessage(m);
    return TRUE;
}

/* ---------------------------------------------------------------- item helpers */

HWND WINAPI GetDlgItem(HWND dlg, int id)
{
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (GetDlgCtrlID(c) == id)
            return c;
    return 0;
}

LRESULT WINAPI SendDlgItemMessage(HWND dlg, int id, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND c = GetDlgItem(dlg, id);
    return c ? SendMessage(c, msg, wp, lp) : 0;
}

void WINAPI SetDlgItemText(HWND dlg, int id, LPCSTR text) { SetWindowText(GetDlgItem(dlg, id), text); }

int WINAPI GetDlgItemText(HWND dlg, int id, LPSTR buf, int max)
{
    return GetWindowText(GetDlgItem(dlg, id), buf, max);
}

void WINAPI SetDlgItemInt(HWND dlg, int id, UINT v, BOOL sign)
{
    char buf[16];
    wsprintf(buf, sign ? "%d" : "%u", v);
    SetDlgItemText(dlg, id, buf);
}

UINT WINAPI GetDlgItemInt(HWND dlg, int id, BOOL *ok, BOOL sign)
{
    char buf[16];
    if (ok)
        *ok = FALSE;
    if (!GetDlgItemText(dlg, id, buf, sizeof(buf)))
        return 0;
    const char *p = buf;
    while (*p == ' ')
        p++;
    int neg = 0;
    if (sign && *p == '-') {
        neg = 1;
        p++;
    }
    if (*p < '0' || *p > '9')
        return 0;
    long v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    while (*p == ' ')
        p++;
    if (*p)
        return 0;
    if (ok)
        *ok = TRUE;
    return (UINT)(neg ? -v : v);
}

void WINAPI CheckDlgButton(HWND dlg, int id, UINT check) { SendDlgItemMessage(dlg, id, BM_SETCHECK, check, 0); }
UINT WINAPI IsDlgButtonChecked(HWND dlg, int id) { return (UINT)SendDlgItemMessage(dlg, id, BM_GETCHECK, 0, 0); }

void WINAPI CheckRadioButton(HWND dlg, int first, int last, int check)
{
    for (int id = first; id <= last; id++)
        CheckDlgButton(dlg, id, id == check);
}

/* ---------------------------------------------------------------- MessageBox */

static BOOL CALLBACK msgbox_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && wp >= IDOK && wp <= IDNO) {
        EndDialog(h, (int)wp);
        return TRUE;
    }
    return FALSE;
}

int WINAPI MessageBox(HWND owner, LPCSTR text, LPCSTR caption, UINT type)
{
    static const struct { int n; int ids[3]; const char *names[3]; } sets[6] = {
        { 1, { IDOK }, { "OK" } },
        { 2, { IDOK, IDCANCEL }, { "OK", "Cancel" } },
        { 3, { IDABORT, IDRETRY, IDIGNORE }, { "&Abort", "&Retry", "&Ignore" } },
        { 3, { IDYES, IDNO, IDCANCEL }, { "&Yes", "&No", "Cancel" } },
        { 2, { IDYES, IDNO }, { "&Yes", "&No" } },
        { 2, { IDRETRY, IDCANCEL }, { "&Retry", "Cancel" } },
    };
    int set = (int)(type & 0x0F);
    if (set > 5)
        set = 0;
    int icon = (int)(type & 0xF0) >> 4;           /* 1 hand, 2 question, 3 excl, 4 asterisk */
    int def = (int)(type & 0x0F00) >> 8;
    if (def >= sets[set].n)
        def = 0;

    /* measure the text in pixels, then convert to dialog units */
    HDC dc = GetDC(0);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    RECT tr = { 0, 0, win_sys.screen.width * 2 / 3, 0 };
    user_draw_text(dc, text ? text : "", -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS);
    ReleaseDC(0, dc);
    int tw = (tr.right + 1) * 4 / DLG_BASE_X + 1, th = (tr.bottom + 1) * 8 / DLG_BASE_Y + 1;
    int ix = 0, iw = icon ? 24 : 0;
    int bw = 40, gap = 8, nb = sets[set].n;
    int width = ix + iw + tw + 16;
    int bwidth = nb * bw + (nb - 1) * gap + 16;
    if (width < bwidth) width = bwidth;
    if (width < 80) width = 80;
    int tx = 8 + ix + iw;
    int textH = th < (icon ? 16 : 8) ? (icon ? 16 : 8) : th;
    int height = 8 + textH + 8 + 14 + 6;

    DLGITEM_N items[5];
    int n = 0;
    char iname[8] = "#3251x";
    if (icon) {
        iname[5] = (char)('2' + icon);       /* #32513.. */
        items[n++] = (DLGITEM_N){ 8, 8, 16, 16, -1, SS_ICON | WS_VISIBLE, "STATIC", iname };
    }
    items[n++] = (DLGITEM_N){ tx, 8, tw, textH, 0xFFFF,
                              SS_LEFT | SS_NOPREFIX | WS_VISIBLE, "STATIC", text ? text : "" };
    int bx = (width - (nb * bw + (nb - 1) * gap)) / 2;
    for (int i = 0; i < nb; i++)
        items[n++] = (DLGITEM_N){ bx + i * (bw + gap), 8 + textH + 8, bw, 14, sets[set].ids[i],
                                  (DWORD)((i == def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) |
                                          WS_TABSTOP | WS_VISIBLE | (i == 0 ? WS_GROUP : 0)),
                                  "BUTTON", sets[set].names[i] };
    int sw = win_sys.screen.width * 4 / DLG_BASE_X, sh = win_sys.screen.height * 8 / DLG_BASE_Y;
    DLGTEMPLATE_N t = { WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_ABSALIGN,
                        (BYTE)n, (sw - width) / 2, (sh - height) / 2, width, height,
                        NULL, NULL, caption ? caption : "Error", 0, NULL };
    /* Without an owner, the active window is the box's when it is the
     * calling task's (the box stays over it, which waits for it); another
     * task's window is not disabled for it (Windows: no owner, nothing
     * disabled). */
    wwnd *act = U.active;
    HWND own = owner ? owner : (act && act->task == task_current()->h ? act->h : 0);
    int r = DialogBoxIndirectN(0, &t, items, own, msgbox_proc);
    if (r == IDCANCEL && !(set == 1 || set == 3 || set == 5))
        r = sets[set].ids[0];
    return r;
}

/* ---------------------------------------------------------------- USER start-up */

int user_init(void)
{
    memset(&U, 0, sizeof(U));
    win_files_init();               /* disk errors fail, before anything reads a file */
    U.keys[VK_NUMLOCK] = 1;
    U.mouse_x = win_sys.screen.width / 2;
    U.mouse_y = win_sys.screen.height / 2;
    user_init_sysmet();
    user_load_colors();
    user_load_desktop();
    user_init_windows();
    HCURSOR arrow = LoadCursor(0, IDC_ARROW), ibeam = LoadCursor(0, IDC_IBEAM);
    int ex = (int)sizeof(void *);
    user_register_builtin("BUTTON", user_button_proc, CS_DBLCLKS, ex, arrow, 0);
    user_register_builtin("STATIC", user_static_proc, 0, ex, arrow, 0);
    user_register_builtin("EDIT", user_edit_proc, CS_DBLCLKS, ex, ibeam, 0);
    user_register_builtin("LISTBOX", user_listbox_proc, CS_DBLCLKS, ex, arrow, 0);
    user_register_builtin("SCROLLBAR", user_scrollbar_proc, CS_DBLCLKS, 0, arrow, 0);
    user_register_builtin("COMBOBOX", user_combo_proc, CS_DBLCLKS, ex, arrow, 0);
    user_register_builtin("#32770", user_dialog_proc, 0, DLG_EXTRA, arrow, 0);
    user_register_builtin("#32768", user_menu_popup_proc, 0, 0, arrow, 0);
    user_register_builtin("MDICLIENT", user_mdiclient_proc, 0, (int)sizeof(void *), arrow, 0);
    return 0;
}

/* ---------------------------------------------------------------- DlgDirList */

/*
 * DlgDirList, DlgDirListComboBox: the list box (combo box) id filled with
 * the files of spec ("C:\\DIR\\*.TXT", "*.*"), as attr asks: DDL_DIRECTORY
 * the subdirectories as "[name]", DDL_DRIVES the drives as "[-c-]",
 * DDL_EXCLUSIVE no files; the current directory first made spec's path's
 * (spec left the pattern), the static control idStatic showing it.
 */
static int dir_list(HWND dlg, LPSTR spec, int id, int id_static, UINT attr, int combo)
{
    char pat[80] = "*.*";
    if (spec && *spec) {
        int n = lstrlen(spec), cut = n;
        while (cut > 0 && spec[cut - 1] != '\\' && spec[cut - 1] != ':')
            cut--;
        if (cut < n)
            lstrcpyn(pat, spec + cut, sizeof(pat));
        else
            lstrcpyn(pat, "*.*", sizeof(pat));
        if (cut) {                          /* its path: the current directory */
            char path[128];
            lstrcpyn(path, spec, cut + 1 < (int)sizeof(path) ? cut + 1 : (int)sizeof(path));
            if (cut >= 2 && path[1] == ':')
                win_file_drive((path[0] | 0x20) - 'a');
            int l = lstrlen(path);
            if (l > 1 && path[l - 1] == '\\' && path[l - 2] != ':')
                path[l - 1] = 0;
            if (!(l == 2 && path[1] == ':') && !win_file_chdir(path))
                return FALSE;
        }
        lstrcpy(spec, pat);
    }
    UINT reset = combo ? CB_RESETCONTENT : LB_RESETCONTENT, add = combo ? CB_ADDSTRING : LB_ADDSTRING;
    if (id) {
        HWND box = GetDlgItem(dlg, id);
        if (!box)
            return FALSE;
        SendMessage(box, reset, 0, 0);
        static char names[2048];            /* big, off the task stack */
        if (!(attr & DDL_EXCLUSIVE) || (attr & ~(DDL_EXCLUSIVE | DDL_DIRECTORY | DDL_DRIVES))) {
            int n = WinDirListAttrN(pat, 0, names, sizeof(names));
            for (const char *p = names; n-- > 0 && *p; p += lstrlen(p) + 1) {
                char low[64];
                lstrcpyn(low, p, sizeof(low));
                AnsiLower(low);
                SendMessage(box, add, 0, (LPARAM)(LPSTR)low);
            }
        }
        if (attr & DDL_DIRECTORY) {
            int n = WinDirListAttrN("*.*", 0x10, names, sizeof(names));
            char cwd[128];
            if (GetCurrentDirectoryN(cwd, sizeof(cwd)) > 3)
                SendMessage(box, add, 0, (LPARAM)(LPSTR)"[..]");
            for (const char *p = names; n-- > 0 && *p; p += lstrlen(p) + 1) {
                char e[66];
                e[0] = '[';
                lstrcpyn(e + 1, p, 63);
                lstrcat(e, "]");
                AnsiLower(e);
                SendMessage(box, add, 0, (LPARAM)(LPSTR)e);
            }
        }
        if (attr & DDL_DRIVES)
            for (int d = 0; d < 26; d++)
                if (GetDriveType(d)) {
                    char e[8];
                    wsprintf(e, "[-%c-]", 'a' + d);
                    SendMessage(box, add, 0, (LPARAM)(LPSTR)e);
                }
    }
    if (id_static) {
        char cwd[128];
        GetCurrentDirectoryN(cwd, sizeof(cwd));
        AnsiLower(cwd);
        SetDlgItemText(dlg, id_static, cwd);
    }
    return TRUE;
}

int WINAPI DlgDirList(HWND dlg, LPSTR spec, int id, int id_static, UINT attr)
{
    return dir_list(dlg, spec, id, id_static, attr, 0);
}

int WINAPI DlgDirListComboBox(HWND dlg, LPSTR spec, int id, int id_static, UINT attr)
{
    return dir_list(dlg, spec, id, id_static, attr, 1);
}

/* DlgDirSelect(Ex), DlgDirSelectComboBox(Ex): the selection; a directory's
 * ("[name]": "name\\") or a drive's ("[-c-]": "c:") TRUE, a file's FALSE */
static BOOL dir_select(HWND dlg, LPSTR out, int size, int id, int combo)
{
    HWND box = GetDlgItem(dlg, id);
    char t[80];
    int i = box ? (int)SendMessage(box, combo ? CB_GETCURSEL : LB_GETCURSEL, 0, 0) : -1;
    if (i < 0 || !out || size < 1)
        return FALSE;
    t[0] = 0;
    SendMessage(box, combo ? CB_GETLBTEXT : LB_GETTEXT, (WPARAM)i, (LPARAM)(LPSTR)t);
    int n = lstrlen(t);
    BOOL dir = FALSE;
    if (n >= 5 && t[0] == '[' && t[1] == '-' && t[n - 2] == '-' && t[n - 1] == ']') {
        t[0] = t[2];
        t[1] = ':';
        t[2] = 0;
        dir = TRUE;
    } else if (n >= 2 && t[0] == '[' && t[n - 1] == ']') {
        memmove(t, t + 1, (size_t)n - 2);
        t[n - 2] = '\\';
        t[n - 1] = 0;
        dir = TRUE;
    }
    lstrcpyn(out, t, size);
    return dir;
}

BOOL WINAPI DlgDirSelect(HWND dlg, LPSTR out, int id) { return dir_select(dlg, out, 128, id, 0); }
BOOL WINAPI DlgDirSelectEx(HWND dlg, LPSTR out, int size, int id)
{
    return dir_select(dlg, out, size, id, 0);
}
BOOL WINAPI DlgDirSelectComboBox(HWND dlg, LPSTR out, int id)
{
    return dir_select(dlg, out, 128, id, 1);
}
BOOL WINAPI DlgDirSelectComboBoxEx(HWND dlg, LPSTR out, int size, int id)
{
    return dir_select(dlg, out, size, id, 1);
}
