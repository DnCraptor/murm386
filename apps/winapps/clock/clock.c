/*
 * clock.c - CLOCK.EXE: the time of day, analog or digital, as the Clock of
 * Windows 3.1 (Settings: Analog / Digital, Seconds, Date). The face is drawn
 * off screen and copied in one piece, so it does not flicker; the settings
 * and the window position are kept in CLOCK.INI.
 */
#include "windows.h"

#define INI         "CLOCK.INI"

#define IDM_ANALOG  101
#define IDM_DIGITAL 102
#define IDM_SECONDS 103
#define IDM_DATE    104
#define IDM_ABOUT   105

static HWND    hmain;
static HMENU   hsettings;
static int     analog = 1, seconds = 1, show_date = 0;
static HDC     mem;                 /* off-screen picture of the client area */
static HBITMAP membmp, oldbmp;
static int     memw, memh;
static SYSTEMTIME_N shown;          /* what is drawn now */

/* sin(i * 6 degrees) * 1000; cos(i) = sin(i + 15) */
static const short sin60[60] = {
    0, 105, 208, 309, 407, 500, 588, 669, 743, 809, 866, 914, 951, 978, 995,
    1000, 995, 978, 951, 914, 866, 809, 743, 669, 588, 500, 407, 309, 208, 105,
    0, -105, -208, -309, -407, -500, -588, -669, -743, -809, -866, -914, -951, -978, -995,
    -1000, -995, -978, -951, -914, -866, -809, -743, -669, -588, -500, -407, -309, -208, -105
};

/* a point at position pos (0-59, clockwise from 12) and radius r (per
 * mille of rx, ry) around (cx, cy) */
static POINT at(int cx, int cy, int rx, int ry, int pos, int r)
{
    pos = ((pos % 60) + 60) % 60;
    POINT p;
    p.x = cx + (int)((long)sin60[pos] * rx / 1000 * r / 1000);
    p.y = cy - (int)((long)sin60[(pos + 15) % 60] * ry / 1000 * r / 1000);
    return p;
}

/* ---------------------------------------------------------------- analog */

static void hand(HDC dc, int cx, int cy, int rx, int ry, int pos, int len, int width)
{
    POINT p[4];
    p[0] = at(cx, cy, rx, ry, pos, len);
    p[1] = at(cx, cy, rx, ry, pos + 15, width);
    p[2] = at(cx, cy, rx, ry, pos + 30, width * 2);
    p[3] = at(cx, cy, rx, ry, pos - 15, width);
    Polygon(dc, p, 4);
}

