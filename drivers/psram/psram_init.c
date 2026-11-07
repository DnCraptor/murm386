/**
 * PSRAM Driver for RP2350
 *
 * Initializes APS6404L-3SQR QSPI PSRAM (8MB) on CS1.
 * Memory is mapped at 0x11000000 (XIP_SRAM_BASE).
 */

#include "board_config.h"
#include "psram_init.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/xip_cache.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>

// PSRAM max frequency from build config (default 133 MHz)
#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ 133
#endif

/**
 * Initialize PSRAM hardware with specified frequency.
 * This function MUST run from RAM, not flash, as it reconfigures the XIP controller.
 */
/*
 * Highest PSRAM clock this part has been seen to work at. Lowered by
 * main.c when detection only succeeds at a slower clock, and applied to
 * every later re-init (clock changes, HDMI boost, config.ini) so they do
 * not push the part back to the speed that failed.
 */
static int psram_freq_cap_mhz = 0;

void psram_set_freq_cap(int mhz) { psram_freq_cap_mhz = mhz; }
int psram_get_freq_cap(void) { return psram_freq_cap_mhz; }

void __no_inline_not_in_flash_func(psram_init_with_freq)(uint cs_pin, int freq_mhz) {
#if QSPI_PSRAM_ALLOWED
    const int clock_hz = clock_get_hz(clk_sys);
    if (psram_freq_cap_mhz > 0 && freq_mhz > psram_freq_cap_mhz)
        freq_mhz = psram_freq_cap_mhz;

    // Configure GPIO for XIP CS1 function
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    // Flash is unreadable while QMI direct mode is on: no interrupt may run
    // flash code in the meantime (the USB stdio IRQ did, and locked core 0)
    uint32_t irq = save_and_disable_interrupts();

    // Enter direct mode with slow clock for initialization
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB |
                        QMI_DIRECT_CSR_EN_BITS |
                        QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);

    // Send QPI enable command (0x35) to PSRAM
    const uint CMD_QPI_EN = 0x35;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);

    // Calculate optimal clock divisor for target PSRAM frequency
    const int max_psram_freq = freq_mhz * 1000000;

    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) {
        divisor = 2;  // Minimum divisor of 2 at high system clocks
    }

    // RX delay compensation for high-speed operation
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) {
        rxdelay += 1;
    }

    // Calculate timing parameters
    const int clock_period_fs = 1000000000000000ll / clock_hz;
    const int max_select_val = (125 * 1000000) / clock_period_fs;
    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    // Configure M1 (PSRAM) timing
    qmi_hw->m[1].timing =
        1 << QMI_M1_TIMING_COOLDOWN_LSB |
        QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
        max_select_val << QMI_M1_TIMING_MAX_SELECT_LSB |
        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
        /* RXDELAY is 3 bits: keep what it held before (the low bits) and
         * stop larger values (slow fallback clocks) spilling into
         * MIN_DESELECT */
        (rxdelay & 7) << QMI_M1_TIMING_RXDELAY_LSB |
        divisor << QMI_M1_TIMING_CLKDIV_LSB;

    // Configure read format: Quad mode, 0xEB fast read command, 6 dummy cycles
    qmi_hw->m[1].rfmt =
        QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
        QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
        QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
        QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
        QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
        QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
        6 << QMI_M0_RFMT_DUMMY_LEN_LSB;

    qmi_hw->m[1].rcmd = 0xEB;  // Fast Read Quad I/O

    // Configure write format: Quad mode, 0x38 write command
    qmi_hw->m[1].wfmt =
        QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
        QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
        QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
        QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
        QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
        QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;

    qmi_hw->m[1].wcmd = 0x38;  // Quad Write

    // Exit direct mode
    qmi_hw->direct_csr = 0;
    restore_interrupts(irq);

    // Enable writes to M1 (PSRAM) region
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
#endif
}

/**
 * Initialize PSRAM hardware with default frequency.
 * This function MUST run from RAM, not flash, as it reconfigures the XIP controller.
 */
static uint8_t psram_id[2];

#if QSPI_PSRAM_ALLOWED
/* One direct-mode transfer with CS1 held for its whole length. In quad
 * mode only the command byte is sent (to leave QPI mode). */
/* Boot-only code below lives in CORE0_STACK_EXT (copied from flash at the
 * top of main()), not in .data: SRAM there is free during boot, while every
 * byte of .data comes out of the heap pc_new() needs. */
