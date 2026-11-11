/*
 * mmsystem.c - MMSYSTEM.DLL, WINN's: Windows' multimedia system with no
 * devices (none of each kind: wave, MIDI, aux, joysticks, mixers; their
 * opens fail, MCI has none, sndPlaySound and PlaySound play nothing), the
 * multimedia timer (timeGetTime, timeSetEvent: its procedure called as its
 * task takes its messages, as a timer's, as often as its period went by
 * since, ten times at most) and mmio's files (unbuffered: RIFF chunks
 * found, made). A native DLL (ddi/windll.h): its functions for native
 * callers, and Win16 programs' calls of them.
 *
 * Its timers and mmio's handles are its own, by task; a task's go at its
 * end.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"

#define TIMERS      32
#define MMIOS       16
#define MMIO_HANDLE 0x6d00u

#define NE (__win_dll_ne)

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* ---------------------------------------------------------------- no devices */

static BOOL WINAPI sndPlaySound(LPCSTR s, UINT flags) { (void)s; (void)flags; return FALSE; }

static BOOL WINAPI PlaySound(LPCSTR s, HINSTANCE m, DWORD flags)
{
    (void)s; (void)m; (void)flags;
    return FALSE;
}

static UINT WINAPI mmsystemGetVersion(void) { return 0x030a; }

static void WINAPI OutputDebugStr(LPCSTR s) { (void)s; }

static UINT WINAPI no_devices(void) { return 0; }

static const char *mm_error(DWORD e)
{
    switch (e) {
    case 0: return "The specified command was carried out.";
    case MMSYSERR_BADDEVICEID: return "The specified device ID is out of range for your system.";
    case MMSYSERR_INVALHANDLE: return "The specified device handle is invalid.";
    case MMSYSERR_NODRIVER: return "There is no driver installed on your system.";
    case MCIERR_DEVICE_NOT_INSTALLED:
        return "The specified device is not installed on the system.";
    default: return "An unknown error has occurred.";
    }
}

static MMRESULT WINAPI error_text(UINT e, LPSTR out, UINT cap)
{
    if (out && cap)
        lstrcpyn(out, mm_error(e), (int)cap);
    return 0;
}

static DWORD WINAPI mciSendCommand(UINT id, UINT msg, DWORD flags, DWORD params)
{
    (void)id; (void)msg; (void)flags; (void)params;
    return MCIERR_DEVICE_NOT_INSTALLED;
}

static DWORD WINAPI mciSendString(LPCSTR cmd, LPSTR ret, UINT cap, HWND h)
{
    (void)cmd; (void)h;
    if (ret && cap)
        ret[0] = 0;
    return MCIERR_DEVICE_NOT_INSTALLED;
}

static BOOL WINAPI mciGetErrorString(DWORD e, LPSTR out, UINT cap)
{
    if (out && cap)
        lstrcpyn(out, mm_error(e), (int)cap);
    return TRUE;
}

/* ---------------------------------------------------------------- the timer */

static struct {
    int used;
    HTASK task;
    UINT id;
    LPTIMECALLBACK native;
    uint32_t proc;                      /* a Win16 program's */
    DWORD user, next;
    UINT delay;
    int once;
} timers[TIMERS];

static void call_timer(int i)
{
    UINT id = timers[i].id;
    DWORD user = timers[i].user;
    if (timers[i].native) {
        timers[i].native(id, 0, user, 0, 0);
    } else if (NE && NE->size >= sizeof(win_dll_ne) && NE->call) {
        /* (UINT id, UINT msg, DWORD user, DWORD, DWORD) */
        const uint16_t w[8] = { (uint16_t)id, 0, (uint16_t)(user >> 16), (uint16_t)user,
                                0, 0, 0, 0 };
        NE->call((uint16_t)timers[i].task, timers[i].proc, w, 8);
    }
}

/* timeSetEvent's procedure: as often as its period went by since the last
 * time (the timer's message comes late when the task was busy; Windows
 * calls it from the timer's interrupt), ten times at most */
static void CALLBACK timer_proc(HWND h, UINT msg, UINT id, DWORD time)
{
    (void)h; (void)msg; (void)time;
    HTASK t = GetCurrentTask();
    for (int i = 0; i < TIMERS; i++) {
        if (!timers[i].used || timers[i].id != id || timers[i].task != t)
            continue;
        DWORD now = GetTickCount();
        unsigned times = 1;
        if (timers[i].once) {
            KillTimer(0, id);
            timers[i].used = 0;
        } else {
            DWORD late = (int32_t)(now - timers[i].next) > 0 ? now - timers[i].next : 0;
            times += late / timers[i].delay;
            if (times > 10)
                times = 10;
            timers[i].next += times * timers[i].delay;
            if ((int32_t)(now - timers[i].next) > 0)
                timers[i].next = now;       /* more than ten: from now on */
        }
        for (unsigned k = 0; k < times; k++) {
            if (k && (!timers[i].used || timers[i].id != id))
                break;                      /* timeKillEvent in it */
            call_timer(i);
        }
        return;
    }
}

