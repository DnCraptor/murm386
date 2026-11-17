/*
 * snd.c - sndPlaySound and PlaySound: a WAV file, a sound of WIN.INI's
 * [sounds] (SystemAsterisk=chimes.wav,Asterisk: the file before the comma)
 * or a WAV image in memory, played on the installed wave driver (as
 * MCI's waveaudio's, at the wave volume). Synchronous (the call returns
 * when it ends; the other tasks run meanwhile), asynchronous (it plays as
 * its task takes its messages), looped (asynchronous, until another
 * sound or NULL stops it). A name that is not found plays [sounds]
 * SystemDefault, unless SND_NODEFAULT. One sound at a time: a new one
 * stops it (SND_NOSTOP: fails instead); a program opening the wave
 * device (waveOut, MCI) stops it too, the device is the program's.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "mmsystem.h"
#include "windll.h"
#include "wave_driver.h"

void winn_volume_pcm(uint8_t *p, unsigned n, unsigned bits, unsigned channels);   /* volume.c */
const win_wave_driver *winn_wave_driver_load(HMODULE *module);                    /* mci_wave.c */

#define SND_SYNC_       0x0000u
#define SND_ASYNC_      0x0001u
#define SND_NODEFAULT_  0x0002u
#define SND_MEMORY_     0x0004u
#define SND_LOOP_       0x0008u
#define SND_NOSTOP_     0x0010u
#define SND_PURGE_      0x0040u
#define SND_ALIAS_      0x00010000ul
#define SND_FILENAME_   0x00020000ul
#define SND_RESOURCE_   0x00040004ul
#define SND_NOWAIT_     0x00002000ul
#define SOUND_MAX       (1024u * 1024u)     /* an image in memory, at most */
#define STAGE           4096u           /* a block: the drivers take 4K */

static struct {
    int playing;
    HTASK task;
    HMODULE module;
    const win_wave_driver *driver;
    HFILE file;                         /* or the image: */
    uint8_t *image;
    uint32_t length;                    /* the image's */
    uint32_t data, size, at;            /* the samples: where, how many, the next */
    uint32_t rate;
    uint16_t bits, channels, block;
    int loop, queued;
    UINT timer;
} snd = { .file = HFILE_ERROR };

static uint8_t stage[STAGE];

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }

/* bytes of the sound at offset: from the file or the image */
static int fetch(uint32_t offset, void *dst, unsigned n)
{
    if (snd.image) {
        if (offset > snd.length || n > snd.length - offset)
            return 0;
        memcpy(dst, snd.image + offset, n);
        return 1;
    }
    return offset <= 0x7fffffffu && _llseek(snd.file, (LONG)offset, 0) == (LONG)offset &&
           _lread(snd.file, dst, (UINT)n) == (UINT)n;
}

/* RIFF WAVE: its PCM format, where its samples are (the file's length,
 * or the image's) */