#ifdef PSRAM_BOOT_IN_STACK_EXT
#define PSRAM_BOOT_FUNC(name) __attribute__((noinline, section(".core0_stack_ext." #name))) name
#else
/* Other link scripts (the C2 slave) have no such section: plain RAM code */
#define PSRAM_BOOT_FUNC(name) __no_inline_not_in_flash_func(name)
#endif

static void PSRAM_BOOT_FUNC(psram_direct_xfer)(const uint8_t *tx, uint8_t *rx,
                                               int n, bool quad) {
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    for (int i = 0; i < n; i++) {
        uint32_t w = tx[i];
        if (quad)
            w |= QMI_DIRECT_TX_OE_BITS |
                 QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB;
        qmi_hw->direct_tx = w;
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
        uint8_t b = (uint8_t)qmi_hw->direct_rx;
        if (rx) rx[i] = b;
    }
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    // CS# high time between commands (tCPH, up to 50 ns on some parts):
    // at 504 MHz the next hw_set_bits() comes only a few cycles later
    for (volatile int i = 0; i < 64; i++);
}

/*
 * Bring the part to a known state before the first init: leave QPI mode
 * (it survives a warm reset of the RP2350, the PSRAM keeps its power),
 * software reset, then read the ID. Same order as the Adafruit/Sparkfun
 * RP2350 PSRAM setup. Only done once, at boot: a reset does not promise
 * to keep the contents, and later re-inits run with data in PSRAM.
 * Ported from rh1tech/frank-386 v1.06 (b786d42).
 */
static void PSRAM_BOOT_FUNC(psram_reset_and_read_id)(uint cs_pin) {
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);
    uint32_t irq = save_and_disable_interrupts();
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);

    const uint8_t exit_qpi = 0xF5;
    psram_direct_xfer(&exit_qpi, NULL, 1, true);
    const uint8_t rst_en = 0x66, rst = 0x99;
    psram_direct_xfer(&rst_en, NULL, 1, false);
    psram_direct_xfer(&rst, NULL, 1, false);
    // tRST is 50 ns; a few hundred cycles of the loop below is plenty
    for (volatile int i = 0; i < 2000; i++);

    // Read ID: 9Fh, 24-bit address, then MF ID (0Dh on APS6404) and KGD (5Dh = good die).
    // Filled byte by byte: an initialiser would be copied from .rodata in
    // flash, and flash cannot be read while direct mode is on.
    uint8_t tx[6], rx[6];
    tx[0] = 0x9F; tx[1] = 0; tx[2] = 0; tx[3] = 0; tx[4] = 0xFF; tx[5] = 0xFF;
    psram_direct_xfer(tx, rx, 6, false);
    psram_id[0] = rx[4];
    psram_id[1] = rx[5];

    qmi_hw->direct_csr = 0;
    restore_interrupts(irq);
}
#endif

void psram_get_id(uint8_t *mfid, uint8_t *kgd) {
    *mfid = psram_id[0];
    *kgd = psram_id[1];
}

/* The plain init, exactly as before the frank-386 v1.06 port: boards on
 * which PSRAM came up this way keep the same bring-up sequence. */
void __no_inline_not_in_flash_func(psram_init)(uint cs_pin) {
    psram_init_with_freq(cs_pin, PSRAM_MAX_FREQ_MHZ);
}

/* Recovery path, only used when the plain init found nothing: leave QPI,
 * software-reset the part, read its ID, then init again. */
void PSRAM_BOOT_FUNC(psram_reset_init)(uint cs_pin) {
#if QSPI_PSRAM_ALLOWED
    psram_reset_and_read_id(cs_pin);
#endif
    psram_init_with_freq(cs_pin, PSRAM_MAX_FREQ_MHZ);
}

static size_t detected_psram_size;

/* RP2350 XIP M1 (PSRAM) uncached/no-allocate alias.
 * Cached M1 starts at 0x11000000; the equivalent uncached XIP window is
 * 0x15000000. POST diagnostics use this alias so every access reaches the
 * external PSRAM instead of being satisfied from the XIP cache. */
#define PSRAM_UNCACHED_BASE_ADDR 0x15000000u

size_t psram_detected_size(void) {
    return detected_psram_size;
}

void psram_set_detected_size(size_t size) {
    detected_psram_size = size;
}

size_t psram_usable_size(void) {
    return detected_psram_size;
}

static uint32_t probe_readback;
uint32_t psram_probe_readback(void) { return probe_readback; }

