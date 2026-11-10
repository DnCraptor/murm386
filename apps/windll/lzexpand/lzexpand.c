/*
 * lzexpand.c - LZEXPAND.DLL, WINN's: files COMPRESS.EXE made ("SZDD"),
 * read expanded; others as they are. A native DLL (ddi/windll.h): its
 * functions for native callers, and Win16 programs' calls of them.
 *
 * COMPRESS.EXE's files: "SZDD" 88 F0 27 33, 'A', the name's last
 * character, the size expanded; then a flag byte for each 8 items (1 a
 * byte as it is, 0 two: 12 bits where in the 4K window, 4 bits the length
 * less 3), the window spaces at first, written from FF0h on.
 *
 * As Windows' LZEXPAND: one table of open compressed files for all tasks,
 * their handles 400h + n; a task's let go at its end (its files are closed
 * by then).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "windll.h"

enum { LZERROR_BADINHANDLE = -1, LZERROR_BADOUTHANDLE = -2, LZERROR_READ = -3,
       LZERROR_WRITE = -4, LZERROR_GLOBALLOC = -5, LZERROR_BADVALUE = -7,
       LZERROR_UNKNOWNALG = -8 };

#define LZ_HANDLE 0x400
#define LZ_FILES  16

typedef struct lzfile {
    HFILE f;
    HTASK task;                     /* whose */
    uint32_t size;                  /* expanded */
    uint32_t pos;                   /* the expanded bytes made so far */
    uint32_t want;                  /* where the next read starts (LZSeek's) */
    unsigned cur, flags, nflags, mpos, mlen;
    unsigned in_at, in_n;
    char last;
    uint8_t in[256];
    uint8_t win[4096];
} lzfile;

static lzfile *files[LZ_FILES];

static const uint8_t szdd[8] = { 'S', 'Z', 'D', 'D', 0x88, 0xf0, 0x27, 0x33 };

static void lz_reset(lzfile *z)
{
    _llseek(z->f, 14, 0);
    memset(z->win, ' ', sizeof(z->win));
    z->cur = 0xff0;
    z->pos = z->flags = z->nflags = z->mlen = z->in_at = z->in_n = 0;
}

static int lz_in(lzfile *z, unsigned *b)
{
    if (z->in_at == z->in_n) {
        UINT n = _lread(z->f, z->in, sizeof(z->in));
        if (n == (UINT)HFILE_ERROR || !n)
            return 0;
        z->in_at = 0;
        z->in_n = n;
    }
    *b = z->in[z->in_at++];
    return 1;
}

/* the next byte expanded; 0 at the end */
static int lz_next(lzfile *z, uint8_t *out)
{
    unsigned b, b2;
    if (z->pos >= z->size)
        return 0;
    if (!z->mlen) {
        if (!z->nflags) {
            if (!lz_in(z, &z->flags))
                return 0;
            z->nflags = 8;
        }
        unsigned bit = z->flags & 1;
        z->flags >>= 1;
        z->nflags--;
        if (bit) {
            if (!lz_in(z, &b))
                return 0;
            z->win[z->cur] = (uint8_t)b;
            z->cur = (z->cur + 1) & 0xfff;
            z->pos++;
            *out = (uint8_t)b;
            return 1;
        }
        if (!lz_in(z, &b) || !lz_in(z, &b2))
            return 0;
        z->mpos = b | (b2 & 0xf0) << 4;
        z->mlen = (b2 & 0x0f) + 3;
    }
    b = z->win[z->mpos & 0xfff];
    z->mpos++;
    z->mlen--;
    z->win[z->cur] = (uint8_t)b;
    z->cur = (z->cur + 1) & 0xfff;
    z->pos++;
    *out = (uint8_t)b;
    return 1;
}

/* n bytes from want on; the number */
static uint32_t lz_read(lzfile *z, uint8_t *buf, uint32_t n)
{
    if (z->want < z->pos)
        lz_reset(z);
    uint8_t c;
    while (z->pos < z->want && lz_next(z, &c))
        ;
    uint32_t got = 0;
    while (got < n && z->pos == z->want && lz_next(z, &buf[got])) {
        got++;
        z->want++;
    }
    return got;
}

/* f's header: an expanding state of it (f its own then), or NULL with *err
 * 0 when it is not compressed (f back at its start) */
static lzfile *lz_of(HFILE f, int *err)
{
    uint8_t h[14];
    *err = 0;
    if (_llseek(f, 0, 0) != 0) {
        *err = LZERROR_BADINHANDLE;
        return NULL;
    }
    UINT n = _lread(f, h, sizeof(h));
    if (n != sizeof(h) || memcmp(h, szdd, sizeof(szdd))) {
        _llseek(f, 0, 0);
        return NULL;
    }
    if (h[8] != 'A') {
        *err = LZERROR_UNKNOWNALG;
        return NULL;
    }
    lzfile *z = (lzfile *)calloc(1, sizeof(*z));
    if (!z) {
        *err = LZERROR_GLOBALLOC;
        return NULL;
    }
    z->f = f;
    z->task = GetCurrentTask();
    z->last = (char)h[9];
    z->size = h[10] | (uint32_t)h[11] << 8 | (uint32_t)h[12] << 16 | (uint32_t)h[13] << 24;
    lz_reset(z);
    return z;
}

