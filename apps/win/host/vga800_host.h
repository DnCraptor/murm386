/*
 * vga800_host.h - fake BIOS and planar video memory for testing the VGA800
 * driver on the host (included by vga800.c when WINDRV_HOST is defined).
 */
#include <stdio.h>

static uint32_t fake_vram[65536];
static uint8_t fake_dac[256][3], fake_attr[16];
static int fake_mode = 3;

static uint8_t *hw_vram(uint32_t *size) { *size = sizeof(fake_vram); return (uint8_t *)fake_vram; }
static int hw_get_mode(void) { return fake_mode; }
static int hw_set_mode(int mode) { fake_mode = mode; return 0; }
static void hw_set_attr(int reg, int value) { fake_attr[reg & 15] = (uint8_t)value; }
static void hw_set_dac(int i, int r, int g, int b)
{
    fake_dac[i][0] = (uint8_t)r; fake_dac[i][1] = (uint8_t)g; fake_dac[i][2] = (uint8_t)b;
}

/* decode the bit planes the way the VGA does */
int vga800_host_snapshot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n800 600\n255\n");
    for (int y = 0; y < 600; y++)
        for (int x = 0; x < 800; x++) {
            uint32_t w = fake_vram[y * 100 + x / 8];
            int bit = 7 - (x & 7), c = 0;
            for (int p = 0; p < 4; p++)
                c |= ((w >> (8 * p + bit)) & 1) << p;
            fwrite(fake_dac[fake_attr[c]], 1, 3, f);
        }
    fclose(f);
    return 0;
}
