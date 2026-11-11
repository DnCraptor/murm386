/*
 * vga800.c - display driver module: VBE 102h, 800x600 in 16 colours.
 *
 * The driver itself is the core's planar one (drivers/dos/planar16.h: an
 * 8 bpp shadow copy, converted to the four VGA planes on every flush, the
 * colours mapped to the standard 16), here at 800x600; this file gives its
 * hardware and makes it a module.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "winddi.h"
#include "winmod.h"

#define PLANAR_W      800
#define PLANAR_H      600
#define PLANAR_MODE   0x102
#define PLANAR_DRIVER win_disp_vga800
#define PLANAR_NAME   "VGA800"
#define PLANAR_DESC   "VGA 800x600 16 colours (VGA800.DRV)"

/* ---------------------------------------------------------------- hardware */

#ifdef WINDRV_HOST
#include "planar16_host.h"         /* host test: fake BIOS and video memory */
#else
#include "planar16_dos.h"          /* the BIOS, the emulator's video memory */
#endif

/* ---------------------------------------------------------------- driver */

#include "planar16.h"

/* ---------------------------------------------------------------- module */

#ifndef WINDRV_HOST
#include <stdio.h>
extern const win_module_host *winmod_host;

static const win_module_info info = {
    WINMOD_VERSION, sizeof(win_module_info), "VGA800", &win_disp_vga800, NULL,
};

const win_module_info *__ez_start(const win_module_host *host)
{
    /* started from DOS: the argument is argc, not a pointer */
    if ((uintptr_t)host < 0x10000u) {
        puts("VGA800.DRV is a display driver for WINN.EXE (see SYSTEM.INI).");
        return NULL;
    }
    if (host->version != WINMOD_VERSION)
        return NULL;
    winmod_host = host;
    return &info;
}
#endif
