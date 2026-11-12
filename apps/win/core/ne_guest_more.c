/*
 * ne_guest_more.c - the rest of Windows 3.1's KERNEL, USER, SHELL and
 * TOOLHELP a Win16 program may call: installable drivers (OpenDriver: a
 * driver DLL of the program's, its DriverProc called), keystrokes and the
 * mouse injected (keybd_event, mouse_event: their arguments the
 * registers), SystemParametersInfo's structures, huge pointers moved
 * (LongPtrAdd), segments described (GetCodeInfo), the environment
 * (DoEnvironmentSubst), KERNEL's registry functions (SHELL's), the network
 * (none: WN_NOT_SUPPORTED), and the hooks and handlers a debugger or a
 * system program sets, which WINN has no use for.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define TH WIN_NE_SERVICE_TOOLHELP
#define SH WIN_NE_SERVICE_SHELL

static int str_in(args *a, uint32_t far, char *dst, uint32_t cap)
{
    if (!(far >> 16) || !win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, cap)) {
        dst[0] = 0;
        return 0;
    }
    return 1;
}

static int put(args *a, uint32_t far, const void *src, uint32_t n)
{
    return (far >> 16) && win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, src, n);
}

static int get(args *a, uint32_t far, void *dst, uint32_t n)
{
    return (far >> 16) && win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, n);
}

/* ---------------------------------------------------------------- installable drivers */

#define DRIVERS 8
#define DRV_HANDLE(i) ((uint16_t)(0x6c01u + (i)))
#define DRV_LOAD    1
#define DRV_ENABLE  2
#define DRV_OPEN    3
#define DRV_CLOSE   4
#define DRV_DISABLE 5
#define DRV_FREE    6
#define DRV_INSTALL 9
#define DRV_REMOVE  10
#define DRVCNF_RESTART 2
#define GND_FIRSTINSTANCEONLY 1
#define GND_REVERSE 2

/* the drivers open: their task, their DLL (its instance) and DriverProc,
 * what DRV_OPEN gave (dwDriverID), the name they were opened by */
static struct {
    HTASK task;
    uint16_t inst;
    uint32_t proc, id;
    char alias[32];
} drv[DRIVERS];

static int drv_index(uint16_t h)
{
    int i = h - DRV_HANDLE(0);
    return i >= 0 && i < DRIVERS && drv[i].task ? i : -1;
}

static uint32_t drv_send(args *a, int i, uint16_t msg, uint32_t l1, uint32_t l2,
                         void *blob, uint16_t len)
{
    const uint16_t words[8] = { (uint16_t)(drv[i].id >> 16), (uint16_t)drv[i].id,
                                DRV_HANDLE(i), msg, (uint16_t)(l1 >> 16), (uint16_t)l1,
                                (uint16_t)(l2 >> 16), (uint16_t)l2 };
    return win_ne_call(a->ctx, drv[i].proc, words, 8, blob, len);
}

/* another open driver of the same DLL (the DLL is loaded, enabled) */
static int drv_shared(int i)
{
    for (int k = 0; k < DRIVERS; k++)
        if (k != i && drv[k].task == drv[i].task && drv[k].inst == drv[i].inst)
            return 1;
    return 0;
}

/* OpenDriver(name, section, lParam2): the name a key of SYSTEM.INI's
 * section ([drivers] by default) naming the file and its parameters, or the
 * file itself; DRV_LOAD and DRV_ENABLE the first time its DLL is, DRV_OPEN
 * (lParam1 the parameters) each time */