static lzfile *lz_handle(HFILE h)
{
    int i = h - LZ_HANDLE;
    return i >= 0 && i < LZ_FILES ? files[i] : NULL;
}

/* ---------------------------------------------------------------- the API */

static int WINAPI LZStart(void) { return 1; }

static void WINAPI LZDone(void) { }

/* an LZ handle for a compressed file, the file's own else */
static HFILE WINAPI LZInit(HFILE f)
{
    int err;
    lzfile *z = lz_of(f, &err);
    if (!z)
        return err ? err : f;
    for (int i = 0; i < LZ_FILES; i++)
        if (!files[i]) {
            files[i] = z;
            return LZ_HANDLE + i;
        }
    free(z);
    return LZERROR_GLOBALLOC;
}

/* src's bytes (expanded) to dst: their number, or an LZERROR */
static LONG WINAPI LZCopy(HFILE src, HFILE dst)
{
    if (dst < 0)
        return LZERROR_BADOUTHANDLE;
    lzfile *z = lz_handle(src), *own = NULL;
    int err = 0;
    if (!z && src >= 0 && !(own = lz_of(src, &err)) && err)
        return err;
    if (own)
        z = own;
    else if (z)
        z->want = 0;
    uint8_t *buf = (uint8_t *)malloc(1024);
    LONG total = buf ? 0 : LZERROR_GLOBALLOC;
    while (buf) {
        uint32_t n = z ? lz_read(z, buf, 1024) : _lread(src, buf, 1024);
        if (n == (UINT)HFILE_ERROR) {
            total = LZERROR_READ;
            break;
        }
        if (!n)
            break;
        if (_lwrite(dst, buf, n) != n) {
            total = LZERROR_WRITE;
            break;
        }
        total += (LONG)n;
    }
    free(buf);
    free(own);                          /* the file stays the caller's */
    return total;
}

/* the name a compressed file has: its extension's last character '_' */
static void compressed_name(const char *name, char *out, int cap)
{
    lstrcpyn(out, name, cap);
    char *dot = NULL, *slash = NULL;
    for (char *p = out; *p; p++)
        if (*p == '.')
            dot = p;
        else if (*p == '\\')
            slash = p;
    int n = (int)strlen(out);
    if (!dot || (slash && dot < slash)) {
        if (n + 2 < cap)
            lstrcpyn(out + n, "._", cap - n);
    } else if (strlen(dot) < 4) {
        if (n + 1 < cap)
            lstrcpyn(out + n, "_", cap - n);
    } else {
        out[n - 1] = '_';
    }
}

/* OpenFile's; for reading only, the compressed one when there is no other,
 * and an LZ handle for it */
static HFILE WINAPI LZOpenFile(LPSTR name, OFSTRUCT *of, UINT mode)
{
    HFILE f = OpenFile(name, of, mode);
    int plain = !(mode & 3) && !(mode & 0xf300u);      /* reading, nothing else */
    if (f == HFILE_ERROR && plain && name) {
        char packed[128];
        compressed_name(name, packed, sizeof(packed));
        f = OpenFile(packed, of, mode);
    }
    return f != HFILE_ERROR && plain ? LZInit(f) : f;
}

static LONG WINAPI LZSeek(HFILE h, LONG off, int how)
{
    lzfile *z = lz_handle(h);
    if (!z)
        return _llseek(h, off, how);
    LONG at = how == 0 ? off : how == 1 ? (LONG)z->want + off : (LONG)z->size + off;
    if (at < 0 || (uint32_t)at > z->size || how < 0 || how > 2)
        return LZERROR_BADVALUE;
    z->want = (uint32_t)at;
    return at;
}

static int WINAPI LZRead(HFILE h, void *buf, int n)
{
    lzfile *z = lz_handle(h);
    if (n <= 0)
        return 0;
    return z ? (int)lz_read(z, (uint8_t *)buf, (uint32_t)n) : (int)(int16_t)_lread(h, buf, (UINT)n);
}

static void WINAPI LZClose(HFILE h)
{
    lzfile *z = lz_handle(h);
    if (z) {
        _lclose(z->f);
        free(z);
        files[h - LZ_HANDLE] = NULL;
    } else {
        _lclose(h);
    }
}

