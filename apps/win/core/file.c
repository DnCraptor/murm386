/*
 * file.c - KERNEL file I/O (_lopen & co.), GetDriveType, and the native
 * directory, file and disk functions of module WINN.
 *
 * Files belong to the task that opened them and are closed when it ends,
 * as the per-task PSP does in Windows 3.x. The host build maps everything
 * to POSIX calls.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#if !(defined(__arm__) || defined(__thumb__))
#define _GNU_SOURCE 1         /* FNM_CASEFOLD */
#endif
#include <string.h>
#include "wincore.h"

#if defined(__arm__) || defined(__thumb__)
#include <fcntl.h>
#include <io.h>
#include <dos.h>
#include <dos_guest_ptr.h>
#define F_RDWR 2
#else
#define FILE_HOST 1
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <time.h>
#define O_BINARY 0
#define F_RDWR O_RDWR
#endif

#define NFILES 16

static struct { int fd; HTASK task; } files[NFILES];

static HFILE track(int fd)
{
    if (fd < 0)
        return HFILE_ERROR;
    for (int i = 0; i < NFILES; i++)
        if (!files[i].task) {
            files[i].fd = fd;
            files[i].task = GetCurrentTask();
            return (HFILE)fd;
        }
    close(fd);                      /* no slot: too many open files */
    return HFILE_ERROR;
}

static int tracked(HFILE f)
{
    for (int i = 0; i < NFILES; i++)
        if (files[i].task && files[i].fd == (int)f)
            return i;
    return -1;
}

#ifdef FILE_HOST
/* host tests: the current directory of drive C: below the test directory
 * ("" its root, else "DIR/SUB") */
static char host_cwd[128];

/* host tests: DOS paths to Linux ones; drive C: is the test directory */
static const char *host_path_to(const char *p, char *buf, int size)
{
    if (p[0] && p[1] == ':')
        p += 2;
    if (*p == '\\' || *p == '/') {
        lstrcpyn(buf, ".", size);
        lstrcpyn(buf + 1, p, size - 1);
    } else if (host_cwd[0]) {
        snprintf(buf, (size_t)size, "%s/%s", host_cwd, p);
    } else {
        lstrcpyn(buf, p, size);
    }
    for (char *q = buf; *q; q++)
        if (*q == '\\')
            *q = '/';
    return buf;
}

static const char *host_path(const char *p)
{
    static char buf[256];
    return host_path_to(p, buf, sizeof(buf));
}
#define open(p, ...) open(host_path(p), __VA_ARGS__)
#endif

HFILE WINAPI _lopen(LPCSTR path, int mode)
{
    int flags = (mode & 3) == OF_WRITE ? O_WRONLY : (mode & 3) == OF_READWRITE ? F_RDWR : O_RDONLY;
    return path ? track(open(path, flags | O_BINARY)) : HFILE_ERROR;
}

HFILE WINAPI _lcreat(LPCSTR path, int attr)
{
    (void)attr;
#ifdef FILE_HOST
    return path ? track(open(path, O_RDWR | O_CREAT | O_TRUNC, 0644)) : HFILE_ERROR;
#else
    return path ? track(open(path, O_WRONLY | O_CREAT | O_TRUNC)) : HFILE_ERROR;
#endif
}

HFILE WINAPI _lclose(HFILE f)
{
    int i = tracked(f);
    if (i < 0)
        return HFILE_ERROR;
    files[i].task = 0;
    return close(files[i].fd) ? HFILE_ERROR : 0;
}

LONG WINAPI _hread(HFILE f, void *buf, LONG n)
{
    if (tracked(f) < 0 || n < 0)
        return HFILE_ERROR;
    int r = (int)read((int)f, buf, (unsigned)n);
    return r < 0 ? HFILE_ERROR : r;
}

LONG WINAPI _hwrite(HFILE f, const void *buf, LONG n)
{
    if (tracked(f) < 0 || n < 0)
        return HFILE_ERROR;
    int r = (int)write((int)f, buf, (unsigned)n);
    return r < 0 ? HFILE_ERROR : r;
}

UINT WINAPI _lread(HFILE f, void *buf, UINT n) { return (UINT)_hread(f, buf, (LONG)n); }
UINT WINAPI _lwrite(HFILE f, const void *buf, UINT n) { return (UINT)_hwrite(f, buf, (LONG)n); }

