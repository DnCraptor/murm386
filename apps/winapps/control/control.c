/*
 * control.c - CONTROL.EXE: the Control Panel, with the settings of
 * Windows 3.1 that apply here:
 *
 *   Color      the 21 system colours, named colour schemes (CONTROL.INI
 *              [color schemes]), saved to WIN.INI [colors]
 *   Desktop    the desktop pattern (CONTROL.INI [Patterns]), WIN.INI
 *              [Desktop] Pattern
 *   Mouse      the double-click speed and the swapped buttons, WIN.INI
 *              [windows] DoubleClickSpeed, SwapMouseButtons
 *   Date/Time  the DOS date and time
 *
 * The colours offered are the 16 of the VGA, which every display driver
 * shows as they are.
 */
#include "windows.h"

#define INI         "CONTROL.INI"

enum { APP_COLOR, APP_DESKTOP, APP_MOUSE, APP_DATETIME, APP_COUNT };

#define IDM_COLOR    100
#define IDM_EXIT     110
#define IDM_ABOUT    111

static HINSTANCE hinst;
static HWND      hmain;
static int       sel;               /* the applet selected */
static const char *const app_names[APP_COUNT] = { "Color", "Desktop", "Mouse", "Date/Time" };

/* ---------------------------------------------------------------- shared */

static const COLORREF vga16[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255),
};

static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    RECT rc = { l, t, r, b };
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

static void frame(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    RECT rc = { l, t, r, b };
    FrameRect(dc, &rc, br);
    DeleteObject(br);
}

static int read_ints(const char *s, int *v, int max)
{
    int k = 0;
    while (*s && k < max) {
        while (*s == ' ' || *s == ',')
            s++;
        if (*s < '0' || *s > '9')
            break;
        int n = 0;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + (*s++ - '0');
        v[k++] = n;
    }
    return k;
}

/* ---------------------------------------------------------------- Color */

/* the elements as Control Panel names them, and their COLOR_ index */
static const struct { const char *name; int index; } elements[] = {
    { "Desktop", COLOR_BACKGROUND },
    { "Application Workspace", COLOR_APPWORKSPACE },
    { "Window Background", COLOR_WINDOW },
    { "Window Text", COLOR_WINDOWTEXT },
    { "Menu Bar", COLOR_MENU },
    { "Menu Text", COLOR_MENUTEXT },
    { "Active Title Bar", COLOR_ACTIVECAPTION },
    { "Inactive Title Bar", COLOR_INACTIVECAPTION },
    { "Active Title Bar Text", COLOR_CAPTIONTEXT },
    { "Inactive Title Bar Text", COLOR_INACTIVECAPTIONTEXT },
    { "Active Border", COLOR_ACTIVEBORDER },
    { "Inactive Border", COLOR_INACTIVEBORDER },
    { "Window Frame", COLOR_WINDOWFRAME },
    { "Scroll Bars", COLOR_SCROLLBAR },
    { "Button Face", COLOR_BTNFACE },
    { "Button Shadow", COLOR_BTNSHADOW },
    { "Button Text", COLOR_BTNTEXT },
    { "Button Highlight", COLOR_BTNHIGHLIGHT },
    { "Disabled Text", COLOR_GRAYTEXT },
    { "Highlight", COLOR_HIGHLIGHT },
    { "Highlighted Text", COLOR_HIGHLIGHTTEXT },
};
#define NELEM ((int)(sizeof(elements) / sizeof(elements[0])))
#define NCOLORS 21

/* the WIN.INI [colors] keys, in COLOR_ order */
static const char *const color_keys[NCOLORS] = {
    "Scrollbar", "Background", "ActiveTitle", "InactiveTitle", "Menu", "Window",
    "WindowFrame", "MenuText", "WindowText", "TitleText", "ActiveBorder",
    "InactiveBorder", "AppWorkspace", "Hilight", "HilightText", "ButtonFace",
    "ButtonShadow", "GrayText", "ButtonText", "InactiveTitleText", "ButtonHilight",
};

/* the colours of Windows 3.1 out of the box, COLOR_ order */
static const COLORREF default_scheme[NCOLORS] = {
    RGB(192, 192, 192), RGB(0, 128, 128), RGB(0, 0, 128), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(0, 0, 0), RGB(0, 0, 0),
    RGB(0, 0, 0), RGB(255, 255, 255), RGB(192, 192, 192), RGB(192, 192, 192),
    RGB(255, 255, 255), RGB(0, 0, 128), RGB(255, 255, 255), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(128, 128, 128), RGB(0, 0, 0), RGB(0, 0, 0),
    RGB(255, 255, 255),
};

