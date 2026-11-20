/*
 * wave.c - WINN's Win16 waveOut implementation and installable native
 * wave-driver dispatcher. SYSTEM.INI [drivers] wave=SBWAVE.DLL selects
 * the module; the module returns a win_wave_driver interface.
 *
 * The Win16 WAVEHDR stays in guest memory. Buffers are read one driver
 * block at a time (rather than retaining a guest pointer across
 * callbacks), the next while one plays.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"
#include "wave_driver.h"

void winn_volume_pcm(uint8_t *p, unsigned n, unsigned bits, unsigned channels);   /* volume.c */
void winn_snd_release(void);                                                       /* snd.c */

#define NE (__win_dll_ne)
#define WAVE_HANDLE    0x6e01u
#define WAVE_QUEUE     16u              /* headers queued at first: more as written */
#define WAVE_BUFSIZE   8192u
#define WAVE_INFLIGHT  4u               /* blocks the driver may have queued */

#define MMSYSERR_NOMEM         7u
#define MMSYSERR_NOTSUPPORTED 8u
#define MMSYSERR_ALLOCATED    4u
#define WAVERR_STILLPLAYING   33u
#define WAVERR_UNPREPARED     34u
#define WAVERR_BADFORMAT      32u

#define WAVE_FORMAT_QUERY     0x0001u
#define WAVE_ALLOWSYNC        0x0002u
#define WAVE_MAPPER           0xffffu
#define CALLBACK_TYPEMASK    0x00070000ul
#define CALLBACK_WINDOW      0x00010000ul
#define CALLBACK_FUNCTION    0x00030000ul
#define MM_WOM_OPEN          0x03bbu
#define MM_WOM_CLOSE         0x03bcu
#define MM_WOM_DONE          0x03bdu
#define WHDR_DONE            0x00000001ul
#define WHDR_PREPARED        0x00000002ul
#define WHDR_BEGINLOOP       0x00000004ul
#define WHDR_ENDLOOP         0x00000008ul
#define WHDR_INQUEUE         0x00000010ul

/* Win16 WAVEFORMAT: PCM fields through wBitsPerSample, 16 bytes. */
typedef struct {
    uint16_t channels, bits, block;
    uint32_t rate;
} pcm_format;

typedef struct {
    uint32_t header, data, bytes;
    uint64_t sent, position;            /* total bytes submitted, played (all iterations) */
    uint64_t limit;                     /* bytes * dwLoops; shortened by BreakLoop */
    int loop;                           /* both WHDR_BEGINLOOP and WHDR_ENDLOOP */
} queued_wave;

typedef struct {
    HTASK task;
    HMODULE module;
    const win_wave_driver *driver;
    uint32_t callback, instance, flags;
    uint32_t played_bytes;
    pcm_format format;
    UINT timer;
    int paused;
    uint32_t inflight[WAVE_INFLIGHT];   /* the driver's blocks queued, their bytes */
    unsigned inflight_head, inflight_count;
    unsigned head, count;
} wave_state;

static wave_state output;
/* the headers written, not yet done: a ring (head, count) of queue_cap,
 * grown as a program writes more (all of a long sound at once) */
static queued_wave *queue;
static unsigned queue_cap;
static uint8_t raw[WAVE_BUFSIZE];

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
    win_wave_driver_entry get =
        (win_wave_driver_entry)(void (*)(void))GetProcAddress(h, "WinWaveDriver");
    const win_wave_driver *d = get ? get() : NULL;
    if (!d || d->version != WIN_WAVE_DRIVER_VERSION || d->size < sizeof(*d) ||
        !d->present || !d->open || !d->close || !d->submit || !d->finished ||
        !d->stop || !d->pause || !d->max_bytes || !d->room || !d->present()) {
        FreeLibrary(h);
        return NULL;
    }
    *module = h;
    return d;
}

/* the device's there: known once its driver has loaded (asked again while
 * it has not: SYSTEM.INI may name one later), not loaded from the card at
 * every question, nor failing for a moment's want of memory */
static int wave_present;

