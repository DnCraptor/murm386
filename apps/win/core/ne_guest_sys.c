/*
 * ne_guest_sys.c - Windows' own DLLs a Win16 program may import, as WINN
 * has them:
 *
 *   MMSYSTEM  no devices: none of each kind (sound, MIDI, joysticks,
 *             mixers), their opens fail, MCI has none; the timers
 *             (timeGetTime, timeSetEvent: its procedure called as the
 *             program takes its messages); mmio's files and RIFF chunks
 *   TOOLHELP  the tasks, WINN's modules, the timer, the memory as
 *             GetFreeSpace has it, MemoryRead/Write; notifications and
 *             interrupt handlers taken, never called
 *   VER       a file's VERSIONINFO, VerQueryValue in its block,
 *             VerFindFile, VerInstallFile (a copy, expanded by LZEXPAND's)
 *
 * and the native DLLs' calls (core/windll.c, ddi/windll.h): the program's
 * arguments and memory for the DLL.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "windll.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define MM WIN_NE_SERVICE_MMSYSTEM
#define TH WIN_NE_SERVICE_TOOLHELP
#define VR WIN_NE_SERVICE_VER

/* MMSYSTEM's errors */
enum { MMSYSERR_BADDEVICEID = 2, MMSYSERR_INVALHANDLE = 5, MMSYSERR_NODRIVER = 6,
       MMSYSERR_INVALPARAM = 11, JOYERR_UNPLUGGED = 167, MCIERR_DEVICE_NOT_INSTALLED = 306,
       MMIOERR_FILENOTFOUND = 257, MMIOERR_CANNOTOPEN = 259, MMIOERR_CANNOTREAD = 261,
       MMIOERR_CANNOTWRITE = 262, MMIOERR_CANNOTSEEK = 263, MMIOERR_CHUNKNOTFOUND = 265,
       MMIOERR_UNBUFFERED = 266 };

#define MMIO_HANDLE 0x6d00u

static int rd_(args *a, uint32_t far, void *d, uint32_t n)
{
    if (!n)
        return 1;
    if (!(far >> 16) || !win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, d, n))
        return a->bad = 1, 0;
    return 1;
}

static int wr_(args *a, uint32_t far, const void *s, uint32_t n)
{
    if (!n)
        return 1;
    if (!(far >> 16) || !win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, s, n))
        return a->bad = 1, 0;
    return 1;
}

/* a string of the program's; NULL (0:0) is "" and fails */
static int str_(args *a, uint32_t far, char *d, uint32_t cap)
{
    d[0] = 0;
    if (!(far >> 16))
        return 0;
    if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, d, cap))
        return a->bad = 1, 0;
    return 1;
}

/* s into the program's buffer of cap bytes (none: nothing), cut to fit;
 * its length */
static uint32_t str_out(args *a, uint32_t far, const char *s, uint32_t cap)
{
    if (!cap || !(far >> 16))
        return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (n >= cap)
        n = cap - 1;
    wr_(a, far, s, n);
    wr_(a, far + n, "", 1);
    return n;
}

/* a word or a dword into the program's memory, if it gave a place */
static void put16_(args *a, uint32_t far, uint16_t v)
{
    uint8_t b[2];
    wr16(b, v);
    if (far >> 16)
        wr_(a, far, b, 2);
}

static void put32_(args *a, uint32_t far, uint32_t v)
{
    uint8_t b[4];
    wr32(b, v);
    if (far >> 16)
        wr_(a, far, b, 4);
}

/* the most bytes from far on in its segment, of n */
static uint32_t in_segment(uint32_t far, uint32_t n)
{
    uint32_t room = 0x10000u - (uint16_t)far;
    return n < room ? n : room;
}

/* dir and name, a backslash between them */
static void join(char *out, uint32_t cap, const char *dir, const char *name)
{
    uint32_t n = (uint32_t)strlen(dir);
    lstrcpyn(out, dir, (int)cap);
    if (n && n + 1 < cap && dir[n - 1] != '\\' && dir[n - 1] != ':')
        out[n++] = '\\', out[n] = 0;
    if (n < cap)
        lstrcpyn(out + n, name, (int)(cap - n));
}

/* ================================================================ MMSYSTEM */

/* timeSetEvent's procedure: as often as its period went by since the last
 * time (the timer's message comes late when the program was busy; Windows
 * calls it from the timer's interrupt), ten times at most */
static void CALLBACK mm_timerproc(HWND h, UINT msg, UINT id, DWORD time)
{
    (void)h; (void)msg; (void)time;
    ne_user *u = ne_user_of(GetCurrentTask());
    for (int i = 0; u && i < 8; i++)
        if (u->mmtimer[i].proc && u->mmtimer[i].id == id) {
            uint32_t proc = u->mmtimer[i].proc, user = u->mmtimer[i].user, now = GetTickCount();
            unsigned times = 1;
            if (u->mmtimer[i].once) {
                KillTimer(0, id);
                u->mmtimer[i].proc = 0;
            } else {
                uint32_t late = (int32_t)(now - u->mmtimer[i].next) > 0 ?
                                now - u->mmtimer[i].next : 0;
                times += late / u->mmtimer[i].delay;
                if (times > 10)
                    times = 10;
                u->mmtimer[i].next += times * u->mmtimer[i].delay;
                if ((int32_t)(now - u->mmtimer[i].next) > 0)
                    u->mmtimer[i].next = now;       /* more than ten: from now on */
            }
            /* (UINT id, UINT msg, DWORD user, DWORD, DWORD) */
            const uint16_t words[8] = { (uint16_t)id, 0, (uint16_t)(user >> 16), (uint16_t)user,
                                        0, 0, 0, 0 };
            for (unsigned k = 0; k < times; k++) {
                if (k && (u->mmtimer[i].proc != proc || u->mmtimer[i].id != id))
                    break;                          /* timeKillEvent in it */
                win_ne_call(u->ctx, proc, words, 8, NULL, 0);
            }
            return;
        }
}