LONG WINAPI _llseek(HFILE f, LONG off, int origin)
{
    if (tracked(f) < 0)
        return HFILE_ERROR;
    LONG r = (LONG)lseek((int)f, off, origin);
    return r < 0 ? HFILE_ERROR : r;
}

BOOL win_file_save(const char *path, const void *p, uint32_t size)
{
    HFILE f = path ? _lcreat(path, 0) : HFILE_ERROR;
    if (f == HFILE_ERROR)
        return FALSE;
    BOOL ok = !size || _hwrite(f, p, (LONG)size) == (LONG)size;
    return _lclose(f) == 0 && ok;
}

uint8_t *win_file_load(const char *path, uint32_t *size, uint32_t max)
{
    HFILE f = path ? _lopen(path, OF_READ) : HFILE_ERROR;
    if (f == HFILE_ERROR)
        return NULL;
    LONG n = _llseek(f, 0, 2);
    uint8_t *b = n > 0 && (uint32_t)n <= max ? (uint8_t *)win_alloc((size_t)n) : NULL;
    if (b && (_llseek(f, 0, 0) != 0 || _hread(f, b, n) != n)) {
        win_free(b);
        b = NULL;
    }
    _lclose(f);
    if (b)
        *size = (uint32_t)n;
    return b;
}

/* a task ended: the files it left open are closed */
void win_files_task_ended(HTASK task)
{
    for (int i = 0; i < NFILES; i++)
        if (files[i].task == task) {
            files[i].task = 0;
            close(files[i].fd);
        }
}

/* ---------------------------------------------------------------- WINN */

static int add_name(LPSTR buf, int size, int *len, const char *name)
{
    int n = lstrlen(name);
    if (*len + n + 2 > size)
        return 0;
    memcpy(buf + *len, name, (size_t)n + 1);
    *len += n + 1;
    return 1;
}

#ifdef FILE_HOST
/* "C:\\DIR\\*.EXE" -> the Linux directory in dir, the pattern returned */
static const char *host_split(LPCSTR spec, char *dir, int size)
{
    const char *pat = spec;
    for (const char *p = spec; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            pat = p + 1;
    int dn = (int)(pat - spec);
    char d[256];
    if (dn >= (int)sizeof(d))
        return NULL;
    memcpy(d, spec, (size_t)dn);
    d[dn] = 0;
    host_path_to(d, dir, size);
    if (!dir[0])
        lstrcpyn(dir, ".", size);
    return pat;
}

/* DOS wildcard match: "NAME.*" also matches "NAME" (no extension) */
static int host_match(const char *pat, const char *name)
{
    int miss = fnmatch(pat, name, FNM_CASEFOLD);
    int pl = (int)strlen(pat);
    if (miss && pl >= 2 && !strcmp(pat + pl - 2, ".*") && !strchr(name, '.')) {
        char p2[64];
        lstrcpyn(p2, pat, pl - 1 < (int)sizeof(p2) ? pl - 1 : (int)sizeof(p2));
        miss = fnmatch(p2, name, FNM_CASEFOLD);
    }
    return !miss;
}
#endif

/* Names of the entries matching spec ("C:\\WINN\\*.EXE"), NUL-separated with
 * a second NUL at the end; returns how many. attr 0: files only;
 * WIN_DIR_DIRS (0x10): subdirectories only ("." and ".." left out). The
 * whole search is done in one call, so no search state lives across task
 * switches. */
int WINAPI WinDirListAttrN(LPCSTR spec, UINT attr, LPSTR buf, int size)
{
    if (!spec || !buf || size < 2)
        return 0;
    int dirs = (attr & 0x10) != 0;
    int len = 0, count = 0;
#ifdef FILE_HOST
    char dir[256];
    const char *pat = host_split(spec, dir, sizeof(dir));
    if (!pat)
        return 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            if (!host_match(pat, e->d_name))
                continue;
            char full[512];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (stat(full, &st) || (S_ISDIR(st.st_mode) != 0) != dirs)
                continue;
            if (!add_name(buf, size, &len, e->d_name))
                break;
            count++;
        }
        closedir(d);
    }
