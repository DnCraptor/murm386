/*
 * print.c - printing: printers' DCs on native printer drivers
 * (include/printer_driver.h). CreateDC("EPSON9", "Epson LX-350", "FILE:")
 * loads the driver (EPSON9.DLL) and gives a DC whose surface is the page,
 * a monochrome bitmap at the driver's resolution; StartDoc opens the port,
 * EndPage hands the page to the driver (and clears it for the next), EndDoc
 * closes it. Windows 3.0's escapes (STARTDOC, NEWFRAME, NEXTBAND, ENDDOC
 * ...) the same. No spooler: the output goes to the port as the program
 * prints.
 *
 * Ports: FILE: asks for a file's name (Windows' "Print To File"), LPTn:,
 * COMn: and PRN are DOS's devices, any other a file of that name.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "printer_driver.h"

typedef struct wprinter {
    HMODULE module;
    const win_printer_driver *drv;
    char model[64], port[128];
    HBITMAP page;
    int ic;
    int in_doc, band;               /* a document begun; NEXTBAND's state */
    HFILE out;
    int failed;
    unsigned used;
    uint8_t buf[512];
    win_printer_out sink;
} wprinter;

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
    if (!d || d->version != WIN_PRINTER_DRIVER_VERSION || d->size < sizeof(*d) ||
        !d->start_doc || !d->page || !d->end_doc || d->dpi_x <= 0 || d->dpi_y <= 0 ||
        d->width <= 0 || d->height <= 0 || d->width > 4096 || d->height > 4096) {
        FreeLibrary(h);
        return NULL;
    }
    *module = h;
    return d;
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

/* ---------------------------------------------------------------- DCs */

HDC print_create_dc(LPCSTR driver, LPCSTR device, LPCSTR port, int ic)
{
    HMODULE module;
    const win_printer_driver *d = driver_load(driver, &module);
    if (!d)
        return 0;
    wprinter *p = (wprinter *)win_alloc(sizeof(wprinter));
    HDC h = p ? CreateCompatibleDC(0) : 0;
    /* an IC draws nowhere: a dot of a page */
    HBITMAP page = h ? CreateBitmap(ic ? 1 : d->width, ic ? 1 : d->height, 1, 1, NULL) : 0;
    if (!page) {
        if (h)
            DeleteDC(h);
        win_free(p);
        FreeLibrary(module);
        return 0;
    }
    memset(p, 0, sizeof(*p));
    p->module = module;
    p->drv = d;
    p->page = page;
    p->ic = ic;
    p->out = HFILE_ERROR;
    lstrcpyn(p->model, device && *device ? device : (d->models && d->models[0] ? d->models[0] : ""),
             sizeof(p->model));
    if (port && *port)
        lstrcpyn(p->port, port, sizeof(p->port));
    else                                /* the device's in WIN.INI, or a file */
        GetProfileString("devices", p->model, "", p->port, sizeof(p->port));
    char *comma = strchr(p->port, ',');
    if (comma)                          /* "EPSON9,FILE:": the port after it */
        memmove(p->port, comma + 1, strlen(comma + 1) + 1);
    if (!p->port[0])
        lstrcpy(p->port, "FILE:");
    SelectObject(h, page);
    PatBlt(h, 0, 0, d->width, d->height, WHITENESS);
    wdc *dc = gdi_dc(h);
    dc->prn = p;
    dc->dpi = d->dpi_y;
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
        if (p->out != HFILE_ERROR)
            _lclose(p->out);
    }
    FreeLibrary(p->module);
    win_free(p);
}

