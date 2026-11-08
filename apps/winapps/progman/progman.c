/*
 * progman.c - PROGMAN.EXE, the Program Manager: the shell WINN.EXE starts
 * when SYSTEM.INI says nothing else, as in Windows 3.x. Closing it ends the
 * Windows session.
 *
 * Program groups and their items live in PROGMAN.INI (text, not the .GRP
 * files of Windows 3.x):
 *
 *   [Settings]
 *   Window=left top right bottom maximized
 *   MinOnRun=0
 *   [Group1]
 *   Name=Main
 *   Item1=Demo,DEMO.EXE
 *
 * On the first start the Main group gets every Windows application found
 * next to WINN.EXE. Groups are shown one under another in the main window
 * (no MDI yet); items use the standard application icon (no resources yet).
 */
#include "windows.h"

#define INI         "PROGMAN.INI"
#define MAXGROUPS   8
#define MAXITEMS    24

#define IDM_OPEN    101
#define IDM_NEWITEM 102
#define IDM_NEWGRP  103
#define IDM_PROPS   104
#define IDM_DELETE  105
#define IDM_RUN     106
#define IDM_EXIT    107
#define IDM_MINRUN  108
#define IDM_ABOUT   109

#define IDC_DESC    201
#define IDC_CMD     202
#define IDC_MIN     203

typedef struct {
    char desc[32];
    char cmd[80];
} pm_item;

typedef struct {
    char    name[32];
    int     n;
    pm_item it[MAXITEMS];
    int     top, height;            /* layout, in document coordinates */
} pm_group;

static HINSTANCE hinst;
static HWND      hmain;
static pm_group  groups[MAXGROUPS];
static int       ngroups, saved_groups;
static int       cur_g, cur_i = -1;
static int       min_on_run;
static int       line_h = 16, cell_w = 80, cell_h = 80, hdr_h = 20, cols = 1;
static int       scroll_y, doc_h;

/* ---------------------------------------------------------------- PROGMAN.INI */

static void split_item(const char *v, pm_item *it)
{
    int i = 0;
    while (v[i] && v[i] != ',' && i < (int)sizeof(it->desc) - 1) {
        it->desc[i] = v[i];
        i++;
    }
    it->desc[i] = 0;
    while (v[i] && v[i] != ',')
        i++;
    if (v[i] == ',')
        i++;
    while (v[i] == ' ')
        i++;
    lstrcpyn(it->cmd, v + i, sizeof(it->cmd));
}

static void load_groups(void)
{
    char sec[16], key[16], val[128];
    ngroups = 0;
    for (int g = 0; g < MAXGROUPS; g++) {
        wsprintf(sec, "Group%d", g + 1);
        if (!GetPrivateProfileString(sec, "Name", "", val, sizeof(val), INI))
            break;
        pm_group *gr = &groups[ngroups++];
        lstrcpyn(gr->name, val, sizeof(gr->name));
        gr->n = 0;
        for (int i = 0; i < MAXITEMS; i++) {
            wsprintf(key, "Item%d", i + 1);
            if (!GetPrivateProfileString(sec, key, "", val, sizeof(val), INI))
                break;
            split_item(val, &gr->it[gr->n++]);
        }
    }
    saved_groups = ngroups;
}

static void save_groups(void)
{
    char sec[16], key[16], val[128];
    for (int g = ngroups; g < saved_groups; g++) {     /* groups deleted */
        wsprintf(sec, "Group%d", g + 1);
        WritePrivateProfileString(sec, NULL, NULL, INI);
    }
    for (int g = 0; g < ngroups; g++) {
        pm_group *gr = &groups[g];
        wsprintf(sec, "Group%d", g + 1);
        WritePrivateProfileString(sec, NULL, NULL, INI);
        WritePrivateProfileString(sec, "Name", gr->name, INI);
        for (int i = 0; i < gr->n; i++) {
            wsprintf(key, "Item%d", i + 1);
            wsprintf(val, "%s,%s", gr->it[i].desc, gr->it[i].cmd);
            WritePrivateProfileString(sec, key, val, INI);
        }
    }
    saved_groups = ngroups;
}

