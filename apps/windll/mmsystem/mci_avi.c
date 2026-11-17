/*
 * mci_avi.c - Win16 MCI "AVIVideo" device: Video for Windows' MCIAVI as
 * WINN has it, for AVI files of the codecs Windows 3.1 itself brought:
 * uncompressed DIBs (8, 16, 24 bits), RLE8 and Microsoft Video 1 (CRAM,
 * 8 and 16 bits). The sound (PCM) goes to the installed wave driver as
 * MCI's waveaudio's does; a file of another video codec plays its sound,
 * the picture black; of another sound format, silent.
 *
 * The frames are drawn into the default window (a window of its own,
 * shown when it plays) or the program's (MCI_WINDOW), at the destination
 * (MCI_PUT; the movie's size at the top left), as the frame's time comes:
 * a frame late is decoded, not drawn. The time format: frames (the
 * default) or milliseconds.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "winuser.h"
#include "mmsystem.h"
#include "windll.h"
#include "wave_driver.h"

void winn_volume_pcm(uint8_t *p, unsigned n, unsigned bits, unsigned channels);   /* volume.c */
const win_wave_driver *winn_wave_driver_load(HMODULE *module);                    /* mci_wave.c */

#define NE (__win_dll_ne)
#define AVI_MCI_ID      3u

#define MCI_OPEN        0x0803u
#define MCI_CLOSE       0x0804u
#define MCI_PLAY        0x0806u
#define MCI_SEEK        0x0807u
#define MCI_STOP        0x0808u
#define MCI_PAUSE       0x0809u
#define MCI_INFO        0x080au
#define MCI_GETDEVCAPS  0x080bu
#define MCI_SET         0x080du
#define MCI_STEP        0x080eu
#define MCI_STATUS      0x0814u
#define MCI_CUE         0x0830u
#define MCI_REALIZE     0x0840u
#define MCI_WINDOW      0x0841u
#define MCI_PUT         0x0842u
#define MCI_WHERE       0x0843u
#define MCI_FREEZE      0x0844u
#define MCI_UNFREEZE    0x0845u
#define MCI_LOAD        0x0850u
#define MCI_UPDATE      0x0854u
#define MCI_RESUME      0x0855u

#define MCI_NOTIFY      0x00000001ul
#define MCI_WAIT        0x00000002ul
#define MCI_FROM        0x00000004ul
#define MCI_TO          0x00000008ul
#define MCI_LOAD_FILE   0x00000100ul
#define MCI_SEEK_TO_START 0x00000100ul
#define MCI_SEEK_TO_END 0x00000200ul
#define MCI_STATUS_ITEM 0x00000100ul
#define MCI_SET_TIME_FORMAT 0x00000400ul
#define MCI_SET_AUDIO   0x00000800ul
#define MCI_SET_VIDEO   0x00001000ul
#define MCI_SET_ON      0x00002000ul
#define MCI_SET_OFF     0x00004000ul
#define MCI_GETDEVCAPS_ITEM 0x00000100ul
#define MCI_INFO_PRODUCT 0x00000100ul
#define MCI_INFO_FILE   0x00000200ul
#define MCI_DGV_RECT    0x00010000ul        /* MCI_PUT, MCI_UPDATE */
#define MCI_DGV_PUT_SOURCE 0x00020000ul     /* MCI_PUT, MCI_WHERE */
#define MCI_DGV_PUT_DESTINATION 0x00040000ul
#define MCI_DGV_PUT_FRAME 0x00080000ul
#define MCI_DGV_PUT_VIDEO 0x00100000ul
#define MCI_DGV_PUT_WINDOW 0x00200000ul
#define MCI_DGV_WINDOW_HWND 0x00010000ul
#define MCI_DGV_WINDOW_STATE 0x00040000ul
#define MCI_DGV_WINDOW_TEXT 0x00080000ul
#define MCI_DGV_UPDATE_HDC 0x00020000ul
#define MCI_DGV_STEP_REVERSE 0x00010000ul
#define MCI_DGV_STEP_FRAMES 0x00020000ul
#define MCI_DGV_PLAY_REPEAT 0x00010000ul
#define MCI_DGV_INFO_TEXT 0x00010000ul

#define MCI_MODE_STOP   525u
#define MCI_MODE_PLAY   526u
#define MCI_MODE_PAUSE  529u
#define MCI_FORMAT_MILLISECONDS 0u
#define MCI_FORMAT_FRAMES 3u
#define MCI_DEVTYPE_DIGITAL_VIDEO 520u
#define MCI_STATUS_LENGTH 1u
#define MCI_STATUS_POSITION 2u
#define MCI_STATUS_NUMBER_OF_TRACKS 3u
#define MCI_STATUS_MODE 4u
#define MCI_STATUS_MEDIA_PRESENT 5u
#define MCI_STATUS_TIME_FORMAT 6u
#define MCI_STATUS_READY 7u
#define MCI_STATUS_CURRENT_TRACK 8u
#define MCI_STATUS_START 0x200ul            /* the flag: the start position */
#define MCI_DGV_STATUS_HWND 0x4001u
#define MCI_DGV_STATUS_SPEED 0x4003u
#define MCI_DGV_STATUS_HPAL 0x4004u
#define MCI_DGV_STATUS_FRAME_RATE 0x400eu
#define MCI_DGV_STATUS_FORWARD 0x402cu
#define MCI_NOTIFY_SUCCESSFUL 1u
#define MCI_NOTIFY_SUPERSEDED 2u
#define MCI_NOTIFY_ABORTED 4u
#define MM_MCINOTIFY    0x03b9u

#define MCIERR_INVALID_DEVICE_ID 257u
#define MCIERR_OUT_OF_MEMORY 264u
#define MCIERR_UNSUPPORTED_FUNCTION 274u
#define MCIERR_FILE_NOT_FOUND 275u
#define MCIERR_DEVICE_NOT_READY 276u
#define MCIERR_MISSING_PARAMETER 273u
#define MCIERR_BAD_TIME_FORMAT 293u
#define MCIERR_INVALID_FILE 296u
#define MCIERR_UNRECOGNIZED_COMMAND 261u
#define MCIERR_OUTOFRANGE 282u

#define FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define KEY_FRAME 0x80000000u           /* in a chunk's size */
#define MAX_CHUNKS 32768u
#define MAX_SIDE 1024
#define STAGE 8192u

/* the codecs, of the frames as they are in the file */
enum { RAW, RLE8, CRAM };

typedef struct { uint32_t offset, size; } chunk;

typedef struct {
    int opened;
    HTASK task;
    HFILE file;
    char filename[160], alias[64];
    /* the video stream */
    int width, height, bits, codec, video_ok;
    uint32_t compression;               /* the strf's, as Windows names a codec */
    uint32_t rate, scale, frames;
    chunk *video;
    uint8_t *packed;                    /* a frame as read */
    uint32_t packed_size;
    uint8_t *pixels;                    /* drawn: 8 bits, bottom up */
    uint16_t *pixels16;                 /* CRAM 16's own state */
    int pitch;
    struct { BITMAPINFOHEADER h; RGBQUAD colors[256]; } bmi;
    int32_t decoded;                    /* the frame in pixels, -1 none */
    uint32_t position;                  /* frames */
    /* the sound */
    chunk *audio;
    uint32_t audio_chunks, audio_bytes, wrate, wavg;
    uint16_t wbits, wchannels, wblock;
    int audio_on;
    HMODULE module;
    const win_wave_driver *driver;
    uint32_t a_chunk, a_in, a_cursor, a_end;
    int a_queued;
    /* playing */
    uint8_t playing, paused, repeat;
    uint32_t origin, started, end_frame, time_format, mode;
    uint16_t timer;
    HWND notify;
    /* the windows */
    HWND def, app;
    RECT dst, src;                      /* left, top, width, height */
} avi_mci;

