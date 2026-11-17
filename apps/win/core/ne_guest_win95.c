/*
 * ne_guest_win95.c - the Win16 entry points whose arguments are
 * structures, buffers of several strings or a program's memory: Windows
 * 95's KERNEL (directories, attributes, finding files, profile sections and
 * structures, free space), USER (scroll and menu item information, DrawState,
 * tiling, MessageBoxIndirect, icons from resource bits and directories) and
 * GDI (region data, GDI's Copy, the printer drivers' data), SHELL's
 * InternalExtractIcon and ExtractIconEx, and the control classes' window
 * procedures USER exports (BUTTONWNDPROC ...).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"
#include "ne_exec.h"
#include "user.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define SH WIN_NE_SERVICE_SHELL
#define U16(n) (0x1000u + (n))
#define G16(n) (0x3000u + (n))

static int str_in(args *a, uint32_t far, char *dst, uint32_t cap)
{
    if (!(far >> 16) || !win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, cap)) {
        dst[0] = 0;
        return 0;
    }
    return 1;
}

static int put(args *a, uint32_t far, const void *src, uint32_t n)
{
    return (far >> 16) && win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, src, n);
}

static int get(args *a, uint32_t far, void *dst, uint32_t n)
{
    return (far >> 16) && win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, n);
}

static int put32(args *a, uint32_t far, uint32_t v)
{
    uint8_t b[4];
    wr32(b, v);
    return !far || put(a, far, b, 4);
}

/* ---------------------------------------------------------------- KERNEL */

/* a profile's section as "key=value" strings, NUL after each, another at
 * the end (file NULL: WIN.INI); its length without the last NUL */
static int section_get(const char *sec, char *out, int size, const char *file)
{
    static char keys[1024];
    char value[256];
    int n = file ? GetPrivateProfileString(sec, NULL, "", keys, sizeof(keys), file)
                 : GetProfileString(sec, NULL, "", keys, sizeof(keys));
    int at = 0;
    if (size < 2)
        return 0;
    for (const char *k = keys; n > 0 && k < keys + n && *k; k += lstrlen(k) + 1) {
        if (file)
            GetPrivateProfileString(sec, k, "", value, sizeof(value), file);
        else
            GetProfileString(sec, k, "", value, sizeof(value));
        int need = lstrlen(k) + 1 + lstrlen(value) + 1;
        if (at + need > size - 1)
            break;
        wsprintf(out + at, "%s=%s", k, value);
        at += need;
    }
    out[at] = 0;
    if (!at)
        out[1] = 0;
    return at ? at - 1 : 0;
}

/* the section replaced by the program's strings at far */
static int section_put(args *a, const char *sec, uint32_t far, const char *file)
{
    char line[300];
    if (file)
        WritePrivateProfileString(sec, NULL, NULL, file);
    else
        WriteProfileString(sec, NULL, NULL);
    for (int i = 0; i < 256 && far >> 16; i++) {
        if (!str_in(a, far, line, sizeof(line)) || !line[0])
            break;
        far += (uint32_t)lstrlen(line) + 1;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        if (file)
            WritePrivateProfileString(sec, line, eq + 1, file);
        else
            WriteProfileString(sec, line, eq + 1);
    }
    return 1;
}

/* Get/WritePrivateProfileStruct: hexadecimal bytes, their sum's byte after
 * them (Windows 95's) */
static int profile_struct(args *a, int write, uint32_t *v)
{
    char sec[80], key[80], file[144], hex[2 * 256 + 3];
    uint8_t data[256];
    uint32_t far = dw(a, 10);
    UINT size = w(a, 8);
    *v = 0;
    int named = dw(a, 4) != 0;
    if (!str_in(a, dw(a, 18), sec, sizeof(sec)) || !str_in(a, dw(a, 14), key, sizeof(key)) ||
        (named && !str_in(a, dw(a, 4), file, sizeof(file))) || size > sizeof(data))
        return 1;
    static const char digit[] = "0123456789ABCDEF";
    if (write) {
        uint8_t sum = 0;
        if (far && !get(a, far, data, size))
            return 1;
        for (UINT i = 0; i < size; i++) {
            hex[2 * i] = digit[data[i] >> 4];
            hex[2 * i + 1] = digit[data[i] & 15];
            sum = (uint8_t)(sum + data[i]);
        }
        hex[2 * size] = digit[sum >> 4];
        hex[2 * size + 1] = digit[sum & 15];
        hex[2 * size + 2] = 0;
        *v = (uint16_t)(named ? WritePrivateProfileString(sec, key, far ? hex : NULL, file) :
                                WriteProfileString(sec, key, far ? hex : NULL));
        return 1;
    }
    int n = named ? GetPrivateProfileString(sec, key, "", hex, sizeof(hex), file) :
                    GetProfileString(sec, key, "", hex, sizeof(hex));
    if (n != (int)(2 * size + 2))
        return 1;
    uint8_t sum = 0;
    for (UINT i = 0; i <= size; i++) {
        int hi = -1, lo = -1;
        for (int k = 0; k < 16; k++) {
            if ((hex[2 * i] | 0x20) == (digit[k] | 0x20)) hi = k;
            if ((hex[2 * i + 1] | 0x20) == (digit[k] | 0x20)) lo = k;
        }
        if (hi < 0 || lo < 0)
            return 1;
        uint8_t b = (uint8_t)(hi << 4 | lo);
        if (i == size) {
            if (b != sum)
                return 1;
        } else {
            data[i] = b;
            sum = (uint8_t)(sum + b);
        }
    }
    *v = put(a, far, data, size);
    return 1;
}

/* DOS's date and time as a FILETIME: 100 ns since 1601 */
static void file_time(uint8_t *out, WORD date, WORD time_)
{
    static const uint16_t before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int y = 1980 + (date >> 9), m = (date >> 5) & 15, d = date & 31;
    if (m < 1 || m > 12)
        m = 1;
    if (d < 1)
        d = 1;
    uint32_t days = 0;
    for (int k = 1601; k < y; k++)
        days += (k % 4 == 0 && (k % 100 || k % 400 == 0)) ? 366u : 365u;
    days += before[m - 1] + (uint32_t)(d - 1);
    if (m > 2 && y % 4 == 0 && (y % 100 || y % 400 == 0))
        days++;
    uint64_t s = (uint64_t)days * 86400u + (uint64_t)(time_ >> 11) * 3600u +
                 ((time_ >> 5) & 63u) * 60u + (time_ & 31u) * 2u;
    uint64_t t = s * 10000000u;
    wr32(out, (uint32_t)t);
    wr32(out + 4, (uint32_t)(t >> 32));
}

