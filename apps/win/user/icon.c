/*
 * icon.c - cursor and icon handles, built-in shapes, keyboard layout.
 *
 * Cursor and icon handles are WOBJ_CURSOR entries whose object pointer
 * carries a small id: 1..WCUR_COUNT built-in cursors, 101.. built-in icons;
 * or points at a uicon, an icon made from an application's winapp_info
 * (ExtractIconN, LoadIcon of the application's instance).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

static HANDLE shape_handles[16];

static HANDLE shape_handle(int slot, int id)
{
    if (!shape_handles[slot]) {
        shape_handles[slot] = win_handle_alloc(WOBJ_CURSOR, (void *)(intptr_t)id);
        win_handle_set_stock(shape_handles[slot]);
    }
    return shape_handles[slot];
}

HCURSOR WINAPI LoadCursor(HINSTANCE inst, LPCSTR name)
{
    (void)inst;
    if (!IS_INTRESOURCE(name))
        return shape_handle(0, 1);
    switch ((uintptr_t)name) {
    case 32513: return shape_handle(1, 1 + WCUR_IBEAM);
    case 32514: return shape_handle(2, 1 + WCUR_WAIT);
    case 32515: return shape_handle(3, 1 + WCUR_CROSS);
    case 32644: return shape_handle(4, 1 + WCUR_SIZEWE);
    case 32645: return shape_handle(5, 1 + WCUR_SIZENS);
    default:    return shape_handle(0, 1 + WCUR_ARROW);
    }
}

/* ---------------------------------------------------------------- application icons */

#define UICON_MAGIC 0x4E434955u     /* "UICN" */
#define ICON_IDS    1000            /* below: built-in ids, not pointers */

typedef struct {
    uint32_t magic;
    HBITMAP  color;                 /* black where transparent */
    HBITMAP  mask;                  /* 1: transparent */
} uicon;

static const COLORREF vga16[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255),
};

static uicon *icon_of(HICON h)
{
    void *p = win_handle_get(h, WOBJ_CURSOR);
    if ((uintptr_t)p < ICON_IDS)
        return NULL;
    uicon *u = (uicon *)p;
    return u->magic == UICON_MAGIC ? u : NULL;
}

/* icon n (0 the first) of an application's winapp_info */
static HICON icon_from_info(const winapp_info *inf, int n)
{
    if (!inf || n < 0 || n >= inf->icon_count)
        return 0;
    const winapp_icon *ic = &inf->icon[n];
    uicon *u = (uicon *)win_alloc(sizeof(uicon));
    if (!u)
        return 0;
    u->magic = UICON_MAGIC;
    u->mask = CreateBitmap(32, 32, 1, 1, ic->and_bits);
    HDC sdc = GetDC(0);
    u->color = CreateCompatibleBitmap(sdc, 32, 32);
    HDC m = CreateCompatibleDC(sdc);
    ReleaseDC(0, sdc);
    HGDIOBJ old = SelectObject(m, u->color);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int c = (ic->xor_bits[y * 16 + x / 2] >> (x & 1 ? 0 : 4)) & 15;
            int clear = (ic->and_bits[y * 4 + x / 8] >> (7 - (x & 7))) & 1;
            SetPixel(m, x, y, clear ? RGB(0, 0, 0) : vga16[c]);
        }
    SelectObject(m, old);
    DeleteDC(m);
    HICON h = win_handle_alloc(WOBJ_CURSOR, u);
    if (!h || !u->mask || !u->color) {
        if (h)
            win_handle_free(h);
        DeleteObject(u->mask);
        DeleteObject(u->color);
        win_free(u);
        return 0;
    }
    return h;
}

BOOL WINAPI DestroyIcon(HICON icon)
{
    uicon *u = icon_of(icon);
    if (!u)
        return FALSE;               /* the built-in ones stay */
    DeleteObject(u->mask);
    DeleteObject(u->color);
    u->magic = 0;
    win_free(u);
    win_handle_free(icon);
    return TRUE;
}

