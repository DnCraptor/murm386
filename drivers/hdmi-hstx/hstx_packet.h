#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(HDMI_SOUND)
#include "hstx_data_island.h"
#endif

enum {
    HSTX_PACKET_MAX_PIXELS = 800,
    HSTX_VGA_PIXELS = 640,
#if defined(HDMI_SOUND)
    HSTX_PACKET_ISLAND = 7, // commands[] index of the data island
    HSTX_PACKET_HEADER_WORDS = 56,
#else
    HSTX_PACKET_HEADER_WORDS = 9,
#endif
    HSTX_PACKET_PIXEL_WORDS = HSTX_PACKET_MAX_PIXELS / 2,
    HSTX_PACKET_WORDS = HSTX_PACKET_HEADER_WORDS + HSTX_PACKET_PIXEL_WORDS,
    HSTX_CMD_RAW = 0x0000,
    HSTX_CMD_RAW_REPEAT = 0x1000,
    HSTX_CMD_TMDS = 0x2000,
    HSTX_CMD_NOP = 0xf000,
    HSTX_SYNC_V0_H0 = 0x354u | (0x354u << 10) | (0x354u << 20),
    HSTX_SYNC_V0_H1 = 0x0abu | (0x354u << 10) | (0x354u << 20),
    HSTX_SYNC_V1_H0 = 0x154u | (0x354u << 10) | (0x354u << 20),
    HSTX_SYNC_V1_H1 = 0x2abu | (0x354u << 10) | (0x354u << 20)
};

typedef struct {
    uint16_t width, height;
    uint16_t h_front, h_sync, h_back;
    uint8_t v_front, v_sync, v_back;
    bool positive_sync;
    uint8_t clock_div;
} hstx_timing_t;

static inline unsigned hstx_active_start(const hstx_timing_t *timing) {
    return timing->v_front + timing->v_sync + timing->v_back;
}

// Within one mode, every line has the same transfer length and FIFO consumption schedule.
// Active payload words hold two BGR555 pixels. Blank payload words hold a
// 30-bit sync symbol consumed twice (RAW_N_SHIFTS=2, RAW_SHIFT=0); with
// HDMI_SOUND once (RAW_N_SHIFTS=1), as each data island word is one symbol.
typedef struct {
    uint32_t commands[HSTX_PACKET_HEADER_WORDS];
    union {
        uint16_t pixels[HSTX_PACKET_MAX_PIXELS];
        uint32_t pairs[HSTX_PACKET_PIXEL_WORDS];
        uint32_t vga[HSTX_VGA_PIXELS];
    } payload;
} hstx_video_packet_t;

_Static_assert(offsetof(hstx_video_packet_t, payload) == HSTX_PACKET_HEADER_WORDS * sizeof(uint32_t),
               "HSTX payload must immediately follow its header");
_Static_assert(sizeof(hstx_video_packet_t) == (HSTX_PACKET_HEADER_WORDS + HSTX_VGA_PIXELS) * sizeof(uint32_t),
               "HSTX packet must be contiguous DMA words");

static inline uint32_t hstx_sync_symbol(bool h, bool v) {
    if (v) return h ? HSTX_SYNC_V1_H1 : HSTX_SYNC_V1_H0;
    return h ? HSTX_SYNC_V0_H1 : HSTX_SYNC_V0_H0;
}

// VGA output: RGB222 in bits 0..5 of each byte, HSYNC in bit 6 and VSYNC in bit 7 (the pins
// invert the syncs). A word is four time steps of a pixel, so each channel has 13 levels.
// A narrow frame (320 words a line) has RAW_N_SHIFTS=2: each word is two pixels.
enum {
    HSTX_VGA_HEADER_WORDS = 7,
    HSTX_VGA_BLANK_STEP = 4, // pixels in each RAW_REPEAT at the end of a blank line
    HSTX_VGA_BLANK_WORDS = 6 + 2 * HSTX_VGA_PIXELS / HSTX_VGA_BLANK_STEP
};
#define HSTX_VGA_HSYNC 0x40404040u
#define HSTX_VGA_VSYNC 0x80808080u

// The VGA header ends commands[], so the pixels follow it. RAW counts pixels.
static inline void hstx_vga_packet_init(hstx_video_packet_t *packet,
                                        const hstx_timing_t *timing) {
    uint32_t *header = packet->commands + HSTX_PACKET_HEADER_WORDS - HSTX_VGA_HEADER_WORDS;
    header[0] = HSTX_CMD_RAW_REPEAT | timing->h_front;
    header[1] = 0;
    header[2] = HSTX_CMD_RAW_REPEAT | timing->h_sync;
    header[3] = HSTX_VGA_HSYNC;
    header[4] = HSTX_CMD_RAW_REPEAT | timing->h_back;
    header[5] = 0;
    header[6] = HSTX_CMD_RAW | HSTX_VGA_PIXELS;
}

