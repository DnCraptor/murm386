/*
 * QEMU Proxy for OPL2/3 emulation by MAME team
 *
 * Copyright (c) 2004-2005 Vassili Karpov (malc)
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

#include <pico.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "adlib.h"
#include "pc.h"
#include "emu8950/emu8950.h"

extern PC *pc;

/* __dmb() is a CMSIS intrinsic; pico.h should pull it in transitively,
 * but include cmsis_compiler.h explicitly as a fallback. */
#if defined(__has_include) && __has_include("cmsis_compiler.h")
#  include "cmsis_compiler.h"
#elif !defined(__dmb)
#  define __dmb()  __asm volatile ("dmb" ::: "memory")
#endif

#define ADLIB_DESC "Yamaha YM3812 (OPL2)"

/*
 * AdLib is asynchronous with respect to x86 execution.
 *
 * Guest OUT 388h only changes the address latch. OUT 389h publishes a compact
 * register/value command into an SPSC queue. Core1 owns the emu8950 state: its
 * non-IRQ service drains commands, calls OPL_writeReg() and refills the PCM
 * double-buffer. The 44.1 kHz audio IRQ on the same core only consumes already
 * rendered PCM, so FM rendering cannot extend the time-critical audio/video IRQ.
 *
 * PCM remains the existing double-buffer, now entirely serviced on core1:
 *   ready[2]  — non-IRQ core1 service publishes a filled buffer;
 *               audio IRQ/core1 clears it after consumption.
 *   play_buf  — buffer currently consumed on core1.
 *   read_pos  — sample index within play_buf.
 */
#define ADLIB_CMD_COUNT 256u
#define ADLIB_CMD_MASK  (ADLIB_CMD_COUNT - 1u)

typedef struct {
    uint8_t reg;
    uint8_t value;
} AdlibCommand;

struct AdlibState {
    uint32_t freq;
    uint16_t adlibregmem[5], adlib_register;
    uint8_t  adlibstatus;
    OPL     *opl;

    /*
     * SPSC command queue. Producer is guest I/O on core0; consumer is the
     * non-IRQ AdLib service on core1. Payload is written before cmd_head is
     * published, so core1 never observes a partially published command.
     */
    AdlibCommand cmd[ADLIB_CMD_COUNT];
    volatile uint16_t cmd_head;
    volatile uint16_t cmd_tail;
    uint32_t cmd_overflow_count;
    uint8_t  started;

    int16_t  buf[2][ADLIB_BATCH_SIZE];
    volatile uint8_t ready[2];  /* 1 = filled, not yet consumed on Core 1 */
    uint8_t  play_buf;          /* Core 1: which buf is being played */
    uint32_t read_pos;          /* Core 1: next sample index in play_buf */

    uint32_t underrun_count;
};

/*
 * OPL_calc_buffer_linear() produces 32-bit working samples.  Keep only a
 * small render tile here and convert into the long 16-bit playback queue.
 *
 * Keep only 64 working samples instead of two 1024-sample int32 queues.
 * This scratch buffer is ordinary SRAM; it is not in CORE0_STACK_EXT.
 */
#define ADLIB_RENDER_TILE 64u
static int32_t adlib_render_scratch[ADLIB_RENDER_TILE]
    __attribute__((aligned(4)));

static inline void adlib_queue_command(AdlibState *s,
                                       uint8_t reg, uint8_t value)
{
    uint16_t head = s->cmd_head;
    uint16_t next = (uint16_t)((head + 1u) & ADLIB_CMD_MASK);

    /*
     * OPL register writes are ordered device I/O and must never be dropped:
     * losing a B0..B8 key-off leaves a voice sounding indefinitely.  Core1 is
     * the independent consumer, so apply backpressure to guest OUT until one
     * queue slot is free instead of discarding an arbitrary command.
     */
    while (next == s->cmd_tail)
        ;
    __dmb();

    s->cmd[head].reg = reg;
    s->cmd[head].value = value;
    __dmb();
    s->cmd_head = next;
}

void adlib_write(void *opaque, uint32_t nport, uint32_t val)
{
    AdlibState *s = opaque;
    switch (nport) {
        case 0x388:
            s->adlib_register = val;
            break;
        case 0x389:
            if (s->adlib_register <= 4) {
                s->adlibregmem[s->adlib_register] = val;
                if (s->adlib_register == 4 && (val & 0x80)) {
                    s->adlibstatus = 0;
                    s->adlibregmem[4] = 0;
                }
            }
            adlib_queue_command(s, (uint8_t)s->adlib_register, (uint8_t)val);
            break;
    }
}

