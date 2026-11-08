/*
 * calc.c - CALC.EXE: the standard Calculator of Windows 3.1.
 *
 * Buttons or the keyboard: digits, . + - * / = (Enter), Backspace, Del (CE),
 * Esc (C), @ (sqrt), % , r (1/x), F9 (+/-), Ctrl+L/R/M/P (MC/MR/MS/M+).
 * Edit > Copy and Paste go through the clipboard. Numbers are doubles shown
 * with up to 13 significant digits.
 */
#include "windows.h"

enum {
    ID_0 = 100, ID_1, ID_2, ID_3, ID_4, ID_5, ID_6, ID_7, ID_8, ID_9,
    ID_DOT = 120, ID_SIGN, ID_ADD, ID_SUB, ID_MUL, ID_DIV, ID_EQ, ID_SQRT, ID_PCT, ID_INV,
    ID_BACK = 140, ID_CE, ID_C, ID_MC, ID_MR, ID_MS, ID_MPLUS,
    IDM_COPY = 200, IDM_PASTE, IDM_ABOUT
};

#define BW      36                  /* button */
#define BH      26
#define GAP     4
#define MARGIN  8
#define DISP_H  24
#define DIGITS  13

static HWND   hmain;
static char   entry[32];            /* the number being typed */
static int    typing;               /* entry is being typed */
static double acc;                  /* the left operand */
static double last_val;             /* "=" again repeats the last operation */
static int    op, last_op;
static double memory;
static int    error;
static char   shown[40] = "0.";

/* ---------------------------------------------------------------- numbers */

/* x as Windows 3.1 shows it: up to DIGITS significant digits, always with
 * the point ("12.", "0.5"), an exponent outside that range */
static void format(double x, char *out)
{
    if (x != x || x > 1e300 || x < -1e300) {
        lstrcpy(out, "Error");
        return;
    }
    int o = 0;
    if (x < 0) {
        out[o++] = '-';
        x = -x;
    }
    if (x == 0) {
        lstrcpy(out, "0.");
        return;
    }
    /* scale to [1, 10) and round to DIGITS digits */
    int e = 0;
    while (x >= 10.0) { x /= 10.0; e++; }
    while (x < 1.0) { x *= 10.0; e--; }
    char d[DIGITS + 1];
    double r = x;
    for (int i = 0; i < DIGITS; i++) {
        int k = (int)r;
        if (k > 9) k = 9;
        d[i] = (char)('0' + k);
        r = (r - k) * 10.0;
    }
    if (r >= 5.0) {                 /* round the last digit, carrying */
        int i = DIGITS - 1;
        while (i >= 0 && d[i] == '9')
            d[i--] = '0';
        if (i >= 0)
            d[i]++;
        else {
            d[0] = '1';
            e++;
        }
    }
    int n = DIGITS;
    while (n > 1 && d[n - 1] == '0')
        n--;
    if (e >= 0 && e < DIGITS) {
        for (int i = 0; i <= e; i++)
            out[o++] = i < n ? d[i] : '0';
        out[o++] = '.';
        for (int i = e + 1; i < n; i++)
            out[o++] = d[i];
    } else if (e < 0 && e > -5 && n - e <= DIGITS + 4) {
        out[o++] = '0';
        out[o++] = '.';
        for (int i = -1; i > e; i--)
            out[o++] = '0';
        for (int i = 0; i < n; i++)
            out[o++] = d[i];
    } else {
        out[o++] = d[0];
        out[o++] = '.';
        for (int i = 1; i < n; i++)
            out[o++] = d[i];
        out[o++] = 'e';
        out[o++] = e < 0 ? '-' : '+';
        int ae = e < 0 ? -e : e;
        char t[6];
        int k = 0;
        do { t[k++] = (char)('0' + ae % 10); ae /= 10; } while (ae);
        while (k)
            out[o++] = t[--k];
    }
    out[o] = 0;
}

static double parse(const char *s)
{
    double v = 0, scale = 1;
    int neg = 0, frac = 0;
    if (*s == '-') { neg = 1; s++; }
    for (; *s; s++) {
        if (*s == '.') { frac = 1; continue; }
        if (*s < '0' || *s > '9') break;
        if (frac) {
            scale /= 10.0;
            v += (*s - '0') * scale;
        } else {
            v = v * 10.0 + (*s - '0');
        }
    }
    return neg ? -v : v;
}