/* FindFirstFile's searches: the names found, the next one */
#define FINDS 4
#define FIND_MAX 256
static struct {
    WINFIND_N *e;
    int n, next;
    HTASK task;
} finds[FINDS];

/* WIN32_FIND_DATA: attributes, three FILETIMEs, size (high, low), two
 * reserved, the name (260), the short name (14) */
static int find_put(args *a, uint32_t far, const WINFIND_N *f)
{
    static uint8_t d[318];
    memset(d, 0, sizeof(d));
    wr32(d, f->attr ? f->attr : 0x80u);     /* FILE_ATTRIBUTE_NORMAL */
    file_time(d + 4, f->date, f->time);
    memcpy(d + 12, d + 4, 8);
    memcpy(d + 20, d + 4, 8);
    wr32(d + 32, f->size);
    lstrcpyn((char *)d + 44, f->name, 13);
    lstrcpyn((char *)d + 304, f->name, 13);
    return put(a, far, d, sizeof(d));
}

static int find_service(args *a, uint16_t service, uint32_t *v)
{
    switch (service) {
    case 413: {                         /* FindFirstFile(LPCSTR, LPWIN32_FIND_DATA): 0xFFFF none */
        char spec[144];
        int i = 0;
        *v = 0xffffu;
        while (i < FINDS && finds[i].e && IsTask(finds[i].task))
            i++;
        if (i == FINDS || !str_in(a, dw(a, 8), spec, sizeof(spec)))
            return 1;
        if (!finds[i].e)
            finds[i].e = win_alloc(FIND_MAX * sizeof(WINFIND_N));
        if (!finds[i].e)
            return 1;
        finds[i].n = WinFindFilesN(spec, 0x37, finds[i].e, FIND_MAX);
        finds[i].next = 0;
        finds[i].task = GetCurrentTask();
        if (finds[i].n <= 0 || !find_put(a, dw(a, 4), &finds[i].e[0])) {
            SetLastError(2);            /* ERROR_FILE_NOT_FOUND */
            win_free(finds[i].e);
            finds[i].e = NULL;
            return 1;
        }
        finds[i].next = 1;
        *v = 0x7101u + (unsigned)i;
        return 1;
    }
    case 414: case 415: {               /* FindNextFile(HANDLE, LPWIN32_FIND_DATA), FindClose */
        unsigned i = (unsigned)w(a, service == 414 ? 8 : 4) - 0x7101u;
        *v = 0;
        if (i >= FINDS || !finds[i].e)
            return 1;
        if (service == 415) {
            win_free(finds[i].e);
            finds[i].e = NULL;
            *v = 1;
        } else if (finds[i].next < finds[i].n) {
            *v = (uint16_t)find_put(a, dw(a, 4), &finds[i].e[finds[i].next++]);
        } else {
            SetLastError(18);           /* ERROR_NO_MORE_FILES */
        }
        return 1;
    }
    }
    return 0;
}

static int kernel95(args *a, uint16_t service, uint32_t *v, int *dword)
{
    char s1[144], s2[144];
    static char buf[4096];
    if (find_service(a, service, v))
        return 1;
    switch (service) {
    case 406: case 407:
        return profile_struct(a, service == 406, v);
    case 411: {                         /* GetCurrentDirectory(DWORD size, LPSTR): its length */
        uint32_t size = dw(a, 8);
        int n = GetCurrentDirectoryN(s1, sizeof(s1));
        *dword = 1;
        *v = 0;
        if (n <= 0)
            return 1;
        *v = (uint32_t)n < size ? (put(a, dw(a, 4), s1, (uint32_t)n + 1) ? (uint32_t)n : 0)
                                : (uint32_t)n + 1;
        return 1;
    }
    case 412:                           /* SetCurrentDirectory(LPCSTR) */
        *v = str_in(a, dw(a, 4), s1, sizeof(s1)) && win_file_chdir(s1);
        return 1;
    case 416: case 417:                 /* Write(Private)ProfileSection(sec, data, file) */
        if (!str_in(a, dw(a, service == 416 ? 12 : 8), s1, sizeof(s1)) ||
            (service == 416 && !str_in(a, dw(a, 4), s2, sizeof(s2))))
            *v = 0;
        else
            *v = (uint16_t)section_put(a, s1, dw(a, service == 416 ? 8 : 4),
                                       service == 416 ? s2 : NULL);
        return 1;
    case 418: case 419: {               /* Get(Private)ProfileSection(sec, LPSTR, size, file) */
        int priv = service == 418;
        uint32_t out = dw(a, priv ? 10 : 6);
        int size = w(a, priv ? 8 : 4);
        *v = 0;
        if (!str_in(a, dw(a, priv ? 14 : 10), s1, sizeof(s1)) ||
            (priv && !str_in(a, dw(a, 4), s2, sizeof(s2))) || size <= 0)
            return 1;
        if (size > (int)sizeof(buf))
            size = sizeof(buf);
        int n = section_get(s1, buf, size, priv ? s2 : NULL);
        *v = put(a, out, buf, (uint32_t)n + 2 <= (uint32_t)size ? (uint32_t)n + 2 : (uint32_t)size) ?
             (uint16_t)n : 0;
        return 1;
    }
    case 422: {                         /* GetDiskFreeSpace(root, LPDWORD sectors a cluster,
                                         * bytes a sector, free clusters, clusters) */
        DWORD free_b = 0, total = 0;
        int drive = 0;
        if (dw(a, 20) && str_in(a, dw(a, 20), s1, sizeof(s1)) && s1[0] && s1[1] == ':')
            drive = (s1[0] | 0x20) - 'a' + 1;
        *v = 0;
        if (!GetDiskFreeSpaceN(drive, &free_b, &total))
            return 1;
        uint32_t spc = 8;               /* 4 KB clusters, more for a big disk */
        while (spc < 128 && total / (512u * spc) > 65535u)
            spc *= 2;
        *v = put32(a, dw(a, 16), spc) && put32(a, dw(a, 12), 512) &&
             put32(a, dw(a, 8), free_b / (512u * spc)) && put32(a, dw(a, 4), total / (512u * spc));
        return 1;
    }
    case 431: {                         /* IsPeFormat(LPCSTR, HFILE): a Win32 program */
        char full[160];
        *v = 0;
        if (str_in(a, dw(a, 6), s1, sizeof(s1))) {
            win_path(s1, full, sizeof(full));
            *v = win_pe_is(full) != 0;
        }
        return 1;
    }
    }
    return 0;
}

/* ---------------------------------------------------------------- GDI */

