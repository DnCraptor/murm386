/*
 * winreg.c - WINN's registry (winreg.h): WINNREG.INI, a section a key
 * (its path, "_WINN_ROOT" HKEY_CLASSES_ROOT itself), "_WINN_PRESENT=1" in
 * each that exists, a value a line of it ("_WINN_DEFAULT" the unnamed
 * one's): a REG_SZ string as it is, else "#type:" and its bytes in hex
 * (a string beginning with # or with a line break too).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "winreg.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define REG_FILE       "WINNREG.INI"
#define REG_PRESENT    "_WINN_PRESENT"
#define REG_DEFAULT    "_WINN_DEFAULT"
#define REG_SLOTS      32
#define REG_TEXT_BYTES 1024
#define REG_NAMES_BYTES 8192
#define REG_HANDLE     0x53480000ul

static struct {
    HTASK task;
    char path[WINREG_NAME_BYTES];
} reg_open[REG_SLOTS];

static char text[REG_TEXT_BYTES];
static uint8_t data[WINREG_DATA_BYTES + 1];

/* Win32's roots but HKEY_CLASSES_ROOT: their keys' names */
static const char *const roots[] = { "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE", "HKEY_USERS",
                                     "HKEY_PERFORMANCE_DATA", "HKEY_CURRENT_CONFIG",
                                     "HKEY_DYN_DATA" };
#define ROOTS (sizeof(roots) / sizeof(roots[0]))

/* a key's path: "" HKEY_CLASSES_ROOT (Win16's 0 and 1 too); NULL none */
static const char *key_name(HTASK task, uint32_t key)
{
    if (key == 0 || key == 1 || key == 0x80000000ul)
        return "";
    if (key > 0x80000000ul && key <= 0x80000000ul + ROOTS)
        return roots[key - 0x80000001ul];
    if ((key & 0xffffff00ul) != REG_HANDLE || !(key & 0xffu) || (key & 0xffu) > REG_SLOTS)
        return NULL;
    unsigned i = (unsigned)(key & 0xffu) - 1u;
    return reg_open[i].task == task ? reg_open[i].path : NULL;
}

static int is_root_path(const char *path)
{
    if (!path[0])
        return 1;
    for (unsigned i = 0; i < ROOTS; i++)
        if (!lstrcmpi(path, roots[i]))
            return 1;
    return 0;
}

/* the path of a key's subkey; 0 too long, or a name INI files cannot have */
static int path_name(HTASK task, uint32_t key, const char *child, char out[WINREG_NAME_BYTES])
{
    const char *base = key_name(task, key);
    if (!base)
        return 0;
    if (!child)
        child = "";
    while (*child == '\\')
        child++;
    unsigned n = (unsigned)strlen(base), m = (unsigned)strlen(child);
    while (m && child[m - 1] == '\\')
        m--;
    if (n + (n && m ? 1u : 0u) + m >= WINREG_NAME_BYTES)
        return 0;
    memcpy(out, base, n);
    if (n && m)
        out[n++] = '\\';
    memcpy(out + n, child, m);
    out[n + m] = 0;
    for (unsigned i = 0; out[i]; i++)
        if (out[i] == ']' || out[i] == '[' || out[i] == '\r' || out[i] == '\n')
            return 0;
    return 1;
}

static const char *section_name(const char *path)
{
    return path[0] ? path : "_WINN_ROOT";
}

static int key_exists(const char *path)
{
    char mark[8];
    if (is_root_path(path))
        return 1;
    return GetPrivateProfileString(section_name(path), REG_PRESENT, "", mark, sizeof(mark),
                                   REG_FILE) && mark[0] == '1';
}

int winreg_key_exists(HTASK task, uint32_t key, const char *sub)
{
    char path[WINREG_NAME_BYTES];
    return path_name(task, key, sub, path) && key_exists(path);
}

/* the key and those on the way to it */
static int key_create(const char *path)
{
    char parent[WINREG_NAME_BYTES];
    if (!path[0])
        return 1;
    strcpy(parent, path);
    for (char *p = parent; *p; p++) {
        if (*p != '\\')
            continue;
        *p = 0;
        if (!key_exists(parent) &&
            !WritePrivateProfileString(section_name(parent), REG_PRESENT, "1", REG_FILE))
            return 0;
        *p = '\\';
    }
    return key_exists(path) || WritePrivateProfileString(section_name(path), REG_PRESENT, "1",
                                                         REG_FILE);
}

