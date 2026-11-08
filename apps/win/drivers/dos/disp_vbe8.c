/*
 * disp_vbe8.c - display driver: VBE 8 bpp packed mode, framebuffer taken
 * directly from the emulator's video memory.
 *
 * The VBE facade of the native BIOS keeps 8 bpp modes linear in gfx_buffer
 * (pitch = width), so the core draws straight into what the renderer scans
 * out and flush() has nothing to do. The mode is chosen from a table by the
 * caller's hint; everything that fits the 256 KB video memory works.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "dos.h"
#include "conio.h"
#include "dos_video.h"
#include "winddi.h"

static const struct { uint16_t mode; int w, h; } vbe8_modes[] = {
    { 0x100, 640, 400 },     /* default: largest 8 bpp mode that fits 256 KB */
    { 0x013, 320, 200 },     /* plain VGA, always present */
};

static win_surface surf;
static uint8_t saved_mode;

static int bios_set_mode(uint16_t mode)
{
    union REGS r = { 0 };
    if (mode < 0x100) {
        r.w.ax = mode;
        int386(0x10, &r, &r);
        return 0;
    }
    r.w.ax = 0x4F02;
    r.w.bx = mode;
    int386(0x10, &r, &r);
    return r.w.ax == 0x004F ? 0 : -1;
}

static int vbe8_enable(int want_w, int want_h, int want_bpp)
{
    (void)want_bpp;
    union REGS r = { 0 };
    r.h.ah = 0x0F;
    int386(0x10, &r, &r);
    saved_mode = r.h.al & 0x7F;

    uint32_t size = 0;
    uint8_t *fb = dos_video_get_buffer(&size);
    if (!fb)
        return -1;
    for (unsigned i = 0; i < sizeof(vbe8_modes) / sizeof(vbe8_modes[0]); i++) {
        int w = vbe8_modes[i].w, h = vbe8_modes[i].h;
        if ((want_w && w != want_w) || (want_h && h != want_h))
            continue;
        if ((uint32_t)(w * h) > size)
            continue;
        if (bios_set_mode(vbe8_modes[i].mode))
            continue;
        surf.bits = fb;
        surf.width = w;
        surf.height = h;
        surf.pitch = w;
        surf.bpp = 8;
        return 0;
    }
    return -1;
}

static void vbe8_disable(void)
{
    bios_set_mode(saved_mode ? saved_mode : 3);
}

static const win_surface *vbe8_surface(void) { return &surf; }

static void vbe8_set_palette(int first, int count, const uint8_t *rgb)
{
    outp(0x3C8, (uint8_t)first);
    for (int i = 0; i < 3 * count; i++)
        outp(0x3C9, (uint8_t)(rgb[i] >> 2));       /* 6-bit DAC */
}

static void vbe8_flush(int l, int t, int r, int b) { (void)l; (void)t; (void)r; (void)b; }

const win_display_driver win_disp_vbe8 = {
    WINDDI_VERSION, "VBE8", vbe8_enable, vbe8_disable, vbe8_surface,
    vbe8_set_palette, vbe8_flush,
    "VBE 640x400 256 colours (built in)", 256,
};