UINT WINAPI winn_wave_num(void)
{
    HMODULE h;
    if (wave_present || output.driver)
        return 1;
    const win_wave_driver *d = wave_load(&h);
    if (!d)
        return 0;
    FreeLibrary(h);
    wave_present = 1;
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
    queued_wave old = queue[output.head];
    output.head = (output.head + 1u) % queue_cap;
    output.count--;
    uint32_t flags;
    if (header_flags(old.header, &flags))
        set_header_flags(old.header, (flags & ~WHDR_INQUEUE) | WHDR_DONE);
    if (notify)
        wave_notify(MM_WOM_DONE, old.header);
}

/* bytes played (the driver's blocks): the headers they finish go back */
static void wave_played(uint32_t bytes)
{
    output.played_bytes += bytes;
    while (bytes && output.count) {
        queued_wave *q = &queue[output.head];
        uint32_t take = bytes;
        if (q->limit - q->position < take)
            take = (uint32_t)(q->limit - q->position);
        q->position += take;
        bytes -= take;
        if (q->position == q->limit)
            wave_return_head(1);
    }
}

/* Win16 huge allocations are tiled at 64 KiB with __AHINCR = 8.
 * A far pointer is selector:offset, so ordinary DWORD arithmetic cannot
 * advance it across a tile boundary (it increments selector by 1, not 8).
 * Preserve the existing flat-program behaviour. */
static int wave_data_read(uint16_t task, uint32_t far, uint32_t offset,
                          uint8_t *dst, unsigned count)
{
    int flat = NE->size > offsetof(win_dll_ne, task_flat) && NE->task_flat &&
               NE->task_flat(task);
    if (flat)
        return NE->task_read(task, far + offset, dst, count);

    uint64_t pos = (uint16_t)far + (uint64_t)offset;
    uint64_t selector = (far >> 16) + (pos >> 16) * 8u;
    if (selector > 0xffffu)
        return 0;
    while (count) {
        unsigned off = (unsigned)pos & 0xffffu;
        unsigned n = 0x10000u - off;
        if (n > count)
            n = count;
        if (!NE->task_read(task, (uint32_t)(selector << 16) | off, dst, n))
            return 0;
        dst += n;
        pos += n;
        count -= n;
        if (count && (selector += 8u) > 0xffffu)
            return 0;
    }
    return 1;
}

/* The driver's blocks played, the headers they finish returned; then as
 * many blocks as it takes, each filled from the headers in turn (a block
 * may hold the end of one and the start of the next): the next is queued
 * while one plays. */
