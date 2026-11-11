/*
 * disp_vga.c - the built-in display driver: VGA, mode 12h, 640x480 in 16
 * colours, as Windows 3.x's own VGA.DRV (planar16.h: an 8 bpp shadow copy,
 * converted to the four planes as it changes). The default; disp_vbe8.c
 * (640x400 in 256 colours) when it cannot start or the [display] hints
 * ask for another size.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "winddi.h"

#define PLANAR_W      640
#define PLANAR_H      480
#define PLANAR_MODE   0x12
#define PLANAR_DRIVER win_disp_vga
#define PLANAR_NAME   "VGA"
#define PLANAR_DESC   "VGA 640x480 16 colours (built in)"

#ifdef WINDRV_HOST
#include "planar16_host.h"         /* host test: fake BIOS and video memory */
#else
#include "planar16_dos.h"          /* the BIOS, the emulator's video memory */
#endif

#include "planar16.h"