#else
    static struct find_t ft;        /* big, keep it off the task stack */
    for (int r = _dos_findfirst(spec, dirs ? 0x10 : 0, &ft); !r; r = _dos_findnext(&ft)) {
        if (((ft.attrib & 0x10) != 0) != dirs || ft.name[0] == '.')
            continue;
        if (!add_name(buf, size, &len, ft.name))
            break;
        count++;
    }
#endif
    buf[len] = 0;
    return count;
}

int WINAPI WinDirListN(LPCSTR spec, LPSTR buf, int size)
{
    return WinDirListAttrN(spec, 0, buf, size);
}

/* Is the file a Windows application (an EZ image built with the SDK)? */
BOOL WINAPI IsWinAppN(LPCSTR path)
{
#ifdef FILE_HOST
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    struct stat st;
    return win_host_app(base) != NULL && !stat(host_path(path), &st);
#else
    return win_ez_is_app(path);
#endif
}

/* ---------------------------------------------------------------- files and disks */

#ifndef FILE_HOST
/* DOS reads paths and writes the DTA in guest memory: a block of its own,
 * allocated on first use. */
#define DB_DTA    0u
#define DB_PATH1  128u
#define DB_PATH2  320u
#define DB_INT24  512u
#define DB_SIZE   528u
#define DB_PATHMAX 192

static uint16_t db_seg;
static unsigned char *db_mem;

static int dos_block(void)
{
    if (db_mem)
        return 1;
    union REGS r = { 0 };
    r.h.ah = 0x48;
    r.w.bx = (uint16_t)(DB_SIZE / 16u);
    int386(0x21, &r, &r);
    if (r.x.cflag)
        return 0;
    db_seg = r.w.ax;
    db_mem = (unsigned char *)dos_guest_far_ptr(db_seg, 0);
    return 1;
}

/* INT 21h with DS and ES on the block (or on seg); true without carry */
static int dos_call_seg(union REGS *r, uint16_t seg)
{
    struct SREGS s;
    segread(&s);
    s.ds = s.es = seg;
    int386x(0x21, r, r, &s);
    return !r->x.cflag;
}

static int dos_call(union REGS *r) { return dos_call_seg(r, db_seg); }

static int dos_path(unsigned ofs, LPCSTR p)
{
    int n = p ? lstrlen(p) : -1;
    if (n < 0 || n >= DB_PATHMAX || !dos_block())
        return 0;
    memcpy(db_mem + ofs, p, (size_t)n + 1);
    return 1;
}

static BOOL dos_path_op(UINT ah, LPCSTR path)
{
    if (!dos_path(DB_PATH1, path))
        return FALSE;
    union REGS r = { 0 };
    r.h.ah = (uint8_t)ah;
    r.w.dx = DB_PATH1;
    return dos_call(&r);
}
#endif

/* DOS critical errors (a drive without a disk) fail the call instead of
 * asking "Abort, Retry, Fail?" on a screen in graphics mode: INT 24h points
 * at "mov al,3 / iret" in the block. DOS puts the old vector back from the
 * PSP when WINN.EXE ends. */
void win_files_init(void)
{
#ifndef FILE_HOST
    if (!dos_block())
        return;
    db_mem[DB_INT24] = 0xB0;
    db_mem[DB_INT24 + 1] = 0x03;
    db_mem[DB_INT24 + 2] = 0xCF;
    union REGS r = { 0 };
    r.w.ax = 0x2524;
    r.w.dx = DB_INT24;
    dos_call(&r);
#endif
}

/* The entries matching spec ("C:\\WINN\\*.*") with their attributes, sizes
 * and times, "." and ".." left out; attr as for DOS (0x10 adds
 * directories, 0x02 hidden, 0x04 system files). Returns how many were
 * stored, at most max. */