static UINT set_event(UINT delay, LPTIMECALLBACK native, uint32_t proc, DWORD user, UINT flags)
{
    int i = 0;
    while (i < TIMERS && timers[i].used)
        i++;
    if (i == TIMERS || (!native && !proc))
        return 0;
    UINT id = SetTimer(0, 0, delay ? delay : 1, timer_proc);
    if (!id)
        return 0;
    timers[i].used = 1;
    timers[i].task = GetCurrentTask();
    timers[i].id = id;
    timers[i].native = native;
    timers[i].proc = proc;
    timers[i].user = user;
    timers[i].delay = delay ? delay : 1;
    timers[i].next = GetTickCount() + timers[i].delay;
    timers[i].once = !(flags & TIME_PERIODIC);
    return id;
}

static UINT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK proc, DWORD user,
                                UINT flags)
{
    (void)resolution;
    return set_event(delay, proc, 0, user, flags);
}

static MMRESULT WINAPI timeKillEvent(UINT id)
{
    MMRESULT r = MMSYSERR_INVALPARAM;
    for (int i = 0; i < TIMERS; i++)
        if (timers[i].used && timers[i].id == id) {
            KillTimer(0, id);
            timers[i].used = 0;
            r = 0;
        }
    return r;
}

static DWORD WINAPI timeGetTime(void) { return GetTickCount(); }

static MMRESULT WINAPI timeGetSystemTime(LPMMTIME t, UINT size)
{
    if (t && size >= sizeof(MMTIME)) {
        t->wType = TIME_MS;
        t->ms = GetTickCount();
    }
    return 0;
}

static MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS c, UINT size)
{
    if (c && size >= sizeof(TIMECAPS)) {
        c->wPeriodMin = 1;
        c->wPeriodMax = 0xffff;
    }
    return 0;
}

static MMRESULT WINAPI time_period(UINT ms) { (void)ms; return 0; }

/* ---------------------------------------------------------------- mmio */

static struct {
    int used;
    HTASK task;
    HFILE f;
} mmios[MMIOS];

static int file_of(HMMIO h, HFILE *f)
{
    unsigned i = (unsigned)h - MMIO_HANDLE;
    if (h < MMIO_HANDLE || i >= MMIOS || !mmios[i].used)
        return 0;
    *f = mmios[i].f;
    return 1;
}

static HMMIO WINAPI mmioOpen(LPSTR name, LPMMIOINFO info, DWORD flags)
{
    UINT err = MMIOERR_FILENOTFOUND;
    HMMIO h = 0;
    if (info && (info->fccIOProc || info->pIOProc)) {
        err = MMIOERR_CANNOTOPEN;               /* an I/O procedure, memory: not WINN's */
    } else if (!name) {
        err = MMIOERR_CANNOTOPEN;
    } else if (flags & MMIO_PARSE) {
        h = 1;
    } else if (flags & MMIO_DELETE) {
        h = (HMMIO)DeleteFileN(name);
    } else if (flags & MMIO_EXIST) {
        HFILE f = _lopen(name, OF_READ);
        if (f != HFILE_ERROR)
            _lclose(f);
        h = f != HFILE_ERROR;
    } else {
        int i = 0;
        while (i < MMIOS && mmios[i].used)
            i++;
        HFILE f = HFILE_ERROR;
        if (i < MMIOS && (flags & MMIO_CREATE)) {
            f = _lcreat(name, 0);
            if (f != HFILE_ERROR && (flags & 3) != OF_WRITE) {
                _lclose(f);
                f = _lopen(name, (int)(flags & 3));
            }
            err = MMIOERR_CANNOTOPEN;
        } else if (i < MMIOS) {
            f = _lopen(name, (int)(flags & 3));
        }
        if (f != HFILE_ERROR) {
            mmios[i].used = 1;
            mmios[i].task = GetCurrentTask();
            mmios[i].f = f;
            h = (HMMIO)(MMIO_HANDLE + (unsigned)i);
        }
    }
    if (info)
        info->wErrorRet = h ? 0 : err;
    return h;
}

static MMRESULT WINAPI mmioClose(HMMIO h, UINT flags)
{
    HFILE f;
    (void)flags;
    if (!file_of(h, &f))
        return MMSYSERR_INVALHANDLE;
    _lclose(f);
    mmios[h - MMIO_HANDLE].used = 0;
    return 0;
}

