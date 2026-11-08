/*
 * winsys.c - driver registry and start/stop of the core.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

#define MAX_DRV 4

static const win_display_driver *disp_drv[MAX_DRV];
static const win_input_driver   *input_drv[MAX_DRV];
static int n_disp, n_input;

void win_register_display_driver(const win_display_driver *drv)
{
    if (drv && drv->ddi_version == WINDDI_VERSION && n_disp < MAX_DRV)
        disp_drv[n_disp++] = drv;
}

void win_register_input_driver(const win_input_driver *drv)
{
    if (drv && drv->ddi_version == WINDDI_VERSION && n_input < MAX_DRV)
        input_drv[n_input++] = drv;
}

static const win_display_driver *pick_disp(const char *name)
{
    for (int i = 0; i < n_disp; i++)
        if (!name || !lstrcmpi(disp_drv[i]->name, name))
            return disp_drv[i];
    return n_disp ? disp_drv[0] : NULL;
}

static const win_input_driver *pick_input(const char *name)
{
    for (int i = 0; i < n_input; i++)
        if (!name || !lstrcmpi(input_drv[i]->name, name))
            return input_drv[i];
    return n_input ? input_drv[0] : NULL;
}

/* Bring up display, GDI and cursor. Names select drivers (NULL = first),
 * want_* are mode hints for the display driver. */
int win_init(const char *display, const char *input, int want_w, int want_h, int want_bpp)
{
    memset(&win_sys, 0, sizeof(win_sys));
    win_sys.disp = pick_disp(display);
    win_sys.input = pick_input(input);
    if (!win_sys.disp)
        return -1;
    if (win_sys.disp->enable(want_w, want_h, want_bpp))
        return -2;
    const win_surface *s = win_sys.disp->surface();
    if (!s || s->bpp != 8)
        goto fail;
    win_sys.screen = *s;
    win_sys.screen_ok = 1;
    if (gdi_init())
        goto fail;
    if (win_sys.input && win_sys.input->enable(s->width, s->height))
        win_sys.input = NULL;
    cursor_init();
    return 0;
fail:
    win_sys.disp->disable();
    win_sys.screen_ok = 0;
    return -3;
}

void win_shutdown(void)
{
    if (win_sys.input)
        win_sys.input->disable();
    if (win_sys.disp && win_sys.screen_ok)
        win_sys.disp->disable();
    win_sys.screen_ok = 0;
}
