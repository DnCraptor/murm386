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

/* points of the program's (POINT FAR *, n of them) natively, at most 1024 */
static POINT *points_in(args *a, uint32_t far, int n)
{
    if (n <= 0 || n > 1024)
        return NULL;
    uint8_t *g = (uint8_t *)win_alloc(4u * (uint32_t)n);
    POINT *p = (POINT *)win_alloc(sizeof(POINT) * (size_t)n);
    if (g && p && rd_(a, far, g, 4u * (uint32_t)n)) {
        for (int i = 0; i < n; i++) {
            p[i].x = (int16_t)rd16(g + 4 * i);
            p[i].y = (int16_t)rd16(g + 4 * i + 2);
        }
        win_free(g);
        return p;
    }
    win_free(g);
    win_free(p);
    return NULL;
}

/* PolyPolygon, CreatePolyPolygonRgn: the points and their counts */
static __attribute__((noinline)) int poly_polygon(args *a, int region, uint32_t *v)
{
    int n = (int16_t)w(a, region ? 6 : 4);
    uint32_t counts = dw(a, region ? 8 : 6), pts = dw(a, region ? 12 : 10);
    if (n <= 0 || n > 64)
        return 1;
    uint8_t c16[128];
    int c[64], total = 0;
    if (!rd_(a, counts, c16, 2u * (uint32_t)n))
        return 1;
    for (int i = 0; i < n; i++)
        total += c[i] = (int16_t)rd16(c16 + 2 * i);
    POINT *p = points_in(a, pts, total);
    if (p)
        *v = region ? (uint32_t)CreatePolyPolygonRgn(p, c, n, (int16_t)w(a, 4))
                    : (uint32_t)PolyPolygon(w(a, 14), p, c, n);
    win_free(p);
    return 1;
}

/* EnumFonts, EnumFontFamilies(HDC, LPCSTR, proc, LPARAM): each font's
 * LOGFONT and TEXTMETRIC (NEWTEXTMETRIC's 10 bytes more, NEWLOGFONT's 96)
 * on the program's stack for its procedure */
typedef struct {
    win_ne_guest_api_context *c;
    uint32_t proc, lp;
    int family;
} font_enum;

static int CALLBACK ne_fontproc(const LOGFONT *lf, const TEXTMETRIC *tm, int type, LPARAM lp)
{
    const font_enum *e = (const font_enum *)(uintptr_t)lp;
    uint8_t blob[146 + 42];
    uint16_t lfn = e->family ? 146 : 50, tmn = e->family ? 42 : 32;   /* even */
    memset(blob, 0, sizeof(blob));
    uint8_t *g = blob;
    wr16(g, (uint16_t)lf->lfHeight); wr16(g + 2, (uint16_t)lf->lfWidth);
    wr16(g + 4, (uint16_t)lf->lfEscapement); wr16(g + 6, (uint16_t)lf->lfOrientation);
    wr16(g + 8, (uint16_t)lf->lfWeight);
    g[10] = lf->lfItalic; g[11] = lf->lfUnderline; g[12] = lf->lfStrikeOut;
    g[13] = lf->lfCharSet; g[14] = lf->lfOutPrecision; g[15] = lf->lfClipPrecision;
    g[16] = lf->lfQuality; g[17] = lf->lfPitchAndFamily;
    memcpy(g + 18, lf->lfFaceName, LF_FACESIZE);
    if (e->family)                      /* NEWLOGFONT: its full name, its style */
        lstrcpyn((char *)g + 50, lf->lfFaceName, 64);
    g = blob + lfn;
    const int v16[] = { tm->tmHeight, tm->tmAscent, tm->tmDescent, tm->tmInternalLeading,
                        tm->tmExternalLeading, tm->tmAveCharWidth, tm->tmMaxCharWidth,
                        tm->tmWeight };
    for (int i = 0; i < 8; i++)
        wr16(g + 2 * i, (uint16_t)v16[i]);
    g[16] = tm->tmItalic; g[17] = tm->tmUnderlined; g[18] = tm->tmStruckOut;
    g[19] = tm->tmFirstChar; g[20] = tm->tmLastChar; g[21] = tm->tmDefaultChar;
    g[22] = tm->tmBreakChar; g[23] = tm->tmPitchAndFamily; g[24] = tm->tmCharSet;
    wr16(g + 25, (uint16_t)tm->tmOverhang); wr16(g + 27, (uint16_t)tm->tmDigitizedAspectX);
    wr16(g + 29, (uint16_t)tm->tmDigitizedAspectY);
    if (e->family) {                    /* NEWTEXTMETRIC: flags, em, cell height, average */
        wr16(g + 35, (uint16_t)tm->tmHeight);
        wr16(g + 37, (uint16_t)tm->tmHeight);
        wr16(g + 39, (uint16_t)tm->tmAveCharWidth);
    }
    uint16_t len = (uint16_t)(lfn + tmn);
    uint32_t far = win_ne_blob_far(e->c, len);
    const uint16_t words[7] = { (uint16_t)(far >> 16), (uint16_t)far, (uint16_t)(far >> 16),
                                (uint16_t)(far + lfn), (uint16_t)type, (uint16_t)(e->lp >> 16),
                                (uint16_t)e->lp };
    return (int16_t)win_ne_call(e->c, e->proc, words, 7, blob, len);
}