static void save_settings(void)
{
    char val[48];
    wsprintf(val, "%d", min_on_run);
    WritePrivateProfileString("Settings", "MinOnRun", val, INI);
    if (!IsIconic(hmain)) {
        RECT r;
        GetWindowRect(hmain, &r);
        int max = IsZoomed(hmain) ? 1 : 0;
        if (max) {                  /* keep the restored position */
            char old[48];
            GetPrivateProfileString("Settings", "Window", "", old, sizeof(old), INI);
            int k = lstrlen(old);
            while (k && old[k - 1] != ' ')
                k--;
            if (k) {
                old[k] = '1';
                old[k + 1] = 0;
                WritePrivateProfileString("Settings", "Window", old, INI);
                return;
            }
        }
        wsprintf(val, "%d %d %d %d %d", r.left, r.top, r.right, r.bottom, max);
        WritePrivateProfileString("Settings", "Window", val, INI);
    }
}

/* "DEMO.EXE" -> "Demo" */
static void title_of(const char *file, char *out, int size)
{
    const char *b = file;
    for (const char *p = file; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    int n = 0;
    for (; b[n] && b[n] != '.' && n < size - 1; n++) {
        char c = b[n];
        if (n && c >= 'A' && c <= 'Z')
            c += 32;
        else if (!n && c >= 'a' && c <= 'z')
            c -= 32;
        out[n] = c;
    }
    out[n] = 0;
}

/* first start: a Main group with the Windows applications next to WINN.EXE */
static void make_main_group(void)
{
    char dir[128], spec[144], path[160];
    static char names[1024];
    GetWindowsDirectory(dir, sizeof(dir));
    int n = lstrlen(dir);
    if (n && dir[n - 1] != '\\')
        lstrcat(dir, "\\");
    wsprintf(spec, "%s*.EXE", dir);
    pm_group *gr = &groups[0];
    lstrcpy(gr->name, "Main");
    gr->n = 0;
    ngroups = 1;
    WinDirListN(spec, names, sizeof(names));
    for (const char *p = names; *p && gr->n < MAXITEMS; p += lstrlen(p) + 1) {
        if (!lstrcmpi(p, "PROGMAN.EXE") || !lstrcmpi(p, "WINN.EXE"))
            continue;
        wsprintf(path, "%s%s", dir, p);
        if (!IsWinAppN(path))
            continue;
        pm_item *it = &gr->it[gr->n++];
        title_of(p, it->desc, sizeof(it->desc));
        lstrcpyn(it->cmd, p, sizeof(it->cmd));
        AnsiUpper(it->cmd);
    }
    save_groups();
}

/* ---------------------------------------------------------------- layout */

static void layout(void)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    int w = rc.right - 12;
    cols = (w - 4) / cell_w;
    if (cols < 1)
        cols = 1;
    int y = 6;
    for (int g = 0; g < ngroups; g++) {
        int rows = (groups[g].n + cols - 1) / cols;
        if (rows < 1)
            rows = 1;
        groups[g].top = y;
        groups[g].height = hdr_h + 4 + rows * cell_h + 2;
        y += groups[g].height + 6;
    }
    doc_h = y;
    int view = rc.bottom;
    int max = doc_h > view ? doc_h - view : 0;
    if (scroll_y > max)
        scroll_y = max;
    SetScrollRange(hmain, SB_VERT, 0, max, FALSE);
    SetScrollPos(hmain, SB_VERT, scroll_y, TRUE);
}

static void group_rect(int g, RECT *r)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    SetRect(r, 6, groups[g].top - scroll_y, rc.right - 6, groups[g].top - scroll_y + groups[g].height);
}

static void item_rect(int g, int i, RECT *r)
{
    RECT gr;
    group_rect(g, &gr);
    int x = gr.left + 2 + (i % cols) * cell_w;
    int y = gr.top + hdr_h + 4 + (i / cols) * cell_h;
    SetRect(r, x, y, x + cell_w, y + cell_h);
}

static void scroll_to(int y)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    int max = doc_h > rc.bottom ? doc_h - rc.bottom : 0;
    if (y > max) y = max;
    if (y < 0) y = 0;
    if (y == scroll_y)
        return;
    scroll_y = y;
    SetScrollPos(hmain, SB_VERT, scroll_y, TRUE);
    InvalidateRect(hmain, NULL, TRUE);
}

