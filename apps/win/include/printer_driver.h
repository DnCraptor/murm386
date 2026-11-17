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
 * resolution and the printable size of the paper chosen, then gives it to
 * the driver, always upright (a landscape page turned a quarter to the
 * left, as Windows' drivers print it), as many times as the copies; the
 * driver turns it into the printer's language and writes it out (the
 * port: a file, LPTn:).
 *
 * The printer's settings (the paper, the orientation) are GDI's: DEVMODE,
 * kept in WIN.INI's [<device>,<port>] section. A Windows program asks the
 * driver for them (LoadLibrary("EPSON9.DRV"), ExtDeviceMode,
 * DeviceCapabilities, DeviceMode, AdvancedSetUpDialog): the driver passes
 * these on to GDI, WIN_PRINTER_DRIVER_EXPORTS below gives it them.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_PRINTER_DRIVER_H
#define WINN_PRINTER_DRIVER_H
#include <stdint.h>

#define WIN_PRINTER_DRIVER_VERSION 2u

typedef struct win_printer_out {
    void *ctx;
    /* the bytes to the printer: 0 if they could not be written */
    int (*write)(void *ctx, const void *data, unsigned n);
} win_printer_out;

/* a paper the printer takes */
typedef struct win_printer_paper {
    int id;                             /* DMPAPER_LETTER ... */
    const char *name;                   /* "Letter 8 1/2 x 11 in" */
    int width, length;                  /* the sheet, tenths of a millimetre */
    int x, y, cx, cy;                   /* what it prints of it, dots: where, how much */
} win_printer_paper;

/* a document's settings, as the driver is told them */
typedef struct win_printer_job {
    const char *model;                  /* the device: "Epson LX-350" */
    const win_printer_paper *paper;
    int landscape;                      /* the pages come upright all the same */
    int copies;                         /* each page given this many times */
} win_printer_job;

typedef struct win_printer_driver {
    uint32_t version, size;
    const char *description;            /* "Epson 9-pin (ESC/P)" */
    const char *const *models;          /* the devices it prints, NULL ended */
    int dpi_x, dpi_y;                   /* dots an inch */
    const win_printer_paper *papers;    /* the first the default; a NULL name ends them */
    /* a document: its start, each page (rows of pitch bytes, the paper's
     * cx by cy), its end (aborted: what has been written stays as it is);
     * 0 on failure */
    int (*start_doc)(const win_printer_out *out, const win_printer_job *job);
    int (*page)(const win_printer_out *out, const uint8_t *bits, int pitch, int width, int height);
    int (*end_doc)(const win_printer_out *out, int aborted);
} win_printer_driver;

typedef const win_printer_driver *(*win_printer_driver_entry)(void);

/* The exports of a driver DLL named NAME (the module's, "EPSON9") whose
 * win_printer_driver is DRIVER: GDI's entry and Windows' four, for native
 * programs and Win16 ones (their ordinals: Windows 3.1's DDK's), in
 * <prefix>_exports, <prefix>_ne_call for its win_dll_info. */
#define WIN_PRINTER_DRIVER_EXPORTS(prefix, NAME, DRIVER)                                        \
    static const win_printer_driver *prefix##_entry(void) { return &(DRIVER); }                 \
    static int WINAPI prefix##_ext_device_mode(HWND w, HANDLE drv, DEVMODE *out, LPCSTR dev,    \
                                               LPCSTR port, const DEVMODE *in, LPCSTR profile,  \
                                               UINT m)                                          \
    {                                                                                           \
        (void)drv;                                                                              \
        return PrinterDeviceModeN(w, NAME, out, dev, port, in, profile, m);                     \
    }                                                                                           \
    static DWORD WINAPI prefix##_device_caps(LPCSTR dev, LPCSTR port, UINT index, LPSTR out,    \
                                             const DEVMODE *dm)                                 \
    {                                                                                           \
        return PrinterCapabilitiesN(NAME, dev, port, index, out, dm);                           \
    }                                                                                           \
    static void WINAPI prefix##_device_mode(HWND w, HANDLE drv, LPCSTR dev, LPCSTR port)        \
    {                                                                                           \
        (void)drv;                                                                              \
        PrinterDeviceModeN(w, NAME, NULL, dev, port, NULL, NULL, DM_PROMPT | DM_UPDATE);        \
    }                                                                                           \
    static LONG WINAPI prefix##_advanced(HWND w, HANDLE drv, const DEVMODE *in, DEVMODE *out)   \
    {                                                                                           \
        (void)drv;                                                                              \
        return PrinterDeviceModeN(w, NAME, out, in ? in->dmDeviceName : NULL, NULL, in, NULL,   \
                                  DM_PROMPT | DM_COPY | DM_MODIFY);                             \
    }                                                                                           \
    static int prefix##_ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *value)             \
    {                                                                                           \
        return PrinterNeCallN(NAME, c, ordinal, (DWORD *)value);                                \
    }                                                                                           \
    static const win_dll_export prefix##_exports[] = {                                          \
        { 13, 12, "DeviceMode", (const void *)prefix##_device_mode },                           \
        { 90, 26, "ExtDeviceMode", (const void *)prefix##_ext_device_mode },                    \
        { 91, 18, "DeviceCapabilities", (const void *)prefix##_device_caps },                   \
        { 93, 12, "AdvancedSetUpDialog", (const void *)prefix##_advanced },                     \
        { 200, 0, "WinPrinterDriver", (const void *)prefix##_entry },                           \
    }
#endif /* WINN_PRINTER_DRIVER_H */
