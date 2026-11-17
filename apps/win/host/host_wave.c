/*
 * host_wave.c - HOSTWAVE.DLL, the host's wave output driver (wave_driver.h):
 * a device that plays its blocks one after another in the script's time
 * (a block lasts its bytes at the rate), as SBWAVE.DLL queues them: four
 * at the most, 8192 bytes in all, the
 * samples appended to HOSTWAVE.RAW, each block on stderr ("wave block 4096
 * at 120, gap 0": a gap is time between blocks with none to play). A
 * program names it in SYSTEM.INI ([drivers] wave=HOSTWAVE.DLL); none of
 * the host's runs does otherwise.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include "windows.h"
#include "windll.h"
#include "wave_driver.h"

#define BLOCKS 4
#define RING 8192u

static int opened, count, paused;
static DWORD ends[BLOCKS];              /* the queued blocks' ends, the first playing */
static unsigned sizes[BLOCKS], bytes;
static DWORD last_end;

static int present(void) { return 1; }
static int open_(void) { opened = 1; count = 0; bytes = 0; return 1; }
static void close_(void) { opened = 0; count = 0; }

static int submit(const uint8_t *p, unsigned n, unsigned rate, unsigned bits, unsigned channels)
{
    if (!opened || count == BLOCKS || !n || !rate || n > RING - bytes)
        return 0;
    DWORD now = GetTickCount(), start = count ? ends[count - 1] : now;
    DWORD ms = (DWORD)((uint64_t)n * 1000u / ((uint64_t)rate * (bits / 8u) * channels));
    fprintf(stderr, "wave block %u at %lu, gap %ld\n", n, (unsigned long)start,
            count || !last_end ? 0L : (long)(start - last_end));
    sizes[count] = n;
    bytes += n;
    ends[count++] = start + ms;
    FILE *f = fopen("HOSTWAVE.RAW", "ab");
    if (f) {
        fwrite(p, 1, n, f);
        fclose(f);
    }
    return 1;
}

static int finished(void)
{
    DWORD now = GetTickCount();
    int done = 0;
    while (count && !paused && (long)(now - ends[0]) >= 0) {
        last_end = ends[0];
        bytes -= sizes[0];
        for (int i = 1; i < count; i++) {
            ends[i - 1] = ends[i];
            sizes[i - 1] = sizes[i];
        }
        count--;
        done++;
    }
    return done;
}

static void stop(void) { count = 0; bytes = 0; last_end = 0; }
static void pause_(int p) { paused = p; }
static int room(void)
{
    unsigned n = (RING - bytes) / 4096u;
    return opened ? (int)(n < (unsigned)(BLOCKS - count) ? n : (unsigned)(BLOCKS - count)) : 0;
}

static const win_wave_driver driver = {
    WIN_WAVE_DRIVER_VERSION, sizeof(win_wave_driver), present, open_, close_, submit,
    finished, stop, pause_, 4096, room,
};

static const win_wave_driver *WinWaveDriver(void) { return &driver; }

static const win_dll_export exports[] = {
    { 1, 0, "WinWaveDriver", (const void *)WinWaveDriver },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "HOSTWAVE",
    exports, sizeof(exports) / sizeof(exports[0]), NULL, NULL, NULL, NULL,
};

const win_dll_info *hostwave_WinDllInit(void) { return &info; }
