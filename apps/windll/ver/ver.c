/*
 * ver.c - VER.DLL, WINN's: a file's resources and its VERSIONINFO
 * (GetFileResource, GetFileVersionInfo, VerQueryValue in the block),
 * VerLanguageName, VerFindFile, VerInstallFile (a copy, expanded by
 * LZEXPAND's LZCopy when that DLL is there). A native DLL (ddi/windll.h):
 * its functions for native callers, and Win16 programs' calls of them.
 *
 * A VERSIONINFO block: nodes of a length (word), their value's length
 * (word), a key (zero-ended), the value and the children each 4-aligned.
 *
 * It keeps nothing: no task's data, nothing to let go.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "windll.h"

/* VerFindFile's, VerInstallFile's results */
enum { VFFF_ISSHAREDFILE = 1, VFF_CURNEDEST = 1, VFF_BUFFTOOSMALL = 4 };
#define VIF_OUTOFSPACE    0x00000100ul
#define VIF_CANNOTCREATE  0x00000800ul
#define VIF_OUTOFMEMORY   0x00008000ul
#define VIF_CANNOTREADSRC 0x00010000ul
#define VIF_BUFFTOOSMALL  0x00040000ul

#define RT_VERSION_ 16

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---------------------------------------------------------------- resources */

static DWORD WINAPI GetFileResourceSize(LPCSTR file, LPCSTR type, LPCSTR id, DWORD *off)
{
    if (off)
        *off = 0;
    return file ? GetFileResourceN(file, type, id, 0, NULL, 0) : 0;
}

static BOOL WINAPI GetFileResource(LPCSTR file, LPCSTR type, LPCSTR id, DWORD off, DWORD len,
                                   void *buf)
{
    (void)off;                          /* GetFileResourceSize's: always 0 */
    return file && buf && GetFileResourceN(file, type, id, 0, buf, len) != 0;
}

/* VERSIONINFO's: the first of its type; its own length (a block may have
 * padding after it) */
static DWORD WINAPI GetFileVersionInfoSize(LPCSTR file, DWORD *handle)
{
    uint8_t head[2];
    if (handle)
        *handle = 0;
    DWORD n = file ? GetFileResourceN(file, MAKEINTRESOURCE(RT_VERSION_), NULL, 0, head, 2) : 0;
    if (n >= 2 && rd16(head) <= n)
        n = rd16(head);
    return n;
}

static BOOL WINAPI GetFileVersionInfo(LPCSTR file, DWORD handle, DWORD len, void *buf)
{
    (void)handle;
    return file && buf &&
           GetFileResourceN(file, MAKEINTRESOURCE(RT_VERSION_), NULL, 0, buf, len) != 0;
}

/* ---------------------------------------------------------------- VerQueryValue */

typedef struct {
    uint32_t len, vlen, val, kids;
    const char *key;
} ver_node;

/* the node at off of the block b (n bytes) */
static int node_at(const uint8_t *b, uint32_t n, uint32_t off, ver_node *v)
{
    if (off + 5 > n)
        return 0;
    v->len = rd16(b + off);
    v->vlen = rd16(b + off + 2);
    if (v->len < 5 || off + v->len > n)
        return 0;
    v->key = (const char *)b + off + 4;
    uint32_t k = off + 4;
    while (k < off + v->len && b[k])
        k++;
    if (k >= off + v->len)
        return 0;
    v->val = (k + 1 + 3) & ~3u;
    v->kids = (v->val + v->vlen + 3) & ~3u;
    return 1;
}

/* a path ("\", "\StringFileInfo\040904E4\FileVersion",
 * "\VarFileInfo\Translation") in the block (its own length): its node's
 * value, where in the block, and its length */
