/*
 * ne_guest_int21.c - DOS for a Win16 program: its INT 21h (and DOS3Call)
 * as its C runtime makes them (files, directories, the search, the date),
 * done with WINN's own file functions (file.c), in its task: its files are
 * closed when it ends. Its buffers and names are in its own memory, read
 * and written through its selectors and checked against them. The standard
 * handles 0-4 are the console Windows programs have none of: writes go
 * nowhere, a read gets nothing.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "wincore.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

/* DOS's error codes */
enum { E_FUNCTION = 1, E_NO_FILE = 2, E_NO_PATH = 3, E_ACCESS = 5, E_HANDLE = 6,
       E_NO_MORE = 18 };

#define STD_HANDLES 5

static int fail(ne_user *u, win_ne_dos_regs *r, uint16_t code)
{
    u->dos_error = code;
    r->carry = 1;
    r->ax = code;
    return 1;
}

static int ok(win_ne_dos_regs *r)
{
    r->carry = 0;
    return 1;
}

/* a name at sel:off */
static int name_at(win_ne_guest_api_context *ctx, uint16_t sel, uint16_t off, char *buf,
                   uint32_t cap)
{
    return win_ne_string(ctx, sel, off, buf, cap) && buf[0];
}

/* a handle of the program's files */
static int is_file(uint16_t h)
{
    return _llseek((HFILE)h, 0, 1) != HFILE_ERROR;
}

/* one of the standard handles (a file's handle may have the number, on
 * the host) */
static int is_std(uint16_t h)
{
    return h < STD_HANDLES && !is_file(h);
}

/* 3Fh, 40h: CX bytes at DS:DX, in pieces */
static int transfer(win_ne_guest_api_context *ctx, ne_user *u, win_ne_dos_regs *r, int write)
{
    uint8_t buf[256];
    if (is_std(r->bx)) {
        r->ax = write ? r->cx : 0;      /* no console */
        return ok(r);
    }
    if (!is_file(r->bx))
        return fail(u, r, E_HANDLE);
    uint32_t done = 0;
    while (done < r->cx) {
        uint32_t n = r->cx - done;
        if (n > sizeof(buf))
            n = sizeof(buf);
        uint16_t off = (uint16_t)(r->dx + done);
        UINT got;
        if (write) {
            if (!win_ne_read(ctx, r->ds, off, buf, n))
                return fail(u, r, E_ACCESS);
            got = _lwrite((HFILE)r->bx, buf, (UINT)n);
        } else {
            got = _lread((HFILE)r->bx, buf, (UINT)n);
            if (got != (UINT)HFILE_ERROR && got && !win_ne_write(ctx, r->ds, off, buf, got))
                return fail(u, r, E_ACCESS);
        }
        if (got == (UINT)HFILE_ERROR)
            return fail(u, r, E_ACCESS);
        done += got;
        if (got < n)
            break;
    }
    r->ax = (uint16_t)done;
    return ok(r);
}

/* the DTA: the PSP's 80h until the program sets one */
static uint32_t dta(win_ne_guest_api_context *ctx, ne_user *u)
{
    return u->dta ? u->dta : (uint32_t)ctx->image->psp.selector << 16 | 0x80u;
}

/* 4Eh, 4Fh: the search's next entry into the DTA (name at 30, attribute
 * at 21, time, date, size from 22); the entries are looked up anew each
 * time, so no search lives across the program's turns */
static int find(win_ne_guest_api_context *ctx, ne_user *u, win_ne_dos_regs *r)
{
    int want = u->find.next + 1;
    WINFIND_N *all = (WINFIND_N *)win_alloc((uint32_t)want * sizeof(WINFIND_N));
    if (!all)
        return fail(u, r, E_NO_MORE);
    int n = WinFindFilesN(u->find.spec, u->find.attr, all, want);
    if (n < want) {
        win_free(all);
        return fail(u, r, u->find.next ? E_NO_MORE : E_NO_FILE);
    }
    const WINFIND_N *f = &all[want - 1];
    uint8_t d[43];
    memset(d, 0, sizeof(d));
    d[21] = f->attr;
    wr16(d + 22, f->time);
    wr16(d + 24, f->date);
    wr32(d + 26, f->size);
    memcpy(d + 30, f->name, 12);
    win_free(all);
    uint32_t to = dta(ctx, u);
    if (!win_ne_write(ctx, (uint16_t)(to >> 16), (uint16_t)to, d, sizeof(d)))
        return fail(u, r, E_ACCESS);
    u->find.next = want;
    return ok(r);
}

