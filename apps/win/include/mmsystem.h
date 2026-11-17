/*
 * mmsystem.h - Windows' multimedia system (MMSYSTEM.DLL, a native DLL of
 * WINN's: apps/windll/mmsystem) for native callers, who take its
 * functions with LoadLibrary("MMSYSTEM") and GetProcAddress, as Windows
 * 3.1's own. Win16 waveOut and midiOut use native drivers selected in
 * SYSTEM.INI. MCI supports Standard MIDI Files via the MIDI drivers.
 * Wave input and MCI waveaudio remain unavailable. Timers and mmio
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

/* the volumes (a DWORD: the left channel low, the right high, 0-FFFFh):
 * waveOut's, midiOut's and the master's (aux device 0), kept in SYSTEM.INI
 * [volume]; by device ID (or a handle). GetProcAddress names them. */
typedef MMRESULT (WINAPI *WAVEOUTGETVOLUME)(UINT id, LPDWORD volume);
typedef MMRESULT (WINAPI *WAVEOUTSETVOLUME)(UINT id, DWORD volume);
typedef MMRESULT (WINAPI *MIDIOUTGETVOLUME)(UINT id, LPDWORD volume);
typedef MMRESULT (WINAPI *MIDIOUTSETVOLUME)(UINT id, DWORD volume);
typedef UINT (WINAPI *AUXGETNUMDEVS)(void);
typedef MMRESULT (WINAPI *AUXGETVOLUME)(UINT id, LPDWORD volume);
typedef MMRESULT (WINAPI *AUXSETVOLUME)(UINT id, DWORD volume);
MMRESULT WINAPI waveOutGetVolume(UINT id, LPDWORD volume);
MMRESULT WINAPI waveOutSetVolume(UINT id, DWORD volume);
MMRESULT WINAPI midiOutGetVolume(UINT id, LPDWORD volume);
MMRESULT WINAPI midiOutSetVolume(UINT id, DWORD volume);
UINT     WINAPI auxGetNumDevs(void);
MMRESULT WINAPI auxGetDevCaps(UINT id, void *caps, UINT size);
MMRESULT WINAPI auxGetVolume(UINT id, LPDWORD volume);
MMRESULT WINAPI auxSetVolume(UINT id, DWORD volume);

/* The mixer (Windows 95's API, mixer.c): one device, its destination the
 * speakers (the master volume) with two sources, the wave output and the
 * synthesizer (MIDI); each line a volume and a mute (the volumes above,
 * the same ones). The structures are Win32's (a Win16 program's are
 * converted). */
#define CALLBACK_TYPEMASK 0x00070000ul
#define CALLBACK_WINDOW 0x00010000ul
#define CALLBACK_FUNCTION 0x00030000ul
typedef UINT HMIXER;
typedef UINT HMIXEROBJ;
#define MIXER_SHORT_NAME_CHARS  16
#define MIXER_LONG_NAME_CHARS   64
typedef struct {
    WORD wMid, wPid;
    UINT vDriverVersion;
    char szPname[32];
    DWORD fdwSupport, cDestinations;
} MIXERCAPS, *LPMIXERCAPS;
typedef struct {
    DWORD cbStruct, dwDestination, dwSource, dwLineID, fdwLine, dwUser, dwComponentType;
    DWORD cChannels, cConnections, cControls;
    char szShortName[MIXER_SHORT_NAME_CHARS], szName[MIXER_LONG_NAME_CHARS];
    struct {
        DWORD dwType, dwDeviceID;
        WORD wMid, wPid;
        UINT vDriverVersion;
        char szPname[32];
    } Target;
} MIXERLINE, *LPMIXERLINE;
typedef struct {
    DWORD cbStruct, dwControlID, dwControlType, fdwControl, cMultipleItems;
    char szShortName[MIXER_SHORT_NAME_CHARS], szName[MIXER_LONG_NAME_CHARS];
    union {
        struct { LONG lMinimum, lMaximum; };
        struct { DWORD dwMinimum, dwMaximum; };
        DWORD dwReserved[6];
    } Bounds;
    union {
        DWORD cSteps, cbCustomData;
        DWORD dwReserved[6];
    } Metrics;
} MIXERCONTROL, *LPMIXERCONTROL;
typedef struct {
    DWORD cbStruct, dwLineID;
    union { DWORD dwControlID, dwControlType; };
    DWORD cControls, cbmxctrl;
    LPMIXERCONTROL pamxctrl;
} MIXERLINECONTROLS, *LPMIXERLINECONTROLS;
typedef struct {
    DWORD cbStruct, dwControlID, cChannels;
    union { HWND hwndOwner; DWORD cMultipleItems; };
    DWORD cbDetails;
    void *paDetails;
} MIXERCONTROLDETAILS, *LPMIXERCONTROLDETAILS;
typedef struct { DWORD dwValue; } MIXERCONTROLDETAILS_UNSIGNED;
typedef struct { LONG fValue; } MIXERCONTROLDETAILS_BOOLEAN;

