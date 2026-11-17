/*
 * mixer.c - MMSYSTEM's mixer (Windows 95's mixer API: mixerGetNumDevs,
 * mixerGetLineInfo, mixerGetLineControls, mixerGet/SetControlDetails ...)
 * over the volumes WINN keeps (volume.c). One mixer device; its one
 * destination, the speakers, is the master volume; its sources the wave
 * output and the synthesizer (MIDI). Each line has a volume (the left and
 * the right channel, 0 to 65535) and a mute: the same volumes as
 * waveOutSetVolume's, midiOutSetVolume's, auxSetVolume's and Control
 * Panel's, so whichever changes them, the mixer's windows (opened with
 * CALLBACK_WINDOW) are told MM_MIXM_CONTROL_CHANGE.
 *
 * Line IDs: the speakers FFFF0000h, the wave output 0, the synthesizer
 * 10000h; control IDs 1 to 6 (a line's volume, its mute).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"

#define NE (__win_dll_ne)
#define MIXER_BASE 0x7001u
#define MIXERS 8
#define WAVE_HANDLE 0x6e01u
#define MIDI_HANDLE_BASE 0x6f01u
#ifndef MMSYSERR_NOTSUPPORTED
#define MMSYSERR_NOTSUPPORTED 8
#endif
#ifndef MMSYSERR_ALLOCATED
#define MMSYSERR_ALLOCATED 4
#endif
#ifndef MMSYSERR_INVALFLAG
#define MMSYSERR_INVALFLAG 10
#endif
#define OBJECT_MASK 0xf0000000u

DWORD winn_volume_get(int line);
void winn_volume_set(int line, DWORD v);
int winn_mute_get(int line);
void winn_mute_set(int line, int m);
UINT WINAPI winn_wave_num(void);
UINT WINAPI winn_midi_num(void);

enum { L_DEST, L_WAVE, L_SYNTH, LINES };    /* volume.c's lines the same */
static const DWORD line_ids[LINES] = { 0xffff0000u, 0x00000000u, 0x00010000u };

static struct { int used; HWND hwnd; HTASK task; } opened[MIXERS];

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

static int line_of(DWORD id)
{
    for (int i = 0; i < LINES; i++)
        if (line_ids[i] == id)
            return i;
    return -1;
}

static DWORD control_id(int line, int mute) { return (DWORD)(line * 2 + mute + 1); }

/* a control's line, -1 for none; mute or the volume */
static int control_line(DWORD id, int *mute)
{
    if (id < 1 || id > 2u * LINES)
        return -1;
    *mute = (int)((id - 1) & 1);
    return (int)((id - 1) / 2);
}

/* a mixer object (the flags say what it is): 0 if it is this mixer's */
static MMRESULT object(HMIXEROBJ h, DWORD flags)
{
    switch (flags & OBJECT_MASK) {
    case MIXER_OBJECTF_MIXER:
        return h == 0 ? 0 : MMSYSERR_BADDEVICEID;
    case MIXER_OBJECTF_HMIXER:
        return h >= MIXER_BASE && h < MIXER_BASE + MIXERS && opened[h - MIXER_BASE].used ? 0 :
               MMSYSERR_INVALHANDLE;
    case MIXER_OBJECTF_WAVEOUT:
        return (h == 0 || h == 0xffffu) && winn_wave_num() ? 0 : MMSYSERR_BADDEVICEID;
    case MIXER_OBJECTF_HWAVEOUT:
        return h == WAVE_HANDLE ? 0 : MMSYSERR_INVALHANDLE;
    case MIXER_OBJECTF_MIDIOUT:
        return h < winn_midi_num() || (h == 0xffffu && winn_midi_num()) ? 0 : MMSYSERR_BADDEVICEID;
    case MIXER_OBJECTF_HMIDIOUT:
        return h >= MIDI_HANDLE_BASE && h < MIDI_HANDLE_BASE + 4u ? 0 : MMSYSERR_INVALHANDLE;
    case MIXER_OBJECTF_AUX:
        return h == 0 ? 0 : MMSYSERR_BADDEVICEID;
    }
    return MMSYSERR_NODRIVER;           /* the inputs: none of them */
}

