/*
 * ne_res.c - a Win16 program's icons, cursors and bitmaps (its resources)
 * as WINN's own: for the Program Manager and the Task List (its icons and
 * its description, as an EZ application's winapp_info), and for the
 * program itself (LoadIcon, LoadCursor, LoadBitmap of its instance).
 *
 * An icon becomes WINN's 32x32 16-colour one: the group's entry closest to
 * that, scaled if it is another size, its colours the nearest of the 16.
 * A cursor: 32x32 masks and its hot spot.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "ne.h"

enum { RT_CURSOR_ = 1, RT_BITMAP_ = 2, RT_ICON_ = 3, RT_GROUP_CURSOR_ = 12,
       RT_GROUP_ICON_ = 14 };

static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static const COLORREF vga16[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255),
};

static int nearest16(COLORREF c)
{
    long best = 0x7fffffff;
    int bi = 0;
    for (int i = 0; i < 16; i++) {
        long dr = GetRValue(c) - GetRValue(vga16[i]), dg = GetGValue(c) - GetGValue(vga16[i]),
             db = GetBValue(c) - GetBValue(vga16[i]);
        long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < best) {
            best = d;
            bi = i;
        }
    }
    return bi;
}

static int bright(COLORREF c)
{
    return GetRValue(c) + GetGValue(c) + GetBValue(c) >= 384;
}

/* An icon's DIB (its XOR image, then its AND mask) as WINN's icon */
static int icon_from_dib(const uint8_t *d, uint32_t n, winapp_icon *out)
{
    gdi_dib b;
    if (!gdi_dib_parse(d, n, 2, &b))
        return 0;
    memset(out, 0, sizeof(*out));
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int sx = x * b.w / 32, sy = y * b.h / 32;
            int clear = (int)gdi_dib_value(&b, 1, sx, sy);
            int c = clear ? 0 : nearest16(gdi_dib_pixel(&b, sx, sy));
            out->xor_bits[y * 16 + x / 2] |= (uint8_t)(c << (x & 1 ? 0 : 4));
            if (clear)
                out->and_bits[y * 4 + x / 8] |= (uint8_t)(0x80 >> (x & 7));
        }
    return 1;
}

/* A group's entry closest to 32x32 with 16 colours: its resource id.
 * Icons: width, height, colours (bytes), planes, bits a pixel (words),
 * size (dword), id; 14 bytes each. Cursors: width, height (words), planes,
 * bits, size, id. */
static int group_pick(const uint8_t *g, uint32_t n, int cursor, uint16_t *id)
{
    if (n < 6)
        return 0;
    unsigned count = r16(g + 4);
    long best = 0x7fffffff;
    for (unsigned i = 0; i < count && 6u + 14u * (i + 1) <= n; i++) {
        const uint8_t *e = g + 6 + 14 * i;
        int w = cursor ? r16(e) : (e[0] ? e[0] : 256);
        int h = cursor ? r16(e + 2) / 2 : (e[1] ? e[1] : 256);
        int bits = r16(e + 6) * (cursor ? 1 : r16(e + 4) ? r16(e + 4) : 1);
        if (!cursor && !bits)
            bits = e[2] == 2 ? 1 : e[2] == 16 || !e[2] ? 4 : 8;
        long score = (long)abs(w - 32) * 100 + (long)abs(h - 32) * 100 +
                     (bits == 4 ? 0 : bits == 8 ? 1 : bits == 1 ? 2 : 3);
        if (score < best) {
            best = score;
            *id = r16(e + 12);
        }
    }
    return best != 0x7fffffff;
}

/* the DIB of a group's best entry (a cursor's after its hot spot), by the
 * group's id or name, or the nth group */