static void ensure_visible(void)
{
    if (cur_g >= ngroups)
        return;
    RECT r, rc;
    if (cur_i >= 0)
        item_rect(cur_g, cur_i, &r);
    else
        group_rect(cur_g, &r);
    GetClientRect(hmain, &rc);
    if (r.top < 0)
        scroll_to(scroll_y + r.top - 6);
    else if (r.bottom > rc.bottom)
        scroll_to(scroll_y + r.bottom - rc.bottom + 6);
}

static void set_sel(int g, int i)
{
    if (g == cur_g && i == cur_i)
        return;
    cur_g = g;
    cur_i = i;
    InvalidateRect(hmain, NULL, FALSE);
    ensure_visible();
}

/* ---------------------------------------------------------------- painting */

static void paint(HDC dc)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    FillRect(dc, &rc, (HBRUSH)(COLOR_APPWORKSPACE + 1));
    int active = GetFocus() == hmain;
    HICON icon = LoadIcon(0, IDI_APPLICATION);
    SetBkMode(dc, TRANSPARENT);
    for (int g = 0; g < ngroups; g++) {
        pm_group *gr = &groups[g];
        RECT r;
        group_rect(g, &r);
        if (r.bottom < 0 || r.top > rc.bottom)
            continue;
        FillRect(dc, &r, (HBRUSH)(COLOR_WINDOW + 1));
        FrameRect(dc, &r, GetStockObject(BLACK_BRUSH));
        RECT h = { r.left + 1, r.top + 1, r.right - 1, r.top + hdr_h };
        int hot = g == cur_g && active;
        FillRect(dc, &h, (HBRUSH)((hot ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION) + 1));
        RECT l = { r.left, r.top + hdr_h, r.right, r.top + hdr_h + 1 };
        FillRect(dc, &l, GetStockObject(BLACK_BRUSH));
        SetTextColor(dc, GetSysColor(hot ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
        DrawText(dc, gr->name, -1, &h, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!gr->n) {
            RECT e = { r.left, r.top + hdr_h + 4, r.right, r.bottom };
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            DrawText(dc, "(empty)", -1, &e, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        for (int i = 0; i < gr->n; i++) {
            RECT c;
            item_rect(g, i, &c);
            DrawIcon(dc, c.left + (cell_w - 32) / 2, c.top + 4, icon);
            RECT t = { c.left + 2, c.top + 40, c.right - 2, c.bottom };
            RECT m = t;
            DrawText(dc, gr->it[i].desc, -1, &m, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            int tw = m.right - m.left;
            m.left = t.left + ((t.right - t.left) - tw) / 2;
            m.right = m.left + tw;
            if (m.bottom > t.bottom)
                m.bottom = t.bottom;
            int sel = g == cur_g && i == cur_i;
            if (sel) {
                RECT b = { m.left - 1, m.top, m.right + 1, m.bottom };
                FillRect(dc, &b, (HBRUSH)((active ? COLOR_HIGHLIGHT : COLOR_BTNFACE) + 1));
            }
            SetTextColor(dc, GetSysColor(sel && active ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            DrawText(dc, gr->it[i].desc, -1, &t, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
    }
}

static int hit(int x, int y, int *gi)
{
    for (int g = 0; g < ngroups; g++) {
        RECT r;
        group_rect(g, &r);
        POINT p = { x, y };
        if (!PtInRect(&r, p))
            continue;
        *gi = g;
        for (int i = 0; i < groups[g].n; i++) {
            RECT c;
            item_rect(g, i, &c);
            if (PtInRect(&c, p))
                return i;
        }
        return -1;
    }
    *gi = -1;
    return -1;
}

/* ---------------------------------------------------------------- commands */

static void run(const char *cmd, int minimized)
{
    UINT r = WinExec(cmd, minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);
    if (r >= 32) {
        if (min_on_run)
            ShowWindow(hmain, SW_MINIMIZE);
        return;
    }
    char msg[160];
    wsprintf(msg, "Cannot run '%s':\n%s.", cmd,
             r == 2 ? "the file was not found" :
             r == 8 ? "there is not enough memory" :
             r == 11 ? "it is not a Windows application" : "it cannot be started");
    MessageBox(hmain, msg, "Program Manager", MB_OK | MB_ICONEXCLAMATION);
}

static const DLGTEMPLATE_N item_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 6, 30, 40, 220, 64,
    NULL, NULL, "Program Item Properties", 0, NULL
};
static const DLGITEM_N item_items[] = {
    { 6, 8, 54, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Description:" },
    { 62, 6, 100, 12, IDC_DESC, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 6, 26, 54, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Command Line:" },
    { 62, 24, 100, 12, IDC_CMD, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 170, 6, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 170, 24, 44, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static const DLGTEMPLATE_N group_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 4, 30, 40, 220, 46,
    NULL, NULL, "Program Group Properties", 0, NULL
};
static const DLGITEM_N group_items[] = {
    { 6, 8, 54, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Description:" },
    { 62, 6, 100, 12, IDC_DESC, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 170, 6, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 170, 24, 44, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static const DLGTEMPLATE_N run_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 5, 30, 40, 220, 56,
    NULL, NULL, "Run", 0, NULL
};
static const DLGITEM_N run_items[] = {
    { 6, 6, 120, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Command Line:" },
    { 6, 18, 156, 12, IDC_CMD, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 6, 36, 120, 10, IDC_MIN, BS_AUTOCHECKBOX | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Run &Minimized" },
    { 170, 6, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 170, 24, 44, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static pm_item  edit_item;           /* dialog <-> caller */
static int      run_min;

static BOOL CALLBACK ItemProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_DESC, edit_item.desc);
        SendDlgItemMessage(dlg, IDC_DESC, EM_LIMITTEXT, sizeof(edit_item.desc) - 1, 0);
        if (GetDlgItem(dlg, IDC_CMD)) {
            SetDlgItemText(dlg, IDC_CMD, edit_item.cmd);
            SendDlgItemMessage(dlg, IDC_CMD, EM_LIMITTEXT, sizeof(edit_item.cmd) - 1, 0);
        }
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(dlg, IDC_DESC, edit_item.desc, sizeof(edit_item.desc));
            if (GetDlgItem(dlg, IDC_CMD)) {
                GetDlgItemText(dlg, IDC_CMD, edit_item.cmd, sizeof(edit_item.cmd));
                if (!edit_item.cmd[0]) {
                    MessageBox(dlg, "Please type a command line.", "Program Manager",
                               MB_OK | MB_ICONEXCLAMATION);
                    SetFocus(GetDlgItem(dlg, IDC_CMD));
                    return TRUE;
                }
                if (!edit_item.desc[0])
                    title_of(edit_item.cmd, edit_item.desc, sizeof(edit_item.desc));
            } else if (!edit_item.desc[0]) {
                MessageBox(dlg, "Please type a description.", "Program Manager",
                           MB_OK | MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(dlg, IDC_DESC));
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL CALLBACK RunProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_CMD, edit_item.cmd);
        CheckDlgButton(dlg, IDC_MIN, run_min);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(dlg, IDC_CMD, edit_item.cmd, sizeof(edit_item.cmd));
            run_min = IsDlgButtonChecked(dlg, IDC_MIN);
            EndDialog(dlg, edit_item.cmd[0] ? IDOK : IDCANCEL);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void changed(void)
{
    save_groups();
    layout();
    InvalidateRect(hmain, NULL, TRUE);
    ensure_visible();
}

static void new_item(void)
{
    if (!ngroups) {
        MessageBox(hmain, "Create a program group first.", "Program Manager", MB_OK | MB_ICONINFORMATION);
        return;
    }
    pm_group *gr = &groups[cur_g];
    if (gr->n >= MAXITEMS) {
        MessageBox(hmain, "This group is full.", "Program Manager", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    edit_item.desc[0] = edit_item.cmd[0] = 0;
    if (DialogBoxIndirectN(hinst, &item_tpl, item_items, hmain, ItemProc) != IDOK)
        return;
    gr->it[gr->n] = edit_item;
    cur_i = gr->n++;
    changed();
}

static void new_group(void)
{
    if (ngroups >= MAXGROUPS) {
        MessageBox(hmain, "There are too many groups.", "Program Manager", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    edit_item.desc[0] = 0;
    if (DialogBoxIndirectN(hinst, &group_tpl, group_items, hmain, ItemProc) != IDOK)
        return;
    pm_group *gr = &groups[ngroups];
    lstrcpyn(gr->name, edit_item.desc, sizeof(gr->name));
    gr->n = 0;
    cur_g = ngroups++;
    cur_i = -1;
    changed();
}

static void properties(void)
{
    if (!ngroups)
        return;
    pm_group *gr = &groups[cur_g];
    if (cur_i >= 0) {
        edit_item = gr->it[cur_i];
        if (DialogBoxIndirectN(hinst, &item_tpl, item_items, hmain, ItemProc) == IDOK) {
            gr->it[cur_i] = edit_item;
            changed();
        }
    } else {
        lstrcpyn(edit_item.desc, gr->name, sizeof(edit_item.desc));
        if (DialogBoxIndirectN(hinst, &group_tpl, group_items, hmain, ItemProc) == IDOK) {
            lstrcpyn(gr->name, edit_item.desc, sizeof(gr->name));
            changed();
        }
    }
}

static void delete_selected(void)
{
    if (!ngroups)
        return;
    pm_group *gr = &groups[cur_g];
    char msg[96];
    if (cur_i >= 0) {
        wsprintf(msg, "Are you sure you want to delete the item '%s'?", gr->it[cur_i].desc);
        if (MessageBox(hmain, msg, "Delete", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        for (int i = cur_i; i < gr->n - 1; i++)
            gr->it[i] = gr->it[i + 1];
        gr->n--;
        if (cur_i >= gr->n)
            cur_i = gr->n - 1;
    } else {
        wsprintf(msg, "Are you sure you want to delete the group '%s'?", gr->name);
        if (MessageBox(hmain, msg, "Delete", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        for (int g = cur_g; g < ngroups - 1; g++)
            groups[g] = groups[g + 1];
        ngroups--;
        if (cur_g >= ngroups)
            cur_g = ngroups ? ngroups - 1 : 0;
    }
    changed();
}

static void open_selected(void)
{
    if (cur_g < ngroups && cur_i >= 0)
        run(groups[cur_g].it[cur_i].cmd, 0);
}

static void exit_windows(void)
{
    if (MessageBox(hmain, "This will end your Windows session.", "Exit Windows",
                   MB_OKCANCEL | MB_ICONINFORMATION) == IDOK)
        ExitWindows(0, 0);
}

static void key(int vk)
{
    if (!ngroups)
        return;
    int n = groups[cur_g].n;
    int i = cur_i;
    switch (vk) {
    case VK_RETURN: open_selected(); return;
    case VK_DELETE: delete_selected(); return;
    case VK_TAB:                    /* next group (Shift: previous) */
        if (GetKeyState(VK_SHIFT) < 0)
            set_sel((cur_g + ngroups - 1) % ngroups, -1);
        else
            set_sel((cur_g + 1) % ngroups, -1);
        if (groups[cur_g].n)
            set_sel(cur_g, 0);
        return;
    case VK_LEFT:  i = i > 0 ? i - 1 : 0; break;
    case VK_RIGHT: i = i + 1 < n ? i + 1 : n - 1; break;
    case VK_UP:    i = i - cols >= 0 ? i - cols : i; break;
    case VK_DOWN:  i = i < 0 ? 0 : (i + cols < n ? i + cols : i); break;
    case VK_HOME:  i = 0; break;
    case VK_END:   i = n - 1; break;
    default: return;
    }
    if (n)
        set_sel(cur_g, i < 0 ? 0 : i);
}

/* ---------------------------------------------------------------- window */

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        hmain = h;
        HDC dc = GetDC(h);
        TEXTMETRIC tm;
        GetTextMetrics(dc, &tm);
        ReleaseDC(h, dc);
        line_h = tm.tmHeight;
        hdr_h = line_h + 4;
        cell_h = 40 + 2 * line_h + 4;
        return 0;
    }
    case WM_SIZE:
        layout();
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;                   /* paint() fills everything */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int g, i = hit((short)LOWORD(lp), (short)HIWORD(lp), &g);
        if (g >= 0)
            set_sel(g, i);
        if (msg == WM_LBUTTONDBLCLK && g >= 0 && i >= 0)
            open_selected();
        return 0;
    }
    case WM_KEYDOWN:
        key((int)wp);
        return 0;
    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(h, &rc);
        switch (wp) {
        case SB_LINEUP:   scroll_to(scroll_y - line_h); break;
        case SB_LINEDOWN: scroll_to(scroll_y + line_h); break;
        case SB_PAGEUP:   scroll_to(scroll_y - rc.bottom); break;
        case SB_PAGEDOWN: scroll_to(scroll_y + rc.bottom); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: scroll_to((short)LOWORD(lp)); break;
        case SB_TOP:      scroll_to(0); break;
        case SB_BOTTOM:   scroll_to(doc_h); break;
        }
        return 0;
    }
    case WM_INITMENUPOPUP: {
        HMENU m = (HMENU)wp;
        int has_item = ngroups && cur_i >= 0;
        EnableMenuItem(m, IDM_OPEN, has_item ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_PROPS, ngroups ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_DELETE, ngroups ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_NEWITEM, ngroups ? MF_ENABLED : MF_GRAYED);
        CheckMenuItem(m, IDM_MINRUN, min_on_run ? MF_CHECKED : MF_UNCHECKED);
        return 0;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDM_OPEN:    open_selected(); break;
        case IDM_NEWITEM: new_item(); break;
        case IDM_NEWGRP:  new_group(); break;
        case IDM_PROPS:   properties(); break;
        case IDM_DELETE:  delete_selected(); break;
        case IDM_RUN:
            edit_item.cmd[0] = 0;
            if (DialogBoxIndirectN(hinst, &run_tpl, run_items, h, RunProc) == IDOK)
                run(edit_item.cmd, run_min);
            break;
        case IDM_EXIT:    exit_windows(); break;
        case IDM_MINRUN:
            min_on_run = !min_on_run;
            save_settings();
            break;
        case IDM_ABOUT: {
            char drv[64], text[256];
            UINT colors = GetDisplayDriverInfoN(drv, sizeof(drv));
            HDC dc = GetDC(h);
            wsprintf(text, "Program Manager\nNative Windows 3.x API for murm386.\n\n"
                           "Display: %s\n%d x %d, %u colours\nTasks running: %u",
                     drv, GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES), colors,
                     GetNumTasks());
            ReleaseDC(h, dc);
            MessageBox(h, text, "About Program Manager", MB_OK | MB_ICONINFORMATION);
            break;
        }
        }
        return 0;
    case WM_CLOSE:                  /* the shell: closing it ends Windows */
        exit_windows();
        return 0;
    case WM_ENDSESSION:
        if (wp)
            save_settings();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)cmd;
    hinst = inst;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = MainProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.lpszClassName = "Progman";
        if (!RegisterClass(&wc))
            return 1;
    }
    load_groups();
    if (!ngroups)
        make_main_group();
    min_on_run = (int)GetPrivateProfileInt("Settings", "MinOnRun", 0, INI);

    HMENU file = CreatePopupMenu();
    AppendMenu(file, MF_STRING, IDM_OPEN, "&Open\tEnter");
    AppendMenu(file, MF_STRING, IDM_NEWITEM, "New Program &Item...");
    AppendMenu(file, MF_STRING, IDM_NEWGRP, "New Program &Group...");
    AppendMenu(file, MF_STRING, IDM_PROPS, "&Properties...");
    AppendMenu(file, MF_STRING, IDM_DELETE, "&Delete\tDel");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_RUN, "&Run...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT, "E&xit Windows...");
    HMENU opts = CreatePopupMenu();
    AppendMenu(opts, MF_STRING, IDM_MINRUN, "&Minimize on Use");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Program Manager...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)file, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)opts, "&Options");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");

    /* the saved position, or most of the screen */
    int x = 20, y = 20, w = GetSystemMetrics(SM_CXSCREEN) - 40, hgt = GetSystemMetrics(SM_CYSCREEN) - 60;
    int max = 0;
    char pos[48];
    if (GetPrivateProfileString("Settings", "Window", "", pos, sizeof(pos), INI)) {
        int v[5] = { 0 }, k = 0;
        for (const char *p = pos; *p && k < 5;) {
            while (*p == ' ')
                p++;
            int neg = *p == '-', n = 0;
            if (neg)
                p++;
            if (*p < '0' || *p > '9')
                break;
            while (*p >= '0' && *p <= '9')
                n = n * 10 + (*p++ - '0');
            v[k++] = neg ? -n : n;
        }
        if (k >= 4 && v[2] - v[0] >= 100 && v[3] - v[1] >= 60) {
            x = v[0]; y = v[1]; w = v[2] - v[0]; hgt = v[3] - v[1];
            max = k == 5 && v[4];
        }
    }
    HWND h = CreateWindow("Progman", "Program Manager", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                          x, y, w, hgt, 0, bar, inst, NULL);
    if (ngroups && groups[0].n)
        cur_i = 0;
    ShowWindow(h, max ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(h);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
