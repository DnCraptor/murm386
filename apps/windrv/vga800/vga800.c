/*
 * vga800.c - display driver module: VBE 102h, 800x600 in 16 colours.
 *
 * The core draws 8 bpp; this driver keeps an 8 bpp shadow copy of the
 * screen in PSRAM and, on every flush, converts the changed rectangle into
 * the four VGA bit planes. The 256 core colours are mapped to the 16
 * standard VGA colours by nearest match, so the 20 Windows static colours
 * (16 of which are exactly these) look right.
 *
 * The emulator keeps planar video memory as 32-bit words, one byte per
 * plane: word (y * 100 + x / 8) holds 8 pixels of the 800-pixel line,
 * bit 7 = leftmost.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "winddi.h"
#include "winmod.h"

#define W      800
#define H      600
#define WORDS  (W / 8)

/* ---------------------------------------------------------------- hardware */

#ifdef WINDRV_HOST
#include "vga800_host.h"           /* host test: fake BIOS and video memory */
#else
#include "dos.h"
#include "dos_video.h"

static uint8_t *hw_vram(uint32_t *size) { return dos_video_get_buffer(size); }

static int hw_get_mode(void)
{
    union REGS r = { 0 };
    r.h.ah = 0x0F;
    int386(0x10, &r, &r);
    return r.h.al & 0x7F;
}

static int hw_set_mode(int mode)
{
    union REGS r = { 0 };
    if (mode < 0x100) {
        r.w.ax = (uint16_t)mode;
        int386(0x10, &r, &r);
        return 0;
    }
    r.w.ax = 0x4F02;
    r.w.bx = (uint16_t)mode;
    int386(0x10, &r, &r);
    return r.w.ax == 0x004F ? 0 : -1;
}

/* attribute controller palette register -> DAC index */
static void hw_set_attr(int reg, int value)
{
    union REGS r = { 0 };
    r.w.ax = 0x1000;
    r.h.bl = (uint8_t)reg;
    r.h.bh = (uint8_t)value;
    int386(0x10, &r, &r);
}

static void hw_set_dac(int i, int red, int green, int blue)
{
    union REGS r = { 0 };
    r.w.ax = 0x1010;
    r.w.bx = (uint16_t)i;
    r.h.dh = (uint8_t)(red >> 2);
    r.h.ch = (uint8_t)(green >> 2);
    r.h.cl = (uint8_t)(blue >> 2);
    int386(0x10, &r, &r);
}
#endif

/* ---------------------------------------------------------------- driver */

static const uint8_t vga16[16][3] = {
    { 0, 0, 0 },       { 0, 0, 128 },     { 0, 128, 0 },     { 0, 128, 128 },
    { 128, 0, 0 },     { 128, 0, 128 },   { 128, 128, 0 },   { 192, 192, 192 },
    { 128, 128, 128 }, { 0, 0, 255 },     { 0, 255, 0 },     { 0, 255, 255 },
    { 255, 0, 0 },     { 255, 0, 255 },   { 255, 255, 0 },   { 255, 255, 255 },
};

static win_surface surf;
static uint8_t *vram;
static int saved_mode;
/* core colour -> its VGA colour spread over the four plane bytes, bit 0 */
static uint32_t spread[256];

static uint32_t spread_of(int c)
{
    return (uint32_t)(c & 1) | ((uint32_t)((c >> 1) & 1) << 8) |
           ((uint32_t)((c >> 2) & 1) << 16) | ((uint32_t)((c >> 3) & 1) << 24);
}

static int nearest16(int r, int g, int b)
{
    int best = 0;
    long bd = 0x7FFFFFFF;
    for (int i = 0; i < 16; i++) {
        long dr = r - vga16[i][0], dg = g - vga16[i][1], db = b - vga16[i][2];
        long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void convert(int l, int t, int r, int b)
{
    l &= ~7;
    r = (r + 7) & ~7;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > W) r = W;
    if (b > H) b = H;
    uint32_t *planes = (uint32_t *)vram;
    for (int y = t; y < b; y++) {
        const uint8_t *src = surf.bits + y * W + l;
        uint32_t *dst = planes + y * WORDS + l / 8;
        for (int x = l; x < r; x += 8, src += 8) {
            uint32_t v = (spread[src[0]] << 7) | (spread[src[1]] << 6) |
                         (spread[src[2]] << 5) | (spread[src[3]] << 4) |
                         (spread[src[4]] << 3) | (spread[src[5]] << 2) |
                         (spread[src[6]] << 1) | spread[src[7]];
            *dst++ = v;
        }
    }
}

static int vga800_enable(int want_w, int want_h, int want_bpp)
{
    (void)want_bpp;
    if ((want_w && want_w != W) || (want_h && want_h != H))
        return -1;
    uint32_t size = 0;
    vram = hw_vram(&size);
    if (!vram || size < (uint32_t)WORDS * H * 4)
        return -1;
    if (!surf.bits) {
        surf.bits = (uint8_t *)malloc((size_t)W * H);
        if (!surf.bits)
            return -1;
    }
    memset(surf.bits, 0, (size_t)W * H);
    surf.width = W;
    surf.height = H;
    surf.pitch = W;
    surf.bpp = 8;
    saved_mode = hw_get_mode();
    if (hw_set_mode(0x102))
        return -1;
    for (int i = 0; i < 16; i++) {
        hw_set_attr(i, i);
        hw_set_dac(i, vga16[i][0], vga16[i][1], vga16[i][2]);
    }
    for (int i = 0; i < 256; i++)
        spread[i] = 0;
    convert(0, 0, W, H);
    return 0;
}

static void vga800_disable(void)
{
    hw_set_mode(saved_mode ? saved_mode : 3);
}

static const win_surface *vga800_surface(void) { return &surf; }

static void vga800_set_palette(int first, int count, const uint8_t *rgb)
{
    int changed = 0;
    for (int i = 0; i < count; i++) {
        uint32_t s = spread_of(nearest16(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]));
        if (spread[first + i] != s) {
            spread[first + i] = s;
            changed = 1;
        }
    }
    if (changed)
        convert(0, 0, W, H);       /* colours already on screen change too */
}

static void vga800_flush(int l, int t, int r, int b) { convert(l, t, r, b); }

const win_display_driver win_disp_vga800 = {
    WINDDI_VERSION, "VGA800", vga800_enable, vga800_disable, vga800_surface,
    vga800_set_palette, vga800_flush,
    "VGA 800x600 16 colours (VGA800.DRV)", 16,
};

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
