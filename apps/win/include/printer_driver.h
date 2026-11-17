/*
 * printer_driver.h - native printer driver ABI of WINN's GDI. A driver is
 * a native DLL (ddi/windll.h) exporting WinPrinterDriver, named in WIN.INI
 * as Windows 3.1 names its printer drivers:
 *
 *   [windows]       device=Epson LX-350,EPSON9,FILE:
 *   [devices]       Epson LX-350=EPSON9,FILE:
 *   [PrinterPorts]  Epson LX-350=EPSON9,FILE:,15,45
 *
 * GDI draws a page into a bitmap of one bit a pixel (1 white, the most
 * significant bit left, as Windows' monochrome bitmaps), at the driver's
 * resolution and size, then gives it to the driver; the driver turns it into
 * the printer's language and writes it out (the port: a file, LPTn:).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_PRINTER_DRIVER_H
#define WINN_PRINTER_DRIVER_H
#include <stdint.h>

#define WIN_PRINTER_DRIVER_VERSION 1u

typedef struct win_printer_out {
    void *ctx;
    /* the bytes to the printer: 0 if they could not be written */
    int (*write)(void *ctx, const void *data, unsigned n);
} win_printer_out;

typedef struct win_printer_driver {
    uint32_t version, size;
    const char *description;            /* "Epson 9-pin (ESC/P)" */
    const char *const *models;          /* the devices it prints, NULL ended */
    int dpi_x, dpi_y;                   /* dots an inch */
    int width, height;                  /* the printable page, dots */
    /* a document: its start, each page (rows of pitch bytes), its end
     * (aborted: what has been written stays as it is); 0 on failure */
    int (*start_doc)(const win_printer_out *out, const char *model);
    int (*page)(const win_printer_out *out, const uint8_t *bits, int pitch, int width, int height);
    int (*end_doc)(const win_printer_out *out, int aborted);
} win_printer_driver;

typedef const win_printer_driver *(*win_printer_driver_entry)(void);
#endif /* WINN_PRINTER_DRIVER_H */
