/*
 * midi.c - Win16 midiOut API using installable native MIDI drivers.
 * SYSTEM.INI [drivers] midi=MPUMIDI.DLL, midi1=OPLMIDI.DLL, ...
 * Each driver writes to the emulated ISA device, not RP2350 audio hardware.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"
#include "midi_driver.h"

#define NE (__win_dll_ne)
#define MIDI_SLOTS 4u
#define MIDI_HANDLE_BASE 0x6f01u
#define MIDI_MAPPER 0xffffu
#define MMSYSERR_ALLOCATED 4u
#define MMSYSERR_NOTSUPPORTED 8u
#define MMSYSERR_NOMEM 7u
#define MIDIERR_UNPREPARED 64u
#define MIDIERR_STILLPLAYING 65u
#define CALLBACK_TYPEMASK 0x00070000ul
#define CALLBACK_WINDOW 0x00010000ul
#define CALLBACK_FUNCTION 0x00030000ul
#define MM_MOM_OPEN  0x03c7u
#define MM_MOM_CLOSE 0x03c8u
#define MHDR_PREPARED 0x00000002ul
#define MHDR_INQUEUE  0x00000004ul

typedef struct {
    HMODULE module;
    const win_midi_driver *driver;
    HTASK task;
    uint32_t callback, instance, flags;
    uint16_t device;
} midi_open_state;
static midi_open_state devices[MIDI_SLOTS];
static midi_open_state mci_output;   /* reserves the selected physical port */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

static const win_midi_driver *driver_load(unsigned cfg, HMODULE *module)
{
    char key[8], name[64];
    *module = 0;
    lstrcpy(key, "midi");
    if (cfg) {
        key[4] = (char)('0' + cfg);
        key[5] = 0;
    }
    if (!GetPrivateProfileString("drivers", key, "", name, sizeof(name), "SYSTEM.INI") ||
        !name[0]) return NULL;
    HMODULE h = LoadLibrary(name);
    if (h < 32u) return NULL;
    win_midi_driver_entry entry =
        (win_midi_driver_entry)(void (*)(void))GetProcAddress(h, "WinMidiDriver");
    const win_midi_driver *d = entry ? entry() : NULL;
    if (!d || d->version != WIN_MIDI_DRIVER_VERSION || d->size < sizeof(*d) ||
        !d->present || !d->open || !d->close || !d->short_msg || !d->reset ||
        !d->name || !d->present()) {
        FreeLibrary(h);
        return NULL;
    }
    *module = h;
    return d;
}

/* Device IDs are compacted over installed/present MIDI drivers. */
static const win_midi_driver *driver_by_id(unsigned id, HMODULE *module)
{
    unsigned found = 0;
    if (id == MIDI_MAPPER) id = 0;
    for (unsigned cfg = 0; cfg < MIDI_SLOTS; cfg++) {
        HMODULE h;
        const win_midi_driver *d = driver_load(cfg, &h);
        if (!d) continue;
        if (found++ == id) {
            *module = h;
            return d;
        }
        FreeLibrary(h);
    }
    *module = 0;
    return NULL;
}

/* MCI and midiOut must not open the same hardware driver simultaneously. */
int winn_midi_mci_open(uint16_t task)
{
    if (mci_output.driver) return 0;
    HMODULE h;
    const win_midi_driver *d = driver_by_id(0, &h);
    if (!d) return 0;
    for (unsigned i = 0; i < MIDI_SLOTS; i++) {
        if (devices[i].driver == d) {
            FreeLibrary(h);
            return 0;
        }
    }
    if (!d->open()) { FreeLibrary(h); return 0; }
    memset(&mci_output, 0, sizeof(mci_output));
    mci_output.module = h;
    mci_output.driver = d;
    mci_output.task = task;
    return 1;
}
int winn_midi_mci_send(uint32_t packed)
{
    return mci_output.driver && mci_output.task == GetCurrentTask() &&
           mci_output.driver->short_msg(packed);
}
void winn_midi_mci_reset(void)
{
    if (mci_output.driver) mci_output.driver->reset();
}
void winn_midi_mci_close(uint16_t task)
{
    if (!mci_output.driver || (task && mci_output.task != task)) return;
    mci_output.driver->reset();
    mci_output.driver->close();
    HMODULE h = mci_output.module;
    memset(&mci_output, 0, sizeof(mci_output));
    FreeLibrary(h);
}

