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
static uint32_t gp_dur_x = GP_COUNT_CENTRE;
static uint32_t gp_dur_y = GP_COUNT_CENTRE;
static uint8_t  gp_buttons;       /* bit 0 = button 1, bit 1 = button 2 */
static bool     gp_running;


static uint32_t gp_duration(int axis) {
    if (axis < 0) return GP_COUNT_MIN;
    if (axis > 0) return GP_COUNT_MAX;
    return GP_COUNT_CENTRE;
}

void gameport_set(int x, int y, uint8_t buttons) {
    gp_dur_x = gp_duration(x);
    gp_dur_y = gp_duration(y);
    gp_buttons = buttons;
}

void gameport_write(void) {
    gp_count = 0;
    gp_running = true;
}

uint8_t gameport_read(void) {
    /* Buttons are active low and are readable without firing the
     * one-shots — plenty of games poll only the buttons. */
    uint8_t v = 0xf0;
    if (gp_buttons & 1u) v &= (uint8_t)~0x10u;
    if (gp_buttons & 2u) v &= (uint8_t)~0x20u;

    if (gp_running) {
        const uint32_t count = gp_count++;
        if (count < gp_dur_x) v |= 0x01u;
        if (count < gp_dur_y) v |= 0x02u;
        /* Joystick B is not present: its bits stay low, which is how a
         * one-stick adapter reads. Games probing for a second stick see
         * an immediate timeout and move on. */
        if (gp_count >= gp_dur_x && gp_count >= gp_dur_y)
            gp_running = false;
    }
    return v;
}
