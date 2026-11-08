/*
 * winapp_entry.c - start of a Windows application module (linked into
 * every application instead of crt0).
 *
 * WINN.EXE calls __ez_start on the application's own task stack with the
 * export tables; the API stubs (winapi_stubs.S) call through them.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdio.h>
#include "windows.h"
#include "winmod.h"

const void *const *const *__win_api;

int __ez_start(const win_app_host *host)
{
    /* started from DOS directly: the argument is argc, not a pointer */
    if ((uintptr_t)host < 0x10000u) {
        puts("This program requires Microsoft Windows (run it with WINN.EXE).");
        return 1;
    }
    if (host->magic != WINAPP_MAGIC || host->version != WINAPP_VERSION)
        return 1;
    __win_api = host->modules;
    return WinMain(host->hinstance, 0, (LPSTR)host->cmdline, host->show);
}