/* volume.c: a line's volume (control 0) or mute (1) changed */
void winn_mixer_changed(int line, int control)
{
    for (unsigned i = 0; i < MIXERS; i++)
        if (opened[i].used && opened[i].hwnd && IsWindow(opened[i].hwnd))
            PostMessage(opened[i].hwnd, MM_MIXM_CONTROL_CHANGE, (WPARAM)(MIXER_BASE + i),
                        (LPARAM)control_id(line, control));
}

void winn_mixer_task_end(uint16_t task)
{
    for (unsigned i = 0; i < MIXERS; i++)
        if (opened[i].used && opened[i].task == (HTASK)task)
            opened[i].used = 0;
}

/* ---------------------------------------------------------------- the API */

UINT WINAPI mixerGetNumDevs(void) { return 1; }

MMRESULT WINAPI mixerGetDevCaps(UINT id, LPMIXERCAPS caps, UINT size)
{
    MIXERCAPS c;
    if (object(id, id >= MIXER_BASE ? MIXER_OBJECTF_HMIXER : MIXER_OBJECTF_MIXER))
        return MMSYSERR_BADDEVICEID;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    memset(&c, 0, sizeof(c));
    c.wMid = 2;                         /* Creative Labs, as the wave output's */
    c.wPid = 4;
    c.vDriverVersion = 0x0100;
    lstrcpy(c.szPname, "SB16 Mixer (murm386)");
    c.cDestinations = 1;
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return 0;
}

MMRESULT WINAPI mixerOpen(HMIXER *h, UINT id, DWORD callback, DWORD instance, DWORD flags)
{
    (void)instance;
    if (h)
        *h = 0;
    MMRESULT r = object(id, flags);
    if (r)
        return r;
    DWORD type = flags & CALLBACK_TYPEMASK;
    if (type && type != CALLBACK_WINDOW)
        return MMSYSERR_INVALFLAG;
    if (type == CALLBACK_WINDOW && !IsWindow((HWND)callback))
        return MMSYSERR_INVALPARAM;
    for (unsigned i = 0; i < MIXERS; i++)
        if (!opened[i].used) {
            opened[i].used = 1;
            opened[i].hwnd = type == CALLBACK_WINDOW ? (HWND)callback : 0;
            opened[i].task = GetCurrentTask();
            if (h)
                *h = MIXER_BASE + i;
            return 0;
        }
    return MMSYSERR_ALLOCATED;
}

MMRESULT WINAPI mixerClose(HMIXER h)
{
    if (object(h, MIXER_OBJECTF_HMIXER))
        return MMSYSERR_INVALHANDLE;
    opened[h - MIXER_BASE].used = 0;
    return 0;
}

DWORD WINAPI mixerMessage(HMIXER h, UINT msg, DWORD p1, DWORD p2)
{
    (void)msg; (void)p1; (void)p2;
    return object(h, MIXER_OBJECTF_HMIXER) ? MMSYSERR_INVALHANDLE : MMSYSERR_NOTSUPPORTED;
}

MMRESULT WINAPI mixerGetID(HMIXEROBJ h, UINT *id, DWORD flags)
{
    MMRESULT r = object(h, flags);
    if (r)
        return r;
    if (!id)
        return MMSYSERR_INVALPARAM;
    *id = 0;
    return 0;
}

static void fill_line(int l, MIXERLINE *ml)
{
    static const char *const shorts[LINES] = { "Master", "Wave", "MIDI" };
    static const char *const longs[LINES] = { "Master Volume", "Wave Output", "MIDI Synthesizer" };
    DWORD size = ml->cbStruct;
    memset(ml, 0, sizeof(*ml));
    ml->cbStruct = size;
    ml->dwSource = l == L_SYNTH ? 1 : 0;
    ml->dwLineID = line_ids[l];
    ml->fdwLine = MIXERLINE_LINEF_ACTIVE | (l != L_DEST ? MIXERLINE_LINEF_SOURCE : 0);
    if (l == L_WAVE && !winn_wave_num())
        ml->fdwLine &= ~(DWORD)MIXERLINE_LINEF_ACTIVE;
    if (l == L_SYNTH && !winn_midi_num())
        ml->fdwLine &= ~(DWORD)MIXERLINE_LINEF_ACTIVE;
    ml->dwComponentType = l == L_DEST ? MIXERLINE_COMPONENTTYPE_DST_SPEAKERS :
                          l == L_WAVE ? MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT :
                                        MIXERLINE_COMPONENTTYPE_SRC_SYNTHESIZER;
    ml->cChannels = 2;
    ml->cConnections = l == L_DEST ? 2 : 0;
    ml->cControls = 2;
    lstrcpyn(ml->szShortName, shorts[l], MIXER_SHORT_NAME_CHARS);
    lstrcpyn(ml->szName, longs[l], MIXER_LONG_NAME_CHARS);
    if (l == L_WAVE) {
        ml->Target.dwType = MIXERLINE_TARGETTYPE_WAVEOUT;
        ml->Target.wMid = 2;
        ml->Target.wPid = 4;
        ml->Target.vDriverVersion = 0x030a;
        lstrcpy(ml->Target.szPname, "SB16 (murm386)");
    } else if (l == L_SYNTH) {
        ml->Target.dwType = MIXERLINE_TARGETTYPE_MIDIOUT;
        ml->Target.vDriverVersion = 0x0100;
        lstrcpy(ml->Target.szPname, "MIDI Output");
    }
}

