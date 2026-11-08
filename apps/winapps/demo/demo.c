/*
 * demo.c - DEMO.EXE, a tour of the native Windows API for WINN.EXE.
 *
 * A main window that draws with GDI, a few controls, a modal dialog and
 * message boxes. Built the same way for the device (EZ) and the host
 * harness.
 */
#include "windows.h"

#define ID_ABOUT   101
#define ID_ADD     102
#define ID_EDIT    103
#define ID_LIST    104
#define ID_CHECK   105
#define ID_QUIT    106
#define ID_DIALOG  107

#define IDC_NAME   201
#define IDC_R1     202
#define IDC_R2     203
#define IDC_R3     204
#define IDC_AGREE  205
#define IDC_COLOR  206
#define IDC_LEVEL  207
#define IDC_LEVELV 208

#define IDM_NEW    301
#define IDM_EXIT   302
#define IDM_CLEAR  303
#define IDM_TIMER  304
#define IDM_RED    305
#define IDM_GREEN  306
#define IDM_BLUE   307
#define IDM_ABOUT  308
#define IDM_RUN    309

static HINSTANCE hinst;
static HWND hedit, hlist;
static int ticks;
static char user_name[64] = "World";
static int color_ix = 0;         /* 0 red, 1 green, 2 blue */
static int level = 50;
static const COLORREF colors[3] = { RGB(255, 0, 0), RGB(0, 128, 0), RGB(0, 0, 255) };
static const char *const color_names[3] = { "Red", "Green", "Blue" };

