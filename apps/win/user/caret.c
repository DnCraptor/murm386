/*
 * caret.c - the text caret: one per system, XOR-drawn, blinking.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "user.h"

static struct {
    HWND     owner;
    int      w, h, x, y;
    int      hide;          /* HideCaret count; created hidden (1) */
    int      paint_hide;    /* hidden while the owner paints */
    int      on;            /* currently drawn */
    uint32_t next;
    UINT     blink;
} C = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 500 };

static void toggle(void)
{
    if (!C.owner || !IsWindow(C.owner))
        return;
    HDC dc = GetDC(C.owner);
    PatBlt(dc, C.x, C.y, C.w, C.h, DSTINVERT);
    ReleaseDC(C.owner, dc);
    C.on ^= 1;
}

static int visible(void) { return C.owner && !C.hide && !C.paint_hide; }

static void off(void)
{
    if (C.on)
        toggle();
}

static void on_now(void)
{
    if (visible() && !C.on)
        toggle();
    C.next = GetTickCount() + C.blink;
}

void WINAPI CreateCaret(HWND h, HBITMAP bmp, int w, int hh)
{
    (void)bmp;
    DestroyCaret();
    C.owner = h;
    C.w = w > 0 ? w : 1;
    C.h = hh > 0 ? hh : 16;
    C.x = C.y = 0;
    C.hide = 1;
    C.paint_hide = 0;
    C.on = 0;
}

void WINAPI DestroyCaret(void)
{
    off();
    C.owner = 0;
}

void WINAPI HideCaret(HWND h)
{
    if (!C.owner || (h && h != C.owner))
        return;
    off();
    C.hide++;
}

void WINAPI ShowCaret(HWND h)
{
    if (!C.owner || (h && h != C.owner))
        return;
    if (C.hide > 0)
        C.hide--;
    on_now();
}

void WINAPI SetCaretPos(int x, int y)
{
    if (!C.owner)
        return;
    off();
    C.x = x;
    C.y = y;
    on_now();
}

void WINAPI GetCaretPos(LPPOINT pt)
{
    pt->x = C.x;
    pt->y = C.y;
}

void WINAPI SetCaretBlinkTime(UINT ms) { C.blink = ms < 50 ? 50 : ms; }
UINT WINAPI GetCaretBlinkTime(void) { return C.blink; }

void user_caret_hide_for_paint(wwnd *w)
{
    if (!C.owner || w->h != C.owner)
        return;
    off();
    C.paint_hide++;
}

void user_caret_restore(wwnd *w)
{
    if (!C.owner || w->h != C.owner || !C.paint_hide)
        return;
    C.paint_hide--;
    on_now();
}

void user_caret_tick(uint32_t now)
{
    if (!C.owner)
        return;
    if (!IsWindow(C.owner)) {
        C.owner = 0;
        C.on = 0;
        return;
    }
    if (!visible() || (int32_t)(now - C.next) < 0)
        return;
    toggle();
    C.next = now + C.blink;
}
