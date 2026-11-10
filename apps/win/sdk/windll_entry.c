/*
 * windll_entry.c - start of a native DLL (linked into every DLL module
 * instead of crt0, see ddi/windll.h).
 *
 * WINN.EXE calls __ez_start once, as it loads the DLL, with its
 * win_dll_host; the API stubs (winapi_stubs.S) call through its tables
 * from then on. __ez_start begins with a branch over WINDLL_MAGIC (at
 * entry+4): how WINN.EXE tells a native DLL from Windows' own NE DLL of
 * the same name, an application or a driver.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "windows.h"
#include "winmod.h"
#include "windll.h"

const void *const *const *__win_api;
const win_dll_ne *__win_dll_ne;

const win_dll_info *__windll_start(const win_dll_host *host);

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
    "    .word 0x4C44574E\n"                /* WINDLL_MAGIC */
    "1:  ldr  r3, =__windll_start\n"       /* r0 (the host table) untouched */
    "    bx   r3\n"
    "    .ltorg\n"
    "    .size __ez_start, .-__ez_start\n"
    "    .text\n");

_Static_assert(WINDLL_MAGIC == 0x4C44574Eu, "signature in __ez_start");

const win_dll_info *__windll_start(const win_dll_host *host)
{
    /* started from DOS directly: the argument is argc, not a pointer */
    if ((uintptr_t)host < 0x10000u || host->magic != WINDLL_MAGIC ||
        host->version != WINDLL_VERSION || host->size < sizeof(win_dll_host) ||
        !host->ne || host->ne->size < sizeof(win_dll_ne))
        return 0;
    __win_api = host->modules;
    __win_dll_ne = host->ne;
    return WinDllInit();
}
