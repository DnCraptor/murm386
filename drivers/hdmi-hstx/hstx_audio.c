// HDMI audio for the HSTX output: the sample buffer and the data island of each line.
// The island plan and make_audio_packet() are from rh1tech/frank-quake2
// drivers/hdmi/psx_video.c and hstx_data_island_queue.c (public domain, see
// LICENSE.pico_hdmi); here the packets take four real samples from the buffer.
#include "hstx_audio.h"
#include "hstx_data_island.h"
#include "hstx_video.h"

#include <pico.h>

enum {
    ACR_N = 6272, // 44.1 kHz at the 25.2 MHz pixel clock (HDMI 1.4, table 7-2)
    ACR_CTS = 28000,
    SAMPLES_PER_FRAME = HSTX_AUDIO_SAMPLE_RATE / 60, // exact: 735
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

static hstx_data_island_t avi_island, audio_infoframe_island, acr_island, acr_vsync_island, audio_island;
static hstx_data_island_t null_island;
// The audio packet with zero samples, lanes 1 and 2 cleared: [1] has the B flag.
static uint32_t audio_template[2][HSTX_DI_WORDS];
static uint32_t packets_in_block;
static uint32_t sample_credit; // in 1/V_TOTAL samples

void hstx_audio_init(void) {
    hstx_di_packet_t packet;
    hstx_di_packet_set_avi_infoframe(&packet, 1, 0); // VIC 1: 640x480
    hstx_encode_data_island(&avi_island, &packet, false, true);
    hstx_di_packet_set_audio_infoframe(&packet, HSTX_AUDIO_SAMPLE_RATE, 2, 16);
    hstx_encode_data_island(&audio_infoframe_island, &packet, true, true);
    hstx_di_packet_set_acr(&packet, ACR_N, ACR_CTS);
    hstx_encode_data_island(&acr_island, &packet, false, true);
    hstx_encode_data_island(&acr_vsync_island, &packet, true, true);
    const hstx_di_packet_t null_packet = {0}; // its parity bytes are zero too
    hstx_encode_data_island(&null_island, &null_packet, false, true);

    for (int b = 0; b < 2; b++) {
        const hstx_di_sample_t zero[SAMPLES_PER_PACKET] = {0};
        hstx_data_island_t island;
        hstx_di_packet_set_audio_samples(&packet, zero, SAMPLES_PER_PACKET, b ? 0 : 1);
        hstx_encode_data_island(&island, &packet, false, true);
        for (int i = 0; i < HSTX_DI_WORDS; i++)
            audio_template[b][i] = i >= 2 && i < 34 ? island.words[i] & 0x3ff : island.words[i];
    }
}

static inline uint32_t parity8(uint32_t v) {
    v ^= v >> 4;
    return (0x6996u >> (v & 0xf)) & 1;
}

static void __time_critical_func(make_audio_packet)(void) {
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
        uint8_t v = hstx_bch_table[0];
        for (int i = 1; i < 7; i++) v = hstx_bch_table[d[i] ^ v];
        d[7] = v;
    }
    __atomic_store_n(&buffer_tail, tail, __ATOMIC_RELEASE);

    const uint32_t *t = audio_template[packets_in_block == 0];
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

const uint32_t *__time_critical_func(hstx_audio_island)(const unsigned line) {
    sample_credit += SAMPLES_PER_FRAME; // a frame has V_TOTAL lines
    if (line == 0) return avi_island.words;
    if (line == V_FRONT) return acr_vsync_island.words;
    if (line == V_FRONT + 1) return audio_infoframe_island.words;
    if (line >= V_FRONT + V_SYNC && line < V_ACTIVE_START && line % 4 == 0) return acr_island.words;
    if (sample_credit >= SAMPLES_PER_PACKET * V_TOTAL) {
        sample_credit -= SAMPLES_PER_PACKET * V_TOTAL;
        make_audio_packet();
        return audio_island.words;
    }
    return null_island.words;
}