MMRESULT WINAPI mixerGetLineInfo(HMIXEROBJ h, LPMIXERLINE ml, DWORD flags)
{
    MMRESULT r = object(h, flags);
    if (r)
        return r;
    if (!ml || ml->cbStruct < sizeof(MIXERLINE))
        return MMSYSERR_INVALPARAM;
    int l = -1;
    switch (flags & MIXER_GETLINEINFOF_QUERYMASK) {
    case MIXER_GETLINEINFOF_DESTINATION:
        l = ml->dwDestination == 0 ? L_DEST : -1;
        break;
    case MIXER_GETLINEINFOF_SOURCE:
        l = ml->dwDestination != 0 ? -1 : ml->dwSource == 0 ? L_WAVE : ml->dwSource == 1 ? L_SYNTH : -1;
        break;
    case MIXER_GETLINEINFOF_LINEID:
        l = line_of(ml->dwLineID);
        break;
    case MIXER_GETLINEINFOF_COMPONENTTYPE:
        l = ml->dwComponentType == MIXERLINE_COMPONENTTYPE_DST_SPEAKERS ? L_DEST :
            ml->dwComponentType == MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT ? L_WAVE :
            ml->dwComponentType == MIXERLINE_COMPONENTTYPE_SRC_SYNTHESIZER ? L_SYNTH : -1;
        break;
    case MIXER_GETLINEINFOF_TARGETTYPE:
        l = ml->Target.dwType == MIXERLINE_TARGETTYPE_WAVEOUT ? L_WAVE :
            ml->Target.dwType == MIXERLINE_TARGETTYPE_MIDIOUT ? L_SYNTH : -1;
        break;
    default:
        return MMSYSERR_INVALFLAG;
    }
    if (l < 0)
        return MIXERR_INVALLINE;
    fill_line(l, ml);
    return 0;
}

static void fill_control(int l, int mute, MIXERCONTROL *c)
{
    static const char *const longs[LINES] = { "Master", "Wave", "MIDI" };
    memset(c, 0, sizeof(*c));
    c->cbStruct = sizeof(*c);
    c->dwControlID = control_id(l, mute);
    c->dwControlType = mute ? MIXERCONTROL_CONTROLTYPE_MUTE : MIXERCONTROL_CONTROLTYPE_VOLUME;
    c->fdwControl = mute ? MIXERCONTROL_CONTROLF_UNIFORM : 0;
    lstrcpy(c->szShortName, mute ? "Mute" : "Volume");
    wsprintf(c->szName, "%s %s", longs[l], mute ? "Mute" : "Volume");
    c->Bounds.dwMinimum = 0;
    c->Bounds.dwMaximum = mute ? 1 : 65535;
    c->Metrics.cSteps = mute ? 0 : 65535;
}

