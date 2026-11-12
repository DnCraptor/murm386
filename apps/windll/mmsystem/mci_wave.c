/*
 * mci_wave.c - Win16 MCI waveaudio device (PCM RIFF/WAVE), backed by
 * WINN's installable wave driver. It shares the SBWAVE hardware with waveOut.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "winuser.h"
#include "mmsystem.h"
#include "windll.h"
#include "wave_driver.h"

#define NE (__win_dll_ne)
#define WAVE_MCI_ID   2u
#define MCI_OPEN      0x0803u
#define MCI_CLOSE     0x0804u
#define MCI_PLAY      0x0806u
#define MCI_SEEK      0x0807u
#define MCI_STOP      0x0808u
#define MCI_PAUSE     0x0809u
#define MCI_INFO      0x080au
#define MCI_GETDEVCAPS 0x080bu
#define MCI_SET       0x080du
#define MCI_STATUS    0x0814u
#define MCI_LOAD      0x0850u
#define MCI_RESUME    0x0855u
#define MCI_LOAD_FILE 0x00000100ul
#define MCI_NOTIFY    0x00000001ul
#define MCI_WAIT      0x00000002ul
#define MCI_FROM      0x00000004ul
#define MCI_TO        0x00000008ul
#define MCI_SEEK_TO_START 0x00000100ul
#define MCI_SEEK_TO_END   0x00000200ul
#define MCI_STATUS_ITEM   0x00000100ul
#define MCI_SET_TIME_FORMAT 0x00000400ul
#define MCI_GETDEVCAPS_ITEM 0x00000100ul
#define MCI_INFO_PRODUCT 0x00000100ul
#define MCI_INFO_FILE    0x00000200ul
#define MCI_MODE_STOP    525u
#define MCI_MODE_PLAY    526u
#define MCI_MODE_PAUSE   529u
#define MCI_FORMAT_MILLISECONDS 0u
#define MCI_FORMAT_BYTES 8u
#define MCI_FORMAT_SAMPLES 9u
#define MCI_DEVTYPE_WAVEFORM_AUDIO 522u
#define MCI_STATUS_LENGTH 1u
#define MCI_STATUS_POSITION 2u
#define MCI_STATUS_NUMBER_OF_TRACKS 3u
#define MCI_STATUS_MODE 4u
#define MCI_STATUS_MEDIA_PRESENT 5u
#define MCI_STATUS_TIME_FORMAT 6u
#define MCI_STATUS_READY 7u
#define MCI_STATUS_CURRENT_TRACK 8u
#define MCI_NOTIFY_SUCCESSFUL 1u
#define MCI_NOTIFY_SUPERSEDED 2u
#define MCI_NOTIFY_ABORTED 4u
#define MM_MCINOTIFY 0x03b9u
#define MCIERR_INVALID_DEVICE_ID 257u
#define MCIERR_INVALID_FILE 296u
#define MCIERR_FILE_NOT_FOUND 275u
#define MCIERR_UNSUPPORTED_FUNCTION 274u
#define MCIERR_DEVICE_NOT_READY 276u
#define MCIERR_MISSING_PARAMETER 273u
#define MCIERR_BAD_TIME_FORMAT 293u
#define WAV_BUFSIZE 8192u

typedef struct {
    HTASK task;
    HMODULE module;
    HFILE file;
    const win_wave_driver *driver;
    uint32_t data_offset, data_size, cursor, completed, active;
    uint32_t active_started, rate, avg_rate, end_at, time_format, mode;
    uint16_t bits, channels, block, timer;
    HWND notify;
    uint8_t playing, paused;
    char filename[160], alias[64];
} wave_mci;

static wave_mci wave;
static uint8_t buffer[WAV_BUFSIZE];
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static int read_guest(win_dll_call *c, uint32_t p, void *buf, unsigned n) {
    return p && (p >> 16) && NE && NE->read(c, p, buf, n);
}
static int write_guest(win_dll_call *c, uint32_t p, const void *buf, unsigned n) {
    return p && (p >> 16) && NE && NE->write(c, p, buf, n);
}
static void notify(HWND hwnd, unsigned code) {
    if (hwnd) PostMessage(hwnd, MM_MCINOTIFY, code, WAVE_MCI_ID);
}
static void abort_notify(unsigned code) {
    HWND hwnd = wave.notify;
    wave.notify = 0;
    notify(hwnd, code);
}
static int owned(void) {
    return wave.driver && wave.task == GetCurrentTask();
}
int winn_mci_wave_owned(void) { return owned(); }
int winn_mci_wave_match(const char *s) {
    return owned() && ((wave.alias[0] && !lstrcmpi(s, wave.alias)) ||
                       (wave.filename[0] && !lstrcmpi(s, wave.filename)) ||
                       !lstrcmpi(s, "waveaudio") || !lstrcmpi(s, "2"));
}
static int read_at(HFILE f, uint32_t offset, void *dst, unsigned n) {
    return offset <= 0x7fffffff && _llseek(f, (LONG)offset, 0) == (LONG)offset &&
           _lread(f, dst, (UINT)n) == (UINT)n;
}
static uint32_t read_wave(HFILE f, wave_mci *out) {
    LONG len = _llseek(f, 0, 2);
    uint8_t h[12];
    if (len < 44 || !read_at(f, 0, h, sizeof(h)) || memcmp(h, "RIFF", 4) ||
        memcmp(h + 8, "WAVE", 4)) return MCIERR_INVALID_FILE;
    uint32_t end = get32(h + 4);
    if (end > (uint32_t)len - 8u) return MCIERR_INVALID_FILE;
    end += 8u;
    uint32_t off = 12;
    int fmt = 0, data = 0;
    while (off <= end - 8u) {
        uint8_t ck[8];
        if (!read_at(f, off, ck, sizeof(ck))) return MCIERR_INVALID_FILE;
        uint32_t n = get32(ck + 4);
        if (n > end - off - 8u) return MCIERR_INVALID_FILE;
        if (!memcmp(ck, "fmt ", 4) && !fmt) {
            uint8_t pcm[16];
            if (n < sizeof(pcm) || !read_at(f, off + 8u, pcm, sizeof(pcm)))
                return MCIERR_INVALID_FILE;
            out->channels = get16(pcm + 2);
            out->rate = get32(pcm + 4);
            out->avg_rate = get32(pcm + 8);
            out->block = get16(pcm + 12);
            out->bits = get16(pcm + 14);
            if (get16(pcm) != 1 || (out->channels != 1 && out->channels != 2) ||
                (out->bits != 8 && out->bits != 16) ||
                out->rate < 4000u || out->rate > 48000u ||
                out->block != out->channels * (out->bits / 8u) ||
                out->avg_rate != out->rate * out->block)
                return MCIERR_INVALID_FILE;
            fmt = 1;
        } else if (!memcmp(ck, "data", 4) && !data) {
            out->data_offset = off + 8u;
            out->data_size = n;
            data = 1;
        }
        off += 8u + n + (n & 1u);
    }
    if (!fmt || !data || !out->data_size || out->data_size % out->block)
        return MCIERR_INVALID_FILE;
    return 0;
}
static const win_wave_driver *load_driver(HMODULE *module) {
    char name[64];
    *module = 0;
    if (!GetPrivateProfileString("drivers", "wave", "", name, sizeof(name), "SYSTEM.INI") ||
        !name[0]) return NULL;
    HMODULE h = LoadLibrary(name);
    if (h < 32u) return NULL;
    win_wave_driver_entry init =
        (win_wave_driver_entry)(void (*)(void))GetProcAddress(h, "WinWaveDriver");
    const win_wave_driver *d = init ? init() : NULL;
    if (!d || d->version != WIN_WAVE_DRIVER_VERSION || d->size < sizeof(*d) ||
        !d->present || !d->open || !d->close || !d->submit || !d->finished ||
        !d->stop || !d->pause || !d->max_bytes || !d->present()) {
        FreeLibrary(h);
        return NULL;
    }
    *module = h;
    return d;
}
uint32_t winn_mci_wave_open(const char *filename, const char *alias) {
    if (wave.driver) return MCIERR_DEVICE_NOT_READY;
    HFILE file = HFILE_ERROR;
    wave_mci next = {0};
    if (filename && filename[0]) {
        file = _lopen(filename, OF_READ);
        if (file == HFILE_ERROR) return MCIERR_FILE_NOT_FOUND;
        uint32_t err = read_wave(file, &next);
        if (err) { _lclose(file); return err; }
    }
    HMODULE module;
    const win_wave_driver *driver = load_driver(&module);
    if (!driver) {
        if (file != HFILE_ERROR) _lclose(file);
        return MCIERR_DEVICE_NOT_INSTALLED;
    }
    if (!driver->open()) {
        if (file != HFILE_ERROR) _lclose(file);
        FreeLibrary(module);
        return MCIERR_DEVICE_NOT_READY;
    }
    next.file = file;
    next.module = module;
    next.driver = driver;
    next.task = GetCurrentTask();
    next.time_format = MCI_FORMAT_MILLISECONDS;
    next.mode = MCI_MODE_STOP;
    next.end_at = next.data_size;
    lstrcpyn(next.filename, filename, sizeof(next.filename));
    if (alias) lstrcpyn(next.alias, alias, sizeof(next.alias));
    wave = next;
    return 0;
}
static uint32_t to_bytes(uint32_t position) {
    uint64_t result = wave.time_format == MCI_FORMAT_BYTES ? position :
                      wave.time_format == MCI_FORMAT_SAMPLES ?
                      (uint64_t)position * wave.block :
                      ((uint64_t)position * wave.avg_rate) / 1000u;
    if (result > wave.data_size) result = wave.data_size;
    return (uint32_t)result - (uint32_t)result % wave.block;
}
static uint32_t from_bytes(uint32_t position) {
    if (!wave.avg_rate || !wave.block) return 0;
    if (wave.time_format == MCI_FORMAT_BYTES) return position;
    if (wave.time_format == MCI_FORMAT_SAMPLES) return position / wave.block;
    return (uint32_t)(((uint64_t)position * 1000u) / wave.avg_rate);
}
static uint32_t position_bytes(void) {
    if (!wave.playing || !wave.active) return wave.completed;
    uint32_t elapsed = GetTickCount() - wave.active_started;
    uint64_t estimate = (uint64_t)elapsed * wave.avg_rate / 1000u;
    if (estimate > wave.active) estimate = wave.active;
    uint32_t bytes = (uint32_t)estimate;
    bytes -= bytes % wave.block;
    return wave.completed + bytes;
}
static void stop_play(unsigned abort) {
    if (wave.driver) wave.driver->stop();
    if (wave.playing) wave.cursor = wave.completed = position_bytes();
    wave.active = 0;
    wave.playing = wave.paused = 0;
    wave.mode = MCI_MODE_STOP;
    if (wave.timer) { KillTimer(0, wave.timer); wave.timer = 0; }
    if (abort) abort_notify(MCI_NOTIFY_ABORTED);
}
void winn_mci_wave_close(void) {
    if (!wave.driver) return;
    stop_play(1);
    wave.driver->close();
    if (wave.file != HFILE_ERROR) _lclose(wave.file);
    HMODULE module = wave.module;
    memset(&wave, 0, sizeof(wave));
    FreeLibrary(module);
}
static void pump(void) {
    if (!owned() || !wave.playing || wave.paused) return;
    if (wave.active) {
        if (!wave.driver->finished()) return;
        wave.completed += wave.active;
        wave.active = 0;
    }
    if (wave.completed >= wave.end_at || wave.cursor >= wave.end_at) {
        HWND h = wave.notify;
        wave.notify = 0;
        stop_play(0);
        notify(h, MCI_NOTIFY_SUCCESSFUL);
        return;
    }
    uint32_t n = wave.end_at - wave.cursor;
    if (n > sizeof(buffer)) n = sizeof(buffer);
    if (n > wave.driver->max_bytes) n = wave.driver->max_bytes;
    n -= n % wave.block;
    if (!n || !read_at(wave.file, wave.data_offset + wave.cursor, buffer, n) ||
        !wave.driver->submit(buffer, n, wave.rate, wave.bits, wave.channels)) {
        stop_play(1);
        return;
    }
    wave.active = n;
    wave.active_started = GetTickCount();
    wave.cursor += n;
}
static void CALLBACK tick(HWND h, UINT m, UINT id, DWORD now) {
    (void)h; (void)m; (void)id; (void)now;
    pump();
}
static uint32_t play(uint32_t flags, uint32_t from, uint32_t to, HWND hwnd) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    if (wave.file == HFILE_ERROR || !wave.data_size) return MCIERR_DEVICE_NOT_READY;
    if (wave.playing || wave.paused) stop_play(1);
    if (flags & MCI_FROM) wave.cursor = wave.completed = to_bytes(from);
    wave.end_at = (flags & MCI_TO) ? to_bytes(to) : wave.data_size;
    if (wave.end_at < wave.cursor) wave.end_at = wave.cursor;
    wave.notify = (flags & MCI_NOTIFY) ? hwnd : 0;
    wave.playing = 1;
    wave.mode = MCI_MODE_PLAY;
    wave.timer = SetTimer(0, 0, 5, tick);
    if (!wave.timer) { stop_play(1); return MCIERR_DEVICE_NOT_READY; }
    pump();
    if (flags & MCI_WAIT) {
        while (wave.playing && owned()) { Yield(); pump(); }
    }
    return 0;
}
/* Routes MCI commands addressed to device 2. The caller writes the
 * wDeviceID for OPEN; here all other parameter blocks remain Win16. */
