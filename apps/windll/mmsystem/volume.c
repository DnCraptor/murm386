/*
 * volume.c - MMSYSTEM's volumes: the wave output's and the MIDI output's
 * (waveOut/midiOutGet/SetVolume) and a master volume as the auxiliary
 * device 0 (auxGetNumDevs 1, "Master Volume"), each a DWORD (the left
 * channel low, the right high, 0 to FFFFh), kept in SYSTEM.INI [volume].
 * The drivers have no volume of their own: the samples are scaled as they
 * go to the wave driver (left and right), MIDI's notes by their velocity
 * (the two sides' mean), each by its volume and the master's; a muted
 * one (the mixer's mute, mixer.c) is silent.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"

#define NE (__win_dll_ne)
#define WAVE_HANDLE 0x6e01u
#define MIDI_HANDLE_BASE 0x6f01u
#define AUXCAPS_AUXIN 2u
#define AUXCAPS_VOLUME 1u
#define AUXCAPS_LRVOLUME 2u

static void wr16_(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32_(uint8_t *p, uint32_t v) { wr16_(p, (uint16_t)v); wr16_(p + 2, (uint16_t)(v >> 16)); }

UINT WINAPI winn_wave_num(void);
UINT WINAPI winn_midi_num(void);

enum { VOL_MASTER, VOL_WAVE, VOL_MIDI, VOL_COUNT };
static const char *const keys[VOL_COUNT] = { "Master", "Wave", "MIDI" };
static const char *const mute_keys[VOL_COUNT] = { "MasterMute", "WaveMute", "MIDIMute" };
static DWORD vol[VOL_COUNT];
static int muted[VOL_COUNT];
static int loaded;

void winn_mixer_changed(int line, int control);     /* mixer.c: its windows told */

static void load(void)
{
    char s[16];
    if (loaded)
        return;
    loaded = 1;
    for (int i = 0; i < VOL_COUNT; i++) {
        DWORD v = 0;
        GetPrivateProfileString("volume", keys[i], "FFFFFFFF", s, sizeof(s), "SYSTEM.INI");
        for (const char *p = s; *p; p++) {
            int d = *p >= '0' && *p <= '9' ? *p - '0' : (*p | 0x20) >= 'a' && (*p | 0x20) <= 'f' ?
                    (*p | 0x20) - 'a' + 10 : -1;
            if (d < 0)
                break;
            v = v << 4 | (DWORD)d;
        }
        vol[i] = v;
        muted[i] = GetPrivateProfileInt("volume", mute_keys[i], 0, "SYSTEM.INI") != 0;
    }
}

static void set(int i, DWORD v)
{
    char s[16];
    load();
    if (vol[i] == v)
        return;
    vol[i] = v;
    wsprintf(s, "%08lX", (unsigned long)v);
    WritePrivateProfileString("volume", keys[i], s, "SYSTEM.INI");
    winn_mixer_changed(i, 0);
}

/* the mixer's (mixer.c): a line's (VOL_*) volume, mute */
DWORD winn_volume_get(int i)
{
    load();
    return i >= 0 && i < VOL_COUNT ? vol[i] : 0;
}

void winn_volume_set(int i, DWORD v)
{
    if (i >= 0 && i < VOL_COUNT)
        set(i, v);
}

int winn_mute_get(int i)
{
    load();
    return i >= 0 && i < VOL_COUNT && muted[i];
}

void winn_mute_set(int i, int m)
{
    load();
    if (i < 0 || i >= VOL_COUNT || muted[i] == !!m)
        return;
    muted[i] = !!m;
    WritePrivateProfileString("volume", mute_keys[i], m ? "1" : "0", "SYSTEM.INI");
    winn_mixer_changed(i, 1);
}

/* a side's (0 left, 1 right) of a volume times the master's: 0 to 65535 */
static unsigned side(int i, int right)
{
    load();
    if (muted[i] || muted[VOL_MASTER])
        return 0;
    uint32_t a = right ? vol[i] >> 16 : vol[i] & 0xffffu;
    uint32_t m = right ? vol[VOL_MASTER] >> 16 : vol[VOL_MASTER] & 0xffffu;
    return (unsigned)(a * m / 65535u);
}

void winn_volume_pcm(uint8_t *p, unsigned n, unsigned bits, unsigned channels)
{
    unsigned l = side(VOL_WAVE, 0), r = side(VOL_WAVE, 1);
    if (l == 65535u && r == 65535u)
        return;
    if (bits == 16) {
        for (unsigned i = 0; i + 1 < n; i += 2) {
            unsigned g = channels == 2 && (i / 2) % 2 ? r : l;
            int32_t s = (int16_t)(p[i] | p[i + 1] << 8);
            s = s * (int32_t)g >> 16;   /* 32767 * 65535 fits: no 64-bit division */
            p[i] = (uint8_t)s;
            p[i + 1] = (uint8_t)((uint16_t)s >> 8);
        }
    } else {
        for (unsigned i = 0; i < n; i++) {
            unsigned g = channels == 2 && i % 2 ? r : l;
            int32_t s = (int32_t)p[i] - 128;
            p[i] = (uint8_t)((s * (int32_t)g >> 16) + 128);
        }
    }
}

/* a note's velocity scaled (a note on stays one: 1 at the least) */
uint32_t winn_volume_midi(uint32_t packed)
{
    if ((packed & 0xf0u) != 0x90u || !(packed & 0x7f0000u))
        return packed;
    unsigned g = (side(VOL_MIDI, 0) + side(VOL_MIDI, 1)) / 2u;
    if (g == 65535u)
        return packed;
    if (!g)                             /* silent: as a note off (velocity 0) */
        return packed & 0xffffu;
    uint32_t v = (packed >> 16 & 0x7fu) * g / 65535u;
    if (!v)                             /* a note on of velocity 0 is a note off */
        v = 1;
    return (packed & 0xffffu) | v << 16;
}

