/*
 * winsys.c - driver registry and start/stop of the core.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"
#include "winmod.h"

#define MAX_DRV 4

static const win_display_driver *disp_drv[MAX_DRV];
static const win_input_driver   *input_drv[MAX_DRV];
static const win_sound_driver   *sound_drv;
static int n_disp, n_input;

void win_register_display_driver(const win_display_driver *drv)
{
    if (drv && drv->ddi_version >= 1 && drv->ddi_version <= WINDDI_VERSION && n_disp < MAX_DRV)
        disp_drv[n_disp++] = drv;
}

void win_register_input_driver(const win_input_driver *drv)
{
    if (drv && drv->ddi_version >= 1 && drv->ddi_version <= WINDDI_VERSION && n_input < MAX_DRV)
        input_drv[n_input++] = drv;
}

/* the first one registered is the one */
void win_register_sound_driver(const win_sound_driver *drv)
{
    if (drv && drv->ddi_version >= 1 && drv->ddi_version <= WINDDI_VERSION && !sound_drv)
        sound_drv = drv;
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
    win_sys.sound = sound_drv && !sound_drv->enable() ? sound_drv : NULL;
    cursor_init();
    return 0;
fail:
    win_sys.disp->disable();
    win_sys.screen_ok = 0;
    return -3;
}

void win_shutdown(void)
{
    if (win_sys.sound)
        win_sys.sound->disable();
    win_sys.sound = NULL;
    if (win_sys.input)
        win_sys.input->disable();
    if (win_sys.disp && win_sys.screen_ok)
        win_sys.disp->disable();
    win_sys.screen_ok = 0;
}

/* Load the module named by [boot] key in SYSTEM.INI, register what it has. */
static const char *boot_module(const char *key, int want_display)
{
    char name[64], path[128];
    GetPrivateProfileString("boot", key, "", name, sizeof(name), "SYSTEM.INI");
    if (!name[0])
        return NULL;
    win_path(name, path, sizeof(path));
    const win_module_info *mi;
    int rc = win_load_module(path, &mi);
    if (rc) {
        printf("Windows: cannot load %s (%d), using the built-in driver\n", path, rc);
        return NULL;
    }
    if (mi->display)
        win_register_display_driver(mi->display);
    if (mi->input)
        win_register_input_driver(mi->input);
    if (want_display)
        return mi->display ? mi->display->name : NULL;
    return mi->input ? mi->input->name : NULL;
}

/* Start-up used by the runtime: Windows directory from the program path,
 * drivers from SYSTEM.INI [boot] display.drv / input.drv (falling back to
 * the built-in ones: the display ones in disp's order, NULL-ended, the
 * first that starts with the [display] hints, else without them), then
 * GDI and USER. */
int win_boot(const char *exe_path, const win_display_driver *const *disp,
             const win_input_driver *in)
{
    win_set_home(exe_path);
    win_trace_init();
    const char *dname = boot_module("display.drv", 1);
    const char *iname = boot_module("input.drv", 0);
    for (int i = 0; disp && disp[i]; i++)
        win_register_display_driver(disp[i]);
    if (in)
        win_register_input_driver(in);
    int w = (int)GetPrivateProfileInt("display", "width", 0, "SYSTEM.INI");
    int h = (int)GetPrivateProfileInt("display", "height", 0, "SYSTEM.INI");
    int rc = dname ? win_init(dname, iname, w, h, 8) : -1;
    for (int pass = 0; rc && pass < 2; pass++)          /* the built-in ones */
        for (int i = 0; rc && disp && disp[i]; i++)
            rc = win_init(disp[i]->name, iname, pass ? 0 : w, pass ? 0 : h, 8);
    if (rc)
        return rc;
    return user_init();
}

/* Native: description of the display driver in use; returns its colours. */
UINT WINAPI GetDisplayDriverInfoN(LPSTR buf, int size)
{
    const win_display_driver *d = win_sys.disp;
    const char *desc = d ? d->name : "";
    UINT colors = 256;
    if (d && d->ddi_version >= 2) {
        if (d->description)
            desc = d->description;
        if (d->colors)
            colors = (UINT)d->colors;
    }
    if (buf && size > 0)
        lstrcpyn(buf, desc, size);
    return colors;
}