static LONG WINAPI mmioRead(HMMIO h, char *buf, LONG n)
{
    HFILE f;
    if (!file_of(h, &f) || n < 0)
        return -1;
    UINT r = n ? _lread(f, buf, (UINT)n) : 0;
    return r == (UINT)HFILE_ERROR ? -1 : (LONG)r;
}

static LONG WINAPI mmioWrite(HMMIO h, const char *buf, LONG n)
{
    HFILE f;
    if (!file_of(h, &f) || n < 0)
        return -1;
    UINT r = n ? _lwrite(f, buf, (UINT)n) : 0;
    return r == (UINT)HFILE_ERROR ? -1 : (LONG)r;
}

static LONG WINAPI mmioSeek(HMMIO h, LONG off, int origin)
{
    HFILE f;
    return file_of(h, &f) ? _llseek(f, off, origin) : -1;
}

static MMRESULT WINAPI mmioFlush(HMMIO h, UINT flags) { (void)h; (void)flags; return 0; }

static MMRESULT WINAPI mmioSetBuffer(HMMIO h, LPSTR buf, LONG n, UINT flags)
{
    (void)h; (void)flags;
    return buf || n ? MMIOERR_UNBUFFERED : 0;   /* unbuffered it stays */
}

static FOURCC WINAPI mmioStringToFOURCC(LPCSTR s, UINT flags)
{
    uint8_t c[4] = { ' ', ' ', ' ', ' ' };
    for (int i = 0; s && i < 4 && s[i]; i++)
        c[i] = (uint8_t)((flags & MMIO_TOUPPER) && s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    return rd32(c);
}

static MMRESULT WINAPI mmioRename(LPCSTR from, LPCSTR to, LPMMIOINFO info, DWORD flags)
{
    (void)info; (void)flags;
    return from && to && MoveFileN(from, to) ? 0 : MMIOERR_FILENOTFOUND;
}

/* the next chunk (of the parent's, when there is one) that is the one
 * looked for: the file at its data */
static MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent, UINT flags)
{
    HFILE f;
    if (!file_of(h, &f))
        return MMSYSERR_INVALHANDLE;
    if (!ck)
        return MMIOERR_CANNOTREAD;
    DWORD end = parent ? parent->dwDataOffset + parent->cksize : 0xffffffffu;
    FOURCC want_id = ck->ckid, want_type = ck->fccType;
    for (;;) {
        LONG at = _llseek(f, 0, 1);
        uint8_t hd[12];
        if (at < 0 || (DWORD)at + 8 > end || _lread(f, hd, 8) != 8)
            return MMIOERR_CHUNKNOTFOUND;
        FOURCC id = rd32(hd), type = 0;
        DWORD size = rd32(hd + 4);
        int list = id == mmioFOURCC('R', 'I', 'F', 'F') || id == mmioFOURCC('L', 'I', 'S', 'T');
        if (list && _lread(f, hd + 8, 4) == 4)
            type = rd32(hd + 8);
        int match = (flags & MMIO_FINDCHUNK) ? id == want_id
                  : (flags & MMIO_FINDRIFF) ? id == mmioFOURCC('R', 'I', 'F', 'F') && type == want_type
                  : (flags & MMIO_FINDLIST) ? id == mmioFOURCC('L', 'I', 'S', 'T') && type == want_type
                  : 1;
        if (match) {
            ck->ckid = id;
            ck->cksize = size;
            ck->fccType = type;
            ck->dwDataOffset = (DWORD)at + 8;
            ck->dwFlags = 0;
            return 0;
        }
        if (_llseek(f, at + 8 + (LONG)((size + 1) & ~1u), 0) < 0)
            return MMIOERR_CANNOTSEEK;
    }
}

/* after the chunk; one made here gets its size */
static MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT flags)
{
    HFILE f;
    (void)flags;
    if (!file_of(h, &f))
        return MMSYSERR_INVALHANDLE;
    if (!ck)
        return MMIOERR_CANNOTREAD;
    DWORD data = ck->dwDataOffset, size = ck->cksize;
    if (ck->dwFlags & MMIO_DIRTY) {
        LONG at = _llseek(f, 0, 1);
        if (at < 0 || (DWORD)at < data)
            return MMIOERR_CANNOTSEEK;
        size = (DWORD)at - data;
        uint8_t b[4];
        wr32(b, size);
        if (size & 1)
            _lwrite(f, "", 1);
        if (_llseek(f, (LONG)data - 4, 0) < 0 || _lwrite(f, b, 4) != 4)
            return MMIOERR_CANNOTWRITE;
        ck->cksize = size;
        ck->dwFlags = 0;
    }
    return _llseek(f, (LONG)(data + ((size + 1) & ~1u)), 0) < 0 ? MMIOERR_CANNOTSEEK : 0;
}