static avi_mci avi;
static ATOM wnd_class;
static uint8_t stage[STAGE];

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
static int text_guest(win_dll_call *c, uint32_t p, char *buf, unsigned cap) {
    return p && (p >> 16) && NE && NE->string(c, p, buf, cap);
}
static int write_dword(win_dll_call *c, uint32_t p, uint32_t v) {
    uint8_t b[4]; put32(b, v); return write_guest(c, p, b, 4);
}
static int write_text(win_dll_call *c, uint32_t dest, uint32_t cap, const char *s) {
    unsigned n = (unsigned)strlen(s);
    if (!dest || !cap) return 0;
    if (n >= cap) n = (unsigned)cap - 1u;
    return write_guest(c, dest, s, n) && write_guest(c, dest + n, "", 1);
}
static void notify(HWND h, unsigned code) {
    if (h) PostMessage(h, MM_MCINOTIFY, code, AVI_MCI_ID);
}
static void abort_notify(unsigned code) {
    HWND h = avi.notify;
    avi.notify = 0;
    notify(h, code);
}
static int owned(void) {
    return avi.opened && avi.task == GetCurrentTask();
}
int winn_mci_avi_type(const char *type) {
    return !lstrcmpi(type, "avivideo") || !lstrcmpi(type, "mciavi") ||
           !lstrcmpi(type, "digitalvideo");
}
int winn_mci_avi_extension(const char *path) {
    const char *dot = NULL;
    for (const char *p = path; *p; ++p)
        if (*p == '.') dot = p;
    return dot && !lstrcmpi(dot, ".avi");
}
int winn_mci_avi_match(const char *s) {
    return owned() && ((avi.alias[0] && !lstrcmpi(s, avi.alias)) ||
                       (avi.filename[0] && !lstrcmpi(s, avi.filename)) ||
                       winn_mci_avi_type(s) || !lstrcmpi(s, "3"));
}

/* ---------------------------------------------------------------- the file */

static int read_at(uint32_t offset, void *dst, unsigned n) {
    return offset <= 0x7fffffffu && _llseek(avi.file, (LONG)offset, 0) == (LONG)offset &&
           _lread(avi.file, dst, (UINT)n) == (UINT)n;
}

static int add_chunk(chunk **all, uint32_t *n, uint32_t *cap, uint32_t offset, uint32_t size) {
    if (*n >= MAX_CHUNKS) return 0;
    if (*n == *cap) {
        uint32_t next = *cap ? *cap * 2u : 256u;
        chunk *p = (chunk *)realloc(*all, next * sizeof(chunk));
        if (!p) return 0;
        *all = p;
        *cap = next;
    }
    (*all)[*n].offset = offset;
    (*all)[*n].size = size;
    ++*n;
    return 1;
}

typedef struct {
    uint32_t vcap, acap, vstream, astream, frames;
} index_state;

/* a chunk of the movie: the stream's (its id "nnxx"), where its data is */
static int index_chunk(index_state *s, uint32_t id, uint32_t data, uint32_t size, int key) {
    unsigned d1 = id & 0xffu, d2 = (id >> 8) & 0xffu;
    if (d1 < '0' || d1 > '9' || d2 < '0' || d2 > '9') return 1;
    uint32_t stream = (d1 - '0') * 10u + (d2 - '0');
    uint32_t kind = id >> 16;
    if (stream == s->vstream && (kind == ('d' | 'c' << 8) || kind == ('d' | 'b' << 8))) {
        if (size > 0x7fffffffu) return 0;
        return add_chunk(&avi.video, &avi.frames, &s->vcap, data, size | (key ? KEY_FRAME : 0));
    }
    if (stream == s->vstream && kind == ('p' | 'c' << 8))   /* a palette change: frame's */
        return 1;
    if (stream == s->astream && kind == ('w' | 'b' << 8) && size) {
        avi.audio_bytes += size;
        return add_chunk(&avi.audio, &avi.audio_chunks, &s->acap, data, size);
    }
    return 1;
}

/* the movie's chunks in order (no index: the frames all key ones but the
 * first only for the codecs of deltas) */
static int scan_movi(index_state *s, uint32_t at, uint32_t end) {
    while (at + 8u <= end) {
        uint8_t ck[12];
        if (!read_at(at, ck, 8)) return 0;
        uint32_t id = get32(ck), n = get32(ck + 4);
        if (n > end - at - 8u) n = end - at - 8u;
        if (id == FOURCC('L', 'I', 'S', 'T')) {
            if (!scan_movi(s, at + 12u, at + 8u + n)) return 0;
        } else if (!index_chunk(s, id, at + 8u, n, avi.codec == RAW || avi.frames == 0)) {
            return 0;
        }
        at += 8u + n + (n & 1u);
    }
    return 1;
}

static int read_index(index_state *s, uint32_t idx, uint32_t n, uint32_t movi) {
    uint8_t e[16];
    uint32_t count = n / 16u;
    if (!count) return 0;
    /* offsets from the 'movi' list's type, or (some writers) from the file's start */
    uint32_t base = movi;
    if (!read_at(idx, e, 16)) return 0;
    {
        uint8_t id[4];
        uint32_t off = get32(e + 8);
        if (!(read_at(movi + off, id, 4) && !memcmp(id, e, 4)) &&
            read_at(off, id, 4) && !memcmp(id, e, 4))
            base = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!read_at(idx + i * 16u, e, 16)) return 0;
        uint32_t id = get32(e), flags = get32(e + 4), off = get32(e + 8), size = get32(e + 12);
        if (id == FOURCC('r', 'e', 'c', ' ') || (flags & 1u))   /* AVIIF_LIST */
            continue;
        if (!index_chunk(s, id, base + off + 8u, size, (flags & 0x10u) != 0))
            return 0;
    }
    return 1;
}

static void cube_palette(void) {
    for (int i = 0; i < 216; i++) {
        avi.bmi.colors[i].rgbRed = (BYTE)(i / 36 * 51);
        avi.bmi.colors[i].rgbGreen = (BYTE)(i / 6 % 6 * 51);
        avi.bmi.colors[i].rgbBlue = (BYTE)(i % 6 * 51);
        avi.bmi.colors[i].rgbReserved = 0;
    }
    avi.bmi.h.biClrUsed = 216;
}

