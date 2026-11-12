/*
 * mci.c - Win16 MCI sequencer for standard MIDI files (SMF type 0/1).
 * WINN's installable MIDI output driver handles the actual synthesis via
 * the emulated MPU-401 or OPL2. This module does not touch the x86 CPU.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"

#define NE (__win_dll_ne)

#define MCI_OPEN            0x0803u
#define MCI_CLOSE           0x0804u
#define MCI_PLAY            0x0806u
#define MCI_SEEK            0x0807u
#define MCI_STOP            0x0808u
#define MCI_PAUSE           0x0809u
#define MCI_INFO            0x080au
#define MCI_GETDEVCAPS      0x080bu
#define MCI_SET             0x080du
#define MCI_SYSINFO         0x0810u
#define MCI_STATUS          0x0814u
#define MCI_LOAD            0x0850u
#define MCI_RESUME          0x0855u

#define MCI_NOTIFY          0x00000001ul
#define MCI_WAIT            0x00000002ul
#define MCI_FROM            0x00000004ul
#define MCI_TO              0x00000008ul
#define MCI_OPEN_ELEMENT    0x00000200ul
#define MCI_OPEN_ALIAS      0x00000400ul
#define MCI_OPEN_TYPE_ID    0x00001000ul
#define MCI_OPEN_TYPE       0x00002000ul
#define MCI_SEEK_TO_START   0x00000100ul
#define MCI_SEEK_TO_END     0x00000200ul
#define MCI_STATUS_ITEM     0x00000100ul
#define MCI_SET_TIME_FORMAT 0x00000400ul
#define MCI_SYSINFO_QUANTITY 0x00000100ul
#define MCI_SYSINFO_NAME    0x00000400ul
#define MCI_GETDEVCAPS_ITEM 0x00000100ul
#define MCI_LOAD_FILE       0x00000100ul
#define MCI_INFO_PRODUCT    0x00000100ul
#define MCI_INFO_FILE       0x00000200ul

#define MCI_DEVTYPE_SEQUENCER 523u
#define MCI_MODE_STOP       525u
#define MCI_MODE_PLAY       526u
#define MCI_MODE_PAUSE      529u
#define MCI_FORMAT_MILLISECONDS 0u
#define MCI_SEQ_FORMAT_SONGPTR 0x4001u
#define MCI_STATUS_LENGTH   1u
#define MCI_STATUS_POSITION 2u
#define MCI_STATUS_NUMBER_OF_TRACKS 3u
#define MCI_STATUS_MODE     4u
#define MCI_STATUS_MEDIA_PRESENT 5u
#define MCI_STATUS_TIME_FORMAT 6u
#define MCI_STATUS_READY    7u
#define MCI_STATUS_CURRENT_TRACK 8u
#define MCI_GETDEVCAPS_CAN_RECORD 1u
#define MCI_GETDEVCAPS_HAS_AUDIO 2u
#define MCI_GETDEVCAPS_HAS_VIDEO 3u
#define MCI_GETDEVCAPS_DEVICE_TYPE 4u
#define MCI_GETDEVCAPS_USES_FILES 5u
#define MCI_GETDEVCAPS_COMPOUND_DEVICE 6u
#define MCI_GETDEVCAPS_CAN_EJECT 7u
#define MCI_GETDEVCAPS_CAN_PLAY 8u
#define MCI_GETDEVCAPS_CAN_SAVE 9u
#define MM_MCINOTIFY        0x03b9u
#define MCI_NOTIFY_SUCCESSFUL 1u
#define MCI_NOTIFY_SUPERSEDED 2u
#define MCI_NOTIFY_ABORTED 4u

#define MCIERR_INVALID_DEVICE_ID 257u
#define MCIERR_UNRECOGNIZED_COMMAND 261u
#define MCIERR_INVALID_FILE 296u
#define MCIERR_FILE_NOT_FOUND 275u
#define MCIERR_OUT_OF_MEMORY 264u
#define MCIERR_UNSUPPORTED_FUNCTION 274u
#define MCIERR_BAD_TIME_FORMAT 293u
#define MCIERR_DEVICE_NOT_READY 276u
#define MCIERR_INVALID_DEVICE_NAME 263u
#define MCIERR_MISSING_PARAMETER 273u

#define MIDI_FILE_MAX (256u * 1024u)
#define MIDI_EVENT_MAX 16384u
#define MCI_ID 1u

typedef struct {
    uint32_t tick, ms, data;
    uint8_t tempo;        /* 1: data is microseconds per quarter-note */
} seq_event;

