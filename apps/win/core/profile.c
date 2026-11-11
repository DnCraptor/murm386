/*
 * profile.c - INI files (GetPrivateProfileString, WritePrivateProfileString
 * & co.) and the Windows directory.
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

/* the Windows directory's SYSTEM, as Windows has it (its fonts, its DLLs);
 * the Windows directory itself when there is none */
UINT WINAPI GetSystemDirectory(LPSTR buf, UINT size)
{
    char sys[sizeof(win_dir) + 8];
    win_path("SYSTEM", sys, sizeof(sys));
    int attr = lstrcmp(win_dir, ".") ? win_file_attr(sys, 0, 0) : -1;
    if (attr < 0 || !(attr & 0x10))
        return GetWindowsDirectory(buf, size);
    if (buf && size)
        lstrcpyn(buf, sys, (int)size);
    return (UINT)lstrlen(sys);
}

/* a file of the Windows directory's, or of the system one's when it is
 * there only (WIN.INI's fonts, DLLs): its full path */
void win_find_path(const char *name, char *out, int size)
{
    win_path(name, out, size);
    if (win_file_attr(out, 0, 0) >= 0 || !lstrcmp(out, name))
        return;
    char sys[sizeof(win_dir) + 8], alt[160];
    UINT n = GetSystemDirectory(sys, sizeof(sys));
    if (!n || !lstrcmp(sys, win_dir))
        return;
#if defined(__arm__) || defined(__thumb__)
    wsprintf(alt, "%s\\%s", sys, name);
#else
    wsprintf(alt, "%s/%s", sys, name);
#endif
    if (win_file_attr(alt, 0, 0) >= 0)
        lstrcpyn(out, alt, size);
}

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
#if defined(__arm__) || defined(__thumb__)
    const char sep = '\\';
#else
    const char sep = '/';           /* host tests */
#endif
    int n = lstrlen(win_dir);
    lstrcpyn(out, win_dir, size);
    if (n && win_dir[n - 1] != sep && n + 1 < size) {
        out[n++] = sep;
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

/* The last file read stays in memory: applications read their settings key
 * by key, and every key would otherwise be another pass over the SD card.
 * Writes go through the same copy. */
#define PROFILE_MAX 16384

static char  cache_path[128];
static char *cache_text;            /* NUL-terminated, NULL = no such file */
static int   cache_len, cache_valid;

static const char *load(const char *file, int *len)
{
    char path[128];
    win_path(file, path, sizeof(path));
    if (cache_valid && !lstrcmpi(path, cache_path)) {
        *len = cache_len;
        return cache_text;
    }
    win_free(cache_text);
    cache_text = NULL;
    cache_len = 0;
    cache_valid = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        char *t = (char *)win_alloc(PROFILE_MAX + 1);
        if (!t) {
            fclose(f);
            *len = 0;
            return NULL;            /* not cached: try again next time */
        }
        size_t n = fread(t, 1, PROFILE_MAX, f);
        fclose(f);
        t[n] = 0;
        cache_text = (char *)win_realloc(t, n + 1);
        if (!cache_text)
            cache_text = t;
        cache_len = (int)n;
    }
    lstrcpyn(cache_path, path, sizeof(cache_path));
    cache_valid = 1;
    *len = cache_len;
    return cache_text;
}