#define MIXER_OBJECTF_HANDLE            0x80000000L
#define MIXER_OBJECTF_MIXER             0x00000000L
#define MIXER_OBJECTF_HMIXER            0x80000000L
#define MIXER_OBJECTF_WAVEOUT           0x10000000L
#define MIXER_OBJECTF_HWAVEOUT          0x90000000L
#define MIXER_OBJECTF_WAVEIN            0x20000000L
#define MIXER_OBJECTF_HWAVEIN           0xA0000000L
#define MIXER_OBJECTF_MIDIOUT           0x30000000L
#define MIXER_OBJECTF_HMIDIOUT          0xB0000000L
#define MIXER_OBJECTF_MIDIIN            0x40000000L
#define MIXER_OBJECTF_HMIDIIN           0xC0000000L
#define MIXER_OBJECTF_AUX               0x50000000L
#define MIXER_GETLINEINFOF_DESTINATION  0x00000000L
#define MIXER_GETLINEINFOF_SOURCE       0x00000001L
#define MIXER_GETLINEINFOF_LINEID       0x00000002L
#define MIXER_GETLINEINFOF_COMPONENTTYPE 0x00000003L
#define MIXER_GETLINEINFOF_TARGETTYPE   0x00000004L
#define MIXER_GETLINEINFOF_QUERYMASK    0x0000000FL
#define MIXER_GETLINECONTROLSF_ALL      0x00000000L
#define MIXER_GETLINECONTROLSF_ONEBYID  0x00000001L
#define MIXER_GETLINECONTROLSF_ONEBYTYPE 0x00000002L
#define MIXER_GETLINECONTROLSF_QUERYMASK 0x0000000FL
#define MIXER_GETCONTROLDETAILSF_VALUE  0x00000000L
#define MIXER_GETCONTROLDETAILSF_LISTTEXT 0x00000001L
#define MIXER_GETCONTROLDETAILSF_QUERYMASK 0x0000000FL
#define MIXER_SETCONTROLDETAILSF_VALUE  0x00000000L
#define MIXER_SETCONTROLDETAILSF_QUERYMASK 0x0000000FL
#define MIXERLINE_LINEF_ACTIVE          0x00000001L
#define MIXERLINE_LINEF_SOURCE          0x80000000L
#define MIXERLINE_COMPONENTTYPE_DST_SPEAKERS    4
#define MIXERLINE_COMPONENTTYPE_SRC_SYNTHESIZER 0x1004
#define MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT     0x1008
#define MIXERLINE_TARGETTYPE_UNDEFINED  0
#define MIXERLINE_TARGETTYPE_WAVEOUT    1
#define MIXERLINE_TARGETTYPE_MIDIOUT    3
#define MIXERCONTROL_CONTROLF_UNIFORM   0x00000001L
#define MIXERCONTROL_CONTROLTYPE_MUTE   0x20010002L
#define MIXERCONTROL_CONTROLTYPE_VOLUME 0x50030001L
#define MIXERR_INVALLINE                1024
#define MIXERR_INVALCONTROL             1025
#define MIXERR_INVALVALUE               1026
#define MM_MIXM_LINE_CHANGE             0x3D0
#define MM_MIXM_CONTROL_CHANGE          0x3D1
UINT     WINAPI mixerGetNumDevs(void);
MMRESULT WINAPI mixerGetDevCaps(UINT id, LPMIXERCAPS caps, UINT size);
MMRESULT WINAPI mixerOpen(HMIXER *h, UINT id, DWORD callback, DWORD instance, DWORD flags);
MMRESULT WINAPI mixerClose(HMIXER h);
DWORD    WINAPI mixerMessage(HMIXER h, UINT msg, DWORD p1, DWORD p2);
MMRESULT WINAPI mixerGetLineInfo(HMIXEROBJ h, LPMIXERLINE line, DWORD flags);
MMRESULT WINAPI mixerGetID(HMIXEROBJ h, UINT *id, DWORD flags);
MMRESULT WINAPI mixerGetLineControls(HMIXEROBJ h, LPMIXERLINECONTROLS lc, DWORD flags);
MMRESULT WINAPI mixerGetControlDetails(HMIXEROBJ h, LPMIXERCONTROLDETAILS cd, DWORD flags);
MMRESULT WINAPI mixerSetControlDetails(HMIXEROBJ h, LPMIXERCONTROLDETAILS cd, DWORD flags);

#endif /* WINN_MMSYSTEM_H */
