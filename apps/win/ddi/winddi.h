/*
 * winddi.h - driver interface of the native Windows core.
 *
 * The core (KERNEL/GDI/USER) knows nothing about video modes or input
 * hardware. A display driver hands it a framebuffer described as a DIB
 * surface (pointer, size, pitch, bits per pixel) and takes palette and
 * cursor requests; an input driver feeds keyboard and mouse events and the
 * time base. Drivers are tables of functions selected by name, so a mode or
 * device change is a driver change, not a core change.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINDDI_H
#define MURM_WINDDI_H

#include <stdint.h>

#define WINDDI_VERSION 1

/* A framebuffer or memory bitmap. bpp: 1 (MSB = leftmost pixel) or 8
 * (palette index). pitch is in bytes. */
typedef struct win_surface {
    uint8_t *bits;
    int      width, height;
    int      pitch;
    int      bpp;
} win_surface;

typedef struct win_display_driver {
    uint32_t    ddi_version;          /* WINDDI_VERSION */
    const char *name;                 /* e.g. "VBE8" */

    /* Switch the display on. want_* are hints (0 = driver default).
     * Returns 0 on success; the surface is valid afterwards. */
    int  (*enable)(int want_w, int want_h, int want_bpp);
    /* Back to the mode the display had before enable(). */
    void (*disable)(void);
    /* The visible framebuffer. */
    const win_surface *(*surface)(void);
    /* Hardware palette, 8 bits per component, rgb[3 * count]. */
    void (*set_palette)(int first, int count, const uint8_t *rgb);
    /* Called after the core changed pixels inside the rectangle
     * (right/bottom exclusive). Drivers that scan the framebuffer directly
     * ignore it; a shadow-buffer driver copies the area out. */
    void (*flush)(int left, int top, int right, int bottom);
} win_display_driver;

/* Key and mouse events go into the core through these calls. */
void win_ddi_key_event(int vk, int scancode, int down);
/* Absolute position in screen pixels; buttons: bit0 left, bit1 right,
 * bit2 middle. */
void win_ddi_mouse_event(int x, int y, unsigned buttons);

typedef struct win_input_driver {
    uint32_t    ddi_version;
    const char *name;
    int      (*enable)(int screen_w, int screen_h);
    void     (*disable)(void);
    /* Collect pending device events and pass them to win_ddi_*_event(). */
    void     (*poll)(void);
    /* Milliseconds since some fixed point. */
    uint32_t (*ticks_ms)(void);
    /* Nothing to do: give the platform a service point (may sleep). */
    void     (*idle)(void);
} win_input_driver;

/* Driver registry: drivers register themselves, the core picks them by
 * name (from configuration) or takes the first one. */
void win_register_display_driver(const win_display_driver *drv);
void win_register_input_driver(const win_input_driver *drv);

#endif /* MURM_WINDDI_H */
