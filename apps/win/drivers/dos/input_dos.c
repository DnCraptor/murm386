/*
 * input_dos.c - input driver: native keyboard events, INT 33h mouse,
 * time from the emulator's microsecond counter.
 *
 * Keyboard events arrive as Linux input keycodes; codes 1..95 equal the
 * PC set 1 scancodes, the extended keys are translated by a table.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "dos.h"
#include "dos_keyboard.h"
#include "dos_yield.h"
#include "windows.h"
#include "winddi.h"

/* Set 1 scancode -> virtual key, keys 0x00..0x58. */
static const uint8_t sc_vk[0x59] = {
    0, VK_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xBD, 0xBB, VK_BACK, VK_TAB,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0xDB, 0xDD, VK_RETURN, VK_CONTROL, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', 0xBA, 0xDE, 0xC0, VK_SHIFT, 0xDC, 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', 0xBC, 0xBE, 0xBF, VK_SHIFT, VK_MULTIPLY, VK_MENU, VK_SPACE, VK_CAPITAL,
    VK_F1, VK_F1 + 1, VK_F1 + 2, VK_F1 + 3, VK_F1 + 4,
    VK_F1 + 5, VK_F1 + 6, VK_F1 + 7, VK_F1 + 8, VK_F10, VK_NUMLOCK, VK_SCROLL,
    VK_NUMPAD0 + 7, VK_NUMPAD0 + 8, VK_NUMPAD0 + 9, VK_SUBTRACT,
    VK_NUMPAD0 + 4, VK_NUMPAD0 + 5, VK_NUMPAD0 + 6, VK_ADD,
    VK_NUMPAD0 + 1, VK_NUMPAD0 + 2, VK_NUMPAD0 + 3, VK_NUMPAD0, VK_DECIMAL,
    0, 0, 0xE2, VK_F11, VK_F12,
};

/* Linux keycodes 96..127 -> (vk, set 1 scancode with 0x100 = extended). */
static const struct { uint8_t vk; uint16_t sc; } ext_keys[32] = {
    { VK_RETURN, 0x11C }, { VK_CONTROL, 0x11D }, { VK_DIVIDE, 0x135 }, { VK_SNAPSHOT, 0x137 },
    { VK_MENU, 0x138 }, { 0, 0 }, { VK_HOME, 0x147 }, { VK_UP, 0x148 },
    { VK_PRIOR, 0x149 }, { VK_LEFT, 0x14B }, { VK_RIGHT, 0x14D }, { VK_END, 0x14F },
    { VK_DOWN, 0x150 }, { VK_NEXT, 0x151 }, { VK_INSERT, 0x152 }, { VK_DELETE, 0x153 },
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { VK_PAUSE, 0x45 },
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0x5B, 0x15B }, { 0x5C, 0x15C }, { 0x5D, 0x15D },
};

/* The microsecond counter wraps every ~71 minutes: accumulate deltas. */
static uint32_t last_us, ms_acc, us_rem;
static uint32_t update_ticks(void);

static int has_mouse;
static int last_x = -1, last_y = -1;
static unsigned last_btn;

static int dos_in_enable(int w, int h)
{
    union REGS r = { 0 };
    r.w.ax = 0x0000;
    int386(0x33, &r, &r);
    has_mouse = r.w.ax == 0xFFFF;
    if (has_mouse) {
        r.w.ax = 0x0007; r.w.cx = 0; r.w.dx = (uint16_t)(w - 1);
        int386(0x33, &r, &r);
        r.w.ax = 0x0008; r.w.cx = 0; r.w.dx = (uint16_t)(h - 1);
        int386(0x33, &r, &r);
        r.w.ax = 0x0004; r.w.cx = (uint16_t)(w / 2); r.w.dx = (uint16_t)(h / 2);
        int386(0x33, &r, &r);
    }
    last_us = dos_yield();
    /* drop keys typed before start */
    dos_keyboard_event_t ev;
    while (dos_keyboard_get_event(&ev, DOS_KEYBOARD_EVENT_CONSUME) > 0)
        ;
    return 0;
}

static void dos_in_disable(void)
{
    if (has_mouse) {
        union REGS r = { 0 };
        r.w.ax = 0x0000;
        int386(0x33, &r, &r);
    }
}

static void dos_in_poll(void)
{
    update_ticks();
    dos_keyboard_event_t ev;
    while (dos_keyboard_get_event(&ev, DOS_KEYBOARD_EVENT_CONSUME) > 0) {
        int code = ev.keycode, vk = 0, sc = 0;
        if (code > 0 && code < (int)sizeof(sc_vk)) {
            vk = sc_vk[code];
            sc = code;
        } else if (code >= 96 && code <= 127) {
            vk = ext_keys[code - 96].vk;
            sc = ext_keys[code - 96].sc;
        }
        if (vk)
            win_ddi_key_event(vk, sc, ev.is_down);
    }
    if (has_mouse) {
        union REGS r = { 0 };
        r.w.ax = 0x0003;
        int386(0x33, &r, &r);
        int x = (int16_t)r.w.cx, y = (int16_t)r.w.dx;
        unsigned b = r.w.bx & 7;
        if (x != last_x || y != last_y || b != last_btn) {
            last_x = x; last_y = y; last_btn = b;
            win_ddi_mouse_event(x, y, b);
        }
    }
}


/* dos_yield() services the emulator (devices, guest IRQs such as the
 * mouse); it runs once per poll/idle and the time is cached in between, so
 * GetTickCount() stays cheap. */
static uint32_t update_ticks(void)
{
    uint32_t now = dos_yield();
    us_rem += now - last_us;
    last_us = now;
    ms_acc += us_rem / 1000u;
    us_rem %= 1000u;
    return ms_acc;
}

static uint32_t dos_in_ticks(void) { return ms_acc; }
static void dos_in_idle(void) { (void)update_ticks(); }

const win_input_driver win_input_dos = {
    WINDDI_VERSION, "DOS", dos_in_enable, dos_in_disable, dos_in_poll,
    dos_in_ticks, dos_in_idle,
};
