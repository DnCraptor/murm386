/*
 * planar16_dos.h - the hardware of planar16.h's driver on the target: the
 * BIOS (INT 10h: the mode, the attribute palette, the DAC) and the
 * emulator's video memory.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "dos.h"
#include "dos_video.h"

static uint8_t *hw_vram(uint32_t *size) { return dos_video_get_buffer(size); }

static int hw_get_mode(void)
{
    union REGS r = { 0 };
    r.h.ah = 0x0F;
    int386(0x10, &r, &r);
    return r.h.al & 0x7F;
}

static int hw_set_mode(int mode)
{
    union REGS r = { 0 };
    if (mode < 0x100) {
        r.w.ax = (uint16_t)mode;
        int386(0x10, &r, &r);
        return 0;
    }
    r.w.ax = 0x4F02;
    r.w.bx = (uint16_t)mode;
    int386(0x10, &r, &r);
    return r.w.ax == 0x004F ? 0 : -1;
}

/* attribute controller palette register -> DAC index */
static void hw_set_attr(int reg, int value)
{
    union REGS r = { 0 };
    r.w.ax = 0x1000;
    r.h.bl = (uint8_t)reg;
    r.h.bh = (uint8_t)value;
    int386(0x10, &r, &r);
}

static void hw_set_dac(int i, int red, int green, int blue)
{
    union REGS r = { 0 };
    r.w.ax = 0x1010;
    r.w.bx = (uint16_t)i;
    r.h.dh = (uint8_t)(red >> 2);
    r.h.ch = (uint8_t)(green >> 2);
    r.h.cl = (uint8_t)(blue >> 2);
    int386(0x10, &r, &r);
}
