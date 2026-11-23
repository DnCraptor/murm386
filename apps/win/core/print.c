/*
 * print.c - printing: printers' DCs on native printer drivers
 * (include/printer_driver.h). CreateDC("EPSON9", "Epson LX-350", "FILE:")
 * loads the driver (EPSON9.DLL) and gives a DC whose surface is the page,
 * a monochrome bitmap at the driver's resolution; StartDoc opens the port,
 * EndPage hands the page to the driver (and clears it for the next), EndDoc
 * closes it. The page is the paper's printable part in the settings
 * (DEVMODE: the paper, the orientation, the copies; WIN.INI's, the
 * program's), ResetDC changes them between pages. Windows 3.0's escapes (STARTDOC, NEWFRAME, NEXTBAND, ENDDOC
 * ...) the same.
 *
 * Ports: FILE: asks for a file's name (Windows' "Print To File"), LPTn:,
 * COMn: and PRN are DOS's devices, any other a file of that name.
 *
 * The spooler (WIN.INI [windows] Spooler=yes, Windows' default): a job for
 * a device (LPT1: ...) goes to a file in TEMP (~SPLnnnn.TMP) as it is
 * printed and, ended, to the queue; WINN copies the queue's jobs to their
 * ports while the system is idle (print_spool_idle), one at a time, as
 * Print Manager does. A port that fails asks to retry or cancel. GDI's
 * spooler functions (OpenJob, WriteSpool, CloseJob, SpoolFile ...: the
 * printer drivers') make jobs of the same queue. Spooler=no, or FILE: or
 * a file: straight to the port.
 *
 * SetAbortProc's procedure is asked at each page (and by QueryAbort):
 * FALSE ends the job (SP_APPABORT).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "windll.h"
#include "printer_driver.h"

typedef struct wprinter {
    HMODULE module;
    const win_printer_driver *drv;
    char model[64], port[128];
    DEVMODE dm;                         /* its settings */
    const win_printer_paper *paper;
    int land, copies;
    int pw, ph;                         /* the page as it is drawn (landscape: across) */
    HBITMAP page;
    int ic;
    int in_doc, band;               /* a document begun; NEXTBAND's state */
    int restart;                        /* another paper: the driver told before the page */
    HFILE out;
    struct spjob *job;                  /* spooled: the queue's job it writes */
    ABORTPROC abort;                    /* SetAbortProc's */
    int failed;
    unsigned used;
    uint8_t buf[512];
    win_printer_out sink;
} wprinter;

_Static_assert(sizeof(DEVMODE) == 68, "DEVMODE: Windows 3.1's, 68 bytes");

/* ---------------------------------------------------------------- drivers */

