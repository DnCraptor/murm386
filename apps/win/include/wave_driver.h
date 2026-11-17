/*
 * wave_driver.h - native WINN wave output driver interface.
 * Installed by SYSTEM.INI [drivers] wave=<module>.DLL.
 *
 * Output is the PCM format selected at waveOutOpen: unsigned 8-bit or
 * signed little-endian 16-bit, mono or interleaved stereo. No conversion.
 *
 * The blocks are queued: while one plays the next is submitted (room()
 * says how many it takes now), so they play without a gap between them;
 * finished() counts those played. Once none is left the driver stops by
 * itself, the next submit starts it again.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_WAVE_DRIVER_H
#define WINN_WAVE_DRIVER_H

#include <stdint.h>

#define WIN_WAVE_DRIVER_VERSION 3u

typedef struct win_wave_driver {
    uint32_t version, size;
    int (*present)(void);
    int (*open)(void);
    void (*close)(void);
    /* A block after those queued, length in bytes (<= max_bytes), in the
     * format of those (a new one once they are played). bits: 8/16,
     * channels: 1/2; sample format follows Win16 PCM. 0: not taken. */
    int (*submit)(const uint8_t *samples, unsigned length, unsigned rate,
                  unsigned bits, unsigned channels);
    int (*finished)(void);       /* the blocks played since it was asked (acknowledges IRQs) */
    void (*stop)(void);          /* now: the blocks queued are dropped */
    void (*pause)(int paused);
    unsigned max_bytes;
    int (*room)(void);           /* the blocks submit takes now */
} win_wave_driver;

typedef const win_wave_driver *(*win_wave_driver_entry)(void);

#endif /* WINN_WAVE_DRIVER_H */