static MMRESULT WINAPI mmioCreateChunk(HMMIO h, LPMMCKINFO ck, UINT flags)
{
    HFILE f;
    uint8_t hd[12];
    if (!file_of(h, &f))
        return MMSYSERR_INVALHANDLE;
    if (!ck)
        return MMIOERR_CANNOTREAD;
    LONG at = _llseek(f, 0, 1);
    FOURCC id = (flags & MMIO_CREATERIFF) ? mmioFOURCC('R', 'I', 'F', 'F')
              : (flags & MMIO_CREATELIST) ? mmioFOURCC('L', 'I', 'S', 'T') : ck->ckid;
    UINT n = (flags & (MMIO_CREATERIFF | MMIO_CREATELIST)) ? 12 : 8;
    wr32(hd, id);
    wr32(hd + 4, ck->cksize);
    wr32(hd + 8, ck->fccType);
    if (at < 0 || _lwrite(f, hd, n) != n)
        return MMIOERR_CANNOTWRITE;
    ck->ckid = id;
    ck->dwDataOffset = (DWORD)at + 8;
    ck->dwFlags = MMIO_DIRTY;
    return 0;
}

/* ---------------------------------------------------------------- Win16 programs */

/* the most bytes from far on in its segment, of n */
static uint32_t in_segment(uint32_t far, uint32_t n)
{
    uint32_t room = 0x10000u - (uint16_t)far;
    return n < room ? n : room;
}

static void str_back(win_dll_call *c, uint32_t far, const char *s, unsigned cap)
{
    char tmp[256];
    if (!(far >> 16) || !cap)
        return;
    lstrcpyn(tmp, s, cap < sizeof(tmp) ? (int)cap : (int)sizeof(tmp));
    NE->write(c, far, tmp, (uint32_t)strlen(tmp) + 1);
}

static void put16(win_dll_call *c, uint32_t far, unsigned v)
{
    uint8_t b[2];
    wr16(b, v);
    if (far >> 16)
        NE->write(c, far, b, 2);
}

/* a Win16 MMCKINFO (20 bytes) and back */
static int ck_in(win_dll_call *c, uint32_t far, MMCKINFO *ck)
{
    uint8_t b[20];
    if (!(far >> 16) || !NE->read(c, far, b, 20))
        return 0;
    ck->ckid = rd32(b);
    ck->cksize = rd32(b + 4);
    ck->fccType = rd32(b + 8);
    ck->dwDataOffset = rd32(b + 12);
    ck->dwFlags = rd32(b + 16);
    return 1;
}

static void ck_out(win_dll_call *c, uint32_t far, const MMCKINFO *ck)
{
    uint8_t b[20];
    wr32(b, ck->ckid);
    wr32(b + 4, ck->cksize);
    wr32(b + 8, ck->fccType);
    wr32(b + 12, ck->dwDataOffset);
    wr32(b + 16, ck->dwFlags);
    NE->write(c, far, b, 20);
}