/* WINN: icon n (0 the first) of an application file, 0 if there is no such
 * (free it with DestroyIcon); n (UINT)-1: how many there are. As SHELL's
 * ExtractIcon of Windows 3.1. */
HICON WINAPI ExtractIconExN(LPCSTR path, UINT n)
{
    winapp_info *inf = path ? win_app_info(path) : NULL;
    if (!inf)
        return 0;
    HICON h = n == (UINT)-1 ? (HICON)(uintptr_t)inf->icon_count : icon_from_info(inf, (int)n);
    win_free(inf);
    return h;
}

/* WINN: the program's icon (the first) of an application file */
HICON WINAPI ExtractIconN(LPCSTR path)
{
    return ExtractIconExN(path, 0);
}

/* WINN: an application file's name for the Program Manager */
int WINAPI GetAppInfoN(LPCSTR path, LPSTR desc, int size)
{
    winapp_info *inf = path && desc && size > 0 ? win_app_info(path) : NULL;
    if (!inf)
        return 0;
    lstrcpyn(desc, inf->description, size);
    win_free(inf);
    return lstrlen(desc);
}

HICON WINAPI LoadIcon(HINSTANCE inst, LPCSTR name)
{
    /* an application's own icons: MAKEINTRESOURCE(n) the n-th, a name the
     * first (Win16: its icon resources) */
    if (inst && !(IS_INTRESOURCE(name) && (uintptr_t)name >= 32512)) {
        wtask *t = task_from_handle((HTASK)inst);
        int n = IS_INTRESOURCE(name) && name ? (int)(uintptr_t)name - 1 : 0;
        if (t && t->info && n >= 0 && n < WINAPP_MAX_ICONS) {
            if (!t->icons[n])
                t->icons[n] = icon_from_info(t->info, n);
            if (t->icons[n])
                return t->icons[n];
        }
    }
    int id = IS_INTRESOURCE(name) ? (int)(uintptr_t)name - 32512 : 0;
    if (id < 0 || id > 4)
        id = 0;
    return shape_handle(8 + id, 101 + id);
}

static HCURSOR current_cursor;

HCURSOR WINAPI SetCursor(HCURSOR c)
{
    HCURSOR old = current_cursor;
    int id = (int)(intptr_t)win_handle_get(c, WOBJ_CURSOR);
    if (id >= 1 && id <= WCUR_COUNT) {
        cursor_select_builtin(id - 1);
        current_cursor = c;
    } else if (!c) {
        cursor_set_shape(NULL, NULL, 0, 0);
        current_cursor = 0;
    }
    return old;
}

int WINAPI ShowCursor(BOOL show) { return cursor_show(show); }

/* ---------------------------------------------------------------- icons */

static void circle(HDC dc, int x, int y, int d, COLORREF fill_c)
{
    HBRUSH b = CreateSolidBrush(fill_c);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(BLACK_PEN));
    Ellipse(dc, x, y, x + d, y + d);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
}

static void glyph(HDC dc, int x, int y, const char *s, COLORREF c, int bold)
{
    HFONT f = CreateFont(16, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "System");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    SIZE sz;
    GetTextExtentPoint(dc, s, lstrlen(s), &sz);
    TextOut(dc, x - sz.cx / 2, y - 8, s, lstrlen(s));
    SelectObject(dc, of);
    DeleteObject(f);
}