static double square_root(double x)
{
    if (x <= 0)
        return 0;
    double r = x > 1 ? x / 2 : 1;
    for (int i = 0; i < 60; i++) {
        double n = (r + x / r) / 2;
        if (n == r)
            break;
        r = n;
    }
    return r;
}

static int has_dot(const char *s)
{
    for (; *s; s++)
        if (*s == '.')
            return 1;
    return 0;
}

/* ---------------------------------------------------------------- display */

static void redisplay(void)
{
    RECT r;
    GetClientRect(hmain, &r);
    r.left = MARGIN;
    r.right -= MARGIN;
    r.top = MARGIN;
    r.bottom = MARGIN + DISP_H;
    InvalidateRect(hmain, &r, FALSE);
    RECT m = { MARGIN, MARGIN + DISP_H + 8, MARGIN + BW, MARGIN + DISP_H + 8 + BH };
    InvalidateRect(hmain, &m, FALSE);
}

static void show_value(double v)
{
    format(v, shown);
    if (!lstrcmp(shown, "Error"))
        error = 1;
    redisplay();
}

static void show_entry(void)
{
    lstrcpy(shown, entry);
    if (!has_dot(shown))
        lstrcat(shown, ".");
    redisplay();
}

/* the number on the display */
static double current(void) { return typing ? parse(entry) : parse(shown); }

static double apply(double a, int o, double b)
{
    switch (o) {
    case ID_ADD: return a + b;
    case ID_SUB: return a - b;
    case ID_MUL: return a * b;
    case ID_DIV:
        if (b == 0) {
            error = 1;
            return 0;
        }
        return a / b;
    }
    return b;
}

static void clear_all(void)
{
    entry[0] = 0;
    typing = 0;
    acc = 0;
    op = last_op = 0;
    error = 0;
    lstrcpy(shown, "0.");
    redisplay();
}

static void result(double v)
{
    if (error) {
        lstrcpy(shown, "Error");
        redisplay();
        return;
    }
    show_value(v);
}

static void command(int id)
{
    if (error && id != ID_C && id != ID_CE)
        return;                     /* Error stays until C or CE */
    if (id >= ID_0 && id <= ID_9) {
        if (!typing) {
            entry[0] = 0;
            typing = 1;
        }
        int digits = 0;
        for (const char *p = entry; *p; p++)
            if (*p >= '0' && *p <= '9')
                digits++;
        if (digits >= DIGITS)
            return;
        if (!lstrcmp(entry, "0"))
            entry[0] = 0;
        int n = lstrlen(entry);
        entry[n] = (char)('0' + id - ID_0);
        entry[n + 1] = 0;
        show_entry();
        return;
    }
    switch (id) {
    case ID_DOT:
        if (!typing) {
            lstrcpy(entry, "0");
            typing = 1;
        }
        if (!has_dot(entry))
            lstrcat(entry, ".");
        show_entry();
        break;
    case ID_BACK:
        if (typing && entry[0]) {
            entry[lstrlen(entry) - 1] = 0;
            if (!entry[0] || !lstrcmp(entry, "-"))
                lstrcpy(entry, "0");
            show_entry();
        }
        break;
    case ID_CE:
        error = 0;
        lstrcpy(entry, "0");
        typing = 1;
        show_entry();
        break;
    case ID_C:
        clear_all();
        break;
    case ID_SIGN:
        if (typing) {
            if (entry[0] == '-')
                lstrcpy(entry, entry + 1);
            else if (lstrcmp(entry, "0")) {
                char t[32] = "-";
                lstrcat(t, entry);
                lstrcpy(entry, t);
            }
            show_entry();
        } else {
            show_value(-current());
        }
        break;
    case ID_ADD: case ID_SUB: case ID_MUL: case ID_DIV:
        if (typing && op)
            acc = apply(acc, op, parse(entry));
        else if (typing || !op)
            acc = current();
        op = id;
        typing = 0;
        result(acc);
        break;
    case ID_EQ: {
        double b;
        if (op) {
            b = current();
            acc = apply(acc, op, b);
            last_op = op;
            last_val = b;
            op = 0;
        } else if (last_op) {
            acc = apply(current(), last_op, last_val);
        } else {
            acc = current();
        }
        typing = 0;
        result(acc);
        break;
    }
    case ID_SQRT: {
        double v = current();
        if (v < 0) {
            error = 1;
            result(0);
        } else {
            typing = 0;
            show_value(square_root(v));
        }
        break;
    }
    case ID_PCT:
        typing = 0;
        show_value(acc * current() / 100.0);
        break;
    case ID_INV: {
        double v = current();
        typing = 0;
        if (v == 0) {
            error = 1;
            result(0);
        } else {
            show_value(1.0 / v);
        }
        break;
    }
    case ID_MC: memory = 0; redisplay(); break;
    case ID_MR: typing = 0; show_value(memory); break;
    case ID_MS: memory = current(); typing = 0; redisplay(); break;
    case ID_MPLUS: memory += current(); typing = 0; redisplay(); break;
    }
}