int WINAPI WinFindFilesN(LPCSTR spec, UINT attr, WINFIND_N *out, int max)
{
    if (!spec || !out || max <= 0)
        return 0;
    int n = 0;
#ifdef FILE_HOST
    char dir[256];
    const char *pat = host_split(spec, dir, sizeof(dir));
    DIR *d = pat ? opendir(dir) : NULL;
    if (!d)
        return 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 12 || !host_match(pat, e->d_name))
            continue;
        char full[512];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (stat(full, &st))
            continue;
        int isdir = S_ISDIR(st.st_mode) != 0;
        if (isdir && !(attr & 0x10))
            continue;
        WINFIND_N *f = &out[n++];
        memset(f, 0, sizeof(*f));
        for (int i = 0; e->d_name[i] && i < 12; i++) {
            char c = e->d_name[i];
            f->name[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
        }
        f->attr = (BYTE)(isdir ? 0x10 : 0x20);
        if (!(st.st_mode & 0200))
            f->attr |= 0x01;
        f->size = isdir ? 0 : (DWORD)st.st_size;
        struct tm *t = localtime(&st.st_mtime);
        if (t && t->tm_year >= 80) {
            f->date = (WORD)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
            f->time = (WORD)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
        }
    }
    closedir(d);
#else
    if (!dos_path(DB_PATH1, spec))
        return 0;
    union REGS r = { 0 };
    struct SREGS s;
    r.h.ah = 0x2F;                          /* the DTA in use, put back after */
    segread(&s);
    int386x(0x21, &r, &r, &s);
    uint16_t old_seg = s.es, old_ofs = r.w.bx;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x1A;
    r.w.dx = DB_DTA;
    dos_call(&r);
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x4E;
    r.w.cx = (uint16_t)(attr & 0x16);
    r.w.dx = DB_PATH1;
    for (int ok = dos_call(&r); ok && n < max; ) {
        const unsigned char *dta = db_mem + DB_DTA;
        if (dta[30] != '.') {
            WINFIND_N *f = &out[n++];
            memset(f, 0, sizeof(*f));
            for (int i = 0; i < 12 && dta[30 + i]; i++)
                f->name[i] = (char)dta[30 + i];
            f->attr = dta[21];
            f->time = (WORD)(dta[22] | (dta[23] << 8));
            f->date = (WORD)(dta[24] | (dta[25] << 8));
            f->size = (DWORD)dta[26] | ((DWORD)dta[27] << 8) | ((DWORD)dta[28] << 16) |
                      ((DWORD)dta[29] << 24);
        }
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x4F;
        ok = dos_call(&r);
    }
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x1A;
    r.w.dx = old_ofs;
    dos_call_seg(&r, old_seg);
#endif
    return n;
}

BOOL WINAPI DeleteFileN(LPCSTR path)
{
#ifdef FILE_HOST
    return path && !unlink(host_path(path));
#else
    return dos_path_op(0x41, path);
#endif
}

/* Renames or moves a file or directory within one drive. */
BOOL WINAPI MoveFileN(LPCSTR from, LPCSTR to)
{
#ifdef FILE_HOST
    char a[256], b[256];
    return from && to && !rename(host_path_to(from, a, sizeof(a)), host_path_to(to, b, sizeof(b)));
#else
    if (!dos_path(DB_PATH1, from) || !dos_path(DB_PATH2, to))
        return FALSE;
    union REGS r = { 0 };
    r.h.ah = 0x56;
    r.w.dx = DB_PATH1;
    r.w.di = DB_PATH2;
    return dos_call(&r);
#endif
}

BOOL WINAPI CreateDirectoryN(LPCSTR path)
{
#ifdef FILE_HOST
    return path && !mkdir(host_path(path), 0755);
#else
    return dos_path_op(0x39, path);
#endif
}

BOOL WINAPI RemoveDirectoryN(LPCSTR path)
{
#ifdef FILE_HOST
    return path && !rmdir(host_path(path));
#else
    return dos_path_op(0x3A, path);
#endif
}

/* Free and total bytes of a drive (0 the current one, 1 A:, ...). */
BOOL WINAPI GetDiskFreeSpaceN(int drive, DWORD *free_bytes, DWORD *total_bytes)
{
#ifdef FILE_HOST
    struct statvfs v;
    if (drive != 0 && drive != 3)
        return FALSE;
    if (statvfs(".", &v))
        return FALSE;
    unsigned long long f = (unsigned long long)v.f_bavail * v.f_frsize;
    unsigned long long t = (unsigned long long)v.f_blocks * v.f_frsize;
    if (free_bytes) *free_bytes = f > 0xFFFFFFFFull ? 0xFFFFFFFFu : (DWORD)f;
    if (total_bytes) *total_bytes = t > 0xFFFFFFFFull ? 0xFFFFFFFFu : (DWORD)t;
    return TRUE;
#else
    union REGS r = { 0 };
    r.h.ah = 0x36;
    r.h.dl = (uint8_t)drive;
    int386(0x21, &r, &r);
    if (r.w.ax == 0xFFFF)
        return FALSE;
    DWORD cluster = (DWORD)r.w.ax * r.w.cx;
    if (free_bytes) *free_bytes = cluster * r.w.bx;
    if (total_bytes) *total_bytes = cluster * r.w.dx;
    return TRUE;
#endif
}

