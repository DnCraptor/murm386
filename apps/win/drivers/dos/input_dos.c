/*
 * input_dos.c - input driver: keyboard through the guest BIOS, INT 33h
 * mouse, time from the emulator's microsecond counter.
 *
 * Keys are taken from the BIOS keyboard buffer (INT 16h) and the shift
 * flags at 40:17h, i.e. after the firmware has seen them. Reading the raw
 * host queue (dos_keyboard_get_event) would race the firmware for every
 * key: its hot keys (Win+F11 and others) and the NumPad mouse would lose
 * the keys we took. The BIOS gives no key releases, so every key is
 * reported as a press immediately followed by a release; Shift, Ctrl and
 * Alt follow the flag bits and are exact.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "dos.h"
#include "dos_guest_ptr.h"
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

/* Extended BIOS codes (AL = 0 or E0h) -> virtual key; 0x100 marks keys
 * that come from the grey block (E0 prefix). */
static int ext_vk(uint8_t sc, uint8_t al, int *xsc)
{
    static const struct { uint8_t sc; uint8_t vk; } nav[] = {
        { 0x47, VK_HOME }, { 0x48, VK_UP }, { 0x49, VK_PRIOR }, { 0x4B, VK_LEFT },
        { 0x4D, VK_RIGHT }, { 0x4F, VK_END }, { 0x50, VK_DOWN }, { 0x51, VK_NEXT },
        { 0x52, VK_INSERT }, { 0x53, VK_DELETE }, { 0x4C, VK_CLEAR },
        /* Ctrl+ and Alt+ variants of the same keys */
        { 0x77, VK_HOME }, { 0x8D, VK_UP }, { 0x84, VK_PRIOR }, { 0x73, VK_LEFT },
        { 0x74, VK_RIGHT }, { 0x75, VK_END }, { 0x91, VK_DOWN }, { 0x76, VK_NEXT },
        { 0x92, VK_INSERT }, { 0x93, VK_DELETE },
        { 0x97, VK_HOME }, { 0x98, VK_UP }, { 0x99, VK_PRIOR }, { 0x9B, VK_LEFT },
        { 0x9D, VK_RIGHT }, { 0x9F, VK_END }, { 0xA0, VK_DOWN }, { 0xA1, VK_NEXT },
        { 0xA2, VK_INSERT }, { 0xA3, VK_DELETE },
        { 0x94, VK_TAB }, { 0xA5, VK_TAB }, { 0xA6, VK_RETURN }, { 0xA4, VK_DIVIDE },
        { 0x85, VK_F11 }, { 0x86, VK_F12 }, { 0x87, VK_F11 }, { 0x88, VK_F12 },
        { 0x89, VK_F11 }, { 0x8A, VK_F12 }, { 0x8B, VK_F11 }, { 0x8C, VK_F12 },
    };
    *xsc = (al == 0xE0) ? 0x100 : 0;
    for (unsigned i = 0; i < sizeof(nav) / sizeof(nav[0]); i++)
        if (nav[i].sc == sc)
            return nav[i].vk;
    if (sc >= 0x54 && sc <= 0x71)                 /* Shift/Ctrl/Alt + F1..F10 */
        return VK_F1 + (sc - 0x54) % 10;
    if (sc >= 0x78 && sc <= 0x83) {               /* Alt + 1..0 - = */
        static const uint8_t d[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xBD, 0xBB };
        return d[sc - 0x78];
    }
    if (sc < sizeof(sc_vk))
        return sc_vk[sc];
    return 0;
}

/* The microsecond counter wraps every ~71 minutes: accumulate deltas. */
static uint32_t last_us, ms_acc, us_rem;
static uint32_t update_ticks(void);

static int has_mouse;
static uint8_t kb_flags;        /* last 40:17h seen */
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
    kb_flags = 0x20;            /* what USER starts with: NumLock on */
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
    volatile uint8_t *bda = (volatile uint8_t *)dos_guest_far_ptr(0x40, 0);
    uint8_t f = bda[0x17], ch = f ^ kb_flags;
    /* lock keys: one press toggles USER's state */
    if (ch & 0x20) { win_ddi_key_event(VK_NUMLOCK, 0x45, 1); win_ddi_key_event(VK_NUMLOCK, 0x45, 0); }
    if (ch & 0x40) { win_ddi_key_event(VK_CAPITAL, 0x3A, 1); win_ddi_key_event(VK_CAPITAL, 0x3A, 0); }
    if (ch & 0x10) { win_ddi_key_event(VK_SCROLL, 0x46, 1); win_ddi_key_event(VK_SCROLL, 0x46, 0); }
    /* modifiers going down before the keys, going up after them */
    if ((ch & 0x03) && (f & 0x03)) win_ddi_key_event(VK_SHIFT, 0x2A, 1);
    if ((ch & 0x04) && (f & 0x04)) win_ddi_key_event(VK_CONTROL, 0x1D, 1);
    if ((ch & 0x08) && (f & 0x08)) win_ddi_key_event(VK_MENU, 0x38, 1);

    while (*(volatile uint16_t *)&bda[0x1A] != *(volatile uint16_t *)&bda[0x1C]) {
        union REGS r = { 0 };
        r.h.ah = 0x10;
        int386(0x16, &r, &r);
        uint8_t sc = r.h.ah, al = r.h.al;
        int vk, xsc = 0;
        if (al == 0 || al == 0xE0) {
            vk = ext_vk(sc, al, &xsc);
        } else if (sc == 0xE0) {              /* grey Enter and / */
            vk = al == '/' ? VK_DIVIDE : VK_RETURN;
            xsc = 0x100;
            sc = al == '/' ? 0x35 : 0x1C;
        } else if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E &&
                   ((al >= '0' && al <= '9') || al == '.' || al == ',')) {
            vk = al == '.' || al == ',' ? VK_DECIMAL : VK_NUMPAD0 + (al - '0');
        } else {
            vk = sc < sizeof(sc_vk) ? sc_vk[sc] : 0;
        }
        if (!vk)
            continue;
        win_ddi_key_event(vk, sc | xsc, 1);
        win_ddi_key_event(vk, sc | xsc, 0);
    }

    if ((ch & 0x08) && !(f & 0x08)) win_ddi_key_event(VK_MENU, 0x38, 0);
    if ((ch & 0x04) && !(f & 0x04)) win_ddi_key_event(VK_CONTROL, 0x1D, 0);
    if ((ch & 0x03) && !(f & 0x03)) win_ddi_key_event(VK_SHIFT, 0x2A, 0);
    kb_flags = f;

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