/* "EPSON9", "EPSON9.DRV", "C:\WINDOWS\EPSON9.DRV": the native DLL's */
static const win_printer_driver *driver_load(LPCSTR name, HMODULE *module)
{
    char m[16];
    const char *base = name;
    *module = 0;
    if (!name)
        return NULL;
    for (const char *p = name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    int n = 0;
    while (base[n] && base[n] != '.' && n < 8) {
        m[n] = base[n];
        n++;
    }
    if (!n || !lstrcmpi(base, "DISPLAY"))
        return NULL;
    lstrcpy(m + n, ".DLL");
    HMODULE h = LoadLibrary(m);
    if ((UINT)(uintptr_t)h < 32u)
        return NULL;
    win_printer_driver_entry entry =
        (win_printer_driver_entry)(void (*)(void))GetProcAddress(h, "WinPrinterDriver");
    const win_printer_driver *d = entry ? entry() : NULL;
    int ok = d && d->version == WIN_PRINTER_DRIVER_VERSION && d->size >= sizeof(*d) &&
             d->start_doc && d->page && d->end_doc && d->dpi_x > 0 && d->dpi_y > 0 &&
             d->papers && d->papers[0].name;
    for (const win_printer_paper *p = ok ? d->papers : NULL; p && p->name; p++)
        if (p->cx <= 0 || p->cy <= 0 || p->cx > 4096 || p->cy > 4096 || p->width <= 0 ||
            p->length <= 0)
            ok = 0;
    if (!ok) {
        FreeLibrary(h);
        return NULL;
    }
    *module = h;
    return d;
}

static const win_printer_paper *paper_find(const win_printer_driver *d, int id)
{
    for (const win_printer_paper *p = d->papers; p->name; p++)
        if (p->id == id)
            return p;
    return NULL;
}

static int paper_count(const win_printer_driver *d)
{
    int n = 0;
    while (d->papers[n].name)
        n++;
    return n;
}

int WINAPI GetPrinterModelsN(LPCSTR driver, LPSTR buf, int size, LPSTR desc, int descsize)
{
    HMODULE h;
    const win_printer_driver *d = driver_load(driver, &h);
    if (!d)
        return 0;
    int at = 0, n = 0;
    for (const char *const *m = d->models; m && *m && buf; m++, n++) {
        int len = lstrlen(*m) + 1;
        if (at + len + 1 > size)
            break;
        lstrcpy(buf + at, *m);
        at += len;
    }
    if (buf && size > 0)
        buf[at < size ? at : size - 1] = 0;
    if (desc && descsize > 0)
        lstrcpyn(desc, d->description ? d->description : "", descsize);
    FreeLibrary(h);
    return n ? n : 1;
}

int WINAPI GetPrintersN(LPSTR buf, int size, LPSTR def, int defsize)
{
    static char keys[1024];
    char value[128];
    int n = GetProfileString("devices", NULL, "", keys, sizeof(keys)), at = 0, count = 0;
    for (const char *k = keys; n > 0 && *k && k < keys + n; k += lstrlen(k) + 1) {
        GetProfileString("devices", k, "", value, sizeof(value));
        int len = lstrlen(k) + 1 + lstrlen(value) + 1;
        if (!buf || at + len + 1 > size)
            break;
        wsprintf(buf + at, "%s=%s", k, value);
        at += len;
        count++;
    }
    if (buf && size > 0)
        buf[at < size ? at : size - 1] = 0;
    if (def && defsize > 0)
        GetProfileString("windows", "device", "", def, defsize);
    return count;
}

/* ---------------------------------------------------------------- settings */

/* the device's model (the driver's first if none) */
static void model_of(const win_printer_driver *d, LPCSTR device, char *out, int size)
{
    lstrcpyn(out, device && *device ? device : (d->models && d->models[0] ? d->models[0] : ""),
             size);
}

/* the port given, else the device's in WIN.INI ("EPSON9,FILE:"), else FILE: */
static void port_of(LPCSTR device, LPCSTR port, char *out, int size)
{
    out[0] = 0;
    if (port && *port)
        lstrcpyn(out, port, size);
    else if (device && *device)
        GetProfileString("devices", device, "", out, size);
    char *comma = strchr(out, ',');
    if (comma && !(port && *port))
        memmove(out, comma + 1, strlen(comma + 1) + 1);
    if (!out[0])
        lstrcpyn(out, "FILE:", size);
}

/* its settings' section: "Epson LX-350,FILE:" */
static void section_of(const char *device, const char *port, char *out)
{
    wsprintf(out, "%.60s,%.60s", device, port);
}

static int prof_int(const char *sec, const char *key, int def, LPCSTR profile)
{
    return (int)(profile && *profile ? GetPrivateProfileInt(sec, key, def, profile) :
                                       GetProfileInt(sec, key, def));
}

static void prof_put(const char *sec, const char *key, int v, LPCSTR profile)
{
    char s[12];
    wsprintf(s, "%d", v);
    if (profile && *profile)
        WritePrivateProfileString(sec, key, s, profile);
    else
        WriteProfileString(sec, key, s);
}

/* what the settings say, made whole: a paper of the driver's, its size,
 * the resolution */
static void mode_fill(const win_printer_driver *d, DEVMODE *m)
{
    const win_printer_paper *p = paper_find(d, m->dmPaperSize);
    if (!p)
        p = &d->papers[0];
    m->dmSpecVersion = DM_SPECVERSION;
    m->dmDriverVersion = 0x0100;
    m->dmSize = sizeof(DEVMODE);
    m->dmDriverExtra = 0;
    m->dmFields = DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERLENGTH | DM_PAPERWIDTH | DM_COPIES |
                  DM_DEFAULTSOURCE | DM_PRINTQUALITY | DM_YRESOLUTION | DM_TTOPTION;
    if (m->dmOrientation != DMORIENT_LANDSCAPE)
        m->dmOrientation = DMORIENT_PORTRAIT;
    m->dmPaperSize = (short)p->id;
    m->dmPaperLength = (short)p->length;
    m->dmPaperWidth = (short)p->width;
    m->dmScale = 100;
    if (m->dmCopies < 1)
        m->dmCopies = 1;
    if (m->dmCopies > 99)
        m->dmCopies = 99;
    m->dmDefaultSource = DMBIN_ONLYONE;
    m->dmPrintQuality = (short)d->dpi_x;
    m->dmYResolution = (short)d->dpi_y;
    m->dmColor = DMCOLOR_MONOCHROME;
    m->dmDuplex = DMDUP_SIMPLEX;
    m->dmTTOption = DMTT_BITMAP;
}

/* a program's DEVMODE over them: the fields it says it has */
static void mode_merge(const win_printer_driver *d, DEVMODE *m, const DEVMODE *in)
{
    if (!in)
        return;
    DWORD f = in->dmFields;
    if ((f & DM_ORIENTATION) &&
        (in->dmOrientation == DMORIENT_PORTRAIT || in->dmOrientation == DMORIENT_LANDSCAPE))
        m->dmOrientation = in->dmOrientation;
    if ((f & DM_PAPERSIZE) && paper_find(d, in->dmPaperSize))
        m->dmPaperSize = in->dmPaperSize;
    if ((f & DM_COPIES) && in->dmCopies >= 1)
        m->dmCopies = in->dmCopies;
}

/* the defaults, WIN.INI's (or profile's), the program's */
static void mode_get(const win_printer_driver *d, const char *device, const char *port,
                     LPCSTR profile, const DEVMODE *in, DEVMODE *m)
{
    char sec[128];
    memset(m, 0, sizeof(*m));
    lstrcpyn(m->dmDeviceName, device, CCHDEVICENAME);
    section_of(device, port, sec);
    m->dmOrientation = (short)prof_int(sec, "Orientation", DMORIENT_PORTRAIT, profile);
    m->dmPaperSize = (short)prof_int(sec, "PaperSize", d->papers[0].id, profile);
    m->dmCopies = 1;
    mode_fill(d, m);
    /* the port's environment (SetEnvironment's: a driver's last settings
     * for it), if of this device */
    DEVMODE env;
    char name[CCHDEVICENAME + 1];
    memset(&env, 0, sizeof(env));
    int got = GetEnvironment(port, &env, sizeof(env));
    lstrcpyn(name, env.dmDeviceName, sizeof(name));
    if (got >= 44 && !lstrcmpi(name, m->dmDeviceName)) {
        mode_merge(d, m, &env);
        mode_fill(d, m);
    }
    mode_merge(d, m, in);
    mode_fill(d, m);
}

static void mode_put(const char *device, const char *port, LPCSTR profile, const DEVMODE *m)
{
    char sec[128];
    section_of(device, port, sec);
    prof_put(sec, "Orientation", m->dmOrientation, profile);
    prof_put(sec, "PaperSize", m->dmPaperSize, profile);
}

/* the settings' dialog: the paper, the orientation */
#define IDC_PRN_PAPER    120
#define IDC_PRN_PORTRAIT 121
#define IDC_PRN_LAND     122

static struct {
    const win_printer_driver *d;
    DEVMODE *m;
    char title[128];
} setup;

static BOOL CALLBACK SetupProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        SetWindowText(dlg, setup.title);
        for (const win_printer_paper *p = setup.d->papers; p->name; p++) {
            int i = (int)SendDlgItemMessage(dlg, IDC_PRN_PAPER, CB_ADDSTRING, 0, (LPARAM)p->name);
            if (p->id == setup.m->dmPaperSize)
                SendDlgItemMessage(dlg, IDC_PRN_PAPER, CB_SETCURSEL, (WPARAM)i, 0);
        }
        CheckRadioButton(dlg, IDC_PRN_PORTRAIT, IDC_PRN_LAND,
                         setup.m->dmOrientation == DMORIENT_LANDSCAPE ? IDC_PRN_LAND :
                                                                       IDC_PRN_PORTRAIT);
        return TRUE;
    }
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        if (wp == IDOK) {
            int i = (int)SendDlgItemMessage(dlg, IDC_PRN_PAPER, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < paper_count(setup.d))
                setup.m->dmPaperSize = (short)setup.d->papers[i].id;
            setup.m->dmOrientation = IsDlgButtonChecked(dlg, IDC_PRN_LAND) ?
                                     DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
        }
        EndDialog(dlg, (int)wp);
        return TRUE;
    }
    return FALSE;
}

