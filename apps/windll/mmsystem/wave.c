/*
 * wave.c - WINN's Win16 waveOut implementation and installable native
 * wave-driver dispatcher. SYSTEM.INI [drivers] wave=SBWAVE.DLL selects
 * the module; the module returns a win_wave_driver interface.
 *
 * The Win16 WAVEHDR stays in guest memory. Buffers are read one DMA block
 * at a time (rather than retaining a guest pointer across callbacks).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"
#include "wave_driver.h"

#define NE (__win_dll_ne)
#define WAVE_HANDLE    0x6e01u
#define WAVE_QUEUE     16
#define WAVE_SAMPLES   2048u

#define MMSYSERR_NOMEM         7u
#define MMSYSERR_NOTSUPPORTED 8u
#define MMSYSERR_ALLOCATED    4u
#define WAVERR_STILLPLAYING   33u
#define WAVERR_UNPREPARED     34u
#define WAVERR_BADFORMAT      32u

#define WAVE_FORMAT_QUERY     0x0001u
#define WAVE_MAPPER           0xffffu
#define CALLBACK_TYPEMASK    0x00070000ul
#define CALLBACK_WINDOW      0x00010000ul
#define CALLBACK_FUNCTION    0x00030000ul
#define MM_WOM_OPEN          0x03bbu
#define MM_WOM_CLOSE         0x03bcu
#define MM_WOM_DONE          0x03bdu
#define WHDR_DONE            0x00000001ul
#define WHDR_PREPARED        0x00000002ul
#define WHDR_INQUEUE         0x00000010ul

/* Win16 WAVEFORMAT: PCM fields through wBitsPerSample, 16 bytes. */
typedef struct {
    uint16_t channels, bits, block;
    uint32_t rate;
} pcm_format;

typedef struct {
    uint32_t header, data, bytes, position;
} queued_wave;

typedef struct {
    HTASK task;
    HMODULE module;
    const win_wave_driver *driver;
    uint32_t callback, instance, flags;
    uint32_t played_bytes;
    pcm_format format;
    UINT timer;
    int paused, playing;
    uint32_t active_bytes;
    queued_wave queue[WAVE_QUEUE];
    unsigned head, count;
} wave_state;

static wave_state output;
static uint8_t raw[WAVE_SAMPLES * 4u];
static uint8_t pcm[WAVE_SAMPLES];

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

/* Per-call references prevent a driver from surviving the task that loaded
 * it. waveOutOpen retains a reference until waveOutClose/task_end. */
static const win_wave_driver *wave_load(HMODULE *module)
{
    char name[64];
    *module = 0;
    if (!GetPrivateProfileString("drivers", "wave", "", name, sizeof(name), "SYSTEM.INI") ||
        !name[0])
        return NULL;
    HMODULE h = LoadLibrary(name);
    if (h < 32u)
        return NULL;
    win_wave_driver_entry get = (win_wave_driver_entry)GetProcAddress(h, "WinWaveDriver");
    const win_wave_driver *d = get ? get() : NULL;
    if (!d || d->version != WIN_WAVE_DRIVER_VERSION || d->size < sizeof(*d) ||
        !d->present || !d->open || !d->close || !d->submit || !d->finished ||
        !d->stop || !d->pause || !d->max_bytes || !d->present()) {
        FreeLibrary(h);
        return NULL;
    }
    *module = h;
    return d;
}

UINT WINAPI winn_wave_num(void)
{
    HMODULE h;
    const win_wave_driver *d = wave_load(&h);
    if (!d)
        return 0;
    FreeLibrary(h);
    return 1;
}

static MMRESULT valid_device(unsigned id)
{
    return (id == 0 || id == WAVE_MAPPER) && winn_wave_num() ?
           MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
}

static MMRESULT wave_format(win_dll_call *c, uint32_t far, pcm_format *fmt)
{
    uint8_t b[16];
    if (!(far >> 16) || !NE->read(c, far, b, sizeof(b)))
        return MMSYSERR_INVALPARAM;
    unsigned bits = rd16(b + 14), channels = rd16(b + 2);
    uint32_t rate = rd32(b + 4);
    unsigned block = rd16(b + 12);
    if (rd16(b) != 1 || (bits != 8 && bits != 16) ||
        (channels != 1 && channels != 2) || rate < 4000u || rate > 48000u ||
        block != channels * (bits / 8u) || rd32(b + 8) != rate * block)
        return WAVERR_BADFORMAT;
    fmt->bits = (uint16_t)bits;
    fmt->channels = (uint16_t)channels;
    fmt->block = (uint16_t)block;
    fmt->rate = rate;
    return MMSYSERR_NOERROR;
}

