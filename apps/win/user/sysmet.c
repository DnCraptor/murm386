/*
 * sysmet.c - system colours, metrics, DrawText.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

/* Windows 3.1 default scheme. */
static COLORREF sys_colors[COLOR_MAX] = {
    RGB(192, 192, 192),   /* SCROLLBAR */
    RGB(0, 128, 128),     /* BACKGROUND */
    RGB(0, 0, 128),       /* ACTIVECAPTION */
    RGB(255, 255, 255),   /* INACTIVECAPTION */
    RGB(255, 255, 255),   /* MENU */
    RGB(255, 255, 255),   /* WINDOW */
    RGB(0, 0, 0),         /* WINDOWFRAME */
    RGB(0, 0, 0),         /* MENUTEXT */
    RGB(0, 0, 0),         /* WINDOWTEXT */
    RGB(255, 255, 255),   /* CAPTIONTEXT */
    RGB(192, 192, 192),   /* ACTIVEBORDER */
    RGB(192, 192, 192),   /* INACTIVEBORDER */
    RGB(255, 255, 255),   /* APPWORKSPACE */
    RGB(0, 0, 128),       /* HIGHLIGHT */
    RGB(255, 255, 255),   /* HIGHLIGHTTEXT */
    RGB(192, 192, 192),   /* BTNFACE */
    RGB(128, 128, 128),   /* BTNSHADOW */
    RGB(128, 128, 128),   /* GRAYTEXT */
    RGB(0, 0, 0),         /* BTNTEXT */
    RGB(0, 0, 0),         /* INACTIVECAPTIONTEXT */
    RGB(255, 255, 255),   /* BTNHIGHLIGHT */
};

static HBRUSH sys_brushes[COLOR_MAX];

void user_init_sysmet(void)
{
    gdi_sys_color_brush = user_sys_brush;
    for (int i = 0; i < COLOR_MAX; i++) {
        sys_brushes[i] = CreateSolidBrush(sys_colors[i]);
        win_handle_set_stock(sys_brushes[i]);
    }
}

DWORD WINAPI GetSysColor(int i) { return (i >= 0 && i < COLOR_MAX) ? sys_colors[i] : 0; }

HBRUSH user_sys_brush(int i) { return (i >= 0 && i < COLOR_MAX) ? sys_brushes[i] : 0; }
HBRUSH WINAPI GetSysColorBrush(int i) { return user_sys_brush(i); }

/* WIN.INI [colors] key names, in COLOR_xxx order (as in Windows 3.1) */
static const char *const color_keys[COLOR_MAX] = {
    "Scrollbar", "Background", "ActiveTitle", "InactiveTitle", "Menu", "Window",
    "WindowFrame", "MenuText", "WindowText", "TitleText", "ActiveBorder",
    "InactiveBorder", "AppWorkspace", "Hilight", "HilightText", "ButtonFace",
    "ButtonShadow", "GrayText", "ButtonText", "InactiveTitleText", "ButtonHilight",
};

static void mark_nc(wwnd *w)
{
    for (; w; w = w->next) {
        w->ncpaint = 1;
        mark_nc(w->child);
    }
}

/* "r g b" -> colour; 0 if it is not three numbers */
static int parse_rgb(const char *s, COLORREF *c)
{
    int v[3], k = 0;
    while (*s && k < 3) {
        while (*s == ' ' || *s == ',')
            s++;
        if (*s < '0' || *s > '9')
            return 0;
        int n = 0;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + (*s++ - '0');
        v[k++] = n > 255 ? 255 : n;
    }
    if (k < 3)
        return 0;
    *c = RGB(v[0], v[1], v[2]);
    return 1;
}

/* the colours of WIN.INI [colors], at start-up */
void user_load_colors(void)
{
    int idx[COLOR_MAX], n = 0;
    COLORREF col[COLOR_MAX];
    for (int i = 0; i < COLOR_MAX; i++) {
        char v[32];
        if (GetProfileString("colors", color_keys[i], "", v, sizeof(v)) && parse_rgb(v, &col[n]))
            idx[n++] = i;
    }
    for (int k = 0; k < n; k++) {
        sys_colors[idx[k]] = col[k];
        wbrush *b = (wbrush *)win_handle_get(sys_brushes[idx[k]], WOBJ_BRUSH);
        if (b)
            b->color = col[k];
    }
}