size_t psram_detect_size(void) {
    volatile uint32_t *base = (volatile uint32_t *)PSRAM_UNCACHED_BASE_ADDR;
    static const size_t boundaries[] = {
        1u << 20, 2u << 20, 4u << 20, 8u << 20
    };
    const uint32_t mark0 = 0x13579BDFu;
    const uint32_t mark1 = 0x2468ACE0u;
    uint32_t old0 = base[0];

    base[0] = mark0;
    __dmb();
    probe_readback = base[0];
    if (probe_readback != mark0) {
        base[0] = old0;
        __dmb();
        detected_psram_size = 0;
        return 0;
    }
    base[0] = old0;
    __dmb();

    /* A smaller serial PSRAM aliases at its capacity boundary.  No access
     * beyond +8 MiB is required: absence of aliases at 1/2/4/8 identifies
     * the largest addressable 16 MiB device. */
    for (unsigned i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        size_t bytes = boundaries[i];
        volatile uint32_t *probe =
            (volatile uint32_t *)((uintptr_t)PSRAM_UNCACHED_BASE_ADDR + bytes);
        old0 = base[0];
        uint32_t oldp = probe[0];

        base[0] = mark0;
        __dmb();
        probe[0] = mark1;
        __dmb();

        if (base[0] == mark1) {
            base[0] = old0;
            __dmb();
            detected_psram_size = bytes;
            return bytes;
        }

        probe[0] = oldp;
        base[0] = old0;
        __dmb();
    }

    detected_psram_size = 16u << 20;
    return detected_psram_size;
}

void psram_prepare_nondestructive_test(void) {
    /* Guest/BIOS state has already been written through the cached M1 alias.
     * Commit only the PSRAM portion of XIP before taking the physical uncached
     * view as the source of truth for save/restore.  clean_range() also avoids
     * the RP2350-E11 clean_all() side effect of invalidating the whole cache. */
    if (detected_psram_size)
        xip_cache_clean_range((uintptr_t)PSRAM_BASE_ADDR - XIP_BASE,
                              detected_psram_size);
    __dmb();
}

bool psram_test_nondestructive(size_t offset, size_t length) {
    volatile uint32_t *p =
        (volatile uint32_t *)((uintptr_t)PSRAM_UNCACHED_BASE_ADDR + offset);
    size_t words = length >> 2;

    for (size_t i = 0; i < words; ++i) {
        uint32_t old = p[i];
        uint32_t pat0 = 0xA5A55A5Au ^ (uint32_t)(offset + (i << 2));
        uint32_t pat1 = ~pat0;

        p[i] = pat0;
        if (p[i] != pat0) {
            p[i] = old;
            __dmb();
            xip_cache_invalidate_range((uintptr_t)PSRAM_BASE_ADDR - XIP_BASE + offset,
                                       length);
            return false;
        }

        p[i] = pat1;
        if (p[i] != pat1) {
            p[i] = old;
            __dmb();
            xip_cache_invalidate_range((uintptr_t)PSRAM_BASE_ADDR - XIP_BASE + offset,
                                       length);
            return false;
        }

        p[i] = old;
    }
    __dmb();

    /* The video renderer can read guest VRAM through the cached M1 alias while
     * this routine temporarily writes test patterns through the uncached alias.
     * Drop any cache lines which may have captured those transient patterns, so
     * subsequent cached reads see the restored physical PSRAM contents. */
    xip_cache_invalidate_range((uintptr_t)PSRAM_BASE_ADDR - XIP_BASE + offset,
                               length);
    return true;
}

/**
 * Test PSRAM by writing and reading back test patterns.
 */
bool psram_test(void) {
    volatile uint32_t *psram = (volatile uint32_t *)PSRAM_BASE_ADDR;

    // Test pattern at various locations
    const uint32_t test_offsets[] = {
        0,                      // Start
        1024,                   // 4KB
        256 * 1024,             // 1MB
        1024 * 1024,            // 4MB
        2 * 1024 * 1024 - 4,    // Near end of 8MB
    };

    const uint32_t test_pattern = 0xDEADBEEF;
    const uint32_t test_pattern2 = 0x12345678;

    for (int i = 0; i < (int)(sizeof(test_offsets) / sizeof(test_offsets[0])); i++) {
        uint32_t offset = test_offsets[i] / 4;  // Convert to word offset

        // Write pattern
        psram[offset] = test_pattern;

        // Read back and verify
        if (psram[offset] != test_pattern) {
            return false;
        }

        // Write second pattern
        psram[offset] = test_pattern2;

        // Read back and verify
        if (psram[offset] != test_pattern2) {
            return false;
        }

        // Clear
        psram[offset] = 0;
    }

    return true;
}
