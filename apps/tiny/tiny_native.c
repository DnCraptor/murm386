/* Native 80x25 text UI for TINY, via the existing murm386 DOS services. */
#include <stdarg.h>
#include <stdint.h>
#include "tiny_native.h"
#include "dos_video.h"
#include "dos_phys.h"
#include "ez.h"
#include "dos_api_version.h"
#include "stdio.h"
#include "native_process.h"

/* Same process requirements as apps/doom (i_process.c): 64 KiB ARM stack,
 * 4 KiB DOS stack for nested BIOS/DOS callbacks.  Both records are provided,
 * as doom does, so the EZ header and the DOS process record agree. */
const native_ez_process_requirements __native_ez_process_requirements = {
    64u * 1024u,  /* ARM stack in the native PSRAM stack arena */
     4u * 1024u,  /* DOS stack for nested BIOS/DOS callbacks */
    DOS_API_VERSION,
};

static native_dos_process_requirements tiny_process_requirements = {
    sizeof(native_dos_process_requirements),
    64u * 1024u,  /* native ARM stack */
     4u * 1024u,  /* guest DOS stack */
    0,
    0
};

native_dos_process_requirements *__native_dos_process_requirements(void)
{
    return &tiny_process_requirements;
}

#define SCREEN_W 80
#define SCREEN_H 25
#define TEXT_BASE 0xB8000u

static volatile uint8_t *video;
static int left = 1, top = 1, right = SCREEN_W, bottom = SCREEN_H;
static int cx = 1, cy = 1, attr = 7;
static uint32_t rng = 1;

/* The VGA text renderer reads each cell from bytes 4*n, 4*n+1 of
 * gfx_buffer (see vga_text_refresh), not from a packed 2-byte array.
 * The other two bytes belong to the unused planes. */
static void refresh_video(void)
{
    uint32_t bytes = 0;
    uint8_t *buffer = dos_video_get_buffer(&bytes);
    video = (buffer && bytes >= SCREEN_W * SCREEN_H * 4) ? buffer : 0;
}

static uint16_t cell_at(int x, int y)
{
    uint32_t cell = (uint32_t)(y - 1) * SCREEN_W + (unsigned)(x - 1);
    if (video) {
        uint32_t off = cell * 4u;
        return (uint16_t)(video[off] | ((uint16_t)video[off + 1] << 8));
    }
    return dos_phys_read16(TEXT_BASE + cell * 2u);
}

static void set_cell(int x, int y, uint16_t cell)
{
    uint32_t index = (uint32_t)(y - 1) * SCREEN_W + (unsigned)(x - 1);
    if (video) {
        uint32_t off = index * 4u;
        video[off] = (uint8_t)cell;
        video[off + 1] = (uint8_t)(cell >> 8);
    } else {
        dos_phys_write16(TEXT_BASE + index * 2u, cell);
    }
}

static void scroll_window(void)
{
    int x, y;
    for (y = top; y < bottom; ++y)
        for (x = left; x <= right; ++x)
            set_cell(x, y, cell_at(x, y + 1));
    for (x = left; x <= right; ++x)
        set_cell(x, bottom, (uint16_t)((attr << 8) | ' '));
}

static void next_row(void)
{
    cx = 1;
    ++cy;
    if (cy > bottom - top + 1) {
        scroll_window();
        cy = bottom - top + 1;
    }
}

clock_t tiny_clock(void)
{
    return dos_yield();
}

unsigned long tiny_time(void *unused)
{
    (void)unused;
    return dos_yield();
}

void tiny_srand(unsigned seed)
{
    rng = seed ? (uint32_t)seed : 1u;
}

int tiny_rand(void)
{
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) & 0x7fffu);
}

int tiny_fgetc(FILE *fp)
{
    unsigned char c;
    return fread(&c, 1, 1, fp) == 1 ? c : EOF;
}

/* DOS AH=3Fh on CON enters FreeDOS read_line()->read_char_sft_dev().
 * The device-busy loop only calls DosIdle_int() for AH < 0Dh; with AH=3Fh
 * it blocks core0 without polling the host keyboard. Use the native
 * cooperative BIOS keyboard API for CON, preserving DOS read() for redirected
 * STDIN (files/pipes). INT 21h/AX=4400h reports bit 7 for character devices. */
static int tiny_stdin_is_device(void)
{
    union REGS r = {0};
    r.w.ax = 0x4400;
    r.w.bx = 0;                  /* STDIN handle */
    int386(0x21, &r, &r);
    return !r.x.cflag && (r.w.dx & 0x0080u) != 0;
}