static const char *mm_error(uint32_t e)
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

/* an mmio handle's file */
static int mmio_of(ne_user *u, uint16_t h, HFILE *f)
{
    unsigned i = (unsigned)h - MMIO_HANDLE;
    if (h < MMIO_HANDLE || i >= 8 || !u->mmio_open[i])
        return 0;
    *f = u->mmio[i];
    return 1;
}

/* MMCKINFO: ckid, cksize, fccType, dwDataOffset, dwFlags */
#define FCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | \
                         (uint32_t)(d) << 24)
#define MMIO_DIRTY 0x10000000u

static uint16_t descend(args *a, HFILE f, uint32_t ck_far, uint32_t parent_far, UINT flags)
{
    uint8_t ck[20], parent[20];
    if (!rd_(a, ck_far, ck, 20) || ((parent_far >> 16) && !rd_(a, parent_far, parent, 20)))
        return MMIOERR_CANNOTREAD;
    uint32_t end = (parent_far >> 16) ? rd32(parent + 12) + rd32(parent + 4) : 0xffffffffu;
    uint32_t want_id = rd32(ck), want_type = rd32(ck + 8);
    for (;;) {
        int32_t at = _llseek(f, 0, 1);
        uint8_t h[12];
        if (at < 0 || (uint32_t)at + 8 > end || _lread(f, h, 8) != 8)
            return MMIOERR_CHUNKNOTFOUND;
        uint32_t id = rd32(h), size = rd32(h + 4), type = 0;
        int list = id == FCC('R', 'I', 'F', 'F') || id == FCC('L', 'I', 'S', 'T');
        if (list && _lread(f, h + 8, 4) == 4)
            type = rd32(h + 8);
        int match = (flags & 0x10) ? id == want_id
                  : (flags & 0x20) ? id == FCC('R', 'I', 'F', 'F') && type == want_type
                  : (flags & 0x40) ? id == FCC('L', 'I', 'S', 'T') && type == want_type : 1;
        if (match) {
            wr32(ck, id);
            wr32(ck + 4, size);
            wr32(ck + 8, type);
            wr32(ck + 12, (uint32_t)at + 8);
            wr32(ck + 16, 0);
            wr_(a, ck_far, ck, 20);
            return 0;
        }
        if (_llseek(f, at + 8 + (int32_t)((size + 1) & ~1u), 0) < 0)
            return MMIOERR_CANNOTSEEK;
    }
}

static uint16_t ascend(args *a, HFILE f, uint32_t ck_far)
{
    uint8_t ck[20];
    if (!rd_(a, ck_far, ck, 20))
        return MMIOERR_CANNOTREAD;
    uint32_t data = rd32(ck + 12), size = rd32(ck + 4);
    if (rd32(ck + 16) & MMIO_DIRTY) {               /* made here: its size now */
        int32_t at = _llseek(f, 0, 1);
        if (at < 0 || (uint32_t)at < data)
            return MMIOERR_CANNOTSEEK;
        size = (uint32_t)at - data;
        uint8_t b[4];
        wr32(b, size);
        if (size & 1)
            _lwrite(f, "", 1);
        if (_llseek(f, (int32_t)data - 4, 0) < 0 || _lwrite(f, b, 4) != 4)
            return MMIOERR_CANNOTWRITE;
        wr32(ck + 4, size);
        wr32(ck + 16, 0);
        wr_(a, ck_far, ck, 20);
    }
    return _llseek(f, (int32_t)(data + ((size + 1) & ~1u)), 0) < 0 ? MMIOERR_CANNOTSEEK : 0;
}

static uint16_t create_chunk(args *a, HFILE f, uint32_t ck_far, UINT flags)
{
    uint8_t ck[20], h[12];
    if (!rd_(a, ck_far, ck, 20))
        return MMIOERR_CANNOTREAD;
    int32_t at = _llseek(f, 0, 1);
    uint32_t id = (flags & 0x20) ? FCC('R', 'I', 'F', 'F')
                : (flags & 0x40) ? FCC('L', 'I', 'S', 'T') : rd32(ck);
    UINT n = (flags & 0x60) ? 12 : 8;
    wr32(h, id);
    wr32(h + 4, rd32(ck + 4));
    wr32(h + 8, rd32(ck + 8));
    if (at < 0 || _lwrite(f, h, n) != n)
        return MMIOERR_CANNOTWRITE;
    wr32(ck, id);
    wr32(ck + 12, (uint32_t)at + 8);
    wr32(ck + 16, MMIO_DIRTY);
    wr_(a, ck_far, ck, 20);
    return 0;
}

static __attribute__((noinline)) uint16_t mmio_open(args *a, ne_user *u)
{
    uint32_t flags = dw(a, 4), info = dw(a, 8);
    char name[128];
    uint16_t err = MMIOERR_FILENOTFOUND, h = 0;
    uint8_t mi[16];
    if ((info >> 16) && rd_(a, info, mi, 16) && (rd32(mi + 4) || rd32(mi + 8))) {
        err = MMIOERR_CANNOTOPEN;               /* an I/O procedure, memory: not WINN's */
    } else if (!str_(a, dw(a, 12), name, sizeof(name))) {
        err = MMIOERR_CANNOTOPEN;
    } else if (flags & 0x100) {                 /* MMIO_PARSE */
        h = 1;
    } else if (flags & 0x200) {                 /* MMIO_DELETE */
        h = (uint16_t)DeleteFileN(name);
    } else if (flags & 0x4000) {                /* MMIO_EXIST */
        HFILE f = _lopen(name, OF_READ);
        if (f != HFILE_ERROR)
            _lclose(f);
        h = f != HFILE_ERROR;
    } else {
        int i = 0;
        while (i < 8 && u->mmio_open[i])
            i++;
        HFILE f = HFILE_ERROR;
        if (i < 8 && (flags & 0x1000)) {        /* MMIO_CREATE */
            f = _lcreat(name, 0);
            if (f != HFILE_ERROR && (flags & 3) != OF_WRITE) {
                _lclose(f);
                f = _lopen(name, (int)(flags & 3));
            }
            err = MMIOERR_CANNOTOPEN;
        } else if (i < 8) {
            f = _lopen(name, (int)(flags & 3));
        }
        if (f != HFILE_ERROR) {
            u->mmio[i] = f;
            u->mmio_open[i] = 1;
            h = (uint16_t)(MMIO_HANDLE + (unsigned)i);
        }
    }
    if (!h && (info >> 16))
        put16_(a, info + 12, err);              /* wErrorRet */
    return h;
}