typedef struct {
    uint16_t task;
    uint16_t timer;
    uint16_t division, tracks;
    uint32_t time_format, mode;
    uint32_t position_ms, duration_ms, origin_ms, started;
    uint32_t end_at, index, count;
    seq_event *events;
    HWND notify_hwnd;
    char filename[160], alias[64];
    uint8_t opened, playing, paused;
} mci_sequence;

static mci_sequence seq;

/* In midi.c, MCI reserves the same driver that midiOut uses. */
int winn_midi_mci_open(uint16_t task);
int winn_midi_mci_send(uint32_t packed);
void winn_midi_mci_reset(void);
void winn_midi_mci_close(uint16_t task);
UINT WINAPI winn_midi_num(void);

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

static int read_guest(win_dll_call *c, uint32_t p, void *buf, unsigned n) {
    return p && (p >> 16) && NE && NE->read(c, p, buf, n);
}
static int write_guest(win_dll_call *c, uint32_t p, const void *buf, unsigned n) {
    return p && (p >> 16) && NE && NE->write(c, p, buf, n);
}
static int text_guest(win_dll_call *c, uint32_t p, char *buf, unsigned cap) {
    return p && (p >> 16) && NE && NE->string(c, p, buf, cap);
}
static int write_dword(win_dll_call *c, uint32_t far, uint32_t n) {
    uint8_t b[4]; put32(b, n); return write_guest(c, far, b, 4);
}
static int write_string(win_dll_call *c, uint32_t far, unsigned cap, const char *str) {
    uint32_t n = (uint32_t)strlen(str);
    if (!far || !cap) return 0;
    if (n >= cap) n = cap - 1u;
    return write_guest(c, far, str, n + 1u);
}

static void send_notify(HWND h, unsigned what) {
    if (h) PostMessage(h, MM_MCINOTIFY, what, MCI_ID);
}
static void abort_notify(unsigned why) {
    HWND old = seq.notify_hwnd;
    seq.notify_hwnd = 0;
    send_notify(old, why);
}

/* Only this task may control the current instance of the sequencer. */
static int owned(void) {
    return seq.opened && seq.task == (uint16_t)GetCurrentTask();
}

static uint32_t read_vlq(const uint8_t *data, uint32_t *at, uint32_t end, int *ok) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (*at >= end) break;
        unsigned b = data[(*at)++];
        v = (v << 7) | (b & 0x7fu);
        if (!(b & 0x80u)) return v;
    }
    *ok = 0;
    return 0;
}

/* The array grows on demand and contains only MIDI short/tempo events. */
static int append(seq_event **all, uint32_t *n, uint32_t *cap,
                  uint32_t tick, uint32_t data, int tempo) {
    if (*n == MIDI_EVENT_MAX) return 0;
    if (*n == *cap) {
        uint32_t next = *cap ? *cap * 2u : 256u;
        if (next > MIDI_EVENT_MAX) next = MIDI_EVENT_MAX;
        void *p = realloc(*all, next * sizeof(seq_event));
        if (!p) return 0;
        *all = (seq_event *)p;
        *cap = next;
    }
    (*all)[*n].tick = tick;
    (*all)[*n].ms = 0;
    (*all)[*n].data = data;
    (*all)[*n].tempo = (uint8_t)tempo;
    ++*n;
    return 1;
}
static int event_order(const void *va, const void *vb) {
    const seq_event *a = (const seq_event *)va, *b = (const seq_event *)vb;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    if (a->tempo != b->tempo) return a->tempo ? -1 : 1;
    return 0;
}

/* The native-DOS stdlib supplies malloc/realloc/free, but not qsort.
 * Sort in place to avoid allocating a second large MIDI event buffer. */
static void event_heap_down(seq_event *events, uint32_t root, uint32_t count)
{
    while (root < count / 2u) {
        uint32_t child = root * 2u + 1u;
        if (child + 1u < count &&
            event_order(&events[child], &events[child + 1u]) < 0)
            child++;
        if (event_order(&events[root], &events[child]) >= 0)
            break;
        seq_event tmp = events[root];
        events[root] = events[child];
        events[child] = tmp;
        root = child;
    }
}

static void sort_events(seq_event *events, uint32_t count)
{
    for (uint32_t start = count / 2u; start > 0;) {
        --start;
        event_heap_down(events, start, count);
    }
    for (uint32_t end = count; end > 1;) {
        --end;
        seq_event tmp = events[0];
        events[0] = events[end];
        events[end] = tmp;
        event_heap_down(events, 0, end);
    }
}