void WINAPI SetSysColors(int n, const int *idx, const COLORREF *colors)
{
    for (int k = 0; k < n; k++) {
        int i = idx[k];
        if (i < 0 || i >= COLOR_MAX)
            continue;
        sys_colors[i] = colors[k];
        wbrush *b = (wbrush *)win_handle_get(sys_brushes[i], WOBJ_BRUSH);
        if (b)
            b->color = colors[k];
    }
    SendMessage(0xFFFF, WM_SYSCOLORCHANGE, 0, 0);
    mark_nc(U.desktop->child);
    user_repaint_all();
}

/* every window, the desktop and the frames */
void user_repaint_all(void)
{
    RECT all = { 0, 0, win_sys.screen.width, win_sys.screen.height };
    wrgn g;
    wrgn_init(&g);
    wrgn_set_rect(&g, &all);
    user_invalidate_screen(&g);
    wrgn_free(&g);
    mark_nc(U.desktop->child);
}

/* ---------------------------------------------------------------- desktop pattern */

static HBRUSH desk_pattern;         /* 0: plain desktop colour */

HBRUSH user_desktop_pattern(void) { return desk_pattern; }

/* "170 85 170 85 170 85 170 85" (the rows, top first) or "(None)" */
static void set_pattern(const char *s)
{
    if (desk_pattern) {
        DeleteObject(desk_pattern);
        desk_pattern = 0;
    }
    BYTE rows[16];                  /* 8 rows of one byte, each padded to a word */
    int k = 0, any = 0;
    while (s && *s && k < 8) {
        while (*s == ' ' || *s == ',')
            s++;
        if (*s < '0' || *s > '9')
            break;
        int n = 0;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + (*s++ - '0');
        rows[2 * k] = (BYTE)n;
        rows[2 * k + 1] = 0;
        k++;
        any |= n & 0xFF;
    }
    if (k < 8 || !any)
        return;
    HBITMAP bm = CreateBitmap(8, 8, 1, 1, rows);
    if (bm) {
        desk_pattern = CreatePatternBrush(bm);
        DeleteObject(bm);
    }
}

/* ---------------------------------------------------------------- wallpaper */

/* the desktop's bitmap (WIN.INI [Desktop] Wallpaper, a .BMP), centred or
 * tiled (TileWallpaper=1) over the pattern; 0: none */
