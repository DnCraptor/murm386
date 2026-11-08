/*
 * file.c - KERNEL file I/O (_lopen & co.) and the native directory and
 * program queries of module WINN.
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
#define F_RDWR 2
#else
#define FILE_HOST 1
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
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

/* Names of the files matching spec ("C:\WINN\*.EXE"), NUL-separated with
 * a second NUL at the end; returns how many. The whole search is done in
 * one call, so no search state lives across task switches. */
int WINAPI WinDirListN(LPCSTR spec, LPSTR buf, int size)
{
    if (!spec || !buf || size < 2)
        return 0;
    int len = 0, count = 0;
#ifdef FILE_HOST
    char dir[256];
    const char *pat = spec;
    for (const char *p = spec; *p; p++)
        if (*p == '\\' || *p == '/')
            pat = p + 1;
    int dn = (int)(pat - spec);
    if (dn >= (int)sizeof(dir))
        return 0;
    memcpy(dir, spec, (size_t)dn);
    dir[dn] = 0;
    if (!dn)
        lstrcpy(dir, ".");
    for (char *p = dir; *p; p++)
        if (*p == '\\')
            *p = '/';
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' || fnmatch(pat, e->d_name, FNM_CASEFOLD))
                continue;
            if (!add_name(buf, size, &len, e->d_name))
                break;
            count++;
        }
        closedir(d);
    }
#else
    static struct find_t ft;        /* big, keep it off the task stack */
    for (int r = _dos_findfirst(spec, 0, &ft); !r; r = _dos_findnext(&ft)) {
        if (!add_name(buf, size, &len, ft.name))
            break;
        count++;
    }
#endif
    buf[len] = 0;
    return count;
}

/* Is the file a Windows application (an EZ image built with the SDK)? */
BOOL WINAPI IsWinAppN(LPCSTR path)
{
#ifdef FILE_HOST
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    char host[256];
    lstrcpyn(host, path, sizeof(host));
    for (char *p = host; *p; p++)
        if (*p == '\\')
            *p = '/';
    struct stat st;
    return win_host_app(base) != NULL && !stat(host, &st);
#else
    return win_ez_is_app(path);
#endif
}