/* LineDDA's points to the program's procedure (x, y, LPARAM) */
typedef struct {
    win_ne_guest_api_context *c;
    uint32_t proc, lp;
} dda_ctx;

static void CALLBACK ne_ddaproc(int x, int y, LPARAM lp)
{
    const dda_ctx *d = (const dda_ctx *)(uintptr_t)lp;
    const uint16_t words[4] = { (uint16_t)x, (uint16_t)y, (uint16_t)(d->lp >> 16),
                                (uint16_t)d->lp };
    win_ne_call(d->c, d->proc, words, 4, NULL, 0);
}

/* A DIB's header of the program's (BITMAPINFOHEADER or BITMAPCOREHEADER,
 * its colours after it: RGBQUADs, RGBTRIPLEs, DIB_PAL_COLORS' indices;
 * usage -1: the header alone) as a native one, room for 256 colours more;
 * its bytes a row, its rows */
static uint8_t *dib_info_in(args *a, uint32_t far, int usage, uint32_t *len, uint32_t *pitch,
                            uint32_t *rows)
{
    uint8_t h[40];
    if (!rd_(a, far, h, 4))
        return NULL;
    uint32_t hs = rd16(h) | (uint32_t)rd16(h + 2) << 16;
    if ((hs != 12 && hs != 40) || !rd_(a, far, h, hs))
        return NULL;
    uint32_t wd = hs == 12 ? rd16(h + 4) : rd16(h + 4) | (uint32_t)rd16(h + 6) << 16;
    int32_t ht = hs == 12 ? rd16(h + 6) : (int32_t)(rd16(h + 8) | (uint32_t)rd16(h + 10) << 16);
    uint32_t bpp = hs == 12 ? rd16(h + 10) : rd16(h + 14);
    uint32_t used = hs == 12 ? 0 : rd16(h + 32) | (uint32_t)rd16(h + 34) << 16;
    if (wd > 4096 || bpp > 32)
        return NULL;
    uint32_t colors = bpp <= 8 ? (used && used < (1u << bpp) ? used : 1u << bpp) : 0;
    uint32_t entry = usage == DIB_PAL_COLORS ? 2 : hs == 12 ? 3 : 4;
    *len = hs + (usage < 0 ? 0 : colors * entry);
    *pitch = (wd * bpp + 31u) / 32u * 4u;
    *rows = (uint32_t)(ht < 0 ? -ht : ht);
    uint8_t *bi = (uint8_t *)win_alloc(hs + 256u * 4u);
    if (bi && !rd_(a, far, bi, *len)) {
        win_free(bi);
        bi = NULL;
    }
    return bi;
}

/* n bytes of the program's at far, in its segment */
static uint8_t *bytes_in(args *a, uint32_t far, uint32_t n)
{
    if (!n || (uint32_t)(uint16_t)far + n > 0x10000u)
        return NULL;
    uint8_t *d = (uint8_t *)win_alloc(n);
    if (d && !rd_(a, far, d, n)) {
        win_free(d);
        d = NULL;
    }
    return d;
}

/* SetDIBitsToDevice, StretchDIBits, CreateDIBitmap, SetDIBits, GetDIBits,
 * CreateDIBPatternBrush */