/* 47h: the current directory of drive DL (0 the current one), without the
 * drive and the first backslash, into DS:SI (64 bytes) */
static int getcwd_(win_ne_guest_api_context *ctx, ne_user *u, win_ne_dos_regs *r)
{
    char cwd[80];
    if ((r->dx & 0xff) && (r->dx & 0xff) - 1 != win_file_drive(-1))
        return fail(u, r, 15);          /* only the current drive's */
    if (GetCurrentDirectoryN(cwd, sizeof(cwd)) < 3)
        return fail(u, r, E_NO_PATH);
    const char *p = cwd + 3;
    uint32_t n = (uint32_t)strlen(p) + 1;
    if (n > 64 || !win_ne_write(ctx, r->ds, r->si, p, n))
        return fail(u, r, E_ACCESS);
    r->ax = 0x0100;
    return ok(r);
}

static __attribute__((noinline)) int paths(win_ne_guest_api_context *ctx, ne_user *u,
                                           win_ne_dos_regs *r)
{
    char path[128], to[128];
    uint8_t ah = (uint8_t)(r->ax >> 8);
    if (!name_at(ctx, r->ds, r->dx, path, sizeof(path)))
        return fail(u, r, E_NO_PATH);
    switch (ah) {
    case 0x39:                          /* mkdir */
        return CreateDirectoryN(path) ? ok(r) : fail(u, r, E_ACCESS);
    case 0x3a:                          /* rmdir */
        return RemoveDirectoryN(path) ? ok(r) : fail(u, r, E_ACCESS);
    case 0x3b:                          /* chdir */
        return win_file_chdir(path) ? ok(r) : fail(u, r, E_NO_PATH);
    case 0x3c:                          /* create (truncate) */
    case 0x5b: {                        /* create new */
        HFILE f;
        if (ah == 0x5b && (f = _lopen(path, OF_READ)) != HFILE_ERROR) {
            _lclose(f);
            return fail(u, r, 80);      /* the file exists */
        }
        f = _lcreat(path, r->cx);
        if (f == HFILE_ERROR)
            return fail(u, r, E_ACCESS);
        if (r->cx & 0x01)
            win_file_attr(path, 1, r->cx & 0x27);
        r->ax = (uint16_t)f;
        return ok(r);
    }
    case 0x3d: {                        /* open, AL: the access (sharing ignored) */
        HFILE f = _lopen(path, r->ax & 3);
        if (f == HFILE_ERROR)
            return fail(u, r, win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS);
        r->ax = (uint16_t)f;
        return ok(r);
    }
    case 0x41:                          /* delete */
        return DeleteFileN(path) ? ok(r) :
               fail(u, r, win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS);
    case 0x43: {                        /* attributes: AL 0 get (CX), 1 set */
        int at = win_file_attr(path, (r->ax & 0xff) == 1, r->cx);
        if (at < 0)
            return fail(u, r, E_NO_FILE);
        r->cx = (uint16_t)at;
        return ok(r);
    }
    case 0x4e:                          /* find first: CX the attributes */
        lstrcpyn(u->find.spec, path, sizeof(u->find.spec));
        u->find.attr = r->cx;
        u->find.next = 0;
        return find(ctx, u, r);
    case 0x56:                          /* rename DS:DX to ES:DI */
        if (!name_at(ctx, r->es, r->di, to, sizeof(to)))
            return fail(u, r, E_NO_PATH);
        return MoveFileN(path, to) ? ok(r) :
               fail(u, r, win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS);
    }
    return 0;
}