static uint32_t parse(void) {
    LONG len = _llseek(avi.file, 0, 2);
    uint8_t h[64];
    if (len < 24 || !read_at(0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "AVI ", 4))
        return MCIERR_INVALID_FILE;
    uint32_t end = get32(h + 4) + 8u;
    if (end > (uint32_t)len) end = (uint32_t)len;
    uint32_t at = 12, movi = 0, movi_end = 0, idx = 0, idx_size = 0, stream = 0;
    uint32_t vtype_stream = 0xffffffffu, atype_stream = 0xffffffffu;
    int have_main = 0;
    while (at + 8u <= end) {
        if (!read_at(at, h, 12)) return MCIERR_INVALID_FILE;
        uint32_t id = get32(h), n = get32(h + 4);
        if (n > end - at - 8u) n = end - at - 8u;
        if (id == FOURCC('L', 'I', 'S', 'T') && !memcmp(h + 8, "hdrl", 4)) {
            /* avih, then a strl a stream: strh, strf */
            uint32_t p = at + 12u, e = at + 8u + n;
            while (p + 8u <= e) {
                if (!read_at(p, h, 12)) return MCIERR_INVALID_FILE;
                uint32_t sid = get32(h), sn = get32(h + 4);
                if (sn > e - p - 8u) sn = e - p - 8u;
                if (sid == FOURCC('a', 'v', 'i', 'h') && sn >= 40) {
                    if (!read_at(p + 8u, h, 40)) return MCIERR_INVALID_FILE;
                    have_main = 1;
                } else if (sid == FOURCC('L', 'I', 'S', 'T') && !memcmp(h + 8, "strl", 4)) {
                    uint32_t q = p + 12u, qe = p + 8u + sn, type = 0;
                    uint8_t strh[56];
                    while (q + 8u <= qe) {
                        uint8_t k[8];
                        if (!read_at(q, k, 8)) return MCIERR_INVALID_FILE;
                        uint32_t kid = get32(k), kn = get32(k + 4);
                        if (kn > qe - q - 8u) kn = qe - q - 8u;
                        if (kid == FOURCC('s', 't', 'r', 'h') && kn >= 48) {
                            memset(strh, 0, sizeof(strh));
                            if (!read_at(q + 8u, strh, kn < 56 ? kn : 56)) return MCIERR_INVALID_FILE;
                            type = get32(strh);
                        } else if (kid == FOURCC('s', 't', 'r', 'f') &&
                                   type == FOURCC('v', 'i', 'd', 's') &&
                                   vtype_stream == 0xffffffffu && kn >= 40) {
                            uint8_t bi[40];
                            if (!read_at(q + 8u, bi, 40)) return MCIERR_INVALID_FILE;
                            vtype_stream = stream;
                            avi.scale = get32(strh + 20);
                            avi.rate = get32(strh + 24);
                            avi.width = (int)get32(bi + 4);
                            avi.height = (int)get32(bi + 8);
                            avi.bits = get16(bi + 14);
                            uint32_t comp = get32(bi + 16), used = get32(bi + 32);
                            avi.compression = comp;
                            if (avi.height < 0) avi.height = -avi.height;
                            avi.video_ok = 1;
                            if (comp == 0 && (avi.bits == 8 || avi.bits == 16 || avi.bits == 24))
                                avi.codec = RAW;
                            else if ((comp == 1 || comp == FOURCC('R', 'L', 'E', ' ') ||
                                      comp == FOURCC('m', 'r', 'l', 'e')) && avi.bits == 8)
                                avi.codec = RLE8;
                            else if ((comp == FOURCC('C', 'R', 'A', 'M') || comp == FOURCC('c', 'r', 'a', 'm') ||
                                      comp == FOURCC('M', 'S', 'V', 'C') || comp == FOURCC('m', 's', 'v', 'c')) &&
                                     (avi.bits == 8 || avi.bits == 16))
                                avi.codec = CRAM;
                            else
                                avi.video_ok = 0;       /* black: its sound only */
                            if (avi.bits == 8) {
                                uint32_t colors = used && used <= 256 ? used : 256;
                                uint32_t hs = get32(bi);
                                if (hs < 40 || hs > kn) hs = 40;
                                if (kn >= hs + colors * 4u &&
                                    read_at(q + 8u + hs, avi.bmi.colors, colors * 4u))
                                    avi.bmi.h.biClrUsed = colors;
                                else
                                    cube_palette();
                            } else {
                                cube_palette();
                            }
                        } else if (kid == FOURCC('s', 't', 'r', 'f') &&
                                   type == FOURCC('a', 'u', 'd', 's') &&
                                   atype_stream == 0xffffffffu && kn >= 16) {
                            uint8_t wf[16];
                            if (!read_at(q + 8u, wf, 16)) return MCIERR_INVALID_FILE;
                            avi.wchannels = get16(wf + 2);
                            avi.wrate = get32(wf + 4);
                            avi.wavg = get32(wf + 8);
                            avi.wblock = get16(wf + 12);
                            avi.wbits = get16(wf + 14);
                            if (get16(wf) == 1 && (avi.wchannels == 1 || avi.wchannels == 2) &&
                                (avi.wbits == 8 || avi.wbits == 16) &&
                                avi.wrate >= 4000u && avi.wrate <= 48000u &&
                                avi.wblock == avi.wchannels * (avi.wbits / 8u))
                            {
                                avi.wavg = avi.wrate * avi.wblock;
                                atype_stream = stream;
                            }
                        }
                        q += 8u + kn + (kn & 1u);
                    }
                    stream++;
                }
                p += 8u + sn + (sn & 1u);
            }
        } else if (id == FOURCC('L', 'I', 'S', 'T') && !memcmp(h + 8, "movi", 4)) {
            movi = at + 8u;
            movi_end = at + 8u + n;
        } else if (id == FOURCC('i', 'd', 'x', '1')) {
            idx = at + 8u;
            idx_size = n;
        }
        at += 8u + n + (n & 1u);
    }
    if (!have_main || vtype_stream == 0xffffffffu || !movi || !avi.rate || !avi.scale ||
        avi.width < 1 || avi.height < 1 || avi.width > MAX_SIDE || avi.height > MAX_SIDE)
        return MCIERR_INVALID_FILE;
    index_state s = { 0, 0, vtype_stream, atype_stream, 0 };
    if (!(idx && read_index(&s, idx, idx_size, movi))) {
        free(avi.video); free(avi.audio);
        avi.video = avi.audio = NULL;
        avi.frames = avi.audio_chunks = avi.audio_bytes = 0;
        s.vcap = s.acap = 0;
        if (!scan_movi(&s, movi + 4u, movi_end)) return MCIERR_INVALID_FILE;
    }
    if (!avi.frames) return MCIERR_INVALID_FILE;
    if (avi.frames && !(avi.video[0].size & KEY_FRAME))
        avi.video[0].size |= KEY_FRAME;     /* the first is one, marked or not */
    if (atype_stream == 0xffffffffu) avi.audio_bytes = 0;
    avi.audio_bytes -= avi.audio_bytes % (avi.wblock ? avi.wblock : 1u);

    uint32_t largest = 0;
    for (uint32_t i = 0; i < avi.frames; i++)
        if ((avi.video[i].size & ~KEY_FRAME) > largest) largest = avi.video[i].size & ~KEY_FRAME;
    avi.pitch = (avi.width + 3) & ~3;
    avi.packed_size = largest;
    avi.packed = (uint8_t *)malloc(largest ? largest : 1u);
    avi.pixels = (uint8_t *)calloc((size_t)avi.pitch * (size_t)avi.height, 1);
    if (avi.video_ok && avi.codec == CRAM && avi.bits == 16)
        avi.pixels16 = (uint16_t *)calloc((size_t)avi.width * (size_t)avi.height, 2);
    if (!avi.packed || !avi.pixels || (avi.video_ok && avi.codec == CRAM && avi.bits == 16 &&
                                       !avi.pixels16))
        return MCIERR_OUT_OF_MEMORY;
    BITMAPINFOHEADER *bh = &avi.bmi.h;
    bh->biSize = sizeof(BITMAPINFOHEADER);
    bh->biWidth = avi.width;
    bh->biHeight = avi.height;
    bh->biPlanes = 1;
    bh->biBitCount = 8;
    bh->biCompression = BI_RGB;
    bh->biSizeImage = (DWORD)avi.pitch * (DWORD)avi.height;
    avi.decoded = -1;
    return 0;
}

/* ---------------------------------------------------------------- the codecs */

static uint8_t cube(unsigned r, unsigned g, unsigned b) {   /* 0..255 each */
    return (uint8_t)((r * 6u >> 8) * 36u + (g * 6u >> 8) * 6u + (b * 6u >> 8));
}
static uint8_t cube555(unsigned v) {
    unsigned r = (v >> 10) & 31u, g = (v >> 5) & 31u, b = v & 31u;
    return cube(r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2);
}

static void raw(const uint8_t *in, uint32_t n) {
    int w = avi.width, h = avi.height;
    int sp = ((w * avi.bits + 31) / 32) * 4;
    for (int y = 0; y < h; y++) {
        if ((uint32_t)(y + 1) * (uint32_t)sp > n) break;
        const uint8_t *s = in + (size_t)y * (size_t)sp;
        uint8_t *d = avi.pixels + (size_t)y * (size_t)avi.pitch;
        if (avi.bits == 8)
            memcpy(d, s, (size_t)w);
        else if (avi.bits == 16)
            for (int x = 0; x < w; x++) d[x] = cube555(get16(s + 2 * x));
        else
            for (int x = 0; x < w; x++) d[x] = cube(s[3 * x + 2], s[3 * x + 1], s[3 * x]);
    }
}