/* Standard MIDI File format 0/1; preserve a global tempo map across tracks. */
static uint32_t load_smf(const char *path, seq_event **out, uint32_t *count,
                         uint32_t *duration, uint16_t *division, uint16_t *tracks) {
    HFILE f = _lopen(path, OF_READ);
    if (f == HFILE_ERROR) return MCIERR_FILE_NOT_FOUND;
    LONG sz = _llseek(f, 0, 2);
    if (sz < 14 || (uint32_t)sz > MIDI_FILE_MAX || _llseek(f, 0, 0) < 0) {
        _lclose(f);
        return MCIERR_INVALID_FILE;
    }
    uint8_t *bytes = (uint8_t *)malloc((uint32_t)sz);
    if (!bytes) { _lclose(f); return MCIERR_OUT_OF_MEMORY; }
    uint32_t got = 0;
    while (got < (uint32_t)sz) {
        unsigned chunk = (uint32_t)sz - got > 0x4000u ? 0x4000u : (uint32_t)sz - got;
        UINT n = _lread(f, bytes + got, chunk);
        if (n != chunk) break;
        got += n;
    }
    _lclose(f);
    if (got != (uint32_t)sz || memcmp(bytes, "MThd", 4) || be32(bytes + 4) < 6 ||
        be32(bytes + 4) > (uint32_t)sz - 8u) {
        free(bytes); return MCIERR_INVALID_FILE;
    }
    uint16_t fmt = be16(bytes + 8), nt = be16(bytes + 10), div = be16(bytes + 12);
    if (fmt > 1 || !nt || nt > 128 || !div || (div & 0x8000u)) {
        free(bytes); return MCIERR_INVALID_FILE; /* SMPTE timing is not implemented */
    }
    uint32_t cursor = 8u + be32(bytes + 4), capacity = 0, used = 0, endtick = 0;
    seq_event *events = NULL;
    for (unsigned track = 0; track < nt; ++track) {
        if (cursor > (uint32_t)sz - 8u || memcmp(bytes + cursor, "MTrk", 4))
            goto bad;
        uint32_t tlen = be32(bytes + cursor + 4);
        if (tlen > (uint32_t)sz - cursor - 8u) goto bad;
        cursor += 8u;
        uint32_t off = cursor, end = cursor + tlen, tick = 0;
        uint8_t running = 0;
        while (off < end) {
            int ok = 1;
            uint32_t dt = read_vlq(bytes, &off, end, &ok);
            if (!ok || dt > UINT32_MAX - tick || off >= end) goto bad;
            tick += dt;
            uint8_t status = bytes[off];
            if (status & 0x80u) {
                ++off;
                if (status < 0xf0u) running = status;
                else running = 0;
            } else if (!(status = running)) goto bad;
            if (status == 0xffu) {
                if (off >= end) goto bad;
                unsigned meta = bytes[off++];
                uint32_t n = read_vlq(bytes, &off, end, &ok);
                if (!ok || n > end - off) goto bad;
                if (meta == 0x51u && n == 3) {
                    uint32_t us = ((uint32_t)bytes[off] << 16) |
                                  ((uint32_t)bytes[off + 1] << 8) | bytes[off + 2];
                    if (!us || !append(&events, &used, &capacity, tick, us, 1)) goto oom;
                }
                off += n;
            } else if (status == 0xf0u || status == 0xf7u) {
                uint32_t n = read_vlq(bytes, &off, end, &ok);
                if (!ok || n > end - off) goto bad;
                off += n; /* SysEx unsupported by the native MIDI output driver */
            } else if (status >= 0x80u && status < 0xf0u) {
                unsigned n = (status & 0xf0u) == 0xc0u ||
                             (status & 0xf0u) == 0xd0u ? 1u : 2u;
                if (end - off < n || (bytes[off] & 0x80u) ||
                    (n == 2 && (bytes[off + 1] & 0x80u))) goto bad;
                uint32_t packed = status | ((uint32_t)bytes[off] << 8);
                if (n == 2) packed |= (uint32_t)bytes[off + 1] << 16;
                if (!append(&events, &used, &capacity, tick, packed, 0)) goto oom;
                off += n;
            } else goto bad;
        }
        if (tick > endtick) endtick = tick;
        cursor = end;
    }
    free(bytes);
    sort_events(events, used);
    uint64_t us = 0;
    uint32_t last = 0, tempo = 500000u;
    for (uint32_t i = 0; i < used; ++i) {
        uint32_t tick = events[i].tick;
        us += ((uint64_t)(tick - last) * tempo) / div;
        last = tick;
        events[i].ms = (uint32_t)(us / 1000u);
        if (events[i].tempo) tempo = events[i].data;
    }
    us += ((uint64_t)(endtick - last) * tempo) / div;
    if (us / 1000u > UINT32_MAX) goto large;
    *out = events;
    *count = used;
    *duration = (uint32_t)(us / 1000u);
    *division = div;
    *tracks = nt;
    return 0;
large:
    free(events);
    return MCIERR_INVALID_FILE;
oom:
    free(bytes); free(events);
    return MCIERR_OUT_OF_MEMORY;
bad:
    free(bytes); free(events);
    return MCIERR_INVALID_FILE;
}