static int ver_query(const uint8_t *b, const char *path, uint32_t *val, uint32_t *len)
{
    ver_node cur;
    uint32_t at = 0, n = rd16(b);
    if (!node_at(b, n, 0, &cur))
        return 0;
    while (*path) {
        while (*path == '\\')
            path++;
        if (!*path)
            break;
        char part[64];
        uint32_t k = 0;
        while (*path && *path != '\\' && k + 1 < sizeof(part))
            part[k++] = *path++;
        part[k] = 0;
        uint32_t c = cur.kids, end = at + cur.len;
        ver_node child;
        int found = 0;
        while (c < end && node_at(b, n, c, &child)) {
            if (!lstrcmpi(child.key, part)) {
                found = 1;
                break;
            }
            c = (c + child.len + 3) & ~3u;
        }
        if (!found)
            return 0;
        cur = child;
        at = c;
    }
    *val = cur.val;
    *len = cur.vlen;
    return cur.vlen != 0;
}

static BOOL WINAPI VerQueryValue(const void *block, LPCSTR path, void **out, UINT *len)
{
    uint32_t val, n;
    if (!block || !path || !ver_query((const uint8_t *)block, path, &val, &n))
        return FALSE;
    if (out)
        *out = (uint8_t *)(uintptr_t)block + val;
    if (len)
        *len = (UINT)n;
    return TRUE;
}

static UINT WINAPI VerLanguageName(UINT lang, LPSTR buf, UINT cap)
{
    static const struct { uint16_t lang; const char *name; } l[] = {
        { 0x0000, "Language Neutral" }, { 0x0405, "Czech" }, { 0x0407, "German" },
        { 0x0409, "English (United States)" }, { 0x0809, "English (United Kingdom)" },
        { 0x040a, "Spanish (Traditional Sort)" }, { 0x040c, "French" },
        { 0x0410, "Italian" }, { 0x0411, "Japanese" }, { 0x0415, "Polish" },
        { 0x0419, "Russian" }, { 0x0422, "Ukrainian" },
    };
    const char *name = "Unknown language";
    for (unsigned i = 0; i < sizeof(l) / sizeof(l[0]); i++)
        if (l[i].lang == lang)
            name = l[i].name;
    if (!buf || !cap)
        return 0;
    lstrcpyn(buf, name, (int)cap);
    return (UINT)strlen(buf);
}

/* ---------------------------------------------------------------- installing */

/* dir and name, a backslash between them */
static void join(char *out, uint32_t cap, const char *dir, const char *name)
{
    uint32_t n = dir ? (uint32_t)strlen(dir) : 0;
    lstrcpyn(out, dir ? dir : "", (int)cap);
    if (n && n + 1 < cap && dir[n - 1] != '\\' && dir[n - 1] != ':')
        out[n++] = '\\', out[n] = 0;
    if (n < cap)
        lstrcpyn(out + n, name ? name : "", (int)(cap - n));
}

static int exists(const char *path)
{
    HFILE f = _lopen(path, OF_READ);
    if (f == HFILE_ERROR)
        return 0;
    _lclose(f);
    return 1;
}

/* s into out (cap bytes, cut); its length then */
static UINT put(LPSTR out, UINT cap, const char *s)
{
    if (!out || !cap)
        return 0;
    lstrcpyn(out, s, (int)cap);
    return (UINT)strlen(out);
}

/* where the file is to go (a shared one: Windows', the application's
 * else) and where it is now, if anywhere */
static UINT WINAPI VerFindFile(UINT flags, LPCSTR file, LPCSTR windir, LPCSTR appdir,
                               LPSTR curdir, UINT *curlen, LPSTR destdir, UINT *destlen)
{
    char path[200];
    int shared = flags & VFFF_ISSHAREDFILE;
    const char *dest = shared ? windir : appdir, *other = shared ? appdir : windir, *cur = "";
    if (!dest)
        dest = "";
    if (!other)
        other = "";
    join(path, sizeof(path), dest, file);
    if (exists(path)) {
        cur = dest;
    } else {
        join(path, sizeof(path), other, file);
        if (exists(path))
            cur = other;
    }
    UINT r = cur[0] && lstrcmpi(cur, dest) ? VFF_CURNEDEST : 0;
    UINT curcap = curlen ? *curlen : 0, destcap = destlen ? *destlen : 0;
    if (strlen(cur) >= curcap || strlen(dest) >= destcap)
        r |= VFF_BUFFTOOSMALL;
    if (curlen)
        *curlen = put(curdir, curcap, cur) + 1;
    if (destlen)
        *destlen = put(destdir, destcap, dest) + 1;
    return r;
}

