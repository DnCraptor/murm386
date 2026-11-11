/*
 * sound.h - Windows 3.x's sound driver (SOUND.DLL, a native DLL of WINN's:
 * apps/windll/sound): one voice, the PC speaker's, of notes and tones
 * queued and played; for native callers, who take its functions with
 * LoadLibrary("SOUND") and GetProcAddress.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_SOUND_H
#define WINN_SOUND_H

#include "windows.h"

#define S_QUEUEEMPTY    0
#define S_THRESHOLD     1
#define S_ALLTHRESHOLD  2

#define S_NORMAL        0
#define S_LEGATO        1
#define S_STACCATO      2

#define S_SERDVNA       (-1)        /* the device is another task's */
#define S_SEROFM        (-2)
#define S_SERMACT       (-3)
#define S_SERQFUL       (-4)        /* the queue is full */
#define S_SERBDNT       (-5)        /* a note */
#define S_SERDLN        (-6)        /* a note's length */
#define S_SERDCC        (-7)
#define S_SERDTP        (-8)        /* a tempo */
#define S_SERDVL        (-9)        /* a volume */
#define S_SERDMD        (-10)       /* a mode */
#define S_SERDSH        (-11)
#define S_SERDPT        (-12)       /* a pitch */
#define S_SERDFQ        (-13)       /* a frequency */
#define S_SERDDR        (-14)       /* a duration */
#define S_SERDSR        (-15)
#define S_SERDST        (-16)       /* a state */

/* the functions' types, for GetProcAddress (their names: Windows') */
typedef int (WINAPI *OPENSOUND)(void);
typedef void (WINAPI *CLOSESOUND)(void);
typedef int (WINAPI *SETVOICENOTE)(int voice, int value, int length, int cdots);
typedef int (WINAPI *SETVOICEACCENT)(int voice, int tempo, int volume, int mode, int pitch);
typedef int (WINAPI *SETVOICESOUND)(int voice, DWORD freq, int duration);
typedef int (WINAPI *STARTSOUND)(void);
typedef int (WINAPI *STOPSOUND)(void);
typedef int (WINAPI *WAITSOUNDSTATE)(int state);
typedef int (WINAPI *COUNTVOICENOTES)(int voice);

#endif /* WINN_SOUND_H */