uint32_t winn_mci_wave_command(win_dll_call *c, uint16_t msg, uint32_t flags, uint32_t p) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    uint8_t b[16] = {0};
    if (p && !read_guest(c, p, b, 4)) return MCIERR_MISSING_PARAMETER;
    HWND cb = (HWND)get16(b);
    uint32_t ret = 0, err = 0;
    switch (msg) {
    case MCI_CLOSE:
        winn_mci_wave_close();
        if (flags & MCI_NOTIFY) notify(cb, MCI_NOTIFY_SUCCESSFUL);
        return 0;
    case MCI_PLAY:
        if ((flags & (MCI_FROM | MCI_TO)) && !read_guest(c, p, b, 12))
            return MCIERR_MISSING_PARAMETER;
        return play(flags, get32(b + 4), get32(b + 8), cb);
    case MCI_STOP: stop_play(1); break;
    case MCI_PAUSE:
        if (wave.playing) {
            wave.completed = position_bytes();
            wave.cursor = wave.completed;
            wave.driver->stop();
            wave.active = 0;
            wave.playing = 0;
            wave.paused = 1;
            wave.mode = MCI_MODE_PAUSE;
            if (wave.timer) { KillTimer(0, wave.timer); wave.timer = 0; }
        }
        break;
    case MCI_RESUME:
        if (wave.paused) return play((flags & ~MCI_FROM) | (wave.end_at < wave.data_size ? MCI_TO : 0),
                     0, from_bytes(wave.end_at), cb);
        break;
    case MCI_LOAD: {
        if (!(flags & MCI_LOAD_FILE) || !read_guest(c, p, b, 8))
            return MCIERR_MISSING_PARAMETER;
        char name[160];
        uint32_t ptr = get32(b + 4);
        if (!ptr || !(ptr >> 16) || !NE->string(c, ptr, name, sizeof(name)))
            return MCIERR_MISSING_PARAMETER;
        HFILE file = _lopen(name, OF_READ);
        if (file == HFILE_ERROR) return MCIERR_FILE_NOT_FOUND;
        wave_mci incoming = {0};
        uint32_t error = read_wave(file, &incoming);
        if (error) { _lclose(file); return error; }
        stop_play(1);
        if (wave.file != HFILE_ERROR) _lclose(wave.file);
        wave.file = file;
        wave.data_offset = incoming.data_offset;
        wave.data_size = incoming.data_size;
        wave.rate = incoming.rate;
        wave.avg_rate = incoming.avg_rate;
        wave.channels = incoming.channels;
        wave.bits = incoming.bits;
        wave.block = incoming.block;
        wave.cursor = wave.completed = 0;
        wave.end_at = wave.data_size;
        lstrcpyn(wave.filename, name, sizeof(wave.filename));
        break;
    }
    case MCI_SEEK:
        if (!read_guest(c, p, b, 8)) return MCIERR_MISSING_PARAMETER;
        stop_play(1);
        wave.cursor = wave.completed = (flags & MCI_SEEK_TO_START) ? 0 :
                        (flags & MCI_SEEK_TO_END) ? wave.data_size : to_bytes(get32(b + 4));
        break;
    case MCI_STATUS:
        if (!(flags & MCI_STATUS_ITEM) || !read_guest(c, p, b, 12))
            return MCIERR_MISSING_PARAMETER;
        switch (get32(b + 8)) {
        case MCI_STATUS_LENGTH: ret = from_bytes(wave.data_size); break;
        case MCI_STATUS_POSITION: ret = from_bytes(position_bytes()); break;
        case MCI_STATUS_MODE: ret = wave.mode; break;
        case MCI_STATUS_NUMBER_OF_TRACKS: case MCI_STATUS_CURRENT_TRACK: ret = 1; break;
        case MCI_STATUS_MEDIA_PRESENT: case MCI_STATUS_READY:
            ret = wave.file != HFILE_ERROR && wave.data_size != 0; break;
        case MCI_STATUS_TIME_FORMAT: ret = wave.time_format; break;
        default: return MCIERR_UNSUPPORTED_FUNCTION;
        }
        {
            uint8_t v[4]; put32(v, ret);
            if (!write_guest(c, p + 4u, v, 4)) return MCIERR_MISSING_PARAMETER;
        }
        break;
    case MCI_SET:
        if (!(flags & MCI_SET_TIME_FORMAT) || !read_guest(c, p, b, 8))
            return MCIERR_UNSUPPORTED_FUNCTION;
        ret = get32(b + 4);
        if (ret != MCI_FORMAT_MILLISECONDS && ret != MCI_FORMAT_BYTES &&
            ret != MCI_FORMAT_SAMPLES) return MCIERR_BAD_TIME_FORMAT;
        wave.time_format = ret;
        break;
    case MCI_GETDEVCAPS:
        if (!(flags & MCI_GETDEVCAPS_ITEM) || !read_guest(c, p, b, 12))
            return MCIERR_MISSING_PARAMETER;
        switch (get32(b + 8)) {
        case 1: case 3: case 7: case 9: ret = 0; break;
        case 2: case 5: case 6: case 8: ret = 1; break;
        case 4: ret = MCI_DEVTYPE_WAVEFORM_AUDIO; break;
        default: return MCIERR_UNSUPPORTED_FUNCTION;
        }
        {
            uint8_t v[4]; put32(v, ret);
            if (!write_guest(c, p + 4u, v, 4)) return MCIERR_MISSING_PARAMETER;
        }
        break;
    case MCI_INFO:
        if (!(flags & (MCI_INFO_PRODUCT | MCI_INFO_FILE)) || !read_guest(c, p, b, 12))
            return MCIERR_UNSUPPORTED_FUNCTION;
        {
            const char *s = flags & MCI_INFO_PRODUCT ? "WINN PCM Wave Audio" : wave.filename;
            uint32_t dest = get32(b + 4), cap = get32(b + 8);
            unsigned len = (unsigned)strlen(s);
            if (!dest || !cap) return MCIERR_MISSING_PARAMETER;
            if (len >= cap) len = cap - 1u;
            if (!write_guest(c, dest, s, len) ||
                !write_guest(c, dest + len, "", 1)) return MCIERR_MISSING_PARAMETER;
        }
        break;
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (!err && (flags & MCI_NOTIFY) && msg != MCI_CLOSE && msg != MCI_PLAY)
        notify(cb, MCI_NOTIFY_SUCCESSFUL);
    return err;
}
void winn_mci_wave_task_end(uint16_t task) {
    if (wave.driver && wave.task == task) winn_mci_wave_close();
}
void winn_mci_wave_term(void) { winn_mci_wave_close(); }