static int gdi95(args *a, uint16_t service, uint32_t *v, int *dword)
{
    switch (service) {
    case G16(250): {                    /* Copy(LPVOID from, LPVOID to, WORD bytes) */
        uint8_t b[256];
        uint32_t from = dw(a, 10), to = dw(a, 6);
        for (uint32_t done = 0, n = w(a, 4); done < n;) {
            uint32_t k = n - done < sizeof(b) ? n - done : sizeof(b);
            if (!get(a, from + done, b, k) || !put(a, to + done, b, k))
                break;
            done += k;
        }
        *v = 0;
        return 1;
    }
    case G16(245): {                    /* GetSpoolJob(int, LONG): DWORD. 14h: the queues'
                                         * list (name, port; pairs of strings, as many as
                                         * twenty: PRINTMAN reads them all) in a buffer whose
                                         * first word is its size, empty: zeros */
        uint32_t far = dw(a, 4);
        uint8_t b[2], zero[64];
        *dword = 1;
        *v = 0;
        if (w(a, 8) == 0x14 && get(a, far, b, 2)) {
            uint32_t size = rd16(b);
            if (size > 4096)
                size = 4096;
            memset(zero, 0, sizeof(zero));
            for (uint32_t done = 0; done < size;) {
                uint32_t k = size - done < sizeof(zero) ? size - done : sizeof(zero);
                if (!put(a, far + done, zero, k))
                    break;
                done += k;
            }
        }
        return 1;
    }
    case G16(281): case G16(282):       /* Drv{Set,Get}PrinterData: no such printer */
        *v = 1801;                      /* ERROR_INVALID_PRINTER_NAME */
        *dword = 1;
        return 1;
    case G16(607): {                    /* GetRegionData(HRGN, DWORD bytes, LPRGNDATA): a
                                         * RGNDATAHEADER (32 bytes), RECTs of LONGs */
        wrgn *g = gdi_region((HRGN)w(a, 12));
        uint32_t cap = dw(a, 8), out = dw(a, 4);
        *dword = 1;
        *v = 0;
        if (!g)
            return 1;
        uint32_t need = 32u + 16u * (uint32_t)g->n;
        *v = need;
        if (!out || cap < need)
            return 1;
        uint8_t h[32], r[16];
        memset(h, 0, sizeof(h));
        wr32(h, 32);
        wr32(h + 4, 1);                 /* RDH_RECTANGLES */
        wr32(h + 8, (uint32_t)g->n);
        wr32(h + 12, 16u * (uint32_t)g->n);
        wr32(h + 16, (uint32_t)g->box.left); wr32(h + 20, (uint32_t)g->box.top);
        wr32(h + 24, (uint32_t)g->box.right); wr32(h + 28, (uint32_t)g->box.bottom);
        if (!g->n)
            memset(h + 16, 0, 16);
        int ok = put(a, out, h, sizeof(h));
        for (int i = 0; ok && i < g->n; i++) {
            wr32(r, (uint32_t)g->r[i].left); wr32(r + 4, (uint32_t)g->r[i].top);
            wr32(r + 8, (uint32_t)g->r[i].right); wr32(r + 12, (uint32_t)g->r[i].bottom);
            ok = put(a, out + 32u + 16u * (uint32_t)i, r, 16);
        }
        if (!ok)
            *v = 0;
        return 1;
    }
    case G16(608): {                    /* ExtCreateRegion(LPXFORM, DWORD, const RGNDATA FAR *):
                                         * no transform */
        uint32_t in = dw(a, 4);
        uint8_t h[32], r[16];
        HRGN rgn = CreateRectRgn(0, 0, 0, 0);
        *v = 0;
        if (!rgn || !get(a, in, h, sizeof(h)) || rd32(h + 4) != 1) {
            if (rgn)
                DeleteObject(rgn);
            return 1;
        }
        uint32_t n = rd32(h + 8), size = rd32(h);
        for (uint32_t i = 0; i < n && i < 4096; i++) {
            if (!get(a, in + size + 16u * i, r, 16))
                break;
            HRGN one = CreateRectRgn((int32_t)rd32(r), (int32_t)rd32(r + 4),
                                     (int32_t)rd32(r + 8), (int32_t)rd32(r + 12));
            CombineRgn(rgn, rgn, one, RGN_OR);
            DeleteObject(one);
        }
        *v = (uint16_t)(uintptr_t)rgn;
        return 1;
    }
    }
    return 0;
}

/* ---------------------------------------------------------------- USER */

#define SIF_RANGE           0x0001
#define SIF_PAGE            0x0002
#define SIF_POS             0x0004
#define SIF_DISABLENOSCROLL 0x0008
#define SIF_TRACKPOS        0x0010

#define MIIM_STATE      0x0001
#define MIIM_ID         0x0002
#define MIIM_SUBMENU    0x0004
#define MIIM_CHECKMARKS 0x0008
#define MIIM_TYPE       0x0010
#define MIIM_DATA       0x0020
#define MFT_TYPES       (MF_BITMAP | MF_MENUBARBREAK | MF_MENUBREAK | MF_OWNERDRAW | \
                         MF_SEPARATOR | 0x0200u | 0x4000u)
#define MFS_STATES      (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE)

/* a menu item by position or command: its position, -1 none */
static int item_pos(HMENU menu, UINT item, int by_pos)
{
    int n = GetMenuItemCount(menu);
    if (by_pos)
        return (int)item < n ? (int)item : -1;
    for (int i = 0; i < n; i++)
        if (GetMenuItemID(menu, i) == item)
            return i;
    return -1;
}

/* MENUITEMINFO (Win16): cbSize, fMask, fType, fState, wID, hSubMenu,
 * hbmpChecked, hbmpUnchecked (words), dwItemData, dwTypeData (dwords),
 * cch (word) */