// A VGA blank line is RAW_REPEAT only, so a RAW_N_SHIFTS change cannot change its length.
// The short repeats at its end keep its DMA completion near the line end, as on an active line.
static inline void hstx_vga_blank_init(uint32_t words[HSTX_VGA_BLANK_WORDS],
                                       const hstx_timing_t *timing, bool vsync) {
    const uint32_t level = vsync ? HSTX_VGA_VSYNC : 0u;
    *words++ = HSTX_CMD_RAW_REPEAT | timing->h_front;
    *words++ = level;
    *words++ = HSTX_CMD_RAW_REPEAT | timing->h_sync;
    *words++ = level | HSTX_VGA_HSYNC;
    *words++ = HSTX_CMD_RAW_REPEAT | timing->h_back;
    *words++ = level;
    for (unsigned x = 0; x < HSTX_VGA_PIXELS; x += HSTX_VGA_BLANK_STEP) {
        *words++ = HSTX_CMD_RAW_REPEAT | HSTX_VGA_BLANK_STEP;
        *words++ = level;
    }
}

#if defined(HDMI_SOUND)
// HDMI 1.4 5.2: a data island in the sync pulse, and a video preamble and
// guard band before the pixels. The island words are left for hstx_video.c,
// which writes them for each line.
static inline __attribute__((always_inline)) void hstx_packet_init(hstx_video_packet_t *packet,
                                    const hstx_timing_t *timing, bool active, bool vsync) {
    enum {
        CTRL_00 = 0x354u,
        CTRL_01 = 0x0abu,
        ISLAND_PREAMBLE = CTRL_01 << 10 | CTRL_01 << 20,
        VIDEO_PREAMBLE = CTRL_01 << 10 | CTRL_00 << 20,
        VIDEO_GUARD_BAND = 0x2ccu | 0x133u << 10 | 0x2ccu << 20,
        VIDEO_PREAMBLE_SYMBOLS = 8,
        VIDEO_GUARD_BAND_SYMBOLS = 2
    };
    const bool h = timing->positive_sync;
    const bool v = vsync == timing->positive_sync;
    const uint32_t idle = hstx_sync_symbol(!h, v);
    const uint32_t sync = hstx_sync_symbol(h, v);
    const unsigned pixel_words = timing->width / 2u;
    uint32_t *command = packet->commands;
    *command++ = HSTX_CMD_RAW_REPEAT | timing->h_front;
    *command++ = idle;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | HSTX_DI_PREAMBLE;
    *command++ = (sync & 0x3ffu) | ISLAND_PREAMBLE;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW | HSTX_DI_WORDS;
    command += HSTX_DI_WORDS;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | (timing->h_sync - HSTX_DI_PREAMBLE - HSTX_DI_WORDS);
    *command++ = sync;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | (timing->h_back - VIDEO_PREAMBLE_SYMBOLS - VIDEO_GUARD_BAND_SYMBOLS);
    *command++ = idle;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | VIDEO_PREAMBLE_SYMBOLS;
    *command++ = active ? (idle & 0x3ffu) | VIDEO_PREAMBLE : idle;
    *command++ = HSTX_CMD_NOP;
    if (active) {
        *command++ = HSTX_CMD_RAW_REPEAT | VIDEO_GUARD_BAND_SYMBOLS;
        *command++ = VIDEO_GUARD_BAND;
        *command++ = HSTX_CMD_TMDS | timing->width;
    } else {
        // One symbol per payload word: the rest of the line comes first.
        *command++ = HSTX_CMD_RAW_REPEAT | (VIDEO_GUARD_BAND_SYMBOLS + timing->width - pixel_words);
        *command++ = idle;
        *command++ = HSTX_CMD_RAW | pixel_words;
    }
    for (unsigned i = 0; i < pixel_words; ++i) {
        packet->payload.pairs[i] = active ? 0u : idle;
    }
}

static inline void hstx_packet_set_island(hstx_video_packet_t *packet, const uint32_t *island) {
    for (unsigned i = 0; i < HSTX_DI_WORDS; ++i) {
        packet->commands[HSTX_PACKET_ISLAND + i] = island[i];
    }
}
#else
static inline __attribute__((always_inline)) void hstx_packet_init(hstx_video_packet_t *packet,
                                    const hstx_timing_t *timing, bool active, bool vsync) {
    const bool h = timing->positive_sync;
    const bool v = vsync == timing->positive_sync;
    const uint32_t idle = hstx_sync_symbol(!h, v);
    const uint32_t sync = hstx_sync_symbol(h, v);
    const unsigned pixel_words = timing->width / 2u;
    packet->commands[0] = HSTX_CMD_RAW_REPEAT | timing->h_front;
    packet->commands[1] = idle;
    packet->commands[2] = HSTX_CMD_NOP;
    packet->commands[3] = HSTX_CMD_RAW_REPEAT | timing->h_sync;
    packet->commands[4] = sync;
    packet->commands[5] = HSTX_CMD_NOP;
    packet->commands[6] = HSTX_CMD_RAW_REPEAT | timing->h_back;
    packet->commands[7] = idle;
    packet->commands[8] = (active ? HSTX_CMD_TMDS : HSTX_CMD_RAW) | timing->width;
    for (unsigned i = 0; i < pixel_words; ++i) {
        packet->payload.pairs[i] = active ? 0u : idle;
    }
}
#endif

// Repeated rows keep their complete packet. Rotate on actual writes so every
// return, including a repeated row, retains at least four callback lifetimes.
static inline unsigned hstx_packet_next_slot(unsigned slot) {
    return (slot + 1u) & 3u;
}
