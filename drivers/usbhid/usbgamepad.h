/**
 * murm-286 — USB HID gamepad decoding.
 *
 * SPDX-License-Identifier: MIT
 *
 * Gamepad maps ported from FRANK NES (murmnes), which in turn generated
 * them from murmsnes/scripts/gen_gamepad_maps.py.
 *
 * Deliberately additive: the keyboard and mouse paths in hid_app.c are
 * untouched. This only claims reports whose HID usage is Joystick or
 * Gamepad, so a build with no pad attached behaves exactly as before.
 *
 * Feeds the emulated game port (src/gameport.h), the same sink the NES
 * pad uses.
 */
#ifndef USBGAMEPAD_H
#define USBGAMEPAD_H

#include <stdint.h>

/* Called from hid_app.c. `instance` is the TinyUSB HID instance. */
void usbgamepad_set_ids(uint8_t instance, uint16_t vid, uint16_t pid);
void usbgamepad_report(uint8_t instance, const uint8_t *report, uint16_t len);
/* Decode devices whose reports cannot safely go through TinyUSB's generic
 * HID report parser (DS4/DS5, F710 DInput, selected clone pads). Returns
 * non-zero when the report belonged to such a device and was consumed. */
int usbgamepad_report_special(uint8_t instance, const uint8_t *report, uint16_t len);
void usbgamepad_umount(uint8_t instance);

/* XInput host class feeds the same emulated DOS game port. */
void usbgamepad_xinput_report(uint16_t buttons, int16_t lx, int16_t ly, int connected);
void usbgamepad_xinput_umount(void);

/* Non-zero once any pad has delivered a report. */
int usbgamepad_connected(void);

/*
 * Merged state of every connected pad, in the form the game port wants:
 * x/y are -1, 0 or +1; buttons bit 0 = button 1, bit 1 = button 2.
 *
 * Merged rather than per-slot because a DOS game port presents one
 * stick; two pads pressing at once is not a case worth arbitrating.
 */
void usbgamepad_get(int *x, int *y, uint8_t *buttons);

#endif /* USBGAMEPAD_H */