/* src (srcdir) to dest (destdir), expanded by LZEXPAND's LZCopy (a copy
 * as it is without the DLL): 0 or VIF_... */
static DWORD install_file(const char *src, const char *dest)
{
    HFILE in = _lopen(src, OF_READ);
    if (in == HFILE_ERROR)
        return VIF_CANNOTREADSRC;
    HFILE out = _lcreat(dest, 0);
    if (out == HFILE_ERROR) {
        _lclose(in);
        return VIF_CANNOTCREATE;
    }
    DWORD r = 0;
    HINSTANCE lz = LoadLibrary("LZEXPAND");
    LONG (WINAPI *copy)(HFILE, HFILE) = lz >= 32 ?                 /* LZCopy */
        (LONG (WINAPI *)(HFILE, HFILE))(void (*)(void))GetProcAddress(lz, MAKEINTRESOURCE(1)) :
        NULL;
    if (copy) {
        LONG n = copy(in, out);
        r = n >= 0 ? 0 : n == -4 ? VIF_OUTOFSPACE : n == -5 ? VIF_OUTOFMEMORY : VIF_CANNOTREADSRC;
    } else {
        uint8_t *buf = (uint8_t *)malloc(1024);
        r = buf ? 0 : VIF_OUTOFMEMORY;
        while (!r) {
            UINT n = _lread(in, buf, 1024);
            if (n == (UINT)HFILE_ERROR)
                r = VIF_CANNOTREADSRC;
            else if (!n)
                break;
            else if (_lwrite(out, buf, n) != n)
                r = VIF_OUTOFSPACE;
        }
        free(buf);
    }
    if (lz >= 32)
        FreeLibrary(lz);
    _lclose(in);
    _lclose(out);
    return r;
}

/* no temporary copy is left: tmp is "" */
static DWORD WINAPI VerInstallFile(UINT flags, LPCSTR src, LPCSTR dest, LPCSTR srcdir,
                                   LPCSTR destdir, LPCSTR curdir, LPSTR tmp, UINT *tmplen)
{
    char from[200], to[200];
    (void)flags;
    (void)curdir;
    join(from, sizeof(from), srcdir, src);
    join(to, sizeof(to), destdir, dest);
    DWORD r = src && dest ? install_file(from, to) : VIF_CANNOTREADSRC;
    if (tmplen) {
        if (*tmplen)
            put(tmp, *tmplen, "");
        *tmplen = 0;
    }
    return r;
}

/* ---------------------------------------------------------------- Win16 programs */

#define NE (__win_dll_ne)

/* the most bytes from far on in its segment, of n */
static uint32_t in_segment(uint32_t far, uint32_t n)
{
    uint32_t room = 0x10000u - (uint16_t)far;
    return n < room ? n : room;
}

static void put16(win_dll_call *c, uint32_t far, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    if (far >> 16)
        NE->write(c, far, b, 2);
}

static void put32(win_dll_call *c, uint32_t far, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    if (far >> 16)
        NE->write(c, far, b, 4);
}

static uint16_t get16(win_dll_call *c, uint32_t far)
{
    uint8_t b[2];
    return (far >> 16) && NE->read(c, far, b, 2) ? rd16(b) : 0;
}

/* a program's string, "" for none (0:0 or a bad one) */
static void str(win_dll_call *c, uint32_t far, char *out, uint32_t cap)
{
    if (!far || !NE->string(c, far, out, cap))
        out[0] = 0;
}