static __attribute__((noinline)) int mm_service(args *a, ne_user *u, uint16_t service,
                                               uint32_t *v, int *dword)
{
    unsigned o = service - MM;
    HFILE f;
    switch (o) {
    case 2: case 3:                     /* sndPlaySound, PlaySound: no sound */
        *v = 0;
        return 1;
    case 5:                             /* mmsystemGetVersion */
        *v = 0x030a;
        return 1;
    case 30:                            /* OutputDebugStr */
        return 1;
    case 101: case 201: case 301: case 350: case 401: case 501: case 800:
        *v = 0;                         /* ...GetNumDevs: none */
        return 1;
    case 103: case 110: case 106:       /* joyGetPos(Ex), joySetCapture */
        *v = JOYERR_UNPLUGGED;
        return 1;
    case 102: case 104: case 105: case 107: case 109:
        *v = MMSYSERR_NODRIVER;
        return 1;
    case 203: case 303: case 403: case 503:     /* ...GetErrorText(UINT, LPSTR, UINT) */
        str_out(a, dw(a, 6), mm_error(w(a, 10)), w(a, 4));
        *v = 0;
        return 1;
    case 202: case 302: case 351: case 402: case 502: case 801:  /* the capabilities */
    case 204: case 304: case 404: case 504: case 802:            /* opens */
    case 216: case 313: case 354: case 421: case 514: case 806:  /* by the device */
        if (o == 204 || o == 304 || o == 404 || o == 504 || o == 802)
            put16_(a, dw(a, o == 404 || o == 504 ? 22 : 18), 0);    /* no handle */
        *v = MMSYSERR_BADDEVICEID;
        *dword = o == 216 || o == 313 || o == 354 || o == 421 || o == 514;
        return 1;
    case 601: {                         /* timeGetSystemTime(MMTIME FAR *, UINT): TIME_MS */
        uint8_t t[6];
        wr16(t, 1);
        wr32(t + 2, GetTickCount());
        if (w(a, 4) >= 6)
            wr_(a, dw(a, 6), t, 6);
        *v = 0;
        return 1;
    }
    case 602: {                         /* timeSetEvent(delay, resolution, proc, DWORD, flags) */
        UINT flags = w(a, 4), delay = w(a, 16);
        uint32_t user = dw(a, 6), proc = dw(a, 10);
        int i = 0;
        while (i < 8 && u->mmtimer[i].proc)
            i++;
        UINT id = i < 8 && proc && !a->bad ? SetTimer(0, 0, delay ? delay : 1, mm_timerproc)
                                           : 0;
        if (id) {
            u->mmtimer[i].id = id;
            u->mmtimer[i].proc = proc;
            u->mmtimer[i].user = user;
            u->mmtimer[i].delay = (uint16_t)(delay ? delay : 1);
            u->mmtimer[i].next = GetTickCount() + u->mmtimer[i].delay;
            u->mmtimer[i].once = !(flags & 1);  /* TIME_PERIODIC */
        }
        *v = (uint16_t)id;
        return 1;
    }
    case 603: {                         /* timeKillEvent(UINT) */
        UINT id = w(a, 4);
        *v = MMSYSERR_INVALPARAM;
        for (int i = 0; i < 8; i++)
            if (u->mmtimer[i].proc && u->mmtimer[i].id == id) {
                KillTimer(0, id);
                u->mmtimer[i].proc = 0;
                *v = 0;
            }
        return 1;
    }
    case 604: {                         /* timeGetDevCaps(TIMECAPS FAR *, UINT) */
        uint8_t t[4];
        wr16(t, 1);
        wr16(t + 2, 0xffff);
        if (w(a, 4) >= 4)
            wr_(a, dw(a, 6), t, 4);
        *v = 0;
        return 1;
    }
    case 605: case 606:                 /* timeBeginPeriod, timeEndPeriod */
        *v = 0;
        return 1;
    case 607:                           /* timeGetTime */
        *v = GetTickCount();
        *dword = 1;
        return 1;
    case 701:                           /* mciSendCommand: no devices */
        *v = MCIERR_DEVICE_NOT_INSTALLED;
        *dword = 1;
        return 1;
    case 702:                           /* mciSendString(cmd, LPSTR, UINT, HWND) */
        str_out(a, dw(a, 8), "", w(a, 6));
        *v = MCIERR_DEVICE_NOT_INSTALLED;
        *dword = 1;
        return 1;
    case 706:                           /* mciGetErrorString(DWORD, LPSTR, UINT) */
        str_out(a, dw(a, 6), mm_error(dw(a, 10)), w(a, 4));
        *v = 1;
        return 1;
    case 703: case 707: case 708: case 710: case 711: case 712: case 713: case 714:
    case 715: case 716: case 717:
        *v = 0;
        *dword = o == 708 || o == 716;
        return 1;
    case 1100: case 1101: case 1102: case 1103: case 1104:   /* installable drivers: none */
        *v = 0;
        *dword = o == 1102 || o == 1104;
        return 1;
    case 1210:                          /* mmioOpen(LPSTR, MMIOINFO FAR *, DWORD) */
        *v = mmio_open(a, u);
        return 1;
    case 1211: {                        /* mmioClose(HMMIO, UINT) */
        uint16_t h = w(a, 6);
        *v = MMSYSERR_INVALHANDLE;
        if (mmio_of(u, h, &f)) {
            _lclose(f);
            u->mmio_open[h - MMIO_HANDLE] = 0;
            *v = 0;
        }
        return 1;
    }
    case 1212: case 1213: {             /* mmioRead, mmioWrite(HMMIO, HPSTR, LONG) */
        uint32_t buf = dw(a, 8), n = dw(a, 4);
        int32_t got = -1;
        if (mmio_of(u, w(a, 12), &f) && (int32_t)n >= 0) {
            n = in_segment(buf, n);
            uint8_t *tmp = n ? (uint8_t *)win_alloc(n) : NULL;
            if (!n)
                got = 0;
            else if (tmp && o == 1212) {
                UINT r = _lread(f, tmp, n);
                got = r == (UINT)HFILE_ERROR ? -1 : (int32_t)r;
                if (got > 0 && !wr_(a, buf, tmp, (uint32_t)got))
                    got = -1;
            } else if (tmp && rd_(a, buf, tmp, n)) {
                UINT r = _lwrite(f, tmp, n);
                got = r == (UINT)HFILE_ERROR ? -1 : (int32_t)r;
            }
            win_free(tmp);
        }
        *v = (uint32_t)got;
        *dword = 1;
        return 1;
    }
    case 1214:                          /* mmioSeek(HMMIO, LONG, int) */
        *v = mmio_of(u, w(a, 10), &f) ? (uint32_t)_llseek(f, (LONG)dw(a, 6), (int16_t)w(a, 4))
                                      : 0xffffffffu;
        *dword = 1;
        return 1;
    case 1215: {                        /* mmioGetInfo(HMMIO, MMIOINFO FAR *, UINT): unbuffered */
        uint8_t mi[66];
        memset(mi, 0, sizeof(mi));
        wr16(mi + 64, w(a, 10));
        *v = mmio_of(u, w(a, 10), &f) ? (wr_(a, dw(a, 6), mi, sizeof(mi)), 0)
                                      : MMSYSERR_INVALHANDLE;
        return 1;
    }
    case 1216: case 1218:               /* mmioSetInfo, mmioFlush */
        *v = 0;
        return 1;
    case 1217:                          /* mmioSetBuffer: unbuffered it stays */
        *v = dw(a, 10) || dw(a, 6) ? MMIOERR_UNBUFFERED : 0;
        return 1;
    case 1219:                          /* mmioAdvance */
        *v = MMIOERR_UNBUFFERED;
        return 1;
    case 1220: {                        /* mmioStringToFOURCC(LPCSTR, UINT) */
        char s[8];
        uint8_t c[4] = { ' ', ' ', ' ', ' ' };
        if (dw(a, 6) >> 16) {
            win_ne_string(a->ctx, (uint16_t)(dw(a, 6) >> 16), (uint16_t)dw(a, 6), s, 5);
            for (int i = 0; i < 4 && s[i]; i++)
                c[i] = (uint8_t)((w(a, 4) & 0x10) && s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32
                                                                                  : s[i]);
        }
        *v = rd32(c);
        *dword = 1;
        return 1;
    }
    case 1221: case 1222:               /* mmioInstallIOProc, mmioSendMessage */
        *v = 0;
        *dword = 1;
        return 1;
    case 1223:                          /* mmioDescend(HMMIO, ck, parent, UINT) */
        *v = mmio_of(u, w(a, 14), &f) ? descend(a, f, dw(a, 10), dw(a, 6), w(a, 4))
                                      : MMSYSERR_INVALHANDLE;
        return 1;
    case 1224:                          /* mmioAscend(HMMIO, ck, UINT) */
        *v = mmio_of(u, w(a, 10), &f) ? ascend(a, f, dw(a, 6)) : MMSYSERR_INVALHANDLE;
        return 1;
    case 1225:                          /* mmioCreateChunk(HMMIO, ck, UINT) */
        *v = mmio_of(u, w(a, 10), &f) ? create_chunk(a, f, dw(a, 6), w(a, 4))
                                      : MMSYSERR_INVALHANDLE;
        return 1;
    case 1226: {                        /* mmioRename(old, new, info, flags) */
        char from[128], to[128];
        *v = str_(a, dw(a, 16), from, sizeof(from)) && str_(a, dw(a, 12), to, sizeof(to)) &&
             MoveFileN(from, to) ? 0 : MMIOERR_FILENOTFOUND;
        return 1;
    }
    default:
        if ((o >= 205 && o <= 215) || (o >= 305 && o <= 312) || (o >= 352 && o <= 353) ||
            (o >= 405 && o <= 420) || (o >= 505 && o <= 513) || (o >= 803 && o <= 809)) {
            *v = MMSYSERR_INVALHANDLE;  /* a device's handle: there are none */
            return 1;
        }
        return 0;
    }
}

