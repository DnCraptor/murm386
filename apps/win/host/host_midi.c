/*
 * host_midi.c - HOSTMIDI.DLL, the host's MIDI output driver (midi_driver.h):
 * its messages on stderr ("midi 90 3C 7F at 120"), the time the script's.
 * A program names it in SYSTEM.INI ([drivers] midi=HOSTMIDI.DLL); none of
 * the host's runs does otherwise.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include "windows.h"
#include "windll.h"
#include "midi_driver.h"

static int present(void) { return 1; }
static int open_(void) { return 1; }
static void close_(void) {}

static int short_msg(uint32_t m)
{
    fprintf(stderr, "midi %02X %02X %02X at %lu\n", (unsigned)(m & 0xff),
            (unsigned)(m >> 8 & 0xff), (unsigned)(m >> 16 & 0xff),
            (unsigned long)GetTickCount());
    return 1;
}

static void reset(void) { fprintf(stderr, "midi reset\n"); }

static const win_midi_driver driver = {
    WIN_MIDI_DRIVER_VERSION, sizeof(win_midi_driver), present, open_, close_, short_msg,
    reset, "Host MIDI", 1, 16, 16, 0xffff,
};

static const win_midi_driver *WinMidiDriver(void) { return &driver; }

static const win_dll_export exports[] = {
    { 1, 0, "WinMidiDriver", (const void *)WinMidiDriver },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "HOSTMIDI",
    exports, sizeof(exports) / sizeof(exports[0]), NULL, NULL, NULL, NULL,
};

const win_dll_info *hostmidi_WinDllInit(void) { return &info; }