static void wave_pump(void)
{
    if (!output.driver || output.paused)
        return;
    for (int n = output.inflight_count ? output.driver->finished() : 0;
         n > 0 && output.inflight_count; n--) {
        uint32_t bytes = output.inflight[output.inflight_head];
        output.inflight_head = (output.inflight_head + 1u) % WAVE_INFLIGHT;
        output.inflight_count--;
        wave_played(bytes);
    }
    while (output.driver && !output.paused && output.inflight_count < WAVE_INFLIGHT &&
           output.driver->room() > 0) {
        unsigned max = output.driver->max_bytes, got = 0;
        if (max > sizeof(raw))
            max = sizeof(raw);
        max -= max % output.format.block;
        for (unsigned i = 0; i < output.count && got < max; i++) {
            queued_wave *q = &queue[(output.head + i) % queue_cap];
            while (q->sent < q->limit && got < max) {
                uint32_t offset = q->loop ? (uint32_t)(q->sent % q->bytes) : (uint32_t)q->sent;
                unsigned take = q->bytes - offset;
                if (take > max - got)
                    take = max - got;
                if (q->limit - q->sent < take)
                    take = (unsigned)(q->limit - q->sent);
                if (!wave_data_read((uint16_t)output.task, q->data, offset,
                                    raw + got, take))
                    memset(raw + got, output.format.bits == 16 ? 0 : 0x80, take);
                q->sent += take;
                got += take;
            }
        }
        if (!got)
            break;
        winn_volume_pcm(raw, got, output.format.bits, output.format.channels);
        if (!output.driver->submit(raw, got, output.format.rate,
                                   output.format.bits, output.format.channels)) {
            wave_played(got);           /* not playable: as if it were played */
            break;
        }
        output.inflight[(output.inflight_head + output.inflight_count) % WAVE_INFLIGHT] = got;
        output.inflight_count++;
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
    if (flags & ~(WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC | CALLBACK_TYPEMASK))
        return MMSYSERR_INVALPARAM;
    unsigned cb = (unsigned)(flags & CALLBACK_TYPEMASK);
    if (cb != 0 && cb != CALLBACK_WINDOW && cb != CALLBACK_FUNCTION)
        return MMSYSERR_INVALPARAM;
    winn_snd_release();                 /* a sound playing: the device the program's */
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
    output.inflight_count = 0;
    output.paused = 0;
    while (output.count)
        wave_return_head(notify);
    output.played_bytes = 0;
    return 0;
}

/* Stop a single-header loop after the last iteration already submitted to
 * the driver.  Submitted blocks cannot be withdrawn without stopping DMA. */
static MMRESULT wave_break_loop(void)
{
    if (!output.driver)
        return MMSYSERR_INVALHANDLE;
    for (unsigned i = 0; i < output.count; i++) {
        queued_wave *q = &queue[(output.head + i) % queue_cap];
        if (!q->loop)
            continue;
        uint64_t end = ((q->sent + q->bytes - 1u) / q->bytes) * q->bytes;
        if (end < q->bytes)
            end = q->bytes;
        if (end < q->limit)
            q->limit = end;
        break;
    }
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
    int flat = NE->size > offsetof(win_dll_ne, task_flat) && NE->task_flat &&
               NE->task_flat((uint16_t)output.task);
    uint32_t loop_flags = f & (WHDR_BEGINLOOP | WHDR_ENDLOOP);
    if (!bytes || !data || !(data >> 16) || bytes % output.format.block ||
        (loop_flags && loop_flags != (WHDR_BEGINLOOP | WHDR_ENDLOOP)))
        return MMSYSERR_INVALPARAM;      /* multi-header loops not yet supported */
    /* A single selector cannot represent a buffer crossing 64 KiB.  Huge
     * allocations use the next descriptor (selector + 8) for each tile.
     * Check the final byte before queueing it; ordinary short buffers use
     * the unchanged path. */
    if (!flat && bytes > 0x10000u - (uint16_t)data) {
        uint8_t last;
        if (!wave_data_read((uint16_t)output.task, data, bytes - 1u, &last, 1))
            return MMSYSERR_INVALPARAM;
    }
    if (output.count == queue_cap) {    /* the ring grown, in order from its head */
        unsigned cap = queue_cap ? queue_cap * 2u : WAVE_QUEUE;
        queued_wave *grown = (queued_wave *)malloc(cap * sizeof(queued_wave));
        if (!grown)
            return MMSYSERR_NOMEM;
        for (unsigned i = 0; i < output.count; i++)
            grown[i] = queue[(output.head + i) % queue_cap];
        free(queue);
        queue = grown;
        queue_cap = cap;
        output.head = 0;
    }
    queued_wave *q = &queue[(output.head + output.count) % queue_cap];
    q->header = hdrfar;
    q->data = data;
    q->bytes = bytes;
    q->position = q->sent = 0;
    q->loop = loop_flags != 0;
    uint32_t loops = q->loop ? rd32(b + 20) : 1u;
    q->limit = (uint64_t)bytes * (loops ? loops : 1u);
    if (!set_header_flags(hdrfar, (f | WHDR_INQUEUE) & ~WHDR_DONE))
        return MMSYSERR_INVALPARAM;
    output.count++;
    wave_pump();                        /* playing at once, not at the next tick */
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
    wr16(b + 42, 2);                  /* native stereo channels */
    b[44] = 0x0c;                     /* dwSupport: WAVECAPS_VOLUME, LRVOLUME (volume.c) */
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
    case 419:                                 /* waveOutBreakLoop */
        r = output.task == GetCurrentTask() && NE->arg16(c, 4) == WAVE_HANDLE ?
            wave_break_loop() : MMSYSERR_INVALHANDLE;
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
    free(queue);
    queue = NULL;
    queue_cap = 0;
}