static void wave_notify(UINT msg, uint32_t param)
{
    if ((output.flags & CALLBACK_TYPEMASK) == CALLBACK_WINDOW) {
        PostMessage((HWND)(uint16_t)output.callback, msg, WAVE_HANDLE, (LPARAM)param);
    } else if ((output.flags & CALLBACK_TYPEMASK) == CALLBACK_FUNCTION &&
               output.callback && NE && NE->call) {
        const uint16_t args[8] = {
            WAVE_HANDLE, (uint16_t)msg,
            (uint16_t)(output.instance >> 16), (uint16_t)output.instance,
            (uint16_t)(param >> 16), (uint16_t)param, 0, 0,
        };
        NE->call((uint16_t)output.task, output.callback, args, 8);
    }
}

static int header_flags(uint32_t far, uint32_t *flags)
{
    uint8_t b[4];
    if (!NE->task_read((uint16_t)output.task, far + 16u, b, sizeof(b)))
        return 0;
    *flags = rd32(b);
    return 1;
}

static int set_header_flags(uint32_t far, uint32_t flags)
{
    uint8_t b[4];
    wr32(b, flags);
    return NE->task_write((uint16_t)output.task, far + 16u, b, sizeof(b));
}

static void wave_return_head(int notify)
{
    if (!output.count)
        return;
    queued_wave old = output.queue[output.head];
    output.head = (output.head + 1u) % WAVE_QUEUE;
    output.count--;
    uint32_t flags;
    if (header_flags(old.header, &flags))
        set_header_flags(old.header, (flags & ~WHDR_INQUEUE) | WHDR_DONE);
    if (notify)
        wave_notify(MM_WOM_DONE, old.header);
}

static void wave_pump(void)
{
    if (!output.driver || output.paused)
        return;
    if (output.playing) {
        if (!output.driver->finished())
            return;
        output.playing = 0;
        if (output.count) {
            output.queue[output.head].position += output.active_bytes;
            output.played_bytes += output.active_bytes;
        }
        output.active_bytes = 0;
    }
    while (output.driver && output.count && !output.playing && !output.paused) {
        queued_wave *q = &output.queue[output.head];
        if (q->position >= q->bytes) {
            wave_return_head(1);
            continue;
        }
        unsigned frames = (q->bytes - q->position) / output.format.block;
        unsigned max = output.driver->max_bytes;
        if (max > WAVE_SAMPLES)
            max = WAVE_SAMPLES;
        if (frames > max)
            frames = max;
        if (!frames)
            break;
        unsigned bytes = frames * output.format.block;
        uint32_t addr = q->data + q->position;
        /* A Win16 buffer must remain within the same selector. */
        if ((uint16_t)addr < (uint16_t)q->data ||
            !NE->task_read((uint16_t)output.task, addr, raw, bytes)) {
            output.driver->stop();
            wave_return_head(1);
            continue;
        }
        for (unsigned i = 0; i < frames; i++) {
            unsigned at = i * output.format.block;
            if (output.format.bits == 8) {
                unsigned v = raw[at];
                if (output.format.channels == 2)
                    v = (v + raw[at + 1u]) / 2u;
                pcm[i] = (uint8_t)v;
            } else {
                int32_t v = (int16_t)rd16(raw + at);
                if (output.format.channels == 2)
                    v = (v + (int16_t)rd16(raw + at + 2u)) / 2;
                pcm[i] = (uint8_t)((v + 32768) >> 8);
            }
        }
        if (!output.driver->submit(pcm, frames, output.format.rate)) {
            output.driver->stop();
            wave_return_head(1);
            continue;
        }
        output.active_bytes = bytes;
        output.playing = 1;
    }
}

static void CALLBACK wave_tick(HWND hwnd, UINT msg, UINT id, DWORD ticks)
{
    (void)hwnd; (void)msg; (void)id; (void)ticks;
    if (output.task == GetCurrentTask())
        wave_pump();
}

