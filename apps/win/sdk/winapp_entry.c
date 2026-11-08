/*
 * winapp_entry.c - start of a Windows application module (linked into
 * every application instead of crt0).
 *
 * WINN.EXE calls __ez_start on the application's own task stack with the
 * export tables; the API stubs (winapi_stubs.S) call through them.
 *
 * __ez_start begins with a branch over WINAPP_MAGIC: that word, at entry+4,
 * is how WINN.EXE (WinExec, IsWinAppN) tells a Windows application from a
 * DOS program or a driver before running it. The next word, entry+8, is the
 * address of the application's winapp_info (its name and icon), which
 * winapp.cmake generates as winapp_info.h from DESCRIPTION and ICON.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdio.h>
#include "windows.h"
#include "winmod.h"
#include "winapp_info.h"            /* generated: WINAPP_DESCRIPTION, WINAPP_ICON_* */

const void *const *const *__win_api;

/* winapp_info with room for this program's icons */
const struct {
    uint32_t magic;
    uint16_t size, icon_count;
    char     description[40];
    winapp_icon icon[WINAPP_ICON_SLOTS];
} __winapp_info = {
    WINAPP_INFO_MAGIC, WINAPP_INFO_SIZE(WINAPP_ICON_SLOTS), WINAPP_ICON_COUNT, WINAPP_DESCRIPTION,
    { WINAPP_ICONS },
};

_Static_assert(sizeof(__winapp_info) == WINAPP_INFO_SIZE(WINAPP_ICON_SLOTS), "winapp_info layout");

int __winapp_start(const win_app_host *host);

__asm__(
    "    .syntax unified\n"
    "    .thumb\n"
    "    .section .text.__ez_start,\"ax\",%progbits\n"
    "    .balign 4\n"
    "    .global __ez_start\n"
    "    .type __ez_start, %function\n"
    "    .thumb_func\n"
    "__ez_start:\n"
    "    b    1f\n"
    "    .hword 0\n"
    "    .word 0x4150574E\n"                /* WINAPP_MAGIC */
    "    .word __winapp_info\n"             /* its name and icon */
    "1:  ldr  r3, =__winapp_start\n"       /* r0 (the host table) untouched */
    "    bx   r3\n"
    "    .ltorg\n"
    "    .size __ez_start, .-__ez_start\n"
    "    .text\n");

_Static_assert(WINAPP_MAGIC == 0x4150574Eu, "signature in __ez_start");

int __winapp_start(const win_app_host *host)
{
    /* started from DOS directly: the argument is argc, not a pointer */
    if ((uintptr_t)host < 0x10000u) {
        puts("This program requires Native Windows (run it with WINN.EXE).");
        return 1;
    }
    if (host->magic != WINAPP_MAGIC || host->version != WINAPP_VERSION)
        return 1;
    __win_api = host->modules;
    return WinMain(host->hinstance, 0, (LPSTR)host->cmdline, host->show);
}