static int group_dib(const char *path, int cursor, uint16_t id, const char *name, int nth,
                     uint8_t **dib, uint32_t *bytes)
{
    uint8_t *g;
    uint32_t n;
    uint16_t type = cursor ? RT_GROUP_CURSOR_ : RT_GROUP_ICON_, entry = 0;
    int rc = nth >= 0 ? win_ne_resource_nth(path, type, nth, &g, &n)
                      : win_ne_resource(path, type, id, name, &g, &n);
    if (rc != WIN_NE_OK)
        return 0;
    int ok = group_pick(g, n, cursor, &entry);
    free(g);
    return ok && win_ne_resource(path, cursor ? RT_CURSOR_ : RT_ICON_, entry, NULL, dib,
                                 bytes) == WIN_NE_OK;
}

winapp_info *win_ne_app_info(const char *path)
{
    char desc[64];
    int has_desc = win_ne_description(path, desc, sizeof(desc));
    winapp_icon icons[WINAPP_MAX_ICONS];
    int count = 0;
    for (int i = 0; i < WINAPP_MAX_ICONS; i++) {
        uint8_t *d;
        uint32_t n;
        if (!group_dib(path, 0, 0, NULL, i, &d, &n))
            break;
        count += icon_from_dib(d, n, &icons[count]);
        free(d);
    }
    if (!has_desc && !count)
        return NULL;
    winapp_info *inf = (winapp_info *)win_alloc(WINAPP_INFO_SIZE(count));
    if (!inf)
        return NULL;
    inf->magic = WINAPP_INFO_MAGIC;
    inf->size = (uint16_t)WINAPP_INFO_SIZE(count);
    inf->icon_count = (uint16_t)count;
    lstrcpyn(inf->description, has_desc ? desc : "", sizeof(inf->description));
    if (count)
        memcpy(inf->icon, icons, sizeof(winapp_icon) * (size_t)count);
    return inf;
}

HICON win_ne_icon(const char *path, uint16_t id, const char *name)
{
    uint8_t *d;
    uint32_t n;
    winapp_icon ic;
    if (!group_dib(path, 0, id, name, -1, &d, &n))
        return 0;
    int ok = icon_from_dib(d, n, &ic);
    free(d);
    return ok ? user_icon_create(&ic) : 0;
}

/* a cursor's DIB (its XOR image, then its AND mask) as WINN's cursor */
static HCURSOR cursor_from_dib(const uint8_t *d, uint32_t n, int hx, int hy)
{
    gdi_dib b;
    HCURSOR h = 0;
    if (gdi_dib_parse(d, n, 2, &b)) {
        uint8_t and_m[128], xor_m[128];
        memset(and_m, 0xff, sizeof(and_m));     /* beyond its size: the screen */
        memset(xor_m, 0, sizeof(xor_m));
        for (int y = 0; y < 32 && y < b.h; y++)
            for (int x = 0; x < 32 && x < b.w; x++) {
                uint8_t m = (uint8_t)(0x80 >> (x & 7));
                if (!gdi_dib_value(&b, 1, x, y))
                    and_m[y * 4 + x / 8] &= (uint8_t)~m;
                if (bright(gdi_dib_pixel(&b, x, y)))
                    xor_m[y * 4 + x / 8] |= m;
            }
        h = user_cursor_create(and_m, xor_m, hx < 32 ? hx : 31, hy < 32 ? hy : 31);
    }
    return h;
}

HCURSOR win_ne_cursor(const char *path, uint16_t id, const char *name)
{
    uint8_t *d;
    uint32_t n;
    if (!group_dib(path, 1, id, name, -1, &d, &n))
        return 0;
    HCURSOR h = n > 4 ? cursor_from_dib(d + 4, n - 4, r16(d), r16(d + 2)) : 0;
    free(d);
    return h;
}

/* an .ICO (cursor 0) or .CUR file's image the nearest to 32 x 32 of 16
 * colours: an icon, a cursor; 0 if the file is none. Its entries: width,
 * height, colours, a byte, planes (a cursor's hot spot x), bits (y), the
 * image's size and place; 16 bytes each. */