static void rle8(const uint8_t *in, uint32_t n) {
    int x = 0, y = 0, w = avi.width, h = avi.height;
    uint32_t i = 0;
    while (i + 1u < n && y < h) {
        unsigned count = in[i], v = in[i + 1];
        i += 2;
        if (count) {
            uint8_t *row = avi.pixels + (size_t)y * (size_t)avi.pitch;
            while (count-- && x < w) row[x++] = (uint8_t)v;
        } else if (v == 0) {
            x = 0; y++;
        } else if (v == 1) {
            break;
        } else if (v == 2) {
            if (i + 1u >= n) break;
            x += in[i]; y += in[i + 1];
            i += 2;
        } else {
            uint8_t *row = avi.pixels + (size_t)y * (size_t)avi.pitch;
            for (unsigned k = 0; k < v && i < n; k++, i++)
                if (x < w) row[x++] = in[i];
            i += v & 1u;
        }
    }
}

/* Microsoft Video 1: 4x4 blocks, bottom up, left to right; a block skipped
 * keeps the previous frame's */
static void cram(const uint8_t *in, uint32_t n) {
    int bw = avi.width / 4, bh = avi.height / 4, wide = avi.bits == 16;
    uint32_t i = 0, skip = 0;
    int total = bw * bh;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            if (skip) { skip--; total--; continue; }
            if (i + 2u > n) return;
            unsigned a = in[i], b = in[i + 1];
            i += 2;
            if (!a && !b && !total) return;
            uint16_t colors[8];
            unsigned flags = b << 8 | a, kind;      /* 1: one colour, 2: two, 8: eight */
            if ((b & 0xfcu) == 0x84u) {
                skip = ((b - 0x84u) << 8) + a - 1u;
                total--;
                continue;
            }
            if (wide) {
                if (b < 0x80u) {
                    if (i + 4u > n) return;
                    colors[0] = get16(in + i); colors[1] = get16(in + i + 2); i += 4;
                    kind = 2;
                    if (colors[0] & 0x8000u) {
                        if (i + 12u > n) return;
                        for (int k = 2; k < 8; k++, i += 2) colors[k] = get16(in + i);
                        kind = 8;
                    }
                } else {
                    colors[0] = (uint16_t)flags;
                    kind = 1;
                }
            } else {
                if (b < 0x80u) {
                    if (i + 2u > n) return;
                    colors[0] = in[i]; colors[1] = in[i + 1]; i += 2;
                    kind = 2;
                } else if (b >= 0x90u) {
                    if (i + 8u > n) return;
                    for (int k = 0; k < 8; k++) colors[k] = in[i + k];
                    i += 8;
                    kind = 8;
                } else {
                    colors[0] = (uint16_t)a;
                    kind = 1;
                }
            }
            for (int py = 0; py < 4; py++) {
                int y = by * 4 + py;
                for (int px = 0; px < 4; px++, flags >>= 1) {
                    unsigned c = kind == 1 ? colors[0] :
                                 kind == 2 ? colors[(flags & 1u) ^ 1u] :
                                 colors[((py & 2) << 1) + (px & 2) + ((flags & 1u) ^ 1u)];
                    int x = bx * 4 + px;
                    if (wide)
                        avi.pixels16[(size_t)y * (size_t)avi.width + (size_t)x] = (uint16_t)(c & 0x7fffu);
                    else
                        avi.pixels[(size_t)y * (size_t)avi.pitch + (size_t)x] = (uint8_t)c;
                }
            }
            total--;
        }
    }
}

/* the 16-bit state of the frame into the 8-bit one drawn */
static void cram16_shown(void) {
    for (int y = 0; y < avi.height; y++) {
        const uint16_t *s = avi.pixels16 + (size_t)y * (size_t)avi.width;
        uint8_t *d = avi.pixels + (size_t)y * (size_t)avi.pitch;
        for (int x = 0; x < avi.width; x++) d[x] = cube555(s[x]);
    }
}

static void decode_one(uint32_t f) {
    uint32_t n = avi.video[f].size & ~KEY_FRAME;
    if (!n || n > avi.packed_size || !read_at(avi.video[f].offset, avi.packed, n)) return;
    if (avi.codec == RAW) raw(avi.packed, n);
    else if (avi.codec == RLE8) rle8(avi.packed, n);
    else cram(avi.packed, n);
}

/* the frame decoded into pixels: from the key frame before it, or on from
 * the one there if that is no further */
static void decode(uint32_t f) {
    if (!avi.video_ok || f >= avi.frames || (int32_t)f == avi.decoded) return;
    uint32_t k = f;
    while (k > 0 && !(avi.video[k].size & KEY_FRAME)) k--;
    uint32_t from = avi.decoded >= 0 && (uint32_t)avi.decoded < f && (uint32_t)avi.decoded >= k
                    ? (uint32_t)avi.decoded + 1u : k;
    for (uint32_t i = from; i <= f; i++) decode_one(i);
    if (avi.pixels16) cram16_shown();
    avi.decoded = (int32_t)f;
}

/* ---------------------------------------------------------------- the picture */

static HWND window(void) {
    if (avi.app && IsWindow(avi.app)) return avi.app;
    return avi.def;
}
static uint32_t shown_frame(void) {
    return avi.position < avi.frames ? avi.position : avi.frames - 1u;
}
static void paint(HDC dc) {
    if (!dc || !avi.pixels || !avi.frames) return;
    if (avi.video_ok) decode(shown_frame());
    int sy = avi.height - avi.src.top - avi.src.bottom;
    StretchDIBits(dc, avi.dst.left, avi.dst.top, avi.dst.right, avi.dst.bottom,
                  avi.src.left, sy, avi.src.right, avi.src.bottom, avi.pixels,
                  (const BITMAPINFO *)&avi.bmi, DIB_RGB_COLORS, SRCCOPY);
}
static void draw(void) {
    HWND h = window();
    if (!h || !IsWindowVisible(h)) return;
    HDC dc = GetDC(h);
    paint(dc);
    ReleaseDC(h, dc);
}

static void halt(int report);

static LRESULT CALLBACK avi_wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (h == avi.def && window() == h) paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        if (h == avi.def && window() == h) {
            RECT rc;
            GetClientRect(h, &rc);
            PatBlt((HDC)wp, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, BLACKNESS);
            return 1;
        }
        break;
    case WM_CLOSE:                          /* as Windows': it stops, hides */
        if (h == avi.def) {
            if (avi.playing || avi.paused) halt(1);
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (h == avi.def) avi.def = 0;
        break;
    }
    return DefWindowProc(h, m, wp, lp);
}