static int ne_call(win_dll_call *c, uint16_t o, uint32_t *v)
{
    char s1[128], s2[128];
    switch (o) {
    case 2: case 3:                     /* sndPlaySound, PlaySound: no sound */
        *v = 0;
        return WINDLL_WORD;
    case 5:                             /* mmsystemGetVersion */
        *v = mmsystemGetVersion();
        return WINDLL_WORD;
    case 30:                            /* OutputDebugStr */
        *v = 0;
        return WINDLL_WORD;
    case 101: case 201: case 301: case 350: case 401: case 501: case 800:
        *v = 0;                         /* ...GetNumDevs: none */
        return WINDLL_WORD;
    case 103: case 110: case 106:       /* joyGetPos(Ex), joySetCapture */
        *v = JOYERR_UNPLUGGED;
        return WINDLL_WORD;
    case 102: case 104: case 105: case 107: case 109:
        *v = MMSYSERR_NODRIVER;
        return WINDLL_WORD;
    case 203: case 303: case 403: case 503:     /* ...GetErrorText(UINT, LPSTR, UINT) */
        str_back(c, NE->arg32(c, 6), mm_error(NE->arg16(c, 10)), NE->arg16(c, 4));
        *v = 0;
        return WINDLL_WORD;
    case 202: case 302: case 351: case 402: case 502: case 801:  /* the capabilities */
    case 204: case 304: case 404: case 504: case 802:            /* opens */
    case 216: case 313: case 354: case 421: case 514: case 806:  /* by the device */
        if (o == 204 || o == 304 || o == 404 || o == 504 || o == 802)
            put16(c, NE->arg32(c, o == 404 || o == 504 ? 22 : 18), 0);   /* no handle */
        *v = MMSYSERR_BADDEVICEID;
        return o == 216 || o == 313 || o == 354 || o == 421 || o == 514 ? WINDLL_DWORD
                                                                         : WINDLL_WORD;
    case 601: {                         /* timeGetSystemTime(MMTIME FAR *, UINT): TIME_MS */
        uint8_t t[6];
        wr16(t, TIME_MS);
        wr32(t + 2, GetTickCount());
        if (NE->arg16(c, 4) >= 6)
            NE->write(c, NE->arg32(c, 6), t, 6);
        *v = 0;
        return WINDLL_WORD;
    }
    case 602:                           /* timeSetEvent(delay, resolution, proc, DWORD, flags) */
        *v = set_event(NE->arg16(c, 16), NULL, NE->arg32(c, 10), NE->arg32(c, 6),
                       NE->arg16(c, 4));
        return WINDLL_WORD;
    case 603:                           /* timeKillEvent(UINT) */
        *v = timeKillEvent(NE->arg16(c, 4));
        return WINDLL_WORD;
    case 604: {                         /* timeGetDevCaps(TIMECAPS FAR *, UINT) */
        uint8_t t[4];
        wr16(t, 1);
        wr16(t + 2, 0xffff);
        if (NE->arg16(c, 4) >= 4)
            NE->write(c, NE->arg32(c, 6), t, 4);
        *v = 0;
        return WINDLL_WORD;
    }
    case 605: case 606:                 /* timeBeginPeriod, timeEndPeriod */
        *v = 0;
        return WINDLL_WORD;
    case 607:                           /* timeGetTime */
        *v = GetTickCount();
        return WINDLL_DWORD;
    case 701:                           /* mciSendCommand: no devices */
        *v = MCIERR_DEVICE_NOT_INSTALLED;
        return WINDLL_DWORD;
    case 702:                           /* mciSendString(cmd, LPSTR, UINT, HWND) */
        str_back(c, NE->arg32(c, 8), "", NE->arg16(c, 6));
        *v = MCIERR_DEVICE_NOT_INSTALLED;
        return WINDLL_DWORD;
    case 706:                           /* mciGetErrorString(DWORD, LPSTR, UINT) */
        str_back(c, NE->arg32(c, 6), mm_error(NE->arg32(c, 10)), NE->arg16(c, 4));
        *v = 1;
        return WINDLL_WORD;
    case 703: case 707: case 708: case 710: case 711: case 712: case 713: case 714:
    case 715: case 716: case 717:
        *v = 0;
        return o == 708 || o == 716 ? WINDLL_DWORD : WINDLL_WORD;
    case 1100: case 1101: case 1102: case 1103: case 1104:   /* installable drivers: none */
        *v = 0;
        return o == 1102 || o == 1104 ? WINDLL_DWORD : WINDLL_WORD;
    case 1210: {                        /* mmioOpen(LPSTR, MMIOINFO FAR *, DWORD) */
        uint32_t info_far = NE->arg32(c, 8), name_far = NE->arg32(c, 12);
        MMIOINFO info;
        uint8_t mi[16];
        memset(&info, 0, sizeof(info));
        if ((info_far >> 16) && NE->read(c, info_far, mi, 16)) {
            info.fccIOProc = rd32(mi + 4);
            info.pIOProc = (void *)(uintptr_t)rd32(mi + 8);
        }
        int named = (name_far >> 16) && NE->string(c, name_far, s1, sizeof(s1));
        *v = mmioOpen(named ? s1 : NULL, &info, NE->arg32(c, 4));
        if (!*v && (info_far >> 16))
            put16(c, info_far + 12, info.wErrorRet);    /* wErrorRet */
        return WINDLL_WORD;
    }
    case 1211:                          /* mmioClose(HMMIO, UINT) */
        *v = mmioClose(NE->arg16(c, 6), NE->arg16(c, 4));
        return WINDLL_WORD;
    case 1212: case 1213: {             /* mmioRead, mmioWrite(HMMIO, HPSTR, LONG) */
        uint32_t buf = NE->arg32(c, 8), n = NE->arg32(c, 4);
        HMMIO h = NE->arg16(c, 12);
        HFILE f;
        LONG got = -1;
        if (file_of(h, &f) && (int32_t)n >= 0) {
            n = in_segment(buf, n);
            char *tmp = n ? (char *)malloc(n) : NULL;
            if (!n)
                got = 0;
            else if (tmp && o == 1212) {
                got = mmioRead(h, tmp, (LONG)n);
                if (got > 0 && !NE->write(c, buf, tmp, (uint32_t)got))
                    got = -1;
            } else if (tmp && NE->read(c, buf, tmp, n)) {
                got = mmioWrite(h, tmp, (LONG)n);
            }
            free(tmp);
        }
        *v = (uint32_t)got;
        return WINDLL_DWORD;
    }
    case 1214:                          /* mmioSeek(HMMIO, LONG, int) */
        *v = (uint32_t)mmioSeek(NE->arg16(c, 10), (LONG)NE->arg32(c, 6), (int16_t)NE->arg16(c, 4));
        return WINDLL_DWORD;
    case 1215: {                        /* mmioGetInfo(HMMIO, MMIOINFO FAR *, UINT): unbuffered */
        uint8_t mi[66];
        HFILE f;
        memset(mi, 0, sizeof(mi));
        wr16(mi + 64, NE->arg16(c, 10));
        *v = MMSYSERR_INVALHANDLE;
        if (file_of(NE->arg16(c, 10), &f)) {
            NE->write(c, NE->arg32(c, 6), mi, sizeof(mi));
            *v = 0;
        }
        return WINDLL_WORD;
    }
    case 1216: case 1218:               /* mmioSetInfo, mmioFlush */
        *v = 0;
        return WINDLL_WORD;
    case 1217:                          /* mmioSetBuffer: unbuffered it stays */
        *v = NE->arg32(c, 10) || NE->arg32(c, 6) ? MMIOERR_UNBUFFERED : 0;
        return WINDLL_WORD;
    case 1219:                          /* mmioAdvance */
        *v = MMIOERR_UNBUFFERED;
        return WINDLL_WORD;
    case 1220: {                        /* mmioStringToFOURCC(LPCSTR, UINT) */
        uint32_t far = NE->arg32(c, 6);
        s1[0] = 0;
        if (far >> 16)
            NE->string(c, far, s1, 5);
        *v = mmioStringToFOURCC(s1, NE->arg16(c, 4));
        return WINDLL_DWORD;
    }
    case 1221: case 1222:               /* mmioInstallIOProc, mmioSendMessage */
        *v = 0;
        return WINDLL_DWORD;
    case 1223: {                        /* mmioDescend(HMMIO, ck, parent, UINT) */
        MMCKINFO ck, parent;
        uint32_t ck_far = NE->arg32(c, 10), parent_far = NE->arg32(c, 6);
        if (!ck_in(c, ck_far, &ck) || ((parent_far >> 16) && !ck_in(c, parent_far, &parent))) {
            *v = MMIOERR_CANNOTREAD;
            return WINDLL_WORD;
        }
        *v = mmioDescend(NE->arg16(c, 14), &ck, (parent_far >> 16) ? &parent : NULL,
                         NE->arg16(c, 4));
        if (!*v)
            ck_out(c, ck_far, &ck);
        return WINDLL_WORD;
    }
    case 1224: case 1225: {             /* mmioAscend, mmioCreateChunk(HMMIO, ck, UINT) */
        MMCKINFO ck;
        uint32_t ck_far = NE->arg32(c, 6);
        if (!ck_in(c, ck_far, &ck)) {
            *v = MMIOERR_CANNOTREAD;
            return WINDLL_WORD;
        }
        HMMIO h = NE->arg16(c, 10);
        DWORD before = ck.dwFlags | ck.cksize;
        *v = o == 1224 ? mmioAscend(h, &ck, NE->arg16(c, 4)) : mmioCreateChunk(h, &ck, NE->arg16(c, 4));
        if (!*v && (o == 1225 || before != (ck.dwFlags | ck.cksize)))
            ck_out(c, ck_far, &ck);
        return WINDLL_WORD;
    }
    case 1226:                          /* mmioRename(old, new, info, flags) */
        *v = NE->string(c, NE->arg32(c, 16), s1, sizeof(s1)) &&
             NE->string(c, NE->arg32(c, 12), s2, sizeof(s2)) ? mmioRename(s1, s2, NULL, 0)
                                                              : MMIOERR_FILENOTFOUND;
        return WINDLL_WORD;
    }
    if ((o >= 205 && o <= 215) || (o >= 305 && o <= 312) || (o >= 352 && o <= 353) ||
        (o >= 405 && o <= 420) || (o >= 505 && o <= 513) || (o >= 803 && o <= 809)) {
        *v = MMSYSERR_INVALHANDLE;      /* a device's handle: there are none */
        return WINDLL_WORD;
    }
    return WINDLL_NONE;
}