MMRESULT WINAPI mixerGetLineControls(HMIXEROBJ h, LPMIXERLINECONTROLS lc, DWORD flags)
{
    MMRESULT r = object(h, flags);
    if (r)
        return r;
    if (!lc || lc->cbStruct < sizeof(MIXERLINECONTROLS) || !lc->pamxctrl ||
        lc->cbmxctrl < sizeof(MIXERCONTROL))
        return MMSYSERR_INVALPARAM;
    int l, mute;
    switch (flags & MIXER_GETLINECONTROLSF_QUERYMASK) {
    case MIXER_GETLINECONTROLSF_ALL:
        if ((l = line_of(lc->dwLineID)) < 0)
            return MIXERR_INVALLINE;
        if (lc->cControls != 2)
            return MMSYSERR_INVALPARAM;
        fill_control(l, 0, lc->pamxctrl);
        fill_control(l, 1, (MIXERCONTROL *)(void *)((char *)lc->pamxctrl + lc->cbmxctrl));
        return 0;
    case MIXER_GETLINECONTROLSF_ONEBYID:
        if ((l = control_line(lc->dwControlID, &mute)) < 0)
            return MIXERR_INVALCONTROL;
        lc->dwLineID = line_ids[l];
        fill_control(l, mute, lc->pamxctrl);
        return 0;
    case MIXER_GETLINECONTROLSF_ONEBYTYPE:
        if ((l = line_of(lc->dwLineID)) < 0)
            return MIXERR_INVALLINE;
        if (lc->dwControlType != MIXERCONTROL_CONTROLTYPE_VOLUME &&
            lc->dwControlType != MIXERCONTROL_CONTROLTYPE_MUTE)
            return MIXERR_INVALCONTROL;
        fill_control(l, lc->dwControlType == MIXERCONTROL_CONTROLTYPE_MUTE, lc->pamxctrl);
        return 0;
    }
    return MMSYSERR_INVALFLAG;
}

/* the details' control: its line, mute or not; an error */
static MMRESULT details(HMIXEROBJ h, const MIXERCONTROLDETAILS *cd, DWORD flags, int *l, int *mute)
{
    MMRESULT r = object(h, flags);
    if (r)
        return r;
    if (!cd || cd->cbStruct < sizeof(MIXERCONTROLDETAILS) || !cd->paDetails ||
        cd->cbDetails < sizeof(DWORD) || cd->cMultipleItems || cd->cChannels < 1 ||
        cd->cChannels > 2)
        return MMSYSERR_INVALPARAM;
    if ((flags & MIXER_GETCONTROLDETAILSF_QUERYMASK) != MIXER_GETCONTROLDETAILSF_VALUE)
        return MMSYSERR_INVALFLAG;
    if ((*l = control_line(cd->dwControlID, mute)) < 0)
        return MIXERR_INVALCONTROL;
    return 0;
}

MMRESULT WINAPI mixerGetControlDetails(HMIXEROBJ h, LPMIXERCONTROLDETAILS cd, DWORD flags)
{
    int l, mute;
    MMRESULT r = details(h, cd, flags, &l, &mute);
    if (r)
        return r;
    DWORD v = winn_volume_get(l), left = v & 0xffffu, right = v >> 16;
    DWORD *d0 = (DWORD *)cd->paDetails;
    DWORD *d1 = (DWORD *)(void *)((char *)cd->paDetails + cd->cbDetails);
    if (mute) {
        *d0 = (DWORD)winn_mute_get(l);
        if (cd->cChannels == 2)
            *d1 = *d0;
    } else if (cd->cChannels == 1) {
        *d0 = left > right ? left : right;
    } else {
        *d0 = left;
        *d1 = right;
    }
    return 0;
}

MMRESULT WINAPI mixerSetControlDetails(HMIXEROBJ h, LPMIXERCONTROLDETAILS cd, DWORD flags)
{
    int l, mute;
    MMRESULT r = details(h, cd, flags, &l, &mute);
    if (r)
        return r;
    const DWORD *d0 = (const DWORD *)cd->paDetails;
    const DWORD *d1 = (const DWORD *)(const void *)((const char *)cd->paDetails + cd->cbDetails);
    if (mute) {
        winn_mute_set(l, *d0 != 0);
        return 0;
    }
    DWORD left = *d0, right = cd->cChannels == 2 ? *d1 : *d0;
    if (left > 0xffffu || right > 0xffffu)
        return MIXERR_INVALVALUE;
    winn_volume_set(l, left | right << 16);
    return 0;
}

/* ---------------------------------------------------------------- Win16 programs */

/* MIXERLINE16: Target.vDriverVersion a WORD (166 bytes, Win32's 168) */
#define LINE16 166u
#define CONTROL16 148u                  /* MIXERCONTROL: the same */

