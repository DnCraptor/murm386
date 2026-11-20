/* Standalone host regression test for the GDI surface, scaling and mode13. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "winddi.h"

static uint8_t memory[640 * 400 + 8];
static uint32_t memory_size = 640 * 400;
static int hw_mode = 3;
static int fail_mode;
static int palette_first, palette_count, palette_calls;
static uint8_t palette_data[256 * 3];

int vga320_test_get_mode(void) { return hw_mode; }
int vga320_test_set_mode(unsigned mode)
{
    if (fail_mode) return -1;
    hw_mode = (int)mode;
    return 0;
}
uint8_t *vga320_test_video_buffer(uint32_t *size)
{
    *size = memory_size;
    return memory;
}
void vga320_test_palette(int first, int count, const uint8_t *rgb)
{
    palette_first = first;
    palette_count = count;
    palette_calls++;
    memcpy(palette_data, rgb, (size_t)count * 3);
}

extern const win_display_driver win_disp_vga320;
static const win_display_driver *const d = &win_disp_vga320;

static int src_x(int dx) { return dx * 320 / 533; }
static int src_y(int dy) { return dy * 240 / 400; }

int main(void)
{
    /* Invalid requests and insufficient physical VRAM never switch mode. */
    assert(d->enable(640, 240, 8) != 0);
    assert(d->enable(320, 241, 8) != 0);
    memory_size = 640 * 400 - 1;
    assert(d->enable(320, 240, 8) != 0);
    assert(hw_mode == 3);
    memory_size = 640 * 400;
    fail_mode = 1;
    assert(d->enable(320, 240, 8) != 0);
    fail_mode = 0;

    assert(d->enable(320, 240, 8) == 0);
    assert(hw_mode == 0x100);
    const win_surface *s = d->surface();
    assert(s && s->width == 320 && s->height == 240 &&
           s->pitch == 320 && s->bpp == 8 && s->bits != memory);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
            s->bits[y * 320 + x] = (uint8_t)((x + y * 7) % 251);
    d->flush(0, 0, 320, 240);
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 640; ++x) {
            uint8_t value = x >= 53 && x < 53 + 533 ?
                s->bits[src_y(y) * 320 + src_x(x - 53)] : 0;
            assert(memory[y * 640 + x] == value);
        }
    /* Changing exactly one logical pixel must touch only its scaled area. */
    memset(memory, 0xA5, 640 * 400);
    s->bits[100 * 320 + 101] = 0x36;
    d->flush(101, 100, 102, 101);
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 640; ++x) {
            int mapped = x >= 53 && x < 53 + 533 &&
                src_x(x - 53) == 101 && src_y(y) == 100;
            assert(memory[y * 640 + x] == (mapped ? 0x36 : 0xA5));
        }
    /* Exercise clipped, empty and randomly sized dirty rectangles. */
    memset(memory, 0, 640 * 400);
    memset(s->bits, 0, 320 * 240);
    d->flush(0, 0, 320, 240);
    uint32_t rnd = 0x12345678u;
    for (unsigned n = 0; n < 96; ++n) {
        rnd = rnd * 1664525u + 1013904223u;
        int x0 = (int)(rnd % 320u);
        rnd = rnd * 1664525u + 1013904223u;
        int y0 = (int)(rnd % 240u);
        rnd = rnd * 1664525u + 1013904223u;
        int x1 = x0 + 1 + (int)(rnd % 40u);
        rnd = rnd * 1664525u + 1013904223u;
        int y1 = y0 + 1 + (int)(rnd % 20u);
        if (x1 > 320) x1 = 320;
        if (y1 > 240) y1 = 240;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                s->bits[y * 320 + x] = (uint8_t)((x + y + n) & 255u);
        d->flush(x0, y0, x1, y1);
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 640; ++x) {
                const uint8_t expected = x >= 53 && x < 53 + 533 ?
                    s->bits[src_y(y) * 320 + src_x(x - 53)] : 0;
                assert(memory[y * 640 + x] == expected);
            }
    }
    d->flush(-100, -100, -1, -1);
    d->flush(320, 240, 400, 500);
    d->flush(80, 40, 80, 41);
    d->flush(80, 40, 81, 40);
    assert(memory[640 * 400] == 0); /* physical framebuffer untouched */

    uint8_t rgb[6] = { 255, 120, 0, 0, 100, 255 };
    d->set_palette(12, 2, rgb);
    assert(palette_calls == 1 && palette_first == 12 && palette_count == 2);
    assert(memcmp(palette_data, rgb, sizeof(rgb)) == 0);
    d->disable();
    assert(hw_mode == 3 && d->surface() == NULL);

    /* True mode 13h, the same 320x200x8 mode Doom uses. */
    memory_size = 320 * 200;
    assert(d->enable(320, 200, 8) == 0);
    s = d->surface();
    assert(hw_mode == 0x13 && s->bits == memory && s->height == 200);
    memory[1234] = 0x5E;
    d->flush(0, 0, 320, 200);
    assert(memory[1234] == 0x5E);
    d->disable();
    assert(hw_mode == 3);

    puts("VGA320 host: PASS (320x240 scaling, rects, palette, 320x200)");
    return 0;
}