/* ---------------------------------------------------------------- keyboard */

static int key_char(int c)
{
    if (c >= '0' && c <= '9') return ID_0 + c - '0';
    switch (c) {
    case '.': case ',': return ID_DOT;
    case '+': return ID_ADD;
    case '-': return ID_SUB;
    case '*': return ID_MUL;
    case '/': return ID_DIV;
    case '=': case '\r': return ID_EQ;
    case '@': return ID_SQRT;
    case '%': return ID_PCT;
    case 'r': case 'R': return ID_INV;
    case 8: return ID_BACK;
    case 27: return ID_C;
    case 12: return ID_MC;          /* Ctrl+L */
    case 18: return ID_MR;          /* Ctrl+R */
    case 16: return ID_MPLUS;       /* Ctrl+P */
    }
    return 0;
}

/* a key shows its button pressed for a moment, as when clicked */
static HWND pressed;

static void press(int id)
{
    if (pressed)
        SendMessage(pressed, BM_SETSTATE, FALSE, 0);
    pressed = GetDlgItem(hmain, id);
    if (pressed) {
        SendMessage(pressed, BM_SETSTATE, TRUE, 0);
        SetTimer(hmain, 1, 100, NULL);
    }
    command(id);
}

/* ---------------------------------------------------------------- window */

static const struct { const char *text; int id; int col, row; } buttons[] = {
    { "Back", ID_BACK, 1, 0 }, { "CE", ID_CE, 2, 0 }, { "C", ID_C, 3, 0 },
    { "MC", ID_MC, 0, 1 }, { "7", ID_7, 1, 1 }, { "8", ID_8, 2, 1 }, { "9", ID_9, 3, 1 },
    { "/", ID_DIV, 4, 1 }, { "sqrt", ID_SQRT, 5, 1 },
    { "MR", ID_MR, 0, 2 }, { "4", ID_4, 1, 2 }, { "5", ID_5, 2, 2 }, { "6", ID_6, 3, 2 },
    { "*", ID_MUL, 4, 2 }, { "%", ID_PCT, 5, 2 },
    { "MS", ID_MS, 0, 3 }, { "1", ID_1, 1, 3 }, { "2", ID_2, 2, 3 }, { "3", ID_3, 3, 3 },
    { "-", ID_SUB, 4, 3 }, { "1/x", ID_INV, 5, 3 },
    { "M+", ID_MPLUS, 0, 4 }, { "0", ID_0, 1, 4 }, { "+/-", ID_SIGN, 2, 4 }, { ".", ID_DOT, 3, 4 },
    { "+", ID_ADD, 4, 4 }, { "=", ID_EQ, 5, 4 },
};

static int col_x(int col)
{
    /* the memory column stands apart */
    return MARGIN + (col ? BW + 10 + (col - 1) * (BW + GAP) : 0);
}

static int row_y(int row) { return MARGIN + DISP_H + 8 + row * (BH + GAP); }

static void create_buttons(HWND h, HINSTANCE inst)
{
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        int x = col_x(buttons[i].col), y = row_y(buttons[i].row), w = BW;
        if (buttons[i].row == 0) {  /* Back, CE, C: wider, right-aligned */
            w = (BW * 5 + GAP * 4 - 2 * GAP) / 3;
            x = col_x(1) + (buttons[i].col - 1) * (w + GAP);
        }
        CreateWindow("BUTTON", buttons[i].text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     x, y, w, BH, h, (HMENU)(uintptr_t)buttons[i].id, inst, NULL);
    }
}

