/* ARM-side PCM synthesis: fixed phase accumulator, no per-sample divide,
 * no floating-point math, and no guest x86 execution. */
#include "synth.h"
#include <string.h>

static const int16_t sine256[256] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

void fgen_synth_init(fgen_synth *s)
{
    memset(s, 0, sizeof(*s));
    s->waveform[0] = s->waveform[1] = FGEN_SINE;
    s->gain[0] = s->gain[1] = 32767u;
    fgen_synth_frequency(s, 440u);
}

void fgen_synth_frequency(fgen_synth *s, unsigned hz)
{
    if (hz > 20000u) hz = 20000u;
    /* Calculated once when the frequency changes, never in the PCM loop. */
    s->step = (uint32_t)(((uint64_t)hz << 32) / FGEN_RATE);
}

static int32_t value(uint32_t phase, unsigned wave)
{
    switch (wave) {
    case FGEN_SQUARE:
        return phase < 0x80000000u ? 32767 : -32767;
    case FGEN_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case FGEN_PULSE:
        /* Ten percent duty cycle, independent of the common frequency. */
        return phase < 0x1999999au ? 32767 : -32767;
    case FGEN_SINE:
    default: {
        unsigned index = phase >> 24;
        unsigned frac = (phase >> 8) & 0xffffu;
        int32_t a = sine256[index], b = sine256[(index + 1u) & 255u];
        return a + ((b - a) * (int32_t)frac >> 16);
    }
    }
}

void fgen_synth_render(fgen_synth *s, uint8_t *pcm, unsigned frames)
{
    uint32_t phase = s->phase;
    for (unsigned i = 0; i < frames; ++i) {
        for (unsigned ch = 0; ch < 2; ++ch) {
            int32_t sample = value(phase, s->waveform[ch]);
            /* Product fits signed 32-bit: 32768 * 32767. */
            sample = (sample * s->gain[ch]) >> 15;
            uint16_t packed = (uint16_t)(int16_t)sample;
            pcm[4u * i + 2u * ch] = (uint8_t)packed;
            pcm[4u * i + 2u * ch + 1u] = (uint8_t)(packed >> 8);
        }
        phase += s->step;
    }
    s->phase = phase;
}