static int menu_item_info(args *a, uint16_t service, uint32_t *v)
{
    HMENU menu = (HMENU)w(a, 12);
    UINT item = w(a, 10);
    int by_pos = w(a, 8) != 0;
    uint32_t far = dw(a, 4);
    uint8_t m[26];
    char text[256];
    *v = 0;
    if (!IsMenu(menu) || !get(a, far, m, sizeof(m)))
        return 1;
    UINT mask = rd16(m + 2), type = rd16(m + 4), state = rd16(m + 6), id = rd16(m + 8);
    HMENU sub = (HMENU)rd16(m + 10);
    uint32_t data = rd32(m + 16);
    UINT by = by_pos ? MF_BYPOSITION : MF_BYCOMMAND;
    if (service == U16(443)) {          /* GetMenuItemInfo */
        int pos = item_pos(menu, item, by_pos);
        if (pos < 0)
            return 1;
        UINT st = GetMenuState(menu, (UINT)pos, MF_BYPOSITION);
        HMENU popup = GetSubMenu(menu, pos);
        if (mask & MIIM_TYPE) {
            wr16(m + 4, (uint16_t)(st & MFT_TYPES & ~MF_POPUP));
            uint16_t cch = rd16(m + 24);
            uint32_t out = rd32(m + 20);
            int n = GetMenuString(menu, (UINT)pos, text, sizeof(text), MF_BYPOSITION);
            if (!(st & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) && out && cch) {
                if (n >= cch)
                    n = cch - 1;
                text[n] = 0;
                put(a, out, text, (uint32_t)n + 1);
            }
            wr16(m + 24, (uint16_t)n);
        }
        if (mask & MIIM_STATE)
            wr16(m + 6, (uint16_t)((st & MFS_STATES) |
                                   (GetMenuDefaultItem(menu, TRUE, 0) == (UINT)pos ? MFS_DEFAULT : 0)));
        if (mask & MIIM_ID)
            wr16(m + 8, (uint16_t)(popup ? (UINT)(uintptr_t)popup : GetMenuItemID(menu, pos)));
        if (mask & MIIM_SUBMENU)
            wr16(m + 10, (uint16_t)(uintptr_t)popup);
        *v = put(a, far, m, sizeof(m));
        return 1;
    }
    /* the item's content: its string, bitmap or owner-draw data */
    LPCSTR content = text;
    text[0] = 0;
    if (mask & MIIM_TYPE) {
        uint32_t s = rd32(m + 20);
        if (type & MF_BITMAP)
            content = (LPCSTR)(uintptr_t)(uint16_t)s;
        else if (type & MF_OWNERDRAW)
            content = (LPCSTR)(uintptr_t)data;
        else if (!(type & MF_SEPARATOR))
            str_in(a, s, text, sizeof(text));
    }
    if (service == U16(441)) {          /* InsertMenuItem */
        UINT flags = by | (type & MFT_TYPES) | (state & MFS_STATES) | (sub ? MF_POPUP : 0);
        *v = (uint16_t)InsertMenu(menu, item, flags, sub ? (UINT)(uintptr_t)sub : id, content);
        if (*v && (state & MFS_DEFAULT))
            SetMenuDefaultItem(menu, sub ? (UINT)(uintptr_t)sub : id, FALSE);
        return 1;
    }
    /* SetMenuItemInfo: what the mask names changed, the rest kept */
    int pos = item_pos(menu, item, by_pos);
    if (pos < 0)
        return 1;
    UINT st = GetMenuState(menu, (UINT)pos, MF_BYPOSITION);
    HMENU popup = GetSubMenu(menu, pos);
    if (mask & (MIIM_TYPE | MIIM_ID | MIIM_SUBMENU)) {
        UINT t = mask & MIIM_TYPE ? type & MFT_TYPES : st & MFT_TYPES & ~MF_POPUP;
        HMENU p = mask & MIIM_SUBMENU ? sub : popup;
        UINT new_id = p ? (UINT)(uintptr_t)p : mask & MIIM_ID ? id : GetMenuItemID(menu, pos);
        if (!(mask & MIIM_TYPE) && !(t & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)))
            GetMenuString(menu, (UINT)pos, text, sizeof(text), MF_BYPOSITION);
        if (!ModifyMenu(menu, (UINT)pos, MF_BYPOSITION | t | (st & MFS_STATES) | (p ? MF_POPUP : 0),
                        new_id, content))
            return 1;
    }
    if (mask & MIIM_STATE) {
        CheckMenuItem(menu, (UINT)pos, MF_BYPOSITION | (state & MF_CHECKED));
        EnableMenuItem(menu, (UINT)pos, MF_BYPOSITION | (state & (MF_GRAYED | MF_DISABLED)));
        if (state & MFS_DEFAULT)
            SetMenuDefaultItem(menu, (UINT)pos, TRUE);
    }
    *v = 1;
    return 1;
}

/* SCROLLINFO (Win16): cbSize, fMask, nMin, nMax, nPage, nPos, nTrackPos */
static int scroll_info(args *a, uint16_t service, uint32_t *v)
{
    int set = service == U16(475);
    HWND h = w(a, set ? 12 : 10);
    int bar = (int16_t)w(a, set ? 10 : 8);
    uint32_t far = dw(a, set ? 6 : 4);
    uint8_t s[14];
    *v = 0;
    if (!get(a, far, s, sizeof(s)))
        return 1;
    UINT mask = rd16(s + 2);
    if (set) {
        BOOL redraw = w(a, 4) != 0;
        if (mask & SIF_RANGE)
            SetScrollRange(h, bar, (int16_t)rd16(s + 4), (int16_t)rd16(s + 6),
                           redraw && !(mask & SIF_POS));
        if (mask & SIF_POS)
            SetScrollPos(h, bar, (int16_t)rd16(s + 10), redraw);
        *v = (uint16_t)GetScrollPos(h, bar);
        return 1;
    }
    int lo = 0, hi = 0, pos = GetScrollPos(h, bar);
    GetScrollRange(h, bar, &lo, &hi);
    if (mask & SIF_RANGE) {
        wr16(s + 4, (uint16_t)lo);
        wr16(s + 6, (uint16_t)hi);
    }
    if (mask & SIF_PAGE)
        wr16(s + 8, 0);                 /* no proportional thumbs */
    if (mask & SIF_POS)
        wr16(s + 10, (uint16_t)pos);
    if (mask & SIF_TRACKPOS)
        wr16(s + 12, (uint16_t)pos);
    *v = put(a, far, s, sizeof(s));
    return 1;
}