HANDLE win_image_file(const char *path, int cursor)
{
    uint32_t n = 0;
    uint8_t *d = path ? win_file_load(path, &n, 1u << 18) : NULL;
    HANDLE h = 0;
    long best = 0x7fffffff;
    const uint8_t *pick = NULL;
    for (unsigned i = 0; d && n >= 6 && !r16(d) && r16(d + 2) == (cursor ? 2 : 1) &&
                         i < r16(d + 4) && 6u + 16u * (i + 1) <= n; i++) {
        const uint8_t *e = d + 6 + 16 * i;
        int w = e[0] ? e[0] : 256, ht = e[1] ? e[1] : 256;
        int bits = cursor ? 1 : r16(e + 6) ? r16(e + 6) : e[2] == 2 ? 1 : e[2] == 16 ? 4 : 8;
        long score = (long)abs(w - 32) * 100 + (long)abs(ht - 32) * 100 +
                     (bits == 4 ? 0 : bits == 8 ? 1 : bits == 1 ? 2 : 3);
        uint32_t size = r16(e + 8) | (uint32_t)r16(e + 10) << 16;
        uint32_t at = r16(e + 12) | (uint32_t)r16(e + 14) << 16;
        if (at < n && size <= n - at && score < best) {
            best = score;
            pick = e;
        }
    }
    if (pick) {
        uint32_t size = r16(pick + 8) | (uint32_t)r16(pick + 10) << 16;
        uint32_t at = r16(pick + 12) | (uint32_t)r16(pick + 14) << 16;
        winapp_icon ic;
        if (cursor)
            h = cursor_from_dib(d + at, size, r16(pick + 4), r16(pick + 6));
        else if (icon_from_dib(d + at, size, &ic))
            h = user_icon_create(&ic);
    }
    win_free(d);
    return h;
}

HBITMAP win_ne_bitmap(const char *path, uint16_t id, const char *name)
{
    uint8_t *d;
    uint32_t n;
    if (win_ne_resource(path, RT_BITMAP_, id, name, &d, &n) != WIN_NE_OK)
        return 0;
    HBITMAP h = gdi_bitmap_from_dib(d, n);
    free(d);
    return h;
}

/* A resource of a Win16 file (VER's GetFileResource): its type and id
 * numbers (MAKEINTRESOURCE, or "#n"), the id a name too, or NULL for the
 * first of the type; from offset on, up to cap bytes into buf (NULL: none).
 * Its size, 0 when the file has none. */
DWORD WINAPI GetFileResourceN(LPCSTR path, LPCSTR type, LPCSTR id, DWORD offset, void *buf,
                              DWORD cap)
{
    uint16_t t = 0, i = 0;
    const char *name = NULL;
    if (!path || !type)
        return 0;
    if ((uintptr_t)type < 0x10000u) {
        t = (uint16_t)(uintptr_t)type;
    } else if (type[0] == '#') {
        for (const char *p = type + 1; *p >= '0' && *p <= '9'; p++)
            t = (uint16_t)(t * 10 + (*p - '0'));
    } else {
        return 0;                       /* named types: none of Windows' own */
    }
    if (id && (uintptr_t)id < 0x10000u)
        i = (uint16_t)(uintptr_t)id;
    else if (id)
        name = id;
    uint8_t *d = NULL;
    uint32_t n = 0;
    int rc = id ? win_ne_resource(path, t, i, name, &d, &n)
                : win_ne_resource_nth(path, t, 0, &d, &n);
    if (rc != WIN_NE_OK)
        return 0;
    if (buf && offset < n)
        memcpy(buf, d + offset, n - offset < cap ? n - offset : cap);
    free(d);
    return n;
}

/* the same for a Win32 program's resources (pe_user.c): an icon's DIB, a
 * cursor's (after its hot spot), a group's best entry */
HICON win_icon_from_dib(const uint8_t *d, uint32_t n)
{
    winapp_icon ic;
    return icon_from_dib(d, n, &ic) ? user_icon_create(&ic) : 0;
}

HCURSOR win_cursor_from_dib(const uint8_t *d, uint32_t n, int hx, int hy)
{
    return cursor_from_dib(d, n, hx, hy);
}

int win_group_pick(const uint8_t *g, uint32_t n, int cursor, uint16_t *id)
{
    return group_pick(g, n, cursor, id);
}