static int make_window(DWORD style, int has_style, HWND parent, const char *title) {
    if (!wnd_class) {
        WNDCLASS wc;
        memset(&wc, 0, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = avi_wndproc;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.lpszClassName = "WinnAVIWnd";
        wnd_class = RegisterClass(&wc);
        if (!wnd_class) return 0;
    }
    if (!has_style) style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, avi.width, avi.height };
    AdjustWindowRect(&rc, style, FALSE);
    int x = (style & WS_CHILD) ? 0 : CW_USEDEFAULT, y = (style & WS_CHILD) ? 0 : CW_USEDEFAULT;
    int cx = rc.right - rc.left, cy = rc.bottom - rc.top;
    HWND active = GetActiveWindow();
    if (!(style & WS_CHILD) && active) {
        /* below the program's window, or beside it, where the screen has
         * room: not over its buttons (Media Player's) */
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        RECT a;
        GetWindowRect(active, &a);
        if (a.bottom + cy <= sh) {
            x = a.left + cx <= sw ? a.left : sw - cx;
            y = a.bottom;
        } else if (a.right + cx <= sw) {
            x = a.right;
            y = a.top + cy <= sh ? a.top : sh - cy;
        }
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    avi.def = CreateWindow("WinnAVIWnd", title, style & ~WS_VISIBLE, x, y, cx, cy, parent, 0, 0,
                           NULL);
    return avi.def != 0;
}

/* ---------------------------------------------------------------- the sound */

static void audio_close(void) {
    if (!avi.driver) return;
    avi.driver->stop();
    avi.driver->close();
    FreeLibrary(avi.module);
    avi.driver = NULL;
    avi.module = 0;
    avi.a_queued = 0;
}
static uint32_t ms_of(uint32_t frame) {
    if (!avi.rate) return 0;
    return (uint32_t)((uint64_t)frame * avi.scale * 1000u / avi.rate);
}
static uint32_t frame_of(uint32_t ms) {
    if (!avi.scale) return 0;
    return (uint32_t)((uint64_t)ms * avi.rate / ((uint64_t)avi.scale * 1000u));
}
static uint32_t audio_byte(uint32_t frame) {
    uint64_t b = (uint64_t)ms_of(frame) * avi.wavg / 1000u;
    if (b > avi.audio_bytes) b = avi.audio_bytes;
    return (uint32_t)b - (uint32_t)b % avi.wblock;
}
/* the sound from frame on, to the end frame's */
static void audio_start(uint32_t frame) {
    if (!avi.audio_bytes || !avi.audio_on) return;
    if (!avi.driver) {
        avi.driver = winn_wave_driver_load(&avi.module);
        if (!avi.driver) return;
        if (!avi.driver->open()) { FreeLibrary(avi.module); avi.driver = NULL; avi.module = 0; return; }
    }
    avi.a_cursor = audio_byte(frame);
    avi.a_end = audio_byte(avi.end_frame);
    uint32_t sum = 0;
    for (avi.a_chunk = 0; avi.a_chunk < avi.audio_chunks; avi.a_chunk++) {
        if (sum + avi.audio[avi.a_chunk].size > avi.a_cursor) break;
        sum += avi.audio[avi.a_chunk].size;
    }
    avi.a_in = avi.a_cursor - sum;
    avi.a_queued = 0;
}
static void audio_pump(void) {
    if (!avi.driver) return;
    int done = avi.driver->finished();
    avi.a_queued = done >= avi.a_queued ? 0 : avi.a_queued - done;
    while (avi.a_cursor < avi.a_end && avi.driver->room() > 0) {
        uint32_t want = avi.a_end - avi.a_cursor, got = 0;
        if (want > STAGE) want = STAGE;
        if (want > avi.driver->max_bytes) want = avi.driver->max_bytes;
        want -= want % avi.wblock;
        while (got < want && avi.a_chunk < avi.audio_chunks) {
            const chunk *ck = &avi.audio[avi.a_chunk];
            uint32_t take = ck->size - avi.a_in;
            if (take > want - got) take = want - got;
            if (!read_at(ck->offset + avi.a_in, stage + got, take)) { avi.a_end = avi.a_cursor; break; }
            got += take;
            avi.a_in += take;
            if (avi.a_in >= ck->size) { avi.a_chunk++; avi.a_in = 0; }
        }
        got -= got % avi.wblock;
        if (!got) { avi.a_end = avi.a_cursor; break; }
        winn_volume_pcm(stage, got, avi.wbits, avi.wchannels);
        if (!avi.driver->submit(stage, got, avi.wrate, avi.wbits, avi.wchannels)) {
            audio_close();
            return;
        }
        avi.a_queued++;
        avi.a_cursor += got;
    }
}

/* ---------------------------------------------------------------- playing */

static uint32_t now_frame(void) {
    if (!avi.playing) return avi.position;
    uint32_t f = avi.origin + frame_of(GetTickCount() - avi.started);
    return f > avi.end_frame ? avi.end_frame : f;
}
static void halt(int report) {
    if (avi.playing) avi.position = now_frame();
    avi.playing = avi.paused = 0;
    avi.mode = MCI_MODE_STOP;
    if (avi.timer) { KillTimer(0, avi.timer); avi.timer = 0; }
    audio_close();
    if (report) abort_notify(MCI_NOTIFY_ABORTED);
}
static void begin(void) {
    avi.origin = avi.position;
    avi.started = GetTickCount();
    audio_start(avi.position);
    audio_pump();
}
static void CALLBACK tick(HWND h, UINT m, UINT id, DWORD t) {
    (void)h; (void)m; (void)id; (void)t;
    if (!avi.playing || !owned()) return;
    audio_pump();
    uint32_t f = now_frame();
    if (f != avi.position) {
        avi.position = f;
        draw();
    }
    if (f >= avi.end_frame && (!avi.driver || (avi.a_cursor >= avi.a_end && !avi.a_queued))) {
        if (avi.repeat && avi.end_frame) {
            audio_close();
            avi.position = 0;
            begin();
            draw();
            return;
        }
        HWND n = avi.notify;
        avi.notify = 0;
        halt(0);
        notify(n, MCI_NOTIFY_SUCCESSFUL);
    }
}
static uint32_t to_frames(uint32_t v) {
    uint32_t f = avi.time_format == MCI_FORMAT_MILLISECONDS ? frame_of(v) : v;
    return f > avi.frames ? avi.frames : f;
}
static uint32_t from_frames(uint32_t f) {
    return avi.time_format == MCI_FORMAT_MILLISECONDS ? ms_of(f) : f;
}
static uint32_t play(uint32_t flags, uint32_t from, uint32_t to, HWND cb, int repeat) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    if (avi.notify) abort_notify(MCI_NOTIFY_SUPERSEDED);
    if (avi.playing || avi.paused) halt(0);
    if (flags & MCI_FROM) avi.position = to_frames(from);
    avi.end_frame = (flags & MCI_TO) ? to_frames(to) : avi.frames;
    if (avi.position >= avi.frames && !(flags & MCI_FROM)) avi.position = 0;   /* again from the start */
    if (avi.end_frame < avi.position) avi.end_frame = avi.position;
    avi.repeat = (uint8_t)repeat;
    avi.notify = (flags & MCI_NOTIFY) ? cb : 0;
    HWND w = window();
    if (w && w == avi.def && !IsWindowVisible(w)) {    /* shown, as Windows', not activated */
        SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(w);
    }
    avi.playing = 1;
    avi.mode = MCI_MODE_PLAY;
    avi.timer = SetTimer(0, 0, 10, tick);
    if (!avi.timer) { halt(0); return MCIERR_DEVICE_NOT_READY; }
    begin();
    draw();
    tick(0, 0, 0, 0);
    if (flags & MCI_WAIT)
        while (avi.playing && owned()) { Yield(); tick(0, 0, 0, 0); }
    return 0;
}
static void do_pause(void) {
    if (!avi.playing) return;
    avi.position = now_frame();
    avi.playing = 0;
    if (avi.timer) { KillTimer(0, avi.timer); avi.timer = 0; }
    audio_close();
    avi.paused = 1;
    avi.mode = MCI_MODE_PAUSE;
}
static uint32_t resume(HWND cb) {
    if (!avi.paused) return 0;
    HWND keep = avi.notify;
    avi.notify = 0;
    uint32_t e = play(MCI_TO, 0, from_frames(avi.end_frame), keep ? keep : cb, avi.repeat);
    if (!e && keep) avi.notify = keep;
    return e;
}
static void seek(uint32_t frame) {
    halt(1);
    avi.position = frame > avi.frames ? avi.frames : frame;
    draw();
}
static void step(int32_t by) {
    halt(1);
    int64_t f = (int64_t)avi.position + by;
    if (f < 0) f = 0;
    if (f >= (int64_t)avi.frames) f = avi.frames ? avi.frames - 1 : 0;
    avi.position = (uint32_t)f;
    draw();
}

/* ---------------------------------------------------------------- open, close */

