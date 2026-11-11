/*
 * sound.c - SOUND.DLL, WINN's: Windows 3.x's sound driver with one voice,
 * the PC speaker's (WINN's SoundQueueN). A task opens it (OpenSound, one
 * task at a time), queues notes (SetVoiceNote: 1..84, middle C 37, of a
 * length and dots, in the voice's tempo, mode and pitch: SetVoiceAccent)
 * and tones (SetVoiceSound: a frequency for clock ticks), StartSound plays
 * them, StopSound silences them, WaitSoundState waits for the queue to
 * empty, CountVoiceNotes tells the notes left. Volume, envelopes, noise
 * and thresholds are taken and do nothing. A native DLL (ddi/windll.h).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "windows.h"
#include "sound.h"
#include "windll.h"

#define NE (__win_dll_ne)

#define MAX_NOTES 256

typedef struct {
    uint16_t hz, on, off;           /* a tone (0: a rest) for on ms, then off ms silent */
} note;

static uint16_t owner;              /* the task that opened it */
static int capacity = 32;           /* SetVoiceQueueSize's bytes / 6 (Windows' 192) */
static int tempo = 120, mode = S_NORMAL, pitch;
static note pending[MAX_NOTES];     /* queued, not given to the speaker yet */
static int npending;
static int started;
/* the notes given to the speaker: how many of its entries each took */
static uint8_t given[MAX_NOTES];
static int ngiven, given_entries;

/* the notes the speaker still has, from its entries left */
static int speaker_notes(void)
{
    int left = SoundCountN();
    /* the ones it is done with leave from the front */
    while (ngiven && given_entries - given[0] >= left) {
        given_entries -= given[0];
        memmove(given, given + 1, (size_t)(ngiven - 1));
        ngiven--;
    }
    return ngiven;
}

/* as much of what is queued as the speaker takes */
static void feed(void)
{
    if (!started)
        return;
    speaker_notes();
    while (npending && ngiven < MAX_NOTES) {
        note *n = &pending[0];
        int entries = n->off ? 2 : 1;
        if (SoundCountN() + entries > 128)
            break;
        SoundQueueN(n->hz, n->on);
        if (n->off)
            SoundQueueN(0, n->off);
        given[ngiven++] = (uint8_t)entries;
        given_entries += entries;
        memmove(pending, pending + 1, sizeof(note) * (size_t)(npending - 1));
        npending--;
    }
}

static int queue(unsigned hz, unsigned on, unsigned off)
{
    if (npending >= capacity || npending >= MAX_NOTES)
        return S_SERQFUL;
    pending[npending].hz = (uint16_t)hz;
    pending[npending].on = (uint16_t)(on > 65535u ? 65535u : on);
    pending[npending].off = (uint16_t)(off > 65535u ? 65535u : off);
    npending++;
    feed();
    return 0;
}

static void stop(void)
{
    SoundStopN();
    npending = 0;
    ngiven = 0;
    given_entries = 0;
    started = 0;
}

static int WINAPI snd_open(void)
{
    uint16_t me = (uint16_t)GetCurrentTask();
    if (owner && owner != me && IsTask((HTASK)owner))
        return S_SERDVNA;
    owner = me;
    capacity = 32;
    tempo = 120;
    mode = S_NORMAL;
    pitch = 0;
    stop();
    return 1;                       /* its voices */
}

static void WINAPI snd_close(void)
{
    if (owner == (uint16_t)GetCurrentTask()) {
        stop();
        owner = 0;
    }
}

static int WINAPI snd_queue_size(int voice, int bytes)
{
    if (voice != 1)
        return S_SERDVNA;
    if (started && (npending || speaker_notes()))
        return S_SERMACT;
    int n = bytes / 6;
    if (n < 1 || n > MAX_NOTES)
        return S_SEROFM;
    capacity = n;
    return 0;
}

/* C1 .. B1 in thousandths of a hertz; note 1 is C1 */
static const uint32_t octave1[12] = {
    32703, 34648, 36708, 38891, 41203, 43654, 46249, 48999, 51913, 55000, 58270, 61735,
};

static int WINAPI snd_note(int voice, int value, int length, int cdots)
{
    if (voice != 1)
        return S_SERDVNA;
    if (value < 0 || value > 84)
        return S_SERBDNT;
    if (length < 1 || length > 256)
        return S_SERDLN;
    if (cdots < 0 || cdots > 8)
        return S_SERDCC;
    /* a whole note is four quarters; each dot adds half of the one before */
    unsigned d = 4u * 60000u / (unsigned)tempo / (unsigned)length, add = d / 2;
    for (int i = 0; i < cdots; i++, add /= 2)
        d += add;
    unsigned on = mode == S_LEGATO ? d : mode == S_STACCATO ? d * 3 / 4 : d * 7 / 8;
    unsigned hz = 0;
    if (value) {
        int n = value + pitch;
        if (n > 84)
            n = 84;
        hz = (unsigned)((octave1[(n - 1) % 12] << ((n - 1) / 12)) / 1000u);
    }
    return queue(hz, on, d - on);
}

static int WINAPI snd_accent(int voice, int t, int volume, int m, int p)
{
    if (voice != 1)
        return S_SERDVNA;
    if (t < 32 || t > 255)
        return S_SERDTP;
    if (volume < 0 || volume > 255)
        return S_SERDVL;
    if (m < S_NORMAL || m > S_STACCATO)
        return S_SERDMD;
    if (p < 0 || p > 83)
        return S_SERDPT;
    tempo = t;
    mode = m;
    pitch = p;
    return 0;
}

