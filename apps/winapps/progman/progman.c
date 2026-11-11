/*
 * progman.c - PROGMAN.EXE, the Program Manager: the shell WINN.EXE starts
 * when SYSTEM.INI says nothing else, as in Windows 3.x. Closing it ends the
 * Windows session.
 *
 * An MDI frame: every program group is a child window with its items as
 * icons; groups can be moved, sized, minimized, maximized, cascaded and
 * tiled (Window menu).
 *
 * Groups and items live in PROGMAN.INI (text, not the .GRP files of
 * Windows 3.x):
 *
 *   [Settings]
 *   Window=left top right bottom maximized
 *   MinOnRun=0
 *   [Group1]
 *   Name=Main
 *   Window=left top right bottom state      (0 normal, 1 maximized, 2 minimized)
 *   Item1=Demo,DEMO.EXE
 *
 * On the first start the Main group gets every Windows application found
 * next to WINN.EXE. Items use the standard application icon (no resources
 * yet).
 *
 * As Windows' Program Manager it is a DDE server (DDEML.DLL): service and
 * topic PROGMAN, the commands setup programs send it ([CreateGroup],
 * [ShowGroup], [AddItem], [DeleteItem], [ReplaceItem], [DeleteGroup],
 * [Reload], [ExitProgman]: nothing, it is the shell) and the item Groups
 * (their names, a line each), for DDEML's clients and those speaking DDE's
 * messages themselves (WM_DDE_INITIATE, EXECUTE, REQUEST, TERMINATE).
 */
#include <string.h>
#include "windows.h"
#include "ddeml.h"

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
#define IDM_CASCADE 110
#define IDM_TILE    111
#define IDM_ARRANGE 112
#define IDM_FIRSTCHILD 1000

#define IDC_DESC    201
#define IDC_CMD     202
#define IDC_MIN     203

typedef struct {
    char  desc[32];
    char  cmd[80];
    HICON icon;                     /* from the program file, read when first drawn */
    int   icon_read;
} pm_item;

typedef struct {
    char    name[32];
    int     n;
    pm_item it[MAXITEMS];
    HWND    hwnd;
    int     sel;                    /* selected item, -1 none */
    int     scroll, doc_h, cols;
    RECT    normal;                 /* restored position, client coordinates */
    int     state;                  /* 0 normal, 1 maximized, 2 minimized */
} pm_group;

static HINSTANCE hinst;
static HWND      hframe, hclient;
static pm_group  groups[MAXGROUPS];
static int       ngroups, saved_groups;
static int       min_on_run;
static int       line_h = 16, cell_w = 80, cell_h = 80;
static int       creating = -1;     /* group whose window is being made */

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

/* "a b c d [e]" -> numbers; returns how many */
static int numbers(const char *s, int *v, int max)
{
    int k = 0;
    while (*s && k < max) {
        while (*s == ' ')
            s++;
        int neg = *s == '-', n = 0;
        if (neg)
            s++;
        if (*s < '0' || *s > '9')
            break;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + (*s++ - '0');
        v[k++] = neg ? -n : n;
    }
    return k;
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
        gr->sel = -1;
        gr->state = 0;
        SetRectEmpty(&gr->normal);
        int v[5];
        GetPrivateProfileString(sec, "Window", "", val, sizeof(val), INI);
        if (numbers(val, v, 5) >= 4 && v[2] - v[0] >= 60 && v[3] - v[1] >= 40) {
            SetRect(&gr->normal, v[0], v[1], v[2], v[3]);
            gr->state = numbers(val, v, 5) == 5 ? v[4] : 0;
        }
        for (int i = 0; i < MAXITEMS; i++) {
            wsprintf(key, "Item%d", i + 1);
            if (!GetPrivateProfileString(sec, key, "", val, sizeof(val), INI))
                break;
            split_item(val, &gr->it[gr->n++]);
        }
        if (gr->n)
            gr->sel = 0;
    }
    saved_groups = ngroups;
}

static void group_state(pm_group *gr)
{
    if (!gr->hwnd)
        return;
    gr->state = IsIconic(gr->hwnd) ? 2 : IsZoomed(gr->hwnd) ? 1 : 0;
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
        group_state(gr);
        wsprintf(sec, "Group%d", g + 1);
        WritePrivateProfileString(sec, NULL, NULL, INI);
        WritePrivateProfileString(sec, "Name", gr->name, INI);
        if (!IsRectEmpty(&gr->normal)) {
            wsprintf(val, "%d %d %d %d %d", gr->normal.left, gr->normal.top,
                     gr->normal.right, gr->normal.bottom, gr->state);
            WritePrivateProfileString(sec, "Window", val, INI);
        }
        for (int i = 0; i < gr->n; i++) {
            wsprintf(key, "Item%d", i + 1);
            wsprintf(val, "%s,%s", gr->it[i].desc, gr->it[i].cmd);
            WritePrivateProfileString(sec, key, val, INI);
        }
    }
    saved_groups = ngroups;
}

static RECT frame_normal;           /* restored frame position */

