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
static long host_offset;            /* host tests: SetLocalTimeN moves this clock */
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
    time_t now = time(NULL) + host_offset;
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

/* Sets the date and the time of day (DOS INT 21h 2Bh/2Dh): FALSE if DOS
 * refuses them. */
BOOL WINAPI SetLocalTimeN(const SYSTEMTIME_N *t)
{
    if (!t || t->wMonth < 1 || t->wMonth > 12 || t->wDay < 1 || t->wDay > 31 ||
        t->wHour > 23 || t->wMinute > 59 || t->wSecond > 59)
        return FALSE;
#if defined(__arm__) || defined(__thumb__)
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x2B;
    r.w.cx = t->wYear;
    r.h.dh = (uint8_t)t->wMonth;
    r.h.dl = (uint8_t)t->wDay;
    int386(0x21, &r, &r);
    if (r.h.al)
        return FALSE;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x2D;
    r.h.ch = (uint8_t)t->wHour;
    r.h.cl = (uint8_t)t->wMinute;
    r.h.dh = (uint8_t)t->wSecond;
    r.h.dl = (uint8_t)(t->wMilliseconds / 10);
    int386(0x21, &r, &r);
    return r.h.al == 0;
#else
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = t->wYear - 1900;
    tm.tm_mon = t->wMonth - 1;
    tm.tm_mday = t->wDay;
    tm.tm_hour = t->wHour;
    tm.tm_min = t->wMinute;
    tm.tm_sec = t->wSecond;
    tm.tm_isdst = -1;
    time_t want = mktime(&tm);
    if (want == (time_t)-1)
        return FALSE;
    host_offset = (long)(want - time(NULL));
    return TRUE;
#endif
}