UINT WINAPI winn_midi_num(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < MIDI_SLOTS; i++) {
        HMODULE h;
        if (driver_load(i, &h)) {
            count++;
            FreeLibrary(h);
        }
    }
    return count;
}

static midi_open_state *state_of(unsigned handle)
{
    unsigned i = handle - MIDI_HANDLE_BASE;
    return handle >= MIDI_HANDLE_BASE && i < MIDI_SLOTS && devices[i].driver &&
           devices[i].task == GetCurrentTask() ? &devices[i] : NULL;
}

static void notify(midi_open_state *s, unsigned handle, unsigned msg)
{
    if ((s->flags & CALLBACK_TYPEMASK) == CALLBACK_WINDOW)
        PostMessage((HWND)(uint16_t)s->callback, msg, handle, 0);
    else if ((s->flags & CALLBACK_TYPEMASK) == CALLBACK_FUNCTION &&
             s->callback && NE && NE->call) {
        uint16_t args[8] = {
            (uint16_t)handle, (uint16_t)msg,
            (uint16_t)(s->instance >> 16), (uint16_t)s->instance,
            0, 0, 0, 0,
        };
        NE->call((uint16_t)s->task, s->callback, args, 8);
    }
}

static MMRESULT out_caps(win_dll_call *c)
{
    unsigned id = NE->arg16(c, 10);
    uint32_t far = NE->arg32(c, 6);
    unsigned cb = NE->arg16(c, 4);
    HMODULE h;
    const win_midi_driver *d = driver_by_id(id, &h);
    if (!d) return MMSYSERR_BADDEVICEID;
    /* Windows 3.1 accepts a zero-length capabilities query. */
    if (!cb) { FreeLibrary(h); return MMSYSERR_NOERROR; }
    if (!(far >> 16)) { FreeLibrary(h); return MMSYSERR_INVALPARAM; }
    /* Win16 MIDIOUTCAPS: 2+2+2+32+2+2+2+2+4 = 50 bytes. */
    uint8_t caps[50];
    memset(caps, 0, sizeof(caps));
    wr16(caps, 0);                   /* no assigned hardware manufacturer */
    wr16(caps + 2, id + 1u);
    wr16(caps + 4, 0x0100u);
    lstrcpyn((char *)caps + 6, d->name, 32);
    wr16(caps + 38, d->technology);
    wr16(caps + 40, d->voices);
    wr16(caps + 42, d->notes);
    wr16(caps + 44, d->channels);
    wr32(caps + 46, 0);             /* no hardware volume / caching */
    if (cb > sizeof(caps)) cb = sizeof(caps);
    int ok = NE->write(c, far, caps, cb);
    FreeLibrary(h);
    return ok ? 0 : MMSYSERR_INVALPARAM;
}

static MMRESULT out_open(win_dll_call *c)
{
    uint32_t far = NE->arg32(c, 18);
    unsigned id = NE->arg16(c, 16);
    uint32_t cb = NE->arg32(c, 12), instance = NE->arg32(c, 8);
    uint32_t flags = NE->arg32(c, 4);
    if (!(far >> 16)) return MMSYSERR_INVALPARAM;
    uint8_t zero[2] = { 0, 0 };
    if (!NE->write(c, far, zero, sizeof(zero))) return MMSYSERR_INVALPARAM;
    unsigned type = flags & CALLBACK_TYPEMASK;
    if ((flags & ~CALLBACK_TYPEMASK) ||
        (type != 0 && type != CALLBACK_WINDOW && type != CALLBACK_FUNCTION))
        return MMSYSERR_INVALPARAM;
    unsigned slot = 0;
    while (slot < MIDI_SLOTS && devices[slot].driver) slot++;
    if (slot == MIDI_SLOTS) return MMSYSERR_ALLOCATED;
    HMODULE h;
    const win_midi_driver *d = driver_by_id(id, &h);
    if (!d) return MMSYSERR_BADDEVICEID;
    if (mci_output.driver == d) { FreeLibrary(h); return MMSYSERR_ALLOCATED; }
    for (unsigned i = 0; i < MIDI_SLOTS; i++)
        if (devices[i].driver == d) { FreeLibrary(h); return MMSYSERR_ALLOCATED; }
    if (!d->open()) { FreeLibrary(h); return MMSYSERR_ALLOCATED; }
    midi_open_state *s = &devices[slot];
    s->module = h;
    s->driver = d;
    s->task = GetCurrentTask();
    s->callback = cb;
    s->instance = instance;
    s->flags = flags;
    s->device = (uint16_t)(id == MIDI_MAPPER ? 0u : id);
    uint8_t hb[2];
    wr16(hb, MIDI_HANDLE_BASE + slot);
    NE->write(c, far, hb, sizeof(hb));
    notify(s, MIDI_HANDLE_BASE + slot, MM_MOM_OPEN);
    return 0;
}

