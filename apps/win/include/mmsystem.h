/*
 * mmsystem.h - Windows' multimedia system (MMSYSTEM.DLL, a native DLL of
 * WINN's: apps/windll/mmsystem) for native callers, who take its
 * functions with LoadLibrary("MMSYSTEM") and GetProcAddress, as Windows
 * 3.1's own. Win16 waveOut and midiOut use native drivers selected in
 * SYSTEM.INI. Wave input and MCI remain unavailable. Timers and mmio
 * files (RIFF) work.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_MMSYSTEM_H
#define WINN_MMSYSTEM_H

#include "windows.h"

typedef UINT MMRESULT;
typedef DWORD FOURCC;
typedef UINT HMMIO;

#define MMSYSERR_NOERROR        0
#define MMSYSERR_BADDEVICEID    2
#define MMSYSERR_INVALHANDLE    5
#define MMSYSERR_NODRIVER       6
#define MMSYSERR_INVALPARAM     11
#define JOYERR_UNPLUGGED        167
#define MCIERR_DEVICE_NOT_INSTALLED 306
#define MMIOERR_FILENOTFOUND    257
#define MMIOERR_CANNOTOPEN      259
#define MMIOERR_CANNOTREAD      261
#define MMIOERR_CANNOTWRITE     262
#define MMIOERR_CANNOTSEEK      263
#define MMIOERR_CHUNKNOTFOUND   265
#define MMIOERR_UNBUFFERED      266

#define TIME_ONESHOT    0
#define TIME_PERIODIC   1
#define TIME_MS         1

#define MMIO_READ       0x0000
#define MMIO_WRITE      0x0001
#define MMIO_READWRITE  0x0002
#define MMIO_CREATE     0x1000
#define MMIO_PARSE      0x0100
#define MMIO_DELETE     0x0200
#define MMIO_EXIST      0x4000
#define MMIO_TOUPPER    0x0010
#define MMIO_FINDCHUNK  0x0010
#define MMIO_FINDRIFF   0x0020
#define MMIO_FINDLIST   0x0040
#define MMIO_CREATERIFF 0x0020
#define MMIO_CREATELIST 0x0040
#define MMIO_DIRTY      0x10000000ul

#define mmioFOURCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | \
                                (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)

typedef struct { UINT wType; DWORD ms; } MMTIME, *LPMMTIME;     /* TIME_MS only */
typedef struct { UINT wPeriodMin, wPeriodMax; } TIMECAPS, *LPTIMECAPS;
typedef struct {
    FOURCC ckid;
    DWORD cksize;
    FOURCC fccType;
    DWORD dwDataOffset;
    DWORD dwFlags;
} MMCKINFO, *LPMMCKINFO;
/* only what WINN looks at: an I/O procedure or memory is not its */
typedef struct {
    DWORD dwFlags;
    FOURCC fccIOProc;
    void *pIOProc;
    UINT wErrorRet;
    HMMIO hmmio;
} MMIOINFO, *LPMMIOINFO;

typedef void (CALLBACK *LPTIMECALLBACK)(UINT id, UINT msg, DWORD user, DWORD dw1, DWORD dw2);

/* the functions' types, for GetProcAddress (their names: Windows') */
typedef DWORD (WINAPI *TIMEGETTIME)(void);
typedef UINT (WINAPI *TIMESETEVENT)(UINT delay, UINT resolution, LPTIMECALLBACK proc,
                                    DWORD user, UINT flags);
typedef MMRESULT (WINAPI *TIMEKILLEVENT)(UINT id);
typedef BOOL (WINAPI *SNDPLAYSOUND)(LPCSTR sound, UINT flags);
typedef UINT (WINAPI *WAVEOUTGETNUMDEVS)(void);
typedef HMMIO (WINAPI *MMIOOPEN)(LPSTR name, LPMMIOINFO info, DWORD flags);
typedef MMRESULT (WINAPI *MMIOCLOSE)(HMMIO h, UINT flags);
typedef LONG (WINAPI *MMIOREAD)(HMMIO h, char *buf, LONG n);
typedef LONG (WINAPI *MMIOWRITE)(HMMIO h, const char *buf, LONG n);
typedef LONG (WINAPI *MMIOSEEK)(HMMIO h, LONG off, int origin);
typedef MMRESULT (WINAPI *MMIODESCEND)(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent,
                                       UINT flags);
typedef MMRESULT (WINAPI *MMIOASCEND)(HMMIO h, LPMMCKINFO ck, UINT flags);
typedef MMRESULT (WINAPI *MMIOCREATECHUNK)(HMMIO h, LPMMCKINFO ck, UINT flags);

#endif /* WINN_MMSYSTEM_H */