/* ================================================================ TOOLHELP */

/* a structure's dwSize at least n */
static int sized(args *a, uint32_t far, uint32_t n)
{
    uint8_t b[4];
    return rd_(a, far, b, 4) && rd32(b) >= n;
}

/* TASKENTRY of task t (0: none) */
static int task_entry(args *a, uint32_t far, HTASK t)
{
    wtask *k = t ? task_from_handle(t) : NULL;
    if (!k || !sized(a, far, 40))
        return 0;
    uint8_t e[40];
    memset(e, 0, sizeof(e));
    wr32(e, 40);
    wr16(e + 4, t);
    ne_user *n = ne_user_of(t);
    uint16_t inst = t;
    if (n) {
        const win_ne_guest_image *g = n->ctx->image;
        inst = g->segments[g->info.auto_data ? g->info.auto_data : g->info.ss_index].selector;
    }
    wr16(e + 8, inst);
    wr16(e + 10, inst);
    for (int i = 0; i < 9 && k->name[i] && k->name[i] != '.'; i++)
        e[26 + i] = (uint8_t)(k->name[i] >= 'a' && k->name[i] <= 'z' ? k->name[i] - 32
                                                                     : k->name[i]);
    wr16(e + 36, 0x100);
    wr16(e + 38, task_next(t));
    return wr_(a, far, e, sizeof(e));
}