static uint16_t open_driver(args *a)
{
    win_ne_guest_api_context *c = a->ctx;
    win_ne_guest_image *g = (win_ne_guest_image *)c->image;
    char name[32], section[32], line[128];
    uint32_t l2 = dw(a, 4);
    if (!str_in(a, dw(a, 12), name, sizeof(name)) || !name[0])
        return 0;
    if (!str_in(a, dw(a, 8), section, sizeof(section)) || !section[0])
        lstrcpy(section, "drivers");
    GetPrivateProfileString(section, name, "", line, sizeof(line), "SYSTEM.INI");
    char *file = line[0] ? line : name, *params = file;
    while (*params && *params != ' ' && *params != '\t')
        params++;
    if (*params)
        *params++ = 0;
    while (*params == ' ' || *params == '\t')
        params++;
    int i = 0;
    while (i < DRIVERS && drv[i].task)
        i++;
    if (i == DRIVERS)
        return 0;
    int d = win_ne_guest_load_library(g, file);
    if (d < 0)
        return 0;
    win_ne_dll_init(c);
    uint32_t proc = win_ne_guest_dll_proc(g, d, 0, "DriverProc");
    if (!proc)
        return 0;
    drv[i].task = GetCurrentTask();
    drv[i].inst = win_ne_guest_dll_instance(g, d);
    drv[i].proc = proc;
    drv[i].id = 0;
    lstrcpyn(drv[i].alias, name, sizeof(drv[i].alias));
    if (!drv_shared(i)) {
        if (!drv_send(a, i, DRV_LOAD, 0, 0, NULL, 0)) {
            drv[i].task = 0;
            return 0;
        }
        drv_send(a, i, DRV_ENABLE, 0, 0, NULL, 0);
    }
    uint16_t len = (uint16_t)((strlen(params) + 2) & ~1u);
    uint32_t far = params[0] ? win_ne_blob_far(c, len) : 0;
    uint32_t id = drv_send(a, i, DRV_OPEN, far, l2, params[0] ? params : NULL,
                           params[0] ? len : 0);
    if (!id) {                          /* it would not open */
        if (!drv_shared(i)) {
            drv_send(a, i, DRV_DISABLE, 0, 0, NULL, 0);
            drv_send(a, i, DRV_FREE, 0, 0, NULL, 0);
        }
        drv[i].task = 0;
        return 0;
    }
    drv[i].id = id;
    return DRV_HANDLE(i);
}

static uint32_t close_driver(args *a, int i, uint32_t l1, uint32_t l2)
{
    uint32_t r = drv_send(a, i, DRV_CLOSE, l1, l2, NULL, 0);
    if (!drv_shared(i)) {
        drv_send(a, i, DRV_DISABLE, 0, 0, NULL, 0);
        drv_send(a, i, DRV_FREE, 0, 0, NULL, 0);
    }
    drv[i].task = 0;
    return r;
}

static __attribute__((noinline)) int driver_service(args *a, uint16_t service, uint32_t *v,
                                                   int *dword)
{
    HTASK task = GetCurrentTask();
    int i;
    switch (service) {
    case 0x10fc:                        /* OpenDriver(LPCSTR, LPCSTR, LPARAM) */
        *v = open_driver(a);
        return 1;
    case 0x10fd:                        /* CloseDriver(HDRVR, LPARAM, LPARAM) */
        i = drv_index(w(a, 12));
        *v = i >= 0 && drv[i].task == task ? close_driver(a, i, dw(a, 8), dw(a, 4)) : 0;
        *dword = 1;
        return 1;
    case 0x10fb:                        /* SendDriverMessage(HDRVR, msg, LPARAM, LPARAM) */
        i = drv_index(w(a, 14));
        *v = i >= 0 && drv[i].task == task ?
             drv_send(a, i, w(a, 12), dw(a, 8), dw(a, 4), NULL, 0) : 0;
        *dword = 1;
        return 1;
    case 0x10fe:                        /* GetDriverModuleHandle(HDRVR) */
        i = drv_index(w(a, 4));
        *v = i >= 0 ? drv[i].inst : 0;
        return 1;
    case 0x10ff: {                      /* DefDriverProc(id, HDRVR, msg, LPARAM, LPARAM) */
        uint16_t msg = w(a, 12);
        *v = msg == DRV_LOAD || msg == DRV_FREE || msg == DRV_ENABLE || msg == DRV_DISABLE ? 1 :
             msg == DRV_INSTALL || msg == DRV_REMOVE ? DRVCNF_RESTART : 0;
        *dword = 1;
        return 1;
    }
    case 0x1100: {                      /* GetDriverInfo(HDRVR, DRIVERINFOSTRUCT FAR *) */
        uint8_t info[2 + 2 + 2 + 128];
        uint32_t far = dw(a, 4);
        i = drv_index(w(a, 8));
        *v = 0;
        if (i < 0 || !get(a, far, info, 2) || rd16(info) < sizeof(info))
            return 1;
        memset(info + 2, 0, sizeof(info) - 2);
        wr16(info + 2, DRV_HANDLE(i));
        wr16(info + 4, drv[i].inst);
        lstrcpyn((char *)info + 6, drv[i].alias, 128);
        *v = put(a, far, info, sizeof(info));
        return 1;
    }
    case 0x1101: {                      /* GetNextDriver(HDRVR, DWORD flags) */
        uint32_t flags = dw(a, 4);
        int step = (flags & GND_REVERSE) ? -1 : 1;
        int at = w(a, 8) ? drv_index(w(a, 8)) : (step > 0 ? -1 : DRIVERS);
        *v = 0;
        if (at < 0 && w(a, 8))
            return 1;
        for (int k = at + step; k >= 0 && k < DRIVERS; k += step) {
            if (!drv[k].task)
                continue;
            int first = 1;              /* the first open of its DLL, in that order */
            for (int j = 0; (flags & GND_FIRSTINSTANCEONLY) && j < k; j++)
                if (drv[j].task && drv[j].inst == drv[k].inst && drv[j].task == drv[k].task)
                    first = 0;
            if (first) {
                *v = DRV_HANDLE(k);
                break;
            }
        }
        return 1;
    }
    }
    return 0;
}

