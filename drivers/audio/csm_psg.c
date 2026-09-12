#include "csm_psg.h"
#include "ay_hw.h"

#include <stdbool.h>
#include <string.h>

bool audio_is_hway(void);

/* AY-3-8910 compatible-mode core ported from the current pico-speccy
 * AySound implementation.  Covox Sound Master used AY-3-8930, but this
 * first implementation intentionally uses only its 8910-compatible mode. */
#define AYEMU_MAX_AMP 140
#define AYEMU_DEFAULT_CHIP_FREQ 1773400
#define AYEMU_OUTPUT_FREQ 44100
#define AYEMU_TACTS_PER_SAMPLE (AYEMU_DEFAULT_CHIP_FREQ / AYEMU_OUTPUT_FREQ / 8)

static const uint8_t rampa_ay_table[16] =
    {0,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31};

/* Exact table produced by pico-speccy AySound::set_chip_type(). */
static const uint8_t ay_volume_table[32] = {
    0,0,0,1,1,1,2,2,2,3,3,4,4,5,6,7,
    8,9,10,11,13,14,16,18,20,23,26,29,32,36,40,45
};

typedef struct {
    uint8_t regs_a[16];
    uint8_t regs_b[16];
    uint8_t selected_register;
    uint8_t mode;

    int32_t tone_count[3];
    uint8_t tone_phase[3];
    uint8_t tone_output[3];

    int32_t noise_count;
    uint32_t noise_rng;
    uint8_t noise_output;
    uint16_t noise_value;

    int32_t env_count[3];
    uint8_t env_pos[3];
} CsmPsg;

static CsmPsg psg;
static uint8_t hw_channel_c_output = 1;

static inline int psg_is_expanded(void)
{
    return (psg.mode & 0x0e) == 0x0a;
}

static inline int psg_bank_b(void)
{
    return psg_is_expanded() && (psg.mode & 0x01);
}

