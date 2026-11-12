/*
 * sbwave.c - native Sound Blaster 16 wave output driver for WINN.
 * Uses the existing x86 emulator's SB16 (220h, IRQ 5, DMA 1) through
 * the native DOS I/O port API and conventional guest RAM. No RP2350
 * sound hardware is accessed directly.
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
#define SB_BUFSIZE   8192u

static void *low_block;
static uint8_t *buffer;
static uint32_t physical;
static int opened, running;
static unsigned running_bits;

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
    running = 0;
    running_bits = 0;
    return 1;
}

static void sb_stop(void)
{
    if (!opened)
        return;
    dsp_byte(running_bits == 16u ? 0xd5u : 0xd0u); /* halt correct channel */
    outp(0x0au, 0x04u | SB_DMA);
    outp(0xd4u, 0x05u);            /* mask high-DMA channel 5 */
    outp(SB_BASE + 4u, 0x82u);
    uint8_t irq = inp(SB_BASE + 5u);
    if (irq & 1u) (void)inp(SB_BASE + 0x0eu);
    if (irq & 2u) (void)inp(SB_BASE + 0x0fu);
    if (irq & 3u) outp(0x20u, 0x20u);
    running = 0;
    running_bits = 0;
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
 * (word channel, second 8237) for 16-bit samples. 8 KiB physical
 * alignment prevents crossing either controller's DMA page boundary. */
static void program_dma(unsigned n, unsigned bits)
{
    if (bits == 16u) {
        unsigned words = n / 2u;
        outp(0xd4u, 0x05u);           /* mask DMA5 (controller channel 1) */
        outp(0xd8u, 0);               /* high controller flip-flop */
        outp(0xc4u, (uint8_t)(physical >> 1));
        outp(0xc4u, (uint8_t)(physical >> 9));
        outp(0x8bu, (uint8_t)(physical >> 16));
        outp(0xd8u, 0);
        outp(0xc6u, (uint8_t)(words - 1u));
        outp(0xc6u, (uint8_t)((words - 1u) >> 8));
        outp(0xd6u, 0x49u);           /* single, memory-to-device, channel 1 */
        outp(0xd4u, 0x01u);           /* unmask DMA5 */
    } else {
        outp(0x0au, 0x04u | SB_DMA);
        outp(0x0cu, 0);
        outp(0x02u, (uint8_t)physical);
        outp(0x02u, (uint8_t)(physical >> 8));
        outp(0x83u, (uint8_t)(physical >> 16));
        outp(0x0cu, 0);
        outp(0x03u, (uint8_t)(n - 1u));
        outp(0x03u, (uint8_t)((n - 1u) >> 8));
        outp(0x0bu, 0x48u | SB_DMA);
        outp(0x0au, SB_DMA);
    }
}

static int sb_submit(const uint8_t *data, unsigned n, unsigned rate,
                     unsigned bits, unsigned channels)
{
    unsigned frame_bytes = (bits / 8u) * channels;
    if (!opened || running || !data || !n || n > SB_BUFSIZE ||
        (bits != 8u && bits != 16u) || (channels != 1u && channels != 2u) ||
        n % frame_bytes || rate < 4000u || rate > 48000u)
        return 0;
    memcpy(buffer, data, n);
    /* Clear the previous block's IRQ before arming DMA. */
    outp(SB_BASE + 4u, 0x82u);
    uint8_t irq = inp(SB_BASE + 5u);
    if (irq & 1u) (void)inp(SB_BASE + 0x0eu);
    if (irq & 2u) (void)inp(SB_BASE + 0x0fu);
    if (irq & 3u) outp(0x20u, 0x20u);
    program_dma(n, bits);
    /* DSP B0h: signed 16-bit; C0h: unsigned 8-bit. Bit 5 of mode
     * enables stereo. The length register counts frames (not bytes). */
    unsigned count = n / frame_bytes - 1u;
    uint8_t cmd = bits == 16u ? 0xb0u : 0xc0u;
    uint8_t mode = (bits == 16u ? 0x10u : 0u) | (channels == 2u ? 0x20u : 0u);
    if (!dsp_byte(0x41u) || !dsp_byte((uint8_t)(rate >> 8)) ||
        !dsp_byte((uint8_t)rate) || !dsp_byte(cmd) || !dsp_byte(mode) ||
        !dsp_byte((uint8_t)count) || !dsp_byte((uint8_t)(count >> 8))) {
        running_bits = bits;
        sb_stop();
        return 0;
    }
    running_bits = bits;
    running = 1;
    return 1;
}

static int sb_finished(void)
{
    if (!opened || !running)
        return 0;
    outp(SB_BASE + 4u, 0x82u);
    uint8_t mask = running_bits == 16u ? 2u : 1u;
    if (!(inp(SB_BASE + 5u) & mask))
        return 0;
    (void)inp(SB_BASE + (running_bits == 16u ? 0x0fu : 0x0eu));
    outp(0x20u, 0x20u);          /* PIC IRQ5 EOI */
    running = 0;
    running_bits = 0;
    return 1;
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
    sb_stop, sb_pause, SB_BUFSIZE,
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
