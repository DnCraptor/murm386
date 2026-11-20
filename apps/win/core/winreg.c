/*
 * winreg.c - WINN's registry (winreg.h): WINNREG.INI, a section a key
 * (its path, "_WINN_ROOT" HKEY_CLASSES_ROOT itself), "_WINN_PRESENT=1" in
 * each that exists, a value a line of it ("_WINN_DEFAULT" the unnamed
 * one's): a REG_SZ string as it is, else "#type:" and its bytes in hex
 * (a string beginning with # or with a line break too). REG.DAT is a
 * read-only base for HKCR. Presence 0 hides a subtree, 2 recreates it
 * without inheriting the base, and #- masks a base default value.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"
#include "winreg.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define REG_FILE       "WINNREG.INI"
#define REG_PRESENT    "_WINN_PRESENT"
#define REG_DEFAULT    "_WINN_DEFAULT"
#define REG_DELETED    "#-"
#define REG_ABSENT     "#?"
#define REG_SLOTS      32
#define REG_TEXT_BYTES 1024
#define REG_NAMES_BYTES 16386  /* complete multi-string for a 16 KiB profile */
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

/* SHCC3.10: 32-byte header, 8-byte records, and a string pool.
 * Record 0 points to the root's children. Text records store byte lengths,
 * so strings are copied without transcoding the installation's ANSI bytes.
 * Layout: Debian chntpw WinReg.txt, with the .classes wrapper also handled
 * by historical Wine's Win 3.1 registry reader. */
#define REGDAT_LIMIT 65536u
#define REGDAT_HEADER 32u
#define REGDAT_RECORD 8u

static uint8_t *regdat;
static unsigned regdat_count, regdat_text, regdat_text_size;
static uint8_t *long_value;
static unsigned long_value_size;