/* DrawState(HDC, HBRUSH, DRAWSTATEPROC, LPARAM, WPARAM, x, y, cx, cy, flags) */
static int draw_state(args *a, uint32_t *v)
{
    HDC dc = (HDC)w(a, 26);
    uint32_t proc = dw(a, 20), lp = dw(a, 16);
    UINT wp = w(a, 14), flags = w(a, 4);
    int x = (int16_t)w(a, 12), y = (int16_t)w(a, 10), cx = (int16_t)w(a, 8), cy = (int16_t)w(a, 6);
    UINT type = flags & 0x000f;
    static char text[256];
    *v = 0;
    if (type == DST_COMPLEX || (proc && type != DST_ICON && type != DST_BITMAP)) {
        /* the program's procedure draws it at 0, 0 */
        if (!proc)
            return 1;
        DWORD org = GetViewportOrg(dc);
        SetViewportOrg(dc, (int16_t)LOWORD(org) + x, (int16_t)HIWORD(org) + y);
        const uint16_t words[6] = { (uint16_t)(uintptr_t)dc, (uint16_t)(lp >> 16), (uint16_t)lp,
                                    (uint16_t)wp, (uint16_t)cx, (uint16_t)cy };
        *v = (uint16_t)win_ne_call(a->ctx, proc, words, 6, NULL, 0);
        SetViewportOrg(dc, (int16_t)LOWORD(org), (int16_t)HIWORD(org));
        return 1;
    }
    if (type == DST_TEXT || type == DST_PREFIXTEXT) {
        int n = wp ? (int)wp : -1;
        if (n >= (int)sizeof(text))
            n = sizeof(text) - 1;
        if (n < 0 ? !str_in(a, lp, text, sizeof(text)) : !get(a, lp, text, (uint32_t)n))
            return 1;
        if (n >= 0)
            text[n] = 0;
        *v = (uint16_t)DrawStateN(dc, 0, type, text, -1, 0, x, y, cx, cy, flags);
        return 1;
    }
    *v = (uint16_t)DrawStateN(dc, 0, type, NULL, 0, (HANDLE)(uintptr_t)(uint16_t)lp, x, y, cx,
                              cy, flags);
    return 1;
}

/* TileWindows, CascadeWindows(HWND, UINT, LPCRECT, UINT, const HWND FAR *) */
static int tile(args *a, uint16_t service, uint32_t *v)
{
    HWND kids[64];
    uint8_t b[8];
    RECT rc;
    uint32_t rf = dw(a, 10), kf = dw(a, 4);
    UINT n = w(a, 8);
    if (n > 64)
        n = 64;
    for (UINT i = 0; kf && i < n; i++) {
        if (!get(a, kf + 2u * i, b, 2))
            n = i;
        kids[i] = (HWND)rd16(b);
    }
    if (rf && get(a, rf, b, 8))
        SetRect(&rc, (int16_t)rd16(b), (int16_t)rd16(b + 2), (int16_t)rd16(b + 4),
                (int16_t)rd16(b + 6));
    *v = (uint16_t)TileWindowsN((HWND)w(a, 16), w(a, 14), rf ? &rc : NULL, n, kf ? kids : NULL,
                                service == U16(429));
    return 1;
}

/* the group's entry nearest to 32 x 32 of 16 colours: from a directory
 * in the program's memory (far) or a global block */
static int pick_from(args *a, uint32_t far, int cursor, uint32_t *v)
{
    static uint8_t dir[6 + 14 * 64];
    uint16_t id = 0;
    *v = 0;
    if (!get(a, far, dir, 6))
        return 1;
    uint32_t n = rd16(dir + 4);
    if (n > 64)
        n = 64;
    if (get(a, far + 6, dir + 6, 14u * n) && win_group_pick(dir, 6 + 14u * n, cursor, &id))
        *v = id;
    return 1;
}

/* InternalExtractIcon(HINSTANCE, file, first, n): a global block of n
 * HICONs; ExtractIconEx(file, first, HICON FAR *large, HICON FAR *small,
 * n): how many (first -1, no arrays: the file's count). WINN's icons have
 * one size: the small ones are the same. */
static int extract(args *a, uint16_t op, uint32_t *v)
{
    char file[144], full[160];
    *v = 0;
    if (!str_in(a, dw(a, op == 39 ? 8 : 16), file, sizeof(file)))
        return 1;
    win_path(file, full, sizeof(full));
    UINT count = (UINT)(uintptr_t)ExtractIconExN(full, (UINT)-1);
    if (op == 39) {
        UINT first = w(a, 6), n = w(a, 4);
        win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
        uint16_t sel = n ? win_ne_guest_global_alloc(g, 2u * n) : 0;
        for (UINT i = 0; sel && i < n; i++) {
            uint8_t b[2];
            wr16(b, first + i < count ? (uint16_t)(uintptr_t)ExtractIconExN(full, first + i) : 0);
            win_ne_write(a->ctx, sel, 2u * i, b, 2);
        }
        *v = sel;
        return 1;
    }
    int first = (int16_t)w(a, 14);
    uint32_t large = dw(a, 10), small = dw(a, 6);
    UINT n = w(a, 4);
    if (first == -1 && !large && !small) {
        *v = count;
        return 1;
    }
    UINT done = 0;
    for (UINT i = 0; first >= 0 && i < n && (UINT)first + i < count; i++, done++) {
        uint8_t b[2];
        HICON h = ExtractIconExN(full, (UINT)first + i);
        wr16(b, (uint16_t)(uintptr_t)h);
        if (large)
            put(a, large + 2u * i, b, 2);
        if (small) {
            wr16(b, (uint16_t)(uintptr_t)(large ? CopyIcon(0, h) : h));
            put(a, small + 2u * i, b, 2);
        }
    }
    *v = done;
    return 1;
}

/* the control classes' procedures (EDITWNDPROC ...), as the program calls
 * one it was not given by GetClassLong */
static int control_proc(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    LRESULT (CALLBACK *proc)(HWND, UINT, WPARAM, LPARAM);
    switch (service - 0x1000u) {
    case 301: proc = user_edit_proc; break;
    case 302: proc = user_static_proc; break;
    case 303: proc = user_button_proc; break;
    case 304: proc = user_scrollbar_proc; break;
    case 305: proc = user_desktop_proc; break;
    case 306: proc = user_menu_popup_proc; break;
    case 307: proc = user_listbox_proc; break;
    case 344: proc = user_combo_proc; break;
    default: return 0;
    }
    *v = (uint32_t)ne_defproc(a, u, proc, w(a, 12), w(a, 10), w(a, 8), dw(a, 4));
    *dword = 1;
    return 1;
}

