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

/* Every driver module defines
 *     const win_module_info *__ez_start(const win_module_host *host);
 * (not declared here: applications have a different __ez_start). */

/* Core side: load path, return its info (registers nothing by itself).
 * 0 on success, negative on error. */
int win_load_module(const char *path, const win_module_info **info);

/* ---------------------------------------------------------------- applications */

/*
 * A Windows application is an EZ image too. Its __ez_start (from the SDK,
 * sdk/winapp_entry.c) is called on the task's own stack with this table,
 * keeps `modules` for the API stubs and calls WinMain. When DOS starts the
 * file directly, the first argument is argc instead (a small number), and
 * the program only says that it needs Windows.
 *
 * modules[m][ordinal] is the function exported by module m under the
 * Win16 ordinal (m: WINMOD_KERNEL, WINMOD_GDI, WINMOD_USER, WINMOD_WINN for
 * the native extensions).
 */
#define WINAPP_MAGIC   0x4150574Eu        /* "NWPA" */
#define WINAPP_VERSION 1

enum { WINMOD_KERNEL, WINMOD_GDI, WINMOD_USER, WINMOD_WINN, WINMOD_COUNT };

typedef struct win_app_host {
    uint32_t magic;                 /* WINAPP_MAGIC */
    uint32_t version;               /* WINAPP_VERSION */
    uint32_t size;                  /* sizeof(win_app_host) */
    const void *const *const *modules;
    uint16_t hinstance;             /* the task: HINSTANCE */
    uint16_t reserved;
    const char *cmdline;
    int      show;
} win_app_host;

typedef int (*win_app_entry)(const win_app_host *host);

#endif /* MURM_WINMOD_H */
