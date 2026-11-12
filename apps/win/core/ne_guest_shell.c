/*
 * ne_guest_shell.c - Windows 3.1 SHELL registry and dropped-file services
 * for x86 NE programs. The registry is private to WINN (WINNREG.INI), not
 * the host OS registry or a Windows REG.DAT binary.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define REG_FILE       "WINNREG.INI"
#define REG_PRESENT    "_WINN_PRESENT"
#define REG_DEFAULT    "_WINN_DEFAULT"
#define REG_SLOTS      32
#define REG_KEY_BYTES  128
#define REG_VALUE_BYTES 1024
#define REG_ROOT       0x80000000ul
#define REG_HANDLE     0x53480000ul

#define ERROR_SUCCESS           0ul
#define ERROR_FILE_NOT_FOUND    2ul
#define ERROR_ACCESS_DENIED     5ul
#define ERROR_INVALID_HANDLE   6ul
#define ERROR_NOT_ENOUGH_MEMORY 8ul
#define ERROR_INVALID_PARAMETER 87ul
#define ERROR_MORE_DATA         234ul
#define ERROR_NO_MORE_ITEMS     259ul
#define REG_NAMES_BYTES         8192
#define REG_SZ                  1ul

static struct {
    HTASK task;
    char path[REG_KEY_BYTES];
} reg_open[REG_SLOTS];

/* 0 and 1 were both accepted as HKEY_CLASSES_ROOT by Win16 SHELL.DLL. */
static int is_root(uint32_t key)
{
    return key == 0 || key == 1 || key == REG_ROOT;
}

static int guest_read(args *a, uint32_t far, void *dst, unsigned n)
{
    return (far >> 16) &&
           win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, n);
}

static int guest_write(args *a, uint32_t far, const void *src, unsigned n)
{
    return (far >> 16) &&
           win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, src, n);
}

static int guest_string(args *a, uint32_t far, char *dst, unsigned n)
{
    if (!far) {
        dst[0] = 0;
        return 1;
    }
    return (far >> 16) &&
           win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, n);
}

/* A registry handle belongs to the calling task; root is always open. */
static const char *key_name(HTASK task, uint32_t key)
{
    if (is_root(key))
        return "";
    if ((key & 0xffffff00ul) != REG_HANDLE ||
        !(key & 0xffu) || (key & 0xffu) > REG_SLOTS)
        return NULL;
    unsigned i = (unsigned)(key & 0xffu) - 1u;
    return reg_open[i].task == task ? reg_open[i].path : NULL;
}

static int path_name(HTASK task, uint32_t root, const char *child, char out[REG_KEY_BYTES])
{
    const char *base = key_name(task, root);
    if (!base)
        return 0;
    unsigned n = (unsigned)strlen(base);
    unsigned m = (unsigned)strlen(child);
    if (n + (n && m ? 1u : 0u) + m >= REG_KEY_BYTES)
        return 0;
    memcpy(out, base, n);
    if (n && m)
        out[n++] = '\\';
    memcpy(out + n, child, m + 1u);
    /* INI section names must not be able to introduce a new section. */
    for (unsigned i = 0; out[i]; i++)
        if (out[i] == ']' || out[i] == '\r' || out[i] == '\n')
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
    if (!path[0])
        return 1;
    return GetPrivateProfileString(section_name(path), REG_PRESENT, "", mark,
                                   sizeof(mark), REG_FILE) && mark[0] == '1';
}

static int key_create(const char *path)
{
    /* RegCreateKey and RegSetValue create intermediate subkeys too. */
    char parent[REG_KEY_BYTES];
    strcpy(parent, path);
    for (char *p = parent; *p; p++) {
        if (*p != '\\')
            continue;
        *p = 0;
        if (!WritePrivateProfileString(section_name(parent), REG_PRESENT, "1", REG_FILE))
            return 0;
        *p = '\\';
    }
    return WritePrivateProfileString(section_name(path), REG_PRESENT, "1", REG_FILE);
}

/* the keys of WINNREG.INI (its sections' names, double-NUL ended), in
 * a block of win_alloc's; NULL if there is no memory */
