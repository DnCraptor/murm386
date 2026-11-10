/*
 * fontfile.c - Windows' raster fonts: .FON files (an NE file's RT_FONT
 * resources) and .FNT files, versions 2 and 3. The ones WIN.INI's [fonts]
 * names (in the Windows directory, as Windows has them), and those
 * AddFontResource adds; each face's sizes known from the start, its
 * glyphs read when it is first drawn.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>

#include "wincore.h"
#include "fontfile.h"

#define MAX_FACES 64

static wface *faces[MAX_FACES];
static int nfaces, scanned;

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }

/* the header of a font resource (FNT): a face of it, or NULL (a vector
 * font, not one) */
static wface *face_of(const uint8_t *d, uint32_t n, const char *path, int nth)
{
    if (n < 118)
        return NULL;
    unsigned ver = u16(d), type = u16(d + 66);
    if ((ver != 0x200 && ver != 0x300) || (type & 1) || (ver == 0x300 && n < 148))
        return NULL;
    unsigned height = u16(d + 88), first = d[95], last = d[96];
    if (!height || height > 255 || last < first)
        return NULL;
    uint32_t table = ver == 0x300 ? 148 : 118, entry = ver == 0x300 ? 6 : 4;
    if (table + entry * (last - first + 2) > n)
        return NULL;
    wface *f = (wface *)win_alloc(sizeof(*f));
    if (!f)
        return NULL;
    lstrcpyn(f->file, path, sizeof(f->file));
    f->nth = nth;
    uint32_t face = u32(d + 105);
    if (face && face < n) {
        uint32_t k = 0;
        while (face + k < n && d[face + k] && k + 1 < sizeof(f->name)) {
            f->name[k] = (char)d[face + k];
            k++;
        }
        f->name[k] = 0;
    }
    f->v3 = ver == 0x300;
    f->points = (int)u16(d + 68);
    f->height = (int)height;
    f->ascent = (int)u16(d + 74);
    f->ileading = (int)u16(d + 76);
    f->eleading = (int)u16(d + 78);
    f->italic = d[80];
    f->weight = (int)u16(d + 83);
    f->charset = d[85];
    f->pixwidth = (int)u16(d + 86);
    f->pitch_family = d[90];
    f->avgw = (int)u16(d + 91);
    f->maxw = (int)u16(d + 93);
    f->first = (int)first;
    f->last = (int)last;
    f->defchar = (int)(first + d[97]);
    f->breakchar = (int)(first + d[98]);
    return f;
}

/* an NE file's RT_FONT resources: the nth's place in it */
static int ne_font(const uint8_t *d, uint32_t n, int nth, uint32_t *at, uint32_t *len)
{
    if (n < 0x40 || u16(d) != 0x5a4d)
        return 0;
    uint32_t ne = u32(d + 0x3c);
    if (ne + 0x40 > n || u16(d + ne) != 0x454e)
        return 0;
    uint32_t rt = ne + u16(d + ne + 0x24);
    if (rt + 2 > n)
        return 0;
    unsigned shift = u16(d + rt);
    uint32_t p = rt + 2;
    while (p + 8 <= n && u16(d + p)) {
        unsigned type = u16(d + p), count = u16(d + p + 2);
        p += 8;
        for (unsigned i = 0; i < count && p + 12 <= n; i++, p += 12)
            if (type == 0x8008 && nth-- == 0) {
                *at = (uint32_t)u16(d + p) << shift;
                *len = (uint32_t)u16(d + p + 2) << shift;
                if (*at >= n)
                    return 0;
                if (*len > n - *at)     /* the last one's alignment: past the end */
                    *len = n - *at;
                return 1;
            }
    }
    return 0;
}

/* a face of the same name, size, weight and character set there already
 * (the same file under another name, another copy of it) */
static int known(const wface *f)
{
    for (int i = 0; i < nfaces; i++) {
        const wface *k = faces[i];
        if (!lstrcmpi(k->name, f->name) && k->height == f->height && k->weight == f->weight &&
            k->italic == f->italic && k->charset == f->charset)
            return 1;
    }
    return 0;
}

