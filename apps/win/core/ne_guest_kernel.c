/*
 * ne_guest_kernel.c - more of what Win16 programs call: KERNEL's modules
 * (GetModuleHandle, LoadLibrary and GetProcAddress for the modules WINN
 * has), resources (FindResource, LoadResource ...), temporary files, the
 * environment of the system (GetWinFlags, GetFreeSpace ...); USER's and
 * GDI's few left (the MM_TEXT mapping mode, palettes, CreateDC); COMMDLG's
 * file dialogs and Find/Replace; SHELL's ShellExecute, ShellAbout,
 * ExtractIcon and the rest of it a program asks for.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "commdlg.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

/* the pseudo handles of the modules WINN has: 0xF000 + their number */
#define MODULE_HANDLE(m) ((uint16_t)(0xf000u + (m)))
#define RESOURCE_HANDLE(i) ((uint16_t)(0x5200u + (i)))

static int rd_(args *a, uint32_t far, void *dst, uint32_t n)
{
    if (!n)
        return 1;
    if (!win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, n))
        return a->bad = 1, 0;
    return 1;
}

static int wr_(args *a, uint32_t far, const void *src, uint32_t n)
{
    if (!n)
        return 1;
    if (!win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, src, n))
        return a->bad = 1, 0;
    return 1;
}

/* a string at far (NULL far: ""), 0 if it cannot be read */
static int str_(args *a, uint32_t far, char *dst, uint32_t cap)
{
    dst[0] = 0;
    if (!far)
        return 1;
    if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, dst, cap))
        return a->bad = 1, 0;
    return 1;
}

/* a string out, cut to cap (with its NUL) */
static int str_out(args *a, uint32_t far, const char *s, uint32_t cap)
{
    if (!far || !cap)
        return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (n >= cap)
        n = cap - 1;
    char z = 0;
    return wr_(a, far, s, n) && wr_(a, far + n, &z, 1);
}

static uint16_t dgroup(win_ne_guest_api_context *c)
{
    const win_ne_guest_image *g = c->image;
    return g->segments[g->info.auto_data ? g->info.auto_data : g->info.ss_index].selector;
}

/* ---------------------------------------------------------------- KERNEL */

