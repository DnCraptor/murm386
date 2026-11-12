/*
 * oplmidi.c - 9-voice OPL2 FM MIDI output for WINN.
 * Simple GM-program-family timbres (not a complete GM sound bank):
 * writes to the existing YM3812/AdLib emulation, without another synth.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "windll.h"
#include "midi_driver.h"
#include "sound_hw.h"
#include "conio.h"

#define OPL_ADDR 0x388u
#define OPL_DATA 0x389u

static const uint8_t op[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static const uint16_t fnums[12] = { 345, 365, 387, 410, 435, 460,
                                    488, 517, 547, 580, 614, 651 };
/* Per GM family (16 programs): carrier multiple, attack/decay, sustain/release. */
static const uint8_t timbres[8][4] = {
    { 0x01, 0xf2, 0x85, 0x03 }, /* keys */
    { 0x02, 0xf4, 0x68, 0x02 }, /* chromatic/percussion */
    { 0x01, 0xe4, 0x75, 0x04 }, /* organs */
    { 0x03, 0xc4, 0x69, 0x04 }, /* guitars */
    { 0x01, 0xb4, 0x57, 0x03 }, /* bass */
    { 0x02, 0x94, 0x89, 0x03 }, /* strings */
    { 0x02, 0xe3, 0x75, 0x02 }, /* brass/reeds */
    { 0x01, 0xb3, 0x96, 0x01 }, /* synths/effects */
};
typedef struct { uint8_t on, channel, note; uint32_t age; } voice;
static voice voices[9];
static uint8_t programs[16], volume[16];
static uint32_t stamp;
static int opened;

static void reg_write(unsigned reg, unsigned val)
{
    outp(OPL_ADDR, (uint8_t)reg);
    outp(OPL_DATA, (uint8_t)val);
}
static void key_off(unsigned i)
{
    reg_write(0xb0u + i, 0);
    voices[i].on = 0;
}
static void all_off(void)
{
    for (unsigned i = 0; i < 9; i++) key_off(i);
}
static int opl_present(void)
{
    return (sound_hw_mask() & SOUND_HW_ADLIB) != 0;
}
static int opl_open(void)
{
    if (!opl_present() || opened) return 0;
    memset(voices, 0, sizeof(voices));
    memset(programs, 0, sizeof(programs));
    for (unsigned i = 0; i < 16; i++) volume[i] = 100;
    stamp = 0;
    reg_write(0x01, 0x20);  /* enable OPL2 waveforms */
    reg_write(0xbd, 0);     /* nine melodic channels, no rhythm mode */
    all_off();
    opened = 1;
    return 1;
}
static void opl_close(void)
{
    if (opened) all_off();
    opened = 0;
}
static void start_note(unsigned channel, unsigned note, unsigned velocity)
{
    unsigned i, chosen = 0;
    uint32_t oldest = UINT32_MAX;
    for (i = 0; i < 9; i++) {
        if (voices[i].on && voices[i].channel == channel && voices[i].note == note) {
            chosen = i; break;
        }
        if (!voices[i].on) { chosen = i; break; }
        if (voices[i].age < oldest) { oldest = voices[i].age; chosen = i; }
    }
    i = chosen;
    key_off(i);
    unsigned op0 = op[i], op1 = op0 + 3u;
    const uint8_t *t = timbres[(programs[channel] >> 4) & 7u];
    reg_write(0x20u + op0, 0x21u); /* modulator */
    reg_write(0x20u + op1, t[0]);
    reg_write(0x40u + op0, 0x20u);
    /* attenuation is logarithmic on OPL2: 0 loudest, 63 quietest */
    unsigned gain = (velocity * volume[channel]) / 127u;
    unsigned attenuation = 63u - (gain * 63u) / 127u;
    reg_write(0x40u + op1, attenuation);
    reg_write(0x60u + op0, 0xf2u);
    reg_write(0x60u + op1, t[1]);
    reg_write(0x80u + op0, 0x65u);
    reg_write(0x80u + op1, t[2]);
    reg_write(0xe0u + op0, 0);
    reg_write(0xe0u + op1, 0);
    reg_write(0xc0u + i, t[3]);
    int block = (int)(note / 12u) - 1;
    if (block < 0) block = 0;
    if (block > 7) block = 7;
    unsigned fnum = fnums[note % 12u];
    reg_write(0xa0u + i, (uint8_t)fnum);
    reg_write(0xb0u + i, 0x20u | (unsigned)block << 2 | (fnum >> 8));
    voices[i].on = 1;
    voices[i].channel = (uint8_t)channel;
    voices[i].note = (uint8_t)note;
    voices[i].age = ++stamp;
}
static void stop_note(unsigned channel, unsigned note)
{
    for (unsigned i = 0; i < 9; i++)
        if (voices[i].on && voices[i].channel == channel && voices[i].note == note)
            key_off(i);
}
static int opl_msg(uint32_t packed)
{
    unsigned status = packed & 0xffu;
    if (status < 0x80u || status >= 0xf0u || !opened) return 0;
    unsigned ch = status & 15u, cmd = status & 0xf0u;
    unsigned a = (packed >> 8) & 0x7fu, b = (packed >> 16) & 0x7fu;
    switch (cmd) {
    case 0x80: stop_note(ch, a); break;
    case 0x90: if (b) start_note(ch, a, b); else stop_note(ch, a); break;
    case 0xb0:
        if (a == 7) volume[ch] = (uint8_t)b;
        if (a == 120 || a == 123)
            for (unsigned i = 0; i < 9; i++)
                if (voices[i].on && voices[i].channel == ch) key_off(i);
        break;
    case 0xc0: programs[ch] = (uint8_t)a; break;
    default: break; /* remaining channel messages do not affect OPL2 */
    }
    return 1;
}
static void opl_reset(void)
{
    if (opened) all_off();
}

static const win_midi_driver driver = {
    WIN_MIDI_DRIVER_VERSION, sizeof(win_midi_driver),
    opl_present, opl_open, opl_close, opl_msg, opl_reset,
    "murm386 OPL2 FM", 4, 9, 9, 0xffff,
};
static const win_midi_driver *WINAPI entry(void) { return &driver; }
static const win_dll_export exports[] = {
    { 1, 0, "WinMidiDriver", (const void *)entry },
};
static void term(void) { opl_close(); }
static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "OPLMIDI",
    exports, sizeof(exports) / sizeof(exports[0]), NULL, NULL, term, NULL,
};
const win_dll_info *WinDllInit(void) { return &info; }