/* MODULEENTRY of WINN's module m */
/* the modules: WINN's own (F000h + n), then the native DLLs loaded (their
 * handles); the one after h, 0 after the last */
static uint16_t module_after(uint16_t h)
{
    int s = -1;
    if (h >= 0xf000u && win_ne_guest_module_name(h - 0xf000 + 1))
        return (uint16_t)(h + 1);
    if (h < 0xf000u && (s = win_dll_of(h)) < 0)
        return 0;
    while (++s < WIN_DLL_SLOTS)
        if (win_dll_get(s))
            return win_dll_handle(s);
    return 0;
}

static int module_entry(args *a, uint32_t far, uint16_t h)
{
    int m = h >= 0xf000u ? h - 0xf000 : -1, s = h < 0xf000u ? win_dll_of(h) : -1;
    const char *name = m >= 0 ? win_ne_guest_module_name(m) : s >= 0 ? win_dll_get(s)->name : NULL;
    if (!name || !sized(a, far, 276))
        return 0;
    uint8_t e[276];
    char path[128];
    memset(e, 0, sizeof(e));
    wr32(e, 276);
    lstrcpyn((char *)e + 4, name, 10);
    wr16(e + 14, h);
    wr16(e + 16, 1);
    if (s >= 0) {
        lstrcpyn(path, win_dll_path(s), sizeof(path));
    } else {
        UINT n = GetSystemDirectory(path, sizeof(path) - 16);
        wsprintf(path + n, "%s%s.%s", n && path[n - 1] != '\\' ? "\\" : "", name,
                 m < 3 ? "EXE" : m == 3 ? "DRV" : "DLL");
    }
    lstrcpyn((char *)e + 18, path, 256);
    wr16(e + 274, module_after(h));
    return wr_(a, far, e, sizeof(e));
}

static __attribute__((noinline)) int th_service(args *a, ne_user *u, uint16_t service,
                                               uint32_t *v, int *dword)
{
    (void)u;
    unsigned o = service - TH;
    uint8_t e[44];
    switch (o) {
    case 50:                            /* GlobalHandleToSel: itself */
        *v = w(a, 4);
        return 1;
    case 53:                            /* GlobalInfo(GLOBALINFO FAR *) */
        if ((*v = sized(a, dw(a, 4), 10)) != 0) {
            memset(e, 0, 10);
            wr32(e, 10);
            wr16(e + 4, WIN_NE_GUEST_GLOBALS);
            wr16(e + 6, WIN_NE_GUEST_GLOBALS / 2);
            wr_(a, dw(a, 4), e, 10);
        }
        return 1;
    case 59:                            /* ModuleFirst */
        *v = (uint16_t)module_entry(a, dw(a, 4), 0xf000u);
        return 1;
    case 60: {                          /* ModuleNext: its wNext */
        uint8_t b[2];
        *v = rd_(a, dw(a, 4) + 274, b, 2) && rd16(b) ?
             (uint16_t)module_entry(a, dw(a, 4), rd16(b)) : 0;
        return 1;
    }
    case 61: {                          /* ModuleFindName(MODULEENTRY FAR *, LPCSTR) */
        char name[16];
        int m = str_(a, dw(a, 4), name, sizeof(name)) ? win_ne_guest_module(name) : -1;
        int s = m < 0 && name[0] ? win_dll_find(name) : -1;
        *v = m >= 0 ? (uint16_t)module_entry(a, dw(a, 8), (uint16_t)(0xf000u + (unsigned)m)) :
             s >= 0 ? (uint16_t)module_entry(a, dw(a, 8), win_dll_handle(s)) : 0;
        return 1;
    }
    case 62:                            /* ModuleFindHandle(MODULEENTRY FAR *, HMODULE) */
        *v = (uint16_t)module_entry(a, dw(a, 6), w(a, 4));
        return 1;
    case 63:                            /* TaskFirst */
        *v = (uint16_t)task_entry(a, dw(a, 4), task_next(0));
        return 1;
    case 64: {                          /* TaskNext: its hNext */
        uint8_t b[2];
        *v = rd_(a, dw(a, 4) + 38, b, 2) ? (uint16_t)task_entry(a, dw(a, 4), rd16(b)) : 0;
        return 1;
    }
    case 65:                            /* TaskFindHandle(TASKENTRY FAR *, HTASK) */
        *v = IsTask(w(a, 4)) ? (uint16_t)task_entry(a, dw(a, 6), w(a, 4)) : 0;
        return 1;
    case 71:                            /* SystemHeapInfo: USER's and GDI's, 80% free */
        if ((*v = sized(a, dw(a, 4), 12)) != 0) {
            memset(e, 0, 12);
            wr32(e, 12);
            wr16(e + 4, 80);
            wr16(e + 6, 80);
            wr_(a, dw(a, 4), e, 12);
        }
        return 1;
    case 72:                            /* MemManInfo: as GetFreeSpace, pages of 4K */
        if ((*v = sized(a, dw(a, 4), 42)) != 0) {
            static const uint32_t m[9] = { 0x80000u, 128, 128, 256, 128, 128, 256, 128, 0 };
            memset(e, 0, 42);
            wr32(e, 42);
            for (int i = 0; i < 9; i++)
                wr32(e + 4 + 4 * i, m[i]);
            wr16(e + 40, 4096);
            wr_(a, dw(a, 4), e, 42);
        }
        return 1;
    case 73: case 74: case 75: case 76:  /* notifications, interrupts: taken, never made */
        *v = 1;
        return 1;
    case 77:                            /* TerminateApp: not another's */
        return 1;
    case 78: case 79: {                 /* MemoryRead, MemoryWrite(sel, off, buf, count) */
        uint16_t sel = w(a, 16);
        uint32_t off = dw(a, 12), buf = dw(a, 8), n = dw(a, 4);
        if (off > 0xffffu)
            n = 0;
        n = in_segment(off, in_segment(buf, n));
        uint8_t *tmp = n ? (uint8_t *)win_alloc(n) : NULL;
        int ok = tmp && (o == 78 ? win_ne_read(a->ctx, sel, (uint16_t)off, tmp, n) &&
                                   win_ne_write(a->ctx, (uint16_t)(buf >> 16), (uint16_t)buf,
                                                tmp, n)
                                 : win_ne_read(a->ctx, (uint16_t)(buf >> 16), (uint16_t)buf,
                                               tmp, n) &&
                                   win_ne_write(a->ctx, sel, (uint16_t)off, tmp, n));
        win_free(tmp);
        *v = ok ? n : 0;
        *dword = 1;
        return 1;
    }
    case 80:                            /* TimerCount(TIMERINFO FAR *) */
        if ((*v = sized(a, dw(a, 4), 12)) != 0) {
            wr32(e, 12);
            wr32(e + 4, GetTickCount());
            wr32(e + 8, GetTickCount());
            wr_(a, dw(a, 4), e, 12);
        }
        return 1;
    case 51: case 52: case 54: case 55: case 56: case 57: case 58: case 66: case 67: case 68:
    case 69: case 70: case 84: case 85: case 86:
        *v = 0;                         /* the heaps' blocks, stacks, classes: not walked */
        return 1;
    }
    return 0;
}

