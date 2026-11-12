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
int ne_shell_registry_service(args *a, unsigned op, uint32_t *v)
{
    uint32_t h;
    HTASK task = GetCurrentTask();
    char path[REG_KEY_BYTES], child[REG_KEY_BYTES], value[REG_VALUE_BYTES];
    *v = ERROR_SUCCESS;
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