static COLORREF edit_colors[NCOLORS];  /* what the dialog shows */
static int      cur_elem;               /* index into elements[] */

#define IDC_SCHEMES  201
#define IDC_SAVE     202
#define IDC_REMOVE   203
#define IDC_PREVIEW  204
#define IDC_ELEMENT  205
#define IDC_PALETTE  206
#define IDC_NAME     207

/* scheme "name=RRGGBB,RRGGBB,..." (21 hex colours, COLOR_ order) */
static int parse_scheme(const char *s, COLORREF *out)
{
    for (int i = 0; i < NCOLORS; i++) {
        unsigned v = 0;
        int n = 0;
        while (*s == ' ' || *s == ',')
            s++;
        for (; n < 6; n++, s++) {
            char c = *s;
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0)
                return 0;
            v = v * 16 + (unsigned)d;
        }
        out[i] = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }
    return 1;
}

static void scheme_text(const COLORREF *c, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (int i = 0; i < NCOLORS; i++) {
        unsigned v = ((unsigned)GetRValue(c[i]) << 16) | ((unsigned)GetGValue(c[i]) << 8) | GetBValue(c[i]);
        if (i)
            out[o++] = ',';
        for (int k = 5; k >= 0; k--)
            out[o++] = hex[(v >> (k * 4)) & 15];
    }
    out[o] = 0;
}

static void fill_schemes(HWND dlg, const char *select)
{
    static char names[1024];
    HWND cb = GetDlgItem(dlg, IDC_SCHEMES);
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(intptr_t)"Windows Default");
    GetPrivateProfileString("color schemes", NULL, "", names, sizeof(names), INI);
    for (const char *p = names; *p; p += lstrlen(p) + 1)
        if (lstrcmpi(p, "Windows Default"))
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(intptr_t)p);
    int i = select ? (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(intptr_t)select) : -1;
    SendMessage(cb, CB_SETCURSEL, (WPARAM)(i >= 0 ? i : -1), 0);
}

static void use_scheme(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_SCHEMES);
    int i = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
    if (i < 0)
        return;
    char name[64], val[200];
    SendMessage(cb, CB_GETLBTEXT, (WPARAM)i, (LPARAM)(intptr_t)name);
    if (!lstrcmpi(name, "Windows Default")) {
        for (int k = 0; k < NCOLORS; k++)
            edit_colors[k] = default_scheme[k];
    } else {
        GetPrivateProfileString("color schemes", name, "", val, sizeof(val), INI);
        COLORREF c[NCOLORS];
        if (parse_scheme(val, c))
            for (int k = 0; k < NCOLORS; k++)
                edit_colors[k] = c[k];
    }
    InvalidateRect(GetDlgItem(dlg, IDC_PREVIEW), NULL, FALSE);
    InvalidateRect(GetDlgItem(dlg, IDC_PALETTE), NULL, FALSE);
}