int win_ne_dos(win_ne_guest_api_context *ctx, win_ne_dos_regs *r)
{
    ne_user *u = ctx ? (ne_user *)ctx->user : NULL;
    if (!u)
        return 0;
    uint8_t ah = (uint8_t)(r->ax >> 8), al = (uint8_t)r->ax;
    switch (ah) {
    case 0x0e:                          /* select drive DL: AL the number of drives */
        r->ax = (uint16_t)((r->ax & 0xff00) | (uint8_t)win_file_drive(r->dx & 0xff));
        return 1;
    case 0x19:                          /* the current drive */
        r->ax = (uint16_t)((r->ax & 0xff00) | (uint8_t)win_file_drive(-1));
        return 1;
    case 0x1a:                          /* the DTA: DS:DX */
        u->dta = (uint32_t)r->ds << 16 | r->dx;
        return 1;
    case 0x2f: {                        /* the DTA: ES:BX */
        uint32_t d = dta(ctx, u);
        r->es = (uint16_t)(d >> 16);
        r->bx = (uint16_t)d;
        return 1;
    }
    case 0x2a: case 0x2c: {             /* the date, the time */
        SYSTEMTIME_N t;
        GetLocalTimeN(&t);
        if (ah == 0x2a) {
            r->cx = t.wYear;
            r->dx = (uint16_t)(t.wMonth << 8 | t.wDay);
            r->ax = (uint16_t)(r->ax & 0xff00) | (uint8_t)t.wDayOfWeek;
        } else {
            r->cx = (uint16_t)(t.wHour << 8 | t.wMinute);
            r->dx = (uint16_t)(t.wSecond << 8 | t.wMilliseconds / 10);
        }
        return 1;
    }
    case 0x25:                          /* its own interrupt handlers: none */
        return 1;
    case 0x35:                          /* an interrupt's handler: none */
        r->es = r->bx = 0;
        return 1;
    case 0x33:                          /* Ctrl-Break checking: off */
        if (al == 0)
            r->dx = (uint16_t)(r->dx & 0xff00);
        return 1;
    case 0x36: {                        /* free space of drive DL */
        DWORD free_b, total;
        if (!GetDiskFreeSpaceN(r->dx & 0xff, &free_b, &total)) {
            r->ax = 0xffff;
            return 1;
        }
        r->ax = 8;                      /* sectors per cluster: 4K clusters */
        r->cx = 512;
        r->bx = (uint16_t)(free_b / 4096u > 0xffffu ? 0xffffu : free_b / 4096u);
        r->dx = (uint16_t)(total / 4096u > 0xffffu ? 0xffffu : total / 4096u);
        return 1;
    }
    case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x41: case 0x43:
    case 0x4e: case 0x56: case 0x5b:
        return paths(ctx, u, r);
    case 0x3e:                          /* close */
        if (is_std(r->bx))
            return ok(r);
        return _lclose((HFILE)r->bx) != HFILE_ERROR ? ok(r) : fail(u, r, E_HANDLE);
    case 0x3f:
        return transfer(ctx, u, r, 0);
    case 0x40:
        return transfer(ctx, u, r, 1);
    case 0x42: {                        /* seek: AL the origin, CX:DX; DX:AX */
        if (is_std(r->bx)) {
            r->ax = r->dx = 0;
            return ok(r);
        }
        LONG at = al > 2 ? HFILE_ERROR :
                  _llseek((HFILE)r->bx, (LONG)((uint32_t)r->cx << 16 | r->dx), al);
        if (at == HFILE_ERROR)
            return fail(u, r, al > 2 ? E_FUNCTION : E_HANDLE);
        r->ax = (uint16_t)at;
        r->dx = (uint16_t)((uint32_t)at >> 16);
        return ok(r);
    }
    case 0x44:                          /* IOCTL */
        if (al == 0) {                  /* device information */
            if (is_std(r->bx))
                r->dx = r->bx == 0 ? 0x80d1 : r->bx == 1 || r->bx == 2 ? 0x80d2 : 0x80c0;
            else if (is_file(r->bx))
                r->dx = (uint16_t)win_file_drive(-1);   /* a file on the drive */
            else
                return fail(u, r, E_HANDLE);
            r->ax = r->dx;
            return ok(r);
        }
        if (al == 1)                    /* set: nothing to set */
            return ok(r);
        if (al == 6 || al == 7) {       /* input, output status: ready */
            r->ax = (uint16_t)(r->ax & 0xff00) | 0xff;
            return ok(r);
        }
        return 0;
    case 0x47:
        return getcwd_(ctx, u, r);
    case 0x4f:                          /* find next */
        if (!u->find.spec[0])
            return fail(u, r, E_NO_MORE);
        return find(ctx, u, r);
    case 0x51: case 0x62:               /* its PSP */
        r->bx = ctx->image->psp.selector;
        return 1;
    case 0x57: {                        /* a file's date and time: AL 0 get, 1 set */
        WORD date = r->dx, time_ = r->cx;
        if (al > 1 || !win_file_time((HFILE)r->bx, al, &date, &time_))
            return fail(u, r, al > 1 ? E_FUNCTION : E_HANDLE);
        r->cx = time_;
        r->dx = date;
        return ok(r);
    }
    case 0x59:                          /* the last error: code, class, action, locus */
        r->ax = u->dos_error;
        r->bx = 0x0d01;                 /* class "not found"/action retry */
        r->cx = (uint16_t)(r->cx & 0xff) | 0x0200;     /* locus: block device */
        return 1;
    case 0x67:                          /* the number of handles: as it is */
        return ok(r);
    case 0x68:                          /* commit: written already */
        return is_std(r->bx) || is_file(r->bx) ? ok(r) : fail(u, r, E_HANDLE);
    }
    return 0;
}