int print_caps(const wdc *dc, int index, int *value)
{
    const wprinter *p = dc->prn;
    const win_printer_driver *d = p->drv;
    switch (index) {
    case HORZRES: *value = d->width; return 1;
    case VERTRES: *value = d->height; return 1;
    case LOGPIXELSX: *value = d->dpi_x; return 1;
    case LOGPIXELSY: *value = d->dpi_y; return 1;
    case HORZSIZE: *value = d->width * 254 / (d->dpi_x * 10); return 1;
    case VERTSIZE: *value = d->height * 254 / (d->dpi_y * 10); return 1;
    case ASPECTX: *value = d->dpi_y; return 1;     /* as Windows' drivers: the other axis's */
    case ASPECTY: *value = d->dpi_x; return 1;
    case ASPECTXY: {
        int s = 1;
        while (s * s < d->dpi_x * d->dpi_x + d->dpi_y * d->dpi_y)
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

/* the output's file or device: FILE: asks; a DOS device opened as it is */
static HFILE open_port(const char *port)
{
    char name[128];
    lstrcpyn(name, port, sizeof(name));
    int n = lstrlen(name);
    if (!lstrcmpi(name, "FILE:")) {
        if (!file_name[0])
            lstrcpy(file_name, "OUTPUT.PRN");
        if (DialogBoxIndirectN(0, &file_tpl, file_items, GetActiveWindow(), FileProc) != IDOK ||
            !file_name[0])
            return HFILE_ERROR;
        return _lcreat(file_name, 0);
    }
    if (n > 1 && name[n - 1] == ':') {   /* LPT1:, COM2:, PRN: DOS's names */
        name[n - 1] = 0;
        HFILE f = _lopen(name, OF_WRITE);
        return f != HFILE_ERROR ? f : _lcreat(name, 0);
    }
    return _lcreat(name, 0);
}

/* ---------------------------------------------------------------- documents */

int WINAPI StartDoc(HDC h, const DOCINFO *di)
{
    wprinter *p = printer_of(h);
    if (!p || p->in_doc)
        return SP_ERROR;
    p->out = open_port(di && di->lpszOutput && *di->lpszOutput ? di->lpszOutput : p->port);
    if (p->out == HFILE_ERROR)
        return SP_ERROR;
    p->failed = 0;
    p->used = 0;
    p->band = 0;
    p->sink.ctx = p;
    p->sink.write = sink_write;
    if (!p->drv->start_doc(&p->sink, p->model)) {
        _lclose(p->out);
        p->out = HFILE_ERROR;
        return SP_ERROR;
    }
    p->in_doc = 1;
    PatBlt(h, 0, 0, p->drv->width, p->drv->height, WHITENESS);
    return 1;
}

int WINAPI StartPage(HDC h)
{
    wprinter *p = printer_of(h);
    return p && p->in_doc ? 1 : SP_ERROR;
}

int WINAPI EndPage(HDC h)
{
    wprinter *p = printer_of(h);
    wdc *dc = gdi_dc(h);
    if (!p || !p->in_doc)
        return SP_ERROR;
    const wsurf *s = dc->surf;
    int ok = p->drv->page(&p->sink, s->bits, s->pitch, s->width, s->height) && flush(p);
    PatBlt(h, 0, 0, p->drv->width, p->drv->height, WHITENESS);
    return ok ? 1 : SP_OUTOFDISK;
}

static int finish(HDC h, int aborted)
{
    wprinter *p = printer_of(h);
    if (!p || !p->in_doc)
        return SP_ERROR;
    int ok = p->drv->end_doc(&p->sink, aborted) && flush(p);
    _lclose(p->out);
    p->out = HFILE_ERROR;
    p->in_doc = 0;
    return ok ? 1 : SP_ERROR;
}

int WINAPI EndDoc(HDC h) { return finish(h, 0); }
int WINAPI AbortDoc(HDC h) { return finish(h, 1); }

/* kept by Windows to ask whether to go on; nothing here takes so long
 * that it would ask */
int WINAPI SetAbortProc(HDC h, ABORTPROC proc)
{
    (void)proc;
    return printer_of(h) ? 1 : SP_ERROR;
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
        case GETTECHNOLOGY: case DRAFTMODE:
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
            SetRect(r, 0, 0, d->width, d->height);
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
    case SETABORTPROC: case FLUSHOUTPUT: case DRAFTMODE: case SETCOPYCOUNT:
        if (esc == SETCOPYCOUNT && out)
            *(int *)out = 1;            /* one copy at a time: the program repeats */
        return 1;
    case GETPHYSPAGESIZE: case GETPRINTINGOFFSET: case GETSCALINGFACTOR: {
        POINT *pt = (POINT *)out;
        if (!pt)
            return 0;
        pt->x = esc == GETPHYSPAGESIZE ? d->width : 0;
        pt->y = esc == GETPHYSPAGESIZE ? d->height : 0;
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
