/*
 * wave_driver.h - native WINN wave output driver interface.
 * Installed by SYSTEM.INI [drivers] wave=<module>.DLL.
 *
 * Output is the PCM format selected at waveOutOpen: unsigned 8-bit or
 * signed little-endian 16-bit, mono or interleaved stereo. No conversion.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_WAVE_DRIVER_H
#define WINN_WAVE_DRIVER_H

#include <stdint.h>

#define WIN_WAVE_DRIVER_VERSION 2u

typedef struct win_wave_driver {
    uint32_t version, size;
    int (*present)(void);
    int (*open)(void);
    void (*close)(void);
    /* A single-cycle DMA block, length in bytes (<= max_bytes).
     * bits: 8/16, channels: 1/2; sample format follows Win16 PCM. */
    int (*submit)(const uint8_t *samples, unsigned length, unsigned rate,
                  unsigned bits, unsigned channels);
    int (*finished)(void);       /* 1 when block ended (also acknowledges IRQ) */
    void (*stop)(void);
    void (*pause)(int paused);
    unsigned max_bytes;
} win_wave_driver;

typedef const win_wave_driver *(*win_wave_driver_entry)(void);

#endif /* WINN_WAVE_DRIVER_H */