void winn_mci_avi_close(void) {
    if (!avi.opened) return;
    halt(1);
    if (avi.def) DestroyWindow(avi.def);
    if (avi.file != HFILE_ERROR) _lclose(avi.file);
    free(avi.video); free(avi.audio); free(avi.packed); free(avi.pixels); free(avi.pixels16);
    memset(&avi, 0, sizeof(avi));
}

uint32_t winn_mci_avi_open(const char *filename, const char *alias, DWORD style,
                           int has_style, HWND parent) {
    if (avi.opened) return MCIERR_DEVICE_NOT_READY;
    if (!filename || !filename[0]) {   /* the device, its file later (MCI_LOAD) */
        memset(&avi, 0, sizeof(avi));
        avi.opened = 1;
        avi.file = HFILE_ERROR;
        avi.task = GetCurrentTask();
        if (alias) lstrcpyn(avi.alias, alias, sizeof(avi.alias));
        avi.time_format = MCI_FORMAT_FRAMES;
        avi.mode = MCI_MODE_STOP;
        avi.audio_on = 1;
        return 0;
    }
    HFILE f = _lopen(filename, OF_READ);
    if (f == HFILE_ERROR) return MCIERR_FILE_NOT_FOUND;
    memset(&avi, 0, sizeof(avi));
    avi.opened = 1;
    avi.file = f;
    avi.task = GetCurrentTask();
    uint32_t err = parse();
    if (!err) {
        const char *name = filename;
        for (const char *p = filename; *p; ++p)
            if (*p == '\\' || *p == '/' || *p == ':') name = p + 1;
        if (!make_window(style, has_style, parent, name)) err = MCIERR_OUT_OF_MEMORY;
    }
    if (err) {
        winn_mci_avi_close();
        return err;
    }
    if (!avi.video_ok) {                /* as Windows warns: its sound only */
        char text[96], fcc[5];
        for (int i = 0; i < 4; i++) {
            char ch = (char)(avi.compression >> (8 * i));
            fcc[i] = ch >= ' ' && ch < 127 ? ch : '?';
        }
        fcc[4] = 0;
        wsprintf(text, "Video not available, cannot find 'vids:%s' decompressor.", fcc);
        MessageBox(0, text, "MCI AVI", MB_OK | MB_ICONEXCLAMATION);
    }
    lstrcpyn(avi.filename, filename, sizeof(avi.filename));
    if (alias) lstrcpyn(avi.alias, alias, sizeof(avi.alias));
    avi.time_format = MCI_FORMAT_FRAMES;
    avi.mode = MCI_MODE_STOP;
    avi.audio_on = 1;
    avi.end_frame = avi.frames;
    SetRect(&avi.src, 0, 0, avi.width, avi.height);
    avi.dst = avi.src;
    return 0;
}

/* ---------------------------------------------------------------- the commands */

/* another file in the device open (MCI_LOAD): its window the program's
 * still; none if it cannot be read */
static uint32_t load(const char *name) {
    char alias[64];
    HWND app = avi.app;
    uint32_t format = avi.time_format;
    lstrcpyn(alias, avi.alias, sizeof(alias));
    winn_mci_avi_close();
    uint32_t e = winn_mci_avi_open(name, alias, 0, 0, 0);
    if (e) winn_mci_avi_open("", alias, 0, 0, 0);
    avi.time_format = format;
    if (!e && app) avi.app = IsWindow(app) ? app : 0;
    return e;
}

