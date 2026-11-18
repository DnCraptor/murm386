#pragma once
// HDMI audio for the HSTX output: 44.1 kHz 16-bit stereo.

#include <stdbool.h>
#include <stdint.h>
#include "hstx_packet.h"

enum { HSTX_AUDIO_SAMPLE_RATE = 44100 };

// Single producer. Write a sample only when the queue has room.
bool hstx_audio_has_room(void);
void hstx_audio_write(int16_t left, int16_t right);

// For hstx_video.c.
void hstx_audio_init(void);
const uint32_t *hstx_audio_island(unsigned line, const hstx_timing_t *timing);