/* Exact pre-generated envelope table from current pico-speccy AySound. */
static const uint8_t envelope_table[16][128] = {
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31},
{31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static const uint8_t duty_high_steps[9] = {1, 2, 4, 8, 16, 24, 28, 30, 31};

static void psg_reset_generators(void)
{
    memset(psg.tone_count, 0, sizeof(psg.tone_count));
    memset(psg.tone_phase, 0, sizeof(psg.tone_phase));
    memset(psg.tone_output, 0, sizeof(psg.tone_output));
    memset(psg.env_count, 0, sizeof(psg.env_count));
    memset(psg.env_pos, 0, sizeof(psg.env_pos));
    psg.noise_count = 0;
    psg.noise_value = 0;
    psg.noise_rng = 0xffff;
    psg.noise_output = 0;
}

static void psg_hw_silence_primary(void)
{
    if (!audio_is_hway())
        return;

    /* In expanded mode the first physical AY-3-8910 cannot represent the
     * AY8930 state.  Silence it; the software AY8930 is then emitted as PCM
     * through the second chip by the normal HW audio mixer path. */
    ay_hw_psg_select_register(7);
    ay_hw_psg_write_data(0x3f);
    for (uint8_t r = 8; r <= 10; ++r) {
        ay_hw_psg_select_register(r);
        ay_hw_psg_write_data(0);
    }
}

static uint8_t psg_hw_value(uint8_t reg, uint8_t value)
{
    if (reg == 1 || reg == 3 || reg == 5)
        value &= 0x0f;
    else if (reg == 6)
        value &= 0x1f;
    else if (reg >= 8 && reg <= 10)
        value &= 0x1f;
    else if (reg == 13)
        value &= 0x0f;

    if (reg == 7 && !hw_channel_c_output)
        value |= 0x24;
    return value;
}

static void psg_hw_resync_compatible(void)
{
    if (!audio_is_hway() || psg_is_expanded())
        return;

    /* The physical chip was intentionally frozen while expanded mode was
     * active.  Reconstruct it in one bus transaction.  Doing sixteen
     * select/write pairs through separately locked helpers lets the 44.1-kHz
     * core-1 PCM IRQ repeatedly take the same spinlock between operations and
     * can starve core0 exactly when software leaves expanded mode. */
    uint8_t regs[16];
    for (uint8_t r = 0; r < 16; ++r)
        regs[r] = psg_hw_value(r, psg.regs_a[r]);
    ay_hw_psg_write_registers(regs);

    if (psg.selected_register < 16)
        ay_hw_psg_select_register(psg.selected_register);
}

static void psg_mode_write(uint8_t value)
{
    const int was_expanded = psg_is_expanded();
    const uint8_t old_mode = psg.mode;
    const uint8_t new_mode = (value >> 4) & 0x0f;
    const int will_expand = (new_mode & 0x0e) == 0x0a;

    psg.mode = new_mode;

    /* MAME follows the AY8930 datasheet here: crossing the compatibility /
     * expanded boundary clears both banks 0..12.  R13 is shared and its
     * high nibble carries mode/bank while the low nibble is envelope A. */
    if (was_expanded != will_expand) {
        /* AY8930 mode changes clear the register banks, but the mixer/
         * enable register R7 is retained.  The working 86Box CSM model
         * preserves it explicitly; clearing it here can spuriously enable
         * channel-C timing and therefore restart AYDMA. */
        const uint8_t enable = psg.regs_a[7];
        memset(psg.regs_a, 0, 13);
        memset(psg.regs_b, 0, 13);
        psg.regs_a[7] = enable;
        psg_reset_generators();
    }

    psg.regs_a[13] = value;
    psg.regs_b[13] = value;
    psg.env_pos[0] = 0;
    psg.env_count[0] = 0;

    if (!was_expanded && will_expand)
        psg_hw_silence_primary();
    else if (was_expanded && !will_expand)
        psg_hw_resync_compatible();
    else if (!will_expand && old_mode != new_mode && audio_is_hway()) {
        ay_hw_psg_select_register(13);
        ay_hw_psg_write_data(value & 0x0f);
    }
}

void csm_psg_reset(void)
{
    memset(&psg, 0, sizeof(psg));
    psg.regs_a[7] = 0xff;
    psg.regs_a[15] = 0xe0;
    psg.selected_register = 0xff;
    psg_reset_generators();
    hw_channel_c_output = 1;
}

void csm_psg_select_register(uint8_t reg)
{
    psg.selected_register = reg;
    if (audio_is_hway() && !psg_is_expanded())
        ay_hw_psg_select_register(reg);
}

void csm_psg_write_data(uint8_t value)
{
    const uint8_t reg = psg.selected_register;
    if (reg >= 16)
        return;

    if (reg == 13) {
        const int was_expanded = psg_is_expanded();
        const int will_expand = ((((value >> 4) & 0x0f) & 0x0e) == 0x0a);

        /* Do not send A0/B0 expanded-mode commands to a physical 8910. */
        if (audio_is_hway() && !was_expanded && !will_expand) {
            ay_hw_psg_select_register(13);
            ay_hw_psg_write_data(value & 0x0f);
        }
        psg_mode_write(value);
        return;
    }

    if (psg_bank_b()) {
        switch (reg) {
        case 0: case 1: case 2: case 3:
        case 9: case 10:
            psg.regs_b[reg] = value;
            break;
        case 4: case 5: case 6: case 7: case 8:
            psg.regs_b[reg] = value & 0x0f;
            if (reg == 4) { psg.env_pos[1] = 0; psg.env_count[1] = 0; }
            if (reg == 5) { psg.env_pos[2] = 0; psg.env_count[2] = 0; }
            break;
        default:
            /* AY8930 bank-B reserved registers read as zero. */
            psg.regs_b[reg] = 0;
            break;
        }
        return;
    }

    if (psg_is_expanded()) {
        switch (reg) {
        case 8: case 9: case 10:
            psg.regs_a[reg] = value & 0x3f;
            break;
        default:
            psg.regs_a[reg] = value;
            break;
        }
    } else {
        switch (reg) {
        case 1: case 3: case 5:
            psg.regs_a[reg] = value & 0x0f;
            break;
        case 6:
            psg.regs_a[reg] = value & 0x1f;
            break;
        case 8: case 9: case 10:
            psg.regs_a[reg] = value & 0x1f;
            break;
        default:
            psg.regs_a[reg] = value;
            break;
        }
    }

    if (reg == 13) {
        psg.env_pos[0] = 0;
        psg.env_count[0] = 0;
    }

    if (audio_is_hway() && !psg_is_expanded()) {
        ay_hw_psg_select_register(reg);
        ay_hw_psg_write_data(psg_hw_value(reg, psg.regs_a[reg]));
    }
}

void csm_psg_force_channel_c_output(void)
{
    if (psg.regs_a[15] & 0x80)
        return;

    psg.regs_a[15] |= 0x80;
    if (audio_is_hway() && !psg_is_expanded()) {
        const uint8_t selected = psg.selected_register;
        ay_hw_psg_select_register(15);
        ay_hw_psg_write_data(psg.regs_a[15]);
        if (selected < 16)
            ay_hw_psg_select_register(selected);
    }
}

void csm_psg_set_channel_c_output(int enabled)
{
    const uint8_t new_state = enabled ? 1 : 0;
    if (hw_channel_c_output == new_state)
        return;

    hw_channel_c_output = new_state;

    if (audio_is_hway() && !psg_is_expanded()) {
        const uint8_t selected = psg.selected_register;
        ay_hw_psg_select_register(7);
        ay_hw_psg_write_data(psg_hw_value(7, psg.regs_a[7]));
        if (selected < 16)
            ay_hw_psg_select_register(selected);
    }
}

int csm_psg_is_expanded(void)
{
    return psg_is_expanded();
}

int csm_psg_is_bank_b(void)
{
    return psg_bank_b();
}

uint8_t csm_psg_get_bank_a_register(uint8_t reg)
{
    return reg < 16 ? psg.regs_a[reg] : 0;
}

uint8_t csm_psg_read_data(void)
{
    const uint8_t r = psg.selected_register;
    if (r >= 16)
        return 0xff;

    if (r == 13)
        return psg_is_expanded() ? psg.regs_a[13] : (psg.regs_a[13] & 0x0f);

    if (psg_bank_b()) {
        switch (r) {
        case 0: case 1: case 2: case 3:
        case 9: case 10:
            return psg.regs_b[r];
        case 4: case 5: case 6: case 7: case 8:
            return psg.regs_b[r] & 0x0f;
        default:
            return 0;
        }
    }

    if (!psg_is_expanded()) {
        switch (r) {
        case 1: case 3: case 5: return psg.regs_a[r] & 0x0f;
        case 6: return psg.regs_a[r] & 0x1f;
        case 8: case 9: case 10: return psg.regs_a[r] & 0x1f;
        default: break;
        }
    }

    /* Sound Master I/O ports have no readable external peripheral attached.
     * Preserve the pull-state behaviour already used by csm.c/86Box. */
    if (r == 14 && !(psg.regs_a[7] & 0x40))
        return psg.regs_a[14] == 0xff ? 0xff : 0x00;
    if (r == 15 && !(psg.regs_a[7] & 0x80))
        return psg.regs_a[15] == 0xff ? 0xff : 0xf0;

    return psg.regs_a[r];
}

static inline uint16_t tone_period(int ch)
{
    uint16_t p = (uint16_t)psg.regs_a[ch * 2] |
                 ((uint16_t)psg.regs_a[ch * 2 + 1] << 8);
    if (!psg_is_expanded())
        p &= 0x0fff;
    return p ? p : 1;
}

static inline uint16_t envelope_period(int ch)
{
    uint16_t p;
    if (!psg_is_expanded() || ch == 0)
        p = (uint16_t)psg.regs_a[11] | ((uint16_t)psg.regs_a[12] << 8);
    else if (ch == 1)
        p = (uint16_t)psg.regs_b[0] | ((uint16_t)psg.regs_b[1] << 8);
    else
        p = (uint16_t)psg.regs_b[2] | ((uint16_t)psg.regs_b[3] << 8);
    return p ? p : 1;
}

static inline uint8_t envelope_shape(int ch)
{
    if (!psg_is_expanded() || ch == 0)
        return psg.regs_a[13] & 0x0f;
    return psg.regs_b[ch == 1 ? 4 : 5] & 0x0f;
}

static inline uint8_t duty_index(int ch)
{
    uint8_t duty = psg.regs_b[6 + ch] & 0x0f;
    return duty <= 8 ? duty : 8;
}

static void psg_tick_tones(void)
{
    for (int ch = 0; ch < 3; ++ch) {
        const int period = tone_period(ch);
        if (psg_is_expanded()) {
            /* MAME advances the 32-step duty phase 32x faster in expanded
             * mode.  Relative to our existing compatibility tick, +16 keeps
             * the 50% waveform at the same fundamental period. */
            psg.tone_count[ch] += 16;
            while (psg.tone_count[ch] >= period) {
                psg.tone_count[ch] -= period;
                psg.tone_phase[ch] = (psg.tone_phase[ch] + 1) & 31;
            }
            psg.tone_output[ch] =
                psg.tone_phase[ch] < duty_high_steps[duty_index(ch)];
        } else {
            if (++psg.tone_count[ch] >= period) {
                psg.tone_count[ch] = 0;
                psg.tone_output[ch] ^= 1;
            }
        }
    }
}

static void psg_noise_rng_tick(void)
{
    /* Preserve the pico-speccy/legacy polynomial used by this core. */
    psg.noise_rng = (psg.noise_rng * 2u + 1u) ^
                    (((psg.noise_rng >> 16) ^ (psg.noise_rng >> 13)) & 1u);
}

static void psg_tick_noise(void)
{
    const int period = psg_is_expanded() ?
        (psg.regs_a[6] ? psg.regs_a[6] : 1) :
        ((psg.regs_a[6] & 0x1f) ? ((psg.regs_a[6] & 0x1f) * 2) : 1);

    if (++psg.noise_count < period)
        return;
    psg.noise_count = 0;

    if (psg_is_expanded()) {
        const uint8_t limit =
            ((uint8_t)psg.noise_rng & psg.regs_b[9]) | psg.regs_b[10];
        if (++psg.noise_value >= limit) {
            psg.noise_value = 0;
            psg.noise_output ^= 1;
            psg_noise_rng_tick();
        }
    } else {
        psg_noise_rng_tick();
        psg.noise_output = (psg.noise_rng >> 16) & 1;
    }
}

static void psg_tick_envelopes(void)
{
    const int n = psg_is_expanded() ? 3 : 1;
    for (int ch = 0; ch < n; ++ch) {
        const int period = envelope_period(ch);
        if (++psg.env_count[ch] >= period) {
            psg.env_count[ch] = 0;
            if (++psg.env_pos[ch] > 127)
                psg.env_pos[ch] = 64;
        }
    }
}

int16_t csm_psg_sample(void)
{
    const int expanded = psg_is_expanded();
    int mix = 0;

    for (int m = 0; m < AYEMU_TACTS_PER_SAMPLE; ++m) {
        psg_tick_tones();
        psg_tick_noise();
        psg_tick_envelopes();

        for (int ch = 0; ch < 3; ++ch) {
            const uint8_t volreg = psg.regs_a[8 + ch];
            const int tone_enable = !(psg.regs_a[7] & (1u << ch));
            const int noise_enable = !(psg.regs_a[7] & (1u << (ch + 3)));
            const int gate = (psg.tone_output[ch] | !tone_enable) &
                             (psg.noise_output | !noise_enable);

            uint8_t level;
            if (volreg & (expanded ? 0x20 : 0x10)) {
                const int ech = expanded ? ch : 0;
                level = envelope_table[envelope_shape(ech)][psg.env_pos[ech]];
            } else if (expanded) {
                level = volreg & 0x1f;
            } else {
                level = rampa_ay_table[volreg & 0x0f];
            }

            /* Signed mixer domain: a PSG square wave must be -A/+A here,
             * not 0/+A.  The latter has only half the AC amplitude after
             * the final DAC/output coupling and was the remaining ~2x loss. */
            const int amp = ay_volume_table[level];
            mix += gate ? amp : -amp;
        }
    }

    /* One full-scale channel uses almost the full int16 range.  Keep only
     * modest headroom; simultaneous loud channels saturate, and the normal
     * master volume remains the intended way to avoid overload. */
    const int one_channel_full = AYEMU_TACTS_PER_SAMPLE * ay_volume_table[31];
    int32_t sample = (mix * 30000) / one_channel_full;
    if (sample > 32767)
        sample = 32767;
    else if (sample < -32768)
        sample = -32768;
    return (int16_t)sample;
}
