/*
 * mpumidi.c - WINN MIDI output via murm386's emulated MPU-401 UART.
 * This is the same port used by DOS programs and feeds the emulator's
 * built-in General MIDI synth. System Exclusive is not implemented there.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stddef.h>
#include "windows.h"
#include "windll.h"
#include "midi_driver.h"
#include "sound_hw.h"
#include "conio.h"

#define MPU_DATA 0x330u
#define MPU_CMD  0x331u

static int mpu_present(void)
{
    return (sound_hw_mask() & SOUND_HW_MPU401) != 0;
}
static int mpu_open(void)
{
    if (!mpu_present()) return 0;
    /* Initialize/reset the emulated UART; the synth is retained by PC. */
    outp(MPU_CMD, 0xffu);
    (void)inp(MPU_DATA);
    outp(MPU_CMD, 0x3fu);
    (void)inp(MPU_DATA);
    return 1;
}
static void mpu_close(void) { }

static int mpu_msg(uint32_t packed)
{
    uint8_t status = (uint8_t)packed;
    if (status < 0x80u || status >= 0xf0u)
        return 0;
    unsigned bytes = (status & 0xf0u) == 0xc0u || (status & 0xf0u) == 0xd0u ? 2u : 3u;
    /* The emulator's UART parser expects complete messages. */
    for (unsigned i = 0; i < bytes; i++)
        outp(MPU_DATA, (uint8_t)(packed >> (8u * i)));
    return 1;
}
static void mpu_reset(void)
{
    for (unsigned ch = 0; ch < 16; ch++)
        mpu_msg((uint32_t)(0xb0u | ch) | 123u << 8);
}

static const win_midi_driver driver = {
    WIN_MIDI_DRIVER_VERSION, sizeof(win_midi_driver),
    mpu_present, mpu_open, mpu_close, mpu_msg, mpu_reset,
    "murm386 General MIDI", 2, 32, 32, 0xffff,
};
static const win_midi_driver *WINAPI entry(void) { return &driver; }
static const win_dll_export exports[] = {
    { 1, 0, "WinMidiDriver", (const void *)entry },
};
static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "MPUMIDI",
    exports, sizeof(exports) / sizeof(exports[0]), NULL, NULL, NULL, NULL,
};
const win_dll_info *WinDllInit(void) { return &info; }