static void save_settings(void)
{
    char val[48];
    wsprintf(val, "%d", min_on_run);
    WritePrivateProfileString("Settings", "MinOnRun", val, INI);
    if (!IsRectEmpty(&frame_normal)) {
        wsprintf(val, "%d %d %d %d %d", frame_normal.left, frame_normal.top,
                 frame_normal.right, frame_normal.bottom, IsZoomed(hframe) ? 1 : 0);
        WritePrivateProfileString("Settings", "Window", val, INI);
    }
}

/* the program file of a command line: "NOTEPAD X.TXT" -> "C:\\WINN\\NOTEPAD.EXE" */
static void program_path(const char *cmd, char *out, int size)
{
    char prog[80];
    int n = 0, dir = 0, dot = 0;
    while (*cmd == ' ')
        cmd++;
    for (; cmd[n] && cmd[n] != ' ' && n < (int)sizeof(prog) - 5; n++) {
        prog[n] = cmd[n];
        if (cmd[n] == '\\' || cmd[n] == '/' || cmd[n] == ':')
            dir = 1, dot = 0;
        else if (cmd[n] == '.')
            dot = 1;
    }
    prog[n] = 0;
    if (!dot)
        lstrcat(prog, ".EXE");
    if (dir) {
        lstrcpyn(out, prog, size);
        return;
    }
    GetWindowsDirectory(out, (UINT)size);
    int k = lstrlen(out);
    if (k && out[k - 1] != '\\' && k < size - 1)
        lstrcat(out, "\\");
    k = lstrlen(out);
    lstrcpyn(out + k, prog, size - k);
}

static HICON item_icon(pm_item *it)
{
    if (!it->icon_read) {
        char path[160];
        program_path(it->cmd, path, sizeof(path));
        it->icon = ExtractIconN(path);
        it->icon_read = 1;
    }
    return it->icon ? it->icon : LoadIcon(0, IDI_APPLICATION);
}

static void forget_icon(pm_item *it)
{
    if (it->icon)
        DestroyIcon(it->icon);
    it->icon = 0;
    it->icon_read = 0;
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
    gr->sel = -1;
    ngroups = 1;
    WinDirListN(spec, names, sizeof(names));
    for (const char *p = names; *p && gr->n < MAXITEMS; p += lstrlen(p) + 1) {
        if (!lstrcmpi(p, "PROGMAN.EXE") || !lstrcmpi(p, "WINN.EXE") || !lstrcmpi(p, "TASKMAN.EXE") ||
            !lstrcmpi(p, "WINHELP.EXE"))
            continue;
        wsprintf(path, "%s%s", dir, p);
        if (!IsWinAppN(path))
            continue;
        pm_item *it = &gr->it[gr->n++];
        /* the program's own name for itself, else its file name */
        if (!GetAppInfoN(path, it->desc, sizeof(it->desc)))
            title_of(p, it->desc, sizeof(it->desc));
        lstrcpyn(it->cmd, p, sizeof(it->cmd));
        AnsiUpper(it->cmd);
    }
    if (gr->n)
        gr->sel = 0;
    save_groups();
}

/* ---------------------------------------------------------------- groups */

static pm_group *group_of(HWND h)
{
    for (int g = 0; g < ngroups; g++)
        if (groups[g].hwnd == h)
            return &groups[g];
    return NULL;
}

static pm_group *active_group(void)
{
    HWND a = (HWND)LOWORD(SendMessage(hclient, WM_MDIGETACTIVE, 0, 0));
    return a ? group_of(a) : NULL;
}

static void layout(pm_group *gr)
{
    RECT rc;
    GetClientRect(gr->hwnd, &rc);
    gr->cols = (rc.right - 4) / cell_w;
    if (gr->cols < 1)
        gr->cols = 1;
    int rows = (gr->n + gr->cols - 1) / gr->cols;
    gr->doc_h = 4 + rows * cell_h;
    int max = gr->doc_h > rc.bottom ? gr->doc_h - rc.bottom : 0;
    if (gr->scroll > max)
        gr->scroll = max;
    SetScrollRange(gr->hwnd, SB_VERT, 0, max, FALSE);
    SetScrollPos(gr->hwnd, SB_VERT, gr->scroll, TRUE);
}

static void item_rect(pm_group *gr, int i, RECT *r)
{
    int x = 2 + (i % gr->cols) * cell_w;
    int y = 4 + (i / gr->cols) * cell_h - gr->scroll;
    SetRect(r, x, y, x + cell_w, y + cell_h);
}

static void scroll_to(pm_group *gr, int y)
{
    RECT rc;
    GetClientRect(gr->hwnd, &rc);
    int max = gr->doc_h > rc.bottom ? gr->doc_h - rc.bottom : 0;
    if (y > max) y = max;
    if (y < 0) y = 0;
    if (y == gr->scroll)
        return;
    gr->scroll = y;
    SetScrollPos(gr->hwnd, SB_VERT, y, TRUE);
    InvalidateRect(gr->hwnd, NULL, TRUE);
}

static void set_sel(pm_group *gr, int i)
{
    if (i == gr->sel)
        return;
    gr->sel = i;
    InvalidateRect(gr->hwnd, NULL, FALSE);
    if (i < 0)
        return;
    RECT r, rc;
    item_rect(gr, i, &r);
    GetClientRect(gr->hwnd, &rc);
    if (r.top < 0)
        scroll_to(gr, gr->scroll + r.top - 4);
    else if (r.bottom > rc.bottom)
        scroll_to(gr, gr->scroll + r.bottom - rc.bottom + 4);
}