static uint32_t status(uint32_t item, uint32_t flags, uint32_t *v) {
    switch (item) {
    case MCI_STATUS_LENGTH: *v = from_frames(avi.frames); break;
    case MCI_STATUS_POSITION:
        *v = (flags & MCI_STATUS_START) ? 0 : from_frames(now_frame()); break;
    case MCI_STATUS_MODE: *v = avi.mode; break;
    case MCI_STATUS_MEDIA_PRESENT: *v = avi.frames != 0; break;
    case MCI_STATUS_READY: *v = 1; break;
    case MCI_STATUS_TIME_FORMAT: *v = avi.time_format; break;
    case MCI_STATUS_NUMBER_OF_TRACKS: case MCI_STATUS_CURRENT_TRACK: *v = 1; break;
    case MCI_DGV_STATUS_HWND: *v = (uint32_t)(uintptr_t)window(); break;
    case MCI_DGV_STATUS_SPEED: *v = 1000; break;
    case MCI_DGV_STATUS_HPAL: *v = 0; break;
    case MCI_DGV_STATUS_FRAME_RATE:
        *v = avi.scale ? (uint32_t)((uint64_t)avi.rate * 1000u / avi.scale) : 0; break;
    case MCI_DGV_STATUS_FORWARD: *v = 1; break;
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    return 0;
}
static uint32_t caps(uint32_t item, uint32_t *v) {
    switch (item) {
    case 1: case 7: case 9: *v = 0; break;              /* record, eject, save */
    case 2: case 3: case 5: case 6: case 8: *v = 1; break;   /* audio, video, files, compound, play */
    case 4: *v = MCI_DEVTYPE_DIGITAL_VIDEO; break;
    case 0x4002: *v = 0; break;                         /* MCI_DGV_GETDEVCAPS_CAN_REVERSE */
    case 0x4003: *v = 1; break;                         /* can stretch */
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    return 0;
}
static void set_rect(RECT *r, int x, int y, int w, int h) {
    r->left = x; r->top = y; r->right = w; r->bottom = h;
}
static void client(RECT *r) {
    RECT rc = { 0, 0, avi.width, avi.height };
    HWND h = window();
    if (h) GetClientRect(h, &rc);
    set_rect(r, 0, 0, rc.right - rc.left, rc.bottom - rc.top);
}
static void show(int cmd) {
    HWND h = window();
    if (h) ShowWindow(h, cmd);
}
static void set_window(HWND h) {
    avi.app = h && IsWindow(h) ? h : 0;
    if (avi.app && avi.def && IsWindowVisible(avi.def)) ShowWindow(avi.def, SW_HIDE);
    set_rect(&avi.dst, 0, 0, avi.width, avi.height);
}
/* a Win16 RECT (four shorts): left, top, width, height as MCI's */
static RECT rect16(const uint8_t *b) {
    RECT r;
    set_rect(&r, (int16_t)get16(b), (int16_t)get16(b + 2), (int16_t)get16(b + 4),
             (int16_t)get16(b + 6));
    return r;
}

uint32_t winn_mci_avi_command(win_dll_call *c, uint16_t msg, uint32_t flags, uint32_t p) {
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    uint8_t b[28] = {0};
    if (p && !read_guest(c, p, b, 4)) return MCIERR_MISSING_PARAMETER;
    HWND cb = (HWND)(uintptr_t)get16(b);
    uint32_t v = 0, err = 0;
    if (!avi.frames && (msg == MCI_PLAY || msg == MCI_SEEK || msg == MCI_STEP || msg == MCI_RESUME ||
                        msg == MCI_WINDOW || msg == MCI_PUT || msg == MCI_WHERE || msg == MCI_UPDATE))
        return MCIERR_DEVICE_NOT_READY;     /* no file yet */
    switch (msg) {
    case MCI_LOAD: {
        char name[160];
        if (!(flags & MCI_LOAD_FILE) || !read_guest(c, p, b, 8) ||
            !text_guest(c, get32(b + 4), name, sizeof(name)))
            return MCIERR_MISSING_PARAMETER;
        err = load(name);
        break;
    }
    case MCI_CLOSE:
        winn_mci_avi_close();
        if (flags & MCI_NOTIFY) notify(cb, MCI_NOTIFY_SUCCESSFUL);
        return 0;
    case MCI_PLAY:
        if ((flags & (MCI_FROM | MCI_TO)) && !read_guest(c, p, b, 12)) return MCIERR_MISSING_PARAMETER;
        return play(flags, get32(b + 4), get32(b + 8), cb, (flags & MCI_DGV_PLAY_REPEAT) != 0);
    case MCI_STOP: halt(1); break;
    case MCI_PAUSE: do_pause(); break;
    case MCI_RESUME: return resume(cb);
    case MCI_SEEK:
        if (!(flags & (MCI_SEEK_TO_START | MCI_SEEK_TO_END)) && !read_guest(c, p, b, 8))
            return MCIERR_MISSING_PARAMETER;
        seek((flags & MCI_SEEK_TO_START) ? 0 : (flags & MCI_SEEK_TO_END) ? avi.frames
                                                                         : to_frames(get32(b + 4)));
        break;
    case MCI_STEP: {
        int32_t n = 1;
        if (flags & MCI_DGV_STEP_FRAMES) {
            if (!read_guest(c, p, b, 8)) return MCIERR_MISSING_PARAMETER;
            n = (int32_t)get32(b + 4);
        }
        step((flags & MCI_DGV_STEP_REVERSE) ? -n : n);
        break;
    }
    case MCI_STATUS:
        if (!(flags & MCI_STATUS_ITEM) || !read_guest(c, p, b, 12)) return MCIERR_MISSING_PARAMETER;
        err = status(get32(b + 8), flags, &v);
        if (!err && !write_dword(c, p + 4u, v)) err = MCIERR_MISSING_PARAMETER;
        break;
    case MCI_GETDEVCAPS:
        if (!(flags & MCI_GETDEVCAPS_ITEM) || !read_guest(c, p, b, 12)) return MCIERR_MISSING_PARAMETER;
        err = caps(get32(b + 8), &v);
        if (!err && !write_dword(c, p + 4u, v)) err = MCIERR_MISSING_PARAMETER;
        break;
    case MCI_SET:
        if (!read_guest(c, p, b, 16)) return MCIERR_MISSING_PARAMETER;
        if (flags & MCI_SET_TIME_FORMAT) {
            v = get32(b + 4);
            if (v != MCI_FORMAT_FRAMES && v != MCI_FORMAT_MILLISECONDS) return MCIERR_BAD_TIME_FORMAT;
            avi.time_format = v;
        }
        if (flags & MCI_SET_AUDIO) {
            if (flags & MCI_SET_OFF) { avi.audio_on = 0; audio_close(); }
            else if (flags & MCI_SET_ON) avi.audio_on = 1;
        }
        if (!(flags & (MCI_SET_TIME_FORMAT | MCI_SET_AUDIO | MCI_SET_VIDEO)))
            return MCIERR_UNSUPPORTED_FUNCTION;
        break;
    case MCI_INFO:
        if (!read_guest(c, p, b, 12)) return MCIERR_MISSING_PARAMETER;
        {
            char text[160];
            const char *s;
            if (flags & MCI_INFO_PRODUCT) s = "WINN Video for Windows";
            else if (flags & MCI_INFO_FILE) s = avi.filename;
            else if (flags & MCI_DGV_INFO_TEXT) {
                text[0] = 0;
                if (window()) GetWindowText(window(), text, sizeof(text));
                s = text;
            } else return MCIERR_UNSUPPORTED_FUNCTION;
            if (!write_text(c, get32(b + 4), get32(b + 8), s)) return MCIERR_MISSING_PARAMETER;
        }
        break;
    case MCI_WINDOW:
        if (!read_guest(c, p, b, 16)) return MCIERR_MISSING_PARAMETER;
        if (flags & MCI_DGV_WINDOW_HWND) set_window((HWND)(uintptr_t)get16(b + 4));
        if (flags & MCI_DGV_WINDOW_STATE) show((int16_t)get16(b + 8));
        if (flags & MCI_DGV_WINDOW_TEXT) {
            char text[128];
            if (!text_guest(c, get32(b + 12), text, sizeof(text))) return MCIERR_MISSING_PARAMETER;
            if (window()) SetWindowText(window(), text);
        }
        break;
    case MCI_PUT:
        if (!read_guest(c, p, b, 12)) return MCIERR_MISSING_PARAMETER;
        if (flags & MCI_DGV_PUT_DESTINATION) {
            if (flags & MCI_DGV_RECT) avi.dst = rect16(b + 4);
            else client(&avi.dst);
        } else if (flags & MCI_DGV_PUT_SOURCE) {
            if (flags & MCI_DGV_RECT) avi.src = rect16(b + 4);
            else set_rect(&avi.src, 0, 0, avi.width, avi.height);
            if (avi.src.left < 0 || avi.src.top < 0 || avi.src.right < 1 || avi.src.bottom < 1 ||
                avi.src.left + avi.src.right > avi.width || avi.src.top + avi.src.bottom > avi.height)
                set_rect(&avi.src, 0, 0, avi.width, avi.height);
        } else if (flags & MCI_DGV_PUT_WINDOW) {
            if ((flags & MCI_DGV_RECT) && window()) {
                RECT r = rect16(b + 4);
                MoveWindow(window(), r.left, r.top, r.right, r.bottom, TRUE);
            }
        } else return MCIERR_UNSUPPORTED_FUNCTION;
        if (!avi.playing) draw();
        break;
    case MCI_WHERE: {
        RECT r;
        if (flags & MCI_DGV_PUT_DESTINATION) r = avi.dst;
        else if (flags & MCI_DGV_PUT_SOURCE) r = avi.src;
        else if ((flags & MCI_DGV_PUT_WINDOW) && window()) {
            RECT w;
            GetWindowRect(window(), &w);
            set_rect(&r, w.left, w.top, w.right - w.left, w.bottom - w.top);
        } else return MCIERR_UNSUPPORTED_FUNCTION;
        uint8_t o[8];
        put16(o, (uint16_t)r.left); put16(o + 2, (uint16_t)r.top);
        put16(o + 4, (uint16_t)r.right); put16(o + 6, (uint16_t)r.bottom);
        if (!write_guest(c, p + 4u, o, 8)) return MCIERR_MISSING_PARAMETER;
        break;
    }
    case MCI_UPDATE:
        if (!read_guest(c, p, b, 14)) return MCIERR_MISSING_PARAMETER;
        if (flags & MCI_DGV_UPDATE_HDC) paint((HDC)(uintptr_t)get16(b + 12));
        else draw();
        break;
    case MCI_REALIZE: case MCI_CUE: case MCI_FREEZE: case MCI_UNFREEZE:
        break;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (!err && (flags & MCI_NOTIFY)) notify(cb, MCI_NOTIFY_SUCCESSFUL);
    return err;
}

/* ---------------------------------------------------------------- strings */

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
static int32_t number(const char *s) {
    int neg = *s == '-';
    uint32_t n = 0;
    if (neg) s++;
    for (; *s >= '0' && *s <= '9'; ++s) n = n * 10u + (unsigned)(*s - '0');
    return neg ? -(int32_t)n : (int32_t)n;
}
static int same(const char *a, const char *b) { return !lstrcmpi(a, b); }
static void put_result(char *result, unsigned cap, const char *s) {
    if (result && cap) lstrcpyn(result, s, (int)cap);
}
static void number_result(char *result, unsigned cap, uint32_t v) {
    char s[16];
    wsprintf(s, "%lu", (unsigned long)v);
    put_result(result, cap, s);
}
static int at_rect(const char **rest, RECT *r) {
    char w[32];
    int v[4];
    for (int i = 0; i < 4; i++) {
        next_word(rest, w, sizeof(w));
        if (!w[0]) return 0;
        v[i] = number(w);
    }
    set_rect(r, v[0], v[1], v[2], v[3]);
    return 1;
}

uint32_t winn_mci_avi_string(const char *op, const char *rest, char *result,
                             unsigned cap, HWND hwnd) {
    char arg[160], value[64];
    if (!owned()) return MCIERR_INVALID_DEVICE_ID;
    if (result && cap) result[0] = 0;
    if (same(op, "close")) { winn_mci_avi_close(); return 0; }
    if (same(op, "load")) {
        next_word(&rest, arg, sizeof(arg));
        return arg[0] ? load(arg) : MCIERR_MISSING_PARAMETER;
    }
    if (!avi.frames && (same(op, "play") || same(op, "seek") || same(op, "step") || same(op, "resume") ||
                        same(op, "window") || same(op, "put") || same(op, "where") || same(op, "update")))
        return MCIERR_DEVICE_NOT_READY;
    if (same(op, "stop")) { halt(1); return 0; }
    if (same(op, "pause")) { do_pause(); return 0; }
    if (same(op, "resume")) return resume(hwnd);
    if (same(op, "realize") || same(op, "cue") || same(op, "freeze") || same(op, "unfreeze"))
        return 0;
    if (same(op, "play")) {
        uint32_t flags = 0, from = 0, to = 0;
        int repeat = 0;
        while ((next_word(&rest, arg, sizeof(arg)), arg[0])) {
            if (same(arg, "from")) { next_word(&rest, value, sizeof(value)); from = (uint32_t)number(value); flags |= MCI_FROM; }
            else if (same(arg, "to")) { next_word(&rest, value, sizeof(value)); to = (uint32_t)number(value); flags |= MCI_TO; }
            else if (same(arg, "notify")) flags |= MCI_NOTIFY;
            else if (same(arg, "wait")) flags |= MCI_WAIT;
            else if (same(arg, "repeat")) repeat = 1;
        }
        return play(flags, from, to, hwnd, repeat);
    }
    if (same(op, "seek")) {
        next_word(&rest, arg, sizeof(arg));
        if (same(arg, "to")) next_word(&rest, arg, sizeof(arg));
        seek(same(arg, "start") ? 0 : same(arg, "end") ? avi.frames : to_frames((uint32_t)number(arg)));
        return 0;
    }
    if (same(op, "step")) {
        int32_t n = 1, reverse = 0;
        while ((next_word(&rest, arg, sizeof(arg)), arg[0])) {
            if (same(arg, "reverse")) reverse = 1;
            else if (same(arg, "by")) { next_word(&rest, value, sizeof(value)); n = number(value); }
        }
        step(reverse ? -n : n);
        return 0;
    }
    if (same(op, "set")) {
        while ((next_word(&rest, arg, sizeof(arg)), arg[0])) {
            if (same(arg, "time")) {
                next_word(&rest, arg, sizeof(arg));
                next_word(&rest, value, sizeof(value));
                if (same(value, "frames")) avi.time_format = MCI_FORMAT_FRAMES;
                else if (same(value, "milliseconds") || same(value, "ms"))
                    avi.time_format = MCI_FORMAT_MILLISECONDS;
                else return MCIERR_BAD_TIME_FORMAT;
            } else if (same(arg, "audio")) {
                next_word(&rest, value, sizeof(value));
                if (same(value, "all") || same(value, "left") || same(value, "right"))
                    next_word(&rest, value, sizeof(value));
                if (same(value, "off")) { avi.audio_on = 0; audio_close(); }
                else if (same(value, "on")) avi.audio_on = 1;
            } else if (same(arg, "video")) {
                next_word(&rest, value, sizeof(value));
            }
        }
        return 0;
    }
    if (same(op, "status")) {
        next_word(&rest, arg, sizeof(arg));
        next_word(&rest, value, sizeof(value));
        uint32_t v = 0, e;
        if (same(arg, "mode")) {
            put_result(result, cap, avi.mode == MCI_MODE_PLAY ? "playing" :
                                    avi.mode == MCI_MODE_PAUSE ? "paused" : "stopped");
            return 0;
        }
        if (same(arg, "time")) {
            put_result(result, cap, avi.time_format == MCI_FORMAT_FRAMES ? "frames" : "milliseconds");
            return 0;
        }
        if (same(arg, "length")) e = status(MCI_STATUS_LENGTH, 0, &v);
        else if (same(arg, "position")) e = status(MCI_STATUS_POSITION, 0, &v);
        else if (same(arg, "start")) { v = 0; e = 0; }
        else if (same(arg, "window")) e = status(MCI_DGV_STATUS_HWND, 0, &v);
        else if (same(arg, "frame")) e = status(MCI_DGV_STATUS_FRAME_RATE, 0, &v);
        else if (same(arg, "speed")) e = status(MCI_DGV_STATUS_SPEED, 0, &v);
        else if (same(arg, "ready") || same(arg, "media")) { put_result(result, cap, "true"); return 0; }
        else if (same(arg, "number") || same(arg, "current")) { v = 1; e = 0; }
        else return MCIERR_UNSUPPORTED_FUNCTION;
        if (!e) number_result(result, cap, v);
        return e;
    }
    if (same(op, "info")) {
        next_word(&rest, arg, sizeof(arg));
        if (same(arg, "product")) put_result(result, cap, "WINN Video for Windows");
        else if (same(arg, "file")) put_result(result, cap, avi.filename);
        else if (same(arg, "window")) {
            char text[128] = "";
            if (window()) GetWindowText(window(), text, sizeof(text));
            put_result(result, cap, text);
        } else return MCIERR_UNSUPPORTED_FUNCTION;
        return 0;
    }
    if (same(op, "window")) {
        while ((next_word(&rest, arg, sizeof(arg)), arg[0])) {
            if (same(arg, "handle")) {
                next_word(&rest, value, sizeof(value));
                set_window(same(value, "default") ? 0 : (HWND)(uintptr_t)(uint16_t)number(value));
            } else if (same(arg, "state")) {
                next_word(&rest, value, sizeof(value));
                show(same(value, "hide") ? SW_HIDE : same(value, "minimize") || same(value, "iconize") ? SW_MINIMIZE :
                     same(value, "maximize") ? SW_SHOWMAXIMIZED : same(value, "restore") ? SW_RESTORE :
                     same(value, "show") ? SW_SHOW : SW_SHOWNORMAL);
            } else if (same(arg, "text")) {
                next_word(&rest, value, sizeof(value));
                if (window()) SetWindowText(window(), value);
            }
        }
        return 0;
    }
    if (same(op, "put")) {
        next_word(&rest, arg, sizeof(arg));
        next_word(&rest, value, sizeof(value));
        RECT r;
        int have = same(value, "at") && at_rect(&rest, &r);
        if (same(arg, "destination")) { if (have) avi.dst = r; else client(&avi.dst); }
        else if (same(arg, "source")) { if (have) avi.src = r; else set_rect(&avi.src, 0, 0, avi.width, avi.height); }
        else if (same(arg, "window")) { if (have && window()) MoveWindow(window(), r.left, r.top, r.right, r.bottom, TRUE); }
        else return MCIERR_UNSUPPORTED_FUNCTION;
        if (!avi.playing) draw();
        return 0;
    }
    if (same(op, "where")) {
        next_word(&rest, arg, sizeof(arg));
        RECT r;
        if (same(arg, "destination")) r = avi.dst;
        else if (same(arg, "source")) r = avi.src;
        else if (same(arg, "window") && window()) {
            RECT w;
            GetWindowRect(window(), &w);
            set_rect(&r, w.left, w.top, w.right - w.left, w.bottom - w.top);
        } else return MCIERR_UNSUPPORTED_FUNCTION;
        char s[48];
        wsprintf(s, "%d %d %d %d", r.left, r.top, r.right, r.bottom);
        put_result(result, cap, s);
        return 0;
    }
    if (same(op, "update")) {
        next_word(&rest, arg, sizeof(arg));
        if (same(arg, "hdc")) {
            next_word(&rest, value, sizeof(value));
            paint((HDC)(uintptr_t)(uint16_t)number(value));
        } else draw();
        return 0;
    }
    return MCIERR_UNRECOGNIZED_COMMAND;
}

void winn_mci_avi_task_end(uint16_t task) {
    if (avi.opened && avi.task == task) winn_mci_avi_close();
}
void winn_mci_avi_term(void) {
    winn_mci_avi_close();
    if (wnd_class) { UnregisterClass("WinnAVIWnd", 0); wnd_class = 0; }
}