static void line_in(const uint8_t *b, MIXERLINE *ml)
{
    memset(ml, 0, sizeof(*ml));
    ml->cbStruct = sizeof(*ml);
    ml->dwDestination = rd32(b + 4);
    ml->dwSource = rd32(b + 8);
    ml->dwLineID = rd32(b + 12);
    ml->dwComponentType = rd32(b + 24);
    ml->Target.dwType = rd32(b + 120);
}

static void line_out(const MIXERLINE *ml, uint8_t *b)
{
    wr32(b + 4, ml->dwDestination);
    wr32(b + 8, ml->dwSource);
    wr32(b + 12, ml->dwLineID);
    wr32(b + 16, ml->fdwLine);
    wr32(b + 20, ml->dwUser);
    wr32(b + 24, ml->dwComponentType);
    wr32(b + 28, ml->cChannels);
    wr32(b + 32, ml->cConnections);
    wr32(b + 36, ml->cControls);
    memcpy(b + 40, ml->szShortName, 16);
    memcpy(b + 56, ml->szName, 64);
    wr32(b + 120, ml->Target.dwType);
    wr32(b + 124, ml->Target.dwDeviceID);
    wr16(b + 128, ml->Target.wMid);
    wr16(b + 130, ml->Target.wPid);
    wr16(b + 132, ml->Target.vDriverVersion);
    memcpy(b + 134, ml->Target.szPname, 32);
}

static void control_out(const MIXERCONTROL *c, uint8_t *b)
{
    memset(b, 0, CONTROL16);
    wr32(b, CONTROL16);
    wr32(b + 4, c->dwControlID);
    wr32(b + 8, c->dwControlType);
    wr32(b + 12, c->fdwControl);
    wr32(b + 16, c->cMultipleItems);
    memcpy(b + 20, c->szShortName, 16);
    memcpy(b + 36, c->szName, 64);
    wr32(b + 100, c->Bounds.dwMinimum);
    wr32(b + 104, c->Bounds.dwMaximum);
    wr32(b + 124, c->Metrics.cSteps);
}