void ne_more_task_end(HTASK task)
{
    for (int i = 0; i < DRIVERS; i++)    /* its DLLs went with it: no messages */
        if (drv[i].task == task)
            drv[i].task = 0;
}

/* ---------------------------------------------------------------- the input injected */

/* keybd_event: AL the virtual key, AH 80h a release; BL its scan code, BH
 * bit 0 an extended key. mouse_event: AX what (MOUSEEVENTF_*), BX and CX
 * where (mickeys, or 0..65535 of the screen with ABSOLUTE). AX is on the
 * stack below the return address (the thunk pushed it), the others are
 * the gateway's saved ones */
static void input_event(args *a, uint16_t service)
{
    uint16_t r[5] = { 0, 0, 0, 0, 0 };
    win_ne_saved_regs(a->ctx, a->cpu, r);
    uint16_t ax = w(a, 0), bx = r[0], cx = r[1];
    if (service == WIN_NE_SERVICE_KEYBD_EVENT) {
        win_ddi_key_event(ax & 0xff, (bx & 0xff) | ((bx & 0x100) ? 0x100 : 0), !(ax & 0x8000));
        return;
    }
    POINT pt;
    GetCursorPos(&pt);
    if (ax & 0x8000) {                  /* MOUSEEVENTF_ABSOLUTE */
        pt.x = (int)((uint32_t)bx * (uint32_t)GetSystemMetrics(SM_CXSCREEN) >> 16);
        pt.y = (int)((uint32_t)cx * (uint32_t)GetSystemMetrics(SM_CYSCREEN) >> 16);
    } else if (ax & 1) {                /* MOUSEEVENTF_MOVE */
        pt.x += (int16_t)bx;
        pt.y += (int16_t)cx;
    }
    /* the buttons as they are (not swapped: the events are the physical
     * ones), changed by the event's */
    int swapped = GetSystemMetrics(SM_SWAPBUTTON);
    unsigned b = (GetAsyncKeyState(swapped ? VK_RBUTTON : VK_LBUTTON) < 0 ? 1u : 0) |
                 (GetAsyncKeyState(swapped ? VK_LBUTTON : VK_RBUTTON) < 0 ? 2u : 0) |
                 (GetAsyncKeyState(VK_MBUTTON) < 0 ? 4u : 0);
    if (ax & 0x02) b |= 1u;             /* LEFTDOWN, LEFTUP */
    if (ax & 0x04) b &= ~1u;
    if (ax & 0x08) b |= 2u;             /* RIGHTDOWN, RIGHTUP */
    if (ax & 0x10) b &= ~2u;
    if (ax & 0x20) b |= 4u;             /* MIDDLEDOWN, MIDDLEUP */
    if (ax & 0x40) b &= ~4u;
    win_ddi_mouse_event(pt.x, pt.y, b);
}

/* ---------------------------------------------------------------- SystemParametersInfo */

/* (UINT action, UINT param, LPVOID pv, UINT ini): the program's ints are
 * words, its LOGFONT 50 bytes */