/* ---------------------------------------------------------------- KERNEL and KEYBOARD */

/* a name made full: the current directory's, with the drive, upper case */
static void full_path(const char *name, char *out, int cap)
{
    char cwd[128];
    if (name[0] && name[1] == ':') {
        lstrcpyn(out, name, cap);
    } else if (name[0] == '\\' || name[0] == '/') {
        GetCurrentDirectoryN(cwd, sizeof(cwd));
        cwd[2] = 0;
        wsprintf(out, "%s%s", cwd, name);
    } else {
        int n = GetCurrentDirectoryN(cwd, sizeof(cwd));
        wsprintf(out, n && cwd[n - 1] == '\\' ? "%s%s" : "%s\\%s", cwd, name);
    }
    for (char *p = out; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
        else if (*p == '/')
            *p = '\\';
}

/* KERNEL.74 OpenFile(LPCSTR, OFSTRUCT FAR *, UINT): the file opened
 * (created, only looked for, deleted, its name only made full), found in
 * the current directory or the Windows one; the OFSTRUCT (cBytes,
 * fFixedDisk, nErrCode, 4 reserved, szPathName[128]) filled */
enum { OF_PARSE_ = 0x100, OF_DELETE_ = 0x200, OF_CREATE_ = 0x1000, OF_EXIST_ = 0x4000,
       OF_REOPEN_ = 0x8000 };

static __attribute__((noinline)) uint16_t open_file(args *a)
{
    UINT mode = w(a, 4);
    uint32_t ofs = dw(a, 6), name_far = dw(a, 10);
    uint8_t of[136];
    char name[128], path[128];
    memset(of, 0, sizeof(of));
    if (a->bad)
        return (uint16_t)HFILE_ERROR;
    if (mode & OF_REOPEN_) {
        if (!win_ne_read(a->ctx, (uint16_t)(ofs >> 16), (uint16_t)ofs, of, sizeof(of)))
            return a->bad = 1, (uint16_t)HFILE_ERROR;
        lstrcpyn(name, (const char *)of + 8, sizeof(name));
    } else if (!win_ne_string(a->ctx, (uint16_t)(name_far >> 16), (uint16_t)name_far, name,
                              sizeof(name))) {
        return a->bad = 1, (uint16_t)HFILE_ERROR;
    }
    full_path(name, path, sizeof(path));
    HFILE f = HFILE_ERROR;
    uint16_t err = 0;
    if (mode & OF_PARSE_) {
        f = 0;
    } else if (mode & OF_DELETE_) {
        f = DeleteFileN(path) ? 1 : HFILE_ERROR;
        err = E_NO_FILE;
    } else if (mode & OF_CREATE_) {
        f = _lcreat(path, 0);
        if (f != HFILE_ERROR && (mode & 3) != OF_WRITE) {   /* reopened for reading too */
            _lclose(f);
            f = _lopen(path, mode & 3);
        }
        err = E_ACCESS;
    } else {
        f = _lopen(path, mode & 3);
        if (f == HFILE_ERROR && !(name[0] && name[1] == ':') && name[0] != '\\') {
            char win[128];                  /* not here: in the Windows directory */
            UINT n = GetWindowsDirectory(win, sizeof(win));
            if (n && n < sizeof(win) - 14) {
                wsprintf(path, win[n - 1] == '\\' ? "%s%s" : "%s\\%s", win, name);
                full_path(path, path, sizeof(path));
                f = _lopen(path, mode & 3);
            }
        }
        err = win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS;
        if (f != HFILE_ERROR && (mode & OF_EXIST_)) {
            WORD date = 0, time_ = 0;
            win_file_time(f, 0, &date, &time_);
            wr16(of + 4, date);
            wr16(of + 6, time_);
            _lclose(f);
        }
    }
    of[0] = (uint8_t)sizeof(of);
    of[1] = path[0] >= 'C';                 /* a fixed disk */
    wr16(of + 2, f == HFILE_ERROR ? err : 0);
    lstrcpyn((char *)of + 8, path, 128);
    if (!win_ne_write(a->ctx, (uint16_t)(ofs >> 16), (uint16_t)ofs, of, sizeof(of)))
        a->bad = 1;
    return (uint16_t)f;
}

/* KERNEL.131 GetDOSEnvironment(): WINN's DOS environment, in a block of
 * the program's own (its PSP's 2Ch too) */
static uint32_t dos_environment(args *a, ne_user *u)
{
    if (!u->env) {
        char *env = (char *)win_alloc(4096);
        int n = env ? win_file_environment(env, 4096) : 0;
        if (n <= 0 && env) {
            env[0] = env[1] = 0;
            n = 2;
        }
        win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
        uint16_t sel = env ? win_ne_guest_global_alloc(g, (uint32_t)n) : 0;
        if (sel && win_ne_write(a->ctx, sel, 0, env, (uint32_t)n)) {
            uint8_t w2[2];
            wr16(w2, sel);
            win_ne_write(a->ctx, g->psp.selector, 0x2c, w2, 2);
            u->env = sel;
        }
        win_free(env);
    }
    return (uint32_t)u->env << 16;
}

/* KEYBOARD's AnsiToOem/OemToAnsi: n bytes (0: up to the NUL) from src to
 * dst, in pieces */
static void convert(args *a, uint32_t src, uint32_t dst, uint32_t n, int to_oem)
{
    uint8_t buf[128];
    int nul = !n;
    if (nul)
        n = 0x10000u;
    for (uint32_t done = 0; done < n && !a->bad; ) {
        uint32_t k = n - done < sizeof(buf) ? n - done : sizeof(buf);
        uint16_t so = (uint16_t)((uint16_t)src + done), d = (uint16_t)((uint16_t)dst + done);
        if (nul) {                          /* a string: byte by byte up to the NUL */
            k = 1;
        }
        if (!win_ne_read(a->ctx, (uint16_t)(src >> 16), so, buf, k)) {
            a->bad = 1;
            break;
        }
        for (uint32_t i = 0; i < k; i++)
            buf[i] = to_oem ? win_ansi_to_oem(buf[i]) : win_oem_to_ansi(buf[i]);
        if (!win_ne_write(a->ctx, (uint16_t)(dst >> 16), d, buf, k)) {
            a->bad = 1;
            break;
        }
        done += k;
        if (nul && !buf[0])
            break;
    }
}

int ne_dos_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    switch (service) {
    case 74:                            /* OpenFile */
        *v = open_file(a);
        return 1;
    case 131:                           /* GetDOSEnvironment */
        *v = dos_environment(a, u);
        *dword = 1;
        return 1;
    case 207:                           /* IsDBCSLeadByte: no double-byte characters */
        *v = 0;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 5:   /* AnsiToOem(LPCSTR, LPSTR) */
    case WIN_NE_SERVICE_KEYBOARD + 6:   /* OemToAnsi */
        convert(a, dw(a, 8), dw(a, 4), 0, service == WIN_NE_SERVICE_KEYBOARD + 5);
        *v = 0xffff;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 134: /* AnsiToOemBuff(LPCSTR, LPSTR, UINT) */
    case WIN_NE_SERVICE_KEYBOARD + 135: { /* OemToAnsiBuff */
        uint32_t n = w(a, 4);
        convert(a, dw(a, 10), dw(a, 6), n ? n : 0x10000u, service == WIN_NE_SERVICE_KEYBOARD + 134);
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 130: { /* GetKeyboardType(int): an enhanced one */
        int16_t which = (int16_t)w(a, 4);
        *v = which == 0 ? 4 : which == 2 ? 12 : 0;
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 132: /* GetKBCodePage: the OEM code page */
        *v = 866;
        return 1;
    }
    return 0;
}

#endif