static char *all_keys(void)
{
    char *names = (char *)win_alloc(REG_NAMES_BYTES);
    if (names)
        GetPrivateProfileString(NULL, NULL, "", names, REG_NAMES_BYTES, REG_FILE);
    return names;
}

/* a key's name under path: the rest of it, if it is path's subkey (any
 * deeper with deep), else NULL */
static const char *under(const char *key, const char *path, int deep)
{
    if (!lstrcmp(key, "_WINN_ROOT"))
        return NULL;
    unsigned n = (unsigned)strlen(path);
    if (n) {
        if (strlen(key) <= n || key[n] != '\\')
            return NULL;
        char head[REG_KEY_BYTES];
        memcpy(head, key, n);
        head[n] = 0;
        if (lstrcmpi(head, path))
            return NULL;
        key += n + 1;
    }
    return deep || !strchr(key, '\\') ? key : NULL;
}

/* SHELL.1-7. Registry handles and LONG return codes are 32-bit. */
/* ---------------------------------------------------------------- values (Windows 95's) */

/* A value is a key of its key's section: its name (REG_DEFAULT the
 * unnamed one's), its data a REG_SZ string as it is, else "#type:" and its
 * bytes in hex (a string beginning with # or with a line break too). */
#define REG_DATA_BYTES ((REG_VALUE_BYTES - 16) / 2)

static const char *value_key(const char *name)
{
    return name[0] ? name : REG_DEFAULT;
}

static int value_name_ok(const char *name)
{
    return !strchr(name, '=') && name[0] != '[' && name[0] != ';' &&
           lstrcmpi(name, REG_PRESENT) && lstrcmpi(name, REG_DEFAULT);
}

static void value_encode(uint32_t type, const uint8_t *d, unsigned n, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned len = 0;
    while (len < n && d[len])
        len++;
    if (type == REG_SZ && d[0] != '#' && !memchr(d, '\r', len) && !memchr(d, '\n', len)) {
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
            for (p++; hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && n < REG_DATA_BYTES; p += 2)
                d[n++] = (uint8_t)(hexval(p[0]) << 4 | hexval(p[1]));
            *type = t;
            return n;
        }
    }
    unsigned n = (unsigned)strlen(s) + 1u;
    memcpy(d, s, n);
    *type = REG_SZ;
    return n;
}

/* the data (its type, its size) where the program asked for them: size
 * told always, the bytes if they fit */
static uint32_t value_out(args *a, uint32_t type, const uint8_t *d, unsigned n,
                          uint32_t type_far, uint32_t data_far, uint32_t size_far)
{
    uint8_t b[4];
    uint32_t r = ERROR_SUCCESS;
    if (type_far) {
        wr32(b, type);
        if (!guest_write(a, type_far, b, 4))
            return ERROR_INVALID_PARAMETER;
    }
    if (!size_far)
        return data_far ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!guest_read(a, size_far, b, 4))
        return ERROR_INVALID_PARAMETER;
    uint32_t cap = rd32(b);
    if (data_far)
        r = cap < n ? ERROR_MORE_DATA : guest_write(a, data_far, d, n) ? ERROR_SUCCESS :
            ERROR_INVALID_PARAMETER;
    wr32(b, n);
    return guest_write(a, size_far, b, 4) ? r : ERROR_INVALID_PARAMETER;
}

/* KERNEL.222 RegDeleteValue, 223 RegEnumValue, 225 RegQueryValueEx, 226
 * RegSetValueEx (the op their ordinals) */
