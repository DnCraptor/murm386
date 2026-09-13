/**
 * murm-286 — PC analog game port (joystick) emulation.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * Emulates the standard IBM game adapter at 0x201 so DOS games see a
 * real analog joystick. Input is deliberately source-agnostic: the NES
 * pad drives it today, but a USB gamepad could feed the same API.
 *
 * How the hardware works, because the emulation only makes sense
 * against it: the card has no ADC. Each axis is one half of a 558 quad
 * timer wired as a one-shot whose period is set by the stick's
 * potentiometer. Writing any value to 0x201 fires all four one-shots;
 * the game then reads 0x201 in a tight loop and counts how long each
 * axis bit stays high. In the emulator the observable quantity is the
 * number of guest port reads before the bit falls; using host wall-clock
 * time would make the result depend on emulation overhead.
 *
 * Read layout at 0x201:
 *   bit 0  X axis, joystick A      bit 4  button A1  (0 = pressed)
 *   bit 1  Y axis, joystick A      bit 5  button A2
 *   bit 2  X axis, joystick B      bit 6  button B1
 *   bit 3  Y axis, joystick B      bit 7  button B2
 *
 * Axis bits read 1 while their one-shot is still running. With no write
 * having happened, or once every one-shot has expired, all four read 0 —
 * which with no buttons pressed gives 0xF0, exactly the value returned
 * when no joystick is fitted. That is why an idle emulated port is
 * indistinguishable from an absent one, and why nothing breaks for games
 * that never touch it.
 */
#ifndef GAMEPORT_H
#define GAMEPORT_H

#include <stdbool.h>
#include <stdint.h>

/* Axis positions, as a digital stick supplies them. */
typedef enum {
    GP_AXIS_MIN = -1,   /* left / up    */
    GP_AXIS_CENTRE = 0,
    GP_AXIS_MAX = 1     /* right / down */
} gp_axis_t;

/* Feed a digital stick. `buttons` bit 0 = button 1, bit 1 = button 2. */
void gameport_set(int x, int y, uint8_t buttons);

/* Feed an analog stick. X/Y span the full signed 16-bit range; 0 is centre. */
void gameport_set_analog(int16_t x, int16_t y, uint8_t buttons);

/* Feed both DOS game-port sticks. Joystick B is present only when
 * `b_present` is true; otherwise its axis bits remain low. */
void gameport_set_pair(int ax, int ay, uint8_t a_buttons,
                       int b_present, int bx, int by, uint8_t b_buttons);

/* Globally exchange joystick button 1 and button 2. */
void gameport_set_button_swap(bool enabled);

/* Port 0x201. A write of any value fires the one-shots. */
void gameport_write(void);
uint8_t gameport_read(void);

#endif /* GAMEPORT_H */