/* the keys of WINNREG.INI (its sections' names, double-NUL ended), in a
 * block of win_alloc's; NULL if there is no memory */
static char *all_keys(void)
{
    char *names = (char *)win_alloc(REG_NAMES_BYTES);
    if (names)
        GetPrivateProfileString(NULL, NULL, "", names, REG_NAMES_BYTES, REG_FILE);
    return names;
}

/* a key's name under path: the rest of it, if it is path's subkey (any
 * deeper with deep), else NULL; the other roots are none of
 * HKEY_CLASSES_ROOT's */
static const char *under(const char *key, const char *path, int deep)
{
    if (!lstrcmp(key, "_WINN_ROOT"))
        return NULL;
    unsigned n = (unsigned)strlen(path);
    if (n) {
        if (strlen(key) <= n || key[n] != '\\')
            return NULL;
        char head[WINREG_NAME_BYTES];
        memcpy(head, key, n);
        head[n] = 0;
        if (lstrcmpi(head, path))
            return NULL;
        key += n + 1;
    } else {
        char head[WINREG_NAME_BYTES];
        unsigned k = 0;
        while (key[k] && key[k] != '\\' && k + 1 < sizeof(head)) {
            head[k] = key[k];
            k++;
        }
        head[k] = 0;
        if (is_root_path(head))
            return NULL;
    }
    return deep || !strchr(key, '\\') ? key : NULL;
}

uint32_t winreg_open(HTASK task, uint32_t key, const char *sub, int create, uint32_t *out)
{
    char path[WINREG_NAME_BYTES];
    if (!out || !path_name(task, key, sub, path))
        return WINREG_INVALID_PARAMETER;
    if (create ? !key_create(path) : !key_exists(path))
        return create ? WINREG_ACCESS_DENIED : WINREG_FILE_NOT_FOUND;
    if (is_root_path(path) && (key & 0xffffff00ul) != REG_HANDLE) {
        *out = key;                     /* a root itself */
        return WINREG_SUCCESS;
    }
    int i = 0;
    while (i < REG_SLOTS && reg_open[i].task)
        i++;
    if (i == REG_SLOTS)
        return WINREG_NOT_ENOUGH_MEMORY;
    reg_open[i].task = task;
    strcpy(reg_open[i].path, path);
    *out = REG_HANDLE | (unsigned)(i + 1);
    return WINREG_SUCCESS;
}

uint32_t winreg_close(HTASK task, uint32_t key)
{
    const char *path = key_name(task, key);
    if (!path)
        return WINREG_INVALID_HANDLE;
    if ((key & 0xffffff00ul) == REG_HANDLE)
        reg_open[(key & 0xffu) - 1u].task = 0;
    return WINREG_SUCCESS;
}

uint32_t winreg_delete_key(HTASK task, uint32_t key, const char *sub)
{
    char path[WINREG_NAME_BYTES];
    if (!path_name(task, key, sub, path))
        return WINREG_INVALID_PARAMETER;
    if (is_root_path(path))
        return WINREG_ACCESS_DENIED;
    if (!key_exists(path))
        return WINREG_FILE_NOT_FOUND;
    char *names = all_keys();
    if (!names)
        return WINREG_NOT_ENOUGH_MEMORY;
    for (const char *k = names; *k; k += strlen(k) + 1)
        if (under(k, path, 1))
            WritePrivateProfileString(k, NULL, NULL, REG_FILE);
    WritePrivateProfileString(path, NULL, NULL, REG_FILE);
    win_free(names);
    return WINREG_SUCCESS;
}

/* the index-th direct subkey of path, in names; NULL none */
static const char *subkey(const char *names, const char *path, uint32_t index)
{
    for (const char *k = names; *k; k += strlen(k) + 1) {
        const char *rest = under(k, path, 0);
        if (rest && rest[0] && !index--)
            return rest;
    }
    return NULL;
}