/* the sample: an inactive and an active window, a menu, text, a button */
static LRESULT CALLBACK PreviewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg != WM_PAINT)
        return DefWindowProc(h, msg, wp, lp);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    const COLORREF *c = edit_colors;
    RECT rc;
    GetClientRect(h, &rc);
    int w = rc.right, ht = rc.bottom;
    SetBkMode(dc, TRANSPARENT);
    fill(dc, 0, 0, w, ht, c[COLOR_BACKGROUND]);
    frame(dc, 0, 0, w, ht, RGB(0, 0, 0));
    /* inactive window */
    int x0 = 8, y0 = 6, x1 = w * 3 / 4, y1 = ht / 2;
    fill(dc, x0, y0, x1, y1, c[COLOR_INACTIVEBORDER]);
    frame(dc, x0, y0, x1, y1, c[COLOR_WINDOWFRAME]);
    fill(dc, x0 + 3, y0 + 3, x1 - 3, y0 + 20, c[COLOR_INACTIVECAPTION]);
    RECT t = { x0 + 3, y0 + 3, x1 - 3, y0 + 20 };
    SetTextColor(dc, c[COLOR_INACTIVECAPTIONTEXT]);
    DrawText(dc, "Inactive", -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    /* active window: caption, menu bar, workspace-like client */
    x0 = w / 6; y0 = ht / 4; x1 = w - 8; y1 = ht - 6;
    fill(dc, x0, y0, x1, y1, c[COLOR_ACTIVEBORDER]);
    frame(dc, x0, y0, x1, y1, c[COLOR_WINDOWFRAME]);
    fill(dc, x0 + 3, y0 + 3, x1 - 3, y0 + 20, c[COLOR_ACTIVECAPTION]);
    RECT a = { x0 + 3, y0 + 3, x1 - 3, y0 + 20 };
    SetTextColor(dc, c[COLOR_CAPTIONTEXT]);
    DrawText(dc, "Active", -1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int my = y0 + 21;
    fill(dc, x0 + 3, my, x1 - 3, my + 17, c[COLOR_MENU]);
    SetTextColor(dc, c[COLOR_MENUTEXT]);
    TextOut(dc, x0 + 8, my + 1, "Normal", 6);
    SetTextColor(dc, c[COLOR_GRAYTEXT]);
    TextOut(dc, x0 + 68, my + 1, "Disabled", 8);
    fill(dc, x0 + 140, my, x0 + 210, my + 17, c[COLOR_HIGHLIGHT]);
    SetTextColor(dc, c[COLOR_HIGHLIGHTTEXT]);
    TextOut(dc, x0 + 142, my + 1, "Highlight", 9);
    int cy = my + 18;
    fill(dc, x0 + 3, cy, x1 - 3, y1 - 3, c[COLOR_WINDOW]);
    /* scroll bar on the right */
    fill(dc, x1 - 20, cy, x1 - 3, y1 - 3, c[COLOR_SCROLLBAR]);
    frame(dc, x1 - 20, cy, x1 - 3, y1 - 3, c[COLOR_WINDOWFRAME]);
    SetTextColor(dc, c[COLOR_WINDOWTEXT]);
    TextOut(dc, x0 + 10, cy + 6, "Window Text", 11);
    /* a button */
    int bx = x0 + 10, by = cy + 28;
    if (by + 22 < y1 - 3) {
        fill(dc, bx, by, bx + 60, by + 22, c[COLOR_BTNFACE]);
        frame(dc, bx, by, bx + 60, by + 22, c[COLOR_WINDOWFRAME]);
        fill(dc, bx + 1, by + 1, bx + 59, by + 2, c[COLOR_BTNHIGHLIGHT]);
        fill(dc, bx + 1, by + 1, bx + 2, by + 21, c[COLOR_BTNHIGHLIGHT]);
        fill(dc, bx + 1, by + 20, bx + 59, by + 21, c[COLOR_BTNSHADOW]);
        fill(dc, bx + 58, by + 1, bx + 59, by + 21, c[COLOR_BTNSHADOW]);
        RECT b = { bx, by, bx + 60, by + 22 };
        SetTextColor(dc, c[COLOR_BTNTEXT]);
        DrawText(dc, "OK", -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    EndPaint(h, &ps);
    return 0;
}

/* 16 swatches, 2 rows; the colour of the current element is framed */
static LRESULT CALLBACK PaletteProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT rc;
    GetClientRect(h, &rc);
    int sw = rc.right / 8, sh = rc.bottom / 2;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        fill(dc, 0, 0, rc.right, rc.bottom, GetSysColor(COLOR_BTNFACE));
        COLORREF cur = edit_colors[elements[cur_elem].index];
        for (int i = 0; i < 16; i++) {
            int x = (i % 8) * sw, y = (i / 8) * sh;
            fill(dc, x + 3, y + 3, x + sw - 3, y + sh - 3, vga16[i]);
            frame(dc, x + 3, y + 3, x + sw - 3, y + sh - 3, RGB(0, 0, 0));
            if (vga16[i] == cur) {
                frame(dc, x, y, x + sw, y + sh, RGB(0, 0, 0));
                frame(dc, x + 1, y + 1, x + sw - 1, y + sh - 1, RGB(0, 0, 0));
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp) / (sw ? sw : 1), y = (short)HIWORD(lp) / (sh ? sh : 1);
        if (x >= 0 && x < 8 && y >= 0 && y < 2) {
            edit_colors[elements[cur_elem].index] = vga16[y * 8 + x];
            InvalidateRect(h, NULL, FALSE);
            HWND dlg = GetParent(h);
            InvalidateRect(GetDlgItem(dlg, IDC_PREVIEW), NULL, FALSE);
            SendDlgItemMessage(dlg, IDC_SCHEMES, CB_SETCURSEL, (WPARAM)-1, 0);
        }
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

static char scheme_name[64];

static BOOL CALLBACK NameProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SendDlgItemMessage(dlg, IDC_NAME, EM_LIMITTEXT, sizeof(scheme_name) - 1, 0);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(dlg, IDC_NAME, scheme_name, sizeof(scheme_name));
            EndDialog(dlg, scheme_name[0] ? IDOK : IDCANCEL);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

static const DLGTEMPLATE_N name_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 4, 40, 50, 160, 50,
    NULL, NULL, "Save Scheme", 0, NULL
};
static const DLGITEM_N name_items[] = {
    { 6, 6, 148, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Save this color scheme as:" },
    { 6, 16, 148, 12, IDC_NAME, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 32, 32, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 84, 32, 44, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static BOOL CALLBACK ColorProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        for (int i = 0; i < NCOLORS; i++)
            edit_colors[i] = GetSysColor(i);
        char cur[64];
        GetPrivateProfileString("current", "color schemes", "", cur, sizeof(cur), INI);
        if (!cur[0]) {
            int same = 1;
            for (int i = 0; i < NCOLORS; i++)
                same &= edit_colors[i] == default_scheme[i];
            if (same)
                lstrcpy(cur, "Windows Default");
        }
        fill_schemes(dlg, cur[0] ? cur : NULL);
        HWND el = GetDlgItem(dlg, IDC_ELEMENT);
        for (int i = 0; i < NELEM; i++)
            SendMessage(el, CB_ADDSTRING, 0, (LPARAM)(intptr_t)elements[i].name);
        cur_elem = 0;
        SendMessage(el, CB_SETCURSEL, 0, 0);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_SCHEMES:
            if (HIWORD(lp) == CBN_SELCHANGE)
                use_scheme(dlg);
            return TRUE;
        case IDC_ELEMENT:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                cur_elem = (int)SendDlgItemMessage(dlg, IDC_ELEMENT, CB_GETCURSEL, 0, 0);
                if (cur_elem < 0)
                    cur_elem = 0;
                InvalidateRect(GetDlgItem(dlg, IDC_PALETTE), NULL, FALSE);
            }
            return TRUE;
        case IDC_SAVE: {
            scheme_name[0] = 0;
            if (DialogBoxIndirectN(hinst, &name_tpl, name_items, dlg, NameProc) != IDOK)
                return TRUE;
            if (!lstrcmpi(scheme_name, "Windows Default")) {
                MessageBox(dlg, "The Windows Default scheme cannot be changed.", "Color",
                           MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            char val[200];
            scheme_text(edit_colors, val);
            WritePrivateProfileString("color schemes", scheme_name, val, INI);
            fill_schemes(dlg, scheme_name);
            return TRUE;
        }
        case IDC_REMOVE: {
            HWND cb = GetDlgItem(dlg, IDC_SCHEMES);
            int i = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
            char name[64];
            if (i < 0)
                return TRUE;
            SendMessage(cb, CB_GETLBTEXT, (WPARAM)i, (LPARAM)(intptr_t)name);
            if (!lstrcmpi(name, "Windows Default")) {
                MessageBox(dlg, "The Windows Default scheme cannot be removed.", "Color",
                           MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            char q[120];
            wsprintf(q, "Are you sure you want to remove the scheme '%s'?", name);
            if (MessageBox(dlg, q, "Color", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                WritePrivateProfileString("color schemes", name, NULL, INI);
                fill_schemes(dlg, NULL);
            }
            return TRUE;
        }
        case IDOK: {
            int idx[NCOLORS];
            for (int i = 0; i < NCOLORS; i++) {
                idx[i] = i;
                char v[16];
                wsprintf(v, "%d %d %d", GetRValue(edit_colors[i]), GetGValue(edit_colors[i]),
                         GetBValue(edit_colors[i]));
                WriteProfileString("colors", color_keys[i], v);
            }
            HWND cb = GetDlgItem(dlg, IDC_SCHEMES);
            int s = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
            char name[64] = "";
            if (s >= 0)
                SendMessage(cb, CB_GETLBTEXT, (WPARAM)s, (LPARAM)(intptr_t)name);
            WritePrivateProfileString("current", "color schemes", name[0] ? name : NULL, INI);
            EndDialog(dlg, IDOK);
            SetSysColors(NCOLORS, idx, edit_colors);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static const DLGTEMPLATE_N color_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 11, 16, 10, 248, 186,
    NULL, NULL, "Color", 0, NULL
};
static const DLGITEM_N color_items[] = {
    { 6, 5, 80, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Color &Schemes" },
    { 6, 15, 132, 80, IDC_SCHEMES, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, "COMBOBOX", "" },
    { 144, 14, 46, 14, IDC_SAVE, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Sa&ve Scheme" },
    { 196, 14, 46, 14, IDC_REMOVE, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Remo&ve" },
    { 6, 34, 236, 76, IDC_PREVIEW, WS_VISIBLE, "CPPreview", "" },
    { 6, 116, 80, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Screen &Element:" },
    { 6, 126, 120, 100, IDC_ELEMENT, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, "COMBOBOX", "" },
    { 132, 116, 110, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Basic Colors:" },
    { 132, 126, 110, 32, IDC_PALETTE, WS_VISIBLE, "CPPalette", "" },
    { 70, 166, 46, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 132, 166, 46, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

/* ---------------------------------------------------------------- Desktop */

#define IDC_PATTERNS 301
#define IDC_PATSHOW  302

/* the patterns of Windows 3.1, written to CONTROL.INI when it has none */
static const char *const std_patterns[] = {
    "50% Gray=170 85 170 85 170 85 170 85",
    "Boxes=127 65 65 65 65 65 127 0",
    "Critters=0 80 114 32 0 5 39 2",
    "Diamonds=32 80 136 80 32 0 0 0",
    "Paisley=2 7 7 2 32 80 80 32",
    "Quilt=130 68 40 17 40 68 130 1",
    "Scottie=64 192 200 120 120 72 0 0",
    "Spinner=20 12 200 121 158 19 48 40",
    "Thatches=248 116 34 71 143 23 34 113",
    "Tulip=0 0 84 124 124 56 146 124",
    "Waffle=0 0 0 0 128 128 128 240",
    "Weave=136 84 34 69 136 21 34 81",
};

static char pat_bits[48];           /* the pattern shown */

static LRESULT CALLBACK PatternProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg != WM_PAINT)
        return DefWindowProc(h, msg, wp, lp);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    COLORREF bg = GetSysColor(COLOR_BACKGROUND);
    int rows[8];
    int k = read_ints(pat_bits, rows, 8);
    fill(dc, 0, 0, rc.right, rc.bottom, bg);
    if (k == 8) {
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        for (int y = 1; y < rc.bottom - 1; y++)
            for (int x = 1; x < rc.right - 1; x++)
                if (rows[y & 7] & (0x80 >> (x & 7))) {
                    RECT p = { x, y, x + 1, y + 1 };
                    FillRect(dc, &p, black);
                }
        DeleteObject(black);
    }
    frame(dc, 0, 0, rc.right, rc.bottom, RGB(0, 0, 0));
    EndPaint(h, &ps);
    return 0;
}

static void show_pattern(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_PATTERNS);
    int i = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
    char name[40] = "(None)";
    if (i >= 0)
        SendMessage(cb, CB_GETLBTEXT, (WPARAM)i, (LPARAM)(intptr_t)name);
    if (!lstrcmp(name, "(None)"))
        lstrcpy(pat_bits, "(None)");
    else
        GetPrivateProfileString("Patterns", name, "(None)", pat_bits, sizeof(pat_bits), INI);
    InvalidateRect(GetDlgItem(dlg, IDC_PATSHOW), NULL, FALSE);
}

static BOOL CALLBACK DesktopProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        static char names[1024];
        if (!GetPrivateProfileString("Patterns", NULL, "", names, sizeof(names), INI)) {
            for (unsigned i = 0; i < sizeof(std_patterns) / sizeof(std_patterns[0]); i++) {
                char key[24];
                const char *eq = std_patterns[i];
                int n = 0;
                while (eq[n] != '=')
                    n++;
                lstrcpyn(key, eq, n + 1);
                WritePrivateProfileString("Patterns", key, eq + n + 1, INI);
            }
            GetPrivateProfileString("Patterns", NULL, "", names, sizeof(names), INI);
        }
        HWND cb = GetDlgItem(dlg, IDC_PATTERNS);
        SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(intptr_t)"(None)");
        for (const char *p = names; *p; p += lstrlen(p) + 1)
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(intptr_t)p);
        /* the current one: the entry with the same bits */
        char cur[48];
        GetProfileString("Desktop", "Pattern", "(None)", cur, sizeof(cur));
        int pick = 0;
        for (const char *p = names; *p; p += lstrlen(p) + 1) {
            char bits[48];
            GetPrivateProfileString("Patterns", p, "", bits, sizeof(bits), INI);
            int a[8], b[8];
            if (read_ints(bits, a, 8) == 8 && read_ints(cur, b, 8) == 8) {
                int same = 1;
                for (int k = 0; k < 8; k++)
                    same &= a[k] == b[k];
                if (same)
                    pick = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(intptr_t)p);
            }
        }
        SendMessage(cb, CB_SETCURSEL, (WPARAM)(pick > 0 ? pick : 0), 0);
        show_pattern(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_PATTERNS:
            if (HIWORD(lp) == CBN_SELCHANGE)
                show_pattern(dlg);
            return TRUE;
        case IDOK:
            EndDialog(dlg, IDOK);
            SystemParametersInfo(SPI_SETDESKPATTERN, 0, pat_bits, SPIF_UPDATEINIFILE);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static const DLGTEMPLATE_N desk_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 6, 40, 30, 180, 92,
    NULL, NULL, "Desktop", 0, NULL
};
static const DLGITEM_N desk_items[] = {
    { 6, 6, 60, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Pattern" },
    { 6, 16, 90, 100, IDC_PATTERNS, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, "COMBOBOX", "" },
    { 104, 6, 70, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Sample:" },
    { 104, 16, 70, 44, IDC_PATSHOW, WS_VISIBLE, "CPPattern", "" },
    { 34, 72, 46, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 100, 72, 46, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

/* ---------------------------------------------------------------- Mouse */

#define IDC_SPEED    401
#define IDC_TEST     402
#define IDC_SPEEDVAL 403
#define IDC_SWAP     404

static UINT test_speed, test_last;
static int  test_on;

/* the TEST box: a double click (at the speed being set) turns it over */
static LRESULT CALLBACK TestProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: {
        UINT now = (UINT)GetTickCount();
        if (test_last && now - test_last <= test_speed) {
            test_on = !test_on;
            test_last = 0;
            InvalidateRect(h, NULL, FALSE);
        } else {
            test_last = now;
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        fill(dc, 0, 0, rc.right, rc.bottom, test_on ? RGB(0, 0, 0) : RGB(255, 255, 255));
        frame(dc, 0, 0, rc.right, rc.bottom, RGB(0, 0, 0));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, test_on ? RGB(255, 255, 255) : RGB(0, 0, 0));
        DrawText(dc, "TEST", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* the scroll bar runs from slow (900 ms) to fast (100 ms) */
static int speed_to_pos(UINT ms) { return ms >= 900 ? 0 : ms <= 100 ? 16 : (int)(900 - ms) / 50; }
static UINT pos_to_speed(int pos) { return (UINT)(900 - pos * 50); }

static void show_speed(HWND dlg)
{
    char s[24];
    wsprintf(s, "%u ms", test_speed);
    SetDlgItemText(dlg, IDC_SPEEDVAL, s);
}

static BOOL CALLBACK MouseProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        test_speed = GetDoubleClickTime();
        test_on = 0;
        test_last = 0;
        HWND sb = GetDlgItem(dlg, IDC_SPEED);
        SetScrollRange(sb, SB_CTL, 0, 16, FALSE);
        SetScrollPos(sb, SB_CTL, speed_to_pos(test_speed), TRUE);
        show_speed(dlg);
        CheckDlgButton(dlg, IDC_SWAP, GetSystemMetrics(SM_SWAPBUTTON) ? 1 : 0);
        return TRUE;
    }
    case WM_HSCROLL: {
        HWND sb = GetDlgItem(dlg, IDC_SPEED);
        int pos = GetScrollPos(sb, SB_CTL);
        switch (wp) {
        case SB_LINEUP: pos--; break;
        case SB_LINEDOWN: pos++; break;
        case SB_PAGEUP: pos -= 4; break;
        case SB_PAGEDOWN: pos += 4; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = (short)LOWORD(lp); break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = 16; break;
        }
        if (pos < 0) pos = 0;
        if (pos > 16) pos = 16;
        SetScrollPos(sb, SB_CTL, pos, TRUE);
        test_speed = pos_to_speed(pos);
        show_speed(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            EndDialog(dlg, IDOK);
            SystemParametersInfo(SPI_SETDOUBLECLICKTIME, test_speed, NULL, SPIF_UPDATEINIFILE);
            SystemParametersInfo(SPI_SETMOUSEBUTTONSWAP, IsDlgButtonChecked(dlg, IDC_SWAP) ? 1 : 0,
                                 NULL, SPIF_UPDATEINIFILE);
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

static const DLGTEMPLATE_N mouse_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 9, 40, 30, 180, 96,
    NULL, NULL, "Mouse", 0, NULL
};
static const DLGITEM_N mouse_items[] = {
    { 6, 6, 110, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Double Click Speed" },
    { 6, 18, 110, 10, IDC_SPEED, SBS_HORZ | WS_TABSTOP | WS_VISIBLE, "SCROLLBAR", "" },
    { 6, 30, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Slow" },
    { 96, 30, 20, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "Fast" },
    { 36, 42, 50, 9, IDC_SPEEDVAL, SS_CENTER | WS_VISIBLE, "STATIC", "" },
    { 128, 14, 44, 24, IDC_TEST, WS_VISIBLE, "CPTest", "" },
    { 6, 56, 168, 10, IDC_SWAP, BS_AUTOCHECKBOX | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Swap Left/Right Buttons" },
    { 34, 76, 46, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 100, 76, 46, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

/* ---------------------------------------------------------------- Date/Time */

#define IDC_MONTH 501
#define IDC_DAY   502
#define IDC_YEAR  503
#define IDC_HOUR  504
#define IDC_MIN   505
#define IDC_SEC   506

static int get_num(HWND dlg, int id, int lo, int hi, int *v)
{
    BOOL ok;
    UINT n = GetDlgItemInt(dlg, id, &ok, FALSE);
    if (!ok || (int)n < lo || (int)n > hi) {
        SetFocus(GetDlgItem(dlg, id));
        SendDlgItemMessage(dlg, id, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return 0;
    }
    *v = (int)n;
    return 1;
}

static BOOL CALLBACK DateProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        SYSTEMTIME_N t;
        GetLocalTimeN(&t);
        SetDlgItemInt(dlg, IDC_MONTH, t.wMonth, FALSE);
        SetDlgItemInt(dlg, IDC_DAY, t.wDay, FALSE);
        SetDlgItemInt(dlg, IDC_YEAR, t.wYear, FALSE);
        SetDlgItemInt(dlg, IDC_HOUR, t.wHour, FALSE);
        SetDlgItemInt(dlg, IDC_MIN, t.wMinute, FALSE);
        SetDlgItemInt(dlg, IDC_SEC, t.wSecond, FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            static const int mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            int mo, d, y, h, mi, s;
            if (!get_num(dlg, IDC_MONTH, 1, 12, &mo) || !get_num(dlg, IDC_YEAR, 1980, 2099, &y) ||
                !get_num(dlg, IDC_DAY, 1, mdays[mo - 1], &d) || !get_num(dlg, IDC_HOUR, 0, 23, &h) ||
                !get_num(dlg, IDC_MIN, 0, 59, &mi) || !get_num(dlg, IDC_SEC, 0, 59, &s)) {
                MessageBox(dlg, "That is not a valid date or time.", "Date/Time", MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            if (mo == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
                SetFocus(GetDlgItem(dlg, IDC_DAY));
                MessageBox(dlg, "That is not a valid date or time.", "Date/Time", MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            SYSTEMTIME_N t = { 0 };
            t.wYear = (WORD)y; t.wMonth = (WORD)mo; t.wDay = (WORD)d;
            t.wHour = (WORD)h; t.wMinute = (WORD)mi; t.wSecond = (WORD)s;
            if (!SetLocalTimeN(&t)) {
                MessageBox(dlg, "The date and time could not be set.", "Date/Time", MB_OK | MB_ICONSTOP);
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

#define NUMEDIT (ES_LEFT | WS_BORDER | WS_TABSTOP | WS_VISIBLE)

static const DLGTEMPLATE_N date_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 14, 40, 30, 150, 72,
    NULL, NULL, "Date & Time", 0, NULL
};
static const DLGITEM_N date_items[] = {
    { 6, 8, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Date" },
    { 40, 6, 16, 12, IDC_MONTH, NUMEDIT, "EDIT", "" },
    { 57, 8, 4, 9, -1, SS_CENTER | WS_VISIBLE, "STATIC", "/" },
    { 62, 6, 16, 12, IDC_DAY, NUMEDIT, "EDIT", "" },
    { 79, 8, 4, 9, -1, SS_CENTER | WS_VISIBLE, "STATIC", "/" },
    { 84, 6, 26, 12, IDC_YEAR, NUMEDIT, "EDIT", "" },
    { 6, 28, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Time" },
    { 40, 26, 16, 12, IDC_HOUR, NUMEDIT, "EDIT", "" },
    { 57, 28, 4, 9, -1, SS_CENTER | WS_VISIBLE, "STATIC", ":" },
    { 62, 26, 16, 12, IDC_MIN, NUMEDIT, "EDIT", "" },
    { 79, 28, 4, 9, -1, SS_CENTER | WS_VISIBLE, "STATIC", ":" },
    { 84, 26, 16, 12, IDC_SEC, NUMEDIT, "EDIT", "" },
    { 22, 50, 46, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 80, 50, 46, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

/* ---------------------------------------------------------------- main window */

static void run_applet(int i)
{
    switch (i) {
    case APP_COLOR:    DialogBoxIndirectN(hinst, &color_tpl, color_items, hmain, ColorProc); break;
    case APP_DESKTOP:  DialogBoxIndirectN(hinst, &desk_tpl, desk_items, hmain, DesktopProc); break;
    case APP_MOUSE:    DialogBoxIndirectN(hinst, &mouse_tpl, mouse_items, hmain, MouseProc); break;
    case APP_DATETIME: DialogBoxIndirectN(hinst, &date_tpl, date_items, hmain, DateProc); break;
    }
}

#define CELL_W 80
#define CELL_H 64

static int hit(int x, int y)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    int cols = rc.right / CELL_W;
    if (cols < 1)
        cols = 1;
    int i = (y - 4) / CELL_H * cols + x / CELL_W;
    return x >= 0 && y >= 4 && x / CELL_W < cols && i >= 0 && i < APP_COUNT ? i : -1;
}

static void paint_main(HDC dc)
{
    RECT rc;
    GetClientRect(hmain, &rc);
    int cols = rc.right / CELL_W;
    if (cols < 1)
        cols = 1;
    HICON icon = LoadIcon(0, IDI_APPLICATION);
    int focus = GetFocus() == hmain;
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < APP_COUNT; i++) {
        int x = (i % cols) * CELL_W, y = 4 + (i / cols) * CELL_H;
        DrawIcon(dc, x + (CELL_W - 32) / 2, y + 4, icon);
        RECT t = { x + 2, y + 40, x + CELL_W - 2, y + CELL_H };
        RECT m = t;
        DrawText(dc, app_names[i], -1, &m, DT_CALCRECT | DT_SINGLELINE);
        int tw = m.right - m.left;
        int on = i == sel && focus;
        if (on) {
            RECT b = { x + (CELL_W - tw) / 2 - 1, t.top, x + (CELL_W + tw) / 2 + 1, t.top + (m.bottom - m.top) };
            FillRect(dc, &b, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        }
        SetTextColor(dc, GetSysColor(on ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        DrawText(dc, app_names[i], -1, &t, DT_CENTER | DT_SINGLELINE);
    }
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hmain = h;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_main(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int i = hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (i >= 0 && i != sel) {
            sel = i;
            InvalidateRect(h, NULL, TRUE);
        }
        if (msg == WM_LBUTTONDBLCLK && i >= 0)
            run_applet(i);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_LEFT || wp == VK_UP)
            sel = (sel + APP_COUNT - 1) % APP_COUNT;
        else if (wp == VK_RIGHT || wp == VK_DOWN)
            sel = (sel + 1) % APP_COUNT;
        else if (wp == VK_RETURN) {
            run_applet(sel);
            return 0;
        } else
            return 0;
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_COMMAND:
        if (wp >= IDM_COLOR && wp < IDM_COLOR + APP_COUNT)
            run_applet((int)wp - IDM_COLOR);
        else if (wp == IDM_EXIT)
            DestroyWindow(h);
        else if (wp == IDM_ABOUT)
            MessageBox(h, "Control Panel\nNative Windows 3.x API for murm386.", "About Control Panel",
                       MB_OK | MB_ICONINFORMATION);
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
        wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));   /* its own icon */
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "CtlPanelClass";
        if (!RegisterClass(&wc))
            return 1;
        /* the sample and palette controls of the dialogs */
        wc.style = 0;
        wc.hIcon = 0;
        wc.hbrBackground = 0;
        wc.lpfnWndProc = PreviewProc;
        wc.lpszClassName = "CPPreview";
        RegisterClass(&wc);
        wc.lpfnWndProc = PaletteProc;
        wc.lpszClassName = "CPPalette";
        RegisterClass(&wc);
        wc.lpfnWndProc = PatternProc;
        wc.lpszClassName = "CPPattern";
        RegisterClass(&wc);
        wc.lpfnWndProc = TestProc;
        wc.lpszClassName = "CPTest";
        RegisterClass(&wc);
    }
    HMENU settings = CreatePopupMenu();
    AppendMenu(settings, MF_STRING, IDM_COLOR + APP_COLOR, "&Color...");
    AppendMenu(settings, MF_STRING, IDM_COLOR + APP_DESKTOP, "&Desktop...");
    AppendMenu(settings, MF_STRING, IDM_COLOR + APP_MOUSE, "&Mouse...");
    AppendMenu(settings, MF_STRING, IDM_COLOR + APP_DATETIME, "Date/&Time...");
    AppendMenu(settings, MF_SEPARATOR, 0, NULL);
    AppendMenu(settings, MF_STRING, IDM_EXIT, "E&xit");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Control Panel...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)settings, "&Settings");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");
    HWND h = CreateWindow("CtlPanelClass", "Control Panel", WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, CELL_W * APP_COUNT + 16, 120,
                          0, bar, inst, NULL);
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
