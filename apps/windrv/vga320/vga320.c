/*
 * VGA320.DRV - 320x240x256 native WINN display, backed by VBE 100h.
 *
 * The firmware has no 320x240 8-bpp mode. Present a 320x240 GDI surface
 * and expand only dirty rectangles into its existing 640x400x8 framebuffer.
 * A 533x400 centred viewport preserves the source's 4:3 aspect ratio on
 * the 640x480 HDMI output. Optionally use real VGA 13h at 320x200x8.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "winddi.h"

#ifndef VGA320_HOST_TEST
#include <stdio.h>
#include "winmod.h"
#include "dos.h"
#include "dos_video.h"
#include "conio.h"
#endif

#define VGA320_W           320
#define VGA320_H           240
#define VGA320_DOOM_H      200
#define VGA320_PHYS_W      640
#define VGA320_PHYS_H      400
#define VGA320_VIEW_W      533
#define VGA320_VIEW_X      ((VGA320_PHYS_W - VGA320_VIEW_W) / 2)
#define VGA320_VBE_MODE    0x100u
#define VGA320_VGA_MODE    0x013u

#ifdef VGA320_HOST_TEST
/* Host test substitutes the same four hardware operations. */
extern int vga320_test_get_mode(void);
extern int vga320_test_set_mode(unsigned mode);
extern uint8_t *vga320_test_video_buffer(uint32_t *size);
extern void vga320_test_palette(int first, int count, const uint8_t *rgb);
static int hw_get_mode(void) { return vga320_test_get_mode(); }
static int hw_set_mode(unsigned mode) { return vga320_test_set_mode(mode); }
static uint8_t *hw_buffer(uint32_t *size) { return vga320_test_video_buffer(size); }
static void hw_palette(int first, int count, const uint8_t *rgb)
{ vga320_test_palette(first, count, rgb); }
#else
static int hw_get_mode(void)
{
    union REGS r = { 0 };
    r.h.ah = 0x0F;
    int386(0x10, &r, &r);
    return r.h.al & 0x7F;
}

static int hw_set_mode(unsigned mode)
{
    union REGS r = { 0 };
    if (mode < 0x100u) {
        r.w.ax = (uint16_t)mode;
        int386(0x10, &r, &r);
        return 0;
    }
    r.w.ax = 0x4F02;
    r.w.bx = (uint16_t)mode;
    int386(0x10, &r, &r);
    return r.w.ax == 0x004F ? 0 : -1;
}

static uint8_t *hw_buffer(uint32_t *size)
{
    return dos_video_get_buffer(size);
}

static void hw_palette(int first, int count, const uint8_t *rgb)
{
    outp(0x3C8, (uint8_t)first);
    for (int i = 0; i < count * 3; ++i)
        outp(0x3C9, (uint8_t)(rgb[i] >> 2));
}
#endif

static win_surface screen;
static uint8_t *phys;
static uint8_t *shadow;
static int saved_mode;
static int enabled;
static uint16_t x_source[VGA320_VIEW_W];

/* Pixel x of the 533-wide viewport comes from GDI pixel floor(x*320/533). */
static void init_x_source(void)
{
    for (int x = 0; x < VGA320_VIEW_W; ++x)
        x_source[x] = (uint16_t)(x * VGA320_W / VGA320_VIEW_W);
}

/* A destination pixel belongs to source x >= n at this boundary. */
static int dest_x(int n)
{
    return (n * VGA320_VIEW_W + VGA320_W - 1) / VGA320_W;
}

static int dest_y(int n)
{
    return (n * VGA320_PHYS_H + VGA320_H - 1) / VGA320_H;
}

static int vga320_enable(int want_w, int want_h, int want_bpp)
{
    const int h = want_h ? want_h : VGA320_H;
    const int native_200 = h == VGA320_DOOM_H;
    const uint32_t required = native_200 ?
        VGA320_W * VGA320_DOOM_H : VGA320_PHYS_W * VGA320_PHYS_H;
    uint32_t available = 0;
    uint8_t *fb;

    if (enabled || (want_w && want_w != VGA320_W) ||
        (h != VGA320_H && h != VGA320_DOOM_H) ||
        (want_bpp && want_bpp != 8))
        return -1;
    fb = hw_buffer(&available);
    if (!fb || available < required)
        return -1;

    if (!native_200) {
        shadow = (uint8_t *)malloc(VGA320_W * VGA320_H);
        if (!shadow)
            return -1;
    }
    saved_mode = hw_get_mode();
    if (hw_set_mode(native_200 ? VGA320_VGA_MODE : VGA320_VBE_MODE)) {
        free(shadow);
        shadow = NULL;
        return -1;
    }

    phys = fb;
    if (native_200) {
        screen.bits = phys;
    } else {
        screen.bits = shadow;
        memset(shadow, 0, VGA320_W * VGA320_H);
        memset(phys, 0, VGA320_PHYS_W * VGA320_PHYS_H);
        init_x_source();
    }
    screen.width = VGA320_W;
    screen.height = h;
    screen.pitch = VGA320_W;
    screen.bpp = 8;
    enabled = 1;
    return 0;
}

static void vga320_disable(void)
{
    if (!enabled)
        return;
    hw_set_mode((unsigned)(saved_mode ? saved_mode : 3));
    free(shadow);
    shadow = NULL;
    phys = NULL;
    memset(&screen, 0, sizeof(screen));
    enabled = 0;
}

static const win_surface *vga320_surface(void)
{
    return enabled ? &screen : NULL;
}

static void vga320_set_palette(int first, int count, const uint8_t *rgb)
{
    if (!enabled || !rgb || first < 0 || first >= 256 || count <= 0)
        return;
    if (count > 256 - first)
        count = 256 - first;
    hw_palette(first, count, rgb);
}

static void vga320_flush(int left, int top, int right, int bottom)
{
    if (!enabled || !shadow) /* mode 13h is directly mapped to the hardware */
        return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > VGA320_W) right = VGA320_W;
    if (bottom > VGA320_H) bottom = VGA320_H;
    if (right <= left || bottom <= top)
        return;

    /* Inverse pixel mapping: exactly the physical pixels whose source is
     * inside [left,right) x [top,bottom). Borders remain black. */
    const int x0 = dest_x(left), x1 = dest_x(right);
    const int y0 = dest_y(top), y1 = dest_y(bottom);
    for (int y = y0; y < y1; ++y) {
        uint8_t *dst = phys + y * VGA320_PHYS_W + VGA320_VIEW_X;
        const uint8_t *src = shadow + (y * VGA320_H / VGA320_PHYS_H) * VGA320_W;
        for (int x = x0; x < x1; ++x)
            dst[x] = src[x_source[x]];
    }
}

const win_display_driver win_disp_vga320 = {
    WINDDI_VERSION, "VGA320", vga320_enable, vga320_disable,
    vga320_surface, vga320_set_palette, vga320_flush,
    "VGA 320x240 256 colours (VGA320.DRV)", 256,
};

#ifndef VGA320_HOST_TEST
extern const win_module_host *winmod_host;
static const win_module_info info = {
    WINMOD_VERSION, sizeof(win_module_info), "VGA320", &win_disp_vga320, NULL,
};

const win_module_info *__ez_start(const win_module_host *host)
{
    if ((uintptr_t)host < 0x10000u) {
        puts("VGA320.DRV is a WINN.EXE display module; select it in SYSTEM.INI.");
        return NULL;
    }
    if (host->version != WINMOD_VERSION)
        return NULL;
    winmod_host = host;
    return &info;
}
#endif