uint32_t winreg_enum_key(HTASK task, uint32_t key, uint32_t index, char *name, uint32_t cap)
{
    const char *base = key_name(task, key);
    if (!base)
        return WINREG_INVALID_HANDLE;
    char *names = all_keys();
    if (!names)
        return WINREG_NOT_ENOUGH_MEMORY;
    const char *found = subkey(names, base, index);
    uint32_t r = WINREG_SUCCESS;
    if (!found)
        r = WINREG_NO_MORE_ITEMS;
    else if (strlen(found) + 1 > cap)
        r = WINREG_MORE_DATA;
    else if (name)
        strcpy(name, found);
    win_free(names);
    return r;
}

/* ---------------------------------------------------------------- values */

static const char *value_key(const char *name)
{
    return name && name[0] ? name : REG_DEFAULT;
}

static int value_name_ok(const char *name)
{
    return !name || (!strchr(name, '=') && name[0] != '[' && name[0] != ';' &&
                     !strchr(name, '\r') && !strchr(name, '\n') &&
                     lstrcmpi(name, REG_PRESENT) && lstrcmpi(name, REG_DEFAULT));
}

static void value_encode(uint32_t type, const uint8_t *d, unsigned n, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned len = 0;
    while (len < n && d[len])
        len++;
    if (type == WINREG_SZ && d[0] != '#' && !memchr(d, '\r', len) && !memchr(d, '\n', len)) {
        memcpy(out, d, len);
        out[len] = 0;
        return;
    }
    wsprintf(out, "#%lu:", (unsigned long)type);
    char *p = out + strlen(out);
    for (unsigned i = 0; i < n; i++) {
        *p++ = hex[d[i] >> 4];
        *p++ = hex[d[i] & 15];
    }
    *p = 0;
}

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ?
           (c | 0x20) - 'a' + 10 : -1;
}

/* the stored text as data: its type, its bytes (a string with its NUL) */
static unsigned value_decode(const char *s, uint32_t *type, uint8_t *d)
{
    if (s[0] == '#' && s[1] >= '0' && s[1] <= '9') {
        uint32_t t = 0;
        const char *p = s + 1;
        while (*p >= '0' && *p <= '9')
            t = t * 10 + (uint32_t)(*p++ - '0');
        if (*p == ':') {
            unsigned n = 0;
            for (p++; hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && n < WINREG_DATA_BYTES; p += 2)
                d[n++] = (uint8_t)(hexval(p[0]) << 4 | hexval(p[1]));
            *type = t;
            return n;
        }
    }
    unsigned n = (unsigned)strlen(s);
    if (n > WINREG_DATA_BYTES - 1u)
        n = WINREG_DATA_BYTES - 1u;
    memcpy(d, s, n);
    d[n++] = 0;
    *type = WINREG_SZ;
    return n;
}

/* the value's text in text; 0 none */
static int value_text(const char *path, const char *name)
{
    return GetPrivateProfileString(section_name(path), value_key(name), "\001", text,
                                   sizeof(text), REG_FILE) && !(text[0] == '\001' && !text[1]);
}

uint32_t winreg_set(HTASK task, uint32_t key, const char *sub, const char *name, uint32_t type,
                    const void *bytes, uint32_t n)
{
    char path[WINREG_NAME_BYTES];
    if (!path_name(task, key, sub, path) || !value_name_ok(name) || n > WINREG_DATA_BYTES ||
        (n && !bytes))
        return WINREG_INVALID_PARAMETER;
    if (sub && sub[0] ? !key_create(path) : !key_exists(path))
        return sub && sub[0] ? WINREG_ACCESS_DENIED : WINREG_INVALID_HANDLE;
    if (n)
        memcpy(data, bytes, n);
    data[n] = 0;
    value_encode(type, data, n ? n : 1u, text);
    return WritePrivateProfileString(section_name(path), value_key(name), text, REG_FILE) ?
           WINREG_SUCCESS : WINREG_ACCESS_DENIED;
}

