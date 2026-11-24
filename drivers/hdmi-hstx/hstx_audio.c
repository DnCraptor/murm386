// HDMI audio for the HSTX output: the sample buffer and the data island of each line.
// The island plan and make_audio_packet() are from rh1tech/frank-quake2
// drivers/hdmi/psx_video.c and hstx_data_island_queue.c (public domain, see
// LICENSE.pico_hdmi); here the packets take four real samples from the buffer.
#include "hstx_audio.h"
#include "hstx_data_island.h"

#include <pico.h>

enum {
    ACR_N = 6272, // HDMI 1.4, table 7-2 (44.1 kHz)
    ACR_CTS_640 = 28000, // 25.2 MHz pixel clock
    AUDIO_CLOCK_UNIT = 6300, // divides the sample rate and both pixel clocks
    SAMPLES_PER_PACKET = 4,
    PACKETS_PER_BLOCK = 192 / SAMPLES_PER_PACKET, // an IEC 60958 block starts with the B flag
    BUFFER_SIZE = 256 // 5.8 ms
};

// One producer (boot tone, then the audio timer) and one consumer (HSTX IRQ).
static uint32_t buffer[BUFFER_SIZE]; // left | right << 16
static uint32_t buffer_head, buffer_tail; // free-running counts

bool __time_critical_func(hstx_audio_has_room)(void) {
    const uint32_t head = __atomic_load_n(&buffer_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&buffer_tail, __ATOMIC_ACQUIRE);
    return head - tail < BUFFER_SIZE;
}

void __time_critical_func(hstx_audio_write)(const int16_t left, const int16_t right) {
    const uint32_t head = __atomic_load_n(&buffer_head, __ATOMIC_RELAXED);
    buffer[head % BUFFER_SIZE] = (uint16_t)left | (uint32_t)(uint16_t)right << 16;
    __atomic_store_n(&buffer_head, head + 1, __ATOMIC_RELEASE);
}

typedef struct {
    hstx_data_island_t avi, audio_infoframe, acr, acr_vsync, null, null_vsync;
    // Packets with zero samples and lanes 1–2 cleared in the packet body.
    // Template 1 has the IEC 60958 B flag.
    uint32_t audio_template[2][HSTX_DI_WORDS];
} hstx_audio_timing_t;

static hstx_audio_timing_t audio_timings[2]; // negative sync / positive sync
static hstx_data_island_t audio_island;
static uint32_t packets_in_block;
static uint32_t sample_credit;

// The encoder takes inverted signal levels (its original negative-sync API).
static void encode_island(hstx_data_island_t *island, const hstx_di_packet_t *packet,
                          bool vsync, bool positive_sync) {
    hstx_encode_data_island(island, packet, vsync != positive_sync, !positive_sync);
}

void hstx_audio_init(void) {
    hstx_di_packet_t packet;
    const hstx_di_packet_t null_packet = {0};
    const hstx_di_sample_t zero[SAMPLES_PER_PACKET] = {0};
    for (unsigned svga = 0; svga < 2; ++svga) {
        hstx_audio_timing_t *audio = &audio_timings[svga];
        // CEA VIC 1 is 640x480. VBE 102h uses PC timing without a CEA VIC.
        hstx_di_packet_set_avi_infoframe(&packet, svga ? 0 : 1, 0);
        encode_island(&audio->avi, &packet, false, svga);
        hstx_di_packet_set_audio_infoframe(&packet, HSTX_AUDIO_SAMPLE_RATE, 2, 16);
        encode_island(&audio->audio_infoframe, &packet, true, svga);
        hstx_di_packet_set_acr(&packet, ACR_N, ACR_CTS_640 << svga);
        encode_island(&audio->acr, &packet, false, svga);
        encode_island(&audio->acr_vsync, &packet, true, svga);
        encode_island(&audio->null, &null_packet, false, svga);
        encode_island(&audio->null_vsync, &null_packet, true, svga);

        for (int b = 0; b < 2; b++) {
            hstx_data_island_t island;
            hstx_di_packet_set_audio_samples(&packet, zero, SAMPLES_PER_PACKET, b ? 0 : 1);
            encode_island(&island, &packet, false, svga);
            for (int i = 0; i < HSTX_DI_WORDS; i++)
                audio->audio_template[b][i] =
                    i >= 2 && i < 34 ? island.words[i] & 0x3ff : island.words[i];
        }
    }
}

static inline uint32_t parity8(uint32_t v) {
    v ^= v >> 4;
    return (0x6996u >> (v & 0xf)) & 1;
}