static void locate(uint32_t position) {
    uint32_t i = 0;
    while (i < seq.count && seq.events[i].ms < position) ++i;
    seq.index = i;
}
static uint32_t current_ms(void) {
    if (seq.playing) {
        uint32_t now = seq.origin_ms + (uint32_t)(GetTickCount() - seq.started);
        if (now > seq.end_at) now = seq.end_at;
        return now;
    }
    return seq.position_ms;
}
static void halt(int report) {
    if (seq.playing) {
        seq.position_ms = current_ms();
        seq.playing = 0;
    }
    seq.paused = 0;
    seq.mode = MCI_MODE_STOP;
    if (seq.timer) {
        KillTimer(0, seq.timer);
        seq.timer = 0;
    }
    winn_midi_mci_reset();
    if (report) abort_notify(MCI_NOTIFY_ABORTED);
}
static void timer(HWND hwnd, UINT message, UINT id, DWORD tick) {
    (void)hwnd; (void)message; (void)id; (void)tick;
    if (!seq.playing) return;
    uint32_t now = current_ms();
    unsigned budget = 256;
    while (seq.index < seq.count && seq.events[seq.index].ms <= now && budget--) {
        const seq_event *e = seq.events + seq.index++;
        if (!e->tempo) winn_midi_mci_send(e->data);
    }
    if ((now >= seq.end_at && (seq.index >= seq.count ||
                              seq.events[seq.index].ms > now)) ||
        (seq.index >= seq.count && now >= seq.duration_ms)) {
        halt(0);
        send_notify(seq.notify_hwnd, MCI_NOTIFY_SUCCESSFUL);
        seq.notify_hwnd = 0;
    }
}