/* the font file's faces into the list: their number */
static int add_file(const char *path)
{
    uint32_t n = 0;
    uint8_t *d = win_file_load(path, &n, 0x100000u);
    int added = 0;
    if (!d)
        return 0;
    if (n >= 2 && (u16(d) == 0x200 || u16(d) == 0x300)) {      /* a .FNT */
        wface *f = face_of(d, n, path, -1);
        if (f)
            added++;
        if (f && nfaces < MAX_FACES && !known(f))
            faces[nfaces++] = f;
        else
            win_free(f);
    } else {
        uint32_t at, len;
        for (int i = 0; nfaces < MAX_FACES && ne_font(d, n, i, &at, &len); i++) {
            wface *f = face_of(d + at, len, path, i);
            if (f)
                added++;
            if (f && !known(f))
                faces[nfaces++] = f;
            else
                win_free(f);
        }
    }
    win_free(d);
    return added;
}

/* WIN.INI's [fonts]: "MS Sans Serif 8,10,12,14,18,24 (VGA res)=SSERIFE.FON" */
static void scan(void)
{
    if (scanned)
        return;
    scanned = 1;
    char keys[1024];
    int n = GetProfileString("fonts", NULL, "", keys, sizeof(keys));
    for (int k = 0; k < n && keys[k]; k += lstrlen(keys + k) + 1) {
        char file[80], path[128];
        if (GetProfileString("fonts", keys + k, "", file, sizeof(file)) <= 0)
            continue;
        win_find_path(file, path, sizeof(path));      /* Windows has them in SYSTEM */
        add_file(path);
    }
}

int font_faces(wface *const **list)
{
    scan();
    *list = faces;
    return nfaces;
}

/* a font file's name: as given if there is one, else the Windows
 * directory's or the system one's */
static void font_path(const char *name, char *path, int size)
{
    uint32_t n = 0;
    uint8_t *d = win_file_load(name, &n, 4);
    if (d) {
        win_free(d);
        lstrcpyn(path, name, size);
    } else {
        win_find_path(name, path, size);
    }
}

int WINAPI AddFontResource(LPCSTR file)
{
    char path[128];
    if (!file || !*file)
        return 0;
    scan();
    font_path(file, path, sizeof(path));
    for (int i = 0; i < nfaces; i++)    /* added already: its count */
        if (!lstrcmpi(faces[i]->file, path)) {
            int c = 0;
            for (int j = 0; j < nfaces; j++)
                c += !lstrcmpi(faces[j]->file, path);
            return c;
        }
    return add_file(path);
}

BOOL WINAPI RemoveFontResource(LPCSTR file)
{
    char path[128];
    if (!file || !*file)
        return FALSE;
    scan();
    font_path(file, path, sizeof(path));
    int k = 0, gone = 0;
    for (int i = 0; i < nfaces; i++) {
        wface *f = faces[i];
        if (!lstrcmpi(f->file, path) && !f->used) {
            win_free(f->data);
            win_free(f);
            gone++;
        } else {
            faces[k++] = f;
        }
    }
    nfaces = k;
    return gone > 0;
}

const uint8_t *font_data(wface *f)
{
    if (f->data || f->bad)
        return f->data;
    uint32_t n = 0, at = 0, len;
    uint8_t *d = win_file_load(f->file, &n, 0x100000u);
    if (!d) {
        f->bad = 1;
        return NULL;
    }
    len = n;
    if (f->nth >= 0 && !ne_font(d, n, f->nth, &at, &len))
        len = 0;
    f->data = len ? (uint8_t *)win_alloc(len) : NULL;
    if (f->data) {
        memcpy(f->data, d + at, len);
        f->size = len;
    } else {
        f->bad = 1;
    }
    win_free(d);
    return f->data;
}

int font_glyph(wface *f, unsigned ch, const uint8_t **cols, int *width)
{
    const uint8_t *d = font_data(f);
    if (!d)
        return 0;
    if ((int)ch < f->first || (int)ch > f->last)
        ch = (unsigned)f->defchar;
    if ((int)ch < f->first || (int)ch > f->last)
        return 0;
    uint32_t e = f->v3 ? 148 + 6 * (ch - (unsigned)f->first) : 118 + 4 * (ch - (unsigned)f->first);
    unsigned w = u16(d + e);
    uint32_t off = f->v3 ? u32(d + e + 2) : u16(d + e + 2);
    uint32_t bytes = (w + 7) / 8 * (uint32_t)f->height;
    if (off > f->size || bytes > f->size - off)
        return 0;
    *cols = d + off;
    *width = (int)w;
    return 1;
}
