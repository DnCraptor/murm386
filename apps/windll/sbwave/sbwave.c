/*
 * sbwave.c - native Sound Blaster 16 wave output driver for WINN.
 * Uses the existing x86 emulator's SB16 (220h, IRQ 5, DMA 1) through
 * the native DOS I/O port API and conventional guest RAM. No RP2350
 * sound hardware is accessed directly.
 *
 * The DSP stays in auto-init mode for the entire stream.  The 8237 walks
 * an 8-KiB ring and the DSP raises an IRQ after each 4096 bytes of PCM.
 * The interrupt acknowledges a consumed half and makes it available for
 * the producer again; no 16-bit DMA cursor arithmetic or reissued B6/C6
 * commands are required at PCM block boundaries.  The SB16 emulates a
 * separate 4-KiB output FIFO, so a completed half is safe to reuse.
 * Never issue D9/DA between adjacent blocks: doing so stops DMA at every
 * packet and creates a periodic audio gap when the native task polls it.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <string.h>
#include "windows.h"
#include "windll.h"
#include "wave_driver.h"
#include "conio.h"
#include "dos_mem.h"
#include "sound_hw.h"

#define SB_BASE      0x220u
#define SB_IRQ       5u
#define SB_DMA       1u
#define SB_BUFSIZE   8192u              /* the ring */
#define SB_BLOCK     4096u              /* a block's most */
#define SB_BLOCKS    16u                /* blocks in it at the most */

static void *low_block;
static uint8_t *buffer;
static uint32_t physical;
static int opened, running, prepared;
static unsigned running_bits, running_rate, running_channels;
static uint32_t written, read_;       /* absolute byte positions; read_ = DMA consumed */
static uint32_t ends[SB_BLOCKS];     /* completion of each client buffer */
static unsigned blocks, first, played;  /* in it, the first's, played not yet told */

static int sb_present(void)
{
    return (sound_hw_mask() & SOUND_HW_SB16) != 0;
}

/* DSP port 22Ch: bit 7 indicates that its command FIFO is busy. */
static int dsp_byte(uint8_t byte)
{
    for (unsigned i = 0; i < 65536u; i++) {
        if (!(inp(SB_BASE + 0x0cu) & 0x80u)) {
            outp(SB_BASE + 0x0cu, byte);
            return 1;
        }
    }
    return 0;
}

static int dsp_reset(void)
{
    outp(SB_BASE + 6u, 1);
    for (volatile unsigned i = 0; i < 2048u; i++)
        ;
    outp(SB_BASE + 6u, 0);
    for (unsigned i = 0; i < 65536u; i++) {
        if (inp(SB_BASE + 0x0eu) & 0x80u)
            return inp(SB_BASE + 0x0au) == 0xaau;
    }
    return 0;
}

static int sb_open(void)
{
    if (!sb_present() || opened)
        return 0;
    /* 8 KiB alignment guarantees the 8-bit DMA transfer does not cross
     * a 64 KiB physical boundary. Keep the original DOS block to free. */
    low_block = dos_alloc_low(SB_BUFSIZE * 2u);
    if (!low_block)
        return 0;
    uint32_t addr = dos_ptr_linear(low_block);
    if (addr == UINT32_MAX) {
        dos_free_low(low_block);
        low_block = NULL;
        return 0;
    }
    physical = (addr + SB_BUFSIZE - 1u) & ~(SB_BUFSIZE - 1u);
    if (physical + SB_BUFSIZE > 0x100000u) {
        dos_free_low(low_block);
        low_block = NULL;
        return 0;
    }
    buffer = (uint8_t *)low_block + (physical - addr);
    if (!dsp_reset() || !dsp_byte(0xd1u)) {
        dos_free_low(low_block);
        low_block = NULL;
        buffer = NULL;
        return 0;
    }
    opened = 1;
    running = prepared = 0;
    running_bits = 0;
    blocks = first = played = 0;
    written = read_ = 0;
    return 1;
}

/* the SB16's interrupt (a block read) acknowledged */
static void ack_irq(void)
{
    outp(SB_BASE + 4u, 0x82u);
    uint8_t irq = inp(SB_BASE + 5u);
    if (irq & 1u) (void)inp(SB_BASE + 0x0eu);
    if (irq & 2u) (void)inp(SB_BASE + 0x0fu);
    if (irq & 3u) outp(0x20u, 0x20u);
}