static __attribute__((noinline)) int dib_service(args *a, uint16_t service, uint32_t *v)
{
    win_ne_guest_api_context *c = a->ctx;
    uint32_t len = 0, pitch = 0, rows = 0, bfar, bits_far, n;
    int usage;
    uint8_t *bi = NULL, *bits = NULL;
    switch (service) {
    case 0x31bb:                        /* SetDIBitsToDevice(HDC, x, y, w, h, sx, sy, start,
                                         * lines, bits, BITMAPINFO FAR *, usage) */
        usage = w(a, 4);
        bfar = dw(a, 6);
        bits_far = dw(a, 10);
        if ((bi = dib_info_in(a, bfar, usage, &len, &pitch, &rows)) &&
            (bits = bytes_in(a, bits_far, pitch * w(a, 14))))
            *v = (uint16_t)SetDIBitsToDevice(w(a, 30), (int16_t)w(a, 28), (int16_t)w(a, 26),
                                             (int16_t)w(a, 24), (int16_t)w(a, 22),
                                             (int16_t)w(a, 20), (int16_t)w(a, 18), w(a, 16),
                                             w(a, 14), bits, (const BITMAPINFO *)bi, usage);
        break;
    case 0x31b7:                        /* StretchDIBits(HDC, x, y, w, h, sx, sy, sw, sh, bits,
                                         * BITMAPINFO FAR *, usage, rop) */
        usage = w(a, 8);
        bfar = dw(a, 10);
        bits_far = dw(a, 14);
        if ((bi = dib_info_in(a, bfar, usage, &len, &pitch, &rows)) &&
            (bits = bytes_in(a, bits_far, pitch * rows)))
            *v = (uint16_t)StretchDIBits(w(a, 34), (int16_t)w(a, 32), (int16_t)w(a, 30),
                                         (int16_t)w(a, 28), (int16_t)w(a, 26),
                                         (int16_t)w(a, 24), (int16_t)w(a, 22),
                                         (int16_t)w(a, 20), (int16_t)w(a, 18), bits,
                                         (const BITMAPINFO *)bi, usage, dw(a, 4));
        break;
    case 0x31ba: {                      /* CreateDIBitmap(HDC, BITMAPINFOHEADER FAR *, init,
                                         * bits, BITMAPINFO FAR *, usage) */
        uint8_t *bih;
        uint32_t hl, hp, hr;
        usage = w(a, 4);
        if (!(bih = dib_info_in(a, dw(a, 18), -1, &hl, &hp, &hr)))
            break;
        if ((dw(a, 14) & CBM_INIT) && dw(a, 10) && dw(a, 6) &&
            (bi = dib_info_in(a, dw(a, 6), usage, &len, &pitch, &rows)))
            bits = bytes_in(a, dw(a, 10), pitch * rows);
        *v = (uint16_t)CreateDIBitmap(w(a, 22), (const BITMAPINFOHEADER *)bih,
                                      bits ? dw(a, 14) : 0, bits, (const BITMAPINFO *)bi, usage);
        win_free(bih);
        break;
    }
    case 0x31b8:                        /* SetDIBits(HDC, HBITMAP, start, lines, bits,
                                         * BITMAPINFO FAR *, usage) */
        usage = w(a, 4);
        if ((bi = dib_info_in(a, dw(a, 6), usage, &len, &pitch, &rows)) &&
            (bits = bytes_in(a, dw(a, 10), pitch * w(a, 14))))
            *v = (uint16_t)SetDIBits(w(a, 20), w(a, 18), w(a, 16), w(a, 14), bits,
                                     (const BITMAPINFO *)bi, usage);
        break;
    case 0x31b9: {                      /* GetDIBits(HDC, HBITMAP, start, lines, bits,
                                         * BITMAPINFO FAR *, usage) */
        BITMAP bm;
        usage = w(a, 4);
        bfar = dw(a, 6);
        bits_far = dw(a, 10);
        if (!GetObject(w(a, 18), sizeof(bm), &bm) ||
            !(bi = dib_info_in(a, bfar, -1, &len, &pitch, &rows)))
            break;
        n = w(a, 14);
        uint32_t want = ((const BITMAPINFOHEADER *)bi)->biBitCount, out = 0;
        uint32_t bpitch = ((uint32_t)bm.bmWidth * want + 31u) / 32u * 4u;
        if (bits_far && want && (uint32_t)(uint16_t)bits_far + bpitch * n > 0x10000u)
            break;
        uint32_t size = bpitch * n;
        if (bits_far && want)
            bits = (uint8_t *)win_alloc(size ? size : 1u);
        if (bits_far && want && !bits)
            break;
        *v = (uint16_t)GetDIBits(w(a, 20), w(a, 18), w(a, 16), n, bits,
                                 (BITMAPINFO *)bi, usage);
        want = ((const BITMAPINFOHEADER *)bi)->biBitCount;
        out = 40u + (want && want <= 8 ? 4u << want : 0u);
        wr_(a, bfar, bi, out);
        if (bits)
            wr_(a, bits_far, bits, bpitch * n);
        break;
    }
    case 0x31bd: {                      /* CreateDIBPatternBrush(HGLOBAL, usage): its block */
        win_ne_guest_segment *s = win_ne_guest_global((win_ne_guest_image *)c->image, w(a, 6));
        uint8_t *d = s ? bytes_in(a, (uint32_t)w(a, 6) << 16, s->size) : NULL;
        HGLOBAL hg = d ? GlobalAlloc(GMEM_MOVEABLE, s->size) : 0;
        void *p = hg ? GlobalLock(hg) : NULL;
        if (p) {
            memcpy(p, d, s->size);
            GlobalUnlock(hg);
            *v = (uint16_t)CreateDIBPatternBrush(hg, w(a, 4));
        }
        if (hg)
            GlobalFree(hg);
        win_free(d);
        break;
    }
    default:
        return 0;
    }
    win_free(bi);
    win_free(bits);
    return 1;
}

