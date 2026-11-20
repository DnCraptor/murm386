/*
 * windll.h - native DLLs: Windows' own libraries (LZEXPAND, VER, ...) as
 * modules of their own, loaded when a program needs one and unloaded when
 * none does any more, as Windows 3.x does with its DLLs.
 *
 * A native DLL is an EZ image (built by sdk/windll.cmake, written to
 * apps/compiled/<NAME>.DLL) whose __ez_start begins with a branch over
 * WINDLL_MAGIC and is called with the core's win_dll_host: the API tables
 * (the DLL calls KERNEL, GDI, USER through them, as an application does)
 * and what it needs for Win16 programs' calls. It returns its
 * win_dll_info: its name and its exports, each with the Win16 ordinal and
 * name programs import it by, the bytes of PASCAL arguments a Win16
 * caller's RETF releases, the native function.
 *
 * As in Windows there is one instance of a DLL: one copy of its data for
 * all the tasks that use it. What it keeps for a task it keeps by the task
 * (GetCurrentTask) and lets go in task_end.
 *
 * The core looks for <NAME>.DLL in the Windows directory (WINN.EXE's),
 * then the system one; a file there that is not a native DLL (Windows' own
 * NE file) is left to the Win16 loader. On the host the DLLs are built
 * into the test program (win_dll_builtins).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINDLL_H
#define MURM_WINDLL_H

#include <stddef.h>
#include <stdint.h>

#define WINDLL_MAGIC   0x4C44574Eu       /* "NWDL" */
#define WINDLL_VERSION 1

/* A Win16 program's call of a DLL's function, as the core gives it to the
 * DLL: its arguments (the last at off 4, as on its stack after CS:IP) and
 * its memory (far pointers selector:offset). A pointer outside the
 * program's memory fails the call (the program is told). */
typedef struct win_dll_call win_dll_call;

typedef struct win_dll_ne {
    uint32_t size;                  /* sizeof(win_dll_ne) */
    uint16_t (*arg16)(win_dll_call *c, unsigned off);
    uint32_t (*arg32)(win_dll_call *c, unsigned off);
    int (*read)(win_dll_call *c, uint32_t far, void *dst, uint32_t n);
    int (*write)(win_dll_call *c, uint32_t far, const void *src, uint32_t n);
    /* a zero-ended string of the program's, cut to cap; 0:0 fails */
    int (*string)(win_dll_call *c, uint32_t far, char *dst, uint32_t cap);
    /* a FAR PASCAL procedure (selector:offset) of the Win16 program of the
     * task, called with n words (in the order a PASCAL caller pushes them):
     * its DX:AX; 0 if the task is no Win16 program's (or it has ended) */
    uint32_t (*call)(uint16_t task, uint32_t proc, const uint16_t *words, int n);
    /* the memory of the Win16 program of a task outside its calls (a
     * modeless dialog's structure of its): 0 if it fails */
    int (*task_read)(uint16_t task, uint32_t far, void *dst, uint32_t n);
    int (*task_write)(uint16_t task, uint32_t far, const void *src, uint32_t n);
    /* a global memory block of the task's program (zeroed, its handle
     * the selector: far = handle << 16), as GlobalAlloc gives it the
     * program; 0 if none (size covers it: older cores have none) */
    uint16_t (*task_global)(uint16_t task, uint32_t size);
    /* the task's program a Win32 one: its "far" pointers flat addresses
     * (no segment's 64K bounds); size covers it */
    int (*task_flat)(uint16_t task);
    /* a window procedure of the Win16 program of the task (a dialog's hook)
     * called with a message as USER sends it: lParam's native structures
     * the program's while it runs; its DX:AX (size covers it) */
    uint32_t (*task_message)(uint16_t task, uint32_t proc, uint16_t hwnd, uint16_t msg,
                             uint16_t wp, uint32_t lp);
    /* a dialog of the task's Win16 program's template (inst's resource
     * named tpl, a far pointer or a number; global: the global block tpl)
     * made with a native dialog procedure: as DialogBoxIndirectN (its
     * window, modeless); -1 if it fails (size covers it) */
    int (*task_dialog)(uint16_t task, uint16_t inst, uint32_t tpl, int global, uint16_t owner,
                       void *proc, int modeless);
    /* a block of n bytes of the task's program's memory, zeroed, its "far"
     * pointer (a Win16 program's global block, offset 0; a Win32
     * program's heap's): 0 if none; freed with task_free or as the
     * program ends (size covers them) */
    uint32_t (*task_alloc)(uint16_t task, uint32_t n);
    void (*task_free)(uint16_t task, uint32_t far);
    /* Far-call thunk of an already loaded native DLL export in a Win16 task. */
    uint32_t (*task_proc)(uint16_t task, const char *module, uint16_t ordinal);
    /* Size of a Win16 task's GlobalAlloc block, or 0 for an invalid handle. */
    uint32_t (*task_global_size)(uint16_t task, uint16_t handle);
    /* WinExec can return on timeout before the task reaches its message loop. */
    int (*task_starting)(uint16_t task);
} win_dll_ne;

