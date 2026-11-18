/*
 * ne_guest_shell.c - Windows 3.1 SHELL registry and dropped-file services
 * for x86 NE programs. The registry is WINN's own (core/winreg.c:
 * WINNREG.INI), not the host OS registry or a Windows REG.DAT binary.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "ne_guest_int.h"
#include "winreg.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define ERROR_INVALID_PARAMETER 87ul
#define ERROR_MORE_DATA         234ul
#define REG_SZ                  1ul

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

/* the data (its type, its size) where the program asked for them: size
 * told always, the bytes if they fit */
static uint32_t value_out(args *a, uint32_t type, const uint8_t *d, unsigned n,
                          uint32_t type_far, uint32_t data_far, uint32_t size_far)
{
    uint8_t b[4];
    uint32_t r = WINREG_SUCCESS;
    if (type_far) {
        wr32(b, type);
        if (!guest_write(a, type_far, b, 4))
            return ERROR_INVALID_PARAMETER;
    }
    if (!size_far)
        return data_far ? ERROR_INVALID_PARAMETER : WINREG_SUCCESS;
    if (!guest_read(a, size_far, b, 4))
        return ERROR_INVALID_PARAMETER;
    uint32_t cap = rd32(b);
    if (data_far)
        r = cap < n ? ERROR_MORE_DATA : guest_write(a, data_far, d, n) ? WINREG_SUCCESS :
            ERROR_INVALID_PARAMETER;
    wr32(b, n);
    return guest_write(a, size_far, b, 4) ? r : ERROR_INVALID_PARAMETER;
}

/* SHELL.1-7, KERNEL.222 RegDeleteValue, 223 RegEnumValue, 225
 * RegQueryValueEx, 226 RegSetValueEx (the op their ordinals). Registry
 * handles and LONG return codes are 32-bit. */
int ne_shell_registry_service(args *a, unsigned op, uint32_t *v)
{
    HTASK task = GetCurrentTask();
    char child[WINREG_NAME_BYTES], name[WINREG_NAME_BYTES];
    static char value[WINREG_DATA_BYTES + 1];
    const uint8_t *d;
    uint32_t n, type;
    uint8_t b[4];
    *v = WINREG_SUCCESS;
    switch (op) {
    case 1:                              /* RegOpenKey(HKEY, name, PHKEY) */
    case 2: {                            /* RegCreateKey(HKEY, name, PHKEY) */
        uint32_t dest = dw(a, 4), h;
        if (!dest || !guest_string(a, dw(a, 8), child, sizeof(child))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        *v = winreg_open(task, dw(a, 12), child, op == 2, &h);
        if (*v == WINREG_SUCCESS) {
            wr32(b, h);
            if (!guest_write(a, dest, b, 4)) {
                winreg_close(task, h);
                *v = ERROR_INVALID_PARAMETER;
            }
        }
        return 1;
    }
    case 3:                              /* RegCloseKey(HKEY) */
        *v = winreg_close(task, dw(a, 4));
        return 1;
    case 4:                              /* RegDeleteKey(HKEY, subkey): its subkeys too */
        *v = guest_string(a, dw(a, 4), child, sizeof(child)) ?
             winreg_delete_key(task, dw(a, 8), child) : ERROR_INVALID_PARAMETER;
        return 1;
    case 5:                              /* RegSetValue(HKEY, subkey, REG_SZ, data, cb) */
        if (dw(a, 12) != REG_SZ || !dw(a, 8) ||
            !guest_string(a, dw(a, 16), child, sizeof(child)) ||
            !guest_string(a, dw(a, 8), value, sizeof(value))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        *v = winreg_set(task, dw(a, 20), child, NULL, REG_SZ, value,
                        (uint32_t)strlen(value) + 1u);
        return 1;
    case 6: {                            /* RegQueryValue(HKEY, subkey, data, LPDWORD) */
        if (!guest_read(a, dw(a, 4), b, 4) ||
            !guest_string(a, dw(a, 12), child, sizeof(child))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        *v = winreg_get(task, dw(a, 16), child, NULL, &type, &d, &n);
        if (*v == WINREG_SUCCESS) {
            n = (uint32_t)strlen((const char *)d) + 1u;
            *v = value_out(a, type, d, n, 0, dw(a, 8), dw(a, 4));
        }
        return 1;
    }
    case 7:                              /* RegEnumKey(HKEY, index, LPSTR, cb) */
        *v = winreg_enum_key(task, dw(a, 16), dw(a, 12), child,
                             dw(a, 4) < sizeof(child) ? dw(a, 4) : sizeof(child));
        if (*v == WINREG_SUCCESS && !guest_write(a, dw(a, 8), child, (unsigned)strlen(child) + 1))
            *v = ERROR_INVALID_PARAMETER;
        return 1;
    case 222:                            /* RegDeleteValue(HKEY, name) */
        *v = guest_string(a, dw(a, 4), name, sizeof(name)) ?
             winreg_delete_value(task, dw(a, 8), name) : ERROR_INVALID_PARAMETER;
        return 1;
    case 223: {                          /* RegEnumValue(HKEY, index, name, LPDWORD name's
                                          * size, 0, LPDWORD type, data, LPDWORD size) */
        const char *shown;
        *v = winreg_enum_value(task, dw(a, 32), dw(a, 28), &shown, &type, &d, &n);
        if (*v != WINREG_SUCCESS)
            return 1;
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
        *v = value_out(a, type, d, n, dw(a, 12), dw(a, 8), dw(a, 4));
        return 1;
    }
    case 225:                            /* RegQueryValueEx(HKEY, name, 0, LPDWORD type,
                                          * data, LPDWORD size) */
        if (!guest_string(a, dw(a, 20), name, sizeof(name))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        *v = winreg_get(task, dw(a, 24), NULL, name, &type, &d, &n);
        if (*v == WINREG_SUCCESS)
            *v = value_out(a, type, d, n, dw(a, 12), dw(a, 8), dw(a, 4));
        return 1;
    case 226:                            /* RegSetValueEx(HKEY, name, 0, type, data, size) */
        n = dw(a, 4);
        if (!guest_string(a, dw(a, 20), name, sizeof(name)) || n > WINREG_DATA_BYTES ||
            (n && !guest_read(a, dw(a, 8), value, n))) {
            *v = ERROR_INVALID_PARAMETER;
            return 1;
        }
        *v = winreg_set(task, dw(a, 24), NULL, name, dw(a, 12), value, n);
        return 1;
    }
    return 0;
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
    winreg_task_end(task);
}

#endif
