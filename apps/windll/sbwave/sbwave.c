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
    return 1;
}

static void sb_stop(void)
{
    if (!opened)
        return;
    dsp_byte(0xd0u);                 /* halt 8-bit DMA */
    outp(0x0au, 0x04u | SB_DMA);    /* mask channel 1 */
    outp(SB_BASE + 4u, 0x82u);     /* mixer IRQ status */
    if (inp(SB_BASE + 5u) & 1u) {
        (void)inp(SB_BASE + 0x0eu); /* acknowledge SB8 IRQ */
        outp(0x20u, 0x20u);         /* PIC EOI: SB16 has IRQ 5 */
    }
    running = 0;
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

/* Program the 8237 DMA1 address, page, count and memory-to-device mode. */
static void program_dma(unsigned n)
{
    outp(0x0au, 0x04u | SB_DMA);
    outp(0x0cu, 0);                       /* reset byte flip-flop */
    outp(0x02u, (uint8_t)physical);
    outp(0x02u, (uint8_t)(physical >> 8));
    outp(0x83u, (uint8_t)(physical >> 16));
    outp(0x0cu, 0);
    outp(0x03u, (uint8_t)(n - 1u));
    outp(0x03u, (uint8_t)((n - 1u) >> 8));
    outp(0x0bu, 0x48u | SB_DMA);          /* single cycle, memory read */
    outp(0x0au, SB_DMA);                  /* unmask channel 1 */
}

static int sb_submit(const uint8_t *data, unsigned n, unsigned rate)
{
    if (!opened || running || !data || !n || n > SB_BUFSIZE ||
        rate < 4000u || rate > 48000u)
        return 0;
    memcpy(buffer, data, n);
    /* Clear any old 8-bit IRQ before beginning the next transfer. */
    outp(SB_BASE + 4u, 0x82u);
    if (inp(SB_BASE + 5u) & 1u) {
        (void)inp(SB_BASE + 0x0eu);
        outp(0x20u, 0x20u);
    }
    program_dma(n);
    /* SB16 generalized single-cycle 8-bit unsigned mono DAC. */
    if (!dsp_byte(0x41u) || !dsp_byte((uint8_t)(rate >> 8)) ||
        !dsp_byte((uint8_t)rate) || !dsp_byte(0xc0u) ||
        !dsp_byte(0x00u) || !dsp_byte((uint8_t)(n - 1u)) ||
        !dsp_byte((uint8_t)((n - 1u) >> 8))) {
        sb_stop();
        return 0;
    }
    running = 1;
    return 1;
}

static int sb_finished(void)
{
    if (!opened || !running)
        return 0;
    outp(SB_BASE + 4u, 0x82u);
    if (!(inp(SB_BASE + 5u) & 1u))
        return 0;
    (void)inp(SB_BASE + 0x0eu);  /* clear SB16's latched 8-bit IRQ */
    outp(0x20u, 0x20u);          /* PIC IRQ5 EOI */
    running = 0;
    return 1;
}

static void sb_pause(int paused)
{
    if (opened && running)
        dsp_byte(paused ? 0xd0u : 0xd4u);
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
