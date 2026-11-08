/*
 * winmod.h - loadable driver modules.
 *
 * A driver module is an EZ image (built by elf2ez like an application, but
 * without crt0) whose entry point __ez_start receives a table of core
 * services and returns the drivers it provides. The core loads it with
 * win_load_module(): reads the image into allocated memory, applies the EZ
 * relocations and calls the entry; nothing in the firmware is involved.
 *
 * Driver sources are the same for built-in and module builds: a module
 * links ddi/winmod_shim.c, which routes win_ddi_*() to the host table.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINMOD_H
#define MURM_WINMOD_H

#include <stdint.h>
#include "winddi.h"

#define WINMOD_VERSION 1

/* What the core gives a module. Append only. */
typedef struct win_module_host {
    uint32_t version;               /* WINMOD_VERSION */
    uint32_t size;                  /* sizeof(win_module_host) */
    void (*key_event)(int vk, int scancode, int down);
    void (*mouse_event)(int x, int y, unsigned buttons);
} win_module_host;

/* What a module gives back. Append only. */
typedef struct win_module_info {
    uint32_t version;               /* WINMOD_VERSION */
    uint32_t size;                  /* sizeof(win_module_info) */
    const char *name;               /* module name, e.g. "VGA800" */
    const win_display_driver *display;   /* or NULL */
    const win_input_driver   *input;     /* or NULL */
} win_module_info;

typedef const win_module_info *(*win_module_entry)(const win_module_host *host);

/* Every module defines this one function. */
const win_module_info *__ez_start(const win_module_host *host);

/* Core side: load path, return its info (registers nothing by itself).
 * 0 on success, negative on error. */
int win_load_module(const char *path, const win_module_info **info);

#endif /* MURM_WINMOD_H */