void user_draw_icon_id(HDC dc, int x, int y, int id)
{
    int saved = SaveDC(dc);
    switch (id) {
    case 102: {     /* hand / stop */
        POINT p[8] = { { x + 10, y + 1 }, { x + 21, y + 1 }, { x + 30, y + 10 }, { x + 30, y + 21 },
                       { x + 21, y + 30 }, { x + 10, y + 30 }, { x + 1, y + 21 }, { x + 1, y + 10 } };
        HBRUSH r = CreateSolidBrush(RGB(255, 0, 0));
        SelectObject(dc, r);
        SelectObject(dc, GetStockObject(BLACK_PEN));
        Polygon(dc, p, 8);
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        DeleteObject(r);
        glyph(dc, x + 16, y + 16, "STOP", RGB(255, 255, 255), 0);
        break;
    }
    case 103:       /* question */
        circle(dc, x + 2, y + 2, 28, RGB(255, 255, 255));
        glyph(dc, x + 16, y + 16, "?", RGB(0, 0, 128), 1);
        break;
    case 104:       /* exclamation */
        circle(dc, x + 2, y + 2, 28, RGB(255, 255, 0));
        glyph(dc, x + 16, y + 16, "!", RGB(0, 0, 0), 1);
        break;
    case 105:       /* asterisk / information */
        circle(dc, x + 2, y + 2, 28, RGB(255, 255, 255));
        glyph(dc, x + 16, y + 16, "i", RGB(0, 0, 255), 1);
        break;
    default: {      /* application: a little window */
        RECT r = { x + 2, y + 4, x + 30, y + 28 };
        FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
        FrameRect(dc, &r, GetStockObject(BLACK_BRUSH));
        RECT c = { x + 3, y + 5, x + 29, y + 10 };
        FillRect(dc, &c, user_sys_brush(COLOR_ACTIVECAPTION));
        RECT l = { x + 2, y + 10, x + 30, y + 11 };
        FillRect(dc, &l, GetStockObject(BLACK_BRUSH));
        break;
    }
    }
    RestoreDC(dc, saved);
}

BOOL WINAPI DrawIcon(HDC dc, int x, int y, HICON icon)
{
    uicon *u = icon_of(icon);
    if (u) {
        /* the mask cuts the shape out, the colours go in */
        HDC m = CreateCompatibleDC(dc);
        COLORREF fg = SetTextColor(dc, RGB(0, 0, 0)), bk = SetBkColor(dc, RGB(255, 255, 255));
        HGDIOBJ old = SelectObject(m, u->mask);
        BitBlt(dc, x, y, 32, 32, m, 0, 0, SRCAND);
        SelectObject(m, u->color);
        BitBlt(dc, x, y, 32, 32, m, 0, 0, SRCINVERT);
        SelectObject(m, old);
        DeleteDC(m);
        SetTextColor(dc, fg);
        SetBkColor(dc, bk);
        return TRUE;
    }
    int id = (int)(intptr_t)win_handle_get(icon, WOBJ_CURSOR);
    if (id < 101 || id >= ICON_IDS)
        return FALSE;
    user_draw_icon_id(dc, x, y, id);
    return TRUE;
}

/* ---------------------------------------------------------------- keyboard layout */

/* US: virtual key -> character, unshifted and shifted. */
static const struct { uint8_t vk; char n, s; } us_keys[] = {
    { '1', '1', '!' }, { '2', '2', '@' }, { '3', '3', '#' }, { '4', '4', '$' },
    { '5', '5', '%' }, { '6', '6', '^' }, { '7', '7', '&' }, { '8', '8', '*' },
    { '9', '9', '(' }, { '0', '0', ')' }, { 0xBD, '-', '_' }, { 0xBB, '=', '+' },
    { 0xDB, '[', '{' }, { 0xDD, ']', '}' }, { 0xBA, ';', ':' }, { 0xDE, '\'', '"' },
    { 0xC0, '`', '~' }, { 0xDC, '\\', '|' }, { 0xBC, ',', '<' }, { 0xBE, '.', '>' },
    { 0xBF, '/', '?' }, { 0xE2, '\\', '|' },
};