static void paint_group(pm_group *gr, HDC dc)
{
    RECT rc;
    GetClientRect(gr->hwnd, &rc);
    FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    int focus = GetFocus() == gr->hwnd;
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < gr->n; i++) {
        RECT c;
        item_rect(gr, i, &c);
        if (c.bottom < 0 || c.top > rc.bottom)
            continue;
        DrawIcon(dc, c.left + (cell_w - 32) / 2, c.top + 4, item_icon(&gr->it[i]));
        RECT t = { c.left + 2, c.top + 40, c.right - 2, c.bottom };
        RECT m = t;
        DrawText(dc, gr->it[i].desc, -1, &m, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        int tw = m.right - m.left;
        m.left = t.left + ((t.right - t.left) - tw) / 2;
        m.right = m.left + tw;
        if (m.bottom > t.bottom)
            m.bottom = t.bottom;
        int sel = i == gr->sel && focus;
        if (sel) {
            RECT b = { m.left - 1, m.top, m.right + 1, m.bottom };
            FillRect(dc, &b, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        }
        SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        DrawText(dc, gr->it[i].desc, -1, &t, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }
}

static int hit_item(pm_group *gr, int x, int y)
{
    POINT p = { x, y };
    for (int i = 0; i < gr->n; i++) {
        RECT c;
        item_rect(gr, i, &c);
        if (PtInRect(&c, p))
            return i;
    }
    return -1;
}

/* the group's restored position, in client coordinates */
static void remember_normal(pm_group *gr)
{
    if (!gr->hwnd || IsIconic(gr->hwnd) || IsZoomed(gr->hwnd))
        return;
    RECT r;
    GetWindowRect(gr->hwnd, &r);
    POINT a = { r.left, r.top }, b = { r.right, r.bottom };
    ScreenToClient(hclient, &a);
    ScreenToClient(hclient, &b);
    SetRect(&gr->normal, a.x, a.y, b.x, b.y);
}

static HWND open_group_window(int g, int activate_it)
{
    pm_group *gr = &groups[g];
    MDICREATESTRUCT cs;
    cs.szClass = "PMGroup";
    cs.szTitle = gr->name;
    cs.hOwner = hinst;
    if (!IsRectEmpty(&gr->normal)) {
        cs.x = gr->normal.left;
        cs.y = gr->normal.top;
        cs.cx = gr->normal.right - gr->normal.left;
        cs.cy = gr->normal.bottom - gr->normal.top;
    } else {
        RECT rc;
        GetClientRect(hclient, &rc);
        int d = 22;
        cs.x = cs.y = (g % 6) * d;
        cs.cx = rc.right * 2 / 3;
        cs.cy = rc.bottom / 2;
    }
    cs.style = WS_VSCROLL | (gr->state == 2 ? WS_MINIMIZE : gr->state == 1 ? WS_MAXIMIZE : 0);
    cs.lParam = 0;
    creating = g;
    HWND h = (HWND)SendMessage(hclient, WM_MDICREATE, 0, (LPARAM)(intptr_t)&cs);
    creating = -1;
    (void)activate_it;
    return h;
}

/* ---------------------------------------------------------------- commands */

/* a document (its first word's extension not a program's): the program
 * WIN.INI [Extensions] names for it ("hlp=winhelp.exe ^.hlp") before it */
static void with_program(const char *cmd, char *out, int size)
{
    char ext[8], v[96];
    const char *end = cmd, *dot = NULL;
    while (*end && *end != ' ') {
        if (*end == '.')
            dot = end;
        else if (*end == '\\' || *end == ':')
            dot = NULL;
        end++;
    }
    lstrcpyn(out, cmd, size);
    if (!dot || end - dot - 1 > 3 || end - dot - 1 < 1)
        return;
    lstrcpyn(ext, dot + 1, (int)(end - dot));
    if (!lstrcmpi(ext, "EXE") || !lstrcmpi(ext, "COM") || !lstrcmpi(ext, "BAT") ||
        !lstrcmpi(ext, "PIF") ||
        !GetProfileString("Extensions", ext, "", v, sizeof(v)) || !v[0])
        return;
    int n = 0;
    while (v[n] && v[n] != '^')
        n++;
    while (n && v[n - 1] == ' ')
        n--;
    v[n] = 0;
    if (n && n + 1 + lstrlen(cmd) < size)
        wsprintf(out, "%s %s", v, cmd);
}

static void run(const char *given, int minimized)
{
    char cmd[200];
    with_program(given, cmd, sizeof(cmd));
    UINT r = WinExec(cmd, minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);
    if (r >= 32) {
        if (min_on_run)
            ShowWindow(hframe, SW_MINIMIZE);
        return;
    }
    char msg[300];
    wsprintf(msg, "Cannot run '%s':\n%s.", cmd,
             r == 2 ? "the file was not found" :
             r == 8 ? "there is not enough memory" :
             r == 11 ? "it is not a Windows application" : "it cannot be started");
    MessageBox(hframe, msg, "Program Manager", MB_OK | MB_ICONEXCLAMATION);
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
                if (!edit_item.desc[0]) {
                    char path[160];
                    program_path(edit_item.cmd, path, sizeof(path));
                    if (!GetAppInfoN(path, edit_item.desc, sizeof(edit_item.desc)))
                        title_of(edit_item.cmd, edit_item.desc, sizeof(edit_item.desc));
                }
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

static void items_changed(pm_group *gr)
{
    save_groups();
    layout(gr);
    InvalidateRect(gr->hwnd, NULL, TRUE);
}

static void new_item(void)
{
    pm_group *gr = active_group();
    if (!gr) {
        MessageBox(hframe, "Open or create a program group first.", "Program Manager",
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (gr->n >= MAXITEMS) {
        MessageBox(hframe, "This group is full.", "Program Manager", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    edit_item.desc[0] = edit_item.cmd[0] = 0;
    edit_item.icon = 0;
    edit_item.icon_read = 0;
    if (DialogBoxIndirectN(hinst, &item_tpl, item_items, hframe, ItemProc) != IDOK)
        return;
    gr->it[gr->n] = edit_item;
    gr->n++;
    items_changed(gr);
    set_sel(gr, gr->n - 1);
}

static void new_group(void)
{
    if (ngroups >= MAXGROUPS) {
        MessageBox(hframe, "There are too many groups.", "Program Manager", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    edit_item.desc[0] = 0;
    if (DialogBoxIndirectN(hinst, &group_tpl, group_items, hframe, ItemProc) != IDOK)
        return;
    pm_group *gr = &groups[ngroups];
    lstrcpyn(gr->name, edit_item.desc, sizeof(gr->name));
    gr->n = 0;
    gr->sel = -1;
    gr->state = 0;
    gr->scroll = 0;
    SetRectEmpty(&gr->normal);
    gr->hwnd = 0;
    ngroups++;
    open_group_window(ngroups - 1, 1);
    save_groups();
}

static void properties(void)
{
    pm_group *gr = active_group();
    if (!gr)
        return;
    if (gr->sel >= 0 && !IsIconic(gr->hwnd)) {
        edit_item = gr->it[gr->sel];
        if (DialogBoxIndirectN(hinst, &item_tpl, item_items, hframe, ItemProc) == IDOK) {
            if (lstrcmpi(edit_item.cmd, gr->it[gr->sel].cmd)) {
                forget_icon(&gr->it[gr->sel]);      /* another program: its icon */
                edit_item.icon = 0;
                edit_item.icon_read = 0;
            }
            gr->it[gr->sel] = edit_item;
            items_changed(gr);
        }
    } else {
        lstrcpyn(edit_item.desc, gr->name, sizeof(edit_item.desc));
        if (DialogBoxIndirectN(hinst, &group_tpl, group_items, hframe, ItemProc) == IDOK) {
            lstrcpyn(gr->name, edit_item.desc, sizeof(gr->name));
            SetWindowText(gr->hwnd, gr->name);
            save_groups();
        }
    }
}

static void delete_selected(void)
{
    pm_group *gr = active_group();
    if (!gr)
        return;
    char msg[96];
    if (gr->sel >= 0 && !IsIconic(gr->hwnd)) {
        wsprintf(msg, "Are you sure you want to delete the item '%s'?", gr->it[gr->sel].desc);
        if (MessageBox(hframe, msg, "Delete", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        forget_icon(&gr->it[gr->sel]);
        for (int i = gr->sel; i < gr->n - 1; i++)
            gr->it[i] = gr->it[i + 1];
        gr->n--;
        if (gr->sel >= gr->n)
            gr->sel = gr->n - 1;
        items_changed(gr);
    } else {
        wsprintf(msg, "Are you sure you want to delete the group '%s'?", gr->name);
        if (MessageBox(hframe, msg, "Delete", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        HWND h = gr->hwnd;
        for (int i = 0; i < gr->n; i++)
            forget_icon(&gr->it[i]);
        int g = (int)(gr - groups);
        for (; g < ngroups - 1; g++)
            groups[g] = groups[g + 1];
        ngroups--;
        SendMessage(hclient, WM_MDIDESTROY, h, 0);
        save_groups();
    }
}

static void open_selected(void)
{
    pm_group *gr = active_group();
    if (!gr)
        return;
    if (IsIconic(gr->hwnd)) {
        SendMessage(hclient, WM_MDIRESTORE, gr->hwnd, 0);
        return;
    }
    if (gr->sel >= 0)
        run(gr->it[gr->sel].cmd, 0);
}

/* Commit the shutdown while the confirmation is still on screen.
 * Closing a MessageBox first exposes the MDI frame during slow INI writes. */
static BOOL CALLBACK ExitProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND) {
        if (wp == IDOK) {
            if (ExitWindows(0, 0))
                EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

static void exit_windows(void)
{
    DWORD units = GetDialogBaseUnits();
    int sw = GetSystemMetrics(SM_CXSCREEN) * 4 / LOWORD(units);
    int sh = GetSystemMetrics(SM_CYSCREEN) * 8 / HIWORD(units);
    const DLGTEMPLATE_N tpl = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_ABSALIGN,
        4, (sw - 216) / 2, (sh - 62) / 2, 216, 62,
        NULL, NULL, "Exit Windows", 0, NULL
    };
    static const DLGITEM_N items[] = {
        { 8, 8, 16, 16, -1, SS_ICON | WS_VISIBLE, "STATIC", "#32516" },
        { 30, 10, 174, 16, -1, SS_LEFT | WS_VISIBLE, "STATIC",
          "This will end your Windows session." },
        { 62, 38, 40, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE,
          "BUTTON", "OK" },
        { 114, 38, 40, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE,
          "BUTTON", "Cancel" },
    };
    DialogBoxIndirectN(hinst, &tpl, items, hframe, ExitProc);
}

static void key(pm_group *gr, int vk)
{
    int n = gr->n, i = gr->sel, cols = gr->cols;
    switch (vk) {
    case VK_RETURN: open_selected(); return;
    case VK_DELETE: delete_selected(); return;
    case VK_TAB:                    /* Ctrl+Tab: next group, as Ctrl+F6 */
        if (GetKeyState(VK_CONTROL) < 0)
            SendMessage(hclient, WM_MDINEXT, gr->hwnd, GetKeyState(VK_SHIFT) < 0);
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
        set_sel(gr, i < 0 ? 0 : i);
}

/* ---------------------------------------------------------------- windows */

static LRESULT CALLBACK GroupProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    pm_group *gr = group_of(h);
    if (!gr && msg == WM_CREATE && creating >= 0) {
        gr = &groups[creating];
        gr->hwnd = h;
    }
    if (gr) {
        switch (msg) {
        case WM_CREATE:
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                layout(gr);
                InvalidateRect(h, NULL, TRUE);
            }
            remember_normal(gr);
            break;
        case WM_MOVE:
            remember_normal(gr);
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(h, NULL, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            if (!IsIconic(h))
                paint_group(gr, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            int i = hit_item(gr, (short)LOWORD(lp), (short)HIWORD(lp));
            set_sel(gr, i);
            if (msg == WM_LBUTTONDBLCLK && i >= 0)
                run(gr->it[i].cmd, 0);
            return 0;
        }
        case WM_KEYDOWN:
            key(gr, (int)wp);
            return 0;
        case WM_VSCROLL: {
            RECT rc;
            GetClientRect(h, &rc);
            switch (wp) {
            case SB_LINEUP:   scroll_to(gr, gr->scroll - line_h); break;
            case SB_LINEDOWN: scroll_to(gr, gr->scroll + line_h); break;
            case SB_PAGEUP:   scroll_to(gr, gr->scroll - rc.bottom); break;
            case SB_PAGEDOWN: scroll_to(gr, gr->scroll + rc.bottom); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: scroll_to(gr, (short)LOWORD(lp)); break;
            }
            return 0;
        }
        case WM_CLOSE:              /* closing a group minimizes it, as in Windows 3.x */
            SendMessage(h, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            return 0;
        case WM_DESTROY:
            gr->hwnd = 0;
            break;
        }
    }
    return DefMDIChildProc(h, msg, wp, lp);
}

static LRESULT dde_message(HWND h, UINT msg, WPARAM wp, LPARAM lp);

static LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg >= WM_DDE_FIRST && msg <= WM_DDE_LAST && msg != WM_DDE_ACK && msg != WM_DDE_DATA)
        return dde_message(h, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        hframe = h;
        HDC dc = GetDC(h);
        TEXTMETRIC tm;
        GetTextMetrics(dc, &tm);
        ReleaseDC(h, dc);
        line_h = tm.tmHeight;
        cell_h = 40 + 2 * line_h + 4;
        CLIENTCREATESTRUCT cc;
        cc.hWindowMenu = GetSubMenu(GetMenu(h), 2);
        cc.idFirstChild = IDM_FIRSTCHILD;
        hclient = CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                               0, 0, 0, 0, h, (HMENU)1, hinst, (LPVOID)&cc);
        return hclient ? 0 : -1;
    }
    case WM_SIZE:
    case WM_MOVE:
        if (!IsIconic(h) && !IsZoomed(h))
            GetWindowRect(h, &frame_normal);
        break;
    case WM_INITMENUPOPUP: {
        HMENU m = (HMENU)wp;
        pm_group *gr = active_group();
        int has_item = gr && gr->sel >= 0 && !IsIconic(gr->hwnd);
        EnableMenuItem(m, IDM_OPEN, gr ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_PROPS, gr ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_DELETE, gr ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_NEWITEM, gr ? MF_ENABLED : MF_GRAYED);
        (void)has_item;
        CheckMenuItem(m, IDM_MINRUN, min_on_run ? MF_CHECKED : MF_UNCHECKED);
        return 0;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDM_OPEN:    open_selected(); return 0;
        case IDM_NEWITEM: new_item(); return 0;
        case IDM_NEWGRP:  new_group(); return 0;
        case IDM_PROPS:   properties(); return 0;
        case IDM_DELETE:  delete_selected(); return 0;
        case IDM_RUN:
            edit_item.cmd[0] = 0;
            if (DialogBoxIndirectN(hinst, &run_tpl, run_items, h, RunProc) == IDOK)
                run(edit_item.cmd, run_min);
            return 0;
        case IDM_EXIT:    exit_windows(); return 0;
        case IDM_MINRUN:
            min_on_run = !min_on_run;
            save_settings();
            return 0;
        case IDM_CASCADE: SendMessage(hclient, WM_MDICASCADE, 0, 0); return 0;
        case IDM_TILE:    SendMessage(hclient, WM_MDITILE, MDITILE_VERTICAL, 0); return 0;
        case IDM_ARRANGE: SendMessage(hclient, WM_MDIICONARRANGE, 0, 0); return 0;
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
            return 0;
        }
        }
        break;
    case WM_CLOSE:                  /* the shell: closing it ends Windows */
        exit_windows();
        return 0;
    case WM_ENDSESSION:
        if (wp) {
            save_groups();
            save_settings();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProc(h, hclient, msg, wp, lp);
}

/* ---------------------------------------------------------------- DDE */

static struct {
    HINSTANCE lib;
    DWORD inst;
    HSZ name, groups;                   /* PROGMAN (service and topic), Groups */
    DDEGETDATA get_data;
    DDECREATEDATAHANDLE create_data;
    int current;                        /* the group AddItem goes to */
} dde = { 0, 0, 0, 0, NULL, NULL, -1 };

static int atoi_(const char *s)
{
    int v = 0, neg = *s == '-';
    for (s += neg; *s >= '0' && *s <= '9'; s++)
        v = v * 10 + (*s - '0');
    return neg ? -v : v;
}

static int group_named(const char *name)
{
    for (int g = 0; g < ngroups; g++)
        if (!lstrcmpi(groups[g].name, name))
            return g;
    return -1;
}

/* a group of the name (made when there is none): its index, -1 if none
 * can be made */
static int dde_group(const char *name)
{
    int g = group_named(name);
    if (g >= 0 || ngroups >= MAXGROUPS || !name[0])
        return g;
    pm_group *gr = &groups[ngroups];
    memset(gr, 0, sizeof(*gr));
    lstrcpyn(gr->name, name, sizeof(gr->name));
    gr->sel = -1;
    ngroups++;
    open_group_window(ngroups - 1, 1);
    save_groups();
    return ngroups - 1;
}

static void show_group(int g, int cmd)
{
    pm_group *gr = &groups[g];
    if (!gr->hwnd)
        return;
    if (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE)
        ShowWindow(gr->hwnd, SW_MINIMIZE);
    else if (cmd == SW_SHOWMAXIMIZED)
        SendMessage(hclient, WM_MDIMAXIMIZE, gr->hwnd, 0);
    else if (IsIconic(gr->hwnd))
        SendMessage(hclient, WM_MDIRESTORE, gr->hwnd, 0);
    if (cmd != SW_SHOWNOACTIVATE && cmd != SW_SHOWMINNOACTIVE)
        SendMessage(hclient, WM_MDIACTIVATE, gr->hwnd, 0);
}

static int item_named(pm_group *gr, const char *name)
{
    for (int i = 0; i < gr->n; i++)
        if (!lstrcmpi(gr->it[i].desc, name))
            return i;
    return -1;
}

static void remove_item(pm_group *gr, int i)
{
    forget_icon(&gr->it[i]);
    for (; i < gr->n - 1; i++)
        gr->it[i] = gr->it[i + 1];
    gr->n--;
    if (gr->sel >= gr->n)
        gr->sel = gr->n - 1;
    items_changed(gr);
}

/* "[Cmd(a,"b, c",d)][Cmd2]...": each command and its arguments (quotes
 * kept out); 1 when all were done */
static int dde_execute(const char *s)
{
    int ok = 1;
    while (*s) {
        while (*s && *s != '[')
            s++;
        if (!*s)
            break;
        s++;
        char name[24], arg[8][96];
        int n = 0, k = 0;
        while (*s == ' ')
            s++;
        while (*s && *s != '(' && *s != ']' && *s != ' ' && k < (int)sizeof(name) - 1)
            name[k++] = *s++;
        name[k] = 0;
        while (*s == ' ')
            s++;
        if (*s == '(') {
            s++;
            for (;;) {
                int q = 0;
                k = 0;
                while (*s == ' ')
                    s++;
                while (*s && (q || (*s != ',' && *s != ')'))) {
                    if (*s == '"')
                        q = !q;
                    else if (n < 8 && k < (int)sizeof(arg[0]) - 1)
                        arg[n][k++] = *s;
                    s++;
                }
                while (k && arg[n < 8 ? n : 7][k - 1] == ' ')
                    k--;
                if (n < 8)
                    arg[n++][k] = 0;
                if (*s != ',')
                    break;
                s++;
            }
            if (*s == ')')
                s++;
        }
        while (*s && *s != ']')
            s++;
        if (*s == ']')
            s++;
        for (int i = n; i < 8; i++)
            arg[i][0] = 0;
        if (!lstrcmpi(name, "CreateGroup") || !lstrcmpi(name, "ShowGroup")) {
            int g = dde_group(arg[0]);
            if (g < 0) {
                ok = 0;
                continue;
            }
            dde.current = g;
            if (!lstrcmpi(name, "ShowGroup"))
                show_group(g, n > 1 ? atoi_(arg[1]) : SW_SHOWNORMAL);
            else
                show_group(g, SW_SHOWNORMAL);
        } else if (!lstrcmpi(name, "AddItem")) {
            pm_group *gr = dde.current >= 0 && dde.current < ngroups ? &groups[dde.current] : NULL;
            if (!gr || !arg[0][0] || gr->n >= MAXITEMS) {
                ok = 0;
                continue;
            }
            pm_item *it = &gr->it[gr->n];
            memset(it, 0, sizeof(*it));
            lstrcpyn(it->cmd, arg[0], sizeof(it->cmd));
            if (arg[1][0])
                lstrcpyn(it->desc, arg[1], sizeof(it->desc));
            else
                title_of(arg[0], it->desc, sizeof(it->desc));
            gr->n++;
            items_changed(gr);
        } else if (!lstrcmpi(name, "DeleteItem") || !lstrcmpi(name, "ReplaceItem")) {
            pm_group *gr = dde.current >= 0 && dde.current < ngroups ? &groups[dde.current] : NULL;
            int i = gr ? item_named(gr, arg[0]) : -1;
            if (i < 0)
                ok = 0;
            else
                remove_item(gr, i);
        } else if (!lstrcmpi(name, "DeleteGroup")) {
            int g = group_named(arg[0]);
            if (g < 0) {
                ok = 0;
                continue;
            }
            pm_group *gr = &groups[g];
            HWND h = gr->hwnd;
            for (int i = 0; i < gr->n; i++)
                forget_icon(&gr->it[i]);
            for (int k2 = g; k2 < ngroups - 1; k2++)
                groups[k2] = groups[k2 + 1];
            ngroups--;
            if (dde.current == g)
                dde.current = -1;
            else if (dde.current > g)
                dde.current--;
            if (h)
                SendMessage(hclient, WM_MDIDESTROY, h, 0);
            save_groups();
        } else if (!lstrcmpi(name, "Reload") || !lstrcmpi(name, "ExitProgman")) {
            /* the groups are what PROGMAN.INI has; the shell stays */
        } else {
            ok = 0;
        }
    }
    return ok;
}

static HDDEDATA CALLBACK dde_callback(UINT type, UINT fmt, HCONV conv, HSZ hsz1, HSZ hsz2,
                                      HDDEDATA data, DWORD d1, DWORD d2)
{
    (void)conv; (void)d1; (void)d2;
    switch (type) {
    case XTYP_CONNECT:
        return hsz1 == dde.name && hsz2 == dde.name;
    case XTYP_EXECUTE: {
        static char text[1024];
        DWORD n = dde.get_data(data, (LPBYTE)text, sizeof(text) - 1, 0);
        text[n] = 0;
        return dde_execute(text) ? DDE_FACK : DDE_FNOTPROCESSED;
    }
    case XTYP_REQUEST: {
        if (hsz2 != dde.groups || fmt != CF_TEXT)
            return 0;
        static char list[MAXGROUPS * 34 + 1];
        int k = 0;
        for (int g = 0; g < ngroups; g++)
            k += wsprintf(list + k, "%s\r\n", groups[g].name);
        list[k] = 0;
        return dde.create_data(dde.inst, (LPBYTE)list, (DWORD)k + 1, 0, dde.groups, CF_TEXT, 0);
    }
    }
    return 0;
}

/* DDEML's function of the name (void (*)(void): any function's type) */
static void (*dde_proc(const char *name))(void)
{
    return (void (*)(void))GetProcAddress(dde.lib, name);
}

/* DDE's messages, to the frame: the same as DDEML's */
static int is_progman(ATOM a)
{
    char s[16];
    return !a || (GlobalGetAtomName(a, s, sizeof(s)) && !lstrcmpi(s, "PROGMAN"));
}

static LRESULT dde_message(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND client = (HWND)wp;
    switch (msg) {
    case WM_DDE_INITIATE:
        if (is_progman(LOWORD(lp)) && is_progman(HIWORD(lp)) && client != h)
            SendMessage(client, WM_DDE_ACK, (WPARAM)h,
                        MAKELPARAM(GlobalAddAtom("PROGMAN"), GlobalAddAtom("PROGMAN")));
        return 0;
    case WM_DDE_EXECUTE: {
        HGLOBAL cmds = (HGLOBAL)HIWORD(lp);
        const char *s = cmds ? (const char *)GlobalLock(cmds) : NULL;
        int ok = 0;
        if (s) {
            static char text[1024];
            lstrcpyn(text, s, sizeof(text));
            GlobalUnlock(cmds);
            ok = dde_execute(text);
        }
        PostMessage(client, WM_DDE_ACK, (WPARAM)h, MAKELPARAM(ok ? 0x8000 : 0, cmds));
        return 0;
    }
    case WM_DDE_REQUEST: {
        char item[16];
        ATOM a = HIWORD(lp);
        if (LOWORD(lp) == CF_TEXT && GlobalGetAtomName(a, item, sizeof(item)) &&
            !lstrcmpi(item, "Groups")) {
            HGLOBAL d = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4 + MAXGROUPS * 34 + 1);
            char *p = d ? (char *)GlobalLock(d) : NULL;
            if (p) {
                p[0] = 0;
                p[1] = 0x30;            /* fResponse, fRelease: the client frees it */
                p[2] = CF_TEXT;
                p[3] = 0;
                int k = 4;
                for (int g = 0; g < ngroups; g++)
                    k += wsprintf(p + k, "%s\r\n", groups[g].name);
                p[k] = 0;
                GlobalUnlock(d);
                if (PostMessage(client, WM_DDE_DATA, (WPARAM)h, MAKELPARAM(d, a)))
                    return 0;
                GlobalFree(d);
            }
        }
        PostMessage(client, WM_DDE_ACK, (WPARAM)h, MAKELPARAM(0, a));
        return 0;
    }
    case WM_DDE_TERMINATE:
        PostMessage(client, WM_DDE_TERMINATE, (WPARAM)h, 0);
        return 0;
    case WM_DDE_ADVISE: case WM_DDE_UNADVISE: case WM_DDE_POKE:
        PostMessage(client, WM_DDE_ACK, (WPARAM)h, MAKELPARAM(0, HIWORD(lp)));
        return 0;
    }
    return 0;
}

static void dde_start(void)
{
    dde.lib = LoadLibrary("DDEML");
    if (dde.lib < 32)
        return;
    DDEINITIALIZE init = (DDEINITIALIZE)dde_proc("DdeInitialize");
    DDECREATESTRINGHANDLE str = (DDECREATESTRINGHANDLE)dde_proc("DdeCreateStringHandle");
    DDENAMESERVICE name = (DDENAMESERVICE)dde_proc("DdeNameService");
    dde.get_data = (DDEGETDATA)dde_proc("DdeGetData");
    dde.create_data = (DDECREATEDATAHANDLE)dde_proc("DdeCreateDataHandle");
    if (!init || !str || !name || !dde.get_data || !dde.create_data ||
        init(&dde.inst, dde_callback, CBF_FAIL_POKES | CBF_FAIL_ADVISES |
             CBF_SKIP_REGISTRATIONS | CBF_SKIP_UNREGISTRATIONS, 0)) {
        FreeLibrary(dde.lib);
        dde.lib = 0;
        dde.inst = 0;
        return;
    }
    dde.name = str(dde.inst, "PROGMAN", CP_WINANSI);
    dde.groups = str(dde.inst, "Groups", CP_WINANSI);
    name(dde.inst, dde.name, 0, DNS_REGISTER);
}

static void dde_stop(void)
{
    if (!dde.lib)
        return;
    DDEUNINITIALIZE uninit = (DDEUNINITIALIZE)dde_proc("DdeUninitialize");
    if (uninit)
        uninit(dde.inst);
    FreeLibrary(dde.lib);
    dde.lib = 0;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)cmd;
    hinst = inst;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = FrameProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));   /* its own icon */
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.lpszClassName = "Progman";
        if (!RegisterClass(&wc))
            return 1;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = GroupProc;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "PMGroup";
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
    HMENU win = CreatePopupMenu();
    AppendMenu(win, MF_STRING, IDM_CASCADE, "&Cascade\tShift+F5");
    AppendMenu(win, MF_STRING, IDM_TILE, "&Tile\tShift+F4");
    AppendMenu(win, MF_STRING, IDM_ARRANGE, "&Arrange Icons");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Program Manager...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)file, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)opts, "&Options");
    AppendMenu(bar, MF_POPUP, (UINT)win, "&Window");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");

    static const ACCEL_N keys[] = {
        { FVIRTKEY | FSHIFT, VK_F5, IDM_CASCADE },
        { FVIRTKEY | FSHIFT, VK_F4, IDM_TILE },
    };
    HACCEL acc = CreateAcceleratorTableN(keys, 2);

    /* the saved position, or most of the screen */
    int x = 20, y = 20, w = GetSystemMetrics(SM_CXSCREEN) - 40, hgt = GetSystemMetrics(SM_CYSCREEN) - 60;
    int max = 0;
    char pos[48];
    int v[5];
    if (GetPrivateProfileString("Settings", "Window", "", pos, sizeof(pos), INI)) {
        int k = numbers(pos, v, 5);
        if (k >= 4 && v[2] - v[0] >= 100 && v[3] - v[1] >= 60) {
            x = v[0]; y = v[1]; w = v[2] - v[0]; hgt = v[3] - v[1];
            max = k == 5 && v[4];
        }
    }
    HWND h = CreateWindow("Progman", "Program Manager", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          x, y, w, hgt, 0, bar, inst, NULL);
    if (!h)
        return 1;
    ShowWindow(h, max ? SW_SHOWMAXIMIZED : show);
    /* the groups: minimized ones first, the first group ends on top, active */
    for (int g = ngroups - 1; g >= 0; g--)
        if (groups[g].state == 2)
            open_group_window(g, 0);
    for (int g = ngroups - 1; g >= 0; g--)
        if (groups[g].state != 2)
            open_group_window(g, 0);
    UpdateWindow(h);
    dde_start();

    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        if (TranslateMDISysAccel(hclient, &m) || TranslateAccelerator(h, acc, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DestroyAcceleratorTableN(acc);
    dde_stop();
    return (int)m.wParam;
}