static HBITMAP wallpaper;
static int wall_w, wall_h, wall_tile;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* a .BMP file (Windows', BITMAPINFOHEADER) as a bitmap of the screen's */
HBITMAP user_load_bmp(const char *name, int *w, int *h)
{
    char path[160];
    uint32_t n = 0;
    win_find_path(name, path, sizeof(path));
    uint8_t *d = win_file_load(path, &n, 1u << 20);
    HBITMAP bm = 0;
    if (d && n >= 54 && d[0] == 'B' && d[1] == 'M' && le32(d + 14) == 40) {
        uint32_t off = le32(d + 10), bits = (uint32_t)(d[28] | d[29] << 8);
        uint32_t used = le32(d + 46), colours = bits <= 8 ? (used ? used : 1u << bits) : 0;
        uint32_t info = 40 + colours * 4;
        /* the header and the colours where they can be read as a structure */
        BITMAPINFO *bi = (BITMAPINFO *)win_alloc(info);
        if (bi && 14 + info <= n && off < n) {
            memcpy(bi, d + 14, info);
            HDC dc = GetDC(0);
            bm = CreateDIBitmap(dc, &bi->bmiHeader, CBM_INIT, d + off, bi, DIB_RGB_COLORS);
            ReleaseDC(0, dc);
            *w = (int)bi->bmiHeader.biWidth;
            *h = (int)bi->bmiHeader.biHeight;
            if (*h < 0)
                *h = -*h;
        }
        win_free(bi);
    }
    win_free(d);
    return bm;
}

static void set_wallpaper(const char *name)
{
    if (wallpaper) {
        DeleteObject(wallpaper);
        wallpaper = 0;
    }
    if (name && name[0] && lstrcmpi(name, "(None)"))
        wallpaper = user_load_bmp(name, &wall_w, &wall_h);
}

void user_draw_wallpaper(HDC dc, const RECT *rc)
{
    if (!wallpaper || wall_w <= 0 || wall_h <= 0)
        return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, wallpaper);
    if (wall_tile) {
        for (int y = rc->top; y < rc->bottom; y += wall_h)
            for (int x = rc->left; x < rc->right; x += wall_w)
                BitBlt(dc, x, y, wall_w, wall_h, mem, 0, 0, SRCCOPY);
    } else {
        BitBlt(dc, (rc->left + rc->right - wall_w) / 2, (rc->top + rc->bottom - wall_h) / 2,
               wall_w, wall_h, mem, 0, 0, SRCCOPY);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
}

/* SetDeskWallPaper: a file, or (LPCSTR)-1 the one in WIN.INI again */
BOOL WINAPI SetDeskWallPaper(LPCSTR file)
{
    char v[128];
    if ((intptr_t)file == -1 || !file) {
        GetProfileString("Desktop", "Wallpaper", "(None)", v, sizeof(v));
        file = v;
    }
    wall_tile = GetProfileInt("Desktop", "TileWallpaper", 0) != 0;
    set_wallpaper(file);
    user_repaint_all();
    return !file[0] || !lstrcmpi(file, "(None)") || wallpaper != 0;
}

/* ---------------------------------------------------------------- SystemParametersInfo */

/* what SystemParametersInfo sets and tells, as WIN.INI has them */
static struct {
    int beep, mouse[3], border, kb_speed, kb_delay, icon_dx, icon_dy, saver_time, saver_on;
    int grid, title_wrap, drop_right, dbl_w, dbl_h, fast_switch;
} spi;

static const struct {
    int *v;
    const char *section, *key;
    int def;
} spi_keys[] = {
    { &spi.mouse[0], "windows", "MouseThreshold1", 6 },
    { &spi.mouse[1], "windows", "MouseThreshold2", 10 },
    { &spi.mouse[2], "windows", "MouseSpeed", 1 },
    { &spi.border, "windows", "BorderWidth", 3 },
    { &spi.kb_speed, "windows", "KeyboardSpeed", 31 },
    { &spi.kb_delay, "windows", "KeyboardDelay", 2 },
    { &spi.icon_dx, "Desktop", "IconSpacing", 77 },
    { &spi.icon_dy, "Desktop", "IconVerticalSpacing", 72 },
    { &spi.saver_time, "windows", "ScreenSaveTimeOut", 120 },
    { &spi.saver_on, "windows", "ScreenSaveActive", 0 },
    { &spi.grid, "Desktop", "GridGranularity", 0 },
    { &spi.title_wrap, "Desktop", "IconTitleWrap", 1 },
    { &spi.drop_right, "windows", "MenuDropAlignment", 0 },
    { &spi.dbl_w, "windows", "DoubleClickWidth", 4 },
    { &spi.dbl_h, "windows", "DoubleClickHeight", 4 },
    { &spi.fast_switch, "windows", "CoolSwitch", 1 },
};

void user_load_desktop(void)
{
    SetDoubleClickTime(GetProfileInt("windows", "DoubleClickSpeed", 500));
    SwapMouseButton(GetProfileInt("windows", "SwapMouseButtons", 0) != 0);
    char v[128];
    GetProfileString("windows", "Beep", "yes", v, sizeof(v));
    spi.beep = !lstrcmpi(v, "yes") || !lstrcmp(v, "1");
    for (unsigned i = 0; i < sizeof(spi_keys) / sizeof(spi_keys[0]); i++)
        *spi_keys[i].v = (int)GetProfileInt(spi_keys[i].section, spi_keys[i].key,
                                            spi_keys[i].def);
    GetProfileString("Desktop", "Pattern", "(None)", v, sizeof(v));
    set_pattern(v);
    wall_tile = GetProfileInt("Desktop", "TileWallpaper", 0) != 0;
    GetProfileString("Desktop", "Wallpaper", "(None)", v, sizeof(v));
    set_wallpaper(v);
}

/* one of spi_keys[] set (WIN.INI too, when asked) */
static BOOL spi_set(int *var, int value, UINT ini)
{
    *var = value;
    for (unsigned i = 0; (ini & SPIF_UPDATEINIFILE) && i < sizeof(spi_keys) / sizeof(spi_keys[0]); i++)
        if (spi_keys[i].v == var) {
            char v[12];
            wsprintf(v, "%d", value);
            WriteProfileString(spi_keys[i].section, spi_keys[i].key, v);
        }
    return TRUE;
}

static BOOL spi_get(LPVOID pv, int value)
{
    if (!pv)
        return FALSE;
    *(int *)pv = value;
    return TRUE;
}

BOOL WINAPI SystemParametersInfo(UINT action, UINT param, LPVOID pv, UINT ini)
{
    BOOL ok = TRUE;
    switch (action) {
    case SPI_GETBEEP:
        return spi_get(pv, spi.beep);
    case SPI_SETBEEP:
        spi.beep = param != 0;
        if (ini & SPIF_UPDATEINIFILE)
            WriteProfileString("windows", "Beep", param ? "yes" : "no");
        break;
    case SPI_GETMOUSE:
        if (!pv)
            return FALSE;
        memcpy(pv, spi.mouse, sizeof(spi.mouse));
        return TRUE;
    case SPI_SETMOUSE:
        if (!pv)
            return FALSE;
        for (int i = 0; i < 3; i++)
            spi_set(&spi.mouse[i], ((const int *)pv)[i], ini);
        break;
    case SPI_GETBORDER: return spi_get(pv, spi.border);
    case SPI_SETBORDER: ok = spi_set(&spi.border, (int)param, ini); break;
    case SPI_GETKEYBOARDSPEED: return spi_get(pv, spi.kb_speed);
    case SPI_SETKEYBOARDSPEED: ok = spi_set(&spi.kb_speed, (int)(param & 31), ini); break;
    case SPI_GETKEYBOARDDELAY: return spi_get(pv, spi.kb_delay);
    case SPI_SETKEYBOARDDELAY: ok = spi_set(&spi.kb_delay, (int)(param & 3), ini); break;
    case SPI_LANGDRIVER:
        return FALSE;               /* no language driver */
    case SPI_ICONHORIZONTALSPACING:  /* pv: the spacing; else param sets it */
        if (pv)
            return spi_get(pv, spi.icon_dx);
        ok = spi_set(&spi.icon_dx, (int)param < 32 ? 32 : (int)param, ini);
        break;
    case SPI_ICONVERTICALSPACING:
        if (pv)
            return spi_get(pv, spi.icon_dy);
        ok = spi_set(&spi.icon_dy, (int)param < 32 ? 32 : (int)param, ini);
        break;
    case SPI_GETSCREENSAVETIMEOUT: return spi_get(pv, spi.saver_time);
    case SPI_SETSCREENSAVETIMEOUT: ok = spi_set(&spi.saver_time, (int)param, ini); break;
    case SPI_GETSCREENSAVEACTIVE: return spi_get(pv, spi.saver_on);
    case SPI_SETSCREENSAVEACTIVE: ok = spi_set(&spi.saver_on, param != 0, ini); break;
    case SPI_GETGRIDGRANULARITY: return spi_get(pv, spi.grid);
    case SPI_SETGRIDGRANULARITY: ok = spi_set(&spi.grid, (int)param, ini); break;
    case SPI_GETICONTITLEWRAP: return spi_get(pv, spi.title_wrap);
    case SPI_SETICONTITLEWRAP: ok = spi_set(&spi.title_wrap, param != 0, ini); break;
    case SPI_GETMENUDROPALIGNMENT: return spi_get(pv, spi.drop_right);
    case SPI_SETMENUDROPALIGNMENT: ok = spi_set(&spi.drop_right, param != 0, ini); break;
    case SPI_SETDOUBLECLKWIDTH: ok = spi_set(&spi.dbl_w, (int)param, ini); break;
    case SPI_SETDOUBLECLKHEIGHT: ok = spi_set(&spi.dbl_h, (int)param, ini); break;
    case SPI_GETFASTTASKSWITCH: return spi_get(pv, spi.fast_switch);
    case SPI_SETFASTTASKSWITCH: ok = spi_set(&spi.fast_switch, param != 0, ini); break;
    case SPI_GETICONTITLELOGFONT:
    case SPI_SETICONTITLELOGFONT: {
        /* the icon titles' font: the system font's (it is not changed) */
        if (action == SPI_SETICONTITLELOGFONT)
            return FALSE;
        if (!pv)
            return FALSE;
        LOGFONT *lf = (LOGFONT *)pv;
        memset(lf, 0, sizeof(*lf));
        lf->lfHeight = -8;
        lf->lfWeight = FW_NORMAL;
        lf->lfCharSet = ANSI_CHARSET;
        lf->lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
        lstrcpy(lf->lfFaceName, "MS Sans Serif");
        return TRUE;
    }
    case SPI_SETDESKWALLPAPER:
        /* the bitmap given, or (pv = -1, NULL) the one in WIN.INI again */
        if ((intptr_t)pv != -1 && pv && (ini & SPIF_UPDATEINIFILE))
            WriteProfileString("Desktop", "Wallpaper", (LPCSTR)pv);
        ok = SetDeskWallPaper((intptr_t)pv == -1 ? (LPCSTR)-1 : pv ? (LPCSTR)pv : "(None)");
        break;
    case SPI_SETDESKPATTERN:
        /* the pattern given, or (pv = -1) the one in WIN.INI again */
        if ((intptr_t)pv == -1 || !pv) {
            char v[64];
            GetProfileString("Desktop", "Pattern", "(None)", v, sizeof(v));
            set_pattern(v);
        } else {
            set_pattern((LPCSTR)pv);
            if (ini & SPIF_UPDATEINIFILE)
                WriteProfileString("Desktop", "Pattern", (LPCSTR)pv);
        }
        user_repaint_all();
        break;
    case SPI_SETDOUBLECLICKTIME:
        SetDoubleClickTime(param);
        if (ini & SPIF_UPDATEINIFILE) {
            char v[12];
            wsprintf(v, "%u", param);
            WriteProfileString("windows", "DoubleClickSpeed", v);
        }
        break;
    case SPI_SETMOUSEBUTTONSWAP:
        SwapMouseButton(param != 0);
        if (ini & SPIF_UPDATEINIFILE)
            WriteProfileString("windows", "SwapMouseButtons", param ? "1" : "0");
        break;
    default:
        return FALSE;
    }
    if (ok && (ini & SPIF_SENDWININICHANGE))
        SendMessage(HWND_BROADCAST, WM_WININICHANGE, 0, 0);
    return ok;
}

int WINAPI GetSystemMetrics(int i)
{
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    switch (i) {
    case SM_CXSCREEN: return sw;
    case SM_CYSCREEN: return sh;
    case SM_CXVSCROLL: case SM_CYHSCROLL: return UM_SCROLL;
    case SM_CYCAPTION: return UM_CAPTION + 1;
    case SM_CXBORDER: case SM_CYBORDER: return UM_BORDER;
    case SM_CXDLGFRAME: case SM_CYDLGFRAME: return UM_FRAME;
    case SM_CYVTHUMB: case SM_CXHTHUMB: return UM_SCROLL;
    case SM_CXICON: case SM_CYICON: return UM_ICON;
    case SM_CXCURSOR: case SM_CYCURSOR: return 32;
    case SM_CYMENU: return 18;
    case SM_CXFULLSCREEN: return sw;
    case SM_CYFULLSCREEN: return sh - UM_CAPTION - 1;
    case SM_CXMIN: return 100;
    case SM_CYMIN: return UM_CAPTION + 2 * UM_FRAME + 1;
    case SM_CXSIZE: case SM_CYSIZE: return UM_CAPTION;
    case SM_CXFRAME: case SM_CYFRAME: return UM_FRAME;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    case SM_MOUSEPRESENT: return 1;
    case SM_SWAPBUTTON: return user_buttons_swapped();
    default: return 0;
    }
}

/* the speaker's beep, whatever the type (WINN has no sounds of its own) */
void WINAPI MessageBeep(UINT type)
{
    (void)type;
    sound_beep();
}

/* ---------------------------------------------------------------- DrawText */

/* Width of s[0..n) with '&' prefixes removed unless DT_NOPREFIX. */
static int seg_width(HDC dc, LPCSTR s, int n, UINT fmt, int tabw)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&' && !(fmt & DT_NOPREFIX)) {
            if (i + 1 < n && s[i + 1] == '&')
                i++;
            else
                continue;
            c = '&';
        }
        if (c == '\t' && (fmt & DT_EXPANDTABS)) {
            w = (w / tabw + 1) * tabw;
            continue;
        }
        SIZE sz;
        GetTextExtentPoint(dc, &c, 1, &sz);
        w += sz.cx;
    }
    return w;
}