static __attribute__((noinline)) int reg_values(args *a, unsigned op, uint32_t *v, HTASK task)
{
    static char text[REG_VALUE_BYTES];
    static uint8_t data[REG_DATA_BYTES + 1];
    char name[64];
    uint32_t hkey = op == 223 ? dw(a, 32) : op == 222 ? dw(a, 8) : dw(a, 24);
    const char *path = key_name(task, hkey);
    if (!path) {
        *v = ERROR_INVALID_HANDLE;
        return 1;
    }
    const char *sec = section_name(path);
    if (op != 223 && !guest_string(a, op == 222 ? dw(a, 4) : dw(a, 20), name, sizeof(name))) {
        *v = ERROR_INVALID_PARAMETER;
        return 1;
    }
    switch (op) {
    case 226: {                         /* RegSetValueEx(HKEY, name, 0, type, data, size) */
        uint32_t n = dw(a, 4), type = dw(a, 12);
        if (!value_name_ok(name) || n > REG_DATA_BYTES || (n && !guest_read(a, dw(a, 8), data, n))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        data[n] = 0;
        value_encode(type, data, n ? n : 1, text);
        if (!WritePrivateProfileString(sec, value_key(name), text, REG_FILE))
            *v = ERROR_ACCESS_DENIED;
        return 1;
    }
    case 225: {                         /* RegQueryValueEx(HKEY, name, 0, LPDWORD type,
                                         * data, LPDWORD size) */
        uint32_t type;
        if (!GetPrivateProfileString(sec, value_key(name), "\001", text, sizeof(text), REG_FILE) ||
            (text[0] == '\001' && !text[1])) {
            *v = ERROR_FILE_NOT_FOUND;
            return 1;
        }
        unsigned n = value_decode(text, &type, data);
        *v = value_out(a, type, data, n, dw(a, 12), dw(a, 8), dw(a, 4));
        return 1;
    }
    case 222:                           /* RegDeleteValue(HKEY, name) */
        if (!GetPrivateProfileString(sec, value_key(name), "\001", text, sizeof(text), REG_FILE) ||
            (text[0] == '\001' && !text[1]))
            *v = ERROR_FILE_NOT_FOUND;
        else if (!WritePrivateProfileString(sec, value_key(name), NULL, REG_FILE))
            *v = ERROR_ACCESS_DENIED;
        return 1;
    case 223: {                         /* RegEnumValue(HKEY, index, name, LPDWORD name's
                                         * size, 0, LPDWORD type, data, LPDWORD size) */
        uint32_t index = dw(a, 28), type;
        static char keys[REG_NAMES_BYTES];
        GetPrivateProfileString(sec, NULL, "", keys, sizeof(keys), REG_FILE);
        const char *found = NULL;
        for (const char *k = keys; *k && !found; k += strlen(k) + 1)
            if (lstrcmpi(k, REG_PRESENT) && !index--)
                found = k;
        if (!found) {
            *v = ERROR_NO_MORE_ITEMS;
            return 1;
        }
        const char *shown = lstrcmpi(found, REG_DEFAULT) ? found : "";
        uint8_t b[4];
        uint32_t len = (uint32_t)strlen(shown);
        if (!guest_read(a, dw(a, 20), b, 4)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        if (rd32(b) < len + 1) {
            *v = ERROR_MORE_DATA;
            return 1;
        }
        wr32(b, len);                   /* the name's length, its NUL not counted */
        if (!guest_write(a, dw(a, 24), shown, len + 1) || !guest_write(a, dw(a, 20), b, 4)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        GetPrivateProfileString(sec, found, "", text, sizeof(text), REG_FILE);
        unsigned n = value_decode(text, &type, data);
        *v = value_out(a, type, data, n, dw(a, 12), dw(a, 8), dw(a, 4));
        return 1;
    }
    }
    return 0;
}

int ne_shell_registry_service(args *a, unsigned op, uint32_t *v)
{
    uint32_t h;
    HTASK task = GetCurrentTask();
    char path[REG_KEY_BYTES], child[REG_KEY_BYTES], value[REG_VALUE_BYTES];
    *v = ERROR_SUCCESS;
    if (op == 222 || op == 223 || op == 225 || op == 226)
        return reg_values(a, op, v, task);
    switch (op) {
    case 1:                              /* RegOpenKey(HKEY, name, PHKEY) */
    case 2: {                            /* RegCreateKey(HKEY, name, PHKEY) */
        uint32_t dest = dw(a, 4);
        uint32_t sub = dw(a, 8);
        h = dw(a, 12);
        if (!dest || !guest_string(a, sub, child, sizeof(child)) ||
            !path_name(task, h, child, path)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        if (op == 1 && !key_exists(path)) {
            *v = ERROR_FILE_NOT_FOUND;
            return 1;
        }
        if (!path[0]) {
            if (!guest_write(a, dest, &h, 4))
                *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        int i = 0;
        while (i < REG_SLOTS && reg_open[i].task)
            i++;
        if (i == REG_SLOTS) {
            *v = ERROR_NOT_ENOUGH_MEMORY;
            return 1;
        }
        if (op == 2 && !key_create(path)) {
            *v = ERROR_ACCESS_DENIED;
            return 1;
        }
        uint8_t b[4];
        uint32_t handle = REG_HANDLE | (unsigned)(i + 1);
        wr32(b, handle);
        if (!guest_write(a, dest, b, sizeof(b))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        reg_open[i].task = task;
        strcpy(reg_open[i].path, path);
        return 1;
    }
    case 3:                              /* RegCloseKey(HKEY) */
        h = dw(a, 4);
        if (is_root(h))
            return 1;
        if (!key_name(task, h)) {
            *v = ERROR_INVALID_HANDLE;
            return 1;
        }
        reg_open[(h & 0xffu) - 1u].task = 0;
        return 1;
    case 5: {                            /* RegSetValue(HKEY, subkey, REG_SZ, data, cb) */
        uint32_t data = dw(a, 8);
        uint32_t type = dw(a, 12);
        uint32_t sub = dw(a, 16);
        h = dw(a, 20);
        (void)dw(a, 4);                 /* cbData: string's NUL determines size */
        if (type != REG_SZ || !data ||
            !guest_string(a, sub, child, sizeof(child)) ||
            !guest_string(a, data, value, sizeof(value)) ||
            !path_name(task, h, child, path)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        /* The profile writer uses one line per value; reject line breaks. */
        if (strchr(value, '\r') || strchr(value, '\n')) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        if (!key_create(path) ||
            !WritePrivateProfileString(section_name(path), REG_DEFAULT,
                                       value, REG_FILE))
            *v = ERROR_ACCESS_DENIED;
        return 1;
    }
    case 6: {                            /* RegQueryValue(HKEY, subkey, data, LPDWORD) */
        uint32_t count_far = dw(a, 4);
        uint32_t data_far = dw(a, 8);
        uint32_t sub = dw(a, 12);
        h = dw(a, 16);
        uint8_t b[4];
        if (!guest_read(a, count_far, b, 4) ||
            !guest_string(a, sub, child, sizeof(child)) ||
            !path_name(task, h, child, path)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        if (!key_exists(path)) {
            *v = ERROR_FILE_NOT_FOUND;
            return 1;
        }
        const char missing[] = "\001";
        GetPrivateProfileString(section_name(path), REG_DEFAULT, missing,
                                value, sizeof(value), REG_FILE);
        if (value[0] == '\001' && !value[1]) {
            *v = ERROR_FILE_NOT_FOUND;
            return 1;
        }
        unsigned need = (unsigned)strlen(value) + 1u;
        unsigned cap = rd32(b);
        wr32(b, need);
        if (!guest_write(a, count_far, b, 4)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        if (data_far) {
            if (cap < need) {
                *v = ERROR_MORE_DATA;
                return 1;
            }
            if (!guest_write(a, data_far, value, need))
                *v = ERROR_INVALID_PARAMETER;
        }
        return 1;
    }
    case 4: {                            /* RegDeleteKey(HKEY, subkey): its subkeys too */
        h = dw(a, 8);
        if (!guest_string(a, dw(a, 4), child, sizeof(child)) ||
            !path_name(task, h, child, path)) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        if (!path[0]) {
            *v = ERROR_ACCESS_DENIED;
            return 1;
        }
        if (!key_exists(path)) {
            *v = ERROR_FILE_NOT_FOUND;
            return 1;
        }
        char *names = all_keys();
        if (!names) {
            *v = ERROR_NOT_ENOUGH_MEMORY;
            return 1;
        }
        for (const char *k = names; *k; k += strlen(k) + 1)
            if (under(k, path, 1))
                WritePrivateProfileString(k, NULL, NULL, REG_FILE);
        WritePrivateProfileString(path, NULL, NULL, REG_FILE);
        win_free(names);
        return 1;
    }
    case 7: {                            /* RegEnumKey(HKEY, index, LPSTR, cb) */
        uint32_t cap = dw(a, 4), dest = dw(a, 8), index = dw(a, 12);
        const char *base = key_name(task, dw(a, 16));
        if (!base) {
            *v = ERROR_INVALID_HANDLE;
            return 1;
        }
        char *names = all_keys();
        if (!names) {
            *v = ERROR_NOT_ENOUGH_MEMORY;
            return 1;
        }
        const char *found = NULL;
        for (const char *k = names; *k && !found; k += strlen(k) + 1) {
            const char *rest = under(k, base, 0);
            if (rest && rest[0] && !index--)
                found = rest;
        }
        if (!found)
            *v = ERROR_NO_MORE_ITEMS;
        else if (strlen(found) + 1 > cap)
            *v = ERROR_MORE_DATA;
        else if (!guest_write(a, dest, found, (unsigned)strlen(found) + 1))
            *v = ERROR_INVALID_PARAMETER;
        win_free(names);
        return 1;
    }
    default:
        return 0;
    }
}

/* Win16 HDROP is a GlobalAlloc block beginning with DROPFILESTRUCT16:
 * WORD wSize, POINT16 ptMousePos, BOOL16 fInNonClientArea, ASCIIZ file list.
 * The guest's selectors/global blocks are NOT native GlobalLock handles.
 */
static int drop_header(args *a, uint16_t handle, uint16_t *start, uint32_t *size)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    win_ne_guest_segment *s = win_ne_guest_global(g, handle);
    uint8_t h[8];
    if (!s || s->size < sizeof(h) ||
        !win_ne_read(a->ctx, handle, 0, h, sizeof(h)))
        return 0;
    *start = rd16(h);
    *size = s->size > 0x10000u ? 0x10000u : s->size;
    return *start >= sizeof(h) && *start < *size;
}

int ne_shell_drag_service(args *a, unsigned op, uint32_t *v)
{
    if (op == 12) {                     /* DragFinish(HDROP) */
        uint16_t h = w(a, 4), pos;
        uint32_t size;
        if (drop_header(a, h, &pos, &size))
            win_ne_guest_global_free((win_ne_guest_image *)a->ctx->image, h);
        *v = 0;
        return 1;
    }
    if (op != 11)
        return 0;
    /* DragQueryFile(HDROP, WORD iFile, LPSTR buffer, WORD cch). */
    uint16_t cap = w(a, 4);
    uint32_t dest = dw(a, 6);
    uint16_t wanted = w(a, 10);
    uint16_t handle = w(a, 12), start_pos;
    uint32_t size;
    *v = 0;
    if (!drop_header(a, handle, &start_pos, &size))
        return 1;
    uint32_t pos = start_pos;
    unsigned index = 0;
    while (pos < size) {
        uint32_t start = pos;
        uint8_t ch;
        if (!win_ne_read(a->ctx, handle, pos, &ch, 1) || !ch)
            break;                    /* double NUL: end of filename list */
        while (pos < size &&
               win_ne_read(a->ctx, handle, pos, &ch, 1) && ch)
            pos++;
        if (pos == size)
            break;                    /* malformed/unterminated list */
        if (wanted == index) {
            unsigned n = pos - start;
            *v = n;                     /* characters excluding NUL */
            if (dest && cap) {
                unsigned copy = n < cap ? n : cap - 1u;
                uint8_t buf[64];
                for (unsigned at = 0; at < copy; at += sizeof(buf)) {
                    unsigned len = copy - at < sizeof(buf) ? copy - at : sizeof(buf);
                    if (!win_ne_read(a->ctx, handle, (uint16_t)(start + at),
                                     buf, len) ||
                        !guest_write(a, dest + at, buf, len)) {
                        *v = 0;
                        return 1;
                    }
                }
                ch = 0;
                if (!guest_write(a, dest + copy, &ch, 1))
                    *v = 0;
                else
                    *v = copy;          /* actual characters copied */
            }
            return 1;
        }
        index++;
        pos++;                          /* skip terminator */
    }
    if (wanted == 0xffffu)
        *v = index;
    return 1;
}

void ne_shell_task_end(HTASK task)
{
    for (int i = 0; i < REG_SLOTS; i++)
        if (reg_open[i].task == task)
            reg_open[i].task = 0;
}

#endif