/* a task ended: its timers, its mmio handles (its files are closed) */
static void task_end(uint16_t task)
{
    for (int i = 0; i < TIMERS; i++)
        if (timers[i].used && (uint16_t)timers[i].task == task) {
            KillTimer(0, timers[i].id);
            timers[i].used = 0;
        }
    for (int i = 0; i < MMIOS; i++)
        if (mmios[i].used && (uint16_t)mmios[i].task == task)
            mmios[i].used = 0;
}

static void term(void)
{
    for (int i = 0; i < TIMERS; i++)
        if (timers[i].used)
            KillTimer(0, timers[i].id);
    memset(timers, 0, sizeof(timers));
    for (int i = 0; i < MMIOS; i++)
        if (mmios[i].used)
            _lclose(mmios[i].f);
    memset(mmios, 0, sizeof(mmios));
}

static const win_dll_export exports[] = {
    { 2, 6, "sndPlaySound", (const void *)sndPlaySound },
    { 3, 10, "PlaySound", (const void *)PlaySound },
    { 5, 0, "mmsystemGetVersion", (const void *)mmsystemGetVersion },
    { 30, 4, "OutputDebugStr", (const void *)OutputDebugStr },
    { 101, 0, "joyGetNumDevs", (const void *)no_devices },
    { 102, 8, "joyGetDevCaps", NULL },
    { 103, 6, "joyGetPos", NULL },
    { 104, 6, "joyGetThreshold", NULL },
    { 105, 2, "joyReleaseCapture", NULL },
    { 106, 8, "joySetCapture", NULL },
    { 107, 4, "joySetThreshold", NULL },
    { 109, 2, "joySetCalibration", NULL },
    { 110, 6, "joyGetPosEx", NULL },
    { 201, 0, "midiOutGetNumDevs", (const void *)no_devices },
    { 202, 8, "midiOutGetDevCaps", NULL },
    { 203, 8, "midiOutGetErrorText", (const void *)error_text },
    { 204, 18, "midiOutOpen", NULL },
    { 205, 2, "midiOutClose", NULL },
    { 206, 8, "midiOutPrepareHeader", NULL },
    { 207, 8, "midiOutUnprepareHeader", NULL },
    { 208, 6, "midiOutShortMsg", NULL },
    { 209, 8, "midiOutLongMsg", NULL },
    { 210, 2, "midiOutReset", NULL },
    { 211, 6, "midiOutGetVolume", NULL },
    { 212, 6, "midiOutSetVolume", NULL },
    { 213, 10, "midiOutCachePatches", NULL },
    { 214, 10, "midiOutCacheDrumPatches", NULL },
    { 215, 6, "midiOutGetID", NULL },
    { 216, 12, "midiOutMessage", NULL },
    { 301, 0, "midiInGetNumDevs", (const void *)no_devices },
    { 302, 8, "midiInGetDevCaps", NULL },
    { 303, 8, "midiInGetErrorText", (const void *)error_text },
    { 304, 18, "midiInOpen", NULL },
    { 305, 2, "midiInClose", NULL },
    { 306, 8, "midiInPrepareHeader", NULL },
    { 307, 8, "midiInUnprepareHeader", NULL },
    { 308, 8, "midiInAddBuffer", NULL },
    { 309, 2, "midiInStart", NULL },
    { 310, 2, "midiInStop", NULL },
    { 311, 2, "midiInReset", NULL },
    { 312, 6, "midiInGetID", NULL },
    { 313, 12, "midiInMessage", NULL },
    { 350, 0, "auxGetNumDevs", (const void *)no_devices },
    { 351, 8, "auxGetDevCaps", NULL },
    { 352, 6, "auxGetVolume", NULL },
    { 353, 6, "auxSetVolume", NULL },
    { 354, 12, "auxOutMessage", NULL },
    { 401, 0, "waveOutGetNumDevs", (const void *)no_devices },
    { 402, 8, "waveOutGetDevCaps", NULL },
    { 403, 8, "waveOutGetErrorText", (const void *)error_text },
    { 404, 20, "waveOutOpen", NULL },
    { 405, 2, "waveOutClose", NULL },
    { 406, 8, "waveOutPrepareHeader", NULL },
    { 407, 8, "waveOutUnprepareHeader", NULL },
    { 408, 8, "waveOutWrite", NULL },
    { 409, 2, "waveOutPause", NULL },
    { 410, 2, "waveOutRestart", NULL },
    { 411, 2, "waveOutReset", NULL },
    { 412, 8, "waveOutGetPosition", NULL },
    { 413, 6, "waveOutGetPitch", NULL },
    { 414, 6, "waveOutSetPitch", NULL },
    { 415, 6, "waveOutGetVolume", NULL },
    { 416, 6, "waveOutSetVolume", NULL },
    { 417, 6, "waveOutGetPlaybackRate", NULL },
    { 418, 6, "waveOutSetPlaybackRate", NULL },
    { 419, 2, "waveOutBreakLoop", NULL },
    { 420, 6, "waveOutGetID", NULL },
    { 421, 12, "waveOutMessage", NULL },
    { 501, 0, "waveInGetNumDevs", (const void *)no_devices },
    { 502, 8, "waveInGetDevCaps", NULL },
    { 503, 8, "waveInGetErrorText", (const void *)error_text },
    { 504, 20, "waveInOpen", NULL },
    { 505, 2, "waveInClose", NULL },
    { 506, 8, "waveInPrepareHeader", NULL },
    { 507, 8, "waveInUnprepareHeader", NULL },
    { 508, 8, "waveInAddBuffer", NULL },
    { 509, 2, "waveInStart", NULL },
    { 510, 2, "waveInStop", NULL },
    { 511, 2, "waveInReset", NULL },
    { 512, 8, "waveInGetPosition", NULL },
    { 513, 6, "waveInGetID", NULL },
    { 514, 12, "waveInMessage", NULL },
    { 601, 6, "timeGetSystemTime", (const void *)timeGetSystemTime },
    { 602, 14, "timeSetEvent", (const void *)timeSetEvent },
    { 603, 2, "timeKillEvent", (const void *)timeKillEvent },
    { 604, 6, "timeGetDevCaps", (const void *)timeGetDevCaps },
    { 605, 2, "timeBeginPeriod", (const void *)time_period },
    { 606, 2, "timeEndPeriod", (const void *)time_period },
    { 607, 0, "timeGetTime", (const void *)timeGetTime },
    { 701, 12, "mciSendCommand", (const void *)mciSendCommand },
    { 702, 12, "mciSendString", (const void *)mciSendString },
    { 703, 4, "mciGetDeviceID", NULL },
    { 706, 10, "mciGetErrorString", (const void *)mciGetErrorString },
    { 707, 6, "mciSetDriverData", NULL },
    { 708, 2, "mciGetDriverData", NULL },
    { 710, 2, "mciDriverYield", NULL },
    { 711, 6, "mciDriverNotify", NULL },
    { 712, 4, "mciExecute", NULL },
    { 713, 2, "mciFreeCommandResource", NULL },
    { 714, 10, "mciSetYieldProc", NULL },
    { 715, 8, "mciGetDeviceIDFromElementID", NULL },
    { 716, 6, "mciGetYieldProc", NULL },
    { 717, 2, "mciGetCreatorTask", NULL },
    { 800, 0, "mixerGetNumDevs", (const void *)no_devices },
    { 801, 8, "mixerGetDevCaps", NULL },
    { 802, 18, "mixerOpen", NULL },
    { 803, 2, "mixerClose", NULL },
    { 804, 12, "mixerMessage", NULL },
    { 805, 10, "mixerGetLineInfo", NULL },
    { 806, 10, "mixerGetID", NULL },
    { 807, 10, "mixerGetLineControls", NULL },
    { 808, 10, "mixerGetControlDetails", NULL },
    { 809, 10, "mixerSetControlDetails", NULL },
    { 1100, 12, "DrvOpen", NULL },
    { 1101, 10, "DrvClose", NULL },
    { 1102, 12, "DrvSendMessage", NULL },
    { 1103, 2, "DrvGetModuleHandle", NULL },
    { 1104, 16, "DrvDefDriverProc", NULL },
    { 1210, 12, "mmioOpen", (const void *)mmioOpen },
    { 1211, 4, "mmioClose", (const void *)mmioClose },
    { 1212, 10, "mmioRead", (const void *)mmioRead },
    { 1213, 10, "mmioWrite", (const void *)mmioWrite },
    { 1214, 8, "mmioSeek", (const void *)mmioSeek },
    { 1215, 8, "mmioGetInfo", NULL },
    { 1216, 8, "mmioSetInfo", NULL },
    { 1217, 12, "mmioSetBuffer", (const void *)mmioSetBuffer },
    { 1218, 4, "mmioFlush", (const void *)mmioFlush },
    { 1219, 8, "mmioAdvance", NULL },
    { 1220, 6, "mmioStringToFOURCC", (const void *)mmioStringToFOURCC },
    { 1221, 12, "mmioInstallIOProc", NULL },
    { 1222, 12, "mmioSendMessage", NULL },
    { 1223, 12, "mmioDescend", (const void *)mmioDescend },
    { 1224, 8, "mmioAscend", (const void *)mmioAscend },
    { 1225, 8, "mmioCreateChunk", (const void *)mmioCreateChunk },
    { 1226, 16, "mmioRename", (const void *)mmioRename },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "MMSYSTEM",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, term, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
