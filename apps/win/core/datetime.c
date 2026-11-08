/*
 * datetime.c - the date and time of day (native extension of module WINN).
 *
 * Win16 programs ask DOS for these (INT 21h 2Ah/2Ch, through DOS3Call or
 * their C runtime); a native application gets them here. The host build
 * uses the C library's local time.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

#if defined(__arm__) || defined(__thumb__)
#include <dos.h>
#else
#include <time.h>
#endif

void WINAPI GetLocalTimeN(SYSTEMTIME_N *t)
{
    if (!t)
        return;
    memset(t, 0, sizeof(*t));
#if defined(__arm__) || defined(__thumb__)
    union REGS r;
    /* the date twice around the time: a read at midnight stays consistent */
    for (int tries = 0; tries < 2; tries++) {
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x2A;                          /* get date */
        int386(0x21, &r, &r);
        WORD day = r.h.dl;
        t->wYear = r.w.cx;
        t->wMonth = r.h.dh;
        t->wDay = r.h.dl;
        t->wDayOfWeek = r.h.al;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x2C;                          /* get time */
        int386(0x21, &r, &r);
        t->wHour = r.h.ch;
        t->wMinute = r.h.cl;
        t->wSecond = r.h.dh;
        t->wMilliseconds = (WORD)(r.h.dl * 10);
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x2A;
        int386(0x21, &r, &r);
        if (r.h.dl == day)
            break;
    }
#else
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    t->wYear = (WORD)(tm.tm_year + 1900);
    t->wMonth = (WORD)(tm.tm_mon + 1);
    t->wDay = (WORD)tm.tm_mday;
    t->wDayOfWeek = (WORD)tm.tm_wday;
    t->wHour = (WORD)tm.tm_hour;
    t->wMinute = (WORD)tm.tm_min;
    t->wSecond = (WORD)tm.tm_sec;
#endif
}