static int spi_service(args *a)
{
    UINT action = w(a, 12), param = w(a, 10), ini = w(a, 4);
    uint32_t pv = dw(a, 6);
    uint8_t b[50];
    switch (action) {
    case SPI_GETBEEP: case SPI_GETBORDER: case SPI_GETKEYBOARDSPEED:
    case SPI_GETSCREENSAVETIMEOUT: case SPI_GETSCREENSAVEACTIVE: case SPI_GETGRIDGRANULARITY:
    case SPI_GETKEYBOARDDELAY: case SPI_GETICONTITLEWRAP: case SPI_GETMENUDROPALIGNMENT:
    case SPI_GETFASTTASKSWITCH: case SPI_ICONHORIZONTALSPACING: case SPI_ICONVERTICALSPACING: {
        int x = 0;
        if (!pv)                        /* the spacings with no pointer: set */
            return SystemParametersInfo(action, param, NULL, ini);
        if (!SystemParametersInfo(action, param, &x, ini))
            return 0;
        wr16(b, (uint16_t)x);
        return put(a, pv, b, 2);
    }
    case SPI_GETMOUSE: case SPI_SETMOUSE: {
        int m[3];
        if (action == SPI_SETMOUSE) {
            if (!get(a, pv, b, 6))
                return 0;
            for (int i = 0; i < 3; i++)
                m[i] = (int16_t)rd16(b + 2 * i);
            return SystemParametersInfo(action, param, m, ini);
        }
        if (!SystemParametersInfo(action, param, m, ini))
            return 0;
        for (int i = 0; i < 3; i++)
            wr16(b + 2 * i, (uint16_t)m[i]);
        return put(a, pv, b, 6);
    }
    case SPI_GETICONTITLELOGFONT: {
        LOGFONT lf;
        if (!SystemParametersInfo(action, param, &lf, ini))
            return 0;
        memset(b, 0, sizeof(b));
        wr16(b, (uint16_t)lf.lfHeight); wr16(b + 2, (uint16_t)lf.lfWidth);
        wr16(b + 4, (uint16_t)lf.lfEscapement); wr16(b + 6, (uint16_t)lf.lfOrientation);
        wr16(b + 8, (uint16_t)lf.lfWeight);
        b[10] = lf.lfItalic; b[11] = lf.lfUnderline; b[12] = lf.lfStrikeOut;
        b[13] = lf.lfCharSet; b[14] = lf.lfOutPrecision; b[15] = lf.lfClipPrecision;
        b[16] = lf.lfQuality; b[17] = lf.lfPitchAndFamily;
        memcpy(b + 18, lf.lfFaceName, LF_FACESIZE);
        return put(a, pv, b, sizeof(b));
    }
    case SPI_SETDESKWALLPAPER: case SPI_SETDESKPATTERN: {
        char s[128];
        if (pv == 0xffffffffu || !pv)   /* WIN.INI's again */
            return SystemParametersInfo(action, param, pv ? (LPVOID)(intptr_t)-1 : NULL, ini);
        if (!str_in(a, pv, s, sizeof(s)))
            return 0;
        return SystemParametersInfo(action, param, s, ini);
    }
    }
    return SystemParametersInfo(action, param, NULL, ini);
}

/* ---------------------------------------------------------------- SHELL */

/* the program's environment (its block, made now if it had none): its
 * bytes in a block of win_alloc's, NUL-separated, ending with two NULs */
static char *environment(args *a, ne_user *u, uint32_t *n)
{
    ne_environment(a->ctx, u);
    win_ne_guest_segment *s = u->env ? win_ne_guest_global((win_ne_guest_image *)a->ctx->image,
                                                          u->env) : NULL;
    uint32_t size = s ? (s->size < 0x8000u ? s->size : 0x8000u) : 0;
    char *env = size ? (char *)win_alloc(size + 2) : NULL;
    if (env && !win_ne_read(a->ctx, u->env, 0, env, size)) {
        win_free(env);
        env = NULL;
    }
    *n = size;
    return env;
}

/* the value of a variable (the name compared as Windows does: any case),
 * its place in env; -1 if there is none */
