/*
 * wave_driver.h - native WINN wave output driver interface.
 * Installed by SYSTEM.INI [drivers] wave=<module>.DLL.
 *
 * Output is unsigned 8-bit mono PCM. MMSYSTEM converts Win16 WAVEFORMAT
 * samples to that form before passing fixed-size blocks to the driver.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_WAVE_DRIVER_H
#define WINN_WAVE_DRIVER_H

#include <stdint.h>

#define WIN_WAVE_DRIVER_VERSION 1u

typedef struct win_wave_driver {
    uint32_t version, size;
    int (*present)(void);
    int (*open)(void);
    void (*close)(void);
    /* One non-auto-initialize DMA block at a time, length <= max_bytes. */
    int (*submit)(const uint8_t *samples, unsigned length, unsigned rate);
    int (*finished)(void);       /* 1 when block ended (also acknowledges IRQ) */
    void (*stop)(void);
    void (*pause)(int paused);
    unsigned max_bytes;
} win_wave_driver;

typedef const win_wave_driver *(*win_wave_driver_entry)(void);

#endif /* WINN_WAVE_DRIVER_H */