/* String commands use the same device state as mciSendCommand, not a
 * parallel media player. They are also useful from Win16 scripts. */
static const char *next_word(const char **at, char *out, unsigned cap) {
    const char *p = *at;
    unsigned n = 0;
    while (*p == ' ' || *p == '\t') ++p;
    char quote = *p == '"' || *p == '\'' ? *p++ : 0;
    while (*p && (quote ? *p != quote : *p != ' ' && *p != '\t')) {
        if (n + 1u < cap) out[n++] = *p;
        ++p;
    }
    if (quote && *p == quote) ++p;
    out[n] = 0;
    *at = p;
    return p;
}
static uint32_t number(const char *s) {
    uint32_t n = 0;
    for (; *s >= '0' && *s <= '9'; ++s) n = n * 10u + (unsigned)(*s - '0');
    return n;
}
static void number_text(char *out, unsigned cap, uint32_t v) {
    char reversed[12];
    unsigned n = 0, i = 0;
    do { reversed[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < sizeof(reversed));
    while (n && i + 1 < cap) out[i++] = reversed[--n];
    if (cap) out[i] = 0;
}
uint32_t winn_mci_wave_string(const char *op, const char *rest, char *result,
                               unsigned cap, HWND hwnd) {
    char arg[64], value[64], extra[64];
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    if (!lstrcmpi(op, "close")) { winn_mci_wave_close(); return 0; }
    if (!lstrcmpi(op, "stop")) { stop_play(1); return 0; }
    if (!lstrcmpi(op, "pause")) {
        if (wave.playing) {
            wave.completed = position_bytes(); wave.cursor = wave.completed;
            wave.driver->stop(); wave.playing = 0; wave.active = 0;
            wave.paused = 1; wave.mode = MCI_MODE_PAUSE;
            if (wave.timer) { KillTimer(0, wave.timer); wave.timer = 0; }
        }
        return 0;
    }
    if (!lstrcmpi(op, "resume"))
        return play(wave.end_at < wave.data_size ? MCI_TO : 0, 0,
                    from_bytes(wave.end_at), hwnd);
    if (!lstrcmpi(op, "play")) {
        uint32_t flags = 0, from = 0, to = 0;
        while ((next_word(&rest, arg, sizeof(arg)), arg[0])) {
            if (!lstrcmpi(arg, "from")) {
                next_word(&rest, value, sizeof(value)); from = number(value); flags |= MCI_FROM;
            } else if (!lstrcmpi(arg, "to")) {
                next_word(&rest, value, sizeof(value)); to = number(value); flags |= MCI_TO;
            } else if (!lstrcmpi(arg, "notify")) flags |= MCI_NOTIFY;
            else if (!lstrcmpi(arg, "wait")) flags |= MCI_WAIT;
        }
        return play(flags, from, to, hwnd);
    }
    if (!lstrcmpi(op, "seek")) {
        next_word(&rest, arg, sizeof(arg));
        if (!lstrcmpi(arg, "to")) next_word(&rest, arg, sizeof(arg));
        stop_play(1);
        wave.cursor = wave.completed = !lstrcmpi(arg, "start") ? 0 :
             !lstrcmpi(arg, "end") ? wave.data_size : to_bytes(number(arg));
        return 0;
    }
    if (!lstrcmpi(op, "set")) {
        next_word(&rest, arg, sizeof(arg));
        next_word(&rest, extra, sizeof(extra));
        next_word(&rest, value, sizeof(value));
        if (lstrcmpi(arg, "time") || lstrcmpi(extra, "format"))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (!lstrcmpi(value, "milliseconds")) wave.time_format = MCI_FORMAT_MILLISECONDS;
        else if (!lstrcmpi(value, "bytes")) wave.time_format = MCI_FORMAT_BYTES;
        else if (!lstrcmpi(value, "samples")) wave.time_format = MCI_FORMAT_SAMPLES;
        else return MCIERR_BAD_TIME_FORMAT;
        return 0;
    }
    if (!lstrcmpi(op, "status")) {
        next_word(&rest, arg, sizeof(arg));
        if (!result || !cap) return 0;
        if (!lstrcmpi(arg, "mode"))
            lstrcpyn(result, wave.mode == MCI_MODE_PLAY ? "playing" :
                      wave.mode == MCI_MODE_PAUSE ? "paused" : "stopped", (int)cap);
        else if (!lstrcmpi(arg, "length")) number_text(result, cap, from_bytes(wave.data_size));
        else if (!lstrcmpi(arg, "position")) number_text(result, cap, from_bytes(position_bytes()));
        else if (!lstrcmpi(arg, "ready")) lstrcpyn(result, "1", (int)cap);
        else return MCIERR_UNSUPPORTED_FUNCTION;
        return 0;
    }
    if (!lstrcmpi(op, "info")) {
        next_word(&rest, arg, sizeof(arg));
        if (!result || !cap) return 0;
        lstrcpyn(result, !lstrcmpi(arg, "product") ? "WINN PCM Wave Audio" : wave.filename,
                 (int)cap);
        return 0;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}