static const DLGTEMPLATE_N about_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 14, 40, 30, 180, 130,
    NULL, NULL, "Settings", 0, NULL
};
static const DLGITEM_N about_items[] = {
    { 8, 8, 40, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Name:" },
    { 50, 6, 120, 12, IDC_NAME, ES_LEFT | WS_BORDER | WS_TABSTOP | WS_VISIBLE | ES_AUTOHSCROLL, "EDIT", "" },
    { 8, 24, 100, 44, -1, BS_GROUPBOX | WS_VISIBLE, "BUTTON", "Speed" },
    { 14, 34, 80, 10, IDC_R1, BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "&Slow" },
    { 14, 44, 80, 10, IDC_R2, BS_AUTORADIOBUTTON | WS_VISIBLE, "BUTTON", "&Medium" },
    { 14, 54, 80, 10, IDC_R3, BS_AUTORADIOBUTTON | WS_VISIBLE, "BUTTON", "&Fast" },
    { 8, 74, 120, 10, IDC_AGREE, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "I &agree" },
    { 120, 30, 50, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "OK" },
    { 120, 50, 50, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
    { 8, 90, 40, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Color:" },
    { 50, 88, 70, 60, IDC_COLOR, CBS_DROPDOWNLIST | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "COMBOBOX", "" },
    { 8, 108, 40, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Level:" },
    { 50, 108, 90, 9, IDC_LEVEL, SBS_HORZ | WS_TABSTOP | WS_VISIBLE, "SCROLLBAR", "" },
    { 148, 108, 24, 10, IDC_LEVELV, SS_LEFT | WS_VISIBLE, "STATIC", "" },
};

static void show_level(HWND dlg, int v)
{
    SetDlgItemInt(dlg, IDC_LEVELV, (UINT)v, FALSE);
}

static BOOL CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_NAME, user_name);
        CheckRadioButton(dlg, IDC_R1, IDC_R3, IDC_R2);
        for (int i = 0; i < 3; i++)
            SendDlgItemMessage(dlg, IDC_COLOR, CB_ADDSTRING, 0, (LPARAM)(intptr_t)color_names[i]);
        SendDlgItemMessage(dlg, IDC_COLOR, CB_SETCURSEL, (WPARAM)color_ix, 0);
        SetScrollRange(GetDlgItem(dlg, IDC_LEVEL), SB_CTL, 0, 100, FALSE);
        SetScrollPos(GetDlgItem(dlg, IDC_LEVEL), SB_CTL, level, TRUE);
        show_level(dlg, level);
        return TRUE;
    case WM_HSCROLL: {
        HWND sb = (HWND)HIWORD(lp);
        int v = GetScrollPos(sb, SB_CTL);
        switch (wp) {
        case SB_LINEUP: v -= 1; break;
        case SB_LINEDOWN: v += 1; break;
        case SB_PAGEUP: v -= 10; break;
        case SB_PAGEDOWN: v += 10; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: v = (int)LOWORD(lp); break;
        default: return TRUE;
        }
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        SetScrollPos(sb, SB_CTL, v, TRUE);
        show_level(dlg, v);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            if (!IsDlgButtonChecked(dlg, IDC_AGREE)) {
                MessageBox(dlg, "Please tick \"I agree\" first.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            GetDlgItemText(dlg, IDC_NAME, user_name, sizeof(user_name));
            color_ix = (int)SendDlgItemMessage(dlg, IDC_COLOR, CB_GETCURSEL, 0, 0);
            if (color_ix < 0) color_ix = 0;
            level = GetScrollPos(GetDlgItem(dlg, IDC_LEVEL), SB_CTL);
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

static void paint(HWND h, HDC dc)
{
    RECT rc;
    GetClientRect(h, &rc);
    char buf[96];
    SetBkMode(dc, TRANSPARENT);
    wsprintf(buf, "Hello, %s! Ticks: %d", user_name, ticks);
    TextOut(dc, 10, 10, buf, lstrlen(buf));

    HBRUSH b = CreateSolidBrush(RGB(255, 255, 0));
    HGDIOBJ ob = SelectObject(dc, b);
    Ellipse(dc, rc.right - 110, 10, rc.right - 10, 80);
    SelectObject(dc, ob);
    DeleteObject(b);
    HPEN p = CreatePen(PS_SOLID, 2, colors[color_ix]);
    HGDIOBJ op = SelectObject(dc, p);
    for (int i = 0; i < 5; i++) {
        MoveTo(dc, 10, 40 + i * 6);
        LineTo(dc, 50 + level * 3, 40 + i * 12);
    }
    SelectObject(dc, op);
    DeleteObject(p);
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        CreateWindow("BUTTON", "&About...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     10, 80, 90, 26, h, (HMENU)ID_ABOUT, hinst, NULL);
        CreateWindow("BUTTON", "&Settings...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     110, 80, 100, 26, h, (HMENU)ID_DIALOG, hinst, NULL);
        CreateWindow("BUTTON", "&Quit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     220, 80, 70, 26, h, (HMENU)ID_QUIT, hinst, NULL);
        hedit = CreateWindow("EDIT", "type here", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                             10, 120, 200, 22, h, (HMENU)ID_EDIT, hinst, NULL);
        CreateWindow("BUTTON", "&Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     220, 118, 70, 26, h, (HMENU)ID_ADD, hinst, NULL);
        hlist = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                             10, 150, 200, 100, h, (HMENU)ID_LIST, hinst, NULL);
        {
            HWND chk = CreateWindow("BUTTON", "Tick timer", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    220, 154, 120, 20, h, (HMENU)ID_CHECK, hinst, NULL);
            SendMessage(chk, BM_SETCHECK, 1, 0);
            SetTimer(h, 1, 500, NULL);
        }
        for (int i = 1; i <= 9; i++) {
            char s[32];
            wsprintf(s, "Item number %d", i);
            SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(intptr_t)s);
        }
        return 0;
    case WM_SETFOCUS:
        SetFocus(hedit);
        return 0;
    case WM_INITMENUPOPUP: {
        HMENU m = (HMENU)wp;
        CheckMenuItem(m, IDM_TIMER, IsDlgButtonChecked(h, ID_CHECK) ? MF_CHECKED : MF_UNCHECKED);
        for (int i = 0; i < 3; i++)
            CheckMenuItem(m, IDM_RED + i, i == color_ix ? MF_CHECKED : MF_UNCHECKED);
        EnableMenuItem(m, IDM_CLEAR, SendMessage(hlist, LB_GETCOUNT, 0, 0) ? MF_ENABLED : MF_GRAYED);
        return 0;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDM_NEW:
            SetWindowText(hedit, "");
            SetFocus(hedit);
            break;
        case IDM_EXIT:
            PostMessage(h, WM_COMMAND, ID_QUIT, 0);
            break;
        case IDM_RUN:
        {
            UINT r = WinExec("DEMO.EXE", SW_SHOWNORMAL);
            if (r < 32) {
                char msg[64];
                wsprintf(msg, "Cannot start DEMO.EXE (error %u).", r);
                MessageBox(h, msg, "Demo", MB_OK | MB_ICONSTOP);
            }
            break;
        }
        case IDM_CLEAR:
            SendMessage(hlist, LB_RESETCONTENT, 0, 0);
            break;
        case IDM_TIMER: {
            HWND chk = GetDlgItem(h, ID_CHECK);
            int on = !SendMessage(chk, BM_GETCHECK, 0, 0);
            SendMessage(chk, BM_SETCHECK, (WPARAM)on, 0);
            if (on) SetTimer(h, 1, 500, NULL); else KillTimer(h, 1);
            break;
        }
        case IDM_RED: case IDM_GREEN: case IDM_BLUE:
            color_ix = (int)wp - IDM_RED;
            InvalidateRect(h, NULL, TRUE);
            break;
        case IDM_ABOUT:
        case ID_ABOUT:
        {
            char drv[64], text[256];
            UINT colors = GetDisplayDriverInfoN(drv, sizeof(drv));
            HDC dc = GetDC(h);
            wsprintf(text, "Native Windows 3.x API for murm386.\n\n"
                           "Display: %s\n%d x %d, %u colours\nTasks running: %u",
                     drv, GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES), colors,
                     GetNumTasks());
            ReleaseDC(h, dc);
            MessageBox(h, text, "About Demo", MB_OK | MB_ICONINFORMATION);
        }
            break;
        case ID_DIALOG:
            if (DialogBoxIndirectN(hinst, &about_tpl, about_items, h, SettingsProc) == IDOK)
                InvalidateRect(h, NULL, TRUE);
            break;
        case ID_ADD: {
            char s[64];
            GetWindowText(hedit, s, sizeof(s));
            if (s[0]) {
                int i = (int)SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(intptr_t)s);
                SendMessage(hlist, LB_SETCURSEL, (WPARAM)i, 0);
                SetWindowText(hedit, "");
            }
            SetFocus(hedit);
            break;
        }
        case ID_CHECK:
            if (SendMessage((HWND)LOWORD(lp), BM_GETCHECK, 0, 0))
                SetTimer(h, 1, 500, NULL);
            else
                KillTimer(h, 1);
            break;
        case ID_LIST:
            if (HIWORD(lp) == LBN_DBLCLK) {
                char s[64];
                int i = (int)SendMessage(hlist, LB_GETCURSEL, 0, 0);
                SendMessage(hlist, LB_GETTEXT, (WPARAM)i, (LPARAM)(intptr_t)s);
                MessageBox(h, s, "You picked", MB_OK);
            }
            break;
        case ID_QUIT:
            if (MessageBox(h, "Really quit?", "Demo", MB_YESNO | MB_ICONQUESTION) == IDYES)
                DestroyWindow(h);
            break;
        }
        return 0;
    case WM_TIMER: {
        ticks++;
        RECT r = { 0, 0, 300, 30 };
        InvalidateRect(h, &r, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
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
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MainProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));   /* its own icon */
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "DemoWnd";
        if (!RegisterClass(&wc))
            return 1;
    }
    HMENU colors_menu = CreatePopupMenu();
    AppendMenu(colors_menu, MF_STRING, IDM_RED, "&Red");
    AppendMenu(colors_menu, MF_STRING, IDM_GREEN, "&Green");
    AppendMenu(colors_menu, MF_STRING, IDM_BLUE, "&Blue");
    HMENU file = CreatePopupMenu();
    AppendMenu(file, MF_STRING, IDM_NEW, "&New\tCtrl+N");
    AppendMenu(file, MF_STRING, ID_DIALOG, "&Settings...");
    AppendMenu(file, MF_STRING, IDM_RUN, "&Run another Demo");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT, "E&xit");
    HMENU list = CreatePopupMenu();
    AppendMenu(list, MF_STRING, ID_ADD, "&Add item");
    AppendMenu(list, MF_STRING, IDM_CLEAR, "&Clear all\tDel");
    HMENU opts = CreatePopupMenu();
    AppendMenu(opts, MF_STRING, IDM_TIMER, "&Tick timer\tCtrl+T");
    AppendMenu(opts, MF_POPUP, (UINT)colors_menu, "&Color");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Demo...\tF1");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)file, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)list, "&List");
    AppendMenu(bar, MF_POPUP, (UINT)opts, "&Options");
    AppendMenu(bar, MF_POPUP | MF_HELP, (UINT)help, "&Help");

    static const ACCEL_N keys[] = {
        { FVIRTKEY | FCONTROL, 'N', IDM_NEW },
        { FVIRTKEY | FCONTROL, 'T', IDM_TIMER },
        { FVIRTKEY, VK_F1, IDM_ABOUT },
    };
    HACCEL acc = CreateAcceleratorTableN(keys, 3);

    int shift = ((int)GetNumTasks() - 1) * 24;      /* more copies: cascade */
    HWND h = CreateWindow("DemoWnd", "Demo", WS_OVERLAPPEDWINDOW,
                          40 + shift, 30 + shift, 420, 320, 0, bar, inst, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        if (TranslateAccelerator(h, acc, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DestroyAcceleratorTableN(acc);
    return (int)m.wParam;
}