/* ================================================================ VER */

/* a VERSIONINFO node at off of the block b (n bytes): its length, its
 * value's, its key; where its value and its children are (4-aligned) */
typedef struct {
    uint32_t len, vlen, val, kids;
    const char *key;
} ver_node;

static int node_at(const uint8_t *b, uint32_t n, uint32_t off, ver_node *v)
{
    if (off + 5 > n)
        return 0;
    v->len = rd16(b + off);
    v->vlen = rd16(b + off + 2);
    if (v->len < 5 || off + v->len > n)
        return 0;
    v->key = (const char *)b + off + 4;
    uint32_t k = off + 4;
    while (k < off + v->len && b[k])
        k++;
    if (k >= off + v->len)
        return 0;
    v->val = (k + 1 + 3) & ~3u;
    v->kids = (v->val + v->vlen + 3) & ~3u;
    return 1;
}

/* VerQueryValue's path ("\", "\StringFileInfo\040904E4\FileVersion",
 * "\VarFileInfo\Translation") in the block: its node's value */
static int ver_query(const uint8_t *b, uint32_t n, const char *path, uint32_t *val,
                     uint32_t *len)
{
    ver_node cur;
    uint32_t at = 0;
    if (!node_at(b, n, 0, &cur))
        return 0;
    while (*path) {
        while (*path == '\\')
            path++;
        if (!*path)
            break;
        char part[64];
        uint32_t k = 0;
        while (*path && *path != '\\' && k + 1 < sizeof(part))
            part[k++] = *path++;
        part[k] = 0;
        uint32_t c = cur.kids, end = at + cur.len;
        ver_node child;
        int found = 0;
        while (c < end && node_at(b, n, c, &child)) {
            if (!lstrcmpi(child.key, part)) {
                found = 1;
                break;
            }
            c = (c + child.len + 3) & ~3u;
        }
        if (!found)
            return 0;
        cur = child;
        at = c;
    }
    *val = cur.val;
    *len = cur.vlen;
    return cur.vlen != 0;
}

/* a numbered resource type or id given as a number, or "#n" */
static int res_number(args *a, uint32_t far, uint16_t *num, char *name, uint32_t cap)
{
    name[0] = 0;
    if (!(far >> 16)) {
        *num = (uint16_t)far;
        return 1;
    }
    if (!str_(a, far, name, cap))
        return 0;
    *num = 0;
    if (name[0] == '#') {
        for (const char *p = name + 1; *p >= '0' && *p <= '9'; p++)
            *num = (uint16_t)(*num * 10 + (*p - '0'));
        name[0] = 0;
    }
    return 1;
}

/* a file's resource (VERSIONINFO: the first of type 16): its bytes,
 * malloc'd */
static uint8_t *file_resource(args *a, uint32_t file, uint32_t type, uint32_t id, uint32_t *n)
{
    char path[128], tname[32], iname[32];
    uint16_t t = 16, i = 0;
    uint8_t *d = NULL;
    if (!str_(a, file, path, sizeof(path)))
        return NULL;
    if (type == 0xffffffffu) {
        if (win_ne_resource_nth(path, 16, 0, &d, n) != WIN_NE_OK)
            return NULL;
    } else if (!res_number(a, type, &t, tname, sizeof(tname)) || tname[0] ||
               !res_number(a, id, &i, iname, sizeof(iname)) ||
               win_ne_resource(path, t, i, iname[0] ? iname : NULL, &d, n) != WIN_NE_OK) {
        return NULL;
    }
    return d;
}

