/*
 * hello.c - sample native Windows application.
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

static HINSTANCE hinst;
static HWND hedit, hlist;
static int ticks;
static char user_name[64] = "World";

static const DLGTEMPLATE_N about_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 9, 40, 30, 180, 110,
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
};

static BOOL CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_NAME, user_name);
        CheckRadioButton(dlg, IDC_R1, IDC_R3, IDC_R2);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            if (!IsDlgButtonChecked(dlg, IDC_AGREE)) {
                MessageBox(dlg, "Please tick \"I agree\" first.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            GetDlgItemText(dlg, IDC_NAME, user_name, sizeof(user_name));
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
    HPEN p = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
    HGDIOBJ op = SelectObject(dc, p);
    for (int i = 0; i < 5; i++) {
        MoveTo(dc, 10, 40 + i * 6);
        LineTo(dc, 200, 40 + i * 12);
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
        CreateWindow("BUTTON", "Tick timer", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                     220, 154, 120, 20, h, (HMENU)ID_CHECK, hinst, NULL);
        for (int i = 1; i <= 9; i++) {
            char s[32];
            wsprintf(s, "Item number %d", i);
            SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(intptr_t)s);
        }
        return 0;
    case WM_SETFOCUS:
        SetFocus(hedit);
        return 0;
    case WM_COMMAND:
        switch (wp) {
        case ID_ABOUT:
            MessageBox(h, "Native Windows 3.x API for murm386.\nGDI + USER on a VBE 8-bit display.",
                       "About Hello", MB_OK | MB_ICONINFORMATION);
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
            if (MessageBox(h, "Really quit?", "Hello", MB_YESNO | MB_ICONQUESTION) == IDYES)
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
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "HelloWnd";
        if (!RegisterClass(&wc))
            return 1;
    }
    HWND h = CreateWindow("HelloWnd", "Hello, Windows", WS_OVERLAPPEDWINDOW,
                          40, 30, 420, 300, 0, 0, inst, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
