/*
 * mouse.c - MOUSE.DLL, WINN's: Windows 3.x's mouse driver (MOUSE.DRV) as
 * a program sees it: Inquire (whether there is a mouse, relative, two
 * buttons); Enable and Disable, USER's (the driver's events given to its
 * procedure), take a program's procedure and leave the events USER's: the
 * mouse is WINN's input driver's. A native DLL (ddi/windll.h).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "windows.h"
#include "windll.h"

#define NE (__win_dll_ne)

/* MOUSEINFO: msExists, msRelative (bytes), msNumButtons, msRate,
 * msXThreshold, msYThreshold, msXRes, msYRes, msMouseCommPort (words) */
#define MOUSEINFO_BYTES 16

static void inquire(uint8_t m[MOUSEINFO_BYTES])
{
    memset(m, 0, MOUSEINFO_BYTES);
    m[0] = GetSystemMetrics(SM_MOUSEPRESENT) ? 1 : 0;
    m[1] = 1;                           /* relative: mickeys */
    m[2] = 2;                           /* buttons */
    m[4] = 34;                          /* reports a second at most (Windows') */
}

static WORD WINAPI Inquire(void *info)
{
    if (info)
        inquire((uint8_t *)info);
    return MOUSEINFO_BYTES;
}

static void WINAPI Enable(void *proc) { (void)proc; }
static void WINAPI Disable(void) {}

/* ================================================================ Win16 programs */

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    switch (ordinal) {
    case 1: {                           /* Inquire(LPMOUSEINFO): its size */
        uint8_t m[MOUSEINFO_BYTES];
        inquire(m);
        *v = NE->write(c, NE->arg32(c, 4), m, sizeof(m)) ? MOUSEINFO_BYTES : 0;
        return WINDLL_WORD;
    }
    case 2:                             /* Enable(FARPROC) */
    case 3:                             /* Disable */
        *v = 0;
        return WINDLL_WORD;
    }
    return WINDLL_NONE;
}

static const win_dll_export exports[] = {
    { 1, 4, "Inquire", (const void *)Inquire },
    { 2, 4, "Enable", (const void *)Enable },
    { 3, 0, "Disable", (const void *)Disable },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "MOUSE",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, NULL, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