char *tiny_fgets(char *dst, size_t len)
{
    static int stdin_device = -1;
    size_t n = 0;

    if (!dst || len < 2)
        return NULL;
    if (stdin_device < 0)
        stdin_device = tiny_stdin_is_device();

    if (!stdin_device) {
        char c;
        int rc;
        while (n < len - 1) {
            rc = read(0, &c, 1);
            if (rc <= 0) break;
            if (c == '\r') continue;
            dst[n++] = c;
            if (c == '\n') break;
        }
    } else {
        /* getch() polls kbhit(), which calls dos_yield() while idle. */
        while (n < len - 1) {
            int c = getch();
            if (c == 0) {            /* discard extended scan code */
                (void)getch();
                continue;
            }
            if (c == 3 || c == 26)  /* Ctrl+C / Ctrl+Z */
                return NULL;
            if (c == '\b') {
                if (n != 0) {
                    --n;
                    fputs("\b \b", stdout);
                }
                continue;
            }
            if (c == '\r' || c == '\n') {
                dst[n++] = '\n';
                putchar('\n');
                break;
            }
            if (c >= 32 && c <= 126) {
                dst[n++] = (char)c;
                putchar(c);
            }
        }
    }
    if (!n) return NULL;
    dst[n] = 0;
    return dst;
}

void tiny_init_screen(void)
{
    refresh_video();
}

void tiny_window(int x1, int y1, int x2, int y2)
{
    if (x1 < 1 || y1 < 1 || x2 > SCREEN_W || y2 > SCREEN_H ||
        x2 < x1 || y2 < y1) return;
    left = x1; top = y1; right = x2; bottom = y2;
    cx = cy = 1;
}

void tiny_gotoxy(int x, int y)
{
    if (x >= 1 && x <= right - left + 1 &&
        y >= 1 && y <= bottom - top + 1) {
        cx = x; cy = y;
    }
}

int tiny_wherex(void) { return cx; }
int tiny_wherey(void) { return cy; }
void tiny_textattr(int value) { attr = value & 255; }

int tiny_putch(int ch)
{
    if (ch == '\r') { cx = 1; return ch; }
    if (ch == '\n') { next_row(); return ch; }
    if (ch == '\b') { if (cx > 1) --cx; return ch; }
    set_cell(left + cx - 1, top + cy - 1,
             (uint16_t)((attr << 8) | (unsigned char)ch));
    if (++cx > right - left + 1) next_row();
    return ch;
}

int tiny_cputs(const char *s)
{
    while (*s) tiny_putch((unsigned char)*s++);
    return 0;
}

int tiny_cprintf(const char *fmt, ...)
{
    char b[128];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    tiny_cputs(b);
    return n;
}

void tiny_clrscr(void)
{
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            set_cell(x, y, (uint16_t)((attr << 8) | ' '));
    cx = cy = 1;
}

int tiny_gettext(int x1, int y1, int x2, int y2, void *dst)
{
    unsigned char *p = (unsigned char *)dst;
    if (!p || x1 < 1 || y1 < 1 || x2 > 80 || y2 > 25 || x2 < x1 || y2 < y1)
        return 0;
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x) {
            uint16_t cell = cell_at(x, y);
            *p++ = (unsigned char)cell;
            *p++ = (unsigned char)(cell >> 8);
        }
    return 1;
}

void tiny_gettextinfo(struct text_info *info)
{
    union REGS r = {0};
    r.h.ah = 0x0f;
    int386(0x10, &r, &r);
    info->currmode = r.h.al;
}

void tiny_textmode(int mode)
{
    union REGS r = {0};
    r.h.ah = 0;
    r.h.al = (uint8_t)mode;
    int386(0x10, &r, &r);
    refresh_video();
    tiny_window(1, 1, 80, 25);
}

void tiny_sync_cursor(void)
{
    union REGS r = {0};
    r.h.ah = 0x02;
    r.h.bh = 0;
    r.h.dh = (uint8_t)(top + cy - 2);
    r.h.dl = (uint8_t)(left + cx - 2);
    int386(0x10, &r, &r);
}

static unsigned from_bcd(unsigned c)
{
    return ((c >> 4) & 15u) * 10u + (c & 15u);
}

void tiny_gettime(struct time *t)
{
    union REGS r = {0};
    r.h.ah = 0x02;
    int386(0x1a, &r, &r);
    t->ti_hour = (uint8_t)from_bcd(r.h.ch);
    t->ti_min  = (uint8_t)from_bcd(r.h.cl);
    t->ti_sec  = (uint8_t)from_bcd(r.h.dh);
    t->ti_hund = 0;
}

void tiny_delay(unsigned ms)
{
    uint32_t start = dos_yield(), usec = (uint32_t)ms * 1000u;
    while ((uint32_t)(dos_yield() - start) < usec) { }
}

void tiny_sound(unsigned hz)
{
    uint32_t divisor;
    uint8_t control;
    if (!hz) { tiny_nosound(); return; }
    divisor = 1193182u / hz;
    if (divisor < 1u) divisor = 1u;
    if (divisor > 65535u) divisor = 65535u;
    outp(0x43, 0xb6);
    outp(0x42, (uint8_t)divisor);
    outp(0x42, (uint8_t)(divisor >> 8));
    control = inp(0x61);
    outp(0x61, (uint8_t)(control | 3u));
}

void tiny_nosound(void)
{
    outp(0x61, (uint8_t)(inp(0x61) & (uint8_t)~3u));
}
