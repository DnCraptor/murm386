#include "ay_hw.h"
#include "board_config.h"

#include <pico/stdlib.h>
#include <hardware/clocks.h>

#if HAS_AUDIO_HWAY

/*
 * Murmulator Ultimate / TurboFrank serial AY interface.  Two cascaded
 * 74HC595s carry the AY data byte in bits 0..7 and control lines in
 * bits 8..15.  For PCM output we use port B (AY register 15) of the
 * second AY exactly as the PICO-BK HWAY Covox path does.
 */
#define AY_CS_SAA1099 (1u << 15)
#define AY_ENABLE     (1u << 14)
#define AY_SAVE       (1u << 13)
#define AY_BEEPER     (1u << 12)
#define AY_CS1        (1u << 11)
#define AY_CS0        (1u << 10)
#define AY_BDIR       (1u << 9)
#define AY_BC1        (1u << 8)

static uint16_t control_bits;
static uint8_t last_pcm;
static bool last_pcm_valid;

static inline void ay_wait_to_adjust(uint32_t wait_nops)
{
    for (uint32_t i = 0; i < wait_nops; ++i)
        __asm volatile("nop");
}

static void __not_in_flash_func(ay_shift16)(uint16_t data)
{
    /* Keep the 74HC595 timing identical to the current pico-speccy
     * reference: about 30 MHz maximum shift clock, with explicit setup/
     * hold time around every edge. */
    static uint32_t wait_nops;
    if (wait_nops == 0)
        wait_nops = clock_get_hz(clk_sys) / (30000000u * 5u);

    gpio_put(HWAY_CLOCK_PIN, 0);
    ay_wait_to_adjust(wait_nops);

    for (int i = 0; i < 16; ++i) {
        gpio_put(HWAY_DATA_PIN, (data & 0x8000u) != 0);
        data <<= 1;

        gpio_put(HWAY_CLOCK_PIN, 1);
        ay_wait_to_adjust(wait_nops);
        gpio_put(HWAY_CLOCK_PIN, 0);
        ay_wait_to_adjust(wait_nops);
    }

    gpio_put(HWAY_LATCH_PIN, 1);
    ay_wait_to_adjust(wait_nops);
    gpio_put(HWAY_LATCH_PIN, 0);
}

static inline void control_high(uint16_t mask)
{
    control_bits |= mask;
}

static inline void control_low(uint16_t mask)
{
    control_bits &= (uint16_t)~mask;
}

static void __not_in_flash_func(ay_select_register)(uint8_t reg)
{
    control_high(AY_BDIR | AY_BC1);
    ay_shift16(control_bits | reg);
    control_low(AY_BDIR | AY_BC1);
    ay_shift16(control_bits | reg);
}

static void __not_in_flash_func(ay_write_data)(uint8_t value)
{
    control_low(AY_BDIR);
    ay_shift16(control_bits | value);
    control_high(AY_BDIR);
    ay_shift16(control_bits | value);
    control_low(AY_BDIR);
    ay_shift16(control_bits | value);
}

void ay_hw_init(void)
{
    gpio_init(HWAY_LATCH_PIN);
    gpio_init(HWAY_CLOCK_PIN);
    gpio_init(HWAY_DATA_PIN);
    gpio_set_dir(HWAY_LATCH_PIN, GPIO_OUT);
    gpio_set_dir(HWAY_CLOCK_PIN, GPIO_OUT);
    gpio_set_dir(HWAY_DATA_PIN, GPIO_OUT);
    gpio_put(HWAY_LATCH_PIN, 0);
    gpio_put(HWAY_CLOCK_PIN, 0);
    gpio_put(HWAY_DATA_PIN, 0);

    /* Match the PICO-BK HWAY reset sequence exactly: control_bits starts
     * at zero, AY_Enable is driven low first, then the reference idle
     * state is latched with Beeper high as well. */
    control_bits = 0;
    control_low(AY_ENABLE);
    ay_shift16(control_bits);

    control_bits = AY_CS_SAA1099 | AY_ENABLE | AY_SAVE | AY_BEEPER |
                   AY_CS1 | AY_CS0 | AY_BDIR | AY_BC1;
    ay_shift16(control_bits);

    /* The PICO-BK Covox path re-selects/configures the AY on every changed
     * PCM sample.  Do the same here; do not rely on AY/595 state persisting
     * between timer callbacks. */
    last_pcm_valid = false;
}

void __not_in_flash_func(ay_hw_write_pcm)(uint8_t sample)
{
    /* simple resampling
    static uint8_t n = 0;
    if (++n < 5)
        return;
    n = 0;*/
    if (last_pcm_valid && sample == last_pcm)
        return;
    last_pcm = sample;
    last_pcm_valid = true;

    /* Exact PICO-BK HWAY Covox transaction:
     *   select second AY;
     *   R7  <- 0x80 (port B output);
     *   R15 <- PCM sample.
     */
    control_high(AY_CS1);
    control_low(AY_CS0);
    ay_select_register(7);
    ay_write_data(0x80);
    ay_select_register(15);
    ay_write_data(sample);
}

#else

void ay_hw_init(void) {}
void ay_hw_write_pcm(uint8_t sample) { (void)sample; }

#endif