static MMRESULT wave_open(win_dll_call *c)
{
    uint32_t ph = NE->arg32(c, 22), fmtfar = NE->arg32(c, 16);
    unsigned id = NE->arg16(c, 20);
    uint32_t flags = NE->arg32(c, 4);
    if (valid_device(id))
        return MMSYSERR_BADDEVICEID;
    pcm_format fmt;
    MMRESULT err = wave_format(c, fmtfar, &fmt);
    if (err)
        return err;
    if (flags & WAVE_FORMAT_QUERY)
        return 0;
    uint8_t zero[2] = {0, 0};
    if (!(ph >> 16) || !NE->write(c, ph, zero, 2))
        return MMSYSERR_INVALPARAM;
    if (output.driver)
        return MMSYSERR_ALLOCATED;
    if (flags & ~(WAVE_FORMAT_QUERY | CALLBACK_TYPEMASK))
        return MMSYSERR_INVALPARAM;
    unsigned cb = (unsigned)(flags & CALLBACK_TYPEMASK);
    if (cb != 0 && cb != CALLBACK_WINDOW && cb != CALLBACK_FUNCTION)
        return MMSYSERR_INVALPARAM;
    HMODULE h;
    const win_wave_driver *d = wave_load(&h);
    if (!d)
        return MMSYSERR_NODRIVER;
    if (!d->open()) {
        FreeLibrary(h);
        return MMSYSERR_ALLOCATED;
    }
    memset(&output, 0, sizeof(output));
    output.driver = d;
    output.module = h;
    output.task = GetCurrentTask();
    output.format = fmt;
    output.flags = flags;
    output.callback = NE->arg32(c, 12);
    output.instance = NE->arg32(c, 8);
    output.timer = SetTimer(0, 0, 10, wave_tick);
    if (!output.timer) {
        d->close();
        FreeLibrary(h);
        memset(&output, 0, sizeof(output));
        return MMSYSERR_NOMEM;
    }
    uint8_t hb[2];
    wr16(hb, WAVE_HANDLE);
    NE->write(c, ph, hb, 2);
    wave_notify(MM_WOM_OPEN, 0);
    return 0;
}

static MMRESULT wave_reset(int notify)
{
    if (!output.driver)
        return MMSYSERR_INVALHANDLE;
    output.driver->stop();
    output.playing = 0;
    output.active_bytes = 0;
    output.paused = 0;
    while (output.count)
        wave_return_head(notify);
    output.played_bytes = 0;
    return 0;
}

static MMRESULT wave_close(void)
{
    if (!output.driver)
        return MMSYSERR_INVALHANDLE;
    if (output.count)
        return WAVERR_STILLPLAYING;
    wave_notify(MM_WOM_CLOSE, 0);
    if (output.timer)
        KillTimer(0, output.timer);
    HMODULE h = output.module;
    output.driver->close();
    memset(&output, 0, sizeof(output));
    FreeLibrary(h);
    return 0;
}