/* Win16's 800-809: (HMIXEROBJ, LPSTRUCT, DWORD flags) mostly, the flags at 4 */
int winn_mixer_ne_call(win_dll_call *c, uint16_t o, uint32_t *v)
{
    uint8_t b[24 + CONTROL16];          /* a line's, or a structure and a control */
    MMRESULT r = 0;
    switch (o) {
    case 800:
        *v = mixerGetNumDevs();
        return WINDLL_WORD;
    case 801: {                         /* (UINT, LPMIXERCAPS, UINT): 46 bytes */
        MIXERCAPS caps;
        UINT size = NE->arg16(c, 4);
        uint32_t far = NE->arg32(c, 6);
        r = mixerGetDevCaps(NE->arg16(c, 10), &caps, sizeof(caps));
        if (!r) {
            memset(b, 0, 46);
            wr16(b, caps.wMid);
            wr16(b + 2, caps.wPid);
            wr16(b + 4, caps.vDriverVersion);
            memcpy(b + 6, caps.szPname, 32);
            wr32(b + 38, caps.fdwSupport);
            wr32(b + 42, caps.cDestinations);
            if (!(far >> 16) || !NE->write(c, far, b, size < 46 ? size : 46))
                r = MMSYSERR_INVALPARAM;
        }
        break;
    }
    case 802: {                         /* (LPHMIXER, UINT, DWORD, DWORD, DWORD) */
        HMIXER h = 0;
        uint32_t far = NE->arg32(c, 18);
        r = mixerOpen(&h, NE->arg16(c, 16), NE->arg32(c, 12), NE->arg32(c, 8), NE->arg32(c, 4));
        wr16(b, h);
        if ((far >> 16) && !NE->write(c, far, b, 2) && !r) {
            mixerClose(h);
            r = MMSYSERR_INVALPARAM;
        }
        break;
    }
    case 803:
        r = mixerClose(NE->arg16(c, 4));
        break;
    case 804:                           /* (HMIXER, UINT, DWORD, DWORD): DWORD */
        *v = mixerMessage(NE->arg16(c, 12), NE->arg16(c, 10), NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_DWORD;
    case 805: {                         /* GetLineInfo */
        MIXERLINE ml;
        uint32_t far = NE->arg32(c, 8);
        if (!(far >> 16) || !NE->read(c, far, b, LINE16) || rd32(b) < LINE16) {
            r = MMSYSERR_INVALPARAM;
            break;
        }
        line_in(b, &ml);
        r = mixerGetLineInfo(NE->arg16(c, 12), &ml, NE->arg32(c, 4));
        if (!r) {
            line_out(&ml, b);
            if (!NE->write(c, far, b, LINE16))
                r = MMSYSERR_INVALPARAM;
        }
        break;
    }
    case 806: {                         /* GetID(HMIXEROBJ, UINT FAR *, DWORD) */
        UINT id = 0;
        r = mixerGetID(NE->arg16(c, 12), &id, NE->arg32(c, 4));
        wr16(b, id);
        if (!r && !NE->write(c, NE->arg32(c, 8), b, 2))
            r = MMSYSERR_INVALPARAM;
        break;
    }
    case 807: {                         /* GetLineControls: 24 bytes, the controls apart */
        MIXERLINECONTROLS lc;
        MIXERCONTROL ctl[2];
        uint32_t far = NE->arg32(c, 8);
        if (!(far >> 16) || !NE->read(c, far, b, 24) || rd32(b) < 24) {
            r = MMSYSERR_INVALPARAM;
            break;
        }
        uint32_t each = rd32(b + 16), at = rd32(b + 20);
        lc.cbStruct = sizeof(lc);
        lc.dwLineID = rd32(b + 4);
        lc.dwControlID = rd32(b + 8);
        lc.cControls = rd32(b + 12);
        lc.cbmxctrl = sizeof(MIXERCONTROL);
        lc.pamxctrl = ctl;
        if (each < CONTROL16 || !(at >> 16) || lc.cControls > 2) {
            r = MMSYSERR_INVALPARAM;
            break;
        }
        DWORD flags = NE->arg32(c, 4);
        r = mixerGetLineControls(NE->arg16(c, 12), &lc, flags);
        unsigned n = (flags & MIXER_GETLINECONTROLSF_QUERYMASK) == MIXER_GETLINECONTROLSF_ALL ? 2 : 1;
        for (unsigned i = 0; !r && i < n; i++) {
            control_out(&ctl[i], b + 24);
            if (!NE->write(c, at + i * each, b + 24, CONTROL16))
                r = MMSYSERR_INVALPARAM;
        }
        wr32(b + 4, lc.dwLineID);
        if (!r && !NE->write(c, far, b, 24))
            r = MMSYSERR_INVALPARAM;
        break;
    }
    case 808: case 809: {               /* Get/SetControlDetails: 24 bytes, the values apart */
        MIXERCONTROLDETAILS cd;
        DWORD vals[2] = { 0, 0 };
        uint32_t far = NE->arg32(c, 8);
        if (!(far >> 16) || !NE->read(c, far, b, 24) || rd32(b) < 24) {
            r = MMSYSERR_INVALPARAM;
            break;
        }
        uint32_t each = rd32(b + 16), at = rd32(b + 20);
        memset(&cd, 0, sizeof(cd));
        cd.cbStruct = sizeof(cd);
        cd.dwControlID = rd32(b + 4);
        cd.cChannels = rd32(b + 8);
        cd.cMultipleItems = rd32(b + 12);
        cd.cbDetails = sizeof(DWORD);
        cd.paDetails = vals;
        if (each < 4 || !(at >> 16) || cd.cChannels < 1 || cd.cChannels > 2) {
            r = MMSYSERR_INVALPARAM;
            break;
        }
        for (unsigned i = 0; o == 809 && i < cd.cChannels; i++) {
            if (!NE->read(c, at + i * each, b + 24, 4)) {
                r = MMSYSERR_INVALPARAM;
                break;
            }
            vals[i] = rd32(b + 24);
        }
        if (r)
            break;
        r = o == 808 ? mixerGetControlDetails(NE->arg16(c, 12), &cd, NE->arg32(c, 4)) :
                       mixerSetControlDetails(NE->arg16(c, 12), &cd, NE->arg32(c, 4));
        for (unsigned i = 0; o == 808 && !r && i < cd.cChannels; i++) {
            wr32(b + 24, vals[i]);
            if (!NE->write(c, at + i * each, b + 24, 4))
                r = MMSYSERR_INVALPARAM;
        }
        break;
    }
    default:
        return WINDLL_NONE;
    }
    *v = r;
    return WINDLL_WORD;
}