static int env_find(const char *env, uint32_t n, const char *name, uint32_t len)
{
    for (uint32_t at = 0; at < n && env[at];) {
        uint32_t end = at;
        while (end < n && env[end])
            end++;
        uint32_t k = 0;
        while (k < len && at + k < end &&
               (env[at + k] | 0x20) == (name[k] | 0x20) &&
               ((env[at + k] | 0x20) >= 'a' && (env[at + k] | 0x20) <= 'z' ?
                1 : env[at + k] == name[k]))
            k++;
        if (k == len &&
            (at + len == end || env[at + len] == '='))
            return (int)(at + len + (at + len < end));
        at = end + 1;
    }
    return -1;
}

/* DoEnvironmentSubst(LPSTR, UINT size): %NAME%s replaced by their values;
 * MAKELONG(done, the string's length) */
static uint32_t env_subst(args *a, ne_user *u)
{
    uint32_t far = dw(a, 6), cap = w(a, 4), n;
    char in[256], out[256];
    if (!str_in(a, far, in, sizeof(in)))
        return 0;
    char *env = environment(a, u, &n);
    uint32_t o = 0;
    int ok = 1;
    for (const char *p = in; *p && ok;) {
        const char *e = *p == '%' ? strchr(p + 1, '%') : NULL;
        int at = e && e > p + 1 && env ? env_find(env, n, p + 1, (uint32_t)(e - p - 1)) : -1;
        const char *s = at >= 0 ? env + at : p;
        uint32_t len = at >= 0 ? (uint32_t)strlen(env + at) : 1;
        if (o + len >= sizeof(out))
            ok = 0;
        else
            memcpy(out + o, s, len), o += len;
        p = at >= 0 ? e + 1 : p + 1;
    }
    win_free(env);
    out[o] = 0;
    if (!ok || o + 1 > cap || !put(a, far, out, o + 1))
        return MAKELONG(FALSE, (WORD)strlen(in));
    return MAKELONG(TRUE, (WORD)o);
}

