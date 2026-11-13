/* Native RUSLAT.EXE: a minimized layout indicator and switch. */
#include "windows.h"

#define TIMER_LAYOUT 1

static HWND indicator;
static int last_layout = -1;

static void refresh(HWND h)
{
    int layout = GetKeyboardLayoutN();
    if (layout != last_layout) {
        last_layout = layout;
        SetWindowText(h, layout ? "RU" : "EN");
        InvalidateRect(h, NULL, TRUE);
    }
}

static LRESULT CALLBACK RusLatProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        indicator = h;
        refresh(h);
        if (!SetTimer(h, TIMER_LAYOUT, 250, NULL))
            return -1;
        return 0;
    case WM_TIMER:
        if (wp == TIMER_LAYOUT)
            refresh(h);
        return 0;
    case WM_LBUTTONDBLCLK:
        SetKeyboardLayoutN(!GetKeyboardLayoutN());
        refresh(h);
        return 0;
    case WM_RBUTTONDOWN:
        SetKeyboardLayoutN(!GetKeyboardLayoutN());
        refresh(h);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        DrawText(dc, GetKeyboardLayoutN() ? "RU" : "EN", -1, &r,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_LAYOUT);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)cmd;
    (void)show;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = RusLatProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "RusLatNative";
        if (!RegisterClass(&wc))
            return 1;
    }
    HWND h = CreateWindow("RusLatNative", "EN", WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, 120, 80,
                          0, 0, inst, NULL);
    if (!h)
        return 1;
    ShowWindow(h, SW_SHOWMINNOACTIVE);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