static uint8_t quiet(void) { return running_bits == 16u ? 0 : 0x80; }

/* n bytes of the ring from at (wrapping round) */
static void ring_put(uint32_t at, const uint8_t *data, unsigned n, int silent)
{
    while (n) {
        unsigned off = at % SB_BUFSIZE, k = SB_BUFSIZE - off;
        if (k > n)
            k = n;
        if (silent)
            memset(buffer + off, quiet(), k);
        else {
            memcpy(buffer + off, data, k);
            data += k;               /* never do pointer arithmetic on NULL */
        }
        at += k;
        n -= k;
    }
}

static void sb_stop(void)
{
    if (!opened)
        return;
    if (running) {
        dsp_byte(running_bits == 16u ? 0xd9u : 0xdau);  /* exit auto-init */
        dsp_byte(running_bits == 16u ? 0xd5u : 0xd0u);  /* stop DMA */
    }
    outp(0x0au, 0x04u | SB_DMA);
    outp(0xd4u, 0x05u);            /* mask high-DMA channel 5 */
    ack_irq();
    /* The stream ran on past its samples: the SB16's output FIFO holds the
     * ring's silence it read ahead, of this stream's format. An auto-init
     * command does not flush it, so the next stream (another format: 8-bit
     * silence as 16-bit samples, a click) would play it first; a reset does. */
    if (running && dsp_reset())
        dsp_byte(0xd1u);           /* the speaker on again */
    running = prepared = 0;
    blocks = first = played = 0;
    written = read_ = 0;
}

static void sb_close(void)
{
    if (!opened)
        return;
    sb_stop();
    dsp_byte(0xd3u);
    dos_free_low(low_block);
    low_block = NULL;
    buffer = NULL;
    opened = 0;
}

/* The SB16 uses DMA1 (byte channel) for 8-bit samples and DMA5
 * (word channel, second 8237) for 16-bit samples, auto-initialised over
 * the ring. 8 KiB physical alignment prevents crossing either
 * controller's DMA page boundary. */
static void program_dma(unsigned bits)
{
    if (bits == 16u) {
        unsigned words = SB_BUFSIZE / 2u;
        outp(0xd4u, 0x05u);           /* mask DMA5 (controller channel 1) */
        outp(0xd8u, 0);               /* high controller flip-flop */
        outp(0xc4u, (uint8_t)(physical >> 1));
        outp(0xc4u, (uint8_t)(physical >> 9));
        outp(0x8bu, (uint8_t)(physical >> 16));
        outp(0xd8u, 0);
        outp(0xc6u, (uint8_t)(words - 1u));
        outp(0xc6u, (uint8_t)((words - 1u) >> 8));
        outp(0xd6u, 0x59u);           /* single, auto-init, memory-to-device, channel 1 */
        outp(0xd4u, 0x01u);           /* unmask DMA5 */
    } else {
        outp(0x0au, 0x04u | SB_DMA);
        outp(0x0cu, 0);
        outp(0x02u, (uint8_t)physical);
        outp(0x02u, (uint8_t)(physical >> 8));
        outp(0x83u, (uint8_t)(physical >> 16));
        outp(0x0cu, 0);
        outp(0x03u, (uint8_t)(SB_BUFSIZE - 1u));
        outp(0x03u, (uint8_t)((SB_BUFSIZE - 1u) >> 8));
        outp(0x0bu, 0x58u | SB_DMA);  /* single, auto-init, memory-to-device */
        outp(0x0au, SB_DMA);
    }
}

/* Start one auto-init stream.  The DSP's 4096-byte block IRQ, NOT the
 * DMA address, tells the producer which half of the 8192-byte ring is free.
 * This also avoids the half/whole-ring ambiguity of a polled 8237 cursor. */