uint32_t winreg_get(HTASK task, uint32_t key, const char *sub, const char *name, uint32_t *type,
                    const uint8_t **out, uint32_t *n)
{
    char path[WINREG_NAME_BYTES];
    if (!path_name(task, key, sub, path))
        return key_name(task, key) ? WINREG_INVALID_PARAMETER : WINREG_INVALID_HANDLE;
    if (!key_exists(path) || !value_text(path, name))
        return WINREG_FILE_NOT_FOUND;
    uint32_t t;
    *n = value_decode(text, &t, data);
    if (type)
        *type = t;
    *out = data;
    return WINREG_SUCCESS;
}

uint32_t winreg_delete_value(HTASK task, uint32_t key, const char *name)
{
    const char *path = key_name(task, key);
    if (!path)
        return WINREG_INVALID_HANDLE;
    if (!value_text(path, name))
        return WINREG_FILE_NOT_FOUND;
    return WritePrivateProfileString(section_name(path), value_key(name), NULL, REG_FILE) ?
           WINREG_SUCCESS : WINREG_ACCESS_DENIED;
}

/* the index-th value's key in the section (its keys in names) */
static const char *value_at(const char *names, uint32_t index)
{
    for (const char *k = names; *k; k += strlen(k) + 1)
        if (lstrcmpi(k, REG_PRESENT) && !index--)
            return k;
    return NULL;
}

uint32_t winreg_enum_value(HTASK task, uint32_t key, uint32_t index, const char **name,
                           uint32_t *type, const uint8_t **out, uint32_t *n)
{
    static char found[WINREG_NAME_BYTES];
    const char *path = key_name(task, key);
    if (!path)
        return WINREG_INVALID_HANDLE;
    char *keys = (char *)win_alloc(REG_NAMES_BYTES);
    if (!keys)
        return WINREG_NOT_ENOUGH_MEMORY;
    GetPrivateProfileString(section_name(path), NULL, "", keys, REG_NAMES_BYTES, REG_FILE);
    const char *k = value_at(keys, index);
    if (k)
        lstrcpyn(found, k, sizeof(found));
    win_free(keys);
    if (!k)
        return WINREG_NO_MORE_ITEMS;
    GetPrivateProfileString(section_name(path), found, "", text, sizeof(text), REG_FILE);
    uint32_t t;
    *n = value_decode(text, &t, data);
    if (type)
        *type = t;
    *out = data;
    *name = lstrcmpi(found, REG_DEFAULT) ? found : "";
    return WINREG_SUCCESS;
}

uint32_t winreg_info(HTASK task, uint32_t key, uint32_t *keys, uint32_t *key_len,
                     uint32_t *values, uint32_t *value_len, uint32_t *data_len)
{
    const char *path = key_name(task, key);
    if (!path)
        return WINREG_INVALID_HANDLE;
    char *names = all_keys();
    if (!names)
        return WINREG_NOT_ENOUGH_MEMORY;
    uint32_t nk = 0, kl = 0, nv = 0, vl = 0, dl = 0;
    for (const char *k = names; *k; k += strlen(k) + 1) {
        const char *rest = under(k, path, 0);
        if (rest && rest[0]) {
            nk++;
            if (strlen(rest) > kl)
                kl = (uint32_t)strlen(rest);
        }
    }
    GetPrivateProfileString(section_name(path), NULL, "", names, REG_NAMES_BYTES, REG_FILE);
    for (const char *k = names; *k; k += strlen(k) + 1) {
        if (!lstrcmpi(k, REG_PRESENT))
            continue;
        nv++;
        uint32_t l = lstrcmpi(k, REG_DEFAULT) ? (uint32_t)strlen(k) : 0;
        if (l > vl)
            vl = l;
        uint32_t t;
        GetPrivateProfileString(section_name(path), k, "", text, sizeof(text), REG_FILE);
        uint32_t d = value_decode(text, &t, data);
        if (d > dl)
            dl = d;
    }
    win_free(names);
    if (keys) *keys = nk;
    if (key_len) *key_len = kl;
    if (values) *values = nv;
    if (value_len) *value_len = vl;
    if (data_len) *data_len = dl;
    return WINREG_SUCCESS;
}

void winreg_task_end(HTASK task)
{
    for (int i = 0; i < REG_SLOTS; i++)
        if (reg_open[i].task == task)
            reg_open[i].task = 0;
}

#endif
