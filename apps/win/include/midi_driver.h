/*
 * midi_driver.h - native MIDI output driver ABI for WINN's MMSYSTEM.DLL.
 * Drivers are named in SYSTEM.INI [drivers] midi, midi1, ...
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_MIDI_DRIVER_H
#define WINN_MIDI_DRIVER_H
#include <stdint.h>
#define WIN_MIDI_DRIVER_VERSION 1u

typedef struct win_midi_driver {
    uint32_t version, size;
    int (*present)(void);
    int (*open)(void);
    void (*close)(void);
    int (*short_msg)(uint32_t packed); /* little-endian MIDI status + data */
    void (*reset)(void);              /* all notes off */
    const char *name;
    uint16_t technology, voices, notes, channels;
} win_midi_driver;

typedef const win_midi_driver *(*win_midi_driver_entry)(void);
#endif /* WINN_MIDI_DRIVER_H */