/* Calls fn for every key of [section]; fn returns non-zero to stop. */
static int scan(const char *file, const char *section,
                int (*fn)(const char *key, const char *val, void *ctx), void *ctx)
{
    int n;
    const char *src = load(file, &n);
    if (!src)
        return 0;
    char *text = (char *)win_alloc((size_t)n + 1);
    if (!text)
        return 0;
    memcpy(text, src, (size_t)n + 1);
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

/* ---------------------------------------------------------------- writing */

static int is_space(char c) { return c == ' ' || c == '\t'; }

/* the line [p, end) without its line break; returns where the next starts */
static const char *line_end(const char *p, const char **body_end)
{
    const char *e = p;
    while (*e && *e != '\n')
        e++;
    const char *b = e;
    if (b > p && b[-1] == '\r')
        b--;
    *body_end = b;
    return *e ? e + 1 : e;
}

/* does [s, e) name a [section] / start with "key =" ? */
static int is_section(const char *s, const char *e, const char *name)
{
    while (s < e && is_space(*s))
        s++;
    if (s == e || *s != '[')
        return -1;                  /* not a section line */
    s++;
    while (s < e && is_space(*s))
        s++;
    const char *c = s;
    while (c < e && *c != ']')
        c++;
    while (c > s && is_space(c[-1]))
        c--;
    int n = lstrlen(name);
    if (c - s != n)
        return 0;
    for (int i = 0; i < n; i++) {
        char a = s[i], b = name[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b)
            return 0;
    }
    return 1;
}

static int is_key(const char *s, const char *e, const char *key)
{
    while (s < e && is_space(*s))
        s++;
    if (s == e || *s == ';' || *s == '[')
        return 0;
    const char *q = s;
    while (q < e && *q != '=')
        q++;
    if (q == e)
        return 0;
    const char *k = q;
    while (k > s && is_space(k[-1]))
        k--;
    int n = lstrlen(key);
    if (k - s != n)
        return 0;
    for (int i = 0; i < n; i++) {
        char a = s[i], b = key[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b)
            return 0;
    }
    return 1;
}

static void put(char *out, int *o, const char *s, int n)
{
    memcpy(out + *o, s, (size_t)n);
    *o += n;
}

/* key NULL: the whole section goes; value NULL: the key goes. */
static BOOL write_profile(const char *section, const char *key, const char *value,
                          const char *file)
{
    if (!section || !file)
        return FALSE;
    int len;
    const char *text = load(file, &len);
    if (!cache_valid)
        return FALSE;               /* out of memory reading it */
    if (!text)
        text = "", len = 0;

    /* find the section, the key line in it and the end of its last entry */
    const char *sec = NULL, *sec_end = NULL, *last = NULL, *ks = NULL, *ke = NULL;
    for (const char *p = text; *p;) {
        const char *be, *next = line_end(p, &be);
        int r = is_section(p, be, section);
        if (r >= 0) {
            if (sec && !sec_end)
                sec_end = p;
            if (r == 1 && !sec) {
                sec = p;
                last = next;
            }
        } else if (sec && !sec_end) {
            const char *q = p;
            while (q < be && is_space(*q))
                q++;
            if (q < be)
                last = next;
            if (key && !ks && is_key(p, be, key)) {
                ks = p;
                ke = next;
            }
        }
        p = next;
    }
    if (sec && !sec_end)
        sec_end = text + len;

    int extra = (int)lstrlen(section) + (key ? lstrlen(key) : 0) + (value ? lstrlen(value) : 0) + 16;
    if (len + extra > PROFILE_MAX)
        return FALSE;
    char *out = (char *)win_alloc((size_t)(len + extra + 1));
    if (!out)
        return FALSE;
    int o = 0;
    if (!key) {                                     /* delete the section */
        if (!sec) {
            win_free(out);
            return TRUE;
        }
        put(out, &o, text, (int)(sec - text));
        put(out, &o, sec_end, (int)(text + len - sec_end));
    } else if (ks) {                                /* replace or delete the key */
        put(out, &o, text, (int)(ks - text));
        if (value) {
            put(out, &o, key, lstrlen(key));
            put(out, &o, "=", 1);
            put(out, &o, value, lstrlen(value));
            put(out, &o, "\r\n", 2);
        }
        put(out, &o, ke, (int)(text + len - ke));
    } else if (!value) {                            /* nothing to delete */
        win_free(out);
        return TRUE;
    } else if (sec) {                               /* new key, after the last entry */
        put(out, &o, text, (int)(last - text));
        if (last > text && last[-1] != '\n')
            put(out, &o, "\r\n", 2);
        put(out, &o, key, lstrlen(key));
        put(out, &o, "=", 1);
        put(out, &o, value, lstrlen(value));
        put(out, &o, "\r\n", 2);
        put(out, &o, last, (int)(text + len - last));
    } else {                                        /* new section at the end */
        put(out, &o, text, len);
        if (len && text[len - 1] != '\n')
            put(out, &o, "\r\n", 2);
        if (len)
            put(out, &o, "\r\n", 2);
        put(out, &o, "[", 1);
        put(out, &o, section, lstrlen(section));
        put(out, &o, "]\r\n", 3);
        put(out, &o, key, lstrlen(key));
        put(out, &o, "=", 1);
        put(out, &o, value, lstrlen(value));
        put(out, &o, "\r\n", 2);
    }
    out[o] = 0;

    FILE *f = fopen(cache_path, "wb");
    int ok = f && (int)fwrite(out, 1, (size_t)o, f) == o;
    if (f)
        fclose(f);
    if (!ok) {
        win_free(out);
        cache_valid = 0;            /* the file may be half written: read it again */
        return FALSE;
    }
    win_free(cache_text);
    cache_text = out;
    cache_len = o;
    return TRUE;
}

BOOL WINAPI WritePrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file)
{
    return write_profile(section, key, value, file);
}

BOOL WINAPI WriteProfileString(LPCSTR section, LPCSTR key, LPCSTR value)
{
    return write_profile(section, key, value, "WIN.INI");
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

/* ne_guest.c: where a Win16 program's DLLs are looked for after its own
 * directory */
const char *win_ne_dll_dir(void)
{
    return win_dir;
}

/* ... and then the system directory, when there is one of its own */
const char *win_ne_dll_system_dir(void)
{
    static char dir[128];
    UINT n = GetSystemDirectory(dir, sizeof(dir));
    return n && n < sizeof(dir) && lstrcmpi(dir, win_dir) ? dir : NULL;
}