static void __time_critical_func(make_audio_packet)(const hstx_audio_timing_t *audio) {
    uint8_t sp[SAMPLES_PER_PACKET][8];
    const uint32_t head = __atomic_load_n(&buffer_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&buffer_tail, __ATOMIC_RELAXED);
    for (int k = 0; k < SAMPLES_PER_PACKET; k++) {
        // An empty buffer gives silence.
        const uint32_t sample = tail != head ? buffer[tail++ % BUFFER_SIZE] : 0;
        uint8_t *d = sp[k];
        d[0] = 0;
        d[1] = (uint8_t)sample;
        d[2] = (uint8_t)(sample >> 8);
        d[3] = 0;
        d[4] = (uint8_t)(sample >> 16);
        d[5] = (uint8_t)(sample >> 24);
        d[6] = (uint8_t)(parity8(d[1] ^ d[2]) << 3 | parity8(d[4] ^ d[5]) << 7);
        // The 16-bit stereo packet has zero bytes at d[0] and d[3].
        uint8_t v = hstx_bch_table[d[1]];
        v = hstx_bch_table[d[2] ^ v];
        v = hstx_bch_table[v];
        v = hstx_bch_table[d[4] ^ v];
        v = hstx_bch_table[d[5] ^ v];
        d[7] = hstx_bch_table[d[6] ^ v];
    }
    __atomic_store_n(&buffer_tail, tail, __ATOMIC_RELEASE);

    const uint32_t *t = audio->audio_template[packets_in_block == 0];
    if (++packets_in_block == PACKETS_PER_BLOCK) packets_in_block = 0;
    uint32_t *w = audio_island.words;
    w[0] = t[0];
    w[1] = t[1];
    w[34] = t[34];
    w[35] = t[35];
    for (int i = 0; i < 8; i++) {
        uint32_t v = sp[0][i] | sp[1][i] << 8 | sp[2][i] << 16 | (uint32_t)sp[3][i] << 24;
        uint32_t x = (v ^ (v >> 7)) & 0x00aa00aa;
        v = v ^ x ^ (x << 7);
        x = (v ^ (v >> 14)) & 0x0000cccc;
        v = v ^ x ^ (x << 14);
        const uint32_t *ti = t + 2 + i * 4;
        uint32_t *wi = w + 2 + i * 4;
        wi[0] = ti[0] | (uint32_t)hstx_ter_c4[v & 0xf] << 10 | (uint32_t)hstx_ter_c4[(v >> 8) & 0xf] << 20;
        wi[1] = ti[1] | (uint32_t)hstx_ter_c4[(v >> 16) & 0xf] << 10 | (uint32_t)hstx_ter_c4[(v >> 24) & 0xf] << 20;
        wi[2] = ti[2] | (uint32_t)hstx_ter_c4[(v >> 4) & 0xf] << 10 | (uint32_t)hstx_ter_c4[(v >> 12) & 0xf] << 20;
        wi[3] = ti[3] | (uint32_t)hstx_ter_c4[(v >> 20) & 0xf] << 10 | (uint32_t)hstx_ter_c4[(v >> 28) & 0xf] << 20;
    }
}

const uint32_t *__time_critical_func(hstx_audio_island)(
        unsigned line, const hstx_timing_t *timing) {
    const bool svga = timing->positive_sync;
    const hstx_audio_timing_t *audio = &audio_timings[svga];
    // Accumulate Fs * line duration without a divide in the IRQ. Both
    // pixel clocks are multiples of AUDIO_CLOCK_UNIT (8000 or 4000 units).
    const unsigned pixels = timing->h_front + timing->h_sync + timing->h_back + timing->width;
    const unsigned cost = (SAMPLES_PER_PACKET * (25200000 / AUDIO_CLOCK_UNIT)) << svga;
    sample_credit += pixels * (HSTX_AUDIO_SAMPLE_RATE / AUDIO_CLOCK_UNIT);
    if (line == 0) return audio->avi.words;
    if (line == timing->v_front) return audio->acr_vsync.words;
    if (line == timing->v_front + 1u) return audio->audio_infoframe.words;
    if (line >= timing->v_front && line < timing->v_front + timing->v_sync)
        return audio->null_vsync.words;
    if (line < hstx_active_start(timing) && line % 4 == 0) return audio->acr.words;
    if (sample_credit >= cost) {
        sample_credit -= cost;
        make_audio_packet(audio);
        return audio_island.words;
    }
    return audio->null.words;
}
