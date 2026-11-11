/*
 * sound_dos.c - the PC speaker: PIT channel 2 at the tone's divisor, the
 * speaker gated on through port 61h (bits 0 and 1), off for silence.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "winddi.h"
#include "conio.h"

#define PIT_HZ 1193182u

static int enable(void) { return 0; }

static void tone(unsigned hz)
{
    uint8_t gate = inp(0x61);
    if (!hz) {
        outp(0x61, (uint8_t)(gate & ~3u));
        return;
    }
    unsigned div = PIT_HZ / hz;
    if (div > 0xFFFFu)
        div = 0xFFFFu;
    outp(0x43, 0xB6);               /* channel 2, both bytes, square wave */
    outp(0x42, (uint8_t)div);
    outp(0x42, (uint8_t)(div >> 8));
    outp(0x61, (uint8_t)(gate | 3u));
}

static void disable(void) { tone(0); }

const win_sound_driver win_sound_dos = {
    WINDDI_VERSION, "SPEAKER", enable, disable, tone,
};