/* src (srcdir) to dest (destdir), expanded by LZEXPAND's LZCopy (a copy
 * as it is without the DLL): 0 or VIF_... */
static uint32_t install_file(const char *src, const char *dest)
{
    HFILE in = _lopen(src, OF_READ);
    if (in == HFILE_ERROR)
        return 0x10000u;                /* VIF_CANNOTREADSRC */
    HFILE out = _lcreat(dest, 0);
    if (out == HFILE_ERROR) {
        _lclose(in);
        return 0x800u;                  /* VIF_CANNOTCREATE */
    }
    uint32_t r = 0;
    HINSTANCE lz = LoadLibrary("LZEXPAND");
    LONG (WINAPI *copy)(HFILE, HFILE) =                        /* LZCopy */
        (LONG (WINAPI *)(HFILE, HFILE))(void (*)(void))GetProcAddress(lz, (LPCSTR)(uintptr_t)1);
    if (copy) {
        LONG n = copy(in, out);
        r = n >= 0 ? 0 : n == -4 ? 0x100u : n == -5 ? 0x8000u : 0x10000u;
    } else {
        uint8_t *buf = (uint8_t *)win_alloc(1024);
        r = buf ? 0 : 0x8000u;          /* VIF_OUTOFMEMORY */
        while (!r) {
            uint32_t n = _lread(in, buf, 1024);
            if (n == (UINT)HFILE_ERROR)
                r = 0x10000u;
            else if (!n)
                break;
            else if (_lwrite(out, buf, n) != n)
                r = 0x100u;             /* VIF_OUTOFSPACE */
        }
        win_free(buf);
    }
    FreeLibrary(lz);
    _lclose(in);
    _lclose(out);
    return r;
}

static int exists(const char *path)
{
    HFILE f = _lopen(path, OF_READ);
    if (f == HFILE_ERROR)
        return 0;
    _lclose(f);
    return 1;
}

static __attribute__((noinline)) int ver_service(args *a, uint16_t service, uint32_t *v,
                                                int *dword)
{
    unsigned o = service - VR;
    uint32_t n = 0;
    uint8_t *d;
    switch (o) {
    case 2:                             /* GetFileResourceSize(file, type, id, DWORD FAR *) */
    case 6:                             /* GetFileVersionInfoSize(file, DWORD FAR *) */
        d = o == 6 ? file_resource(a, dw(a, 8), 0xffffffffu, 0, &n)
                   : file_resource(a, dw(a, 16), dw(a, 12), dw(a, 8), &n);
        if (d && o == 6 && rd16(d) <= n)
            n = rd16(d);
        *v = d ? n : 0;
        put32_(a, dw(a, 4), 0);
        free(d);
        *dword = 1;
        return 1;
    case 3:                             /* GetFileResource(file, type, id, offset, len, buf) */
    case 7: {                           /* GetFileVersionInfo(file, handle, len, buf) */
        d = o == 7 ? file_resource(a, dw(a, 16), 0xffffffffu, 0, &n)
                   : file_resource(a, dw(a, 24), dw(a, 20), dw(a, 16), &n);
        uint32_t len = dw(a, 8), buf = dw(a, 4);
        if (len > n)
            len = n;
        len = in_segment(buf, len);
        *v = d && wr_(a, buf, d, len);
        free(d);
        *dword = o == 3;
        return 1;
    }
    case 8: {                           /* VerFindFile(flags, file, windir, appdir, curdir,
                                         * UINT FAR *, destdir, UINT FAR *) */
        char file[64], windir[128], appdir[128], path[200];
        uint8_t b[2];
        str_(a, dw(a, 28), file, sizeof(file));
        str_(a, dw(a, 24), windir, sizeof(windir));
        str_(a, dw(a, 20), appdir, sizeof(appdir));
        int shared = w(a, 32) & 1;      /* VFFF_ISSHAREDFILE */
        const char *dest = shared ? windir : appdir, *cur = "";
        join(path, sizeof(path), dest, file);
        if (exists(path))
            cur = dest;
        else if (join(path, sizeof(path), shared ? appdir : windir, file), exists(path))
            cur = shared ? appdir : windir;
        uint32_t r = cur[0] && lstrcmpi(cur, dest) ? 1 : 0;    /* VFF_CURNEDEST */
        UINT curcap = rd_(a, dw(a, 12), b, 2) ? rd16(b) : 0;
        UINT destcap = rd_(a, dw(a, 4), b, 2) ? rd16(b) : 0;
        if (strlen(cur) >= curcap || strlen(dest) >= destcap)
            r |= 4;                     /* VFF_BUFFTOOSMALL */
        put16_(a, dw(a, 12), (uint16_t)(str_out(a, dw(a, 16), cur, curcap) + 1));
        put16_(a, dw(a, 4), (uint16_t)(str_out(a, dw(a, 8), dest, destcap) + 1));
        *v = r;
        return 1;
    }
    case 9: {                           /* VerInstallFile(flags, src, dest, srcdir, destdir,
                                         * curdir, LPSTR tmp, UINT FAR *) */
        char src[64], dst[64], sdir[128], ddir[128], from[200], to[200];
        str_(a, dw(a, 28), src, sizeof(src));
        str_(a, dw(a, 24), dst, sizeof(dst));
        str_(a, dw(a, 20), sdir, sizeof(sdir));
        str_(a, dw(a, 16), ddir, sizeof(ddir));
        join(from, sizeof(from), sdir, src);
        join(to, sizeof(to), ddir, dst);
        *v = a->bad ? 0x10000u : install_file(from, to);
        uint8_t b[2];
        if (rd_(a, dw(a, 4), b, 2) && rd16(b))
            str_out(a, dw(a, 8), "", rd16(b));
        put16_(a, dw(a, 4), 0);
        *dword = 1;
        return 1;
    }
    case 10: {                          /* VerLanguageName(UINT, LPSTR, UINT) */
        static const struct { uint16_t lang; const char *name; } l[] = {
            { 0x0000, "Language Neutral" }, { 0x0405, "Czech" }, { 0x0407, "German" },
            { 0x0409, "English (United States)" }, { 0x0809, "English (United Kingdom)" },
            { 0x040a, "Spanish (Traditional Sort)" }, { 0x040c, "French" },
            { 0x0410, "Italian" }, { 0x0411, "Japanese" }, { 0x0415, "Polish" },
            { 0x0419, "Russian" }, { 0x0422, "Ukrainian" },
        };
        const char *name = "Unknown language";
        for (unsigned i = 0; i < sizeof(l) / sizeof(l[0]); i++)
            if (l[i].lang == w(a, 10))
                name = l[i].name;
        *v = str_out(a, dw(a, 6), name, w(a, 4));
        return 1;
    }
    case 11: {                          /* VerQueryValue(block, LPCSTR, LPVOID FAR *, UINT FAR *) */
        uint32_t block = dw(a, 16), val, len;
        char path[128];
        uint8_t b[2];
        *v = 0;
        if (!str_(a, dw(a, 12), path, sizeof(path)) || !rd_(a, block, b, 2))
            return 1;
        n = in_segment(block, rd16(b));
        d = (uint8_t *)win_alloc(n ? n : 1);
        if (d && rd_(a, block, d, n) && ver_query(d, n, path, &val, &len)) {
            put32_(a, dw(a, 8), block + val);
            put16_(a, dw(a, 4), (uint16_t)len);
            *v = 1;
        }
        win_free(d);
        return 1;
    }
    }
    return 0;
}