/* Size of the original interface through task_free. */
#define WIN_DLL_NE_BASE_SIZE offsetof(win_dll_ne, task_proc)
#define WIN_DLL_NE_PROC_SIZE offsetof(win_dll_ne, task_global_size)
#define WIN_DLL_NE_GLOBAL_SIZE offsetof(win_dll_ne, task_starting)

typedef struct win_dll_export {
    uint16_t ordinal;               /* Win16's */
    uint16_t argbytes;              /* a Win16 caller's PASCAL arguments */
    const char *name;
    const void *native;             /* GetProcAddress's for native callers; NULL: none */
} win_dll_export;

/* ne_call's results: not the DLL's (the program is told it is not
 * supported), AX, DX:AX */
enum { WINDLL_NONE = 0, WINDLL_WORD = 1, WINDLL_DWORD = 2 };

typedef struct win_dll_info {
    uint32_t version;               /* WINDLL_VERSION */
    uint32_t size;                  /* sizeof(win_dll_info) */
    const char *name;               /* the module's: "LZEXPAND" */
    const win_dll_export *exports;
    uint32_t export_count;
    /* a Win16 program's call of the export with this ordinal */
    int (*ne_call)(win_dll_call *c, uint16_t ordinal, uint32_t *value);
    /* a task ended: what the DLL kept for it (its files are closed already) */
    void (*task_end)(uint16_t task);
    /* the last user is gone: before the DLL is unloaded (WEP) */
    void (*term)(void);
    /* a registered message (C000h on) that one of its windows sends a
     * Win16 program's window, lParam a native structure of the DLL's (the
     * Find dialog's FINDREPLACE): the far pointer the program gets (its
     * own, brought up to date), 0 for none of the DLL's; NULL: none */
    uint32_t (*ne_lparam)(uint32_t msg, uint32_t lparam);
} win_dll_info;

typedef struct win_dll_host {
    uint32_t magic;                 /* WINDLL_MAGIC */
    uint32_t version;               /* WINDLL_VERSION */
    uint32_t size;                  /* sizeof(win_dll_host) */
    const void *const *const *modules;   /* the API tables (winmod.h's WINMOD_*) */
    const win_dll_ne *ne;
} win_dll_host;

typedef const win_dll_info *(*win_dll_entry)(const win_dll_host *host);

/* In the DLL (sdk/windll_entry.c sets it): the core's help with Win16
 * programs' calls; the DLL itself defines
 *     const win_dll_info *WinDllInit(void);
 * which returns its info. */
extern const win_dll_ne *__win_dll_ne;
const win_dll_info *WinDllInit(void);

/* On the host: the DLLs built in, by name (host_dlls.c) */
typedef struct win_dll_builtin {
    const char *name;
    const win_dll_info *(*init)(void);
} win_dll_builtin;

#endif /* MURM_WINDLL_H */