/* a resource's type or id: a number (no selector) or a string */
static LPCSTR res_name(win_dll_call *c, uint32_t far, char *buf, uint32_t cap)
{
    if (!(far >> 16))
        return MAKEINTRESOURCE((uint16_t)far);
    str(c, far, buf, cap);
    return buf;
}

/* a program's string to a buffer of its own (cap bytes) */
static void str_back(win_dll_call *c, uint32_t far, const char *s, uint32_t cap)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (n > cap)
        n = cap;
    if (!n || !(far >> 16))
        return;
    char tmp[256];
    lstrcpyn(tmp, s, (int)(n < sizeof(tmp) ? n : sizeof(tmp)));
    NE->write(c, far, tmp, (uint32_t)strlen(tmp) + 1);
}

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    char file[128], type[32], id[32];
    switch (ordinal) {
    case 2:                             /* GetFileResourceSize(file, type, id, DWORD FAR *) */
    case 6: {                           /* GetFileVersionInfoSize(file, DWORD FAR *) */
        DWORD off;
        if (ordinal == 6) {
            str(c, NE->arg32(c, 8), file, sizeof(file));
            *v = file[0] ? GetFileVersionInfoSize(file, &off) : 0;
        } else {
            str(c, NE->arg32(c, 16), file, sizeof(file));
            LPCSTR t = res_name(c, NE->arg32(c, 12), type, sizeof(type));
            LPCSTR i = res_name(c, NE->arg32(c, 8), id, sizeof(id));
            *v = file[0] ? GetFileResourceSize(file, t, i, &off) : 0;
        }
        put32(c, NE->arg32(c, 4), 0);
        return WINDLL_DWORD;
    }
    case 3:                             /* GetFileResource(file, type, id, offset, len, buf) */
    case 7: {                           /* GetFileVersionInfo(file, handle, len, buf) */
        uint32_t buf = NE->arg32(c, 4), len = in_segment(buf, NE->arg32(c, 8));
        uint8_t *tmp = (uint8_t *)malloc(len ? len : 1);
        LPCSTR t = MAKEINTRESOURCE(RT_VERSION_), i = NULL;
        BOOL ok = FALSE;
        if (ordinal == 7) {
            str(c, NE->arg32(c, 16), file, sizeof(file));
            ok = tmp && file[0] && GetFileVersionInfo(file, 0, len, tmp);
        } else {
            str(c, NE->arg32(c, 24), file, sizeof(file));
            t = res_name(c, NE->arg32(c, 20), type, sizeof(type));
            i = res_name(c, NE->arg32(c, 16), id, sizeof(id));
            ok = tmp && file[0] && GetFileResource(file, t, i, 0, len, tmp);
        }
        /* as much as the resource has */
        DWORD n = ok ? GetFileResourceN(file, t, i, 0, NULL, 0) : 0;
        if (n > len)
            n = len;
        *v = ok && NE->write(c, buf, tmp, n);
        free(tmp);
        return ordinal == 3 ? WINDLL_DWORD : WINDLL_WORD;
    }
    case 8: {                           /* VerFindFile(flags, file, windir, appdir, curdir,
                                         * UINT FAR *, destdir, UINT FAR *) */
        char name[64], windir[128], appdir[128], cur[128], dest[128];
        str(c, NE->arg32(c, 28), name, sizeof(name));
        str(c, NE->arg32(c, 24), windir, sizeof(windir));
        str(c, NE->arg32(c, 20), appdir, sizeof(appdir));
        uint32_t curlen_far = NE->arg32(c, 12), destlen_far = NE->arg32(c, 4);
        UINT curcap = get16(c, curlen_far), destcap = get16(c, destlen_far);
        UINT curlen = curcap < sizeof(cur) ? curcap : sizeof(cur);
        UINT destlen = destcap < sizeof(dest) ? destcap : sizeof(dest);
        UINT r = VerFindFile(NE->arg16(c, 32), name, windir, appdir, cur, &curlen, dest,
                             &destlen);
        if (curcap)
            str_back(c, NE->arg32(c, 16), cur, curcap);
        if (destcap)
            str_back(c, NE->arg32(c, 8), dest, destcap);
        put16(c, curlen_far, (uint16_t)curlen);
        put16(c, destlen_far, (uint16_t)destlen);
        *v = r;
        return WINDLL_WORD;
    }
    case 9: {                           /* VerInstallFile(flags, src, dest, srcdir, destdir,
                                         * curdir, LPSTR tmp, UINT FAR *) */
        char src[64], dst[64], sdir[128], ddir[128], cdir[128], tmp[2];
        str(c, NE->arg32(c, 28), src, sizeof(src));
        str(c, NE->arg32(c, 24), dst, sizeof(dst));
        str(c, NE->arg32(c, 20), sdir, sizeof(sdir));
        str(c, NE->arg32(c, 16), ddir, sizeof(ddir));
        str(c, NE->arg32(c, 12), cdir, sizeof(cdir));
        uint32_t tmplen_far = NE->arg32(c, 4);
        UINT tmpcap = get16(c, tmplen_far), tmplen = tmpcap ? 1 : 0;
        *v = VerInstallFile(NE->arg16(c, 32), src, dst, sdir, ddir, cdir, tmp, &tmplen);
        if (tmpcap)
            str_back(c, NE->arg32(c, 8), "", tmpcap);
        put16(c, tmplen_far, (uint16_t)tmplen);
        return WINDLL_DWORD;
    }
    case 10: {                          /* VerLanguageName(UINT, LPSTR, UINT) */
        char name[64];
        UINT cap = NE->arg16(c, 4);
        UINT n = VerLanguageName(NE->arg16(c, 10), name, sizeof(name));
        if (cap)
            str_back(c, NE->arg32(c, 6), name, cap);
        *v = n < cap ? n : cap ? cap - 1 : 0;
        return WINDLL_WORD;
    }
    case 11: {                          /* VerQueryValue(block, LPCSTR, LPVOID FAR *, UINT FAR *) */
        uint32_t block = NE->arg32(c, 16);
        char path[128];
        uint8_t head[2];
        *v = 0;
        if (!NE->string(c, NE->arg32(c, 12), path, sizeof(path)) ||
            !NE->read(c, block, head, 2))
            return WINDLL_WORD;
        uint32_t n = in_segment(block, rd16(head));
        uint8_t *b = (uint8_t *)malloc(n > 2 ? n : 2);
        void *val;
        UINT len;
        if (b && NE->read(c, block, b, n) && n >= 2) {
            b[0] = (uint8_t)n;          /* as much of it as is there */
            b[1] = (uint8_t)(n >> 8);
            if (VerQueryValue(b, path, &val, &len)) {
                put32(c, NE->arg32(c, 8), block + (uint32_t)((uint8_t *)val - b));
                put16(c, NE->arg32(c, 4), (uint16_t)len);
                *v = 1;
            }
        }
        free(b);
        return WINDLL_WORD;
    }
    }
    return WINDLL_NONE;
}

static const win_dll_export exports[] = {
    { 2, 16, "GetFileResourceSize", (const void *)GetFileResourceSize },
    { 3, 24, "GetFileResource", (const void *)GetFileResource },
    { 6, 8, "GetFileVersionInfoSize", (const void *)GetFileVersionInfoSize },
    { 7, 16, "GetFileVersionInfo", (const void *)GetFileVersionInfo },
    { 8, 30, "VerFindFile", (const void *)VerFindFile },
    { 9, 30, "VerInstallFile", (const void *)VerInstallFile },
    { 10, 8, "VerLanguageName", (const void *)VerLanguageName },
    { 11, 16, "VerQueryValue", (const void *)VerQueryValue },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "VER",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, NULL, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
