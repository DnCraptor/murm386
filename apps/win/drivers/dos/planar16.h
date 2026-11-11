/*
 * planar16.h - a display driver for a VGA 16-colour planar mode, the
 * mode's size and number given by the file that includes it (the built-in
 * VGA driver: 12h, 640x480; the VGA800.DRV module: VBE 102h, 800x600).
 *
 * The core draws 8 bpp; the driver keeps an 8 bpp shadow copy of the
 * screen and, on every flush, converts the changed rectangle into the four
 * VGA bit planes. The 256 core colours are mapped to the 16 standard VGA
 * colours by nearest match, so the 20 Windows static colours (16 of which
 * are exactly these) look right.
 *
 * The emulator keeps planar video memory as 32-bit words, one byte per
 * plane: word (y * W / 8 + x / 8) holds 8 pixels of the line, bit 7 the
 * leftmost.
 *
 * Before including it, the file defines
 *     PLANAR_W, PLANAR_H        the mode's size
 *     PLANAR_MODE               its number (BIOS below 100h, VBE from 100h)
 *     PLANAR_DRIVER             the win_display_driver's name in C
 *     PLANAR_NAME, PLANAR_DESC  its name and description
 * and the hardware it works through:
 *     uint8_t *hw_vram(uint32_t *size);    the emulator's video memory
 *     int  hw_get_mode(void), hw_set_mode(int mode);
 *     void hw_set_attr(int reg, int value);       attribute palette -> DAC
 *     void hw_set_dac(int i, int red, int green, int blue);   8 bits each
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "winddi.h"

#define WORDS (PLANAR_W / 8)

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
    if (r > PLANAR_W) r = PLANAR_W;
    if (b > PLANAR_H) b = PLANAR_H;
    uint32_t *planes = (uint32_t *)vram;
    for (int y = t; y < b; y++) {
        const uint8_t *src = surf.bits + y * PLANAR_W + l;
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

static int planar_enable(int want_w, int want_h, int want_bpp)
{
    (void)want_bpp;
    if ((want_w && want_w != PLANAR_W) || (want_h && want_h != PLANAR_H))
        return -1;
    uint32_t size = 0;
    vram = hw_vram(&size);
    if (!vram || size < (uint32_t)WORDS * PLANAR_H * 4)
        return -1;
    if (!surf.bits) {
        surf.bits = (uint8_t *)malloc((size_t)PLANAR_W * PLANAR_H);
        if (!surf.bits)
            return -1;
    }
    memset(surf.bits, 0, (size_t)PLANAR_W * PLANAR_H);
    surf.width = PLANAR_W;
    surf.height = PLANAR_H;
    surf.pitch = PLANAR_W;
    surf.bpp = 8;
    saved_mode = hw_get_mode();
    if (hw_set_mode(PLANAR_MODE))
        return -1;
    for (int i = 0; i < 16; i++) {
        hw_set_attr(i, i);
        hw_set_dac(i, vga16[i][0], vga16[i][1], vga16[i][2]);
    }
    for (int i = 0; i < 256; i++)
        spread[i] = 0;
    convert(0, 0, PLANAR_W, PLANAR_H);
    return 0;
}

static void planar_disable(void)
{
    hw_set_mode(saved_mode ? saved_mode : 3);
}

static const win_surface *planar_surface(void) { return &surf; }

static void planar_set_palette(int first, int count, const uint8_t *rgb)
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
        convert(0, 0, PLANAR_W, PLANAR_H);   /* colours already on screen change too */
}

static void planar_flush(int l, int t, int r, int b) { convert(l, t, r, b); }

const win_display_driver PLANAR_DRIVER = {
    WINDDI_VERSION, PLANAR_NAME, planar_enable, planar_disable, planar_surface,
    planar_set_palette, planar_flush,
    PLANAR_DESC, 16,
};
