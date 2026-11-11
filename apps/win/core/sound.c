/*
 * sound.c - the speaker's notes: a queue of tones (a frequency, 0 for a
 * rest, and how long), the next one started as the one before has had its
 * time (sound_service, from USER's input pump: a program busy without
 * reading its messages holds the note it is on). SOUND.DLL's voice and
 * MessageBeep play through it; without a sound driver every note still
 * takes its time, unheard.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "wincore.h"

#define NOTES 128

static struct { uint16_t hz, ms; } q[NOTES];
static int head, count;
static int playing;                 /* the note at head sounds */
static uint32_t until;

static void tone(unsigned hz)
{
    if (win_sys.sound)
        win_sys.sound->tone(hz);
}

static uint32_t now(void) { return win_sys.input ? win_sys.input->ticks_ms() : 0; }

void sound_service(uint32_t t)
{
    while (count) {
        if (!playing) {
            playing = 1;
            until = t + q[head].ms;
            tone(q[head].hz);
            return;
        }
        if ((int32_t)(t - until) < 0)
            return;
        head = (head + 1) % NOTES;
        count--;
        playing = 0;
        if (!count)
            tone(0);
    }
}

/* Native: a note queued (hz 0: a rest of ms); FALSE when the queue is full */
BOOL WINAPI SoundQueueN(UINT hz, UINT ms)
{
    if (count >= NOTES)
        return FALSE;
    if (hz && (hz < 19 || hz > 20000))
        hz = 0;
    q[(head + count) % NOTES].hz = (uint16_t)hz;
    q[(head + count) % NOTES].ms = (uint16_t)(ms > 65535u ? 65535u : ms);
    count++;
    sound_service(now());
    return TRUE;
}

/* Native: silence, the queue emptied */
void WINAPI SoundStopN(void)
{
    count = 0;
    playing = 0;
    tone(0);
}

/* Native: the notes left (the one sounding among them) */
int WINAPI SoundCountN(void)
{
    sound_service(now());
    return count;
}

/* Native: until the notes are played (the other tasks running meanwhile,
 * the system idle when none has work) */
void WINAPI SoundWaitN(void)
{
    while (SoundCountN()) {
        user_poll_input();
        task_yield();
        if (win_sys.input)
            win_sys.input->idle();
    }
}

/* MessageBeep's: a short beep unless notes are playing */
void sound_beep(void)
{
    if (!count)
        SoundQueueN(880, 120);
}