static void draw_analog(HDC dc, int w, int h, const SYSTEMTIME_N *t)
{
    int cx = w / 2, cy = h / 2;
    /* round on screen: the pixels are square */
    int r = (w < h ? w : h) / 2 - 4;
    if (r < 8)
        return;
    int rx = r, ry = r;
    HBRUSH marks = CreateSolidBrush(GetSysColor(COLOR_ACTIVECAPTION));
    HGDIOBJ ob = SelectObject(dc, marks);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    for (int i = 0; i < 60; i++) {
        POINT p = at(cx, cy, rx, ry, i, 900);
        int s = i % 5 ? (r > 60 ? 1 : 0) : (r > 60 ? 3 : 2);
        Rectangle(dc, p.x - s, p.y - s, p.x + s + 2, p.y + s + 2);
    }
    SelectObject(dc, GetStockObject(BLACK_PEN));
    HBRUSH hands = CreateSolidBrush(GetSysColor(COLOR_ACTIVECAPTION));
    SelectObject(dc, hands);
    int hpos = (t->wHour % 12) * 5 + t->wMinute / 12;
    hand(dc, cx, cy, rx, ry, hpos, 550, 50);
    hand(dc, cx, cy, rx, ry, t->wMinute, 800, 45);
    if (seconds) {
        POINT p = at(cx, cy, rx, ry, t->wSecond, 820);
        MoveTo(dc, cx, cy);
        LineTo(dc, p.x, p.y);
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(hands);
    DeleteObject(marks);
}

/* ---------------------------------------------------------------- digital */

/* seven segments: a top, b top right, c bottom right, d bottom, e bottom
 * left, f top left, g middle */
static const unsigned char segs[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static void segment(HDC dc, int x, int y, int len, int th, int vertical)
{
    POINT p[6];
    int h = th / 2;
    if (!vertical) {
        p[0].x = x;           p[0].y = y;
        p[1].x = x + h;       p[1].y = y - h;
        p[2].x = x + len - h; p[2].y = y - h;
        p[3].x = x + len;     p[3].y = y;
        p[4].x = x + len - h; p[4].y = y + h;
        p[5].x = x + h;       p[5].y = y + h;
    } else {
        p[0].x = x;     p[0].y = y;
        p[1].x = x + h; p[1].y = y + h;
        p[2].x = x + h; p[2].y = y + len - h;
        p[3].x = x;     p[3].y = y + len;
        p[4].x = x - h; p[4].y = y + len - h;
        p[5].x = x - h; p[5].y = y + h;
    }
    Polygon(dc, p, 6);
}

static void digit(HDC dc, int x, int y, int w, int th, int d)
{
    int h = w;                      /* each half is a square */
    unsigned m = segs[d % 10];
    if (m & 0x01) segment(dc, x, y, w, th, 0);
    if (m & 0x02) segment(dc, x + w, y, h, th, 1);
    if (m & 0x04) segment(dc, x + w, y + h, h, th, 1);
    if (m & 0x08) segment(dc, x, y + 2 * h, w, th, 0);
    if (m & 0x10) segment(dc, x, y + h, h, th, 1);
    if (m & 0x20) segment(dc, x, y, h, th, 1);
    if (m & 0x40) segment(dc, x, y + h, w, th, 0);
}

static void draw_digital(HDC dc, int w, int h, const SYSTEMTIME_N *t)
{
    int hour = t->wHour % 12;
    if (!hour)
        hour = 12;
    int nd = seconds ? 6 : 4;       /* digits */
    int ncol = seconds ? 2 : 1;
    /* a digit is d wide (with its gap), a colon d/2, the AM/PM text fits under */
    int units2 = nd * 2 + ncol;     /* in half digit widths */
    int dw = (w - 16) * 2 / units2;
    int text_h = 18;
    if (dw * 2 > h - 16 - text_h)
        dw = (h - 16 - text_h) / 2;
    if (dw < 6)
        return;
    int seg = dw * 6 / 10, th = dw / 6 < 2 ? 2 : dw / 6;
    int total = dw * units2 / 2;
    int x = (w - total) / 2 + (dw - seg) / 2, y = (h - 2 * seg - text_h) / 2;
    HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_ACTIVECAPTION));
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    int vals[3] = { hour, t->wMinute, t->wSecond };
    for (int k = 0; k < (seconds ? 3 : 2); k++) {
        if (k) {                    /* the colon */
            int cxp = x + dw / 4 - (dw - seg) / 2, s = th;
            Rectangle(dc, cxp - s / 2, y + seg / 2 - s / 2, cxp + s / 2 + 1, y + seg / 2 + s / 2 + 1);
            Rectangle(dc, cxp - s / 2, y + seg * 3 / 2 - s / 2, cxp + s / 2 + 1, y + seg * 3 / 2 + s / 2 + 1);
            x += dw / 2;
        }
        if (k || vals[k] >= 10)
            digit(dc, x, y, seg, th, vals[k] / 10);
        digit(dc, x + dw, y, seg, th, vals[k] % 10);
        x += 2 * dw;
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(b);
    RECT tr = { 0, y + 2 * seg + th + 4, w, h };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawText(dc, t->wHour < 12 ? "AM" : "PM", -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

/* ---------------------------------------------------------------- window */

static void set_title(const SYSTEMTIME_N *t)
{
    char s[48];
    if (show_date && !IsIconic(hmain))
        wsprintf(s, "Clock - %d/%d/%d", t->wMonth, t->wDay, t->wYear);
    else
        lstrcpy(s, "Clock");
    char old[48];
    GetWindowText(hmain, old, sizeof(old));
    if (lstrcmp(old, s))
        SetWindowText(hmain, s);
}

static void render(HDC dc)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
        return;
    if (!mem || memw != rc.right || memh != rc.bottom) {
        if (mem) {
            SelectObject(mem, oldbmp);
            DeleteObject(membmp);
            DeleteDC(mem);
        }
        mem = CreateCompatibleDC(dc);
        membmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        oldbmp = (HBITMAP)SelectObject(mem, membmp);
        memw = rc.right;
        memh = rc.bottom;
    }
    FillRect(mem, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    if (analog)
        draw_analog(mem, rc.right, rc.bottom, &shown);
    else
        draw_digital(mem, rc.right, rc.bottom, &shown);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
}

static void tick(int force)
{
    SYSTEMTIME_N t;
    GetLocalTimeN(&t);
    int changed = force || t.wMinute != shown.wMinute || t.wHour != shown.wHour ||
                  (seconds && t.wSecond != shown.wSecond);
    shown = t;
    set_title(&t);
    if (changed && !IsIconic(hmain)) {
        HDC dc = GetDC(hmain);
        render(dc);
        ReleaseDC(hmain, dc);
    }
}

static void save_settings(void)
{
    char v[48];
    WritePrivateProfileString("Clock", "Analog", analog ? "1" : "0", INI);
    WritePrivateProfileString("Clock", "Seconds", seconds ? "1" : "0", INI);
    WritePrivateProfileString("Clock", "Date", show_date ? "1" : "0", INI);
    if (!IsIconic(hmain) && !IsZoomed(hmain)) {
        RECT r;
        GetWindowRect(hmain, &r);
        wsprintf(v, "%d %d %d %d", r.left, r.top, r.right, r.bottom);
        WritePrivateProfileString("Clock", "Window", v, INI);
    }
}

static void check_menu(void)
{
    CheckMenuItem(hsettings, IDM_ANALOG, analog ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hsettings, IDM_DIGITAL, analog ? MF_UNCHECKED : MF_CHECKED);
    CheckMenuItem(hsettings, IDM_SECONDS, seconds ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hsettings, IDM_DATE, show_date ? MF_CHECKED : MF_UNCHECKED);
}

static LRESULT CALLBACK ClockProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hmain = h;
        GetLocalTimeN(&shown);
        SetTimer(h, 1, 500, NULL);
        return 0;
    case WM_TIMER:
        tick(0);
        return 0;
    case WM_SIZE:
        InvalidateRect(h, NULL, FALSE);
        set_title(&shown);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (!IsIconic(h))
            render(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDM_ANALOG:  analog = 1; break;
        case IDM_DIGITAL: analog = 0; break;
        case IDM_SECONDS: seconds = !seconds; break;
        case IDM_DATE:    show_date = !show_date; break;
        case IDM_ABOUT:
            MessageBox(h, "Clock\nNative Windows 3.x API for murm386.", "About Clock",
                       MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            return 0;
        }
        check_menu();
        tick(1);
        save_settings();
        return 0;
    case WM_ENDSESSION:
        if (wp)
            save_settings();
        return 0;
    case WM_DESTROY:
        save_settings();
        KillTimer(h, 1);
        if (mem) {
            SelectObject(mem, oldbmp);
            DeleteObject(membmp);
            DeleteDC(mem);
            mem = 0;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static int read_ints(const char *s, int *v, int max)
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

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)cmd;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ClockProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "Clock";
        if (!RegisterClass(&wc))
            return 1;
    }
    analog = (int)GetPrivateProfileInt("Clock", "Analog", 1, INI) != 0;
    seconds = (int)GetPrivateProfileInt("Clock", "Seconds", 1, INI) != 0;
    show_date = (int)GetPrivateProfileInt("Clock", "Date", 0, INI) != 0;

    hsettings = CreatePopupMenu();
    AppendMenu(hsettings, MF_STRING, IDM_ANALOG, "&Analog");
    AppendMenu(hsettings, MF_STRING, IDM_DIGITAL, "&Digital");
    AppendMenu(hsettings, MF_SEPARATOR, 0, NULL);
    AppendMenu(hsettings, MF_STRING, IDM_SECONDS, "&Seconds");
    AppendMenu(hsettings, MF_STRING, IDM_DATE, "D&ate");
    AppendMenu(hsettings, MF_SEPARATOR, 0, NULL);
    AppendMenu(hsettings, MF_STRING, IDM_ABOUT, "A&bout Clock...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)hsettings, "&Settings");
    check_menu();

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 220, hgt = 220;
    char pos[48];
    int v[4];
    if (GetPrivateProfileString("Clock", "Window", "", pos, sizeof(pos), INI) &&
        read_ints(pos, v, 4) == 4 && v[2] - v[0] >= 80 && v[3] - v[1] >= 80) {
        x = v[0]; y = v[1]; w = v[2] - v[0]; hgt = v[3] - v[1];
    }
    HWND h = CreateWindow("Clock", "Clock", WS_OVERLAPPEDWINDOW, x, y, w, hgt, 0, bar, inst, NULL);
    if (!h)
        return 1;
    ShowWindow(h, show);
    UpdateWindow(h);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