static void draw_seg(HDC dc, int x, int y, LPCSTR s, int n, UINT fmt, int tabw, int lh)
{
    int x0 = x;
    char buf[256];
    int k = 0, ul_x = -1, ul_w = 0;
    for (int i = 0; i <= n; i++) {
        int flush = i == n;
        char c = i < n ? s[i] : 0;
        if (!flush && c == '&' && !(fmt & DT_NOPREFIX)) {
            if (i + 1 < n && s[i + 1] == '&') {
                i++;
            } else {
                if (i + 1 < n) {
                    /* underline the next character */
                    SIZE pre, ch;
                    GetTextExtentPoint(dc, buf, k, &pre);
                    GetTextExtentPoint(dc, &s[i + 1], 1, &ch);
                    ul_x = x + pre.cx;
                    ul_w = ch.cx - 1;
                }
                continue;
            }
        }
        if (!flush && c == '\t' && (fmt & DT_EXPANDTABS))
            flush = 1;
        if (!flush && k < (int)sizeof(buf)) {
            buf[k++] = c;
            continue;
        }
        if (k) {
            TextOut(dc, x, y, buf, k);
            SIZE sz;
            GetTextExtentPoint(dc, buf, k, &sz);
            x += sz.cx;
            k = 0;
        }
        if (i < n && c == '\t')
            x = x0 + ((x - x0) / tabw + 1) * tabw;
        else if (i < n)
            buf[k++] = c;
    }
    if (ul_x >= 0 && ul_w > 0) {
        HPEN p = CreatePen(PS_SOLID, 1, GetTextColor(dc));
        HGDIOBJ o = SelectObject(dc, p);
        TEXTMETRIC tm;
        GetTextMetrics(dc, &tm);
        MoveTo(dc, ul_x, y + tm.tmAscent + 1);
        LineTo(dc, ul_x + ul_w, y + tm.tmAscent + 1);
        SelectObject(dc, o);
        DeleteObject(p);
    }
    (void)lh;
}

