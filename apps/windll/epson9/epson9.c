/*
 * epson9.c - EPSON9.DLL, WINN's printer driver for Epson's 9-pin dot
 * matrix printers (ESC/P: LX-350, LX-300, FX-850, FX-1050 ...)
 * (include/printer_driver.h). A page at 72 x 72 dots an inch, 8 inches
 * wide (80 columns) by the paper's length (Letter, A4, Legal, German
 * fanfold: continuous paper, its length set by ESC C), is printed as
 * 8-dot bands of the one-to-one bit image (ESC * 5, 72 dots an inch),
 * 8/72 inch between them; blank bands are line feeds, the white at the end
 * of a band is not sent.
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

static const char *const models[] = {
    "Epson LX-350", "Epson LX-300", "Epson LX-800", "Epson FX-850", "Epson FX-1050",
    "Epson 9-pin (generic)", NULL,
};

/* the sheets (tenths of a millimetre), what is printed of them: 8 inches
 * across in the middle, all the length (the paper goes on) */
static const win_printer_paper papers[] = {
    { DMPAPER_LETTER, "Letter 8 1/2 x 11 in", 2159, 2794, 18, 0, WIDTH, 11 * DPI },
    { DMPAPER_A4, "A4 210 x 297 mm", 2100, 2970, 9, 0, WIDTH, 2970 * DPI / 254 },
    { DMPAPER_LEGAL, "Legal 8 1/2 x 14 in", 2159, 3556, 18, 0, WIDTH, 14 * DPI },
    { DMPAPER_FANFOLD_STD_GERMAN, "German Std Fanfold 8 1/2 x 12 in", 2159, 3048, 18, 0, WIDTH,
      12 * DPI },
    { 0, NULL, 0, 0, 0, 0, 0, 0 },
};

static int put(const win_printer_out *out, const void *p, unsigned n)
{
    return out->write(out->ctx, p, n);
}

static int start_doc(const win_printer_out *out, const win_printer_job *job)
{
    int tenths = job->paper->length * 10 / 254;     /* its length, tenths of an inch */
    uint8_t init[6] = { 0x1b, '@', 0x1b, 'C', 0, 0 };   /* ESC @: the printer's defaults */
    unsigned n = 6;
    if (tenths % 10 == 0) {             /* ESC C 0 n: n inches */
        init[5] = (uint8_t)(tenths / 10);
    } else {                            /* ESC C n: n lines of a sixth of an inch */
        init[4] = (uint8_t)((job->paper->length * 60 / 254 + 5) / 10);
        n = 5;
    }
    return put(out, init, n);
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
    DPI, DPI, papers, start_doc, page, end_doc,
};

WIN_PRINTER_DRIVER_EXPORTS(epson9, "EPSON9", driver);

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "EPSON9",
    epson9_exports, sizeof(epson9_exports) / sizeof(epson9_exports[0]), epson9_ne_call,
    NULL, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