static void paint(HDC dc)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    /* the display: sunken, white, the number on the right */
    RECT d = { MARGIN, MARGIN, rc.right - MARGIN, MARGIN + DISP_H };
    FillRect(dc, &d, (HBRUSH)(COLOR_WINDOW + 1));
    FrameRect(dc, &d, GetStockObject(BLACK_BRUSH));
    RECT t = d;
    InflateRect(&t, -6, 0);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawText(dc, shown, -1, &t, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    /* the memory indicator, above MC */
    RECT m = { MARGIN, row_y(0), MARGIN + BW, row_y(0) + BH };
    FillRect(dc, &m, (HBRUSH)(COLOR_BTNFACE + 1));
    InflateRect(&m, -2, -2);
    FrameRect(dc, &m, (HBRUSH)(COLOR_BTNSHADOW + 1));
    if (memory != 0)
        DrawText(dc, "M", 1, &m, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void copy(void)
{
    if (!OpenClipboard(hmain))
        return;
    EmptyClipboard();
    int n = lstrlen(shown);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)n + 1);
    if (g) {
        lstrcpy((LPSTR)GlobalLock(g), shown);
        GlobalUnlock(g);
        SetClipboardData(CF_TEXT, g);
    }
    CloseClipboard();
}

static void paste(void)
{
    if (!OpenClipboard(hmain))
        return;
    HANDLE g = GetClipboardData(CF_TEXT);
    char buf[128] = "";
    if (g) {
        lstrcpyn(buf, (LPCSTR)GlobalLock(g), sizeof(buf));
        GlobalUnlock(g);
    }
    CloseClipboard();
    /* as typed: digits and operators; C, E, M, P, Q, R also as in Windows */
    for (const char *p = buf; *p; p++) {
        int c = *p, id = key_char(c);
        if (c == 'c' || c == 'C') id = ID_C;
        else if (c == 'e' || c == 'E') id = ID_CE;
        else if (c == 'm' || c == 'M') id = ID_MS;
        else if (c == 'p' || c == 'P') id = ID_MPLUS;
        else if (c == 'q' || c == 'Q') id = ID_C;
        else if (c == ':') continue;
        if (id && c != 27 && c != 8)
            command(id);
    }
}

static LRESULT CALLBACK CalcProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hmain = h;
        create_buttons(h, ((CREATESTRUCT *)(intptr_t)lp)->hInstance);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (wp >= ID_0 && wp <= ID_MPLUS) {
            command((int)wp);
            SetFocus(h);            /* the keys stay with the calculator */
            return 0;
        }
        switch (wp) {
        case IDM_COPY:  copy(); break;
        case IDM_PASTE: paste(); break;
        case IDM_ABOUT:
            MessageBox(h, "Calculator\nNative Windows 3.x API for murm386.", "About Calculator",
                       MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    case WM_CHAR: {
        int id = key_char((int)wp);
        if ((int)wp == 13 && GetKeyState(VK_CONTROL) < 0)
            id = ID_MS;             /* Ctrl+M arrives as CR */
        if (id)
            press(id);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_DELETE)
            press(ID_CE);
        else if (wp == VK_F9)
            press(ID_SIGN);
        return 0;
    case WM_TIMER:
        KillTimer(h, 1);
        if (pressed)
            SendMessage(pressed, BM_SETSTATE, FALSE, 0);
        pressed = 0;
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
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = CalcProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "SciCalc";
        if (!RegisterClass(&wc))
            return 1;
    }
    HMENU edit = CreatePopupMenu();
    AppendMenu(edit, MF_STRING, IDM_COPY, "&Copy\tCtrl+C");
    AppendMenu(edit, MF_STRING, IDM_PASTE, "&Paste\tCtrl+V");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Calculator...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)edit, "&Edit");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");
    static const ACCEL_N keys[] = {
        { FVIRTKEY | FCONTROL, 'C', IDM_COPY },
        { FVIRTKEY | FCONTROL, 'V', IDM_PASTE },
        { FVIRTKEY | FCONTROL, VK_INSERT, IDM_COPY },
        { FVIRTKEY | FSHIFT, VK_INSERT, IDM_PASTE },
    };
    HACCEL acc_table = CreateAcceleratorTableN(keys, 4);

    /* the window around the buttons */
    RECT r = { 0, 0, col_x(5) + BW + MARGIN, row_y(4) + BH + MARGIN };
    AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE, 0);
    HWND h = CreateWindow("SciCalc", "Calculator",
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                          0, bar, inst, NULL);
    if (!h)
        return 1;
    ShowWindow(h, show);
    UpdateWindow(h);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        if (TranslateAccelerator(h, acc_table, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DestroyAcceleratorTableN(acc_table);
    return (int)m.wParam;
}