/* ================================================================ native DLLs */

/* a call of the program's for a DLL: its arguments and memory */
struct win_dll_call {
    args *a;
};

static uint16_t call_arg16(win_dll_call *c, unsigned off) { return w(c->a, off); }
static uint32_t call_arg32(win_dll_call *c, unsigned off) { return dw(c->a, off); }
static int call_read(win_dll_call *c, uint32_t far, void *dst, uint32_t n) { return rd_(c->a, far, dst, n); }
static int call_write(win_dll_call *c, uint32_t far, const void *src, uint32_t n)
{
    return wr_(c->a, far, src, n);
}
static int call_string(win_dll_call *c, uint32_t far, char *dst, uint32_t cap)
{
    return str_(c->a, far, dst, cap);
}

static const win_dll_ne dll_ne = {
    sizeof(win_dll_ne), call_arg16, call_arg32, call_read, call_write, call_string,
};

const win_dll_ne *win_dll_ne_glue(void) { return &dll_ne; }

static int dll_slot_of(uint16_t id, uint16_t *ord)
{
    *ord = (uint16_t)((id - WIN_NE_SERVICE_DLL) & 0x3ffu);
    return (int)((id - WIN_NE_SERVICE_DLL) >> 10);
}

static __attribute__((noinline)) int dll_service(args *a, uint16_t service, uint32_t *v,
                                                int *dword)
{
    uint16_t ord;
    const win_dll_info *d = win_dll_get(dll_slot_of(service, &ord));
    win_dll_call c = { a };
    int r = d && d->ne_call ? d->ne_call(&c, ord, v) : WINDLL_NONE;
    *dword = r == WINDLL_DWORD;
    return r != WINDLL_NONE;
}

int win_ne_native_import(const char *module, uint16_t ordinal, const char *name, uint16_t *id)
{
    int s = win_dll_load(module);
    if (s < 0)
        return 0;
    const win_dll_export *e = win_dll_export_of(s, ordinal, name);
    if (!e && name) {
        win_dll_unref(s);
        return -1;
    }
    if (!e && ordinal >= 0x400u) {
        win_dll_unref(s);
        return -1;
    }
    *id = WIN_NE_DLL_SERVICE(s, e ? e->ordinal : ordinal);  /* none: not supported, at its call */
    return 1;
}

int win_ne_native_has(const char *module, uint16_t ordinal, const char *name)
{
    uint16_t id;
    int r = win_ne_native_import(module, ordinal, name, &id);
    if (r > 0)
        win_dll_unref(dll_slot_of(id, &ordinal));
    return r;
}

uint16_t win_ne_native_args(uint16_t id)
{
    uint16_t ord;
    int s = dll_slot_of(id, &ord);
    const win_dll_export *e = win_dll_export_of(s, ord, NULL);
    return e ? e->argbytes : 0;
}

const char *win_ne_native_name(uint16_t id, char *buf, unsigned cap)
{
    uint16_t ord;
    int s = dll_slot_of(id, &ord);
    const win_dll_info *d = win_dll_get(s);
    const win_dll_export *e = win_dll_export_of(s, ord, NULL);
    if (!d)
        return NULL;
    if (e && e->name)
        wsprintf(buf, "%s.%s", d->name, e->name);
    else
        wsprintf(buf, "%s.%u", d->name, (unsigned)ord);
    (void)cap;
    return buf;
}

/* ================================================================ */

int ne_sys_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    if (service >= MM && service < MM + 0x800u)
        return mm_service(a, u, service, v, dword);
    if (service >= TH && service < TH + 0x100u)
        return th_service(a, u, service, v, dword);
    if (service >= VR && service < VR + 0x100u)
        return ver_service(a, service, v, dword);
    if (service >= WIN_NE_SERVICE_DLL && service < WIN_NE_SERVICE_UNDEFINED)
        return dll_service(a, service, v, dword);
    return 0;
}

void ne_sys_end(ne_user *u)
{
    for (int i = 0; i < 8; i++) {
        if (u->mmtimer[i].proc)
            KillTimer(0, u->mmtimer[i].id);
        u->mmtimer[i].proc = 0;
    }
}

#endif
