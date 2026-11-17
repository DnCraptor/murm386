/*
 * QEMU PC speaker emulation
 *
 * Copyright (c) 2006 Joachim Henke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "pcspk.h"
#include <stdlib.h>
#include <string.h>
#include <pico.h>

#if defined(BUILD_ESP32) || defined(RP2350_BUILD)
void *pcmalloc(long size);
#else
#define pcmalloc malloc
#endif

#define PCSPK_SAMPLE_RATE 44100
#define PCSPK_MAX_FREQ (PCSPK_SAMPLE_RATE >> 1)
#define PCSPK_MIN_COUNT ((PIT_FREQ + PCSPK_MAX_FREQ - 1) / PCSPK_MAX_FREQ)

struct PCSpkState {
    PITState *pit;
    int data_on;
    int dummy_refresh_clock;
};

uint32_t pcspk_ioport_read(void *opaque)
{
    PCSpkState *s = opaque;
    int out;

    s->dummy_refresh_clock ^= (1 << 4);
    out = pit_get_out(s->pit, 2) << 5;

    return pit_get_gate(s->pit, 2) | (s->data_on << 1) | s->dummy_refresh_clock | out;
}

void pcspk_ioport_write(void *opaque, uint32_t val)
{
    PCSpkState *s = opaque;
    s->data_on = (val >> 1) & 1;
    pit_set_gate(s->pit, 2, val & 1);
}

PCSpkState *pcspk_init(PITState *pit)
{
    PCSpkState *s = pcmalloc(sizeof(PCSpkState));
    memset(s, 0, sizeof(PCSpkState));

    s->pit = pit;
    return s;
}

int16_t __not_in_flash_func(pcspk_sample)(PCSpkState *s)
{
    if (!s || !s->data_on)
        return 0;

    if (!pit_get_gate(s->pit, 2))
        return 1;  /* Direct speaker drive via port 61h bit 1. */

    if (pit_get_mode(s->pit, 2) != 3)
        return 0;

    uint32_t count = pit_get_initial_count(s->pit, 2);
    if (count < PCSPK_MIN_COUNT)
        return 0;

    /* частота PIT */
    uint32_t freq = PIT_FREQ / count;

    if (freq == 0)
        return 0;

    /* фаза */
    static uint32_t phase = 0;

    /* phase increment в fixed-point 32.32 */
    uint64_t inc = ((uint64_t)freq << 32) / PCSPK_SAMPLE_RATE;
    phase += inc;

    /* квадратная волна */
    return (phase & 0x80000000) ? 1 : -1;
}
