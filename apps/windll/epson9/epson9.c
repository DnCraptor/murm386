/*
 * epson9.c - EPSON9.DLL, WINN's printer driver for Epson's 9-pin dot
 * matrix printers (ESC/P: LX-350, LX-300, FX-850, FX-1050 ...)
 * (include/printer_driver.h). A page at 72 x 72 dots an inch, 8 inches
 * wide by 11 long (continuous paper, 80 columns), is printed as 8-dot
 * bands of the one-to-one bit image (ESC * 5, 72 dots an inch), 8/72 inch
 * between them; blank bands are line feeds, the white at the end of a band
 * is not sent.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stddef.h>
#include "windows.h"
#include "windll.h"
#include "printer_driver.h"

#define DPI     72
#define WIDTH   (8 * DPI)               /* 576 dots: 80 columns at 10 a inch */
#define HEIGHT  (11 * DPI)              /* 792 */

static const char *const models[] = {
    "Epson LX-350", "Epson LX-300", "Epson LX-800", "Epson FX-850", "Epson FX-1050",
    "Epson 9-pin (generic)", NULL,
};

static int put(const win_printer_out *out, const void *p, unsigned n)
{
    return out->write(out->ctx, p, n);
}

static int start_doc(const win_printer_out *out, const char *model)
{
    (void)model;
    static const uint8_t init[] = {
        0x1b, '@',                      /* ESC @: the printer's defaults */
        0x1b, 'C', 0, 11,               /* ESC C 0 11: pages 11 inches long */
    };
    return put(out, init, sizeof(init));
}

/* a dot of the page: black (the bitmap's 0) */
static int black(const uint8_t *bits, int pitch, int x, int y)
{
    return !((bits[y * pitch + (x >> 3)] >> (7 - (x & 7))) & 1);
}

static int page(const win_printer_out *out, const uint8_t *bits, int pitch, int width, int height)
{
    static const uint8_t spacing[] = { 0x1b, '3', 24 };    /* ESC 3 24: 24/216 = 8/72 inch */
    static uint8_t col[WIDTH];
    if (width > WIDTH)
        width = WIDTH;
    if (!put(out, spacing, sizeof(spacing)))
        return 0;
    unsigned feeds = 0;
    for (int y = 0; y < height; y += 8) {
        int last = -1;
        for (int x = 0; x < width; x++) {
            uint8_t b = 0;
            for (int k = 0; k < 8 && y + k < height; k++)
                if (black(bits, pitch, x, y + k))
                    b |= (uint8_t)(0x80 >> k);      /* the top pin the most significant */
            col[x] = b;
            if (b)
                last = x;
        }
        if (last < 0) {
            feeds++;
            continue;
        }
        while (feeds) {                 /* the blank bands before it */
            static const uint8_t lf = '\n';
            if (!put(out, &lf, 1))
                return 0;
            feeds--;
        }
        unsigned n = (unsigned)last + 1;
        uint8_t head[5] = { 0x1b, '*', 5, (uint8_t)n, (uint8_t)(n >> 8) };
        static const uint8_t crlf[2] = { '\r', '\n' };
        if (!put(out, head, sizeof(head)) || !put(out, col, n) || !put(out, crlf, 2))
            return 0;
    }
    static const uint8_t ff = '\f';     /* the next page's top */
    return put(out, &ff, 1);
}

static int end_doc(const win_printer_out *out, int aborted)
{
    static const uint8_t reset[] = { '\r', 0x1b, '@' };
    (void)aborted;
    return put(out, reset, sizeof(reset));
}

static const win_printer_driver driver = {
    WIN_PRINTER_DRIVER_VERSION, sizeof(win_printer_driver), "Epson 9-pin (ESC/P)", models,
    DPI, DPI, WIDTH, HEIGHT, start_doc, page, end_doc,
};

static const win_printer_driver *entry(void) { return &driver; }

static const win_dll_export exports[] = {
    { 1, 0, "WinPrinterDriver", (const void *)entry },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "EPSON9",
    exports, sizeof(exports) / sizeof(exports[0]), NULL, NULL, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