uint32_t adlib_read(void *opaque, uint32_t nport)
{
    AdlibState *s = opaque;
    switch (nport) {
        case 0x388:
        case 0x389:
            if (!s->adlibregmem[4])
                s->adlibstatus = 0;
            else
                s->adlibstatus = 0x80;
            s->adlibstatus = s->adlibstatus
                           + (s->adlibregmem[4] & 1) * 0x40
                           + (s->adlibregmem[4] & 2) * 0x10;
            return s->adlibstatus;
    }
    return 0xFF;
}

AdlibState *adlib_new()
{
    AdlibState *s = malloc(sizeof(AdlibState));
    if (!s)
        return NULL;

    memset(s, 0, sizeof(AdlibState));
    s->freq     = SOUND_FREQUENCY;
    s->play_buf = 0;
    s->read_pos = 0;
    s->opl = OPL_new(3579552, s->freq);
    if (!s->opl) {
        free(s);
        return NULL;
    }

    return s;
}

/* Called by the 44.1 kHz audio IRQ on core1; consume only pre-rendered PCM. */
int16_t __not_in_flash_func(adlib_getsample)(AdlibState *s) {
    if (!s->opl) return 0;

    if (!s->ready[s->play_buf]) {
        s->underrun_count++;
        return 0;
    }

    int16_t sample = s->buf[s->play_buf][s->read_pos++];

    if (s->read_pos >= ADLIB_BATCH_SIZE) {
        /* Mark this buffer as consumed, switch to the other one. */
        s->ready[s->play_buf] = 0;
        s->play_buf ^= 1;
        s->read_pos = 0;
    }

    return sample;
}

/*
 * Core1 1 kHz timer service.
 *
 * Drain every pending register command first, then refill every free PCM
 * buffer. The two buffers are the latency reserve between this opportunistic
 * non-IRQ producer and the fixed-rate 44.1 kHz IRQ consumer; leaving one free
 * 128-sample batch.  This runs from a private, deliberately low-priority
 * hardware-alarm IRQ; audio/video IRQs on core1 may preempt it.
 */
void __not_in_flash_func(adlib_service)(AdlibState *s)
{
    if (!s->opl)
        return;

    uint16_t tail = s->cmd_tail;
    uint16_t head = s->cmd_head;

    /*
     * Producer publishes the payload before cmd_head.  Pair its release DMB
     * with an acquire DMB before reading any slot covered by the observed head.
     */
    __dmb();

    while (tail != head) {
        AdlibCommand command = s->cmd[tail];
        tail = (uint16_t)((tail + 1u) & ADLIB_CMD_MASK);
        OPL_writeReg(s->opl, command.reg, command.value);
        s->started = 1;
    }

    /*
     * Do not publish a freed slot until every payload read from it has
     * completed; otherwise core0 may reuse the slot while core1 still reads it.
     */
    __dmb();
    s->cmd_tail = tail;

    /*
     * Before the first guest command there is no FM state worth advancing.
     * This avoids spending timer time on an enabled-but-unused AdLib device.
     */
    if (!s->started)
        return;

    for (int i = 0; i < 2; ++i) {
        uint8_t fill_buf = (uint8_t)((s->play_buf + i) & 1u);
        if (s->ready[fill_buf])
            continue;

        for (uint32_t pos = 0; pos < ADLIB_BATCH_SIZE;
             pos += ADLIB_RENDER_TILE) {
            uint32_t count = ADLIB_BATCH_SIZE - pos;
            if (count > ADLIB_RENDER_TILE)
                count = ADLIB_RENDER_TILE;

            OPL_calc_buffer_linear(s->opl, adlib_render_scratch, count);

            for (uint32_t j = 0; j < count; ++j) {
                int32_t sample = adlib_render_scratch[j] * 4; /// TODO: ensure x4
                if (sample > 32767)
                    sample = 32767;
                else if (sample < -32768)
                    sample = -32768;
                s->buf[fill_buf][pos + j] = (int16_t)sample;
            }
        }

        __dmb();
        s->ready[fill_buf] = 1;
    }
}

bool __not_in_flash_func(adlib_timer_callback)(repeating_timer_t *rt)
{
    AdlibState *s = (AdlibState *)rt->user_data;
    adlib_service(s);
    return true;
}

uint32_t adlib_underruns(AdlibState *s) {
    uint32_t u = s->underrun_count;
    s->underrun_count = 0;
    return u;
}