static MMRESULT wave_header(win_dll_call *c, unsigned ordinal)
{
    uint32_t hdrfar = NE->arg32(c, 6);
    unsigned cbwh = NE->arg16(c, 4);
    if (!output.driver || output.task != GetCurrentTask() ||
        NE->arg16(c, 10) != WAVE_HANDLE)
        return MMSYSERR_INVALHANDLE;
    if (cbwh < 32u || !(hdrfar >> 16))
        return MMSYSERR_INVALPARAM;
    uint8_t b[32];
    if (!NE->read(c, hdrfar, b, sizeof(b)))
        return MMSYSERR_INVALPARAM;
    uint32_t f = rd32(b + 16);
    if (ordinal == 406) {              /* waveOutPrepareHeader */
        if (f & WHDR_INQUEUE)
            return WAVERR_STILLPLAYING;
        return set_header_flags(hdrfar, (f | WHDR_PREPARED) & ~WHDR_DONE) ? 0 : MMSYSERR_INVALPARAM;
    }
    if (ordinal == 407) {              /* waveOutUnprepareHeader */
        if (f & WHDR_INQUEUE)
            return WAVERR_STILLPLAYING;
        return set_header_flags(hdrfar, f & ~WHDR_PREPARED) ? 0 : MMSYSERR_INVALPARAM;
    }
    if (!(f & WHDR_PREPARED))
        return WAVERR_UNPREPARED;
    if (f & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    unsigned bytes = rd32(b + 4);
    uint32_t data = rd32(b);
    if (!bytes || !data || !(data >> 16) || bytes % output.format.block ||
        bytes > 0x10000u - (uint16_t)data || output.count == WAVE_QUEUE ||
        (f & (0x04u | 0x08u)))
        return MMSYSERR_INVALPARAM;      /* looped WAVEHDRs not yet supported */
    queued_wave *q = &output.queue[(output.head + output.count) % WAVE_QUEUE];
    q->header = hdrfar;
    q->data = data;
    q->bytes = bytes;
    q->position = 0;
    if (!set_header_flags(hdrfar, (f | WHDR_INQUEUE) & ~WHDR_DONE))
        return MMSYSERR_INVALPARAM;
    output.count++;
    return 0;
}

static MMRESULT wave_caps(win_dll_call *c)
{
    if (valid_device(NE->arg16(c, 10)))
        return MMSYSERR_BADDEVICEID;
    uint32_t far = NE->arg32(c, 6);
    unsigned size = NE->arg16(c, 4);
    if (!(far >> 16) || !size)
        return MMSYSERR_INVALPARAM;
    uint8_t b[48];
    memset(b, 0, sizeof(b));
    wr16(b, 2);                        /* Creative Labs (MM_CREATIVE) */
    wr16(b + 2, 4);                    /* Sound Blaster 16 */
    wr16(b + 4, 0x030a);
    memcpy(b + 6, "SB16 (murm386)", sizeof("SB16 (murm386)"));
    wr32(b + 38, 0x0fffu);            /* 11/22/44 kHz PCM, mono/stereo, 8/16-bit */
    wr16(b + 42, 2);                  /* input stereo is converted to mono */
    /* dwSupport at +44: no per-channel volume/pitch/loop control */
    if (size > sizeof(b))
        size = sizeof(b);
    return NE->write(c, far, b, size) ? 0 : MMSYSERR_INVALPARAM;
}

static MMRESULT wave_position(win_dll_call *c)
{
    if (!output.driver || NE->arg16(c, 10) != WAVE_HANDLE)
        return MMSYSERR_INVALHANDLE;
    if (NE->arg16(c, 4) < 6)
        return MMSYSERR_INVALPARAM;
    uint32_t far = NE->arg32(c, 6);
    uint8_t b[6];
    if (!(far >> 16) || !NE->read(c, far, b, 2))
        return MMSYSERR_INVALPARAM;
    uint32_t n = output.played_bytes;
    switch (rd16(b)) {
    case 2: n /= output.format.block; break; /* TIME_SAMPLES */
    case 4: break;                            /* TIME_BYTES */
    default:
        wr16(b, TIME_MS);
        n = (n / output.format.block) * 1000u / output.format.rate;
        break;
    }
    wr32(b + 2, n);
    return NE->write(c, far, b, sizeof(b)) ? 0 : MMSYSERR_INVALPARAM;
}

/* Returns WINDLL_WORD for a supported Win16 waveOut ordinal. */
int winn_wave_ne_call(win_dll_call *c, uint16_t o, uint32_t *v)
{
    if (o < 401 || o > 421)
        return WINDLL_NONE;
    MMRESULT r = MMSYSERR_NOTSUPPORTED;
    switch (o) {
    case 401:
        *v = winn_wave_num();
        return WINDLL_WORD;
    case 402: r = wave_caps(c); break;
    case 404: r = wave_open(c); break;
    case 405:
        r = NE->arg16(c, 4) == WAVE_HANDLE ? wave_close() : MMSYSERR_INVALHANDLE;
        break;
    case 406: case 407: case 408: r = wave_header(c, o); break;
    case 409:                                 /* waveOutPause */
        if (output.driver && NE->arg16(c, 4) == WAVE_HANDLE) {
            output.paused = 1;
            output.driver->pause(1);
            r = 0;
        } else r = MMSYSERR_INVALHANDLE;
        break;
    case 410:                                 /* waveOutRestart */
        if (output.driver && NE->arg16(c, 4) == WAVE_HANDLE) {
            output.paused = 0;
            output.driver->pause(0);
            r = 0;
        } else r = MMSYSERR_INVALHANDLE;
        break;
    case 411:
        r = NE->arg16(c, 4) == WAVE_HANDLE ? wave_reset(1) : MMSYSERR_INVALHANDLE;
        break;
    case 412: r = wave_position(c); break;
    case 420:                                 /* waveOutGetID */
        if (output.driver && NE->arg16(c, 8) == WAVE_HANDLE &&
            NE->arg32(c, 4) && (NE->arg32(c, 4) >> 16)) {
            uint8_t b[2] = {0, 0};
            r = NE->write(c, NE->arg32(c, 4), b, sizeof(b)) ? 0 : MMSYSERR_INVALPARAM;
        } else r = MMSYSERR_INVALHANDLE;
        break;
    case 403: return WINDLL_NONE;             /* shared GetErrorText */
    default: break;
    }
    *v = r;
    return o == 421 ? WINDLL_DWORD : WINDLL_WORD;
}

void winn_wave_task_end(uint16_t task)
{
    if (output.driver && (uint16_t)output.task == task) {
        wave_reset(0);
        if (output.timer)
            KillTimer(0, output.timer);
        output.driver->close();
        /* The core releases this task's DLL references after task_end. */
        memset(&output, 0, sizeof(output));
    }
}

void winn_wave_term(void)
{
    if (output.driver) {
        wave_reset(0);
        if (output.timer)
            KillTimer(0, output.timer);
        output.driver->close();
        memset(&output, 0, sizeof(output));
    }
}