static unsigned regdat_word(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t regdat_dword(const uint8_t *p)
{
    return regdat_word(p) | (uint32_t)regdat_word(p + 2) << 16;
}

static uint8_t *regdat_record(unsigned node)
{
    return regdat + REGDAT_HEADER + node * REGDAT_RECORD;
}

static unsigned regdat_link(unsigned node, unsigned field)
{
    return regdat_word(regdat_record(node) + field * 2);
}

static void regdat_set_link(unsigned node, unsigned field, unsigned value)
{
    uint8_t *p = regdat_record(node) + field * 2;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static const uint8_t *regdat_string(unsigned index, unsigned *len)
{
    if (!index || index >= regdat_count)
        return NULL;
    unsigned offset = regdat_link(index, 3), size = regdat_link(index, 2);
    if (offset > regdat_text_size || size > regdat_text_size - offset)
        return NULL;
    const uint8_t *s = regdat + regdat_text + offset;
    if (size && !s[size - 1])
        size--;
    if (memchr(s, 0, size))
        return NULL;
    *len = size;
    return s;
}

/* Visit each directory record once. Keep pending siblings on an explicit
 * stack so a deeply nested file cannot overflow WINN's startup stack. */
static int regdat_validate(unsigned node, uint8_t *seen)
{
    uint16_t pending[WINREG_NAME_BYTES];
    unsigned depth = 0;
    while (node || depth) {
        if (!node) {
            node = pending[--depth];
            continue;
        }
        if (node >= regdat_count || seen[node])
            return 0;
        seen[node] = 1;
        unsigned key = regdat_link(node, 2), value = regdat_link(node, 3);
        unsigned len, size;
        const uint8_t *name = regdat_string(key, &len);
        if (!name || !len || len >= WINREG_NAME_BYTES || seen[key] == 1 || memchr(name, '\\', len) ||
            memchr(name, '\r', len) || memchr(name, '\n', len))
            return 0;
        seen[key] = 2;
        if (value) {
            if (!regdat_string(value, &size) || seen[value] == 1)
                return 0;
            seen[value] = 2;
        }
        unsigned child = regdat_link(node, 1), sibling = regdat_link(node, 0);
        if (child && sibling) {
            if (depth == sizeof(pending) / sizeof(pending[0]))
                return 0;
            pending[depth++] = (uint16_t)sibling;
        }
        node = child ? child : sibling;
    }
    return 1;
}

static void regdat_name(unsigned node, char *name)
{
    unsigned len;
    const uint8_t *p = regdat_string(regdat_link(node, 2), &len);
    memcpy(name, p, len);
    name[len] = 0;
}

void winreg_shutdown(void)
{
    win_free(regdat);
    win_free(long_value);
    regdat = long_value = NULL;
    long_value_size = 0;
}

void winreg_init(void)
{
    winreg_shutdown();
    char path[128];
    win_path("REG.DAT", path, sizeof(path));
    FILE *file = fopen(path, "rb");
    if (!file)
        return;
    long size = fseek(file, 0, SEEK_END) ? -1 : ftell(file);
    if (size >= (long)(REGDAT_HEADER + REGDAT_RECORD) && size <= (long)REGDAT_LIMIT &&
        !fseek(file, 0, SEEK_SET))
        regdat = win_alloc((size_t)size);
    int read_ok = regdat && fread(regdat, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!read_ok || memcmp(regdat, "SHCC3.10", 8))
        goto invalid;
    regdat_count = regdat_dword(regdat + 16);
    regdat_text = regdat_dword(regdat + 20);
    regdat_text_size = regdat_dword(regdat + 24);
    if (!regdat_count || regdat_text > (unsigned)size || regdat_text < REGDAT_HEADER ||
        regdat_count > (regdat_text - REGDAT_HEADER) / REGDAT_RECORD ||
        regdat_text_size > (unsigned)size - regdat_text)
        goto invalid;
    uint8_t *seen = win_alloc(regdat_count);
    if (!seen)
        goto invalid;
    int valid = regdat_validate(regdat_link(0, 1), seen);
    win_free(seen);
    if (!valid)
        goto invalid;

    /* Unwrap .classes in RAM so its children are visible at HKCR. */
    unsigned parent = 0, field = 1, node = regdat_link(0, 1);
    while (node) {
        char name[WINREG_NAME_BYTES];
        regdat_name(node, name);
        if (!lstrcmpi(name, ".classes")) {
            unsigned first = regdat_link(node, 1), next = regdat_link(node, 0);
            regdat_set_link(parent, field, first ? first : next);
            if (first) {
                parent = first;
                while (regdat_link(parent, 0))
                    parent = regdat_link(parent, 0);
                regdat_set_link(parent, 0, next);
                field = 0;
            }
            node = next;
        } else {
            parent = node;
            field = 0;
            node = regdat_link(node, 0);
        }
    }
    return;
invalid:
    winreg_shutdown();
}

static unsigned regdat_child(unsigned node, const char *name)
{
    for (unsigned i = regdat_link(node, 1); i; i = regdat_link(i, 0)) {
        char candidate[WINREG_NAME_BYTES];
        regdat_name(i, candidate);
        if (!lstrcmpi(candidate, name))
            return i;
    }
    return 0;
}

/* -1 means missing, 0 is the root. */
static int regdat_find(const char *path)
{
    if (!regdat)
        return -1;
    unsigned node = 0;
    while (*path) {
        const char *sep = strchr(path, '\\');
        unsigned len = sep ? (unsigned)(sep - path) : (unsigned)strlen(path);
        char name[WINREG_NAME_BYTES];
        memcpy(name, path, len);
        name[len] = 0;
        if (!node && is_root_path(name))
            return -1;
        node = regdat_child(node, name);
        if (!node)
            return -1;
        path += len + (sep != NULL);
    }
    return (int)node;
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

enum { KEY_DELETED = '0', KEY_PRESENT = '1', KEY_RECREATED = '2' };
enum { LAYER_BASE = 1, LAYER_INI = 2 };

static int key_mark(const char *path)
{
    char mark[2];
    GetPrivateProfileString(section_name(path), REG_PRESENT, "", mark, sizeof(mark), REG_FILE);
    return mark[0];
}

/* A deletion hides both layers; a recreated key hides inherited contents. */
static unsigned key_layers(const char *path)
{
    char prefix[WINREG_NAME_BYTES];
    strcpy(prefix, path);
    unsigned layers = LAYER_BASE;
    for (char *p = prefix; (p = strchr(p, '\\')); *p++ = '\\') {
        *p = 0;
        int mark = key_mark(prefix);
        if (mark == KEY_DELETED)
            return 0;
        if (mark == KEY_RECREATED)
            layers &= ~LAYER_BASE;
    }
    int mark = key_mark(path);
    if (mark == KEY_DELETED)
        return 0;
    if (mark == KEY_RECREATED)
        return LAYER_INI;
    return layers | (mark == KEY_PRESENT ? LAYER_INI : 0);
}

static int key_exists(const char *path)
{
    if (is_root_path(path))
        return 1;
    unsigned layers = key_layers(path);
    return (layers & LAYER_INI) || ((layers & LAYER_BASE) && regdat_find(path) >= 0);
}

int winreg_key_exists(HTASK task, uint32_t key, const char *sub)
{
    char path[WINREG_NAME_BYTES];
    return path_name(task, key, sub, path) && key_exists(path);
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

/* Keep the deletion marker in place while discarding old INI contents.
 * A failed recreation therefore cannot expose any of the old subtree. */
static int recreate_key(const char *path)
{
    char *names = all_keys();
    if (!names)
        return 0;
    int ok = 1;
    for (const char *k = names; ok && *k; k += strlen(k) + 1)
        if (under(k, path, 1))
            ok = WritePrivateProfileString(k, NULL, NULL, REG_FILE);
    if (ok) {
        GetPrivateProfileString(path, NULL, "", names, REG_NAMES_BYTES, REG_FILE);
        for (const char *k = names; ok && *k; k += strlen(k) + 1)
            if (lstrcmpi(k, REG_PRESENT))
                ok = WritePrivateProfileString(path, k, NULL, REG_FILE);
    }
    win_free(names);
    return ok && WritePrivateProfileString(path, REG_PRESENT, "2", REG_FILE);
}

static int create_ini_key(const char *path)
{
    if (is_root_path(path))
        return 1;
    int mark = key_mark(path);
    if (mark == KEY_DELETED)
        return recreate_key(path);
    return mark == KEY_PRESENT || mark == KEY_RECREATED ||
           WritePrivateProfileString(path, REG_PRESENT, "1", REG_FILE);
}

static int key_create(const char *path)
{
    char parent[WINREG_NAME_BYTES];
    strcpy(parent, path);
    for (char *p = parent; (p = strchr(p, '\\')); *p++ = '\\') {
        *p = 0;
        if (!create_ini_key(parent))
            return 0;
    }
    return create_ini_key(path);
}

uint32_t winreg_open(HTASK task, uint32_t key, const char *sub, int create, uint32_t *out)
{
    char path[WINREG_NAME_BYTES];
    if (!out || !path_name(task, key, sub, path))
        return WINREG_INVALID_PARAMETER;
    if (!key_exists(path) && (!create || !key_create(path)))
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
    return WritePrivateProfileString(path, REG_PRESENT, "0", REG_FILE) ?
           WINREG_SUCCESS : WINREG_ACCESS_DENIED;
}

typedef struct {
    const char *path;
    const char *ini;
    int base;
    unsigned node;
} subkeys;

static subkeys subkeys_begin(const char *path, const char *ini)
{
    int base = key_layers(path) & LAYER_BASE ? regdat_find(path) : -1;
    return (subkeys){ path, ini, base, base < 0 ? 0 : regdat_link((unsigned)base, 1) };
}

static int subkeys_next(subkeys *it, char *name)
{
    char child[WINREG_NAME_BYTES];
    while (it->node) {
        unsigned node = it->node;
        it->node = regdat_link(node, 0);
        regdat_name(node, name);
        if (*it->path && strlen(it->path) + strlen(name) + 1 >= sizeof(child))
            continue;
        if (*it->path)
            wsprintf(child, "%s\\%s", it->path, name);
        else
            strcpy(child, name);
        if ((!*it->path && is_root_path(name)) || !key_layers(child))
            continue;
        return 1;
    }
    while (*it->ini) {
        const char *entry = it->ini;
        it->ini += strlen(entry) + 1;
        const char *rest = under(entry, it->path, 0);
        if (!rest || !*rest || strlen(rest) >= WINREG_NAME_BYTES ||
            !(key_layers(entry) & LAYER_INI) ||
            (it->base >= 0 && regdat_child((unsigned)it->base, rest)))
            continue;
        strcpy(name, rest);
        return 1;
    }
    return 0;
}

uint32_t winreg_enum_key(HTASK task, uint32_t key, uint32_t index, char *name, uint32_t cap)
{
    const char *base = key_name(task, key);
    if (!base || !key_exists(base))
        return WINREG_INVALID_HANDLE;
    char *names = all_keys();
    if (!names)
        return WINREG_NOT_ENOUGH_MEMORY;
    subkeys it = subkeys_begin(base, names);
    char found[WINREG_NAME_BYTES];
    uint32_t r = WINREG_NO_MORE_ITEMS;
    while (subkeys_next(&it, found)) {
        if (index--)
            continue;
        if (strlen(found) + 1 > cap) {
            r = WINREG_MORE_DATA;
        } else {
            if (name)
                strcpy(name, found);
            r = WINREG_SUCCESS;
        }
        break;
    }
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
    if (type == WINREG_SZ && d[0] != '#' &&
        (!len || (!strchr(" \t\"", d[0]) && !strchr(" \t\"", d[len - 1]))) &&
        !memchr(d, '\r', len) && !memchr(d, '\n', len)) {
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

/* 1: present, 0: absent, -1: explicitly masked. Empty is present. */
static int value_text(const char *path, const char *name)
{
    GetPrivateProfileString(section_name(path), value_key(name), REG_ABSENT, text,
                            sizeof(text), REG_FILE);
    return !strcmp(text, REG_DELETED) ? -1 : strcmp(text, REG_ABSENT) != 0;
}

static const uint8_t *base_value(const char *path, unsigned *len)
{
    int node = key_layers(path) & LAYER_BASE ? regdat_find(path) : -1;
    return node > 0 ? regdat_string(regdat_link((unsigned)node, 3), len) : NULL;
}

static uint32_t copy_base_value(const uint8_t *src, unsigned len, const uint8_t **out)
{
    uint8_t *dst = data;
    if (len >= sizeof(data)) {
        if (len >= long_value_size) {
            uint8_t *buf = win_realloc(long_value, len + 1);
            if (!buf)
                return WINREG_NOT_ENOUGH_MEMORY;
            long_value = buf;
            long_value_size = len + 1;
        }
        dst = long_value;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
    *out = dst;
    return WINREG_SUCCESS;
}

uint32_t winreg_set(HTASK task, uint32_t key, const char *sub, const char *name, uint32_t type,
                    const void *bytes, uint32_t n)
{
    char path[WINREG_NAME_BYTES];
    if (!path_name(task, key, sub, path) || !value_name_ok(name) || n > WINREG_DATA_BYTES ||
        (n && !bytes))
        return WINREG_INVALID_PARAMETER;
    if ((!sub || !sub[0]) && !key_exists(path))
        return WINREG_INVALID_HANDLE;
    if (!key_create(path))
        return WINREG_ACCESS_DENIED;
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
    if (!key_exists(path))
        return WINREG_FILE_NOT_FOUND;
    int present = value_text(path, name);
    if (present > 0) {
        uint32_t t;
        *n = value_decode(text, &t, data);
        if (type)
            *type = t;
        *out = data;
        return WINREG_SUCCESS;
    }
    unsigned len;
    const uint8_t *src = !present && (!name || !*name) ? base_value(path, &len) : NULL;
    if (!src)
        return WINREG_FILE_NOT_FOUND;
    if (type)
        *type = WINREG_SZ;
    *n = len + 1;
    return copy_base_value(src, len, out);
}

uint32_t winreg_delete_value(HTASK task, uint32_t key, const char *name)
{
    const char *path = key_name(task, key);
    if (!path || !key_exists(path))
        return WINREG_INVALID_HANDLE;
    if (!value_name_ok(name))
        return WINREG_INVALID_PARAMETER;
    int present = value_text(path, name);
    int unnamed = !name || !*name;
    unsigned len;
    if (present < 0 || (!present && (!unnamed || !base_value(path, &len))))
        return WINREG_FILE_NOT_FOUND;
    return WritePrivateProfileString(section_name(path), value_key(name),
                                     unnamed ? REG_DELETED : NULL, REG_FILE) ?
           WINREG_SUCCESS : WINREG_ACCESS_DENIED;
}

typedef struct {
    const char *path;
    const char *ini;
    const uint8_t *inherited;
    unsigned len;
} values_iter;

static values_iter values_begin(const char *path, const char *ini)
{
    values_iter it = { .path = path, .ini = ini };
    if (!value_text(path, NULL))
        it.inherited = base_value(path, &it.len);
    return it;
}

static int values_next(values_iter *it, const char **name, uint32_t *type, uint32_t *n,
                       const uint8_t **inherited)
{
    *inherited = it->inherited;
    if (it->inherited) {
        it->inherited = NULL;
        *name = "";
        *type = WINREG_SZ;
        *n = it->len + 1;
        return 1;
    }
    while (*it->ini) {
        const char *entry = it->ini;
        it->ini += strlen(entry) + 1;
        if (!lstrcmpi(entry, REG_PRESENT))
            continue;
        const char *key = !lstrcmpi(entry, REG_DEFAULT) ? "" : entry;
        if (value_text(it->path, key) > 0) {
            *name = key;
            *n = value_decode(text, type, data);
            return 1;
        }
    }
    return 0;
}

uint32_t winreg_enum_value(HTASK task, uint32_t key, uint32_t index, const char **name,
                           uint32_t *type, const uint8_t **out, uint32_t *n)
{
    static char found[WINREG_NAME_BYTES];
    const char *path = key_name(task, key);
    if (!path || !key_exists(path))
        return WINREG_INVALID_HANDLE;
    char *list = win_alloc(REG_NAMES_BYTES);
    if (!list)
        return WINREG_NOT_ENOUGH_MEMORY;
    GetPrivateProfileString(section_name(path), NULL, "", list, REG_NAMES_BYTES, REG_FILE);
    values_iter it = values_begin(path, list);
    const char *entry;
    const uint8_t *inherited;
    uint32_t t, r = WINREG_NO_MORE_ITEMS;
    while (values_next(&it, &entry, &t, n, &inherited)) {
        if (index--)
            continue;
        lstrcpyn(found, entry, sizeof(found));
        *name = found;
        if (type)
            *type = t;
        *out = data;
        r = inherited ? copy_base_value(inherited, *n - 1, out) : WINREG_SUCCESS;
        break;
    }
    win_free(list);
    return r;
}

uint32_t winreg_info(HTASK task, uint32_t key, uint32_t *keys, uint32_t *key_len,
                     uint32_t *values, uint32_t *value_len, uint32_t *data_len)
{
    const char *path = key_name(task, key);
    if (!path || !key_exists(path))
        return WINREG_INVALID_HANDLE;
    char *names = all_keys();
    if (!names)
        return WINREG_NOT_ENOUGH_MEMORY;
    uint32_t nk = 0, kl = 0, nv = 0, vl = 0, dl = 0;
    char child[WINREG_NAME_BYTES];
    subkeys subs = subkeys_begin(path, names);
    while (subkeys_next(&subs, child)) {
        nk++;
        uint32_t len = (uint32_t)strlen(child);
        if (len > kl)
            kl = len;
    }
    GetPrivateProfileString(section_name(path), NULL, "", names, REG_NAMES_BYTES, REG_FILE);
    values_iter vals = values_begin(path, names);
    const uint8_t *inherited;
    const char *entry;
    uint32_t t, n;
    while (values_next(&vals, &entry, &t, &n, &inherited)) {
        nv++;
        uint32_t len = (uint32_t)strlen(entry);
        if (len > vl)
            vl = len;
        if (n > dl)
            dl = n;
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

/* Read an HKCR default string through the effective WINN registry. */
BOOL WINAPI WinRegQueryClassStringN(LPCSTR key, LPSTR value, UINT capacity)
{
    if (!value || !capacity)
        return FALSE;
    value[0] = 0;
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
    const uint8_t *bytes;
    uint32_t type, size;
    if (!key || winreg_get(GetCurrentTask(), 0x80000000ul, key, NULL, &type, &bytes, &size) ||
        type != WINREG_SZ || !size || size > capacity || bytes[size - 1])
        return FALSE;
    memcpy(value, bytes, size);
#else
    (void)key;
    return FALSE;
#endif
    return TRUE;
}
