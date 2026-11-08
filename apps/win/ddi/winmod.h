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
 * Win16 ordinal (m: WINMOD_KERNEL, WINMOD_GDI, WINMOD_USER, WINMOD_COMMDLG),
 * or the WINN ordinal of a native extension (m: WINMOD_WINN).
 */
#define WINAPP_MAGIC   0x4150574Eu        /* "NWPA" */
#define WINAPP_VERSION 1

enum { WINMOD_KERNEL, WINMOD_GDI, WINMOD_USER, WINMOD_WINN, WINMOD_COMMDLG, WINMOD_COUNT };

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

/*
 * What a Windows 3.x executable says about itself in its resources, before
 * it runs: its name for the Program Manager and its icons. __ez_start holds
 * the address of this block after WINAPP_MAGIC (entry+8); the SDK makes it
 * from win_app(... DESCRIPTION "..." ICON first.icon second.icon ...).
 *
 * The first icon is the program's; LoadIcon(hInstance, MAKEINTRESOURCE(n))
 * gives the n-th (1 the first), ExtractIconExN(file, n - 1) reads it from
 * the file. An icon is 32 x 32 in the 16 VGA colours: xor_bits 4 bits a
 * pixel (left pixel in the high nibble), and_bits 1 bit a pixel (1:
 * transparent), both top row first.
 */
#define WINAPP_INFO_MAGIC 0x4941574Eu     /* "NWAI" */
#define WINAPP_MAX_ICONS  16

typedef struct winapp_icon {
    uint8_t  xor_bits[512];
    uint8_t  and_bits[128];
} winapp_icon;

typedef struct winapp_info {
    uint32_t magic;                 /* WINAPP_INFO_MAGIC */
    uint16_t size;                  /* of the whole block, icons included */
    uint16_t icon_count;
    char     description[40];
    winapp_icon icon[1];            /* icon_count of them (at least room for one) */
} winapp_info;

#define WINAPP_INFO_SIZE(n) (sizeof(winapp_info) + ((n) > 1 ? (n) - 1 : 0) * sizeof(winapp_icon))

#endif /* MURM_WINMOD_H */
