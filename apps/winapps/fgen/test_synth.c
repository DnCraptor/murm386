/* Host test: cc -std=c11 -Wall -Wextra -Werror -O2 synth.c test_synth.c -o test_synth */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "synth.h"

static int16_t pcm16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int main(void)
{
    fgen_synth s;
    fgen_synth_init(&s);
    assert(s.step != 0 && s.gain[0] == 32767);
    assert(FGEN_BLOCK % 4u == 0);
    uint8_t buf[FGEN_BLOCK];
    s.waveform[0] = FGEN_SQUARE;
    s.waveform[1] = FGEN_SAW;
    s.gain[0] = 32767;
    s.gain[1] = 0;
    fgen_synth_render(&s, buf, FGEN_BLOCK / 4u);
    assert(pcm16(buf) > 32000);
    for (unsigned i = 0; i < FGEN_BLOCK; i += 4)
        assert(pcm16(buf + i + 2) == 0);

    fgen_synth_init(&s);
    s.waveform[0] = s.waveform[1] = FGEN_SINE;
    s.phase = 0;
    fgen_synth_render(&s, buf, 1);
    assert(pcm16(buf) == 0 && pcm16(buf + 2) == 0);
    s.phase = 0x40000000u;
    fgen_synth_render(&s, buf, 1);
    assert(pcm16(buf) > 32000);
    s.phase = 0xc0000000u;
    fgen_synth_render(&s, buf, 1);
    assert(pcm16(buf) < -32000);

    s.waveform[0] = s.waveform[1] = FGEN_PULSE;
    s.phase = 0;
    fgen_synth_render(&s, buf, 1);
    assert(pcm16(buf) > 32000);
    s.phase = 0x20000000u;
    fgen_synth_render(&s, buf, 1);
    assert(pcm16(buf) < -32000);

    fgen_synth_frequency(&s, 1000);
    assert(s.step > 90000000u && s.step < 100000000u);
    fgen_synth_frequency(&s, 20001);
    assert(s.step == (uint32_t)(((uint64_t)20000 << 32) / 44100));

    puts("fgen synth: PASS");
    return 0;
}