/* ---------------------------------------------------------------- the API */

static int wave_ok(UINT id) { return (id == 0 || id == 0xffffu || id == WAVE_HANDLE) && winn_wave_num(); }
static int midi_ok(UINT id)
{
    return id == 0xffffu ? winn_midi_num() != 0 :
           id >= MIDI_HANDLE_BASE ? id < MIDI_HANDLE_BASE + 4u : id < winn_midi_num();
}

MMRESULT WINAPI waveOutGetVolume(UINT id, LPDWORD v)
{
    if (!wave_ok(id)) return MMSYSERR_BADDEVICEID;
    if (!v) return MMSYSERR_INVALPARAM;
    load();
    *v = vol[VOL_WAVE];
    return 0;
}

MMRESULT WINAPI waveOutSetVolume(UINT id, DWORD v)
{
    if (!wave_ok(id)) return MMSYSERR_BADDEVICEID;
    set(VOL_WAVE, v);
    return 0;
}

MMRESULT WINAPI midiOutGetVolume(UINT id, LPDWORD v)
{
    if (!midi_ok(id)) return MMSYSERR_BADDEVICEID;
    if (!v) return MMSYSERR_INVALPARAM;
    load();
    *v = vol[VOL_MIDI];
    return 0;
}

MMRESULT WINAPI midiOutSetVolume(UINT id, DWORD v)
{
    if (!midi_ok(id)) return MMSYSERR_BADDEVICEID;
    set(VOL_MIDI, v);
    return 0;
}

UINT WINAPI auxGetNumDevs(void) { return 1; }

MMRESULT WINAPI auxGetVolume(UINT id, LPDWORD v)
{
    if (id != 0 && id != 0xffffu) return MMSYSERR_BADDEVICEID;
    if (!v) return MMSYSERR_INVALPARAM;
    load();
    *v = vol[VOL_MASTER];
    return 0;
}

MMRESULT WINAPI auxSetVolume(UINT id, DWORD v)
{
    if (id != 0 && id != 0xffffu) return MMSYSERR_BADDEVICEID;
    set(VOL_MASTER, v);
    return 0;
}

/* Win16 AUXCAPS: wMid, wPid, vDriverVersion, szPname[32], wTechnology,
 * dwSupport: 44 bytes */
static void aux_caps(uint8_t *b)
{
    memset(b, 0, 44);
    wr16_(b + 4, 0x0100u);
    lstrcpy((char *)b + 6, "Master Volume");
    wr16_(b + 38, AUXCAPS_AUXIN);
    b[40] = AUXCAPS_VOLUME | AUXCAPS_LRVOLUME;
}

/* auxGetDevCaps for native callers: the same 44 bytes */
MMRESULT WINAPI auxGetDevCaps(UINT id, void *caps, UINT size)
{
    uint8_t b[44];
    if (id != 0 && id != 0xffffu) return MMSYSERR_BADDEVICEID;
    if (!caps) return MMSYSERR_INVALPARAM;
    aux_caps(b);
    memcpy(caps, b, size < sizeof(b) ? size : sizeof(b));
    return 0;
}

/* Win16's: 211, 212 midiOut, 415, 416 waveOut Get/SetVolume(UINT, LPDWORD
 * | DWORD); 350-353 aux */
int winn_volume_ne_call(win_dll_call *c, uint16_t o, uint32_t *v)
{
    DWORD x = 0;
    uint8_t b[44];
    MMRESULT r;
    switch (o) {
    case 211: case 415: case 352:           /* Get(UINT, LPDWORD) */
        r = o == 211 ? midiOutGetVolume(NE->arg16(c, 8), &x) :
            o == 415 ? waveOutGetVolume(NE->arg16(c, 8), &x) : auxGetVolume(NE->arg16(c, 8), &x);
        if (!r) {
            wr32_(b, x);
            if (!(NE->arg32(c, 4) >> 16) || !NE->write(c, NE->arg32(c, 4), b, 4))
                r = MMSYSERR_INVALPARAM;
        }
        break;
    case 212: case 416: case 353:           /* Set(UINT, DWORD) */
        r = o == 212 ? midiOutSetVolume(NE->arg16(c, 8), NE->arg32(c, 4)) :
            o == 416 ? waveOutSetVolume(NE->arg16(c, 8), NE->arg32(c, 4)) :
                       auxSetVolume(NE->arg16(c, 8), NE->arg32(c, 4));
        break;
    case 350:
        *v = auxGetNumDevs();
        return WINDLL_WORD;
    case 351: {                             /* auxGetDevCaps(UINT, LPAUXCAPS, UINT) */
        UINT id = NE->arg16(c, 10), size = NE->arg16(c, 4);
        if (id != 0 && id != 0xffffu)
            r = MMSYSERR_BADDEVICEID;
        else if (!(NE->arg32(c, 6) >> 16))
            r = MMSYSERR_INVALPARAM;
        else {
            aux_caps(b);
            r = NE->write(c, NE->arg32(c, 6), b, size < sizeof(b) ? size : sizeof(b)) ? 0 :
                MMSYSERR_INVALPARAM;
        }
        break;
    }
    default:
        return WINDLL_NONE;
    }
    *v = r;
    return WINDLL_WORD;
}