static __attribute__((noinline)) int shell_more(args *a, ne_user *u, unsigned op, uint32_t *v,
                                               int *dword)
{
    switch (op) {
    case 13: {                          /* DragQueryPoint(HDROP, LPPOINT): in the client area */
        uint8_t h[8];
        *v = 0;
        if (win_ne_read(a->ctx, w(a, 8), 0, h, sizeof(h)) && put(a, dw(a, 4), h + 2, 4))
            *v = !rd16(h + 6);
        return 1;
    }
    case 36: {                          /* ExtractAssociatedIcon(HINSTANCE, LPSTR, LPWORD) */
        char file[128], full[160];
        uint8_t idx[2] = { 0, 0 };
        HICON icon = 0;
        if (str_in(a, dw(a, 8), file, sizeof(file)) && get(a, dw(a, 4), idx, 2)) {
            win_path(file, full, sizeof(full));
            icon = ExtractIconExN(full, rd16(idx));
        }
        if ((uintptr_t)icon <= 1) {     /* none of its own: Windows' application icon */
            icon = LoadIcon(0, IDI_APPLICATION);
            wr16(idx, 0);
            put(a, dw(a, 4), idx, 2);
        }
        *v = (uint16_t)(uintptr_t)icon;
        return 1;
    }
    case 37:                            /* DoEnvironmentSubst(LPSTR, UINT) */
        *v = env_subst(a, u);
        *dword = 1;
        return 1;
    case 38: {                          /* FindEnvironmentString(LPSTR): its value */
        char name[64];
        uint32_t n;
        *v = 0;
        *dword = 1;
        if (!str_in(a, dw(a, 4), name, sizeof(name)) || !name[0])
            return 1;
        char *env = environment(a, u, &n);
        int at = env ? env_find(env, n, name, (uint32_t)strlen(name)) : -1;
        if (at >= 0)
            *v = (uint32_t)u->env << 16 | (uint16_t)at;
        win_free(env);
        return 1;
    }
    case 102:                           /* RegisterShellHook(HWND, UINT): no shell's hooks */
        *v = 0;
        return 1;
    case 262:                           /* DriveType(drive): 0 = A: */
        *v = GetDriveType((int)dw(a, 4));
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- KERNEL */

/* GetModuleName(HINSTANCE, LPSTR, int): the module's name (its file's) */
static int module_name(args *a, uint32_t *v)
{
    win_ne_guest_api_context *c = a->ctx;
    uint16_t h = w(a, 10), cap = w(a, 4);
    uint32_t far = dw(a, 6);
    char name[16];
    const char *s = h >= 0xf000u ? win_ne_guest_module_name(h - 0xf000u) : NULL;
    if (!s) {
        const char *path = win_ne_guest_path_of(c->image, h, c->path);
        s = path ? path : "";
        for (const char *p = s; *p; p++)
            if (*p == '\\' || *p == '/' || *p == ':')
                s = p + 1;
    }
    unsigned n = 0;
    while (s[n] && s[n] != '.' && n < sizeof(name) - 1) {
        name[n] = (char)(s[n] >= 'a' && s[n] <= 'z' ? s[n] - 32 : s[n]);
        n++;
    }
    name[n] = 0;
    *v = 0;
    if (cap && n) {
        if (n >= cap)
            name[n = cap - 1u] = 0;
        *v = put(a, far, name, n + 1);
    }
    return 1;
}

/* GetCodeInfo(FARPROC, SEGINFO FAR *): the segment of a procedure (or of
 * a module's segment, its number in the offset): where it is, its size */
static int code_info(args *a, uint32_t *v)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    uint32_t proc = dw(a, 8);
    uint16_t sel = (uint16_t)(proc >> 16);
    win_ne_guest_segment *s = win_ne_guest_segment_of(g, sel);
    if (!s && (uint16_t)proc) {         /* a module (an instance) and a segment number */
        int d = win_ne_guest_dll_of(g, sel);
        if (d >= 0 && (uint16_t)proc <= g->dlls[d].count)
            s = &g->dll_segments[g->dlls[d].first + (uint16_t)proc - 1u];
        else if (d < 0 && (uint16_t)proc <= g->info.segment_count)
            s = &g->segments[(uint16_t)proc];
    }
    uint8_t info[16];
    memset(info, 0, sizeof(info));
    *v = 0;
    if (!s)
        return 1;
    uint32_t size = s->size > 0xffffu ? 0xffffu : s->size;
    wr16(info + 2, (uint16_t)size);     /* offSegment 0: no file sectors */
    wr16(info + 4, s->flags);           /* flags (NE's: 1 data) */
    wr16(info + 6, (uint16_t)size);     /* cbAlloc */
    wr16(info + 8, s->selector);        /* h */
    wr16(info + 10, 9);                 /* alignShift: NE's 512-byte sectors */
    *v = put(a, dw(a, 4), info, sizeof(info));
    return 1;
}

static __attribute__((noinline)) int kernel_more(args *a, ne_user *u, uint16_t service,
                                                uint32_t *v, int *dword)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    static const uint8_t reg_op[12] = { 7, 1, 2, 4, 3, 5, 0, 0, 6, 0, 0, 0 };
    if (service >= 216 && service <= 227) {   /* KERNEL's registry: SHELL's */
        *dword = 1;
        *v = 0;
        return reg_op[service - 216] ? ne_shell_registry_service(a, reg_op[service - 216], v)
                                     : service == 227;   /* RegFlushKey: written */
    }
    switch (service) {
    case 26:                            /* GlobalFreeAll(owner): the blocks go with the task */
    case 324: case 325:                 /* LogError, LogParamError: no debugging kernel */
    case 338:                           /* HasGPHandler(FARPROC): none */
    case 355: case 356:                 /* Get/SetWinDebugInfo: the retail kernel */
    case 118: case 119:                 /* GetTaskQueueDS/ES: no queue of the program's */
        *v = 0;
        return 1;
    case 27:
        return module_name(a, v);
    case 28:                            /* GlobalMasterHandle: no arena of the program's */
    case 110:                           /* PatchCodeHandle(HANDLE): nothing to patch */
    case 130:                           /* FileCDR(proc): the previous one, none */
    case 157:                           /* GetCurPID(DWORD) */
    case 311:                           /* GetSetKernelDOSProc(proc): the previous one */
    case 318:                           /* FatalExitHook(proc) */
    case 341:                           /* ToolHelpHook(proc) */
        *v = 0;
        *dword = 1;
        return 1;
    case 38: {                          /* SetTaskSignalProc(HTASK, proc): the previous one */
        *v = u->signal_proc;
        u->signal_proc = dw(a, 4);
        *dword = 1;
        return 1;
    }
    case 99:                            /* GetLPErrMode: SetErrorMode's */
        *v = u->error_mode;
        return 1;
    case 103:                           /* NetBIOSCall: no network */
        *v = 0xfb;                      /* (AL: NetBIOS not loaded) */
        return 1;
    case 104:
        return code_info(a, v);
    case 140: {                         /* SetSigHandler(proc, LPVOID old, LPVOID old action,
                                         * action, type): none before */
        uint8_t z[4] = { 0, 0, 0, 0 };
        if (dw(a, 12))
            put(a, dw(a, 12), z, 4);
        if (dw(a, 8))
            put(a, dw(a, 8), z, 2);
        *v = 0;
        return 1;
    }
    case 180: {                         /* LongPtrAdd(DWORD ptr, DWORD add): the selector's
                                         * base moved, the pointer as it was */
        uint32_t ptr = dw(a, 8), add = dw(a, 4), base, limit;
        int code;
        uint16_t sel = (uint16_t)(ptr >> 16);
        if (win_ne_guest_selector_get(g, sel, &base, &limit, &code))
            win_ne_guest_selector_set(g, sel, base + add, limit, code);
        *v = ptr;
        *dword = 1;
        return 1;
    }
    }
    return 0;
}