/* the name a compressed file had (its last character back), into out */
static int WINAPI GetExpandedName(LPCSTR src, LPSTR out)
{
    char name[128];
    lstrcpyn(name, src, sizeof(name));
    HFILE f = _lopen(name, OF_READ);
    if (f == HFILE_ERROR)
        return LZERROR_BADINHANDLE;
    int err;
    lzfile *z = lz_of(f, &err);
    _lclose(f);
    int n = (int)strlen(name);
    if (z && n && name[n - 1] == '_') {         /* the character the name lost */
        char c = z->last;
        int lower = n > 1 && name[n - 2] >= 'a' && name[n - 2] <= 'z';
        name[n - 1] = (char)(lower && c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    free(z);
    lstrcpy(out, name);
    return 1;
}

/* ---------------------------------------------------------------- Win16 programs */

#define NE (__win_dll_ne)

/* the most bytes from far on in its segment, of n */
static uint32_t in_segment(uint32_t far, uint32_t n)
{
    uint32_t room = 0x10000u - (uint16_t)far;
    return n < room ? n : room;
}

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    switch (ordinal) {
    case 1: case 8:                     /* LZCopy, CopyLZFile(HFILE src, HFILE dst) */
        *v = (uint32_t)LZCopy((int16_t)NE->arg16(c, 6), (int16_t)NE->arg16(c, 4));
        return WINDLL_DWORD;
    case 2: {                           /* LZOpenFile(LPSTR, OFSTRUCT FAR *, UINT) */
        UINT mode = NE->arg16(c, 4);
        uint32_t ofs = NE->arg32(c, 6), name_far = NE->arg32(c, 10);
        OFSTRUCT of;
        char name[128];
        memset(&of, 0, sizeof(of));
        name[0] = 0;
        if ((mode & OF_REOPEN) ? !NE->read(c, ofs, &of, sizeof(of))
                               : !NE->string(c, name_far, name, sizeof(name))) {
            *v = (uint16_t)HFILE_ERROR;
            return WINDLL_WORD;
        }
        of.szPathName[sizeof(of.szPathName) - 1] = 0;
        HFILE f = LZOpenFile(name, &of, mode);
        NE->write(c, ofs, &of, sizeof(of));
        *v = (uint16_t)f;
        return WINDLL_WORD;
    }
    case 3:                             /* LZInit(HFILE) */
        *v = (uint16_t)LZInit((int16_t)NE->arg16(c, 4));
        return WINDLL_WORD;
    case 4:                             /* LZSeek(HFILE, LONG, int) */
        *v = (uint32_t)LZSeek((int16_t)NE->arg16(c, 10), (LONG)NE->arg32(c, 6),
                              (int16_t)NE->arg16(c, 4));
        return WINDLL_DWORD;
    case 5: {                           /* LZRead(HFILE, LPSTR, int) */
        uint32_t buf = NE->arg32(c, 6), n = in_segment(buf, NE->arg16(c, 4));
        HFILE h = (int16_t)NE->arg16(c, 10);
        uint8_t *tmp = n ? (uint8_t *)malloc(n) : NULL;
        int got = 0;
        if (n && !tmp)
            got = LZERROR_GLOBALLOC;
        else if (n) {
            got = LZRead(h, tmp, (int)n);
            if (got > 0 && !NE->write(c, buf, tmp, (uint32_t)got))
                got = LZERROR_READ;
        }
        free(tmp);
        *v = (uint16_t)got;
        return WINDLL_WORD;
    }
    case 6:                             /* LZClose(HFILE) */
        LZClose((int16_t)NE->arg16(c, 4));
        *v = 0;
        return WINDLL_WORD;
    case 7:                             /* LZStart */
        *v = (uint16_t)LZStart();
        return WINDLL_WORD;
    case 9:                             /* LZDone */
        LZDone();
        *v = 0;
        return WINDLL_WORD;
    case 10: {                          /* GetExpandedName(LPSTR in, LPSTR out) */
        char name[128], out[128];
        int r = NE->string(c, NE->arg32(c, 8), name, sizeof(name)) ? GetExpandedName(name, out)
                                                                    : LZERROR_BADINHANDLE;
        if (r > 0)
            NE->write(c, NE->arg32(c, 4), out, (uint32_t)strlen(out) + 1);
        *v = (uint16_t)r;
        return WINDLL_WORD;
    }
    }
    return WINDLL_NONE;
}

/* a task's compressed files: let go (the files themselves are closed) */
static void task_end(uint16_t task)
{
    for (int i = 0; i < LZ_FILES; i++)
        if (files[i] && (uint16_t)files[i]->task == task) {
            free(files[i]);
            files[i] = NULL;
        }
}

static void term(void)
{
    for (int i = 0; i < LZ_FILES; i++) {
        free(files[i]);
        files[i] = NULL;
    }
}

static const win_dll_export exports[] = {
    { 1, 4, "LZCopy", (const void *)LZCopy },
    { 2, 10, "LZOpenFile", (const void *)LZOpenFile },
    { 3, 2, "LZInit", (const void *)LZInit },
    { 4, 8, "LZSeek", (const void *)LZSeek },
    { 5, 8, "LZRead", (const void *)LZRead },
    { 6, 2, "LZClose", (const void *)LZClose },
    { 7, 0, "LZStart", (const void *)LZStart },
    { 8, 4, "CopyLZFile", (const void *)LZCopy },
    { 9, 0, "LZDone", (const void *)LZDone },
    { 10, 8, "GetExpandedName", (const void *)GetExpandedName },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "LZEXPAND",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, term,
};

const win_dll_info *WinDllInit(void) { return &info; }