/* the program's own module name: its file's without the extension */
static int own_module(args *a, const char *name)
{
    const char *p = a->ctx->path ? a->ctx->path : "", *base = p;
    char mine[16], theirs[16];
    for (; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    lstrcpyn(mine, base, sizeof(mine));
    lstrcpyn(theirs, name, sizeof(theirs));
    char *dot = strchr(mine, '.');
    if (dot)
        *dot = 0;
    if ((dot = strchr(theirs, '.')) != NULL)
        *dot = 0;
    return mine[0] && !lstrcmpi(mine, theirs);
}

static uint16_t module_handle(args *a, uint32_t far)
{
    char name[64];
    if (!(far >> 16) || !str_(a, far, name, sizeof(name)))
        return 0;
    int m = win_ne_guest_module(name);
    if (m >= 0)
        return MODULE_HANDLE(m);
    if (own_module(a, name))
        return dgroup(a->ctx);
    const win_ne_guest_image *g = a->ctx->image;      /* a DLL it has loaded */
    char base[16];
    lstrcpyn(base, name, sizeof(base));
    char *dot = strchr(base, '.');
    if (dot)
        *dot = 0;
    for (int i = 0; i < g->dll_count; i++)
        if (g->dlls[i].count && !lstrcmpi(g->dlls[i].name, base))
            return win_ne_guest_dll_instance(g, i);
    return 0;
}

/* a resource the program found: its type and its number or name */
static int resource_index(ne_user *u, uint16_t h)
{
    int i = h - RESOURCE_HANDLE(0);
    return i >= 0 && i < NE_RESOURCES && u->res[i].type ? i : -1;
}

static int resource_bytes(args *a, ne_user *u, int i, uint8_t **d, uint32_t *n)
{
    return win_ne_resource(win_ne_guest_path_of(a->ctx->image, u->res[i].inst, a->ctx->path),
                           u->res[i].type, u->res[i].id,
                           u->res[i].name[0] ? u->res[i].name : NULL, d, n) == WIN_NE_OK;
}

/* FindResource(HINSTANCE, LPCSTR name, LPCSTR type): numbered types (or
 * "#n"); names or numbers */
static uint16_t find_resource(args *a, ne_user *u)
{
    uint32_t type_far = dw(a, 4), name_far = dw(a, 8);
    uint16_t inst = w(a, 12);
    const char *path = win_ne_guest_path_of(a->ctx->image, inst, a->ctx->path);
    char name[32], type[16];
    uint16_t t = (uint16_t)type_far, id = (uint16_t)name_far;
    if (a->bad)
        return 0;
    if (type_far >> 16) {
        if (!str_(a, type_far, type, sizeof(type)) || type[0] != '#')
            return 0;                   /* a type by its name: not here */
        t = 0;
        for (const char *p = type + 1; *p >= '0' && *p <= '9'; p++)
            t = (uint16_t)(t * 10 + (*p - '0'));
    }
    name[0] = 0;
    if ((name_far >> 16) && !str_(a, name_far, name, sizeof(name)))
        return 0;
    int free_slot = -1;
    for (int i = 0; i < NE_RESOURCES; i++) {
        if (!u->res[i].type) {
            if (free_slot < 0)
                free_slot = i;
        } else if (u->res[i].type == t && u->res[i].id == (name[0] ? 0 : id) &&
                   !lstrcmpi(u->res[i].name, name) &&
                   win_ne_guest_path_of(a->ctx->image, u->res[i].inst, a->ctx->path) == path) {
            return RESOURCE_HANDLE(i);
        }
    }
    uint8_t *d;
    uint32_t n;
    if (free_slot < 0 || win_ne_resource(path, t, name[0] ? 0 : id,
                                         name[0] ? name : NULL, &d, &n) != WIN_NE_OK)
        return 0;
    free(d);
    u->res[free_slot].type = t;
    u->res[free_slot].id = name[0] ? 0 : id;
    u->res[free_slot].inst = inst;
    lstrcpyn(u->res[free_slot].name, name, sizeof(u->res[free_slot].name));
    return RESOURCE_HANDLE(free_slot);
}

/* LoadResource: the bytes in a global block of the program's own */
static uint16_t load_resource(args *a, ne_user *u)
{
    int i = resource_index(u, w(a, 4));
    uint8_t *d;
    uint32_t n;
    if (i < 0 || !resource_bytes(a, u, i, &d, &n))
        return 0;
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    uint16_t sel = n <= 65536u ? win_ne_guest_global_alloc(g, n ? n : 1) : 0;
    if (sel && !win_ne_write(a->ctx, sel, 0, d, n)) {
        win_ne_guest_global_free(g, sel);
        sel = 0;
    }
    free(d);
    return sel;
}

/* GetTempFileName(BYTE drive, LPCSTR prefix, UINT unique, LPSTR): in the
 * Windows directory, ~PRE1234.TMP; unique 0: a name no file has, made */
static uint16_t temp_file_name(args *a)
{
    uint32_t out = dw(a, 4), prefix_far = dw(a, 10);
    uint16_t unique = w(a, 8);
    char prefix[16], dir[128], path[160];
    if (!str_(a, prefix_far, prefix, sizeof(prefix)))
        return 0;
    prefix[3] = 0;
    UINT n = GetWindowsDirectory(dir, sizeof(dir));
    if (!n || n > sizeof(dir) - 2)
        return 0;
    if (dir[n - 1] != '\\' && dir[n - 1] != '/')
        lstrcat(dir, "\\");
    uint16_t num = unique ? unique : (uint16_t)GetTickCount();
    for (int tries = 0; tries < 256; tries++, num++) {
        if (!num)
            num = 1;
        wsprintf(path, "%s~%s%04X.TMP", dir, prefix, num);
        if (unique)
            break;
        HFILE f = _lopen(path, OF_READ);
        if (f != HFILE_ERROR) {
            _lclose(f);
            continue;
        }
        f = _lcreat(path, 0);
        if (f == HFILE_ERROR)
            return 0;
        _lclose(f);
        break;
    }
    return str_out(a, out, path, 144) ? num : 0;
}

/* ShellExecute's and WinExec's command: the program itself, or the one
 * WIN.INI's [Extensions] names for the document ("notepad.exe ^.txt") */
static int command_for(const char *file, const char *params, char *cmd, int cap)
{
    const char *dot = NULL;
    for (const char *p = file; *p; p++)     /* the extension's dot */
        if (*p == '.')
            dot = p;
        else if (*p == '\\' || *p == '/')
            dot = NULL;
    char ext[8], assoc[128];
    if (!dot || strlen(dot + 1) > 3)
        return 0;
    lstrcpyn(ext, dot + 1, sizeof(ext));
    if (!lstrcmpi(ext, "EXE") || !lstrcmpi(ext, "COM") || !lstrcmpi(ext, "BAT")) {
        wsprintf(cmd, params && params[0] ? "%s %s" : "%s", file, params);
        return 1;
    }
    if (!GetProfileString("Extensions", ext, "", assoc, sizeof(assoc)) || !assoc[0])
        return 0;
    char *caret = strchr(assoc, '^');
    if (caret)
        *caret = 0;
    while (caret && caret > assoc && caret[-1] == ' ')
        *--caret = 0;
    if ((int)(strlen(assoc) + strlen(file) + 2) > cap)
        return 0;
    wsprintf(cmd, "%s %s", assoc, file);
    return 1;
}

/* ---------------------------------------------------------------- COMMDLG */

/* Win16 OPENFILENAME: lStructSize, hwndOwner, hInstance, lpstrFilter,
 * lpstrCustomFilter, nMaxCustFilter, nFilterIndex, lpstrFile, nMaxFile,
 * lpstrFileTitle, nMaxFileTitle, lpstrInitialDir, lpstrTitle, Flags,
 * nFileOffset, nFileExtension, lpstrDefExt, lCustData, lpfnHook,
 * lpTemplateName: 72 bytes */
static __attribute__((noinline)) uint16_t file_dialog(args *a, int save)
{
    uint32_t far = dw(a, 4);
    uint8_t g[72];
    char filter[512], file[260], title_buf[64], dir[128], caption[64], defext[8];
    if (!rd_(a, far, g, sizeof(g)))
        return 0;
    /* the filter: pairs of strings up to an empty one */
    uint32_t f = rd32(g + 8), n = 0;
    memset(filter, 0, sizeof(filter));
    if (f) {
        while (n + 2 < sizeof(filter)) {
            if (!rd_(a, f + n, filter + n, 1))
                return 0;
            if (!filter[n] && n && !filter[n - 1])
                break;
            n++;
        }
    }
    uint32_t file_far = rd32(g + 24), max_file = rd32(g + 28);
    uint32_t title_far = rd32(g + 32), max_title = rd32(g + 36);
    if (!file_far || !str_(a, file_far, file, sizeof(file)) ||
        !str_(a, rd32(g + 40), dir, sizeof(dir)) || !str_(a, rd32(g + 44), caption, sizeof(caption)) ||
        !str_(a, rd32(g + 56), defext, sizeof(defext)))
        return 0;
    OPENFILENAME ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = rd16(g + 4);
    ofn.lpstrFilter = f ? filter : NULL;
    ofn.nFilterIndex = rd32(g + 20);
    ofn.lpstrFile = file;
    ofn.nMaxFile = max_file < sizeof(file) ? max_file : sizeof(file);
    ofn.lpstrFileTitle = title_far ? title_buf : NULL;
    ofn.nMaxFileTitle = max_title < sizeof(title_buf) ? max_title : sizeof(title_buf);
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrTitle = caption[0] ? caption : NULL;
    ofn.Flags = rd32(g + 48) & ~(DWORD)(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE);
    ofn.lpstrDefExt = defext[0] ? defext : NULL;
    BOOL ok = save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
    wr32(g + 20, ofn.nFilterIndex);
    wr32(g + 48, ofn.Flags);
    wr16(g + 52, (uint16_t)ofn.nFileOffset);
    wr16(g + 54, (uint16_t)ofn.nFileExtension);
    if (!wr_(a, far, g, sizeof(g)))
        return 0;
    if (ok) {
        str_out(a, file_far, file, max_file);
        if (title_far)
            str_out(a, title_far, title_buf, max_title);
    }
    return (uint16_t)ok;
}

/* Find and Replace: the program's FINDREPLACE (lStructSize, hwndOwner,
 * hInstance, Flags, lpstrFindWhat, lpstrReplaceWith, wFindWhatLen,
 * wReplaceWithLen, lCustData, lpfnHook, lpTemplateName: 36 bytes) and the
 * native one the dialog keeps, its strings in buffers of its own; each
 * message the dialog sends brings the native one's fields back */
static __attribute__((noinline)) uint16_t find_dialog(args *a, ne_user *u, int replace)
{
    uint32_t far = dw(a, 4);
    uint8_t g[36];
    int i = 0;
    while (i < NE_FINDS && u->find_dlg[i].fr)
        i++;
    if (i == NE_FINDS || !rd_(a, far, g, sizeof(g)))
        return 0;
    struct ne_find *nf = &u->find_dlg[i];
    FINDREPLACE *fr = (FINDREPLACE *)win_alloc(sizeof(FINDREPLACE));
    if (!fr)
        return 0;
    nf->what_len = rd16(g + 20) < sizeof(nf->what) ? rd16(g + 20) : sizeof(nf->what);
    nf->with_len = rd16(g + 22) < sizeof(nf->with) ? rd16(g + 22) : sizeof(nf->with);
    nf->guest = far;
    if (!str_(a, rd32(g + 12), nf->what, sizeof(nf->what)) ||
        (replace && !str_(a, rd32(g + 16), nf->with, sizeof(nf->with)))) {
        win_free(fr);
        return 0;
    }
    fr->lStructSize = sizeof(*fr);
    fr->hwndOwner = rd16(g + 4);
    fr->Flags = rd32(g + 8) & ~(DWORD)0x180;       /* no hook, no template */
    fr->lpstrFindWhat = nf->what;
    fr->lpstrReplaceWith = replace ? nf->with : NULL;
    fr->wFindWhatLen = nf->what_len;
    fr->wReplaceWithLen = nf->with_len;
    nf->fr = fr;
    HWND h = replace ? ReplaceText(fr) : FindText(fr);
    if (!h) {
        win_free(fr);
        nf->fr = NULL;
    }
    return h;
}

uint32_t ne_find_message(ne_user *u, LPARAM lp)
{
    for (int i = 0; i < NE_FINDS; i++) {
        struct ne_find *nf = &u->find_dlg[i];
        if (!nf->fr || (LPARAM)(uintptr_t)nf->fr != lp)
            continue;
        args a = { u->ctx, NULL, 0, 0 };
        uint8_t g[36];
        if (rd_(&a, nf->guest, g, sizeof(g))) {
            wr32(g + 8, nf->fr->Flags);
            wr_(&a, nf->guest, g, sizeof(g));
            str_out(&a, rd32(g + 12), nf->what, nf->what_len ? nf->what_len : 1);
            if (nf->fr->lpstrReplaceWith)
                str_out(&a, rd32(g + 16), nf->with, nf->with_len ? nf->with_len : 1);
        }
        uint32_t far = nf->guest;
        if (nf->fr->Flags & FR_DIALOGTERM) {    /* the dialog's last: its end */
            win_free(nf->fr);
            nf->fr = NULL;
        }
        return far;
    }
    return 0;
}

/* ---------------------------------------------------------------- the services */

static __attribute__((noinline)) int gdi_more(args *a, uint16_t service, uint32_t *v, int *dword)
{
    switch (service) {
    case 0x3003: case 0x3051:           /* SetMapMode, GetMapMode: MM_TEXT, all there is */
        *v = 1;                         /* MM_TEXT */
        return 1;
    case 0x300c: case 0x300e:           /* SetWindowExt, SetViewportExt: 1:1 */
    case 0x305e: case 0x3060:           /* GetViewportExt, GetWindowExt */
        *v = 0x00010001u;
        *dword = 1;
        return 1;
    case 0x3043: case 0x3063: {         /* DPtoLP, LPtoDP(HDC, LPPOINT, int): the origins */
        int n = (int16_t)w(a, 4);
        uint32_t pts = dw(a, 6);
        HDC dc = w(a, 10);
        DWORD wo = GetWindowOrg(dc), vo = GetViewportOrg(dc);
        int dx = (int16_t)LOWORD(wo) - (int16_t)LOWORD(vo), dy = (int16_t)HIWORD(wo) - (int16_t)HIWORD(vo);
        if (service == 0x3063) {
            dx = -dx;
            dy = -dy;
        }
        for (int i = 0; i < n && !a->bad; i++) {
            uint8_t p[4];
            if (!rd_(a, pts + 4u * (uint32_t)i, p, 4))
                break;
            wr16(p, (uint16_t)((int16_t)rd16(p) + dx));
            wr16(p + 2, (uint16_t)((int16_t)rd16(p + 2) + dy));
            wr_(a, pts + 4u * (uint32_t)i, p, 4);
        }
        *v = !a->bad;
        return 1;
    }
    case 0x3026:                        /* Escape: no printer escapes */
        *v = 0;
        return 1;
    case 0x3035: {                      /* CreateDC(driver, device, output, init) */
        char drv[32], dev[32], out[32];
        if (str_(a, dw(a, 16), drv, sizeof(drv)) && str_(a, dw(a, 12), dev, sizeof(dev)) &&
            str_(a, dw(a, 8), out, sizeof(out)))
            *v = CreateDC(drv, dev[0] ? dev : NULL, out[0] ? out : NULL, NULL);
        return 1;
    }
    case 0x3168: {                      /* CreatePalette(const LOGPALETTE FAR *) */
        uint8_t h[4];
        uint32_t far = dw(a, 4);
        if (!rd_(a, far, h, 4) || rd16(h + 2) > 256)
            return 1;
        uint32_t size = 4u + 4u * rd16(h + 2);
        uint8_t *lp = (uint8_t *)win_alloc(size < sizeof(LOGPALETTE) ? sizeof(LOGPALETTE) : size);
        if (lp && rd_(a, far, lp, size))
            *v = CreatePalette((const LOGPALETTE *)lp);
        win_free(lp);
        return 1;
    }
    case 0x316b: case 0x316c: case 0x3177: {  /* Get/SetPaletteEntries, GetSystemPaletteEntries */
        uint32_t pe = dw(a, 4);
        UINT n = w(a, 8), first = w(a, 10);
        HANDLE h = w(a, 12);
        PALETTEENTRY e[256];
        if (a->bad || n > 256)
            return 1;
        if (service == 0x316c) {
            if (rd_(a, pe, e, 4u * n))
                *v = SetPaletteEntries(h, first, n, e);
        } else {
            UINT got = service == 0x316b ? GetPaletteEntries(h, first, n, pe ? e : NULL)
                                         : GetSystemPaletteEntries(h, first, n, pe ? e : NULL);
            if (!pe || wr_(a, pe, e, 4u * (got < n ? got : n)))
                *v = got;
        }
        return 1;
    }
    }
    return 0;
}

static __attribute__((noinline)) int shell_service(args *a, uint16_t service, uint32_t *v)
{
    char file[128], params[128], cmd[300];
    switch (service - WIN_NE_SERVICE_SHELL) {
    case 1: case 6:                     /* RegOpenKey, RegQueryValue: no such key */
        *v = 2;
        return 1;
    case 2:                             /* RegCreateKey: no registration database */
        *v = 5;
        return 1;
    case 3: case 9: case 12:            /* RegCloseKey, DragAcceptFiles, DragFinish */
        *v = 0;
        return 1;
    case 20: {                          /* ShellExecute(HWND, op, file, params, dir, show) */
        *v = 2;                         /* file not found */
        if (str_(a, dw(a, 14), file, sizeof(file)) && str_(a, dw(a, 10), params, sizeof(params))) {
            if (!command_for(file, params, cmd, sizeof(cmd)))
                *v = 31;                /* no association */
            else
                *v = WinExec(cmd, w(a, 4));
        }
        return 1;
    }
    case 21: {                          /* FindExecutable(file, dir, LPSTR result) */
        *v = 2;
        if (str_(a, dw(a, 12), file, sizeof(file)) && command_for(file, "", cmd, sizeof(cmd))) {
            char *sp = strchr(cmd, ' ');
            if (sp)
                *sp = 0;
            if (str_out(a, dw(a, 4), cmd, 128))
                *v = 33;
        }
        return 1;
    }
    case 22: {                          /* ShellAbout(HWND, app, other, HICON) */
        char app[64], other[128], text[300], caption[80];
        if (str_(a, dw(a, 10), app, sizeof(app)) && str_(a, dw(a, 6), other, sizeof(other))) {
            char *hash = strchr(app, '#');      /* "caption#application" */
            if (hash)
                *hash++ = 0;
            wsprintf(caption, "About %s", app);
            wsprintf(text, "%s\n\nWINN: Windows on murm386\n%s", hash ? hash : app, other);
            MessageBox(w(a, 14), text, caption, MB_OK | MB_ICONINFORMATION);
            *v = 1;
        }
        return 1;
    }
    case 34: {                          /* ExtractIcon(HINSTANCE, file, UINT n) */
        if (str_(a, dw(a, 6), file, sizeof(file))) {
            char full[160];
            win_path(file, full, sizeof(full));
            *v = (uint16_t)(uintptr_t)ExtractIconExN(full, w(a, 4) == 0xffffu ? (UINT)-1 : w(a, 4));
        }
        return 1;
    }
    }
    return 0;
}

int ne_kernel_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    win_ne_guest_api_context *c = a->ctx;
    win_ne_guest_image *g = (win_ne_guest_image *)c->image;
    if (service >= WIN_NE_SERVICE_SHELL && service < WIN_NE_SERVICE_SHELL + 0x1000u)
        return shell_service(a, service, v);
    if (gdi_more(a, service, v, dword))
        return 1;
    switch (service) {
    case 25: case 169:                  /* GlobalCompact, GetFreeSpace: what DOS has left */
        *v = 0x00080000u;
        *dword = 1;
        return 1;
    case 37:                            /* GetCurrentPDB: its PSP */
        *v = g->psp.selector;
        return 1;
    case 47:                            /* GetModuleHandle(LPCSTR) */
        *v = module_handle(a, dw(a, 4));
        return 1;
    case 48:                            /* GetModuleUsage */
        *v = 1;
        return 1;
    case 50: {                          /* GetProcAddress(HMODULE, LPCSTR) */
        uint32_t far = dw(a, 4);
        uint16_t h = w(a, 8), m = (uint16_t)(h - 0xf000u);
        char name[64];
        *dword = 1;
        if (a->bad || ((far >> 16) && !str_(a, far, name, sizeof(name))))
            return 1;
        int d = win_ne_guest_dll_of(g, h);
        if (d >= 0)
            *v = win_ne_guest_dll_proc(g, d, (uint16_t)far, (far >> 16) ? name : NULL);
        else if (m < 6)
            *v = win_ne_guest_proc(g, m, (uint16_t)far, (far >> 16) ? name : NULL);
        return 1;
    }
    case 54:                            /* GetInstanceData: no other instance */
        *v = 0;
        return 1;
    case 60:
        *v = find_resource(a, u);
        return 1;
    case 61:
        *v = load_resource(a, u);
        return 1;
    case 62:                            /* LockResource(HGLOBAL): its block's start */
        *v = (uint32_t)w(a, 4) << 16;
        *dword = 1;
        return 1;
    case 63:                            /* FreeResource */
        *v = win_ne_guest_global_free(g, w(a, 4)) ? 0 : w(a, 4);
        return 1;
    case 65: {                          /* SizeofResource(HINSTANCE, HRSRC) */
        int i = resource_index(u, w(a, 4));
        uint8_t *d;
        uint32_t n = 0;
        if (i >= 0 && resource_bytes(a, u, i, &d, &n))
            free(d);
        *v = n;
        *dword = 1;
        return 1;
    }
    case 92:                            /* GetTempDrive: C: */
        *v = 0x3a43;
        return 1;
    case 95: {                          /* LoadLibrary: the modules WINN has, DLLs */
        uint16_t h = module_handle(a, dw(a, 4));
        char name[64];
        if (!h && str_(a, dw(a, 4), name, sizeof(name)) && name[0]) {
            int d = win_ne_guest_load_library(g, name);
            if (d >= 0) {
                win_ne_dll_init(c);     /* its entry, and theirs it needs */
                h = win_ne_guest_dll_instance(g, d);
            }
        }
        *v = h ? h : 2;                 /* file not found */
        return 1;
    }
    case 96:                            /* FreeLibrary */
        *v = 0;
        return 1;
    case 97:
        *v = temp_file_name(a);
        return 1;
    case 107:                           /* SetErrorMode: the previous one */
        *v = u->error_mode;
        u->error_mode = w(a, 4);
        return 1;
    case 132:                           /* GetWinFlags */
        *v = WIN_NE_WINFLAGS;
        *dword = 1;
        return 1;
    case 133:                           /* GetExePtr: the module is the instance */
        *v = w(a, 4);
        return 1;
    case 137: {                         /* FatalAppExit(UINT, LPCSTR): a message, the end */
        char msg[128];
        if (str_(a, dw(a, 4), msg, sizeof(msg)))
            MessageBox(0, msg, "Application Error", MB_OK | MB_ICONSTOP);
        c->exited = 1;
        c->exit_code = 0xff;
        return 1;
    }
    case 155:                           /* GetTaskDS */
        *v = dgroup(c);
        return 1;
    case 166: {                         /* WinExec(LPCSTR, UINT) */
        char cmd[260];
        *v = 2;
        if (str_(a, dw(a, 6), cmd, sizeof(cmd)))
            *v = WinExec(cmd, w(a, 4));
        return 1;
    }
    case 0x1010:                        /* ClipCursor: not confined */
        return 1;
    case 0x10b5: {                      /* SetSysColors(int, const int FAR *, const COLORREF FAR *) */
        int n = (int16_t)w(a, 12), idx[32];
        COLORREF col[32];
        uint8_t gi[64], gc[128];
        if (a->bad || n <= 0 || n > 32 || !rd_(a, dw(a, 8), gi, 2u * (uint32_t)n) ||
            !rd_(a, dw(a, 4), gc, 4u * (uint32_t)n))
            return 1;
        for (int i = 0; i < n; i++) {
            idx[i] = (int16_t)rd16(gi + 2 * i);
            col[i] = rd32(gc + 4 * i);
        }
        SetSysColors(n, idx, col);
        return 1;
    }
    case 0x111c:                        /* GetFreeSystemResources: plenty */
        *v = 90;
        return 1;
    case 0x11c3: {                      /* TranslateMDISysAccel(HWND, LPMSG) */
        MSG m;
        if (msg_from_guest(a, dw(a, 4), &m))
            *v = (uint16_t)TranslateMDISysAccel(w(a, 8), &m);
        return 1;
    }
    case WIN_NE_SERVICE_COMMDLG + 1:
    case WIN_NE_SERVICE_COMMDLG + 2:
        *v = file_dialog(a, service == WIN_NE_SERVICE_COMMDLG + 2);
        return 1;
    case WIN_NE_SERVICE_COMMDLG + 11:
    case WIN_NE_SERVICE_COMMDLG + 12:
        *v = find_dialog(a, u, service == WIN_NE_SERVICE_COMMDLG + 12);
        return 1;
    case WIN_NE_SERVICE_COMMDLG + 26:
        *v = CommDlgExtendedError();
        *dword = 1;
        return 1;
    case WIN_NE_SERVICE_COMMDLG + 27: {   /* GetFileTitle(LPCSTR, LPSTR, UINT) */
        char file[260], title[64];
        UINT size = w(a, 4);
        if (str_(a, dw(a, 10), file, sizeof(file))) {
            short r = GetFileTitle(file, title, sizeof(title));
            *v = (uint16_t)r;
            if (!r && !str_out(a, dw(a, 6), title, size))
                *v = (uint16_t)(strlen(title) + 1);
        }
        return 1;
    }
    }
    return 0;
}

#endif