/* a frequency (hertz in the high word) for clock ticks (18.2 a second) */
static int WINAPI snd_sound(int voice, DWORD freq, int ticks)
{
    if (voice != 1)
        return S_SERDVNA;
    unsigned hz = (unsigned)(freq >> 16);
    if (hz && (hz < 19 || hz > 20000))
        return S_SERDFQ;
    if (ticks < 0)
        return S_SERDDR;
    return queue(hz, (unsigned)ticks * 10000u / 182u, 0);
}

static int WINAPI snd_start(void)
{
    started = 1;
    feed();
    return 0;
}

static int WINAPI snd_stop(void)
{
    stop();
    return 0;
}

static int WINAPI snd_wait(int state)
{
    if (state < S_QUEUEEMPTY || state > S_ALLTHRESHOLD)
        return S_SERDST;
    if (state != S_QUEUEEMPTY || !started)
        return 0;
    /* until it is all played: the other tasks run meanwhile */
    for (;;) {
        feed();
        if (!npending && !speaker_notes())
            break;
        SoundWaitN();
    }
    return 0;
}

static int WINAPI snd_count(int voice)
{
    if (voice != 1)
        return S_SERDVNA;
    feed();
    return npending + speaker_notes();
}

static int WINAPI snd_none(void) { return 0; }
static int WINAPI snd_envelope(int voice, int shape, int repeat)
{
    (void)shape; (void)repeat;
    return voice == 1 ? 0 : S_SERDVNA;
}
static int WINAPI snd_noise(int source, int duration) { (void)source; (void)duration; return 0; }
static int WINAPI snd_threshold(int voice, int n) { (void)n; return voice == 1 ? 0 : S_SERDVNA; }
static int WINAPI snd_beep(void)
{
    MessageBeep(0);
    return 0;
}

/* ================================================================ Win16 programs */

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    int r;
    switch (ordinal) {
    case 1: r = snd_open(); break;
    case 2: snd_close(); r = 0; break;
    case 3: r = snd_queue_size((int16_t)NE->arg16(c, 6), (int16_t)NE->arg16(c, 4)); break;
    case 4: r = snd_note((int16_t)NE->arg16(c, 10), (int16_t)NE->arg16(c, 8),
                         (int16_t)NE->arg16(c, 6), (int16_t)NE->arg16(c, 4)); break;
    case 5: r = snd_accent((int16_t)NE->arg16(c, 12), (int16_t)NE->arg16(c, 10),
                           (int16_t)NE->arg16(c, 8), (int16_t)NE->arg16(c, 6),
                           (int16_t)NE->arg16(c, 4)); break;
    case 6: r = snd_envelope((int16_t)NE->arg16(c, 8), 0, 0); break;
    case 7: r = 0; break;                               /* SetSoundNoise */
    case 8: r = snd_sound((int16_t)NE->arg16(c, 10), NE->arg32(c, 6),
                          (int16_t)NE->arg16(c, 4)); break;
    case 9: r = snd_start(); break;
    case 10: r = snd_stop(); break;
    case 11: r = snd_wait((int16_t)NE->arg16(c, 4)); break;
    case 12: r = 0; break;                              /* SyncAllVoices */
    case 13: r = snd_count((int16_t)NE->arg16(c, 4)); break;
    case 14: *v = 0; return WINDLL_DWORD;               /* GetThresholdEvent: none */
    case 15: r = 0; break;                              /* GetThresholdStatus */
    case 16: r = snd_threshold((int16_t)NE->arg16(c, 6), 0); break;
    case 17: r = snd_beep(); break;
    default: return WINDLL_NONE;
    }
    *v = (uint16_t)r;
    return WINDLL_WORD;
}

static void task_end(uint16_t task)
{
    if (owner == task) {
        stop();
        owner = 0;
    }
}

static const win_dll_export exports[] = {
    { 1, 0, "OpenSound", (const void *)snd_open },
    { 2, 0, "CloseSound", (const void *)snd_close },
    { 3, 4, "SetVoiceQueueSize", (const void *)snd_queue_size },
    { 4, 8, "SetVoiceNote", (const void *)snd_note },
    { 5, 10, "SetVoiceAccent", (const void *)snd_accent },
    { 6, 6, "SetVoiceEnvelope", (const void *)snd_envelope },
    { 7, 4, "SetSoundNoise", (const void *)snd_noise },
    { 8, 8, "SetVoiceSound", (const void *)snd_sound },
    { 9, 0, "StartSound", (const void *)snd_start },
    { 10, 0, "StopSound", (const void *)snd_stop },
    { 11, 2, "WaitSoundState", (const void *)snd_wait },
    { 12, 0, "SyncAllVoices", (const void *)snd_none },
    { 13, 2, "CountVoiceNotes", (const void *)snd_count },
    { 14, 0, "GetThresholdEvent", NULL },
    { 15, 0, "GetThresholdStatus", (const void *)snd_none },
    { 16, 4, "SetVoiceThreshold", (const void *)snd_threshold },
    { 17, 0, "DoBeep", (const void *)snd_beep },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "SOUND",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