static int user95_more(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    if (control_proc(a, u, service, v, dword))
        return 1;
    switch (service) {
    case U16(441): case U16(443): case U16(446):
        return menu_item_info(a, service, v);
    case U16(475): case U16(476):
        return scroll_info(a, service, v);
    case U16(449):
        return draw_state(a, v);
    case U16(428): case U16(429):
        return tile(a, service, v);
    case U16(336): case U16(356): case U16(357):    /* Load*Handler(HGLOBAL, HMODULE, HRSRC):
                                                     * the block, as it is */
        *v = w(a, 8);
        return 1;
    case U16(456):                      /* LoadIconHandler(HGLOBAL, BOOL) */
        *v = w(a, 6);
        return 1;
    case U16(364):                      /* LookupIconIdFromDirectoryEx(LPBYTE, BOOL icon,
                                         * cx, cy, flags) */
        return pick_from(a, dw(a, 12), !w(a, 10), v);
    case U16(455):                      /* GetIconID(HGLOBAL directory, DWORD RT_ICON/CURSOR) */
        return pick_from(a, (uint32_t)w(a, 8) << 16, dw(a, 4) == 1, v);
    case U16(450): {                    /* CreateIconFromResourceEx(LPBYTE, UINT bytes, BOOL
                                         * icon, DWORD version, cx, cy, flags) */
        uint32_t n = w(a, 16);
        int icon = w(a, 14) != 0;
        uint8_t *d = n ? win_alloc(n) : NULL;
        *v = 0;
        if (d && get(a, dw(a, 18), d, n))
            *v = (uint16_t)(uintptr_t)(icon ? win_icon_from_dib(d, n) :
                                       n > 4 ? win_cursor_from_dib(d + 4, n - 4, rd16(d),
                                                                   rd16(d + 2)) : 0);
        win_free(d);
        return 1;
    }
    case U16(408): {                    /* CreateCursorIconIndirect(HINSTANCE, CURSORICONINFO
                                         * FAR *, LPCVOID and, LPCVOID xor) */
        uint8_t ci[12];
        *v = 0;
        if (!get(a, dw(a, 12), ci, sizeof(ci)))
            return 1;
        int wd = (int16_t)rd16(ci + 4), ht = (int16_t)rd16(ci + 6), row = rd16(ci + 8);
        int planes = ci[10], bpp = ci[11];
        uint32_t and_n = (uint32_t)((wd + 15) / 16 * 2) * (uint32_t)ht;
        uint32_t xor_n = (uint32_t)row * (uint32_t)ht * (uint32_t)(planes ? planes : 1);
        if (wd <= 0 || ht <= 0 || wd > 64 || ht > 64 || xor_n > 8192)
            return 1;
        uint8_t *m = win_alloc(and_n + xor_n);
        if (m && get(a, dw(a, 8), m, and_n) && get(a, dw(a, 4), m + and_n, xor_n))
            *v = (uint16_t)(uintptr_t)(bpp == 1 && planes == 1 ?
                 CreateCursor(0, (int16_t)rd16(ci), (int16_t)rd16(ci + 2), wd, ht, m, m + and_n) :
                 CreateIcon(0, wd, ht, (BYTE)planes, (BYTE)bpp, m, m + and_n));
        win_free(m);
        return 1;
    }
    case U16(827): {                    /* MessageBoxIndirect(LPMSGBOXPARAMS): cbSize, owner,
                                         * instance, text, caption, style, icon, help id,
                                         * callback, language */
        uint8_t p[24];
        char text[512], caption[128];
        *v = 0;
        if (!get(a, dw(a, 4), p, sizeof(p)) || !str_in(a, rd32(p + 8), text, sizeof(text)))
            return 1;
        if (!str_in(a, rd32(p + 12), caption, sizeof(caption)))
            lstrcpy(caption, "Error");
        *v = (uint16_t)MessageBox((HWND)rd16(p + 4), text, caption,
                                  (UINT)rd32(p + 16) & ~0x0080u);   /* MB_USERICON: none */
        return 1;
    }
    case U16(667): {                    /* TrackPopupMenuEx(HMENU, flags, x, y, HWND, LPTPMPARAMS) */
        *v = (uint16_t)TrackPopupMenu((HMENU)w(a, 16), w(a, 14), (int16_t)w(a, 12),
                                      (int16_t)w(a, 10), 0, (HWND)w(a, 8), NULL);
        return 1;
    }
    case U16(479):                      /* MenuItemFromPoint(HWND, HMENU, POINT): WINN keeps
                                         * no items' places for it */
        *v = 0xffffu;
        return 1;
    case U16(665):                      /* GetMenuItemRect(HWND, HMENU, UINT, LPRECT): neither */
    case U16(668):                      /* SetWindowRgn(HWND, HRGN, BOOL): rectangular only */
    case U16(669):                      /* GetWindowRgn(HWND, HRGN): ERROR, none */
        *v = 0;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- the rest */

/* the keyboard layouts WINN has: US English, Russian (HKL: the language
 * twice) */
#define HKL_US 0x04090409u
#define HKL_RU 0x04190419u

extern int user_keyboard_layout(void);
extern void user_set_keyboard_layout(int russian);

static uint32_t hkl_now(void) { return user_keyboard_layout() ? HKL_RU : HKL_US; }

/* a layout's name ("00000409") as one of WINN's: 1 Russian, 0 US, -1 none */
static int layout_named(const char *s)
{
    uint32_t id = 0;
    for (int i = 0; s[i] && i < 8; i++) {
        char c = s[i];
        id = id << 4 | (uint32_t)(c >= '0' && c <= '9' ? c - '0' :
                                  c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                                  c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
    }
    return (id & 0xffffu) == 0x0419u ? 1 : (id & 0xffffu) == 0x0409u ? 0 : -1;
}

/* a Win16 RECT */
static void rect16_put(uint8_t *g, const RECT *r)
{
    wr16(g, (uint16_t)r->left); wr16(g + 2, (uint16_t)r->top);
    wr16(g + 4, (uint16_t)r->right); wr16(g + 6, (uint16_t)r->bottom);
}

static int win95_extra(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    (void)u;
    switch (service) {
    /* USER */
    case U16(460): {                    /* GetInternalWindowPos(HWND, LPRECT, LPPOINT): the
                                         * restored place, the icon's; its show state */
        WINDOWPLACEMENT wp;
        uint8_t g[8];
        memset(&wp, 0, sizeof(wp));
        wp.length = sizeof(wp);
        *v = 0;
        if (!GetWindowPlacement(w(a, 12), &wp))
            return 1;
        rect16_put(g, &wp.rcNormalPosition);
        if (dw(a, 8))
            put(a, dw(a, 8), g, 8);
        wr16(g, (uint16_t)wp.ptMinPosition.x);
        wr16(g + 2, (uint16_t)wp.ptMinPosition.y);
        if (dw(a, 4))
            put(a, dw(a, 4), g, 4);
        *v = wp.showCmd;
        return 1;
    }
    case U16(461): {                    /* SetInternalWindowPos(HWND, UINT show, LPRECT, LPPOINT) */
        WINDOWPLACEMENT wp;
        uint8_t g[8];
        memset(&wp, 0, sizeof(wp));
        wp.length = sizeof(wp);
        HWND h = w(a, 14);
        if (!GetWindowPlacement(h, &wp))
            return 1;
        wp.showCmd = w(a, 12);
        if (dw(a, 8) && get(a, dw(a, 8), g, 8))
            SetRect(&wp.rcNormalPosition, (int16_t)rd16(g), (int16_t)rd16(g + 2),
                    (int16_t)rd16(g + 4), (int16_t)rd16(g + 6));
        if (dw(a, 4) && get(a, dw(a, 4), g, 4)) {
            wp.ptMinPosition.x = (int16_t)rd16(g);
            wp.ptMinPosition.y = (int16_t)rd16(g + 2);
            wp.flags |= WPF_SETMINPOSITION;
        }
        SetWindowPlacement(h, &wp);
        return 1;
    }
    case U16(464):                      /* DragObject(HWND, HWND, UINT, DWORD, HCURSOR): the
                                         * File Manager's drag protocol; nothing dropped */
        *v = 0;
        *dword = 1;
        return 1;
    case U16(343):                      /* GetFilePortName(LPOFSTRUCT): none */
        *v = 0;
        return 1;
    case U16(499):                      /* WNetErrorText(WORD, LPSTR, WORD): none */
        *v = 0;
        return 1;
    case U16(521): case U16(522): case U16(533):
        *v = 1;                         /* WNetEnable, Disable, Initialize:
                                         * WN_NOT_SUPPORTED */
        return 1;
    case U16(477): {                    /* GetKeyboardLayoutName(LPSTR): 8 hex digits */
        char name[12];
        wsprintf(name, "%08lX", (unsigned long)(hkl_now() & 0xffffu));
        *v = put(a, dw(a, 4), name, 9);
        return 1;
    }
    case U16(478): {                    /* LoadKeyboardLayout(LPCSTR, UINT flags): WINN's
                                         * two; KLF_ACTIVATE (1) makes it the layout */
        char name[16];
        int k = str_in(a, dw(a, 6), name, sizeof(name)) ? layout_named(name) : -1;
        *v = 0;
        if (k >= 0) {
            if (w(a, 4) & 1u)
                user_set_keyboard_layout(k);
            *v = k ? HKL_RU : HKL_US;
        }
        *dword = 1;
        return 1;
    }
    case U16(650): {                    /* ActivateKeyboardLayout(HKL, UINT): the one before;
                                         * HKL_NEXT (1), HKL_PREV (0): the other */
        uint32_t was = hkl_now(), h = dw(a, 6);
        int k = h <= 1u ? !user_keyboard_layout() : (h & 0xffffu) == 0x0419u ? 1 :
                (h & 0xffffu) == 0x0409u ? 0 : -1;
        *v = 0;
        if (k >= 0) {
            user_set_keyboard_layout(k);
            *v = was;
        }
        *dword = 1;
        return 1;
    }
    case U16(651):                      /* GetKeyboardLayout(DWORD thread) */
        *v = hkl_now();
        *dword = 1;
        return 1;
    case U16(652): {                    /* GetKeyboardLayoutList(int, HKL FAR *): the two */
        int n = (int16_t)w(a, 8);
        uint32_t far = dw(a, 4);
        if (n > 0 && far) {
            put32(a, far, HKL_US);
            if (n > 1)
                put32(a, far + 4, HKL_RU);
        }
        *v = n > 0 && n < 2 ? 1 : 2;
        return 1;
    }
    case U16(654):                      /* UnloadKeyboardLayout(HKL): WINN's stay */
        *v = (dw(a, 4) & 0xffffu) == 0x0409u || (dw(a, 4) & 0xffffu) == 0x0419u;
        return 1;
    case U16(604): {                    /* BroadcastSystemMessage(DWORD flags, LPDWORD
                                         * recipients, UINT, WPARAM, LPARAM): to the top-level
                                         * windows (sent, BSF_POSTMESSAGE posted) */
        uint32_t flags = dw(a, 16), lp = dw(a, 4);
        UINT msg = w(a, 10);
        WPARAM wp = w(a, 8);
        if (flags & 0x10u)
            PostMessage(HWND_BROADCAST, msg, wp, (LPARAM)lp);
        else
            SendMessage(HWND_BROADCAST, msg, wp, (LPARAM)lp);
        *v = 1;
        *dword = 1;
        return 1;
    }
    case U16(620): {                    /* ChangeDisplaySettings(LPDEVMODE, DWORD): the mode
                                         * there is; another one: DISP_CHANGE_BADMODE */
        uint32_t far = dw(a, 8);
        uint8_t d[124];
        *v = 0;
        if (far && get(a, far, d, sizeof(d))) {
            uint32_t fields = rd32(d + 40);
            HDC dc = GetDC(0);
            int bits = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
            int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN);
            ReleaseDC(0, dc);
            if (((fields & 0x40000u) && rd32(d + 104) != (uint32_t)bits) ||
                ((fields & 0x80000u) && rd32(d + 108) != (uint32_t)cx) ||
                ((fields & 0x100000u) && rd32(d + 112) != (uint32_t)cy))
                *v = (uint32_t)-2;
        }
        *dword = 1;
        return 1;
    }
    case U16(621): {                    /* EnumDisplaySettings(LPCSTR, DWORD mode, LPDEVMODE):
                                         * the one mode */
        uint32_t mode = dw(a, 8), far = dw(a, 4);
        uint8_t d[124];
        *v = 0;
        if ((mode == 0 || mode == 0xffffffffu) && far) {
            HDC dc = GetDC(0);
            int bits = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
            ReleaseDC(0, dc);
            memset(d, 0, sizeof(d));
            lstrcpyn((char *)d, "DISPLAY", 32);
            wr16(d + 32, 0x0400);       /* dmSpecVersion */
            wr16(d + 36, sizeof(d));    /* dmSize */
            wr32(d + 40, 0x1c0000u | 0x400000u);   /* BITSPERPEL, PELSWIDTH, HEIGHT, FREQUENCY */
            wr32(d + 104, (uint32_t)bits);
            wr32(d + 108, (uint32_t)GetSystemMetrics(SM_CXSCREEN));
            wr32(d + 112, (uint32_t)GetSystemMetrics(SM_CYSCREEN));
            wr32(d + 120, 60);
            *v = put(a, far, d, sizeof(d));
        }
        return 1;
    }
    case U16(640): {                    /* MsgWaitForMultipleObjects(DWORD n, LPHANDLE, BOOL,
                                         * DWORD ms, DWORD mask): Win16 has no objects to
                                         * wait for: the input (WAIT_OBJECT_0 + n), or the
                                         * time out (WAIT_TIMEOUT) */
        uint32_t n = dw(a, 18), ms = dw(a, 8);
        UINT mask = (UINT)(dw(a, 4) & 0xffffu);
        DWORD start = GetTickCount();
        *dword = 1;
        for (;;) {
            if (HIWORD(GetQueueStatus(mask ? mask : 0xff)) || (mask & QS_POSTMESSAGE &&
                GetQueueStatus(QS_POSTMESSAGE))) {
                *v = n;
                return 1;
            }
            if (ms != 0xffffffffu && GetTickCount() - start >= ms) {
                *v = 0x102u;
                return 1;
            }
            Yield();
        }
    }
    case U16(660): {                    /* DrawCaption(HWND, HDC, LPRECT, UINT flags): the
                                         * window's title on the caption's colour */
        uint8_t g[8];
        RECT r;
        HDC dc = w(a, 10);
        *v = 0;
        if (!get(a, dw(a, 6), g, 8))
            return 1;
        SetRect(&r, (int16_t)rd16(g), (int16_t)rd16(g + 2), (int16_t)rd16(g + 4),
                (int16_t)rd16(g + 6));
        int active = (w(a, 4) & 1u) != 0;     /* DC_ACTIVE */
        HBRUSH br = CreateSolidBrush(GetSysColor(active ? COLOR_ACTIVECAPTION
                                                          : COLOR_INACTIVECAPTION));
        FillRect(dc, &r, br);
        DeleteObject(br);
        char title[128];
        GetWindowText(w(a, 12), title, sizeof(title));
        int mode = SetBkMode(dc, TRANSPARENT);
        COLORREF old = SetTextColor(dc, GetSysColor(active ? COLOR_CAPTIONTEXT
                                                           : COLOR_INACTIVECAPTIONTEXT));
        r.left += 2;
        DrawText(dc, title, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, old);
        SetBkMode(dc, mode);
        *v = 1;
        return 1;
    }
    /* GDI */
    case G16(506): {                    /* SetMetaRgn(HDC): the clip region as it is */
        RECT r;
        *v = (uint16_t)GetClipBox(w(a, 4), &r);
        return 1;
    }
    case G16(523): {                    /* ExtCreatePen(DWORD style, DWORD width, LPLOGBRUSH,
                                         * DWORD, LPDWORD): its style and colour, cosmetic
                                         * one pixel wide */
        uint8_t lb[8];
        uint32_t style = dw(a, 20), width = dw(a, 16);
        COLORREF c = get(a, dw(a, 12), lb, 8) ? rd32(lb + 2) & 0xffffffu : 0;
        int ps = (int)(style & 0x0fu);
        if (ps > PS_INSIDEFRAME)
            ps = PS_SOLID;
        *v = (uint16_t)(uintptr_t)CreatePen(ps, (style & 0x10000u) ? (int)width : 1, c);
        return 1;
    }
    case G16(526): case G16(527): {     /* GetMiterLimit(HDC, FLOAT FAR *), SetMiterLimit(HDC,
                                         * FLOAT, FLOAT FAR *): 10, as Windows' default */
        static const uint8_t ten[4] = { 0x00, 0x00, 0x20, 0x41 };      /* 10.0f */
        uint32_t out = dw(a, 4);
        if (out)
            put(a, out, ten, 4);
        *v = 1;
        return 1;
    }
    case G16(611): {                    /* GetRandomRgn(HDC, HRGN, INT which): SYSRGN (4) the
                                         * DC's visible part, in screen coordinates */
        RECT r;
        HDC dc = w(a, 8);
        HRGN rgn = w(a, 6);
        *v = 0;
        if (w(a, 4) != 4 || GetClipBox(dc, &r) == ERROR)
            return 1;
        DWORD org = GetDCOrg(dc);
        LPtoDP(dc, (LPPOINT)&r, 2);
        SetRectRgn(rgn, r.left + (int16_t)LOWORD(org), r.top + (int16_t)HIWORD(org),
                   r.right + (int16_t)LOWORD(org), r.bottom + (int16_t)HIWORD(org));
        *v = 1;
        return 1;
    }
    case G16(615): {                    /* GetCharacterPlacement(HDC, LPCSTR, int, int max,
                                         * LPGCP_RESULTS, DWORD): left to right, a glyph a
                                         * character, their widths and carets */
        HDC dc = w(a, 20);
        int n = (int16_t)w(a, 14);
        uint32_t res = dw(a, 8);
        char text[256];
        uint8_t r[32];
        *v = 0;
        *dword = 1;
        if (n < 0 || n > 255 || !get(a, dw(a, 16), text, (uint32_t)n) ||
            (res && !get(a, res, r, sizeof(r))))
            return 1;
        int x = 0, height = 0;
        uint32_t order = res ? rd32(r + 8) : 0, dx = res ? rd32(r + 12) : 0,
                 caret = res ? rd32(r + 16) : 0, glyphs = res ? rd32(r + 24) : 0;
        int room = res ? rd16(r + 28) : n;
        if (n > room)
            n = room;
        for (int i = 0; i < n; i++) {
            SIZE sz;
            GetTextExtentPoint(dc, text + i, 1, &sz);
            uint8_t g[2];
            wr16(g, (uint16_t)i);
            if (order) put(a, order + 2u * (uint32_t)i, g, 2);
            if (caret) { wr16(g, (uint16_t)x); put(a, caret + 2u * (uint32_t)i, g, 2); }
            wr16(g, (uint16_t)sz.cx);
            if (dx) put(a, dx + 2u * (uint32_t)i, g, 2);
            wr16(g, (uint8_t)text[i]);
            if (glyphs) put(a, glyphs + 2u * (uint32_t)i, g, 2);
            x += sz.cx;
            if (sz.cy > height) height = sz.cy;
        }
        if (res) {
            uint8_t g[4];
            wr16(g, (uint16_t)n);       /* nGlyphs, nMaxFit */
            wr16(g + 2, (uint16_t)n);
            put(a, res + 28, g, 4);
            if (rd32(r + 4) && n)       /* lpOutString: the text as it is */
                put(a, rd32(r + 4), text, (uint32_t)n);
        }
        *v = (uint32_t)(uint16_t)x | (uint32_t)(uint16_t)height << 16;
        return 1;
    }
    case G16(616):                      /* GetFontLanguageInfo(HDC): none of its flags */
        *v = 0;
        *dword = 1;
        return 1;
    }
    return 0;
}

int ne_win95_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    if (win95_extra(a, u, service, v, dword))
        return 1;
    if (service == SH + 39 || service == SH + 40)
        return extract(a, (uint16_t)(service - SH), v);
    if (service < 0x1000u)
        return kernel95(a, service, v, dword);
    if (service < 0x2000u)
        return user95_more(a, u, service, v, dword);
    if (service >= 0x3000u && service < 0x4000u)
        return gdi95(a, service, v, dword);
    return 0;
}

#endif