static uint32_t do_open(const char *filename, const char *alias) {
    if (owned() || seq.opened) return MCIERR_UNSUPPORTED_FUNCTION;
    if (!winn_midi_num()) return MCIERR_DEVICE_NOT_INSTALLED;
    seq_event *events = NULL;
    uint32_t count = 0, duration = 0;
    uint16_t division = 0, tracks = 0;
    if (filename && filename[0]) {
        uint32_t e = load_smf(filename, &events, &count, &duration, &division, &tracks);
        if (e) return e;
    }
    if (!winn_midi_mci_open((uint16_t)GetCurrentTask())) {
        free(events);
        return MCIERR_DEVICE_NOT_INSTALLED;
    }
    memset(&seq, 0, sizeof(seq));
    seq.opened = 1;
    seq.task = (uint16_t)GetCurrentTask();
    seq.time_format = MCI_FORMAT_MILLISECONDS;
    seq.mode = MCI_MODE_STOP;
    seq.events = events;
    seq.count = count;
    seq.duration_ms = duration;
    seq.end_at = duration;
    seq.division = division;
    seq.tracks = tracks;
    if (filename) lstrcpyn(seq.filename, filename, sizeof(seq.filename));
    if (alias) lstrcpyn(seq.alias, alias, sizeof(seq.alias));
    return 0;
}
static void do_close(void) {
    if (!seq.opened) return;
    halt(1);
    winn_midi_mci_close(seq.task);
    free(seq.events);
    memset(&seq, 0, sizeof(seq));
}
static uint32_t to_milliseconds(uint32_t p) {
    /* Song-pointer format uses units of 1/16 quarter note: map through
     * the file's tick grid. With tempo changes a linear mapping would be
     * inaccurate; the sequencer can instead use milliseconds. */
    if (seq.time_format != MCI_SEQ_FORMAT_SONGPTR || !seq.division) return p;
    uint32_t tick = (uint32_t)(((uint64_t)p * seq.division) / 16u);
    uint32_t i = 0;
    while (i < seq.count && seq.events[i].tick < tick) ++i;
    return i < seq.count ? seq.events[i].ms : seq.duration_ms;
}
static uint32_t from_milliseconds(uint32_t ms) {
    if (seq.time_format != MCI_SEQ_FORMAT_SONGPTR || !seq.division) return ms;
    uint32_t i = 0;
    while (i < seq.count && seq.events[i].ms < ms) ++i;
    uint32_t tick = i < seq.count ? seq.events[i].tick :
                    (seq.count ? seq.events[seq.count - 1].tick : 0);
    return (uint32_t)(((uint64_t)tick * 16u) / seq.division);
}
static uint32_t do_play(uint32_t flags, uint32_t from, uint32_t to, HWND callback) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    if (!seq.events) return MCIERR_DEVICE_NOT_READY;
    if (seq.notify_hwnd) abort_notify(MCI_NOTIFY_SUPERSEDED);
    if (seq.playing || seq.paused) halt(0);
    if (flags & MCI_FROM) seq.position_ms = to_milliseconds(from);
    if (seq.position_ms > seq.duration_ms) seq.position_ms = seq.duration_ms;
    seq.end_at = (flags & MCI_TO) ? to_milliseconds(to) : seq.duration_ms;
    if (seq.end_at > seq.duration_ms) seq.end_at = seq.duration_ms;
    if (seq.end_at < seq.position_ms) seq.end_at = seq.position_ms;
    locate(seq.position_ms);
    seq.origin_ms = seq.position_ms;
    seq.started = GetTickCount();
    seq.notify_hwnd = (flags & MCI_NOTIFY) ? callback : 0;
    seq.playing = 1;
    seq.mode = MCI_MODE_PLAY;
    seq.timer = SetTimer(0, 0, 10, timer);
    if (!seq.timer) { halt(0); return MCIERR_DEVICE_NOT_READY; }
    timer(0, 0, 0, 0);
    if (flags & MCI_WAIT) {
        /* Waiting must continue dispatching messages: Yield calls the
         * system's cooperative scheduler, which also drives timers. */
        while (seq.playing && owned()) {
            Yield();
            timer(0, 0, 0, 0);
        }
    }
    return 0;
}
static uint32_t do_seek(uint32_t flags, uint32_t value) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    halt(1);
    uint32_t position = (flags & MCI_SEEK_TO_START) ? 0u :
                        (flags & MCI_SEEK_TO_END) ? seq.duration_ms : to_milliseconds(value);
    if (position > seq.duration_ms) position = seq.duration_ms;
    seq.position_ms = position;
    locate(position);
    return 0;
}
static uint32_t do_status(uint32_t item, uint32_t *value) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    switch (item) {
    case MCI_STATUS_LENGTH: *value = from_milliseconds(seq.duration_ms); break;
    case MCI_STATUS_POSITION: *value = from_milliseconds(current_ms()); break;
    case MCI_STATUS_NUMBER_OF_TRACKS: *value = seq.tracks; break;
    case MCI_STATUS_MODE: *value = seq.mode; break;
    case MCI_STATUS_MEDIA_PRESENT: *value = !!seq.events; break;
    case MCI_STATUS_TIME_FORMAT: *value = seq.time_format; break;
    case MCI_STATUS_READY: *value = !!seq.events; break;
    case MCI_STATUS_CURRENT_TRACK: *value = seq.events ? 1 : 0; break;
    case 0x4002: *value = 120u; break; /* default MIDI tempo, BPM */
    case 0x4003: *value = 0u; break; /* MPU MIDI port */
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    return 0;
}
static uint32_t do_caps(uint32_t item, uint32_t *value) {
    switch (item) {
    case MCI_GETDEVCAPS_CAN_RECORD: *value = 0; break;
    case MCI_GETDEVCAPS_HAS_AUDIO: *value = 1; break;
    case MCI_GETDEVCAPS_HAS_VIDEO: *value = 0; break;
    case MCI_GETDEVCAPS_DEVICE_TYPE: *value = MCI_DEVTYPE_SEQUENCER; break;
    case MCI_GETDEVCAPS_USES_FILES: *value = 1; break;
    case MCI_GETDEVCAPS_COMPOUND_DEVICE: *value = 1; break;
    case MCI_GETDEVCAPS_CAN_EJECT: *value = 0; break;
    case MCI_GETDEVCAPS_CAN_PLAY: *value = 1; break;
    case MCI_GETDEVCAPS_CAN_SAVE: *value = 0; break;
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    return 0;
}

/* Win16 MCI_OPEN_PARMS: dwCallback (4), wDeviceID (2), reserved (2),
 * lpstrDeviceType (4), lpstrElementName (4), lpstrAlias (4). */
static uint32_t cmd_open(win_dll_call *c, uint32_t flags, uint32_t p) {
    uint8_t b[20];
    char file[160] = "", alias[64] = "", type[48] = "";
    if (!read_guest(c, p, b, sizeof(b))) return MCIERR_MISSING_PARAMETER;
    uint32_t t = le32(b + 8), f = le32(b + 12), a = le32(b + 16);
    if (flags & MCI_OPEN_TYPE) {
        if (flags & MCI_OPEN_TYPE_ID) {
            if ((uint16_t)t != MCI_DEVTYPE_SEQUENCER) return MCIERR_DEVICE_NOT_INSTALLED;
        } else if (!text_guest(c, t, type, sizeof(type)) ||
                   (lstrcmpi(type, "sequencer") && lstrcmpi(type, "MCISEQ")))
            return MCIERR_DEVICE_NOT_INSTALLED;
    }
    if ((flags & MCI_OPEN_ELEMENT) && !text_guest(c, f, file, sizeof(file)))
        return MCIERR_MISSING_PARAMETER;
    if ((flags & MCI_OPEN_ALIAS) && !text_guest(c, a, alias, sizeof(alias)))
        return MCIERR_MISSING_PARAMETER;
    if (!file[0] && !(flags & MCI_OPEN_TYPE)) return MCIERR_MISSING_PARAMETER;
    uint32_t err = do_open(file, alias);
    if (!err) {
        uint8_t id[2];
        put16(id, MCI_ID);
        if (!write_guest(c, p + 4u, id, 2)) { do_close(); return MCIERR_MISSING_PARAMETER; }
        if (flags & MCI_NOTIFY) send_notify((HWND)le16(b), MCI_NOTIFY_SUCCESSFUL);
    }
    return err;
}
static uint32_t cmd_sysinfo(win_dll_call *c, uint32_t flags, uint32_t p) {
    uint8_t b[18];
    if (!read_guest(c, p, b, sizeof(b))) return MCIERR_MISSING_PARAMETER;
    uint32_t dest = le32(b + 4), cap = le32(b + 8);
    unsigned type = le16(b + 16);
    if (!dest || !cap) return MCIERR_MISSING_PARAMETER;
    if (flags & MCI_SYSINFO_QUANTITY) {
        if (cap < 4u) return MCIERR_MISSING_PARAMETER;
        /* MIDI sequencer only; cdaudio, waveaudio and others not installed. */
        uint32_t n = (type == 0 || type == MCI_DEVTYPE_SEQUENCER) && winn_midi_num() ? 1u : 0u;
        return write_dword(c, dest, n) ? 0 : MCIERR_MISSING_PARAMETER;
    }
    if (flags & MCI_SYSINFO_NAME) {
        if (type != MCI_DEVTYPE_SEQUENCER || le32(b + 12) != 1 || !winn_midi_num())
            return MCIERR_DEVICE_NOT_INSTALLED;
        return write_string(c, dest, cap, "sequencer") ? 0 : MCIERR_MISSING_PARAMETER;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}
static uint32_t cmd_other(win_dll_call *c, uint16_t id, uint16_t msg,
                          uint32_t flags, uint32_t p) {
    if (msg == MCI_SYSINFO) return cmd_sysinfo(c, flags, p);
    if (msg == MCI_OPEN) return cmd_open(c, flags, p);
    if (!owned() || id != MCI_ID) return MCIERR_INVALID_DEVICE_ID;
    uint8_t b[16] = {0};
    if (!read_guest(c, p, b, sizeof(uint32_t))) return MCIERR_MISSING_PARAMETER;
    HWND callback = (HWND)le16(b);
    uint32_t result = 0, err = 0;
    switch (msg) {
    case MCI_CLOSE:
        do_close();
        if (flags & MCI_NOTIFY) send_notify(callback, MCI_NOTIFY_SUCCESSFUL);
        break;
    case MCI_PLAY:
        if ((flags & (MCI_FROM | MCI_TO)) && !read_guest(c, p, b, 12))
            return MCIERR_MISSING_PARAMETER;
        err = do_play(flags, le32(b + 4), le32(b + 8), callback);
        break;
    case MCI_STOP:
        halt(1); break;
    case MCI_PAUSE:
        if (seq.playing) {
            seq.position_ms = current_ms();
            seq.playing = 0;
            seq.paused = 1;
            seq.mode = MCI_MODE_PAUSE;
            if (seq.timer) { KillTimer(0, seq.timer); seq.timer = 0; }
            winn_midi_mci_reset();
        }
        break;
    case MCI_RESUME:
        if (seq.paused) {
            seq.paused = 0;
            err = do_play(flags & ~MCI_FROM, 0, seq.end_at, callback);
        }
        break;
    case MCI_SEEK:
        if (!read_guest(c, p, b, 8)) return MCIERR_MISSING_PARAMETER;
        err = do_seek(flags, le32(b + 4));
        break;
    case MCI_LOAD: {
        if (!(flags & MCI_LOAD_FILE) || !read_guest(c, p, b, 8))
            return MCIERR_MISSING_PARAMETER;
        char filename[160];
        if (!text_guest(c, le32(b + 4), filename, sizeof(filename)))
            return MCIERR_MISSING_PARAMETER;
        seq_event *events = NULL;
        uint32_t count, duration;
        uint16_t division, tracks;
        err = load_smf(filename, &events, &count, &duration, &division, &tracks);
        if (!err) {
            halt(1);
            free(seq.events);
            seq.events = events;
            seq.count = count;
            seq.duration_ms = seq.end_at = duration;
            seq.division = division;
            seq.tracks = tracks;
            seq.index = seq.position_ms = 0;
            lstrcpyn(seq.filename, filename, sizeof(seq.filename));
        }
        break;
    }
    case MCI_STATUS:
        if (!(flags & MCI_STATUS_ITEM) || !read_guest(c, p, b, 12))
            return MCIERR_MISSING_PARAMETER;
        err = do_status(le32(b + 8), &result);
        if (!err && !write_dword(c, p + 4u, result)) err = MCIERR_MISSING_PARAMETER;
        break;
    case MCI_SET:
        if (!(flags & MCI_SET_TIME_FORMAT)) return MCIERR_UNSUPPORTED_FUNCTION;
        if (!read_guest(c, p, b, 8)) return MCIERR_MISSING_PARAMETER;
        result = le32(b + 4);
        if (result != MCI_FORMAT_MILLISECONDS && result != MCI_SEQ_FORMAT_SONGPTR)
            return MCIERR_BAD_TIME_FORMAT;
        seq.time_format = result;
        break;
    case MCI_GETDEVCAPS:
        if (!(flags & MCI_GETDEVCAPS_ITEM) || !read_guest(c, p, b, 12))
            return MCIERR_MISSING_PARAMETER;
        err = do_caps(le32(b + 8), &result);
        if (!err && !write_dword(c, p + 4u, result)) err = MCIERR_MISSING_PARAMETER;
        break;
    case MCI_INFO:
        if (!read_guest(c, p, b, 12)) return MCIERR_MISSING_PARAMETER;
        if (!(flags & (MCI_INFO_PRODUCT | MCI_INFO_FILE))) return MCIERR_UNSUPPORTED_FUNCTION;
        err = write_string(c, le32(b + 4), le32(b + 8),
                           flags & MCI_INFO_PRODUCT ? "WINN MIDI Sequencer" : seq.filename)
              ? 0 : MCIERR_MISSING_PARAMETER;
        break;
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (!err && (flags & MCI_NOTIFY) && msg != MCI_PLAY && msg != MCI_CLOSE)
        send_notify(callback, MCI_NOTIFY_SUCCESSFUL);
    return err;
}

/* A small MCI string command interpreter for Media Player and scripts. */
static const char *word(const char **at, char *out, unsigned cap) {
    const char *p = *at;
    unsigned i = 0;
    while (*p == ' ' || *p == '\t') ++p;
    char quote = *p == '"' || *p == '\'' ? *p++ : 0;
    while (*p && (quote ? *p != quote : *p != ' ' && *p != '\t')) {
        if (i + 1 < cap) out[i++] = *p;
        ++p;
    }
    if (quote && *p == quote) ++p;
    out[i] = 0;
    *at = p;
    return p;
}
static int same(const char *a, const char *b) { return !lstrcmpi(a, b); }
static int match_device(const char *d) {
    return (same(d, seq.alias) && seq.alias[0]) ||
           (same(d, seq.filename) && seq.filename[0]) ||
           same(d, "sequencer") || same(d, "1");
}
static uint32_t parse_uint(const char *s) {
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10u + (unsigned)(*s++ - '0');
    return n;
}
static uint32_t cmd_string(const char *cmd, char *result, unsigned cap, HWND h) {
    const char *p = cmd;
    char op[32], dev[160], arg[160], last[160];
    word(&p, op, sizeof(op)); word(&p, dev, sizeof(dev));
    if (result && cap) result[0] = 0;
    if (same(op, "sysinfo")) {
        word(&p, arg, sizeof(arg));
        if (same(arg, "quantity")) {
            if (result && cap) {
                char n[2] = { (char)(winn_midi_num() && (same(dev,"all") || same(dev,"sequencer")) ? '1':'0'), 0 };
                lstrcpyn(result, n, (int)cap);
            }
            return 0;
        }
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (same(op, "open")) {
        char file[160] = "", alias[64] = "", type[40] = "";
        if (same(dev, "sequencer")) lstrcpyn(type, "sequencer", sizeof(type));
        else lstrcpyn(file, dev, sizeof(file));
        while ((word(&p, arg, sizeof(arg)), arg[0])) {
            if (same(arg, "type")) word(&p, type, sizeof(type));
            else if (same(arg, "alias")) word(&p, alias, sizeof(alias));
        }
        if (type[0] && !same(type, "sequencer") && !same(type, "MCISEQ"))
            return MCIERR_DEVICE_NOT_INSTALLED;
        return do_open(file, alias);
    }
    if (!owned() || !match_device(dev)) return MCIERR_INVALID_DEVICE_NAME;
    if (same(op, "close")) { do_close(); return 0; }
    if (same(op, "stop")) { halt(1); return 0; }
    if (same(op, "pause")) { if (seq.playing) {
        seq.position_ms = current_ms(); seq.playing = 0; seq.paused = 1;
        seq.mode = MCI_MODE_PAUSE;
        if (seq.timer) { KillTimer(0, seq.timer); seq.timer = 0; }
        winn_midi_mci_reset();
    } return 0; }
    if (same(op, "resume")) return do_play(0, 0, 0, 0);
    if (same(op, "play")) {
        uint32_t flags = 0, from = 0, to = 0;
        while ((word(&p, arg, sizeof(arg)), arg[0])) {
            if (same(arg, "from")) { word(&p, last, sizeof(last)); from = parse_uint(last); flags |= MCI_FROM; }
            else if (same(arg, "to")) { word(&p, last, sizeof(last)); to = parse_uint(last); flags |= MCI_TO; }
            else if (same(arg, "notify")) flags |= MCI_NOTIFY;
            else if (same(arg, "wait")) flags |= MCI_WAIT;
        }
        return do_play(flags, from, to, h);
    }
    if (same(op, "seek")) {
        word(&p, arg, sizeof(arg));
        if (same(arg,"to")) word(&p, arg, sizeof(arg));
        return do_seek(same(arg,"start") ? MCI_SEEK_TO_START :
                       same(arg,"end") ? MCI_SEEK_TO_END : 0, parse_uint(arg));
    }
    if (same(op,"set")) {
        word(&p,arg,sizeof(arg));
        if (!same(arg,"time")) return MCIERR_UNSUPPORTED_FUNCTION;
        word(&p,arg,sizeof(arg));
        if (!same(arg,"format")) return MCIERR_UNSUPPORTED_FUNCTION;
        word(&p,arg,sizeof(arg));
        if (same(arg,"milliseconds")) seq.time_format = MCI_FORMAT_MILLISECONDS;
        else if (same(arg,"songptr")) seq.time_format = MCI_SEQ_FORMAT_SONGPTR;
        else return MCIERR_BAD_TIME_FORMAT;
        return 0;
    }
    if (same(op, "status")) {
        word(&p,arg,sizeof(arg));
        uint32_t item = same(arg,"length") ? MCI_STATUS_LENGTH :
                        same(arg,"position") ? MCI_STATUS_POSITION :
                        same(arg,"mode") ? MCI_STATUS_MODE :
                        same(arg,"number") ? MCI_STATUS_NUMBER_OF_TRACKS :
                        same(arg,"ready") ? MCI_STATUS_READY : 0;
        uint32_t val = 0;
        if (!item) return MCIERR_UNSUPPORTED_FUNCTION;
        uint32_t e = do_status(item, &val);
        if (e) return e;
        if (result && cap) {
            char s[32];
            if (item == MCI_STATUS_MODE) lstrcpyn(s, val == MCI_MODE_PLAY ? "playing" :
                               val == MCI_MODE_PAUSE ? "paused" : "stopped", sizeof(s));
            else { unsigned n = val, k = 0; char tmp[16];
                   do { tmp[k++] = (char)('0'+n%10u); n/=10; } while(n && k<sizeof(tmp));
                   unsigned i = 0; while(k) s[i++]=tmp[--k]; s[i]=0; }
            lstrcpyn(result, s, (int)cap);
        }
        return 0;
    }
    return MCIERR_UNRECOGNIZED_COMMAND;
}

int winn_mci_ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *value) {
    if (ordinal == 701) {
        *value = cmd_other(c, NE->arg16(c, 14), NE->arg16(c, 12),
                           NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_DWORD;
    }
    if (ordinal == 702) {
        char cmd[256], out[128] = "";
        uint32_t command = NE->arg32(c, 12), result = NE->arg32(c, 8);
        unsigned cap = NE->arg16(c, 6);
        if (!text_guest(c, command, cmd, sizeof(cmd)))
            *value = MCIERR_MISSING_PARAMETER;
        else {
            *value = cmd_string(cmd, out, sizeof(out), (HWND)NE->arg16(c, 4));
            if (result && cap && !write_string(c, result, cap, out) && !*value)
                *value = MCIERR_MISSING_PARAMETER;
        }
        return WINDLL_DWORD;
    }
    if (ordinal == 703) {
        char alias[80];
        uint32_t ptr = NE->arg32(c, 4);
        *value = text_guest(c, ptr, alias, sizeof(alias)) && owned() &&
                 match_device(alias) ? MCI_ID : 0;
        return WINDLL_WORD;
    }
    return WINDLL_NONE;
}

void winn_mci_task_end(uint16_t task) {
    if (seq.opened && seq.task == task) do_close();
}
void winn_mci_term(void) { do_close(); }
