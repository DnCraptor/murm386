/*
 * winmod_shim.c - linked into driver modules: the core services the module
 * received at load time, under the names drivers use when built in.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "winmod.h"

const win_module_host *winmod_host;

void win_ddi_key_event(int vk, int scancode, int down)
{
    if (winmod_host && winmod_host->key_event)
        winmod_host->key_event(vk, scancode, down);
}

void win_ddi_mouse_event(int x, int y, unsigned buttons)
{
    if (winmod_host && winmod_host->mouse_event)
        winmod_host->mouse_event(x, y, buttons);
}

void win_ddi_mouse_track(int x, int y)
{
    if (winmod_host && winmod_host->size >= sizeof(win_module_host) && winmod_host->mouse_track)
        winmod_host->mouse_track(x, y);
}
