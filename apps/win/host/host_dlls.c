/*
 * host_dlls.c - the native DLLs (apps/windll) built into the host program,
 * as core/windll.c finds them there instead of their files.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stddef.h>
#include "windll.h"

const win_dll_info *lzexpand_WinDllInit(void);
const win_dll_info *ver_WinDllInit(void);
const win_dll_info *ddeml_WinDllInit(void);
const win_dll_info *mmsystem_WinDllInit(void);
const win_dll_info *commdlg_WinDllInit(void);
const win_dll_info *olecli_WinDllInit(void);

const win_dll_builtin win_dll_builtins[] = {
    { "LZEXPAND", lzexpand_WinDllInit },
    { "VER", ver_WinDllInit },
    { "DDEML", ddeml_WinDllInit },
    { "MMSYSTEM", mmsystem_WinDllInit },
    { "COMMDLG", commdlg_WinDllInit },
    { "OLECLI", olecli_WinDllInit },
    { NULL, NULL },
};