/* GDI's calls with buffers of their own: apart, off gdi_more's frame */
static __attribute__((noinline)) int gdi_buffers(args *a, uint16_t service, uint32_t *v)
{
    switch (service) {
    case 0x315e: {                      /* GetCharWidth(HDC, first, last, int FAR *) */
        UINT first = w(a, 10), last = w(a, 8);
        int wd[256];
        uint8_t g[512];
        if (last > 255 || first > last || !GetCharWidth(w(a, 12), first, last, wd))
            return 1;
        for (UINT i = 0; i <= last - first; i++)
            wr16(g + 2 * i, (uint16_t)wd[i]);
        *v = wr_(a, dw(a, 4), g, 2u * (last - first + 1u));
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

/* EnumMetaFile's procedure of the program's: (HDC, HANDLETABLE FAR *,
 * METARECORD FAR *, int, LPARAM); the table on its stack (its changes
 * back), the record in a block of the program's, as large as the longest */
typedef struct {
    args *a;
    uint32_t proc, lp;
    uint16_t sel;                       /* the record's block */
    uint32_t size;
} mf_enum_ctx;

static int ne_mfproc(void *ctx, HDC h, HGDIOBJ *table, int n, const uint8_t *rec, uint32_t bytes)
{
    mf_enum_ctx *e = (mf_enum_ctx *)ctx;
    uint8_t t[2 * 256];
    if (n > 256 || bytes > e->size || !wr_(e->a, (uint32_t)e->sel << 16, rec, bytes))
        return 0;
    for (int i = 0; i < n; i++)
        wr16(t + 2 * i, table[i]);
    uint16_t len = (uint16_t)(2 * (n ? n : 1));
    uint32_t far = win_ne_blob_far(e->a->ctx, len);
    const uint16_t words[8] = { h, (uint16_t)(far >> 16), (uint16_t)far, e->sel, 0, (uint16_t)n,
                                (uint16_t)(e->lp >> 16), (uint16_t)e->lp };
    int r = (int16_t)win_ne_call(e->a->ctx, e->proc, words, 8, t, len);
    for (int i = 0; i < n; i++)                 /* PlayMetaFileRecord's objects */
        table[i] = rd16(t + 2 * i);
    return r;
}

/* the metafiles' bits in the program's global blocks, the records to its
 * procedure, one record of its played */
static __attribute__((noinline)) int metafile_service(args *a, uint16_t service, uint32_t *v)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    switch (service) {
    case 0x309f: {                      /* GetMetaFileBits(HMETAFILE): a block of its bits */
        uint32_t n = 0;
        const uint8_t *p = mf_bits(w(a, 4), &n);
        uint16_t sel = p && n <= 0x10000u ? win_ne_guest_global_alloc(g, n) : 0;
        if (sel && !wr_(a, (uint32_t)sel << 16, p, n)) {
            win_ne_guest_global_free(g, sel);
            sel = 0;
        }
        if (sel)
            DeleteMetaFile(w(a, 4));
        *v = sel;
        return 1;
    }
    case 0x30a0: case 0x30c4: {         /* SetMetaFileBits(Better)(HGLOBAL): the block taken */
        uint16_t sel = w(a, 4);
        win_ne_guest_segment *s = win_ne_guest_global(g, sel);
        uint8_t *p = s ? bytes_in(a, (uint32_t)sel << 16, s->size) : NULL;
        *v = p ? mf_from_bits(p, s->size) : 0;
        win_free(p);
        if (*v) {
            win_ne_guest_global_free(g, sel);
            win_ne_forget_selector(a->ctx, a->cpu, sel);
        }
        return 1;
    }
    case 0x30af: {                      /* EnumMetaFile(HDC, HMETAFILE, proc, LPARAM) */
        uint32_t n = 0;
        const uint8_t *p = mf_bits(w(a, 12), &n);
        mf_enum_ctx e = { a, dw(a, 8), dw(a, 4), 0, 0 };
        if (p && n >= 18)
            e.size = (uint32_t)(p[12] | p[13] << 8 | p[14] << 16 | (uint32_t)p[15] << 24) * 2u;
        if (a->bad || !e.proc || !e.size || e.size > 0x10000u ||
            !(e.sel = win_ne_guest_global_alloc(g, e.size)))
            return 1;
        *v = (uint16_t)mf_enum(w(a, 14), w(a, 12), ne_mfproc, &e);
        win_ne_guest_global_free(g, e.sel);
        win_ne_forget_selector(a->ctx, a->cpu, e.sel);
        return 1;
    }
    case 0x30b0: {                      /* PlayMetaFileRecord(HDC, HANDLETABLE FAR *,
                                         * METARECORD FAR *, UINT) */
        int n = w(a, 4);
        uint32_t ht = dw(a, 10), mr = dw(a, 6);
        uint8_t head[4], t[2 * 256];
        HGDIOBJ table[256];
        if (a->bad || n > 256 || !rd_(a, mr, head, 4) || !rd_(a, ht, t, 2u * (uint32_t)n))
            return 1;
        uint32_t bytes = rd16(head) * 2u;
        uint8_t *rec = rd16(head + 2) || bytes < 6 ? NULL : bytes_in(a, mr, bytes);
        if (!rec)
            return 1;
        for (int i = 0; i < n; i++)
            table[i] = rd16(t + 2 * i);
        mf_play_record(w(a, 14), table, n, rec, bytes);
        win_free(rec);
        for (int i = 0; i < n; i++)
            wr16(t + 2 * i, table[i]);
        wr_(a, ht, t, 2u * (uint32_t)n);
        return 1;
    }
    default:
        return 0;
    }
}

static __attribute__((noinline)) int gdi_more(args *a, uint16_t service, uint32_t *v, int *dword)
{
    if (dib_service(a, service, v))
        return 1;
    if (metafile_service(a, service, v))
        return 1;
    switch (service) {
    case 0x3043: case 0x3063: {         /* DPtoLP, LPtoDP(HDC, LPPOINT, int) */
        int n = (int16_t)w(a, 4);
        uint32_t pts = dw(a, 6);
        HDC dc = w(a, 10);
        for (int i = 0; i < n && !a->bad; i++) {
            uint8_t p[4];
            if (!rd_(a, pts + 4u * (uint32_t)i, p, 4))
                break;
            POINT pt = { (int16_t)rd16(p), (int16_t)rd16(p + 2) };
            if (service == 0x3063)
                LPtoDP(dc, &pt, 1);
            else
                DPtoLP(dc, &pt, 1);
            wr16(p, (uint16_t)pt.x);
            wr16(p + 2, (uint16_t)pt.y);
            wr_(a, pts + 4u * (uint32_t)i, p, 4);
        }
        *v = !a->bad;
        return 1;
    }
    case 0x31c2: case 0x31c3:           /* PolyPolygon, CreatePolyPolygonRgn */
        return poly_polygon(a, service == 0x31c3, v);
    case 0x315e:                        /* GetCharWidth */
    case 0x316b: case 0x316c: case 0x3177:  /* the palettes' entries */
        return gdi_buffers(a, service, v);
    case 0x3064: {                      /* LineDDA(x0, y0, x1, y1, proc, LPARAM) */
        dda_ctx d = { a->ctx, dw(a, 8), dw(a, 4) };
        if (!a->bad && d.proc)
            LineDDA((int16_t)w(a, 18), (int16_t)w(a, 16), (int16_t)w(a, 14), (int16_t)w(a, 12),
                    ne_ddaproc, (LPARAM)(uintptr_t)&d);
        return 1;
    }
    case 0x3046: case 0x314a: {         /* EnumFonts, EnumFontFamilies(HDC, LPCSTR, proc, LPARAM) */
        char face[LF_FACESIZE];
        font_enum e = { a->ctx, dw(a, 8), dw(a, 4), service == 0x314a };
        face[0] = 0;
        if (dw(a, 12) && !str_(a, dw(a, 12), face, sizeof(face)))
            return 1;
        if (e.proc)
            *v = (uint16_t)EnumFonts(w(a, 16), face, ne_fontproc, (LPARAM)(uintptr_t)&e);
        return 1;
    }
    /* printing: no printers */
    case 0x3179: case 0x317d:           /* StartDoc(HDC, DOCINFO FAR *), SetAbortProc */
    case 0x317a: case 0x317b:           /* EndDoc, StartPage */
    case 0x317c: case 0x317e:           /* EndPage, AbortDoc */
        *v = 0xffff;                    /* SP_ERROR */
        return 1;
    case 0x309b:                        /* QueryAbort(HDC, int): stop */
        *v = 0;
        return 1;
    case 0x3026:                        /* Escape: no printer escapes */
        *v = 0;
        return 1;
    case 0x3035: case 0x3099: {         /* CreateDC, CreateIC(driver, device, output, init) */
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
    }
    return 0;
}

/* whether the program may read (or write, or run) n bytes at far: a
 * selector of its own, the bytes within its limit */
static int good_ptr(args *a, uint32_t far, uint32_t n, int write, int code)
{
    const win_ne_guest_image *g = a->ctx->image;
    uint32_t base, limit, off = (uint16_t)far;
    int is_code;
    if (!win_ne_guest_selector_get(g, (uint16_t)(far >> 16), &base, &limit, &is_code))
        return 0;
    if ((write && is_code) || (code && !is_code))
        return 0;
    return !n || (off <= limit && n - 1u <= limit - off);
}

/* a string at far of up to n characters (to its NUL) readable */
static int good_string(args *a, uint32_t far, uint32_t n)
{
    uint32_t base, limit, off = (uint16_t)far;
    int code;
    uint16_t sel = (uint16_t)(far >> 16);
    if (!win_ne_guest_selector_get(a->ctx->image, sel, &base, &limit, &code) || off > limit)
        return 0;
    while (n) {
        uint8_t buf[32];
        uint32_t k = limit - off + 1u < sizeof(buf) ? limit - off + 1u : (uint32_t)sizeof(buf);
        if (k > n)
            k = n;
        if (!k || !win_ne_read(a->ctx, sel, off, buf, k))
            return 0;
        for (uint32_t i = 0; i < k; i++)
            if (!buf[i])
                return 1;
        n -= k;
        off += k;
        if (n && off > limit)
            return 0;
    }
    return 1;
}

/* KERNEL's memory, selectors and the rest a program asks for */
static __attribute__((noinline)) int kernel_misc(args *a, uint16_t service, uint32_t *v,
                                                 int *dword)
{
    win_ne_guest_api_context *c = a->ctx;
    win_ne_guest_image *g = (win_ne_guest_image *)c->image;
    uint32_t base, limit;
    int code;
    switch (service) {
    case 1:                             /* FatalExit(int): the end */
        c->exited = 1;
        c->exit_code = w(a, 4);
        return 1;
    case 29:                            /* Yield: the other tasks' turn */
        Yield();
        return 1;
    case 105: case 167:                 /* GetExeVersion, GetExpWinVer(HINSTANCE) */
        *v = 0x030a;
        return 1;
    case 199:                           /* SetHandleCount(UINT): as many */
        *v = w(a, 4);
        return 1;
    case 203:                           /* DebugBreak */
        return 1;
    case 353: {                         /* lstrcpyn(LPSTR, LPCSTR, int): dst */
        int n = (int16_t)w(a, 4);
        uint32_t src = dw(a, 6), dst = dw(a, 10);
        char s[256];
        *dword = 1;
        if (a->bad || n <= 0)
            return 1;
        if (n > (int)sizeof(s))
            n = (int)sizeof(s);
        s[0] = 0;
        for (int i = 0; i < n - 1; i++) {   /* the source may end at its segment's end */
            if (!rd_(a, src + (uint32_t)i, &s[i], 1))
                return 1;
            if (!s[i])
                break;
            s[i + 1] = 0;
        }
        s[n - 1] = 0;
        if (wr_(a, dst, s, (uint32_t)strlen(s) + 1u))
            *v = dst;
        return 1;
    }
    case 348: {                         /* hmemcpy(void huge *, const void huge *, LONG) */
        uint32_t n = dw(a, 4), src = dw(a, 8), dst = dw(a, 12);
        uint8_t buf[128];
        /* within the blocks: a block is 64K at most here, no selectors
         * tiled beyond (__AHINCR), so a copy past 64K ends there */
        while (!a->bad && n) {
            uint32_t k = n < sizeof(buf) ? n : (uint32_t)sizeof(buf);
            if (0x10000u - (uint16_t)src < k)
                k = 0x10000u - (uint16_t)src;
            if (0x10000u - (uint16_t)dst < k)
                k = 0x10000u - (uint16_t)dst;
            if (!rd_(a, src, buf, k) || !wr_(a, dst, buf, k))
                break;
            n -= k;
            src += k;
            dst += k;
            if (!(uint16_t)src || !(uint16_t)dst)
                break;
        }
        return 1;
    }
    case 334: case 346:                 /* IsBadReadPtr(LPVOID, UINT), IsBadHugeReadPtr(, DWORD) */
    case 335: case 347:                 /* IsBadWritePtr, IsBadHugeWritePtr */
        *v = !good_ptr(a, dw(a, service >= 346 ? 8 : 6),
                       service >= 346 ? dw(a, 4) : w(a, 4), service == 335 || service == 347, 0);
        return 1;
    case 336:                           /* IsBadCodePtr(FARPROC) */
        *v = !good_ptr(a, dw(a, 4), 1, 0, 1);
        return 1;
    case 337:                           /* IsBadStringPtr(LPCSTR, UINT) */
        *v = !good_string(a, dw(a, 6), w(a, 4));
        return 1;
    /* selectors of its own: aliases (ne_guest.c) */
    case 175:                           /* AllocSelector(UINT): a copy of it (0: an empty one) */
        *v = win_ne_guest_selector_alloc(g, w(a, 4), -1);
        return 1;
    case 170: case 171:                 /* AllocCStoDSAlias, AllocDStoCSAlias(UINT) */
        *v = win_ne_guest_selector_alloc(g, w(a, 4), service == 171);
        return 1;
    case 176: case 185: {               /* FreeSelector, GlobalDOSFree(UINT): 0 */
        uint16_t sel = w(a, 4);
        *v = sel;
        if (win_ne_guest_global_free(g, sel)) {
            win_ne_forget_selector(c, a->cpu, sel);
            *v = 0;
        }
        return 1;
    }
    case 177: {                         /* PrestoChangoSelector(src, dst): dst, the other kind */
        uint16_t dst = w(a, 4);
        *v = win_ne_guest_selector_get(g, w(a, 6), &base, &limit, &code) &&
             win_ne_guest_selector_set(g, dst, base, limit, !code) ? dst : 0;
        return 1;
    }
    case 186: case 188:                 /* GetSelectorBase, GetSelectorLimit(UINT): DWORD */
        *v = win_ne_guest_selector_get(g, w(a, 4), &base, &limit, &code) ?
             (service == 186 ? base : limit) : 0;
        *dword = 1;
        return 1;
    case 187: case 189: {               /* SetSelectorBase(UINT, DWORD): it; SetSelectorLimit: 0 */
        uint16_t sel = w(a, 8);
        uint32_t x = dw(a, 4);
        int ok = win_ne_guest_selector_get(g, sel, &base, &limit, &code) &&
                 win_ne_guest_selector_set(g, sel, service == 187 ? x : base,
                                           service == 189 ? x : limit, code);
        *v = service == 187 ? (ok ? sel : 0) : 0;
        return 1;
    }
    case 206:                           /* AllocSelectorArray(UINT): n of them, in a row */
        *v = win_ne_guest_selector_array(g, w(a, 4));
        return 1;
    case 196: {                         /* SelectorAccessRights(sel, op, rights): 0 get, 1 set */
        uint16_t sel = w(a, 8);
        *v = 0;
        if (!win_ne_guest_selector_get(g, sel, &base, &limit, &code))
            return 1;
        if (!w(a, 6)) {                 /* the access byte; the limit's high bits' flags */
            *v = (uint16_t)(0xf2u | (code ? 0x08u : 0u));
        } else {
            win_ne_guest_selector_set(g, sel, base, limit, (w(a, 4) & 0x08u) != 0);
            *v = 0;
        }
        return 1;
    }
    case 159: {                         /* GlobalHandleNoRIP(UINT): its handle and selector */
        uint16_t sel = w(a, 4);
        *v = win_ne_guest_global(g, sel) ? (uint32_t)sel << 16 | sel : 0;
        *dword = 1;
        return 1;
    }
    case 352: {                         /* lstrcatn(LPSTR, LPCSTR, int): to fit its n */
        int n = (int16_t)w(a, 4);
        uint32_t src = dw(a, 6), dst = dw(a, 10);
        char d[256], s[256];
        *dword = 1;
        if (n <= 0 || n > (int)sizeof(d) || !str_(a, dst, d, sizeof(d)) ||
            !str_(a, src, s, sizeof(s)))
            return 1;
        int len = (int)strlen(d), add = (int)strlen(s);
        if (len + add > n - 1)
            add = n - 1 - len > 0 ? n - 1 - len : 0;
        memcpy(d + len, s, (size_t)add);
        d[len + add] = 0;
        if (wr_(a, dst, d, (uint32_t)(len + add) + 1u))
            *v = dst;
        return 1;
    }
    case 93:                            /* GetCodeHandle(FARPROC): its segment's handle */
        *v = (uint16_t)(dw(a, 4) >> 16);
        return 1;
    case 98:                            /* GetLastDiskChange */
    case 154:                           /* GlobalNotify(proc): nothing is discarded */
        *v = 0;
        return 1;
    case 14:                            /* LocalNotify(proc): the one before */
        *v = 0;
        *dword = 1;
        return 1;
    case 138:                           /* GetHeapSpaces(HMODULE): free, size */
        *v = 0xfff0e000u;
        *dword = 1;
        return 1;
    case 316:                           /* GetFreeMemInfo: no paging */
        *v = 0xffffffffu;
        *dword = 1;
        return 1;
    case 150:                           /* DirectedYield(HTASK) */
        Yield();
        return 1;
    case 46:                            /* FreeModule */
        *v = 0;
        return 1;
    case 45: {                          /* LoadModule(LPCSTR, LPVOID): a program (or -1: a
                                         * library, as LoadLibrary) */
        uint32_t pb = dw(a, 4);
        char cmd[260], tail[130];
        uint8_t blk[10], show[4];
        *v = 2;
        if (!str_(a, dw(a, 8), cmd, 128))
            return 1;
        if (pb == 0xffffffffu) {
            int d = win_ne_guest_load_library(g, cmd);
            if (d >= 0) {
                win_ne_dll_init(c);
                *v = win_ne_guest_dll_instance(g, d);
            }
            return 1;
        }
        UINT sw = SW_SHOWNORMAL;
        tail[0] = 0;
        if (pb && rd_(a, pb, blk, sizeof(blk))) {   /* its environment, command, show */
            uint32_t line = rd16(blk + 2) | (uint32_t)rd16(blk + 4) << 16;
            uint32_t sh = rd16(blk + 6) | (uint32_t)rd16(blk + 8) << 16;
            uint8_t len;
            if (line && rd_(a, line, &len, 1) && len < sizeof(tail) &&
                rd_(a, line + 1, tail, len))
                tail[len] = 0;
            if (sh && rd_(a, sh, show, 4))
                sw = rd16(show + 2);
        }
        if (tail[0]) {
            size_t n = strlen(cmd);
            cmd[n++] = ' ';
            lstrcpyn(cmd + n, tail[0] == ' ' ? tail + 1 : tail, (int)(sizeof(cmd) - n));
        }
        *v = WinExec(cmd, sw);
        return 1;
    }
    case 184: {                         /* GlobalDOSAlloc(DWORD): segment and selector */
        uint32_t bytes = dw(a, 4);
        uint16_t sel = a->bad ? 0 : win_ne_guest_global_alloc(g, bytes ? bytes : 1);
        win_ne_guest_segment *s = sel ? win_ne_guest_global(g, sel) : NULL;
        *dword = 1;
        if (s && !(s->linear & 15u) && s->linear < 0x100000u)
            *v = (s->linear >> 4) << 16 | sel;
        else if (sel)
            win_ne_guest_global_free(g, sel);
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
    if (kernel_misc(a, service, v, dword))
        return 1;
    switch (service) {
    case WIN_NE_SERVICE_WIN87EM + 3:    /* __WinEm87Info, __WinEm87Restore, */
    case WIN_NE_SERVICE_WIN87EM + 4:    /* __WinEm87Save(LPVOID, int): the */
    case WIN_NE_SERVICE_WIN87EM + 5:    /* coprocessor is no emulator's */
        *v = 0;
        return 1;
    }
    if (gdi_more(a, service, v, dword))
        return 1;
    switch (service) {
    /* the serial and parallel ports: none (USER's comm functions) */
    case 0x10c8:                        /* OpenComm(LPCSTR, UINT, UINT) */
        *v = (uint16_t)-10;             /* IE_HARDWARE */
        return 1;
    case 0x10cb:                        /* GetCommError(int, COMSTAT FAR *) */
        *v = 0x8000;                    /* CE_MODE: no such device */
        return 1;
    case 0x10d0: case 0x10d1:           /* SetCommEventMask: no event word, GetCommEventMask */
    case 0x10f5:                        /* EnableCommNotification: FALSE */
        *v = 0;
        *dword = service == 0x10d0;
        return 1;
    case 0x10c9: case 0x10ca: case 0x10cc: case 0x10cd: case 0x10ce: case 0x10cf:
    case 0x10d2: case 0x10d3: case 0x10d4: case 0x10d5: case 0x10d7:
        *v = (uint16_t)-1;              /* the rest of them: an error */
        return 1;
    case 0x10d6:                        /* EscapeCommFunction: a LONG */
        *v = 0xffffffffu;
        *dword = 1;
        return 1;
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
