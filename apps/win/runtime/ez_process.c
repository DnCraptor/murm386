/*
 * ez_process.c - stack sizes requested from the EZ loader.
 *
 * The loader's default native stack is 4 KB. USER nests: a dialog box runs
 * its own message loop, a dialog procedure may open a message box with yet
 * another loop, and each level paints through GDI. A message box opened
 * from a dialog needs over 5 KB on its own (measured on the host build), so
 * ask for 32 KB; the stack lives in PSRAM.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <native_process.h>
#include <dos_api_version.h>

#define WIN_NATIVE_STACK (32u * 1024u)
#define WIN_DOS_STACK    (4u * 1024u)

/* read by elf2ez into the EZ header */
const native_ez_process_requirements __native_ez_process_requirements = {
    WIN_NATIVE_STACK,
    WIN_DOS_STACK,
    DOS_API_VERSION,
};

/* read by the loader when the program is started as an ELF */
static native_dos_process_requirements win_process_requirements = {
    sizeof(native_dos_process_requirements),
    WIN_NATIVE_STACK,
    WIN_DOS_STACK,
    0,
    0
};

native_dos_process_requirements *__native_dos_process_requirements(void)
{
    return &win_process_requirements;
}