static void close_state(unsigned slot, int send_message)
{
    midi_open_state *s = &devices[slot];
    if (!s->driver) return;
    s->driver->reset();
    if (send_message) notify(s, MIDI_HANDLE_BASE + slot, MM_MOM_CLOSE);
    s->driver->close();
    HMODULE h = s->module;
    memset(s, 0, sizeof(*s));
    FreeLibrary(h);
}

static MMRESULT out_header(win_dll_call *c, unsigned ordinal)
{
    unsigned handle = NE->arg16(c, 10);
    midi_open_state *s = state_of(handle);
    if (!s) return MMSYSERR_INVALHANDLE;
    uint32_t far = NE->arg32(c, 6);
    unsigned cb = NE->arg16(c, 4);
    if (cb < 28u || !(far >> 16)) return MMSYSERR_INVALPARAM;
    uint8_t b[4];
    if (!NE->read(c, far + 16u, b, 4)) return MMSYSERR_INVALPARAM;
    uint32_t flags = rd32(b);
    if (ordinal == 206) flags |= MHDR_PREPARED;
    else if (ordinal == 207) {
        if (flags & MHDR_INQUEUE) return MIDIERR_STILLPLAYING;
        flags &= ~MHDR_PREPARED;
    } else return MMSYSERR_NOTSUPPORTED; /* SysEx not implemented in MPU emulator */
    wr32(b, flags);
    return NE->write(c, far + 16u, b, 4) ? 0 : MMSYSERR_INVALPARAM;
}

int winn_midi_ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *value)
{
    if (ordinal < 201 || ordinal > 216) return WINDLL_NONE;
    MMRESULT res = MMSYSERR_NOTSUPPORTED;
    midi_open_state *s;
    switch (ordinal) {
    case 201: *value = winn_midi_num(); return WINDLL_WORD;
    case 202: res = out_caps(c); break;
    case 204: res = out_open(c); break;
    case 205: {
        unsigned handle = NE->arg16(c, 4);
        s = state_of(handle);
        if (!s) res = MMSYSERR_INVALHANDLE;
        else { close_state(handle - MIDI_HANDLE_BASE, 1); res = 0; }
        break;
    }
    case 206: case 207: case 209: res = out_header(c, ordinal); break;
    case 208: /* midiOutShortMsg(HMIDIOUT, DWORD) */
        s = state_of(NE->arg16(c, 8));
        res = !s ? MMSYSERR_INVALHANDLE :
              s->driver->short_msg(NE->arg32(c, 4)) ? 0 : MMSYSERR_NOTSUPPORTED;
        break;
    case 210: /* midiOutReset */
        s = state_of(NE->arg16(c, 4));
        if (!s) res = MMSYSERR_INVALHANDLE;
        else { s->driver->reset(); res = 0; }
        break;
    case 211: case 212: case 213: case 214:
        s = state_of(NE->arg16(c, ordinal == 211 || ordinal == 212 ? 8 : 12));
        res = s ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
        break;
    case 215: /* midiOutGetID(HMIDIOUT, UINT FAR *) */
        s = state_of(NE->arg16(c, 8));
        if (!s) res = MMSYSERR_INVALHANDLE;
        else {
            uint8_t b[2]; wr16(b, s->device);
            res = NE->write(c, NE->arg32(c, 4), b, sizeof(b)) ? 0 : MMSYSERR_INVALPARAM;
        }
        break;
    case 216: /* midiOutMessage (DWORD result) */
        *value = MMSYSERR_NOTSUPPORTED;
        return WINDLL_DWORD;
    default: return WINDLL_NONE; /* ordinal 203: common GetErrorText */
    }
    *value = res;
    return WINDLL_WORD;
}

void winn_midi_task_end(uint16_t task)
{
    if (mci_output.driver && mci_output.task == task) winn_midi_mci_close(task);
    for (unsigned i = 0; i < MIDI_SLOTS; i++)
        if (devices[i].driver && devices[i].task == task) close_state(i, 0);
}
void winn_midi_term(void)
{
    winn_midi_mci_close(0);
    for (unsigned i = 0; i < MIDI_SLOTS; i++)
        if (devices[i].driver) close_state(i, 0);
}