int user_draw_text(HDC dc, LPCSTR s, int n, RECT *rc, UINT fmt)
{
    if (n < 0)
        n = lstrlen(s);
    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);
    int lh = tm.tmHeight + tm.tmExternalLeading;
    int tabw = 8 * tm.tmAveCharWidth;
    int width = rc->right - rc->left;
    int y = rc->top, maxw = 0;

    if (fmt & DT_SINGLELINE) {
        int w = seg_width(dc, s, n, fmt, tabw);
        if (fmt & DT_CALCRECT) {
            rc->right = rc->left + w;
            rc->bottom = rc->top + lh;
            return lh;
        }
        if (fmt & DT_VCENTER)
            y = rc->top + (rc->bottom - rc->top - lh) / 2;
        else if (fmt & DT_BOTTOM)
            y = rc->bottom - lh;
        int x = rc->left;
        if (fmt & DT_CENTER) x = rc->left + (width - w) / 2;
        else if (fmt & DT_RIGHT) x = rc->right - w;
        int saved = 0;
        if (!(fmt & DT_NOCLIP)) {
            saved = SaveDC(dc);
            IntersectClipRect(dc, rc->left, rc->top, rc->right, rc->bottom);
        }
        draw_seg(dc, x, y, s, n, fmt, tabw, lh);
        if (saved)
            RestoreDC(dc, saved);
        return lh;
    }

    int saved = 0;
    if (!(fmt & (DT_NOCLIP | DT_CALCRECT))) {
        saved = SaveDC(dc);
        IntersectClipRect(dc, rc->left, rc->top, rc->right, rc->bottom);
    }
    int i = 0;
    while (i < n || (i == 0 && n == 0)) {
        /* one line: up to newline, or word-wrapped to the width */
        int end = i;
        while (end < n && s[end] != '\n' && s[end] != '\r')
            end++;
        int line_end = end;
        if ((fmt & DT_WORDBREAK) && seg_width(dc, s + i, end - i, fmt, tabw) > width) {
            int brk = -1;
            for (int k = i; k < end; k++) {
                if (s[k] == ' ' || s[k] == '\t') {
                    if (seg_width(dc, s + i, k - i, fmt, tabw) <= width)
                        brk = k;
                    else
                        break;
                }
            }
            if (brk < 0) {          /* no space fits: cut the word */
                brk = i + 1;
                while (brk < end && seg_width(dc, s + i, brk + 1 - i, fmt, tabw) <= width)
                    brk++;
            }
            line_end = brk;
        }
        int w = seg_width(dc, s + i, line_end - i, fmt, tabw);
        if (w > maxw)
            maxw = w;
        if (!(fmt & DT_CALCRECT)) {
            int x = rc->left;
            if (fmt & DT_CENTER) x = rc->left + (width - w) / 2;
            else if (fmt & DT_RIGHT) x = rc->right - w;
            draw_seg(dc, x, y, s + i, line_end - i, fmt, tabw, lh);
        }
        y += lh;
        i = line_end;
        if (i < n && (s[i] == ' ' || s[i] == '\t') && line_end != end)
            while (i < n && s[i] == ' ')
                i++;
        if (i < n && s[i] == '\r')
            i++;
        if (i < n && s[i] == '\n')
            i++;
        if (n == 0)
            break;
    }
    if (saved)
        RestoreDC(dc, saved);
    if (fmt & DT_CALCRECT) {
        rc->right = rc->left + maxw;
        rc->bottom = y;
    }
    return y - rc->top;
}

int WINAPI DrawText(HDC dc, LPCSTR s, int n, LPRECT rc, UINT fmt)
{
    return user_draw_text(dc, s, n, rc, fmt);
}

/* DrawText within margins (average characters' widths); the characters
 * drawn told */
int WINAPI DrawTextEx(HDC dc, LPSTR s, int n, LPRECT rc, UINT fmt, LPDRAWTEXTPARAMS p)
{
    if (!p)
        return user_draw_text(dc, s, n, rc, fmt);
    TEXTMETRIC tm;
    int avg = GetTextMetrics(dc, &tm) ? tm.tmAveCharWidth : 8;
    RECT r = *rc;
    r.left += p->iLeftMargin * avg;
    r.right -= p->iRightMargin * avg;
    int ht = user_draw_text(dc, s, n, &r, fmt);
    if (fmt & DT_CALCRECT) {            /* the margins in what it needs */
        rc->left = r.left - p->iLeftMargin * avg;
        rc->right = r.right + p->iRightMargin * avg;
        rc->top = r.top;
        rc->bottom = r.bottom;
    }
    p->uiLengthDrawn = (UINT)(n < 0 ? lstrlen(s) : n);
    return ht;
}