static int start_stream(void)
{
    const uint8_t cmd = running_bits == 16u ? 0xb6u : 0xc6u;
    const uint8_t mode = (running_bits == 16u ? 0x10u : 0u) |
                         (running_channels == 2u ? 0x20u : 0u);
    const unsigned count = SB_BLOCK / (running_bits / 8u) - 1u;

    /* The ring already contains the first client buffer and PCM silence
     * elsewhere.  The DMA controller is configured but remains idle until
     * the DSP command starts; native pc_service performs the DMA transfers. */
    program_dma(running_bits);
    /* A partially accepted DSP command must also be stopped on failure. */
    running = 1;
    if (!dsp_byte(0x41u) || !dsp_byte((uint8_t)(running_rate >> 8)) ||
        !dsp_byte((uint8_t)running_rate) || !dsp_byte(cmd) || !dsp_byte(mode) ||
        !dsp_byte((uint8_t)count) || !dsp_byte((uint8_t)(count >> 8))) {
        sb_stop();
        return 0;
    }
    return 1;
}

/* An SB16 block IRQ means that the DMA has consumed one whole half-ring.
 * PCM already fetched is kept by the emulator's separate output FIFO.
 * A short application buffer can end within a half; complete it only
 * after DMA has copied that part.  Neither B6 nor D9 is reissued here. */
static void advance(void)
{
    if (!running)
        return;
    outp(SB_BASE + 4u, 0x82u);
    if (!(inp(SB_BASE + 5u) & (running_bits == 16u ? 2u : 1u)))
        return;
    ack_irq();
    ring_put(read_, NULL, SB_BLOCK, 1);
    read_ += SB_BLOCK;
    while (blocks && (int32_t)(read_ - ends[first]) >= 0) {
        first = (first + 1u) % SB_BLOCKS;
        --blocks;
        ++played;
    }
    /* A producer that missed an entire half-ring cannot catch up by
     * writing to memory already read by DMA.  Keep future writes aligned
     * with the current ring position; the gap is PCM silence. */
    if ((int32_t)(read_ - written) > 0)
        written = read_;
}

static int sb_submit(const uint8_t *data, unsigned n, unsigned rate,
                     unsigned bits, unsigned channels)
{
    unsigned frame_bytes = (bits / 8u) * channels;
    if (!opened || !data || !n || n > SB_BLOCK ||
        (bits != 8u && bits != 16u) || (channels != 1u && channels != 2u) ||
        n % frame_bytes || rate < 4000u || rate > 48000u)
        return 0;
    advance();
    if (prepared && (bits != running_bits || rate != running_rate ||
                     channels != running_channels))
        return 0;                     /* a format change requires closing the stream */
    if (!prepared) {
        running_bits = bits;
        running_rate = rate;
        running_channels = channels;
        written = read_ = 0;
        first = blocks = 0;
        ring_put(0, NULL, SB_BUFSIZE, 1);
        ack_irq();
        prepared = 1;
    }
    if (blocks == SB_BLOCKS || n > SB_BUFSIZE - (written - read_))
        return 0;
    ring_put(written, data, n, 0);
    written += n;
    ends[(first + blocks) % SB_BLOCKS] = written;
    ++blocks;
    /* Let a burst of submit() calls fill the DMA ring before the DSP can
     * prefetch the second half as silence.  For short last buffers,
     * finished() starts a partly filled ring on the next service tick. */
    if (!running && written - read_ == SB_BUFSIZE && !start_stream())
        return 0;
    return 1;
}

static int sb_room(void)
{
    if (!opened)
        return 0;
    advance();
    unsigned free = prepared ? SB_BUFSIZE - (written - read_) : SB_BUFSIZE;
    unsigned n = free / SB_BLOCK, left = SB_BLOCKS - blocks;
    return (int)(n < left ? n : left);
}

static int sb_finished(void)
{
    if (opened && !running && blocks)
        (void)start_stream();
    advance();
    unsigned n = played;
    played = 0;
    return (int)n;
}

static void sb_pause(int paused)
{
    if (opened && running)
        dsp_byte(paused ? (running_bits == 16u ? 0xd5u : 0xd0u)
                        : (running_bits == 16u ? 0xd6u : 0xd4u));
}

static const win_wave_driver device = {
    WIN_WAVE_DRIVER_VERSION, sizeof(win_wave_driver),
    sb_present, sb_open, sb_close, sb_submit, sb_finished,
    sb_stop, sb_pause, SB_BLOCK, sb_room,
};

static const win_wave_driver *WINAPI wave_entry(void) { return &device; }

static const win_dll_export exports[] = {
    { 1, 0, "WinWaveDriver", (const void *)wave_entry },
};

static void term(void) { sb_close(); }

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "SBWAVE",
    exports, sizeof(exports) / sizeof(exports[0]), NULL, NULL, term, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
