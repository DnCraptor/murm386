/*
 * host_drivers.c - display and input drivers for running the core on Linux:
 * an in-memory 8 bpp framebuffer saved as PPM, and a scripted input queue.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_drivers.h"

static int fb_w = 640, fb_h = 400;
static uint8_t *fb;
static uint8_t pal[256 * 3];
static win_surface surf;

static int snap_enable(int w, int h, int bpp)
{
    if (w) fb_w = w;
    if (h) fb_h = h;
    (void)bpp;
    fb = calloc((size_t)fb_w, (size_t)fb_h);
    if (!fb)
        return -1;
    surf.bits = fb;
    surf.width = fb_w;
    surf.height = fb_h;
    surf.pitch = fb_w;
    surf.bpp = 8;
    return 0;
}

static void snap_disable(void) { free(fb); fb = NULL; }
static const win_surface *snap_surface(void) { return &surf; }

static void snap_set_palette(int first, int count, const uint8_t *rgb)
{
    memcpy(pal + 3 * first, rgb, (size_t)count * 3);
}

static void snap_flush(int l, int t, int r, int b) { (void)l; (void)t; (void)r; (void)b; }

const win_display_driver host_snap_display = {
    WINDDI_VERSION, "SNAP", snap_enable, snap_disable, snap_surface,
    snap_set_palette, snap_flush,
};

int host_snapshot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", fb_w, fb_h);
    for (int i = 0; i < fb_w * fb_h; i++)
        fwrite(pal + 3 * fb[i], 1, 3, f);
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- input */

#define QMAX 1024
typedef struct { int kind, a, b, c; } hev;
static hev q[QMAX];
static int qh, qt;
static uint32_t now_ms;

void host_push_key(int vk, int sc, int down) { q[qt++ % QMAX] = (hev){ 0, vk, sc, down }; }
void host_push_mouse(int x, int y, unsigned btn) { q[qt++ % QMAX] = (hev){ 1, x, y, (int)btn }; }
void host_advance(uint32_t ms) { now_ms += ms; }
int  host_pending(void) { return qh != qt; }

static int in_enable(int w, int h) { (void)w; (void)h; return 0; }
static void in_disable(void) {}

static void in_poll(void)
{
    while (qh != qt) {
        hev e = q[qh++ % QMAX];
        if (e.kind == 0)
            win_ddi_key_event(e.a, e.b, e.c);
        else
            win_ddi_mouse_event(e.a, e.b, (unsigned)e.c);
    }
}

static uint32_t in_ticks(void) { return now_ms; }
void host_script_idle(uint32_t now) __attribute__((weak));
void host_script_idle(uint32_t now) { (void)now; }
static void in_idle(void)
{
    now_ms += 10;
    host_script_idle(now_ms);
}

const win_input_driver host_script_input = {
    WINDDI_VERSION, "SCRIPT", in_enable, in_disable, in_poll, in_ticks, in_idle,
};