static const DLGTEMPLATE_N setup_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
    7, 30, 30, 200, 76, NULL, NULL, "Printer Setup", 0, NULL
};
static const DLGITEM_N setup_items[] = {
    { 6, 8, 46, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Paper &Size:" },
    { 54, 6, 84, 80, IDC_PRN_PAPER, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE,
      "COMBOBOX", "" },
    { 6, 26, 132, 44, -1, BS_GROUPBOX | WS_VISIBLE, "BUTTON", "Orientation" },
    { 14, 38, 100, 10, IDC_PRN_PORTRAIT, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP | WS_VISIBLE,
      "BUTTON", "&Portrait" },
    { 14, 52, 100, 10, IDC_PRN_LAND, BS_AUTORADIOBUTTON | WS_VISIBLE, "BUTTON", "&Landscape" },
    { 144, 6, 50, 14, IDOK, BS_DEFPUSHBUTTON | WS_GROUP | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 144, 24, 50, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

int WINAPI PrinterDeviceModeN(HWND owner, LPCSTR driver, DEVMODE *out, LPCSTR device,
                              LPCSTR port, const DEVMODE *in, LPCSTR profile, UINT mode)
{
    HMODULE h;
    const win_printer_driver *d = driver_load(driver, &h);
    if (!d)
        return -1;
    if (!mode) {                        /* the size of its DEVMODE */
        FreeLibrary(h);
        return (int)sizeof(DEVMODE);
    }
    char model[64], prt[128];
    DEVMODE m;
    model_of(d, device, model, sizeof(model));
    port_of(model, port, prt, sizeof(prt));
    mode_get(d, model, prt, profile, (mode & DM_MODIFY) ? in : NULL, &m);
    int rc = IDOK;
    if (mode & DM_PROMPT) {
        setup.d = d;
        setup.m = &m;
        wsprintf(setup.title, "%.60s on %.60s", model, prt);
        rc = DialogBoxIndirectN(0, &setup_tpl, setup_items, owner, SetupProc) == IDOK ? IDOK :
             IDCANCEL;
        setup.m = NULL;
        mode_fill(d, &m);
    }
    if (rc == IDOK) {
        if (mode & DM_UPDATE) {
            mode_put(model, prt, profile, &m);
            SetEnvironment(prt, &m, sizeof(m));
        }
        if ((mode & DM_COPY) && out)
            memcpy(out, &m, sizeof(m));
    }
    FreeLibrary(h);
    return rc;
}

DWORD WINAPI PrinterCapabilitiesN(LPCSTR driver, LPCSTR device, LPCSTR port, UINT index,
                                  LPSTR out, const DEVMODE *dm)
{
    HMODULE h;
    const win_printer_driver *d = driver_load(driver, &h);
    if (!d)
        return (DWORD)-1;
    char model[64], prt[128];
    DEVMODE m;
    model_of(d, device, model, sizeof(model));
    port_of(model, port, prt, sizeof(prt));
    mode_get(d, model, prt, NULL, dm, &m);
    int n = paper_count(d);
    DWORD r = (DWORD)n;
    switch (index) {
    case DC_FIELDS: r = m.dmFields; break;
    case DC_PAPERS:
        for (int i = 0; out && i < n; i++)
            ((WORD *)(void *)out)[i] = (WORD)d->papers[i].id;
        break;
    case DC_PAPERSIZE:                  /* POINTs: the sheets, tenths of a millimetre */
        for (int i = 0; out && i < n; i++) {
            ((POINT *)(void *)out)[i].x = d->papers[i].width;
            ((POINT *)(void *)out)[i].y = d->papers[i].length;
        }
        break;
    case DC_PAPERNAMES:                 /* 64 characters each */
        for (int i = 0; out && i < n; i++)
            lstrcpyn(out + 64 * i, d->papers[i].name, 64);
        break;
    case DC_MINEXTENT: case DC_MAXEXTENT: {
        int w = d->papers[0].width, l = d->papers[0].length;
        for (int i = 1; i < n; i++) {
            int pw = d->papers[i].width, pl = d->papers[i].length;
            if (index == DC_MINEXTENT ? pw < w : pw > w) w = pw;
            if (index == DC_MINEXTENT ? pl < l : pl > l) l = pl;
        }
        r = MAKELONG(w, l);
        break;
    }
    case DC_BINS:
        if (out)
            *(WORD *)(void *)out = DMBIN_ONLYONE;
        r = 1;
        break;
    case DC_BINNAMES:                   /* 24 characters each */
        if (out)
            lstrcpyn(out, "Default", 24);
        r = 1;
        break;
    case DC_ENUMRESOLUTIONS:            /* LONG pairs: across, down */
        if (out) {
            ((LONG *)(void *)out)[0] = d->dpi_x;
            ((LONG *)(void *)out)[1] = d->dpi_y;
        }
        r = 1;
        break;
    case DC_FILEDEPENDENCIES:           /* 64 characters each: the driver's file */
        if (out) {
            char f[16];
            int k = 0;
            const char *b = driver;
            for (const char *p = driver; *p; p++)
                if (*p == '\\' || *p == '/' || *p == ':')
                    b = p + 1;
            while (b[k] && b[k] != '.' && k < 8) {
                f[k] = b[k];
                k++;
            }
            lstrcpy(f + k, ".DLL");
            lstrcpyn(out, f, 64);
        }
        r = 1;
        break;
    case DC_DUPLEX: r = 0; break;
    case DC_SIZE: r = sizeof(DEVMODE); break;
    case DC_EXTRA: r = 0; break;
    case DC_VERSION: r = DM_SPECVERSION; break;
    case DC_DRIVER: r = m.dmDriverVersion; break;
    case DC_TRUETYPE: r = DCTT_BITMAP; break;
    case DC_ORIENTATION: r = 90; break;
    case DC_COPIES: r = 99; break;
    default: r = (DWORD)-1; break;
    }
    FreeLibrary(h);
    return r;
}

/* ---------------------------------------------------------------- DCs */

struct spjob;
static void job_free(struct spjob *j);

/* the paper, the orientation, the copies of its settings: the page's size */
static void apply_mode(wprinter *p)
{
    p->paper = paper_find(p->drv, p->dm.dmPaperSize);
    if (!p->paper)
        p->paper = &p->drv->papers[0];
    p->land = p->dm.dmOrientation == DMORIENT_LANDSCAPE;
    p->copies = p->dm.dmCopies;
    p->pw = p->land ? p->paper->cy : p->paper->cx;
    p->ph = p->land ? p->paper->cx : p->paper->cy;
}

HDC print_create_dc(LPCSTR driver, LPCSTR device, LPCSTR port, int ic, const DEVMODE *init)
{
    HMODULE module;
    const win_printer_driver *d = driver_load(driver, &module);
    if (!d)
        return 0;
    wprinter *p = (wprinter *)win_alloc(sizeof(wprinter));
    HDC h = p ? CreateCompatibleDC(0) : 0;
    if (!h) {
        win_free(p);
        FreeLibrary(module);
        return 0;
    }
    memset(p, 0, sizeof(*p));
    p->module = module;
    p->drv = d;
    p->ic = ic;
    p->out = HFILE_ERROR;
    model_of(d, device, p->model, sizeof(p->model));
    port_of(p->model, port, p->port, sizeof(p->port));
    mode_get(d, p->model, p->port, NULL, init, &p->dm);
    apply_mode(p);
    /* an IC draws nowhere: a dot of a page */
    HBITMAP page = CreateBitmap(ic ? 1 : p->pw, ic ? 1 : p->ph, 1, 1, NULL);
    if (!page) {
        DeleteDC(h);
        win_free(p);
        FreeLibrary(module);
        return 0;
    }
    p->page = page;
    SelectObject(h, page);
    PatBlt(h, 0, 0, p->pw, p->ph, WHITENESS);
    wdc *dc = gdi_dc(h);
    dc->prn = p;
    dc->dpi = p->land ? d->dpi_x : d->dpi_y;
    return h;
}

void print_dc_gone(wdc *dc)
{
    wprinter *p = dc->prn;
    if (!p)
        return;
    dc->prn = NULL;
    if (p->in_doc) {
        p->drv->end_doc(&p->sink, 1);
        if (p->job)
            job_free(p->job);
        else if (p->out != HFILE_ERROR)
            _lclose(p->out);
    }
    FreeLibrary(p->module);
    win_free(p);
}

/* the page anew, of the settings' size (white) */
static int new_page(HDC h, wprinter *p)
{
    apply_mode(p);
    wdc *dc = gdi_dc(h);
    if (!p->ic && (!dc->surf || dc->surf->width != p->pw || dc->surf->height != p->ph)) {
        HBITMAP page = CreateBitmap(p->pw, p->ph, 1, 1, NULL);
        if (!page)
            return 0;
        SelectObject(h, page);
        DeleteObject(p->page);
        p->page = page;
    }
    dc->dpi = p->land ? p->drv->dpi_x : p->drv->dpi_y;
    PatBlt(h, 0, 0, p->pw, p->ph, WHITENESS);
    return 1;
}

HDC WINAPI ResetDC(HDC h, const DEVMODE *dm)
{
    wdc *dc = gdi_dc(h);
    wprinter *p = dc ? dc->prn : NULL;
    if (!dc)
        return 0;
    if (!p || !dm)
        return h;                       /* the screen's: nothing to change */
    DEVMODE was = p->dm;
    const win_printer_paper *paper = p->paper;
    mode_merge(p->drv, &p->dm, dm);
    mode_fill(p->drv, &p->dm);
    if (!new_page(h, p)) {
        p->dm = was;
        apply_mode(p);
        return 0;
    }
    if (p->in_doc && p->paper != paper)
        p->restart = 1;
    return h;
}

int print_caps(const wdc *dc, int index, int *value)
{
    const wprinter *p = dc->prn;
    const win_printer_driver *d = p->drv;
    int dx = p->land ? d->dpi_y : d->dpi_x, dy = p->land ? d->dpi_x : d->dpi_y;
    switch (index) {
    case HORZRES: *value = p->pw; return 1;
    case VERTRES: *value = p->ph; return 1;
    case LOGPIXELSX: *value = dx; return 1;
    case LOGPIXELSY: *value = dy; return 1;
    case HORZSIZE: *value = p->pw * 254 / (dx * 10); return 1;
    case VERTSIZE: *value = p->ph * 254 / (dy * 10); return 1;
    case ASPECTX: *value = dy; return 1;     /* as Windows' drivers: the other axis's */
    case ASPECTY: *value = dx; return 1;
    case ASPECTXY: {
        int s = 1;
        while (s * s < dx * dx + dy * dy)
            s++;
        *value = s;
        return 1;
    }
    case TECHNOLOGY: *value = DT_RASPRINTER; return 1;
    case BITSPIXEL: case PLANES: *value = 1; return 1;
    case NUMCOLORS: case NUMPENS: *value = 2; return 1;
    case NUMBRUSHES: *value = -1; return 1;
    case RASTERCAPS:
        *value = RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP | RC_DIBTODEV | RC_STRETCHDIB;
        return 1;
    case SIZEPALETTE: case NUMRESERVED: case COLORRES: *value = 0; return 1;
    }
    return 0;
}

static wprinter *printer_of(HDC h)
{
    wdc *dc = gdi_dc(h);
    return dc && dc->prn && !dc->prn->ic ? dc->prn : NULL;
}

/* ---------------------------------------------------------------- the port */

static int flush(wprinter *p)
{
    if (p->used && !p->failed && _lwrite(p->out, (LPCSTR)p->buf, p->used) != p->used)
        p->failed = 1;
    p->used = 0;
    return !p->failed;
}

static int sink_write(void *ctx, const void *data, unsigned n)
{
    wprinter *p = (wprinter *)ctx;
    const uint8_t *s = (const uint8_t *)data;
    while (n && !p->failed) {
        unsigned k = sizeof(p->buf) - p->used;
        if (k > n)
            k = n;
        memcpy(p->buf + p->used, s, k);
        p->used += k;
        s += k;
        n -= k;
        if (p->used == sizeof(p->buf))
            flush(p);
    }
    return !p->failed;
}

/* FILE:'s question: the file's name */
#define IDC_PRN_FILE 100

static char file_name[128];

static BOOL CALLBACK FileProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        SetDlgItemText(dlg, IDC_PRN_FILE, file_name);
        SendDlgItemMessage(dlg, IDC_PRN_FILE, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return TRUE;
    }
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        if (wp == IDOK)
            GetDlgItemText(dlg, IDC_PRN_FILE, file_name, sizeof(file_name));
        EndDialog(dlg, (int)wp);
        return TRUE;
    }
    return FALSE;
}

static const DLGTEMPLATE_N file_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
    3, 40, 40, 180, 54, NULL, NULL, "Print To File", 0, NULL
};
static const DLGITEM_N file_items[] = {
    { 6, 6, 168, 10, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Output File Name:" },
    { 6, 18, 168, 12, IDC_PRN_FILE, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 112, 36, 62, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
};

/* a device's name as DOS has it: "LPT1:" LPT1, "PRN" PRN; 0 if a file */
static int device_of(const char *port, char *name, int cap)
{
    int n = lstrlen(port);
    if (n < 2 || n >= cap || port[n - 1] != ':' || !lstrcmpi(port, "FILE:"))
        return 0;
    lstrcpyn(name, port, n);             /* without its colon */
    return 1;
}

/* a device or a file opened to write */
static HFILE open_device(const char *port)
{
    char name[128];
    if (device_of(port, name, sizeof(name))) {
        /* a device takes it as it comes: one job after another (a file
         * standing for it too) */
        HFILE f = _lopen(name, OF_WRITE);
        if (f != HFILE_ERROR)
            _llseek(f, 0, 2);
        return f != HFILE_ERROR ? f : _lcreat(name, 0);
    }
    return _lcreat(port, 0);
}

/* the output's file or device: FILE: asks; a DOS device opened as it is */
static HFILE open_port(const char *port)
{
    if (!lstrcmpi(port, "FILE:")) {
        if (!file_name[0])
            lstrcpy(file_name, "OUTPUT.PRN");
        if (DialogBoxIndirectN(0, &file_tpl, file_items, GetActiveWindow(), FileProc) != IDOK ||
            !file_name[0])
            return HFILE_ERROR;
        return _lcreat(file_name, 0);
    }
    return open_device(port);
}

/* ---------------------------------------------------------------- the spooler */

enum { JOB_WRITING, JOB_QUEUED, JOB_PRINTING };

typedef struct spjob {
    struct spjob *next;
    int id;
    int state;
    int direct;                         /* OpenJob's to a file or unspooled: no queue */
    HTASK task;                         /* its writer's (one ending leaves no job open) */
    char title[64], port[128], file[144];
    HFILE f;                            /* written (JOB_WRITING), read (JOB_PRINTING) */
    HFILE dev;                          /* its port, printing */
    LONG page_at;                       /* where its page began (DeleteSpoolPage) */
    LONG size, done;
    unsigned have, at;                  /* read from the file, not yet at the port */
    uint8_t buf[512];
} spjob;

static spjob *jobs;
static int job_ids;
static int despooling;

/* Spooler=no turns it off; FILE: and files are never spooled */
static int spooled(const char *port)
{
    char v[8], name[128];
    GetProfileString("windows", "Spooler", "yes", v, sizeof(v));
    return lstrcmpi(v, "no") && lstrcmpi(v, "0") && lstrcmpi(v, "false") &&
           device_of(port, name, sizeof(name));
}

static spjob *job_of(HANDLE h)
{
    for (spjob *j = jobs; j; j = j->next)
        if (j->id == (int)(uintptr_t)h && h)
            return j;
    return NULL;
}

static void job_unlink(spjob *j)
{
    for (spjob **pp = &jobs; *pp; pp = &(*pp)->next)
        if (*pp == j) {
            *pp = j->next;
            break;
        }
}

/* a job gone: its files closed, its spool file deleted */
static void job_free(spjob *j)
{
    job_unlink(j);
    if (j->f != HFILE_ERROR)
        _lclose(j->f);
    if (j->dev != HFILE_ERROR)
        _lclose(j->dev);
    if (j->file[0] && !j->direct)
        DeleteFileN(j->file);
    win_free(j);
}

/* a new job, last in the queue: spooled to a file of TEMP's, or (direct)
 * straight to its port */
static spjob *job_new(const char *port, const char *title, int direct)
{
    spjob *j = (spjob *)win_alloc(sizeof(spjob));
    if (!j)
        return NULL;
    memset(j, 0, sizeof(*j));
    j->f = j->dev = HFILE_ERROR;
    j->direct = direct;
    lstrcpyn(j->port, port, sizeof(j->port));
    lstrcpyn(j->title, title && *title ? title : "Untitled", sizeof(j->title));
    if (direct) {
        j->f = open_port(port);
    } else if (win_file_temp_name("SPL", 0, j->file)) {
        j->f = _lcreat(j->file, 0);
        if (j->f == HFILE_ERROR)
            DeleteFileN(j->file);
    }
    if (j->f == HFILE_ERROR) {
        win_free(j);
        return NULL;
    }
    if (++job_ids > 0x7fff)
        job_ids = 1;
    j->id = job_ids;
    j->state = JOB_WRITING;
    j->task = GetCurrentTask();
    spjob **pp = &jobs;
    while (*pp)
        pp = &(*pp)->next;
    *pp = j;
    return j;
}

/* written: to the queue (a direct one is done) */
static int job_close(spjob *j)
{
    if (j->direct) {
        job_free(j);
        return 1;
    }
    j->size = _llseek(j->f, 0, 2);
    _lclose(j->f);
    j->f = HFILE_ERROR;
    j->state = JOB_QUEUED;
    return 1;
}

/* the port failed: retry (1) or cancel the job (0) */
static int port_failed(const spjob *j)
{
    char msg[256];
    wsprintf(msg, "Cannot write to the %.40s port.\n\n\"%.60s\": the printer may be off "
                  "line, not ready or not there.", j->port, j->title);
    return MessageBox(0, msg, "Print Manager", MB_RETRYCANCEL | MB_ICONSTOP | MB_SYSTEMMODAL) ==
           IDRETRY;
}

/* The system idle: some of the first job to its port. Nothing is waited
 * for but the port's own writes. */
void print_spool_idle(void)
{
    spjob *j = jobs;
    while (j && j->state == JOB_WRITING)
        j = j->next;
    if (!j || despooling)
        return;
    despooling = 1;
    if (j->state == JOB_QUEUED) {
        j->f = _lopen(j->file, OF_READ);
        if (j->f == HFILE_ERROR) {      /* its file gone: nothing to print */
            job_free(j);
            despooling = 0;
            return;
        }
        j->dev = open_device(j->port);
        if (j->dev == HFILE_ERROR) {
            _lclose(j->f);
            j->f = HFILE_ERROR;
            if (!port_failed(j))
                job_free(j);
            despooling = 0;
            return;
        }
        j->state = JOB_PRINTING;
        j->have = j->at = 0;
    }
    if (j->at == j->have) {
        UINT n = _lread(j->f, j->buf, sizeof(j->buf));
        if (n == (UINT)HFILE_ERROR || !n) { /* all of it printed */
            win_trace_event("spool-done", (uint32_t)j->id, (uint32_t)j->done);
            job_free(j);
            despooling = 0;
            return;
        }
        j->have = n;
        j->at = 0;
    }
    UINT k = j->have - j->at;
    UINT put = _lwrite(j->dev, j->buf + j->at, k);
    if (put == (UINT)HFILE_ERROR)
        put = 0;
    j->at += put;
    j->done += (LONG)put;
    if (put < k && !port_failed(j))
        job_free(j);
    despooling = 0;
}

/* a task gone: the jobs it was writing with it */
void print_task_ended(HTASK task)
{
    for (spjob *j = jobs, *n; j; j = n) {
        n = j->next;
        if (j->state == JOB_WRITING && j->task == task)
            job_free(j);
    }
}

int print_spool_jobs(void)
{
    int n = 0;
    for (const spjob *j = jobs; j; j = j->next)
        n++;
    return n;
}

/* GDI's spooler functions: the printer drivers' (and Print Manager's) */
int WINAPI OpenJob(LPCSTR port, LPCSTR title, HDC hdc)
{
    (void)hdc;
    if (!port || !*port)
        return SP_ERROR;
    spjob *j = job_new(port, title, !spooled(port));
    return j ? j->id : SP_OUTOFDISK;
}

int WINAPI StartSpoolPage(HANDLE job)
{
    spjob *j = job_of(job);
    if (!j || j->state != JOB_WRITING)
        return SP_ERROR;
    j->page_at = _llseek(j->f, 0, 1);
    return 1;
}

int WINAPI EndSpoolPage(HANDLE job)
{
    spjob *j = job_of(job);
    return j && j->state == JOB_WRITING ? 1 : SP_ERROR;
}

/* the page begun last, unwritten: the file cut where it began */
int WINAPI DeleteSpoolPage(HANDLE job)
{
    spjob *j = job_of(job);
    if (!j || j->state != JOB_WRITING || j->direct)
        return SP_ERROR;
    _llseek(j->f, j->page_at, 0);
    _lwrite(j->f, "", 0);               /* DOS: a write of nothing ends the file there */
    return 1;
}

int WINAPI WriteSpool(HANDLE job, LPCVOID data, int cb)
{
    spjob *j = job_of(job);
    if (!j || j->state != JOB_WRITING || cb < 0)
        return SP_ERROR;
    if (!cb)
        return 0;
    return _lwrite(j->f, data, (UINT)cb) == (UINT)cb ? cb : SP_OUTOFDISK;
}

int WINAPI CloseJob(HANDLE job)
{
    spjob *j = job_of(job);
    return j && j->state == JOB_WRITING && job_close(j) ? 1 : SP_ERROR;
}

int WINAPI DeleteJob(HANDLE job, int flags)
{
    (void)flags;
    spjob *j = job_of(job);
    if (!j || (j->state == JOB_PRINTING && despooling))
        return SP_ERROR;
    job_free(j);
    return 1;
}

/* the driver's question to the user, as the spooler asks it */
int WINAPI WriteDialog(HANDLE job, LPCSTR text, int cb)
{
    spjob *j = job_of(job);
    char msg[256];
    if (!j || !text)
        return SP_ERROR;
    if (cb < 0 || cb >= (int)sizeof(msg))
        cb = sizeof(msg) - 1;
    memcpy(msg, text, (size_t)cb);
    msg[cb] = 0;
    return MessageBox(GetActiveWindow(), msg, "Print Manager",
                      MB_OKCANCEL | MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
}

/* SP_QUERYVALIDJOB (30): whether it is one; else the disk's free KB the
 * spool file can grow by */
int WINAPI QueryJob(HANDLE job, int what)
{
    spjob *j = job_of(job);
    if (!j)
        return SP_ERROR;
    if (what == 30)
        return 1;
    DWORD free_bytes = 0, total = 0;
    int drive = j->file[0] && j->file[1] == ':' ? (j->file[0] | 0x20) - 'a' + 1 : 0;
    if (!GetDiskFreeSpaceN(drive, &free_bytes, &total))
        return SP_ERROR;
    free_bytes >>= 10;
    return free_bytes > 0x7fffu ? 0x7fff : (int)free_bytes;
}

/* a driver's file of a whole job queued as it is: printed, then deleted */
HANDLE WINAPI SpoolFile(LPCSTR printer, LPCSTR port, LPCSTR job, LPCSTR file)
{
    (void)printer;
    if (!port || !*port || !file || !*file)
        return (HANDLE)(uintptr_t)(UINT)SP_ERROR;
    HFILE f = _lopen(file, OF_READ);
    if (f == HFILE_ERROR)
        return (HANDLE)(uintptr_t)(UINT)SP_ERROR;
    LONG size = _llseek(f, 0, 2);
    _lclose(f);
    spjob *j = (spjob *)win_alloc(sizeof(spjob));
    if (!j)
        return (HANDLE)(uintptr_t)(UINT)SP_OUTOFDISK;
    memset(j, 0, sizeof(*j));
    j->f = j->dev = HFILE_ERROR;
    lstrcpyn(j->port, port, sizeof(j->port));
    lstrcpyn(j->title, job && *job ? job : "Untitled", sizeof(j->title));
    lstrcpyn(j->file, file, sizeof(j->file));
    j->size = size;
    j->state = JOB_QUEUED;
    if (++job_ids > 0x7fff)
        job_ids = 1;
    j->id = job_ids;
    spjob **pp = &jobs;
    while (*pp)
        pp = &(*pp)->next;
    *pp = j;
    return (HANDLE)(uintptr_t)(UINT)j->id;
}

/* ---------------------------------------------------------------- documents */

/* the program's AbortProc asked: FALSE (or the user's cancel) ends it */
static int go_on(HDC h, wprinter *p, int code)
{
    return !p->abort || p->abort(h, code);
}

BOOL WINAPI QueryAbort(HDC h, int reserved)
{
    wdc *dc = gdi_dc(h);
    wprinter *p = dc ? dc->prn : NULL;
    return !p || go_on(h, p, reserved);
}

int WINAPI StartDoc(HDC h, const DOCINFO *di)
{
    wprinter *p = printer_of(h);
    if (!p || p->in_doc)
        return SP_ERROR;
    const char *port = di && di->lpszOutput && *di->lpszOutput ? di->lpszOutput : p->port;
    p->job = NULL;
    if (spooled(port)) {
        p->job = job_new(port, di ? di->lpszDocName : NULL, 0);
        if (!p->job)
            return SP_OUTOFDISK;
        p->out = p->job->f;
    } else {
        p->out = open_port(port);
    }
    if (p->out == HFILE_ERROR)
        return SP_ERROR;
    p->failed = 0;
    p->used = 0;
    p->band = 0;
    p->restart = 0;
    p->sink.ctx = p;
    p->sink.write = sink_write;
    win_printer_job job = { p->model, p->paper, p->land, p->copies };
    if (!p->drv->start_doc(&p->sink, &job)) {
        if (p->job)
            job_free(p->job);
        else
            _lclose(p->out);
        p->job = NULL;
        p->out = HFILE_ERROR;
        return SP_ERROR;
    }
    p->in_doc = 1;
    PatBlt(h, 0, 0, p->pw, p->ph, WHITENESS);
    return p->job ? p->job->id : 1;
}

int WINAPI StartPage(HDC h)
{
    wprinter *p = printer_of(h);
    return p && p->in_doc ? 1 : SP_ERROR;
}

/* a landscape page upright: turned a quarter to the left (its left edge
 * the sheet's bottom), into rows of pitch bytes; NULL if no memory */
static uint8_t *upright(const wsurf *s, int *pitch)
{
    int w = s->height, hgt = s->width;  /* the sheet's */
    *pitch = (w + 15) / 16 * 2;
    uint8_t *o = (uint8_t *)win_alloc((size_t)*pitch * (size_t)hgt);
    if (!o)
        return NULL;
    memset(o, 0xff, (size_t)*pitch * (size_t)hgt);
    for (int y = 0; y < s->height; y++) {
        const uint8_t *row = s->bits + (size_t)y * (size_t)s->pitch;
        for (int xb = 0; xb < (s->width + 7) / 8; xb++) {
            uint8_t b = row[xb];
            if (b == 0xff)
                continue;
            for (int k = 0; k < 8; k++) {
                int x = xb * 8 + k;
                if (x < s->width && !(b & (0x80 >> k)))
                    o[(size_t)(s->width - 1 - x) * (size_t)*pitch + (size_t)(y >> 3)] &=
                        (uint8_t)~(0x80 >> (y & 7));
            }
        }
    }
    return o;
}

static int finish(HDC h, int aborted);

int WINAPI EndPage(HDC h)
{
    wprinter *p = printer_of(h);
    wdc *dc = gdi_dc(h);
    if (!p || !p->in_doc)
        return SP_ERROR;
    if (!go_on(h, p, 0)) {              /* the program's: before the page is sent */
        finish(h, 1);
        return SP_APPABORT;
    }
    const wsurf *s = dc->surf;
    int ok = 1, pitch = s->pitch;
    if (p->restart) {                   /* the paper changed (ResetDC): a job of its own */
        win_printer_job job = { p->model, p->paper, p->land, p->copies };
        p->restart = 0;
        ok = p->drv->end_doc(&p->sink, 0) && p->drv->start_doc(&p->sink, &job);
    }
    uint8_t *turned = p->land ? upright(s, &pitch) : NULL;
    const uint8_t *bits = p->land ? turned : s->bits;
    if (!bits)
        ok = 0;
    int stop = 0;
    for (int i = 0; ok && !stop && i < p->copies; i++) {
        ok = p->drv->page(&p->sink, bits, pitch, p->paper->cx, p->paper->cy) && flush(p);
        if (!ok && p->job)
            go_on(h, p, SP_OUTOFDISK);  /* the disk full: the program told */
        if (ok && i + 1 < p->copies)
            stop = !go_on(h, p, 0);
    }
    win_free(turned);
    PatBlt(h, 0, 0, p->pw, p->ph, WHITENESS);
    if (stop) {
        finish(h, 1);
        return SP_APPABORT;
    }
    return ok ? 1 : SP_OUTOFDISK;
}

static int finish(HDC h, int aborted)
{
    wprinter *p = printer_of(h);
    if (!p || !p->in_doc)
        return SP_ERROR;
    int ok = p->drv->end_doc(&p->sink, aborted) && flush(p);
    if (p->job) {                       /* spooled: to the queue, or gone */
        if (ok && !aborted)
            job_close(p->job);
        else
            job_free(p->job);
        p->job = NULL;
    } else {
        _lclose(p->out);
    }
    p->out = HFILE_ERROR;
    p->in_doc = 0;
    return ok ? 1 : SP_ERROR;
}

int WINAPI EndDoc(HDC h) { return finish(h, 0); }
int WINAPI AbortDoc(HDC h) { return finish(h, 1); }

/* asked at each page whether to go on (a Win16 or Win32 program's own, its
 * caller's trampoline) */
int WINAPI SetAbortProc(HDC h, ABORTPROC proc)
{
    wprinter *p = printer_of(h);
    if (!p)
        return SP_ERROR;
    p->abort = proc;
    return 1;
}

int WINAPI Escape(HDC h, int esc, int cb, LPCSTR in, void *out)
{
    wdc *dc = gdi_dc(h);
    wprinter *p = dc ? dc->prn : NULL;
    if (!p)
        return 0;                       /* the screen: no escapes */
    const win_printer_driver *d = p->drv;
    switch (esc) {
    case QUERYESCSUPPORT: {
        int what = in && cb >= 2 ? *(const int *)in : 0;
        switch (what) {
        case NEWFRAME: case ABORTDOC: case NEXTBAND: case FLUSHOUTPUT: case QUERYESCSUPPORT:
        case SETABORTPROC: case STARTDOC: case ENDDOC: case GETPHYSPAGESIZE:
        case GETPRINTINGOFFSET: case GETSCALINGFACTOR: case SETCOPYCOUNT: case PASSTHROUGH:
        case GETTECHNOLOGY: case DRAFTMODE: case GETSETPRINTORIENT:
            return 1;
        }
        return 0;
    }
    case STARTDOC: {
        char name[64] = "";
        if (in && cb > 0)
            lstrcpyn(name, in, cb + 1 < (int)sizeof(name) ? cb + 1 : (int)sizeof(name));
        DOCINFO di = { sizeof(DOCINFO), name, NULL };
        return StartDoc(h, &di);
    }
    case NEWFRAME:
        return EndPage(h);
    case NEXTBAND: {                    /* the whole page, then none: a page done */
        RECT *r = (RECT *)out;
        if (!p->in_doc || !r)
            return SP_ERROR;
        if (!p->band) {
            SetRect(r, 0, 0, p->pw, p->ph);
            p->band = 1;
            return 1;
        }
        SetRectEmpty(r);
        p->band = 0;
        return EndPage(h);
    }
    case ENDDOC:
        return EndDoc(h);
    case ABORTDOC:
        return AbortDoc(h);
    case SETCOPYCOUNT:                  /* in an int: the copies, out the copies made */
        if (in && cb >= (int)sizeof(int)) {
            int n = *(const int *)in;
            p->dm.dmCopies = (short)(n < 1 ? 1 : n > 99 ? 99 : n);
            p->copies = p->dm.dmCopies;
        }
        if (out)
            *(int *)out = p->copies;
        return 1;
    case SETABORTPROC:                  /* in: the procedure itself (a native caller's) */
        return SetAbortProc(h, (ABORTPROC)(void (*)(void))(uintptr_t)in) > 0;
    case FLUSHOUTPUT: case DRAFTMODE:
        return 1;
    case GETSETPRINTORIENT: {           /* in a DWORD (or none: asked), the one before */
        int was = p->dm.dmOrientation;
        if (in && cb >= 4) {
            DEVMODE m = p->dm;
            m.dmFields = DM_ORIENTATION;
            m.dmOrientation = (short)*(const DWORD *)(const void *)in;
            if (!ResetDC(h, &m))
                return -1;
        }
        return was;
    }
    case GETPHYSPAGESIZE: case GETPRINTINGOFFSET: case GETSCALINGFACTOR: {
        POINT *pt = (POINT *)out;
        const win_printer_paper *pp = p->paper;
        int sw = pp->width * d->dpi_x / 254, sl = pp->length * d->dpi_y / 254;
        if (!pt)
            return 0;
        pt->x = pt->y = 0;
        if (esc == GETPHYSPAGESIZE) {   /* the sheet */
            pt->x = p->land ? sl : sw;
            pt->y = p->land ? sw : sl;
        } else if (esc == GETPRINTINGOFFSET) {  /* where the page is on it */
            pt->x = p->land ? sl - pp->y - pp->cy : pp->x;
            pt->y = p->land ? pp->x : pp->y;
            if (pt->x < 0)
                pt->x = 0;
        }
        return 1;
    }
    case PASSTHROUGH:                   /* a word's count, the bytes as they are */
        if (!p->in_doc || !in || cb < 2)
            return SP_ERROR;
        {
            unsigned n = (uint8_t)in[0] | (unsigned)(uint8_t)in[1] << 8;
            if (n > (unsigned)cb - 2u)
                n = (unsigned)cb - 2u;
            return sink_write(p, in + 2, n) ? (int)n : SP_ERROR;
        }
    case GETTECHNOLOGY:
        if (out)
            lstrcpy((LPSTR)out, "WINN Printer");
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- Win16 programs */

/* a program's DEVMODE (its dmSize of it, Windows 3.0's 64 at the least),
 * NULL for none or a bad one */
static const DEVMODE *ne_devmode(const win_dll_ne *ne, win_dll_call *c, uint32_t far, DEVMODE *m)
{
    uint8_t b[sizeof(DEVMODE)];
    if (!(far >> 16) || !ne->read(c, far, b, 40))
        return NULL;
    unsigned n = (unsigned)(b[36] | b[37] << 8);
    if (n < 40)
        return NULL;
    if (n > sizeof(b))
        n = sizeof(b);
    memset(b + 40, 0, sizeof(b) - 40);
    if (!ne->read(c, far, b, n))
        return NULL;
    memcpy(m, b, sizeof(*m));
    return m;
}

static int ne_str(const win_dll_ne *ne, win_dll_call *c, uint32_t far, char *s, int cap)
{
    s[0] = 0;
    return !(far >> 16) || ne->string(c, far, s, (uint32_t)cap);
}

/* the driver's exports as a Win16 program calls them (its PASCAL
 * arguments, the last at 4): DeviceMode(HWND, HANDLE, LPSTR, LPSTR) 13,
 * ExtDeviceMode(HWND, HANDLE, LPDEVMODE, LPSTR, LPSTR, LPDEVMODE, LPSTR,
 * WORD) 90, DeviceCapabilities(LPSTR, LPSTR, WORD, LPSTR, LPDEVMODE) 91,
 * AdvancedSetUpDialog(HWND, HANDLE, LPDEVMODE, LPDEVMODE) 93 */
int WINAPI PrinterNeCallN(LPCSTR driver, void *call, UINT ordinal, DWORD *value)
{
    const win_dll_ne *ne = win_dll_ne_glue();
    win_dll_call *c = (win_dll_call *)call;
    char dev[64], port[128], prof[128];
    DEVMODE in, out;
    *value = 0;
    if (!ne)
        return WINDLL_NONE;
    switch (ordinal) {
    case 13:
        if (ne_str(ne, c, ne->arg32(c, 8), dev, sizeof(dev)) &&
            ne_str(ne, c, ne->arg32(c, 4), port, sizeof(port)))
            *value = PrinterDeviceModeN(ne->arg16(c, 14), driver, NULL, dev, port, NULL, NULL,
                                        DM_PROMPT | DM_UPDATE) == IDOK;
        return WINDLL_WORD;
    case 90: {
        UINT mode = ne->arg16(c, 4);
        uint32_t to = ne->arg32(c, 22);
        const DEVMODE *from = (mode & DM_MODIFY) ? ne_devmode(ne, c, ne->arg32(c, 10), &in) : NULL;
        int rc = -1;
        if (ne_str(ne, c, ne->arg32(c, 18), dev, sizeof(dev)) &&
            ne_str(ne, c, ne->arg32(c, 14), port, sizeof(port)) &&
            ne_str(ne, c, ne->arg32(c, 6), prof, sizeof(prof))) {
            rc = PrinterDeviceModeN(ne->arg16(c, 28), driver, &out, dev, port, from, prof,
                                    (mode & DM_COPY) && !(to >> 16) ? mode & ~(UINT)DM_COPY : mode);
            if (rc == IDOK && (mode & DM_COPY) && (to >> 16) && !ne->write(c, to, &out, sizeof(out)))
                rc = -1;
        }
        *value = (uint16_t)rc;
        return WINDLL_WORD;
    }
    case 91: {
        UINT index = ne->arg16(c, 12);
        uint32_t to = ne->arg32(c, 8);
        const DEVMODE *from = ne_devmode(ne, c, ne->arg32(c, 4), &in);
        *value = (DWORD)-1;
        if (!ne_str(ne, c, ne->arg32(c, 18), dev, sizeof(dev)) ||
            !ne_str(ne, c, ne->arg32(c, 14), port, sizeof(port)))
            return WINDLL_DWORD;
        DWORD n = PrinterCapabilitiesN(driver, dev, port, index, NULL, from);
        unsigned each = index == DC_PAPERS || index == DC_BINS ? 2 :
                        index == DC_PAPERSIZE || index == DC_ENUMRESOLUTIONS ? 8 :
                        index == DC_PAPERNAMES || index == DC_FILEDEPENDENCIES ? 64 :
                        index == DC_BINNAMES ? 24 : 0;
        *value = n;
        if (!(to >> 16) || !each || n == (DWORD)-1 || !n || n > 64)
            return WINDLL_DWORD;
        uint8_t *b = (uint8_t *)win_alloc(each * n);
        if (!b)
            return WINDLL_DWORD;
        memset(b, 0, each * n);
        PrinterCapabilitiesN(driver, dev, port, index, (LPSTR)b, from);
        unsigned size = each * n;
        if (index == DC_PAPERSIZE) {    /* POINTs of shorts */
            for (DWORD i = 0; i < n; i++) {
                const POINT *pt = (const POINT *)(const void *)(b + 8 * i);
                int16_t x = (int16_t)pt->x, y = (int16_t)pt->y;
                memcpy(b + 4 * i, &x, 2);
                memcpy(b + 4 * i + 2, &y, 2);
            }
            size = 4 * n;
        }
        if (!ne->write(c, to, b, size))
            *value = (DWORD)-1;
        win_free(b);
        return WINDLL_DWORD;
    }
    case 93: {
        uint32_t to = ne->arg32(c, 4);
        const DEVMODE *from = ne_devmode(ne, c, ne->arg32(c, 8), &in);
        LONG rc = -1;
        if (from && (to >> 16)) {
            char name[CCHDEVICENAME + 1];
            lstrcpyn(name, from->dmDeviceName, sizeof(name));
            rc = PrinterDeviceModeN(ne->arg16(c, 14), driver, &out, name, NULL, from, NULL,
                                    DM_PROMPT | DM_COPY | DM_MODIFY);
            if (rc == IDOK && !ne->write(c, to, &out, sizeof(out)))
                rc = -1;
        }
        *value = (DWORD)rc;
        return WINDLL_DWORD;
    }
    }
    return WINDLL_NONE;
}