/* The current directory with its drive: "C:\\WINN". */
int WINAPI GetCurrentDirectoryN(LPSTR buf, int size)
{
    if (!buf || size < 4)
        return 0;
#ifdef FILE_HOST
    lstrcpyn(buf, "C:\\", size);
    lstrcpyn(buf + 3, host_cwd, size - 3);
    for (char *q = buf; *q; q++)
        if (*q == '/')
            *q = '\\';
#else
    union REGS r = { 0 };
    r.h.ah = 0x19;
    int386(0x21, &r, &r);
    int drive = r.h.al;
    buf[0] = (char)('A' + drive);
    buf[1] = ':';
    buf[2] = '\\';
    buf[3] = 0;
    if (dos_block()) {
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x47;
        r.h.dl = (uint8_t)(drive + 1);
        r.w.si = DB_PATH1;
        if (dos_call(&r))
            lstrcpyn(buf + 3, (const char *)db_mem + DB_PATH1, size - 3);
    }
#endif
    return lstrlen(buf);
}

/* KERNEL: 0 no such drive, DRIVE_REMOVABLE, DRIVE_FIXED, DRIVE_REMOTE
 * (drive 0 is A:). */
UINT WINAPI GetDriveType(int drive)
{
#ifdef FILE_HOST
    return drive == 2 ? DRIVE_FIXED : 0;
#else
    if (drive < 0 || drive >= 26)
        return 0;
    union REGS r = { 0 };
    r.w.ax = 0x4409;                        /* device remote? */
    r.h.bl = (uint8_t)(drive + 1);
    int386(0x21, &r, &r);
    if (r.x.cflag)
        return r.w.ax == 0x0F ? 0 : DRIVE_FIXED;
    if (r.w.dx & 0x1000)
        return DRIVE_REMOTE;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x4408;                        /* removable media? */
    r.h.bl = (uint8_t)(drive + 1);
    int386(0x21, &r, &r);
    if (r.x.cflag)
        return DRIVE_FIXED;
    return r.w.ax == 0 ? DRIVE_REMOVABLE : DRIVE_FIXED;
#endif
}

/* ---------------------------------------------------------------- DOS for Win16 programs */

/* What a Win16 program asks DOS for (INT 21h, ne_guest_int21.c) beyond
 * the functions above, with DOS's meaning. */

/* INT 21h 3Bh: the current directory of the path's drive */
BOOL win_file_chdir(LPCSTR path)
{
    if (!path)
        return FALSE;
#ifdef FILE_HOST
    char d[256], full[256];
    struct stat st;
    host_path_to(path, d, sizeof(d));
    if (stat(d, &st) || !S_ISDIR(st.st_mode))
        return FALSE;
    /* the new one below the test directory, "." and ".." resolved */
    char out[128] = "";
    lstrcpyn(full, d[0] == '.' && (d[1] == '/' || !d[1]) ? d + 1 : d, sizeof(full));
    for (char *t = strtok(full, "/"); t; t = strtok(NULL, "/")) {
        if (!strcmp(t, "."))
            continue;
        if (!strcmp(t, "..")) {
            char *slash = strrchr(out, '/');
            if (slash)
                *slash = 0;
            else
                out[0] = 0;
            continue;
        }
        if (strlen(out) + strlen(t) + 2 > sizeof(out))
            return FALSE;
        if (out[0])
            strcat(out, "/");
        strcat(out, t);
    }
    lstrcpyn(host_cwd, out, sizeof(host_cwd));
    return TRUE;
#else
    return dos_path_op(0x3B, path);
#endif
}

/* INT 21h 19h (set < 0: the current drive, 0 A:) and 0Eh (select drive
 * set: the number of drives) */
