/**
 * murm-286 — PC analog game port (joystick) emulation. See gameport.h.
 */
#include "gameport.h"

/*
 * One-shot lengths expressed in guest port reads, not host microseconds.
 *
 * A real gameport exposes an RC time and DOS software measures it by
 * counting how many INs fit before each axis bit falls.  Using RP2350 wall
 * time here made the result depend on emulator overhead: on a sufficiently
 * slow guest I/O path an axis could expire before software performed even
 * its first read.  Count the observable operation instead.  The ratios are
 * kept from the real 24..1124 us range, so calibration code still sees a
 * sensible min/centre/max spread.
 */
#define GP_COUNT_MIN     24u
#define GP_COUNT_CENTRE  562u
#define GP_COUNT_MAX     1100u

static uint32_t gp_count;
static uint32_t gp_dur[4] = {
    GP_COUNT_CENTRE, GP_COUNT_CENTRE, 0, 0
};
static uint8_t  gp_buttons;       /* bits 0/1 = A1/A2, bits 2/3 = B1/B2 */
static bool     gp_swap_buttons;
static bool     gp_running;


static uint32_t gp_duration(int axis) {
    if (axis < 0) return GP_COUNT_MIN;
    if (axis > 0) return GP_COUNT_MAX;
    return GP_COUNT_CENTRE;
}

static uint32_t gp_duration_analog(int16_t axis) {
    /* -32768..32767 -> the full emulated potentiometer travel.  The
     * constants are symmetric, so zero maps exactly to centre. */
    const uint32_t pos = (uint32_t)((int32_t)axis + 32768);
    const uint32_t span = GP_COUNT_MAX - GP_COUNT_MIN;
    return GP_COUNT_MIN + (pos * span + 32767u) / 65535u;
}

void gameport_set(int x, int y, uint8_t buttons) {
    gp_dur[0] = gp_duration(x);
    gp_dur[1] = gp_duration(y);
    gp_dur[2] = 0;
    gp_dur[3] = 0;
    gp_buttons = buttons & 0x03u;
}

void gameport_set_analog(int16_t x, int16_t y, uint8_t buttons) {
    gp_dur[0] = gp_duration_analog(x);
    gp_dur[1] = gp_duration_analog(y);
    gp_dur[2] = 0;
    gp_dur[3] = 0;
    gp_buttons = buttons & 0x03u;
}

void gameport_set_pair(int ax, int ay, uint8_t a_buttons,
                       int b_present, int bx, int by, uint8_t b_buttons) {
    gp_dur[0] = gp_duration(ax);
    gp_dur[1] = gp_duration(ay);
    if (b_present) {
        gp_dur[2] = gp_duration(bx);
        gp_dur[3] = gp_duration(by);
        gp_buttons = (a_buttons & 0x03u) | ((b_buttons & 0x03u) << 2);
    } else {
        gp_dur[2] = 0;
        gp_dur[3] = 0;
        gp_buttons = a_buttons & 0x03u;
    }
}

void gameport_set_button_swap(bool enabled) {
    gp_swap_buttons = enabled;
}

void gameport_write(void) {
    gp_count = 0;
    gp_running = true;
}

uint8_t gameport_read(void) {
    /* Buttons are active low and are readable without firing the
     * one-shots — plenty of games poll only the buttons. */
    uint8_t v = 0xf0;
    uint8_t buttons = gp_buttons;
    if (gp_swap_buttons)
        buttons = (uint8_t)(((buttons & 0x01u) << 1) |
                            ((buttons & 0x02u) >> 1) |
                            ((buttons & 0x04u) << 1) |
                            ((buttons & 0x08u) >> 1));
    if (buttons & 0x01u) v &= (uint8_t)~0x10u;
    if (buttons & 0x02u) v &= (uint8_t)~0x20u;
    if (buttons & 0x04u) v &= (uint8_t)~0x40u;
    if (buttons & 0x08u) v &= (uint8_t)~0x80u;

    if (gp_running) {
        const uint32_t count = gp_count++;
        if (count < gp_dur[0]) v |= 0x01u;
        if (count < gp_dur[1]) v |= 0x02u;
        if (count < gp_dur[2]) v |= 0x04u;
        if (count < gp_dur[3]) v |= 0x08u;
        if (gp_count >= gp_dur[0] && gp_count >= gp_dur[1] &&
            gp_count >= gp_dur[2] && gp_count >= gp_dur[3])
            gp_running = false;
    }
    return v;
}