/* Russian (Windows-1251): US key -> lower-case letter. */
static const struct { uint8_t vk; uint8_t lo; } ru_keys[] = {
    { 'Q', 0xE9 }, { 'W', 0xF6 }, { 'E', 0xF3 }, { 'R', 0xEA }, { 'T', 0xE5 }, { 'Y', 0xED },
    { 'U', 0xE3 }, { 'I', 0xF8 }, { 'O', 0xF9 }, { 'P', 0xE7 }, { 0xDB, 0xF5 }, { 0xDD, 0xFA },
    { 'A', 0xF4 }, { 'S', 0xFB }, { 'D', 0xE2 }, { 'F', 0xE0 }, { 'G', 0xEF }, { 'H', 0xF0 },
    { 'J', 0xEE }, { 'K', 0xEB }, { 'L', 0xE4 }, { 0xBA, 0xE6 }, { 0xDE, 0xFD },
    { 'Z', 0xFF }, { 'X', 0xF7 }, { 'C', 0xF1 }, { 'V', 0xEC }, { 'B', 0xE8 }, { 'N', 0xF2 },
    { 'M', 0xFC }, { 0xBC, 0xE1 }, { 0xBE, 0xFE }, { 0xC0, 0xB8 },
};

static const struct { uint8_t vk; char s; } ru_shift_digits[] = {
    { '2', '"' }, { '3', (char)0xB9 }, { '4', ';' }, { '6', ':' }, { '7', '?' }, { 0xBF, ',' },
};

int user_vk_to_char(int vk, int sc, unsigned char *out)
{
    (void)sc;
    int shift = (U.keys[VK_SHIFT] & 0x80) != 0;
    int ctrl = (U.keys[VK_CONTROL] & 0x80) != 0;
    int caps = U.keys[VK_CAPITAL] & 1;
    int alt = (U.keys[VK_MENU] & 0x80) != 0;

    switch (vk) {
    case VK_BACK: *out = 8; return 1;
    case VK_TAB: *out = 9; return 1;
    case VK_RETURN: *out = ctrl ? 10 : 13; return 1;
    case VK_ESCAPE: *out = 27; return 1;
    case VK_SPACE: *out = ' '; return 1;
    case VK_MULTIPLY: *out = '*'; return 1;
    case VK_ADD: *out = '+'; return 1;
    case VK_SUBTRACT: *out = '-'; return 1;
    case VK_DIVIDE: *out = '/'; return 1;
    case VK_DECIMAL: *out = '.'; return 1;
    default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9) {
        *out = (unsigned char)('0' + vk - VK_NUMPAD0);
        return 1;
    }
    if (ctrl && !alt && vk >= 'A' && vk <= 'Z') {
        *out = (unsigned char)(vk - 'A' + 1);
        return 1;
    }
    if (ctrl && !alt)
        return 0;

    if (U.layout == 1) {
        for (unsigned i = 0; i < sizeof(ru_keys) / sizeof(ru_keys[0]); i++)
            if (ru_keys[i].vk == vk) {
                unsigned char c = ru_keys[i].lo;
                if (shift ^ caps)
                    c = c == 0xB8 ? 0xA8 : (unsigned char)(c - 0x20);
                *out = c;
                return 1;
            }
        if (shift)
            for (unsigned i = 0; i < sizeof(ru_shift_digits) / sizeof(ru_shift_digits[0]); i++)
                if (ru_shift_digits[i].vk == vk) {
                    *out = (unsigned char)ru_shift_digits[i].s;
                    return 1;
                }
        if (vk == 0xBF) {
            *out = '.';
            return 1;
        }
    }
    if (vk >= 'A' && vk <= 'Z') {
        *out = (unsigned char)((shift ^ caps) ? vk : vk + 32);
        return 1;
    }
    for (unsigned i = 0; i < sizeof(us_keys) / sizeof(us_keys[0]); i++)
        if (us_keys[i].vk == vk) {
            *out = (unsigned char)(shift ? us_keys[i].s : us_keys[i].n);
            return 1;
        }
    return 0;
}