int win_file_drive(int set)
{
#ifdef FILE_HOST
    return set < 0 ? 2 : 26;
#else
    union REGS r = { 0 };
    r.h.ah = set < 0 ? 0x19 : 0x0E;
    r.h.dl = (uint8_t)(set < 0 ? 0 : set);
    int386(0x21, &r, &r);
    return r.h.al;
#endif
}

/* INT 21h 43h: a file's attributes (set: changed to attr); -1 if there is
 * no such file */
int win_file_attr(LPCSTR path, int set, UINT attr)
{
    if (!path)
        return -1;
#ifdef FILE_HOST
    struct stat st;
    const char *h = host_path(path);
    if (stat(h, &st))
        return -1;
    if (set) {
        mode_t m = attr & 0x01 ? st.st_mode & ~0222u : st.st_mode | 0200u;
        return chmod(h, m) ? -1 : (int)attr;
    }
    return (S_ISDIR(st.st_mode) ? 0x10 : 0x20) | (st.st_mode & 0200 ? 0 : 0x01);
#else
    if (!dos_path(DB_PATH1, path))
        return -1;
    union REGS r = { 0 };
    r.w.ax = set ? 0x4301 : 0x4300;
    r.w.cx = (uint16_t)attr;
    r.w.dx = DB_PATH1;
    return dos_call(&r) ? (set ? (int)attr : r.w.cx) : -1;
#endif
}

/* INT 21h 57h: an open file's date and time (set: changed to them) */
BOOL win_file_time(HFILE f, int set, WORD *date, WORD *time_)
{
    if (tracked(f) < 0 || !date || !time_)
        return FALSE;
#ifdef FILE_HOST
    struct stat st;
    if (fstat((int)f, &st))
        return FALSE;
    if (set) {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_year = (*date >> 9) + 80;
        tm.tm_mon = ((*date >> 5) & 15) - 1;
        tm.tm_mday = *date & 31;
        tm.tm_hour = *time_ >> 11;
        tm.tm_min = (*time_ >> 5) & 63;
        tm.tm_sec = (*time_ & 31) * 2;
        tm.tm_isdst = -1;
        struct timespec ts[2];
        ts[0].tv_sec = ts[1].tv_sec = mktime(&tm);
        ts[0].tv_nsec = ts[1].tv_nsec = 0;
        return !futimens((int)f, ts);
    }
    struct tm *t = localtime(&st.st_mtime);
    if (!t || t->tm_year < 80) {
        *date = (1 << 5) | 1;
        *time_ = 0;
    } else {
        *date = (WORD)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
        *time_ = (WORD)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
    }
    return TRUE;
#else
    union REGS r = { 0 };
    r.w.ax = set ? 0x5701 : 0x5700;
    r.w.bx = (uint16_t)f;
    r.w.cx = *time_;
    r.w.dx = *date;
    int386(0x21, &r, &r);
    if (r.x.cflag)
        return FALSE;
    if (!set) {
        *time_ = r.w.cx;
        *date = r.w.dx;
    }
    return TRUE;
#endif
}

/* The DOS environment WINN.EXE got ("NAME=value" strings, each with its
 * NUL, and an empty one at the end) into buf; its bytes, 0 if it does not
 * fit. */
int win_file_environment(char *buf, int size)
{
    if (!buf || size < 2)
        return 0;
#ifdef FILE_HOST
    static const char env[] = "COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0TEMP=C:\\\0";
    if (size < (int)sizeof(env))
        return 0;
    memcpy(buf, env, sizeof(env));
    return (int)sizeof(env);
#else
    union REGS r = { 0 };
    r.h.ah = 0x62;                          /* WINN.EXE's PSP */
    int386(0x21, &r, &r);
    const uint8_t *psp = (const uint8_t *)dos_guest_far_ptr(r.w.bx, 0);
    uint16_t seg = (uint16_t)(psp[0x2C] | psp[0x2D] << 8);
    if (!seg)
        return 0;
    const char *env = (const char *)dos_guest_far_ptr(seg, 0);
    int n = 0;
    while (n + 1 < 32768 && (env[n] || env[n + 1]))
        n++;
    n = env[0] ? n + 2 : 1;                 /* the last string's NUL, the empty one */
    if (n > size)
        return 0;
    memcpy(buf, env, (size_t)n);
    return n;
#endif
}