static int parse(uint32_t length)
{
    uint8_t h[16];
    if (length < 44 || !fetch(0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return 0;
    uint32_t end = get32(h + 4);
    end = end > length - 8u ? length : end + 8u;
    int fmt = 0;
    for (uint32_t off = 12; off + 8u <= end;) {
        if (!fetch(off, h, 8))
            return 0;
        uint32_t n = get32(h + 4);
        if (n > end - off - 8u)
            n = end - off - 8u;
        if (!memcmp(h, "fmt ", 4) && n >= 16) {
            if (!fetch(off + 8u, h, 16))
                return 0;
            snd.channels = get16(h + 2);
            snd.rate = get32(h + 4);
            snd.block = get16(h + 12);
            snd.bits = get16(h + 14);
            if (get16(h) != 1 || (snd.channels != 1 && snd.channels != 2) ||
                (snd.bits != 8 && snd.bits != 16) || snd.rate < 4000u || snd.rate > 48000u ||
                snd.block != snd.channels * (snd.bits / 8u))
                return 0;
            fmt = 1;
        } else if (!memcmp(h, "data", 4) && fmt) {
            snd.data = off + 8u;
            snd.size = n - n % snd.block;
            return snd.size != 0;
        }
        off += 8u + n + (n & 1u);
    }
    return 0;
}

static void CALLBACK tick(HWND h, UINT m, UINT id, DWORD t);

static void stop(void)
{
    if (snd.timer) {
        KillTimer(0, snd.timer);
        snd.timer = 0;
    }
    if (snd.driver) {
        snd.driver->stop();
        snd.driver->close();
        FreeLibrary(snd.module);
        snd.driver = NULL;
        snd.module = 0;
    }
    if (snd.file != HFILE_ERROR)
        _lclose(snd.file);
    snd.file = HFILE_ERROR;
    free(snd.image);
    snd.image = NULL;
    snd.playing = 0;
}

/* the blocks played counted, the next ones submitted; at the end once
 * all are played (or from the start again, looped) */
static void pump(void)
{
    if (!snd.playing || !snd.driver)
        return;
    int done = snd.driver->finished();
    snd.queued = done >= snd.queued ? 0 : snd.queued - done;
    while (snd.driver->room() > 0) {
        if (snd.at >= snd.size) {
            if (!snd.loop)
                break;
            snd.at = 0;
        }
        uint32_t n = snd.size - snd.at;
        if (n > STAGE)
            n = STAGE;
        if (n > snd.driver->max_bytes)
            n = snd.driver->max_bytes;
        n -= n % snd.block;
        if (!n || !fetch(snd.data + snd.at, stage, n)) {
            snd.size = snd.at;          /* what could not be read ends it */
            snd.loop = 0;
            break;
        }
        winn_volume_pcm(stage, n, snd.bits, snd.channels);
        if (!snd.driver->submit(stage, n, snd.rate, snd.bits, snd.channels)) {
            snd.size = snd.at;
            snd.loop = 0;
            break;
        }
        snd.queued++;
        snd.at += n;
    }
    if (snd.at >= snd.size && !snd.loop && !snd.queued)
        stop();
}

static void CALLBACK tick(HWND h, UINT m, UINT id, DWORD t)
{
    (void)h; (void)m; (void)id; (void)t;
    if (snd.task == GetCurrentTask())
        pump();
}

/* the file of a name: as it is, in the Windows directory, its SYSTEM */
static HFILE open_named(const char *name)
{
    HFILE f = _lopen(name, OF_READ);
    if (f != HFILE_ERROR || !name[0] || name[1] == ':' || name[0] == '\\')
        return f;
    char path[260];
    UINT n = GetWindowsDirectory(path, sizeof(path) - 20);
    if (!n || n + strlen(name) + 10 >= sizeof(path))
        return HFILE_ERROR;
    if (path[n - 1] != '\\')
        path[n++] = '\\';
    lstrcpy(path + n, name);
    f = _lopen(path, OF_READ);
    if (f == HFILE_ERROR) {
        lstrcpy(path + n, "SYSTEM\\");
        lstrcpy(path + n + 7, name);
        f = _lopen(path, OF_READ);
    }
    return f;
}

/* [sounds]' file for an event's name (none: "") */
static void sound_of(const char *event, char *file, unsigned cap)
{
    GetProfileString("sounds", event, "", file, (int)cap);
    for (char *p = file; *p; p++)
        if (*p == ',') {
            *p = 0;
            break;
        }
    while (*file == ' ')
        memmove(file, file + 1, strlen(file));
}

/* the sound opened: a file (alias: [sounds]' name, file: a path, either:
 * [sounds] first, as Windows looks); 1 if found */
static int open_sound(const char *name, int alias, int filename)
{
    char file[160];
    file[0] = 0;
    if (!filename)
        sound_of(name, file, sizeof(file));
    if (file[0])
        snd.file = open_named(file);
    if (snd.file == HFILE_ERROR && !alias)
        snd.file = open_named(name);
    return snd.file != HFILE_ERROR;
}

static int start(int async, int loop)
{
    LONG length = snd.image ? (LONG)snd.length : _llseek(snd.file, 0, 2);
    snd.size = 0;
    if (length <= 0 || !parse((uint32_t)length))
        return 0;
    snd.driver = winn_wave_driver_load(&snd.module);
    if (!snd.driver)
        return 0;
    if (!snd.driver->open()) {
        FreeLibrary(snd.module);
        snd.driver = NULL;
        snd.module = 0;
        return 0;                       /* the device is a program's */
    }
    snd.playing = 1;
    snd.task = GetCurrentTask();
    snd.loop = async && loop;
    snd.at = 0;
    snd.queued = 0;
    if (async) {
        snd.timer = SetTimer(0, 0, 10, tick);
        if (!snd.timer)
            return 0;
        pump();
        return 1;
    }
    while (snd.playing) {
        pump();
        if (snd.playing)
            Yield();
    }
    return 1;
}

/* name: a sound's (see open_sound), or an image (SND_MEMORY: bytes
 * long, a copy of the program's); NULL stops the one playing */
static BOOL play(const char *name, const uint8_t *image, uint32_t bytes, DWORD flags)
{
    if (snd.playing && (flags & SND_NOSTOP_) && (name || image))
        return FALSE;
    stop();
    if ((!name && !image) || (flags & SND_PURGE_))
        return TRUE;
    int async = (flags & SND_ASYNC_) != 0, loop = (flags & SND_LOOP_) != 0;
    if (image) {
        snd.image = (uint8_t *)malloc(bytes);
        if (!snd.image)
            return FALSE;
        memcpy(snd.image, image, bytes);
        snd.length = bytes;
        if (start(async, loop))
            return TRUE;
        stop();
        return FALSE;
    }
    if ((flags & SND_RESOURCE_) == SND_RESOURCE_)
        return FALSE;                   /* a module's WAVE resource: not here */
    int found = open_sound(name, (flags & SND_ALIAS_) != 0, (flags & SND_FILENAME_) != 0);
    if (!found && !(flags & SND_NODEFAULT_))
        found = open_sound("SystemDefault", 1, 0);
    if (found && start(async, loop))
        return TRUE;
    stop();
    return FALSE;
}

/* a native image: its length the RIFF's */
static BOOL native_play(LPCSTR name, DWORD flags)
{
    if (name && (flags & SND_MEMORY_)) {
        const uint8_t *p = (const uint8_t *)name;
        if (memcmp(p, "RIFF", 4) || get32(p + 4) > SOUND_MAX - 8u)
            return FALSE;
        return play(NULL, p, get32(p + 4) + 8u, flags);
    }
    return play(name, NULL, 0, flags);
}

BOOL WINAPI winn_sndPlaySound(LPCSTR name, UINT flags)
{
    return native_play(name, flags & 0xffffu);
}

BOOL WINAPI winn_PlaySound(LPCSTR name, HINSTANCE module, DWORD flags)
{
    (void)module;
    return native_play(name, flags);
}

/* a Win16 program's: its name, or its image (SND_MEMORY: the RIFF's
 * length from its header, within its block) */
static BOOL guest_play(win_dll_call *c, uint32_t far, DWORD flags)
{
    if (!far)
        return play(NULL, NULL, 0, flags);
    if (flags & SND_MEMORY_) {
        uint8_t h[8];
        if (!__win_dll_ne->read(c, far, h, 8) || memcmp(h, "RIFF", 4))
            return FALSE;
        uint32_t n = get32(h + 4) + 8u;
        if (n > SOUND_MAX)
            return FALSE;
        uint8_t *p = (uint8_t *)malloc(n);
        BOOL ok = p && __win_dll_ne->read(c, far, p, n) && play(NULL, p, n, flags);
        free(p);
        return ok;
    }
    char name[160];
    if (!__win_dll_ne->string(c, far, name, sizeof(name)))
        return FALSE;
    return play(name, NULL, 0, flags);
}

/* sndPlaySound(LPCSTR, UINT), PlaySound(LPCSTR, HMODULE, DWORD) */
int winn_snd_ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *value)
{
    if (ordinal == 2) {
        *value = guest_play(c, __win_dll_ne->arg32(c, 6), __win_dll_ne->arg16(c, 4));
        return WINDLL_WORD;
    }
    if (ordinal == 3) {
        *value = guest_play(c, __win_dll_ne->arg32(c, 10), __win_dll_ne->arg32(c, 4));
        return WINDLL_WORD;
    }
    return WINDLL_NONE;
}

/* the wave device wanted by a program (waveOut, MCI): the sound stops */
void winn_snd_release(void)
{
    if (snd.playing)
        stop();
}

void winn_snd_task_end(uint16_t task)
{
    if (snd.playing && (uint16_t)snd.task == task)
        stop();
}

void winn_snd_term(void)
{
    stop();
}
