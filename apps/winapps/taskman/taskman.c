/*
 * taskman.c - TASKMAN.EXE: the Task List of Windows 3.1.
 *
 * Ctrl+Esc or a double click on the desktop starts it (SYSTEM.INI [boot]
 * taskman.exe names it). It lists the top-level windows: Switch To brings
 * one forward (restored if it is an icon), End Task closes it; Cascade,
 * Tile and Arrange Icons arrange the windows on the desktop. Every button
 * closes the list.
 */
#include "windows.h"

#define IDC_LIST    100
#define IDC_SWITCH  101
#define IDC_END     102
#define IDC_CASCADE 103
#define IDC_TILE    104
#define IDC_ARRANGE 105

static HWND self;

static BOOL CALLBACK add_window(HWND h, LPARAM lp)
{
    HWND lb = (HWND)lp;
    char t[80];
    if (h == self || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return TRUE;
    if (!GetWindowText(h, t, sizeof(t)) || !t[0])
        return TRUE;
    int i = (int)SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(intptr_t)t);
    if (i >= 0)
        SendMessage(lb, LB_SETITEMDATA, (WPARAM)i, (LPARAM)h);
    return TRUE;
}

static HWND picked(HWND dlg)
{
    HWND lb = GetDlgItem(dlg, IDC_LIST);
    int i = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
    if (i < 0)
        return 0;
    HWND h = (HWND)SendMessage(lb, LB_GETITEMDATA, (WPARAM)i, 0);
    return IsWindow(h) ? h : 0;
}

/* a window held by a modal dialog: the dialog comes forward instead */
static HWND enabled_popup(HWND h)
{
    if (IsWindowEnabled(h))
        return h;
    for (HWND t = GetWindow(GetDesktopWindow(), GW_CHILD); t; t = GetWindow(t, GW_HWNDNEXT))
        if (GetWindow(t, GW_OWNER) == h && IsWindowVisible(t))
            return enabled_popup(t);
    return h;
}

static void switch_to(HWND h)
{
    if (!h)
        return;
    if (IsIconic(h))
        ShowWindow(h, SW_RESTORE);
    SetActiveWindow(enabled_popup(h));
}

static int action;
static HWND target;

static BOOL CALLBACK ListProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        self = dlg;
        HWND lb = GetDlgItem(dlg, IDC_LIST);
        EnumWindows(add_window, (LPARAM)lb);
        SendMessage(lb, LB_SETCURSEL, 0, 0);
        /* in the middle of the screen */
        RECT r;
        GetWindowRect(dlg, &r);
        int w = r.right - r.left, h = r.bottom - r.top;
        SetWindowPos(dlg, 0, (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
        EnableWindow(GetDlgItem(dlg, IDC_SWITCH), SendMessage(lb, LB_GETCOUNT, 0, 0) > 0);
        EnableWindow(GetDlgItem(dlg, IDC_END), SendMessage(lb, LB_GETCOUNT, 0, 0) > 0);
        return TRUE;
    }
    case WM_ACTIVATE:
        /* the list goes away when another window is chosen, as in Windows */
        if (LOWORD(wp) == WA_INACTIVE && !action) {
            action = IDCANCEL;
            PostMessage(dlg, WM_COMMAND, IDCANCEL, 0);
        }
        break;
    case WM_COMMAND:
        if (wp == IDC_LIST && HIWORD(lp) == LBN_DBLCLK)
            wp = IDC_SWITCH;
        switch (wp) {
        case IDOK:
        case IDC_SWITCH:
        case IDC_END:
        case IDC_CASCADE:
        case IDC_TILE:
        case IDC_ARRANGE:
            action = wp == IDOK ? IDC_SWITCH : (int)wp;
            target = picked(dlg);
            EndDialog(dlg, action);
            return TRUE;
        case IDCANCEL:
            if (!action)
                action = IDCANCEL;
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

#define BTN (BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE)

static const DLGTEMPLATE_N list_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 7, 0, 0, 200, 124,
    NULL, NULL, "Task List", 0, NULL
};
static const DLGITEM_N list_items[] = {
    { 6, 6, 188, 74, IDC_LIST, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "LISTBOX", "" },
    { 6, 86, 58, 14, IDC_SWITCH, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE | WS_GROUP, "BUTTON", "&Switch To" },
    { 71, 86, 58, 14, IDC_END, BTN, "BUTTON", "&End Task" },
    { 136, 86, 58, 14, IDCANCEL, BTN, "BUTTON", "Cancel" },
    { 6, 104, 58, 14, IDC_CASCADE, BTN, "BUTTON", "&Cascade" },
    { 71, 104, 58, 14, IDC_TILE, BTN, "BUTTON", "&Tile" },
    { 136, 104, 58, 14, IDC_ARRANGE, BTN, "BUTTON", "&Arrange Icons" },
};

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    action = 0;
    target = 0;
    DialogBoxIndirectN(inst, &list_tpl, list_items, 0, ListProc);
    HWND desk = GetDesktopWindow();
    switch (action) {
    case IDC_SWITCH:
        switch_to(target);
        break;
    case IDC_END:
        if (target)
            PostMessage(target, WM_CLOSE, 0, 0);
        break;
    case IDC_CASCADE:
        CascadeChildWindows(desk, 0);
        break;
    case IDC_TILE:
        TileChildWindows(desk, MDITILE_VERTICAL);
        break;
    case IDC_ARRANGE:
        ArrangeIconicWindows(desk);
        break;
    }
    return 0;
}
