/*
 * profile.c - INI files (GetPrivateProfileString & co., read only for now)
 * and the Windows directory.
 *
 * A file name without a drive or directory is looked up in the Windows
 * directory, which is where WINN.EXE was started from.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"

static char win_dir[96] = ".";

void win_set_home(const char *exe_path)
{
    if (!exe_path)
        return;
    int cut = -1;
    for (int i = 0; exe_path[i]; i++)
        if (exe_path[i] == '\\' || exe_path[i] == '/' || exe_path[i] == ':')
            cut = i;
    if (cut < 0)
        return;
    int n = exe_path[cut] == ':' ? cut + 1 : cut;   /* keep "C:" but drop "\" */
    if (n >= (int)sizeof(win_dir))
        return;
    memcpy(win_dir, exe_path, (size_t)n);
    win_dir[n] = 0;
    if (n == 2 && win_dir[1] == ':') {              /* "C:" alone: root */
        win_dir[2] = '\\';
        win_dir[3] = 0;
    }
}

UINT WINAPI GetWindowsDirectory(LPSTR buf, UINT size)
{
    if (buf && size)
        lstrcpyn(buf, win_dir, (int)size);
    return (UINT)lstrlen(win_dir);
}

UINT WINAPI GetSystemDirectory(LPSTR buf, UINT size) { return GetWindowsDirectory(buf, size); }

/* Full path of a file that belongs to the Windows directory. */
void win_path(const char *name, char *out, int size)
{
    int plain = 1;
    for (const char *p = name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            plain = 0;
    if (!plain || !lstrcmp(win_dir, ".")) {
        lstrcpyn(out, name, size);
        return;
    }
    int n = lstrlen(win_dir);
    lstrcpyn(out, win_dir, size);
    if (n && win_dir[n - 1] != '\\' && n + 1 < size) {
        out[n++] = '\\';
        out[n] = 0;
    }
    if (n < size)
        lstrcpyn(out + n, name, size - n);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    int n = (int)strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    return s;
}

/* Calls fn for every key of [section]; fn returns non-zero to stop. */
static int scan(const char *file, const char *section,
                int (*fn)(const char *key, const char *val, void *ctx), void *ctx)
{
    char path[128];
    win_path(file, path, sizeof(path));
    /* the native stdio has no fgets: read the whole (small) file */
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    char *text = (char *)win_alloc(16384 + 1);
    if (!text) {
        fclose(f);
        return 0;
    }
    size_t n = fread(text, 1, 16384, f);
    fclose(f);
    text[n] = 0;
    int in = 0, stop = 0;
    for (char *line = text; !stop && line && *line;) {
        char *next = line;
        while (*next && *next != '\n')
            next++;
        if (*next)
            *next++ = 0;
        char *s = trim(line);
        line = next;
        if (!*s || *s == ';')
            continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e)
                *e = 0;
            in = !lstrcmpi(trim(s + 1), section);
            continue;
        }
        if (!in)
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        stop = fn(trim(s), trim(eq + 1), ctx);
    }
    win_free(text);
    return 1;
}

typedef struct { const char *key; char *buf; int size; int found; int len; } pctx;

static int find_key(const char *key, const char *val, void *c)
{
    pctx *p = (pctx *)c;
    if (lstrcmpi(key, p->key))
        return 0;
    lstrcpyn(p->buf, val, p->size);
    /* a value in quotes loses them */
    int n = lstrlen(p->buf);
    if (n >= 2 && p->buf[0] == '"' && p->buf[n - 1] == '"') {
        memmove(p->buf, p->buf + 1, (size_t)(n - 2));
        p->buf[n - 2] = 0;
    }
    p->found = 1;
    return 1;
}

static int list_key(const char *key, const char *val, void *c)
{
    (void)val;
    pctx *p = (pctx *)c;
    int n = lstrlen(key);
    if (p->len + n + 2 > p->size)
        return 1;
    memcpy(p->buf + p->len, key, (size_t)n + 1);
    p->len += n + 1;
    return 0;
}

int WINAPI GetPrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR def,
                                   LPSTR buf, int size, LPCSTR file)
{
    if (!buf || size <= 0)
        return 0;
    pctx p = { key, buf, size, 0, 0 };
    if (!key) {                       /* all keys, double-NUL terminated */
        if (size < 2)
            return 0;
        scan(file, section, list_key, &p);
        buf[p.len] = 0;
        if (p.len + 1 < size)
            buf[p.len + 1] = 0;
        return p.len;
    }
    scan(file, section, find_key, &p);
    if (!p.found)
        lstrcpyn(buf, def ? def : "", size);
    return lstrlen(buf);
}

UINT WINAPI GetPrivateProfileInt(LPCSTR section, LPCSTR key, int def, LPCSTR file)
{
    char buf[32];
    if (!GetPrivateProfileString(section, key, "", buf, sizeof(buf), file) || !buf[0])
        return (UINT)def;
    const char *s = buf;
    int neg = *s == '-';
    if (neg)
        s++;
    if (*s < '0' || *s > '9')
        return (UINT)def;
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return (UINT)(neg ? -v : v);
}

int WINAPI GetProfileString(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR buf, int size)
{
    return GetPrivateProfileString(section, key, def, buf, size, "WIN.INI");
}

UINT WINAPI GetProfileInt(LPCSTR section, LPCSTR key, int def)
{
    return GetPrivateProfileInt(section, key, def, "WIN.INI");
}