/* ---------------------------------------------------------------- USER */

static __attribute__((noinline)) int user_more(args *a, uint16_t service, uint32_t *v,
                                              int *dword)
{
    if (service >= 0x1000u + 501 && service <= 0x1000u + 532) {   /* WNet*: no network */
        *v = service == 0x1000u + 513 ? 0 : 1;  /* WNetGetCaps 0; WN_NOT_SUPPORTED */
        return 1;
    }
    switch (service) {
    case WIN_NE_SERVICE_KEYBD_EVENT: case WIN_NE_SERVICE_MOUSE_EVENT:
        input_event(a, service);
        *v = w(a, 0);
        return 1;
    case 0x1151: {                      /* GetMouseEventProc: mouse_event's thunk */
        *v = win_ne_guest_thunk((win_ne_guest_image *)a->ctx->image,
                                WIN_NE_SERVICE_MOUSE_EVENT);
        *dword = 1;
        return 1;
    }
    case 0x111d: {                      /* SetDeskWallPaper(LPCSTR): -1 WIN.INI's again */
        char s[128];
        uint32_t far = dw(a, 4);
        *v = (uint16_t)SetDeskWallPaper(far == 0xffffffffu || !far ? (LPCSTR)(intptr_t)-1 :
                                        str_in(a, far, s, sizeof(s)) ? s : "");
        return 1;
    }
    case 0x11e3:                        /* SystemParametersInfo */
        *v = (uint16_t)spi_service(a);
        return 1;
    case 0x10b8:                        /* QuerySendMessage(h, h, h, LPMSG): not sent */
    case 0x113a:                        /* SignalProc(...): no signals */
        *v = 0;
        return 1;
    case 0x10d8:                        /* UserSeeUserDo(what, ...): nothing */
    case 0x1141:                        /* SetEventHook(proc): the previous one, none */
        *v = 0;
        *dword = 1;
        return 1;
    case 0x1111:                        /* ControlPanelInfo(what, data, LPSTR) */
        *v = 0;
        return 1;
    }
    return driver_service(a, service, v, dword);
}

int ne_more_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    if (service >= SH && service < SH + 0x1000u)
        return shell_more(a, u, service - SH, v, dword);
    if (service >= TH && service < TH + 0x100u) {
        switch (service - TH) {
        case 81: case 82:               /* TaskSetCSIP, TaskGetCSIP: not of a running task */
            *v = 0;
            *dword = 1;
            return 1;
        case 83:                        /* TaskSwitch(HTASK, DWORD): not to there */
            *v = 0;
            return 1;
        }
        return 0;
    }
    if (service < 0x1000u)
        return kernel_more(a, u, service, v, dword);
    if (service < 0x2000u)
        return user_more(a, service, v, dword);
    return 0;
}

#endif
