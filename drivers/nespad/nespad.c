/*
 * NES/SNES gamepad driver via PIO
 * Based on pico-infonesPlus by shuichitakano and fhoedemakers
 * https://github.com/fhoedemakers/pico-infonesPlus
 * SPDX-License-Identifier: MIT
 *
 * Fork maintained as part of FRANK NES by Mikhail Matveev.
 * https://rh1.tech | https://github.com/rh1tech/frank-nes
 */

#include "nespad.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

/*
 * IRQ-triggered NES/SNES gamepad reader via PIO.
 * Based on pico-infonesPlus M1/M2 variant (shared latch/clock).
 *
 * The PIO SM idles at "irq wait 0" between reads — zero bus activity
 * until nespad_read() clears the IRQ to trigger a read cycle (~100µs).
 * This avoids continuous PIO traffic that can disrupt HSTX HDMI output.
 */

/* PIO program: M1/M2 variant (shared latch+clock, single data pin per SM)
 * Reads 8 bits (NES) with right-shift autopush.
 * Sideset = CLK pin (1 bit). Set = LATCH pin. In = DATA pin.
 *
 * .wrap_target
 *   irq    wait 0      side 1      ; idle with CLK HIGH, wait for CPU
 *   set    pins, 1     side 1 [10] ; LATCH HIGH, 11µs pulse
 *   set    x, 15       side 1      ; 16 button clocks (SNES-compatible)
 *   set    pins, 0     side 0      ; LATCH LOW, CLK LOW — first bit available
 *   in     pins, 2     side 0 [4]  ; read both DATA pins, CLK LOW
 *   set    pins, 0     side 1 [4]  ; CLK HIGH (rising edge advances register)
 *   jmp    x--, 3      side 1      ; loop (jumps to "set pins,0 side 0")
 * .wrap
 */
static const uint16_t nespad_program_instructions[] = {
    0xD020, /*  0: irq    wait 0          side 1       */
    0xFA01, /*  1: set    pins, 1         side 1 [10]  */
    0xF02F, /*  2: set    x, 15           side 1       */
    0xE000, /*  3: set    pins, 0         side 0       */
    0x4402, /*  4: in     pins, 2         side 0 [4]   */
    0xF400, /*  5: set    pins, 0         side 1 [4]   */
    0x1043, /*  6: jmp    x--, 3          side 1       */
};

#define NESPAD_WRAP_TARGET 0
#define NESPAD_WRAP        6

static const struct pio_program nespad_program = {
    .instructions = nespad_program_instructions,
    .length = 7,
    .origin = -1,
};

static PIO pio = pio2;  /* pio0 = PS/2 keyboard, pio1 = video (HDMI/VGA) */
static int8_t sm = -1;
static bool pad_initialized = false;

uint32_t nespad_state = 0;
uint32_t nespad_state2 = 0;

bool nespad_begin(uint32_t cpu_khz, uint8_t clkPin, uint8_t dataPin,
                  uint8_t latPin)
{
    sm = pio_claim_unused_sm(pio, false);
    if (sm < 0)
        return false;
    if (!pio_can_add_program(pio, &nespad_program)) {
        pio_sm_unclaim(pio, sm);
        sm = -1;
        return false;
    }

    uint offset = pio_add_program(pio, &nespad_program);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + NESPAD_WRAP_TARGET, offset + NESPAD_WRAP);
    sm_config_set_sideset(&c, 1, false, false);

    sm_config_set_sideset_pins(&c, clkPin);
    sm_config_set_in_pins(&c, dataPin);
    sm_config_set_set_pins(&c, latPin, 1);

    pio_gpio_init(pio, clkPin);
    pio_gpio_init(pio, dataPin);
    pio_gpio_init(pio, dataPin + 1);
    pio_gpio_init(pio, latPin);

    gpio_set_pulls(dataPin, true, false);
    gpio_set_pulls(dataPin + 1, true, false);

    pio_sm_set_pindirs_with_mask(pio, sm,
                                  (1u << clkPin) | (1u << latPin),
                                  (1u << clkPin) | (1u << dataPin) |
                                      (1u << (dataPin + 1)) |
                                      (1u << latPin));

    /* Two DATA pins x 16 clocks -> one 32-bit interleaved word. */
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_clkdiv_int_frac(&c, cpu_khz / 1000, 0); /* 1 MHz */

    pio_set_irq0_source_enabled(pio, (enum pio_interrupt_source)(pis_interrupt0 + sm), false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    pad_initialized = true;
    return true;
}

void nespad_reclock(uint32_t cpu_khz)
{
    if (!pad_initialized || sm < 0)
        return;

    pio_sm_set_clkdiv_int_frac(pio, sm, cpu_khz / 1000, 0); /* 1 MHz */
}

/* Trigger PIO read (non-blocking). Result ready in ~100µs via nespad_read_finish(). */
void nespad_read_start(void)
{
    if (!pad_initialized || sm < 0)
        return;
    pio_interrupt_clear(pio, 0);
}

/* Wait for PIO read to complete and update nespad_state.
 * Call ~100µs+ after nespad_read_start(). If called later, returns instantly.
 * Uses timeout to avoid deadlock if PIO fails. */
void nespad_read_finish(void)
{
    if (!pad_initialized || sm < 0)
        return;

    /* Wait up to ~500µs for PIO to push result (read takes ~100µs) */
    for (int i = 0; i < 5000 && pio_sm_is_rx_fifo_empty(pio, sm); i++)
        tight_loop_contents();
    if (pio_sm_is_rx_fifo_empty(pio, sm))
        return; /* timeout — keep previous state */

    /* Each clock shifts DATA1 then DATA2 into the ISR. After 16 clocks the
     * 32-bit word is interleaved exactly like pico-speccy: even bits are
     * joystick 1, odd bits joystick 2. Inputs are active low. */
    const uint32_t raw = pio->rxf[sm] ^ 0xFFFFFFFFu;
    nespad_state  = raw & 0x00555555u;
    nespad_state2 = (raw >> 1) & 0x00555555u;
}

/* Convenience: trigger + block. Use start/finish for overlapped reads. */
void nespad_read(void)
{
    nespad_read_start();
    nespad_read_finish();
}
