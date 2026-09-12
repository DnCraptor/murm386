#include "ay_hw.h"
#include "board_config.h"

#include <pico/stdlib.h>

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

static void __not_in_flash_func(ay_shift16)(uint16_t data)
{
    for (int i = 0; i < 16; ++i) {
        gpio_put(HWAY_CLOCK_PIN, 0);
        gpio_put(HWAY_CLOCK_PIN, 0);
        gpio_put(HWAY_CLOCK_PIN, 0);
        gpio_put(HWAY_DATA_PIN, (data & 0x8000u) != 0);
        data <<= 1;
        gpio_put(HWAY_CLOCK_PIN, 1);
        gpio_put(HWAY_CLOCK_PIN, 1);
        gpio_put(HWAY_CLOCK_PIN, 1);
    }
    gpio_put(HWAY_LATCH_PIN, 1);
    gpio_put(HWAY_LATCH_PIN, 1);
    gpio_put(HWAY_LATCH_PIN, 1);
    busy_wait_us_32(1);
    gpio_put(HWAY_CLOCK_PIN, 0);
    gpio_put(HWAY_CLOCK_PIN, 0);
    gpio_put(HWAY_LATCH_PIN, 0);
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

static void ay_select_register(uint8_t reg)
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

    /* Reference HWAY control state, with the separate beeper held low. */
    control_bits = AY_CS_SAA1099 | AY_ENABLE | AY_SAVE |
                   AY_CS1 | AY_CS0 | AY_BDIR | AY_BC1;
    ay_shift16(control_bits);

    /* Select the second AY, configure port B, then leave register 15
     * selected so each PCM sample needs only the data-write strobe. */
    control_high(AY_CS1);
    control_low(AY_CS0);
    ay_select_register(7);
    ay_write_data(0x80);
    ay_select_register(15);

    last_pcm_valid = false;
}

void __not_in_flash_func(ay_hw_write_pcm)(uint8_t sample)
{
    if (last_pcm_valid && sample == last_pcm)
        return;
    last_pcm = sample;
    last_pcm_valid = true;
    ay_write_data(sample);
}

#else

void ay_hw_init(void) {}
void ay_hw_write_pcm(uint8_t sample) { (void)sample; }

#endif
