/*
 * pe_dlls.c - the DLLs Win32s has beside KERNEL32, USER32 and GDI32, for a
 * Win32 program, as WINN has them:
 *
 *   ADVAPI32  the registry (core/winreg.c, WINNREG.INI: HKEY_CLASSES_ROOT
 *             the Win16 programs' too), GetUserName
 *   COMDLG32  COMMDLG.DLL's dialogs (the native DLL): the structures of
 *             Win32's layout converted, the strings and buffers the
 *             program's own; Find and Replace modeless, their message's
 *             lParam the program's FINDREPLACE; the hooks and templates
 *             the program's (CDHOSTN), the registered messages' structure
 *             its own
 *   SHELL32   ShellExecute, FindExecutable (WIN.INI's [Extensions]),
 *             ShellAbout, ExtractIcon; the dropped files' (none come)
 *   LZ32      LZEXPAND.DLL's functions (the native DLL)
 *   VERSION   a file's VERSIONINFO: Win16's (VER.DLL's), and Win32's
 *             (Unicode: VerQueryValueA's strings ANSI, after the block)
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
#include "wincore.h"
#include "pe_int.h"
#include "windll.h"
#include "winreg.h"
#include "commdlg.h"


/* ---------------------------------------------------------------- the table */

enum {
    /* ADVAPI32 */
    D_RegOpenKeyA, D_RegOpenKeyExA, D_RegCreateKeyA, D_RegCreateKeyExA, D_RegCloseKey,
    D_RegDeleteKeyA, D_RegDeleteValueA, D_RegEnumKeyA, D_RegEnumKeyExA, D_RegEnumValueA,
    D_RegQueryValueA, D_RegQueryValueExA, D_RegSetValueA, D_RegSetValueExA, D_RegQueryInfoKeyA,
    D_RegFlushKey, D_GetUserNameA,
    /* COMDLG32 */
    D_GetOpenFileNameA, D_GetSaveFileNameA, D_GetFileTitleA, D_CommDlgExtendedError,
    D_ChooseColorA, D_ChooseFontA, D_PrintDlgA, D_FindTextA, D_ReplaceTextA,
    /* SHELL32 */
    D_ShellExecuteA, D_FindExecutableA, D_ShellAboutA, D_ExtractIconA, D_DragAcceptFiles,
    D_DragQueryFileA, D_DragQueryPoint, D_DragFinish,
    /* LZ32 */
    D_LZOpenFileA, D_LZInit, D_LZRead, D_LZSeek, D_LZClose, D_LZCopy, D_CopyLZFile,
    D_GetExpandedNameA, D_LZStart, D_LZDone,
    /* VERSION */
    D_GetFileVersionInfoSizeA, D_GetFileVersionInfoA, D_VerQueryValueA, D_VerLanguageNameA,
    D_VerFindFileA, D_VerInstallFileA,
    D_COUNT
};

static const struct { uint8_t module; uint8_t args; const char *name; } fns[D_COUNT] = {
    { PE_ADVAPI32, 12, "RegOpenKeyA" }, { PE_ADVAPI32, 20, "RegOpenKeyExA" },
    { PE_ADVAPI32, 12, "RegCreateKeyA" }, { PE_ADVAPI32, 36, "RegCreateKeyExA" },
    { PE_ADVAPI32, 4, "RegCloseKey" }, { PE_ADVAPI32, 8, "RegDeleteKeyA" },
    { PE_ADVAPI32, 8, "RegDeleteValueA" }, { PE_ADVAPI32, 16, "RegEnumKeyA" },
    { PE_ADVAPI32, 32, "RegEnumKeyExA" }, { PE_ADVAPI32, 32, "RegEnumValueA" },
    { PE_ADVAPI32, 16, "RegQueryValueA" }, { PE_ADVAPI32, 24, "RegQueryValueExA" },
    { PE_ADVAPI32, 20, "RegSetValueA" }, { PE_ADVAPI32, 24, "RegSetValueExA" },
    { PE_ADVAPI32, 48, "RegQueryInfoKeyA" }, { PE_ADVAPI32, 4, "RegFlushKey" },
    { PE_ADVAPI32, 8, "GetUserNameA" },
    { PE_COMDLG32, 4, "GetOpenFileNameA" }, { PE_COMDLG32, 4, "GetSaveFileNameA" },
    { PE_COMDLG32, 12, "GetFileTitleA" }, { PE_COMDLG32, 0, "CommDlgExtendedError" },
    { PE_COMDLG32, 4, "ChooseColorA" }, { PE_COMDLG32, 4, "ChooseFontA" },
    { PE_COMDLG32, 4, "PrintDlgA" }, { PE_COMDLG32, 4, "FindTextA" },
    { PE_COMDLG32, 4, "ReplaceTextA" },
    { PE_SHELL32, 24, "ShellExecuteA" }, { PE_SHELL32, 12, "FindExecutableA" },
    { PE_SHELL32, 16, "ShellAboutA" }, { PE_SHELL32, 12, "ExtractIconA" },
    { PE_SHELL32, 8, "DragAcceptFiles" }, { PE_SHELL32, 16, "DragQueryFileA" },
    { PE_SHELL32, 8, "DragQueryPoint" }, { PE_SHELL32, 4, "DragFinish" },
    { PE_LZ32, 12, "LZOpenFileA" }, { PE_LZ32, 4, "LZInit" }, { PE_LZ32, 12, "LZRead" },
    { PE_LZ32, 12, "LZSeek" }, { PE_LZ32, 4, "LZClose" }, { PE_LZ32, 8, "LZCopy" },
    { PE_LZ32, 8, "CopyLZFile" }, { PE_LZ32, 8, "GetExpandedNameA" }, { PE_LZ32, 0, "LZStart" },
    { PE_LZ32, 0, "LZDone" },
    { PE_VERSION, 8, "GetFileVersionInfoSizeA" }, { PE_VERSION, 16, "GetFileVersionInfoA" },
    { PE_VERSION, 16, "VerQueryValueA" }, { PE_VERSION, 12, "VerLanguageNameA" },
    { PE_VERSION, 32, "VerFindFileA" }, { PE_VERSION, 32, "VerInstallFileA" },
};

uint32_t win_pe_dll_proc(win_pe_image *img, int module, const char *name)
{
    for (unsigned i = 0; i < D_COUNT; i++)
        if (fns[i].module == module && !strcmp(fns[i].name, name))
            return win_pe_thunk(img, (uint16_t)(WIN_PE_SERVICE_DLLS + i), fns[i].args);
    return 0;
}

const char *win_pe_dll_name(uint16_t service)
{
    unsigned i = service - WIN_PE_SERVICE_DLLS;
    return i < D_COUNT ? fns[i].name : NULL;
}

/* ---------------------------------------------------------------- helpers */

#define ARG(i) pe_arg(a, (i))

static uint8_t *pe_rw(pe_args *a, uint32_t addr, uint32_t n)
{
    return (uint8_t *)pe_buf(a, addr, n);
}

/* a DWORD of the program's (*addr), 0 if addr is none */
static uint32_t get32(pe_args *a, uint32_t addr)
{
    const uint8_t *p = addr ? pe_rw(a, addr, 4) : NULL;
    return p ? pe_rd32(p) : 0;
}

static void put32(pe_args *a, uint32_t addr, uint32_t v)
{
    uint8_t *p = addr ? pe_rw(a, addr, 4) : NULL;
    if (p)
        pe_wr32(p, v);
}

/* a string into the program's buffer of cap bytes (cut), its length */
static uint32_t put_str(pe_args *a, uint32_t addr, uint32_t cap, const char *s)
{
    if (!addr || !cap)
        return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (n >= cap)
        n = cap - 1u;
    char *d = (char *)pe_rw(a, addr, n + 1u);
    if (!d)
        return 0;
    memcpy(d, s, n);
    d[n] = 0;
    return n;
}

/* a list of strings up to an empty one (a filter): the program's own, NULL
 * if it is not all its memory */
static const char *multi_sz(pe_args *a, uint32_t addr)
{
    if (!addr)
        return NULL;
    const char *p = pe_str(a, addr);
    uint32_t at = 0;
    while (p) {
        const char *s = pe_str(a, addr + at);
        if (!s)
            return NULL;
        if (!*s)
            return p;
        at += (uint32_t)strlen(s) + 1u;
    }
    return NULL;
}

/* a native DLL's function (the task keeps the DLL until it ends) */
static const void *native(const char *dll, const char *name)
{
    int slot = win_dll_use(dll);
    const win_dll_export *e = slot >= 0 ? win_dll_export_of(slot, 0, name) : NULL;
    return e ? e->native : NULL;
}

/* ---------------------------------------------------------------- ADVAPI32 */

/* a value's data where the program asked for them (type, data, its size in
 * *size): the size told always, the bytes if they fit */
static uint32_t value_out(pe_args *a, uint32_t type, const uint8_t *d, uint32_t n,
                          uint32_t type_at, uint32_t data_at, uint32_t size_at)
{
    if (type_at)
        put32(a, type_at, type);
    if (!size_at)
        return data_at ? WINREG_INVALID_PARAMETER : WINREG_SUCCESS;
    uint32_t cap = get32(a, size_at), r = WINREG_SUCCESS;
    if (data_at) {
        uint8_t *p = cap >= n ? pe_rw(a, data_at, n) : NULL;
        if (cap < n)
            r = WINREG_MORE_DATA;
        else if (p)
            memcpy(p, d, n);
        else
            r = WINREG_INVALID_PARAMETER;
    }
    put32(a, size_at, n);
    return r;
}

static uint32_t reg_open(pe_args *a, uint32_t key, uint32_t sub, int create, uint32_t out)
{
    const char *s = sub ? pe_str(a, sub) : "";
    uint32_t h;
    if (!out || (sub && !s))
        return WINREG_INVALID_PARAMETER;
    uint32_t r = winreg_open(a->ctx->task, key, s, create, &h);
    if (r == WINREG_SUCCESS)
        put32(a, out, h);
    return r;
}

static __attribute__((noinline)) uint32_t advapi(pe_args *a, unsigned fn)
{
    HTASK task = a->ctx->task;
    const uint8_t *d;
    uint32_t n, type;
    char name[WINREG_NAME_BYTES];
    switch (fn) {
    case D_RegOpenKeyA: return reg_open(a, ARG(0), ARG(1), 0, ARG(2));
    case D_RegOpenKeyExA: return reg_open(a, ARG(0), ARG(1), 0, ARG(4));
    case D_RegCreateKeyA: return reg_open(a, ARG(0), ARG(1), 1, ARG(2));
    case D_RegCreateKeyExA: {
        const char *s = ARG(1) ? pe_str(a, ARG(1)) : "";
        uint32_t h, existed = 0;
        if (!s || !ARG(7))
            return WINREG_INVALID_PARAMETER;
        if (winreg_open(task, ARG(0), s, 0, &h) == WINREG_SUCCESS)
            existed = 1;
        else if (winreg_open(task, ARG(0), s, 1, &h) != WINREG_SUCCESS)
            return WINREG_ACCESS_DENIED;
        put32(a, ARG(7), h);
        if (ARG(8))
            put32(a, ARG(8), existed ? 2u : 1u);    /* REG_OPENED_EXISTING_KEY, _CREATED_NEW_ */
        return WINREG_SUCCESS;
    }
    case D_RegCloseKey: return winreg_close(task, ARG(0));
    case D_RegDeleteKeyA: {
        const char *s = pe_str(a, ARG(1));
        return s ? winreg_delete_key(task, ARG(0), s) : WINREG_INVALID_PARAMETER;
    }
    case D_RegDeleteValueA: {
        const char *s = ARG(1) ? pe_str(a, ARG(1)) : "";
        return s ? winreg_delete_value(task, ARG(0), s) : WINREG_INVALID_PARAMETER;
    }
    case D_RegEnumKeyA:
    case D_RegEnumKeyExA: {
        uint32_t cap = fn == D_RegEnumKeyA ? ARG(3) : get32(a, ARG(3));
        uint32_t r = winreg_enum_key(task, ARG(0), ARG(1), name, sizeof(name));
        if (r != WINREG_SUCCESS)
            return r;
        uint32_t len = (uint32_t)strlen(name);
        if (cap < len + 1u)
            return WINREG_MORE_DATA;
        put_str(a, ARG(2), cap, name);
        if (fn == D_RegEnumKeyExA) {
            put32(a, ARG(3), len);
            if (ARG(5) && ARG(6) && get32(a, ARG(6)))
                put_str(a, ARG(5), get32(a, ARG(6)), "");
            if (ARG(6))
                put32(a, ARG(6), 0);
            uint8_t *ft = ARG(7) ? pe_rw(a, ARG(7), 8) : NULL;
            if (ft)
                memset(ft, 0, 8);
        }
        return WINREG_SUCCESS;
    }
    case D_RegEnumValueA: {
        const char *shown;
        uint32_t r = winreg_enum_value(task, ARG(0), ARG(1), &shown, &type, &d, &n);
        if (r != WINREG_SUCCESS)
            return r;
        uint32_t len = (uint32_t)strlen(shown), cap = get32(a, ARG(3));
        if (cap < len + 1u)
            return WINREG_MORE_DATA;
        put_str(a, ARG(2), cap, shown);
        put32(a, ARG(3), len);
        return value_out(a, type, d, n, ARG(5), ARG(6), ARG(7));
    }
    case D_RegQueryValueA: {
        const char *s = ARG(1) ? pe_str(a, ARG(1)) : "";
        if (!s)
            return WINREG_INVALID_PARAMETER;
        uint32_t r = winreg_get(task, ARG(0), s, NULL, &type, &d, &n);
        if (r == WINREG_FILE_NOT_FOUND) {
            uint32_t h;                 /* the key there, no value: "" */
            if (winreg_open(task, ARG(0), s, 0, &h) != WINREG_SUCCESS)
                return r;
            winreg_close(task, h);
            d = (const uint8_t *)"";
            r = WINREG_SUCCESS;
        }
        if (r != WINREG_SUCCESS)
            return r;
        n = (uint32_t)strlen((const char *)d) + 1u;
        return value_out(a, WINREG_SZ, d, n, 0, ARG(2), ARG(3));
    }
    case D_RegQueryValueExA: {
        const char *s = ARG(1) ? pe_str(a, ARG(1)) : "";
        if (!s)
            return WINREG_INVALID_PARAMETER;
        uint32_t r = winreg_get(task, ARG(0), NULL, s, &type, &d, &n);
        return r == WINREG_SUCCESS ? value_out(a, type, d, n, ARG(3), ARG(4), ARG(5)) : r;
    }
    case D_RegSetValueA: {
        const char *s = ARG(1) ? pe_str(a, ARG(1)) : "";
        const char *v = pe_str(a, ARG(3));
        if (!s || !v || ARG(2) != WINREG_SZ)
            return WINREG_INVALID_PARAMETER;
        return winreg_set(task, ARG(0), s, NULL, WINREG_SZ, v, (uint32_t)strlen(v) + 1u);
    }
    case D_RegSetValueExA: {
        const char *s = ARG(1) ? pe_str(a, ARG(1)) : "";
        n = ARG(5);
        const uint8_t *v = n ? pe_rw(a, ARG(4), n) : NULL;
        if (!s || (n && !v))
            return WINREG_INVALID_PARAMETER;
        return winreg_set(task, ARG(0), NULL, s, ARG(3), v, n);
    }
    case D_RegQueryInfoKeyA: {
        uint32_t keys, klen, values, vlen, dlen;
        uint32_t r = winreg_info(task, ARG(0), &keys, &klen, &values, &vlen, &dlen);
        if (r != WINREG_SUCCESS)
            return r;
        if (ARG(1) && ARG(2) && get32(a, ARG(2)))
            put_str(a, ARG(1), get32(a, ARG(2)), "");
        put32(a, ARG(2), 0);
        put32(a, ARG(4), keys);
        put32(a, ARG(5), klen);
        put32(a, ARG(6), 0);
        put32(a, ARG(7), values);
        put32(a, ARG(8), vlen);
        put32(a, ARG(9), dlen);
        put32(a, ARG(10), 0);
        uint8_t *ft = ARG(11) ? pe_rw(a, ARG(11), 8) : NULL;
        if (ft)
            memset(ft, 0, 8);
        return WINREG_SUCCESS;
    }
    case D_RegFlushKey:
        return WINREG_SUCCESS;          /* written as it is set */
    case D_GetUserNameA: {
        char user[64];
        GetProfileString("Network", "UserName", "User", user, sizeof(user));
        uint32_t need = (uint32_t)strlen(user) + 1u, cap = get32(a, ARG(1));
        put32(a, ARG(1), need);
        if (cap < need) {
            a->ctx->last_error = 122;   /* ERROR_INSUFFICIENT_BUFFER */
            return 0;
        }
        return put_str(a, ARG(0), cap, user) || !user[0];
    }
    }
    return 0;
}

/* ---------------------------------------------------------------- COMDLG32 */

#define FIND_DIALOGS 4

/* a Find or Replace dialog's: its native FINDREPLACE, the program's */
static struct {
    HTASK task;
    win_pe_ctx *ctx;
    FINDREPLACE fr;
    uint32_t guest;
} finds[FIND_DIALOGS];

/* ---- a Win32 program's hooks and templates (COMMDLG's CDHOSTN): the
 * structure's kind and address, the hooks' procedures, the templates
 * (its module's resources, or blocks of its memory: handles) */
enum { P_GUEST, P_KIND, P_HOOK0, P_HOOK1, P_INST, P_TPL0, P_TPL1, P_HANDLES };
enum { PK_FILE, PK_COLOR, PK_FONT, PK_FIND, PK_PRINT };

/* the program's structure brought up to date from the native one */
static DWORD cd_guest(const CDHOSTN *h, LPARAM native)
{
    win_pe_ctx *ctx = (win_pe_ctx *)h->opaque;
    uint32_t at = h->data[P_GUEST];
    uint8_t *g;
    switch (h->data[P_KIND]) {
    case PK_FILE: {
        const OPENFILENAME *o = (const OPENFILENAME *)(intptr_t)native;
        if ((g = (uint8_t *)win_pe_ptr(ctx->img, at, 76)) == NULL)
            break;
        pe_wr32(g + 24, o->nFilterIndex);
        pe_wr32(g + 52, (pe_rd32(g + 52) & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | 0x80u)) |
                        o->Flags);
        pe_wr16(g + 56, (uint16_t)o->nFileOffset);
        pe_wr16(g + 58, (uint16_t)o->nFileExtension);
        break;
    }
    case PK_COLOR:
        if ((g = (uint8_t *)win_pe_ptr(ctx->img, at, 36)) != NULL)
            pe_wr32(g + 12, ((const CHOOSECOLOR *)(intptr_t)native)->rgbResult);
        break;
    case PK_FONT: {
        const CHOOSEFONT *c = (const CHOOSEFONT *)(intptr_t)native;
        if ((g = (uint8_t *)win_pe_ptr(ctx->img, at, 60)) == NULL)
            break;
        pe_wr32(g + 16, (uint32_t)c->iPointSize);
        pe_wr32(g + 20, (pe_rd32(g + 20) & (CF_ENABLEHOOK | CF_ENABLETEMPLATE |
                                            CF_ENABLETEMPLATEHANDLE)) | c->Flags);
        pe_wr32(g + 24, c->rgbColors);
        pe_wr16(g + 48, (uint16_t)c->nFontType);
        break;
    }
    case PK_FIND:
        if ((g = (uint8_t *)win_pe_ptr(ctx->img, at, 40)) != NULL)
            pe_wr32(g + 12, (pe_rd32(g + 12) & (FR_ENABLEHOOK | FR_ENABLETEMPLATE |
                                                FR_ENABLETEMPLATEHANDLE)) |
                            ((const FINDREPLACE *)(intptr_t)native)->Flags);
        break;
    case PK_PRINT: {
        const PRINTDLG *p = (const PRINTDLG *)(intptr_t)native;
        if ((g = (uint8_t *)win_pe_ptr(ctx->img, at, 66)) == NULL)
            break;
        pe_wr32(g + 20, (pe_rd32(g + 20) & 0x3f000u) | p->Flags);
        pe_wr16(g + 24, (uint16_t)p->nFromPage);
        pe_wr16(g + 26, (uint16_t)p->nToPage);
        pe_wr16(g + 32, (uint16_t)p->nCopies);
        break;
    }
    }
    return at;
}

static UINT cd_hook(const CDHOSTN *h, int which, HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    win_pe_ctx *ctx = (win_pe_ctx *)h->opaque;
    uint32_t proc = h->data[P_HOOK0 + which];
    if (msg != WM_INITDIALOG)
        return win_pe_message(ctx, proc, dlg, msg, wp, lp);
    const uint32_t args[4] = { dlg, msg, wp,
                               lp && lp == (LPARAM)(intptr_t)h->native ? cd_guest(h, lp) :
                               (uint32_t)lp };
    return win_pe_call(ctx, proc, args, 4, NULL, 0, NULL);
}

/* made while the program's call runs (opaque2 its arguments) */
static int cd_dialog(const CDHOSTN *h, int which, HWND owner, DLGPROC proc, int modeless)
{
    pe_args *a = (pe_args *)h->opaque2;
    return a ? win_pe_dialog_native(a, h->data[P_INST], h->data[P_TPL0 + which],
                                    (int)((h->data[P_HANDLES] >> which) & 1u), owner, proc,
                                    modeless)
             : -1;
}

static BOOL cd_logfont(const CDHOSTN *h, DWORD at, const LOGFONT *lf)
{
    win_pe_ctx *ctx = (win_pe_ctx *)h->opaque;
    void *p = win_pe_ptr(ctx->img, at, sizeof(LOGFONT));
    if (p)
        memcpy(p, lf, sizeof(LOGFONT));
    return p != NULL;
}

/* COMMDLG's next call's: the program's */
static void cd_host(pe_args *a, int kind, DWORD hooks, uint32_t hook0, uint32_t hook1,
                    DWORD templates, DWORD handles, uint32_t inst, uint32_t tpl0, uint32_t tpl1)
{
    void (WINAPI *fn)(const CDHOSTN *) =
        (void (WINAPI *)(const CDHOSTN *))native("COMMDLG", "CommDlgHostN");
    CDHOSTN h;
    memset(&h, 0, sizeof(h));
    h.hook = cd_hook;
    h.dialog = cd_dialog;
    h.guest = cd_guest;
    h.logfont = cd_logfont;
    h.hooks = hooks & ((hook0 ? 1u : 0) | (hook1 ? 2u : 0));    /* (none given: none) */
    h.templates = templates & ((tpl0 ? 1u : 0) | (tpl1 ? 2u : 0));
    h.opaque = a->ctx;
    h.opaque2 = a;
    h.data[P_GUEST] = ARG(0);
    h.data[P_KIND] = (DWORD)kind;
    h.data[P_HOOK0] = hook0;
    h.data[P_HOOK1] = hook1;
    h.data[P_INST] = inst;
    h.data[P_TPL0] = tpl0;
    h.data[P_TPL1] = tpl1;
    h.data[P_HANDLES] = handles;
    if (fn)
        fn(&h);
}

/* Win32's OPENFILENAMEA (76 bytes) */
static uint32_t file_dialog(pe_args *a, int save)
{
    uint8_t *g = pe_rw(a, ARG(0), 76);
    if (!g)
        return 0;
    OPENFILENAME o;
    memset(&o, 0, sizeof(o));
    o.lStructSize = sizeof(o);
    o.hwndOwner = (HWND)pe_rd32(g + 4);
    o.lpstrFilter = multi_sz(a, pe_rd32(g + 12));
    o.nMaxCustFilter = pe_rd32(g + 20);
    o.lpstrCustomFilter = o.nMaxCustFilter ? (LPSTR)pe_rw(a, pe_rd32(g + 16), o.nMaxCustFilter) :
                          NULL;
    o.nFilterIndex = pe_rd32(g + 24);
    o.nMaxFile = pe_rd32(g + 32);
    o.lpstrFile = (LPSTR)pe_rw(a, pe_rd32(g + 28), o.nMaxFile ? o.nMaxFile : 1u);
    o.nMaxFileTitle = pe_rd32(g + 40);
    o.lpstrFileTitle = o.nMaxFileTitle ? (LPSTR)pe_rw(a, pe_rd32(g + 36), o.nMaxFileTitle) :
                       NULL;
    o.lpstrInitialDir = pe_rd32(g + 44) ? pe_str(a, pe_rd32(g + 44)) : NULL;
    o.lpstrTitle = pe_rd32(g + 48) ? pe_str(a, pe_rd32(g + 48)) : NULL;
    o.Flags = pe_rd32(g + 52) & ~(DWORD)(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | 0x80u);
    o.lpstrDefExt = pe_rd32(g + 60) ? pe_str(a, pe_rd32(g + 60)) : NULL;
    o.lCustData = (LPARAM)pe_rd32(g + 64);
    if (a->bad || !o.lpstrFile)
        return 0;
    DWORD flags = pe_rd32(g + 52);
    int handle = (flags & 0x80u) != 0;          /* OFN_ENABLETEMPLATEHANDLE */
    cd_host(a, PK_FILE, (flags & OFN_ENABLEHOOK) ? 1u : 0, pe_rd32(g + 68), 0,
            (flags & (OFN_ENABLETEMPLATE | 0x80u)) ? 1u : 0, handle, pe_rd32(g + 8),
            handle ? pe_rd32(g + 8) : pe_rd32(g + 72), 0);
    BOOL (WINAPI *fn)(LPOPENFILENAME) =
        (BOOL (WINAPI *)(LPOPENFILENAME))native("COMMDLG", save ? "GetSaveFileName" :
                                                               "GetOpenFileName");
    if (!fn)
        return 0;
    BOOL ok = fn(&o);
    g = pe_rw(a, ARG(0), 76);           /* the dialog's messages ran the program meanwhile */
    if (g) {
        pe_wr32(g + 24, o.nFilterIndex);
        pe_wr32(g + 52, (pe_rd32(g + 52) & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | 0x80u)) |
                        o.Flags);
        pe_wr16(g + 56, (uint16_t)o.nFileOffset);
        pe_wr16(g + 58, (uint16_t)o.nFileExtension);
    }
    return ok;
}

/* Win32's CHOOSECOLORA (36 bytes) */
static uint32_t choose_color(pe_args *a)
{
    uint8_t *g = pe_rw(a, ARG(0), 36);
    if (!g)
        return 0;
    CHOOSECOLOR c;
    memset(&c, 0, sizeof(c));
    c.lStructSize = sizeof(c);
    c.hwndOwner = (HWND)pe_rd32(g + 4);
    c.rgbResult = pe_rd32(g + 12);
    c.lpCustColors = (COLORREF *)pe_rw(a, pe_rd32(g + 16), 64);
    c.Flags = pe_rd32(g + 20) & ~(DWORD)(CC_ENABLEHOOK | CC_ENABLETEMPLATE |
                                         CC_ENABLETEMPLATEHANDLE);
    c.lCustData = (LPARAM)pe_rd32(g + 24);
    BOOL (WINAPI *fn)(LPCHOOSECOLOR) = (BOOL (WINAPI *)(LPCHOOSECOLOR))native("COMMDLG",
                                                                              "ChooseColor");
    if (a->bad || !c.lpCustColors || !fn)
        return 0;
    DWORD flags = pe_rd32(g + 20);
    int handle = (flags & CC_ENABLETEMPLATEHANDLE) != 0;
    cd_host(a, PK_COLOR, (flags & CC_ENABLEHOOK) ? 1u : 0, pe_rd32(g + 28), 0,
            (flags & (CC_ENABLETEMPLATE | CC_ENABLETEMPLATEHANDLE)) ? 1u : 0, handle,
            pe_rd32(g + 8), handle ? pe_rd32(g + 8) : pe_rd32(g + 32), 0);
    BOOL ok = fn(&c);
    if ((g = pe_rw(a, ARG(0), 36)) != NULL)
        pe_wr32(g + 12, c.rgbResult);
    return ok;
}

/* Win32's CHOOSEFONTA (60 bytes); its LOGFONTA has WINN's layout */
static uint32_t choose_font(pe_args *a)
{
    uint8_t *g = pe_rw(a, ARG(0), 60);
    if (!g)
        return 0;
    CHOOSEFONT c;
    memset(&c, 0, sizeof(c));
    c.lStructSize = sizeof(c);
    c.hwndOwner = (HWND)pe_rd32(g + 4);
    c.hDC = (HDC)pe_rd32(g + 8);
    c.lpLogFont = (LOGFONT *)pe_rw(a, pe_rd32(g + 12), sizeof(LOGFONT));
    c.Flags = pe_rd32(g + 20) & ~(DWORD)(CF_ENABLEHOOK | CF_ENABLETEMPLATE |
                                         CF_ENABLETEMPLATEHANDLE);
    c.rgbColors = pe_rd32(g + 24);
    c.lCustData = (LPARAM)pe_rd32(g + 28);
    c.lpszStyle = (c.Flags & CF_USESTYLE) ? (LPSTR)pe_rw(a, pe_rd32(g + 44), 32) : NULL;
    c.nSizeMin = (int)pe_rd32(g + 52);
    c.nSizeMax = (int)pe_rd32(g + 56);
    BOOL (WINAPI *fn)(LPCHOOSEFONT) = (BOOL (WINAPI *)(LPCHOOSEFONT))native("COMMDLG",
                                                                            "ChooseFont");
    if (a->bad || !c.lpLogFont || !fn)
        return 0;
    c.hInstance = 0;
    DWORD flags = pe_rd32(g + 20);
    int handle = (flags & CF_ENABLETEMPLATEHANDLE) != 0;
    cd_host(a, PK_FONT, (flags & CF_ENABLEHOOK) ? 1u : 0, pe_rd32(g + 32), 0,
            (flags & (CF_ENABLETEMPLATE | CF_ENABLETEMPLATEHANDLE)) ? 1u : 0, handle,
            pe_rd32(g + 40), handle ? pe_rd32(g + 40) : pe_rd32(g + 36), 0);
    BOOL ok = fn(&c);
    if ((g = pe_rw(a, ARG(0), 60)) != NULL) {
        pe_wr32(g + 16, (uint32_t)c.iPointSize);
        pe_wr32(g + 24, c.rgbColors);
        pe_wr16(g + 48, (uint16_t)c.nFontType);
    }
    return ok;
}

/* a native global block's bytes as a block of the program's heap (its
 * HGLOBAL); at least min bytes */
static uint32_t global_to_guest(win_pe_ctx *ctx, HGLOBAL h, uint32_t min)
{
    uint32_t n = h ? (uint32_t)GlobalSize(h) : 0;
    const uint8_t *p = n ? (const uint8_t *)GlobalLock(h) : NULL;
    uint32_t g = p ? pe_heap_alloc(ctx, ctx->heap, n > min ? n : min, 1) : 0;
    if (g)
        memcpy(win_pe_ptr(ctx->img, g, n), p, n);
    if (p)
        GlobalUnlock(h);
    return g;
}

static HGLOBAL global_from_guest(pe_args *a, uint32_t h)
{
    uint32_t n = h ? pe_heap_size(a->ctx, a->ctx->heap, h) : 0;
    const uint8_t *s = n ? pe_rw(a, h, n) : NULL;
    HGLOBAL g = s ? GlobalAlloc(GMEM_MOVEABLE, n) : 0;
    void *p = g ? GlobalLock(g) : NULL;
    if (p) {
        memcpy(p, s, n);
        GlobalUnlock(g);
    }
    return g;
}

/* Win32's PRINTDLGA (66 bytes): its DEVMODE and DEVNAMES blocks the
 * program's heap's, copied to native ones and back */
static uint32_t print_dialog(pe_args *a)
{
    win_pe_ctx *ctx = a->ctx;
    uint8_t *g = pe_rw(a, ARG(0), 66);
    if (!g)
        return 0;
    PRINTDLG p;
    memset(&p, 0, sizeof(p));
    p.lStructSize = sizeof(p);
    p.hwndOwner = (HWND)pe_rd32(g + 4);
    uint32_t mode = pe_rd32(g + 8), names = pe_rd32(g + 12);
    p.hDevMode = global_from_guest(a, mode);
    p.hDevNames = global_from_guest(a, names);
    p.Flags = pe_rd32(g + 20) & ~(DWORD)(PD_ENABLEPRINTHOOK | PD_ENABLESETUPHOOK |
                                         PD_ENABLEPRINTTEMPLATE | PD_ENABLESETUPTEMPLATE |
                                         PD_ENABLEPRINTTEMPLATEHANDLE |
                                         PD_ENABLESETUPTEMPLATEHANDLE);
    p.nFromPage = pe_rd16(g + 24);
    p.nToPage = pe_rd16(g + 26);
    p.nMinPage = pe_rd16(g + 28);
    p.nMaxPage = pe_rd16(g + 30);
    p.nCopies = pe_rd16(g + 32);
    p.lCustData = (LPARAM)pe_rd32(g + 38);
    BOOL (WINAPI *fn)(LPPRINTDLG) = (BOOL (WINAPI *)(LPPRINTDLG))native("COMMDLG", "PrintDlg");
    DWORD f = pe_rd32(g + 20);
    DWORD handles = ((f & PD_ENABLEPRINTTEMPLATEHANDLE) ? 1u : 0) |
                    ((f & PD_ENABLESETUPTEMPLATEHANDLE) ? 2u : 0);
    if (fn)
        cd_host(a, PK_PRINT,
                ((f & PD_ENABLEPRINTHOOK) ? 1u : 0) | ((f & PD_ENABLESETUPHOOK) ? 2u : 0),
                pe_rd32(g + 42), pe_rd32(g + 46),
                ((f & (PD_ENABLEPRINTTEMPLATE | PD_ENABLEPRINTTEMPLATEHANDLE)) ? 1u : 0) |
                ((f & (PD_ENABLESETUPTEMPLATE | PD_ENABLESETUPTEMPLATEHANDLE)) ? 2u : 0),
                handles, pe_rd32(g + 34),
                (handles & 1u) ? pe_rd32(g + 58) : pe_rd32(g + 50),
                (handles & 2u) ? pe_rd32(g + 62) : pe_rd32(g + 54));
    BOOL ok = fn ? fn(&p) : FALSE;
    if (ok) {
        uint32_t m = global_to_guest(ctx, p.hDevMode, 124);
        uint32_t d = global_to_guest(ctx, p.hDevNames, 8);
        if (m) {
            uint8_t *dm = (uint8_t *)win_pe_ptr(ctx->img, m, 124);
            if (pe_rd16(dm + 36) < 124u)        /* dmSize: Win32's public part */
                pe_wr16(dm + 36, 124);
        }
        if ((g = pe_rw(a, ARG(0), 66)) != NULL) {
            if (mode)
                pe_heap_free(ctx, ctx->heap, mode);
            if (names)
                pe_heap_free(ctx, ctx->heap, names);
            pe_wr32(g + 8, m);
            pe_wr32(g + 12, d);
            pe_wr32(g + 16, (uint32_t)p.hDC);
            pe_wr32(g + 20, (pe_rd32(g + 20) & 0x3f000u) | p.Flags);
            pe_wr16(g + 24, (uint16_t)p.nFromPage);
            pe_wr16(g + 26, (uint16_t)p.nToPage);
            pe_wr16(g + 32, (uint16_t)p.nCopies);
        }
    }
    if (p.hDevMode)
        GlobalFree(p.hDevMode);
    if (p.hDevNames)
        GlobalFree(p.hDevNames);
    return ok;
}

/* Win32's FINDREPLACEA (40 bytes): the program's strings are the dialog's */
static uint32_t find_dialog(pe_args *a, int replace)
{
    uint8_t *g = pe_rw(a, ARG(0), 40);
    int i = 0;
    while (i < FIND_DIALOGS && finds[i].task)
        i++;
    if (!g || i == FIND_DIALOGS)
        return 0;
    FINDREPLACE *fr = &finds[i].fr;
    memset(fr, 0, sizeof(*fr));
    fr->lStructSize = sizeof(*fr);
    fr->hwndOwner = (HWND)pe_rd32(g + 4);
    fr->Flags = pe_rd32(g + 12) & ~(DWORD)(FR_ENABLEHOOK | FR_ENABLETEMPLATE |
                                           FR_ENABLETEMPLATEHANDLE);
    fr->wFindWhatLen = pe_rd16(g + 24);
    fr->wReplaceWithLen = pe_rd16(g + 26);
    fr->lpstrFindWhat = (LPSTR)pe_rw(a, pe_rd32(g + 16), fr->wFindWhatLen);
    fr->lpstrReplaceWith = replace ? (LPSTR)pe_rw(a, pe_rd32(g + 20), fr->wReplaceWithLen) :
                           NULL;
    fr->lCustData = (LPARAM)pe_rd32(g + 28);
    HWND (WINAPI *fn)(LPFINDREPLACE) =
        (HWND (WINAPI *)(LPFINDREPLACE))native("COMMDLG", replace ? "ReplaceText" : "FindText");
    if (a->bad || !fr->lpstrFindWhat || !fr->wFindWhatLen || (replace && !fr->lpstrReplaceWith) ||
        !fn)
        return 0;
    finds[i].task = a->ctx->task;
    finds[i].ctx = a->ctx;
    finds[i].guest = ARG(0);
    DWORD flags = pe_rd32(g + 12);
    int handle = (flags & FR_ENABLETEMPLATEHANDLE) != 0;
    cd_host(a, PK_FIND, (flags & FR_ENABLEHOOK) ? 1u : 0, pe_rd32(g + 32), 0,
            (flags & (FR_ENABLETEMPLATE | FR_ENABLETEMPLATEHANDLE)) ? 1u : 0, handle,
            pe_rd32(g + 8), handle ? pe_rd32(g + 8) : pe_rd32(g + 36), 0);
    HWND h = fn(fr);
    if (!h)
        finds[i].task = 0;
    return h;
}

uint32_t win_pe_dll_lparam(win_pe_ctx *ctx, uint32_t msg, uint32_t lp)
{
    static UINT find_msg;
    if (msg < 0xc000u || !lp)
        return 0;
    if (!find_msg)
        find_msg = RegisterWindowMessage(FINDMSGSTRING);
    if (msg != find_msg)                /* another dialog's (a hook's, its owner's) */
        return win_dll_ne_lparam(msg, lp);
    for (int i = 0; i < FIND_DIALOGS; i++) {
        if (!finds[i].task || finds[i].ctx != ctx || (uint32_t)(uintptr_t)&finds[i].fr != lp)
            continue;
        uint8_t *g = (uint8_t *)win_pe_ptr(ctx->img, finds[i].guest, 40);
        if (g)
            pe_wr32(g + 12, (pe_rd32(g + 12) & (FR_ENABLEHOOK | FR_ENABLETEMPLATE |
                                                FR_ENABLETEMPLATEHANDLE)) | finds[i].fr.Flags);
        uint32_t guest = finds[i].guest;
        if (finds[i].fr.Flags & FR_DIALOGTERM)
            finds[i].task = 0;
        return guest;
    }
    return 0;
}

void win_pe_dlls_end(win_pe_ctx *ctx)
{
    for (int i = 0; i < FIND_DIALOGS; i++)
        if (finds[i].ctx == ctx)
            finds[i].task = 0;
    winreg_task_end(ctx->task);
}

static __attribute__((noinline)) uint32_t comdlg(pe_args *a, unsigned fn)
{
    switch (fn) {
    case D_GetOpenFileNameA: return file_dialog(a, 0);
    case D_GetSaveFileNameA: return file_dialog(a, 1);
    case D_GetFileTitleA: {
        short (WINAPI *f)(LPCSTR, LPSTR, UINT) =
            (short (WINAPI *)(LPCSTR, LPSTR, UINT))native("COMMDLG", "GetFileTitle");
        const char *s = pe_str(a, ARG(0));
        uint32_t cap = ARG(2) & 0xffffu;
        char *out = cap ? (char *)pe_rw(a, ARG(1), cap) : NULL;
        return s && f ? (uint32_t)(int32_t)f(s, out, cap) : 0xffffffffu;
    }
    case D_CommDlgExtendedError: {
        DWORD (WINAPI *f)(void) = (DWORD (WINAPI *)(void))native("COMMDLG", "CommDlgExtendedError");
        return f ? f() : 0;
    }
    case D_ChooseColorA: return choose_color(a);
    case D_ChooseFontA: return choose_font(a);
    case D_PrintDlgA: return print_dialog(a);
    case D_FindTextA: return find_dialog(a, 0);
    case D_ReplaceTextA: return find_dialog(a, 1);
    }
    return 0;
}

/* ---------------------------------------------------------------- SHELL32 */

static __attribute__((noinline)) uint32_t shell(pe_args *a, unsigned fn)
{
    char cmd[300];
    switch (fn) {
    case D_ShellExecuteA: {
        const char *op = ARG(1) ? pe_str(a, ARG(1)) : "open";
        const char *file = pe_str(a, ARG(2));
        const char *params = ARG(3) ? pe_str(a, ARG(3)) : "";
        if (!file || !op || !params)
            return 2;                   /* file not found */
        if (op[0] && lstrcmpi(op, "open"))
            return 31;                  /* no association for the verb */
        if (!win_shell_command(file, params, cmd, sizeof(cmd)))
            return 31;
        return WinExec(cmd, (UINT)ARG(5));
    }
    case D_FindExecutableA: {
        const char *file = pe_str(a, ARG(0));
        if (!file || !win_shell_command(file, "", cmd, sizeof(cmd)))
            return 2;
        char *sp = strchr(cmd, ' ');
        if (sp)
            *sp = 0;
        return put_str(a, ARG(2), 260, cmd) ? 33 : 2;
    }
    case D_ShellAboutA: {
        const char *app = pe_str(a, ARG(1));
        const char *other = ARG(2) ? pe_str(a, ARG(2)) : "";
        char title[64], text[300], caption[80];
        if (!app || !other)
            return 0;
        lstrcpyn(title, app, sizeof(title));
        char *hash = strchr(title, '#');            /* "caption#application" */
        if (hash)
            *hash++ = 0;
        wsprintf(caption, "About %s", title);
        wsprintf(text, "%s\n\nWINN: Windows on murm386\n%s", hash ? hash : title, other);
        MessageBox((HWND)ARG(0), text, caption, MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    case D_ExtractIconA: {
        const char *file = pe_str(a, ARG(1));
        char full[160];
        if (!file)
            return 0;
        win_path(file, full, sizeof(full));
        return (uint32_t)(uintptr_t)ExtractIconExN(full, ARG(2));
    }
    case D_DragAcceptFiles:         /* nothing drops files yet */
    case D_DragFinish:
        return 0;
    case D_DragQueryFileA:
    case D_DragQueryPoint:
        return 0;
    }
    return 0;
}

/* ---------------------------------------------------------------- LZ32 */

static __attribute__((noinline)) uint32_t lz(pe_args *a, unsigned fn)
{
    static const char *const names[] = { "LZOpenFile", "LZInit", "LZRead", "LZSeek", "LZClose",
                                         "LZCopy", "LZCopy", "GetExpandedName", "LZStart",
                                         "LZDone" };
    const void *f = native("LZEXPAND", names[fn - D_LZOpenFileA]);
    if (!f)
        return 0xffffffffu;
    switch (fn) {
    case D_LZOpenFileA: {
        const char *s = pe_str(a, ARG(0));
        OFSTRUCT *of = (OFSTRUCT *)pe_rw(a, ARG(1), sizeof(OFSTRUCT));
        if (!s || !of)
            return 0xffffffffu;
        char name[144];
        lstrcpyn(name, s, sizeof(name));
        return (uint32_t)((HFILE (WINAPI *)(LPSTR, OFSTRUCT *, UINT))f)(name, of, ARG(2) & 0xffffu);
    }
    case D_LZInit: return (uint32_t)((HFILE (WINAPI *)(HFILE))f)((HFILE)ARG(0));
    case D_LZRead: {
        uint32_t n = ARG(2);
        void *p = n ? pe_rw(a, ARG(1), n) : NULL;
        if (n && !p)
            return 0xffffffffu;
        return (uint32_t)((int (WINAPI *)(HFILE, void *, int))f)((HFILE)ARG(0), p, (int)n);
    }
    case D_LZSeek:
        return (uint32_t)((LONG (WINAPI *)(HFILE, LONG, int))f)((HFILE)ARG(0), (LONG)ARG(1),
                                                                (int)ARG(2));
    case D_LZClose: ((void (WINAPI *)(HFILE))f)((HFILE)ARG(0)); return 0;
    case D_LZCopy:
    case D_CopyLZFile:
        return (uint32_t)((LONG (WINAPI *)(HFILE, HFILE))f)((HFILE)ARG(0), (HFILE)ARG(1));
    case D_GetExpandedNameA: {
        const char *s = pe_str(a, ARG(0));
        char *out = (char *)pe_rw(a, ARG(1), 1);
        char buf[160];
        if (!s || !out)
            return 0xffffffffu;
        int r = ((int (WINAPI *)(LPCSTR, LPSTR))f)(s, buf);
        put_str(a, ARG(1), 144, buf);
        return (uint32_t)r;
    }
    case D_LZStart: return (uint32_t)((int (WINAPI *)(void))f)();
    case D_LZDone: ((void (WINAPI *)(void))f)(); return 0;
    }
    return 0;
}

/* ---------------------------------------------------------------- VERSION */

/* Win32's VERSIONINFO (Unicode): nodes of a length, their value's length
 * (characters for text), a type (1 text), a key, the value and the
 * children each 4-aligned. A block of Win16's has its key at 4. */
static int is_wide(const uint8_t *b) { return b[4] != 'V' && b[6] == 'V' && b[7] == 0; }

typedef struct { uint32_t len, vlen, type, val, kids, key; } ver_node;

static int wnode_at(const uint8_t *b, uint32_t n, uint32_t off, ver_node *v)
{
    if (off + 8 > n)
        return 0;
    v->len = pe_rd16(b + off);
    v->vlen = pe_rd16(b + off + 2);
    v->type = pe_rd16(b + off + 4);
    if (v->len < 8 || off + v->len > n)
        return 0;
    v->key = off + 6;
    uint32_t k = v->key;
    while (k + 1 < off + v->len && pe_rd16(b + k))
        k += 2;
    if (k + 1 >= off + v->len)
        return 0;
    v->val = (k + 2 + 3) & ~3u;
    uint32_t bytes = v->type ? 2u * v->vlen : v->vlen;
    v->kids = (v->val + bytes + 3) & ~3u;
    return 1;
}

static int wkey_is(const uint8_t *b, uint32_t at, const char *s)
{
    for (;; at += 2, s++) {
        uint16_t c = pe_rd16(b + at);
        int x = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (c != x)
            return 0;
        if (!c)
            return 1;
    }
}

/* a path in a Win32 block: the node's value, its length (bytes; text:
 * characters), its type */
static int wver_query(const uint8_t *b, const char *path, ver_node *cur)
{
    uint32_t n = pe_rd16(b), at = 0;
    if (!wnode_at(b, n, 0, cur))
        return 0;
    while (*path) {
        while (*path == '\\')
            path++;
        if (!*path)
            break;
        char part[64];
        uint32_t k = 0;
        while (*path && *path != '\\' && k + 1 < sizeof(part))
            part[k++] = *path++;
        part[k] = 0;
        uint32_t c = cur->kids, end = at + cur->len;
        ver_node child;
        int found = 0;
        while (c < end && wnode_at(b, n, c, &child)) {
            if (wkey_is(b, child.key, part)) {
                found = 1;
                break;
            }
            c = (c + child.len + 3) & ~3u;
        }
        if (!found)
            return 0;
        at = c;
        *cur = child;
    }
    return 1;
}

/* the VERSIONINFO's size: a Win32 one's twice (VerQueryValueA's ANSI
 * strings after it) */
static uint32_t version_size(const char *file)
{
    uint8_t head[8];
    DWORD n = GetFileResourceN(file, MAKEINTRESOURCE(16), NULL, 0, head, sizeof(head));
    if (n < 8)
        return 0;
    if (pe_rd16(head) <= n)
        n = pe_rd16(head);
    return is_wide(head) ? 2u * ((n + 3u) & ~3u) : n;
}

static __attribute__((noinline)) uint32_t version(pe_args *a, unsigned fn)
{
    switch (fn) {
    case D_GetFileVersionInfoSizeA: {
        const char *s = pe_str(a, ARG(0));
        char full[160];
        put32(a, ARG(1), 0);
        if (!s)
            return 0;
        if (!win_file_native(s, full, sizeof(full)))
            return 0;
        uint32_t n = version_size(full);
        if (!n)
            a->ctx->last_error = 1813;  /* ERROR_RESOURCE_TYPE_NOT_FOUND */
        return n;
    }
    case D_GetFileVersionInfoA: {
        const char *s = pe_str(a, ARG(0));
        uint32_t cap = ARG(2);
        uint8_t *d = cap ? pe_rw(a, ARG(3), cap) : NULL;
        char full[160];
        if (!s || !d)
            return 0;
        if (!win_file_native(s, full, sizeof(full)))
            return 0;
        return GetFileResourceN(full, MAKEINTRESOURCE(16), NULL, 0, d, cap) != 0;
    }
    case D_VerQueryValueA: {
        const uint8_t *b = pe_rw(a, ARG(0), 8);
        const char *path = pe_str(a, ARG(1));
        if (!b || !path)
            return 0;
        uint32_t n = pe_rd16(b);
        b = pe_rw(a, ARG(0), n);
        if (!b)
            return 0;
        if (!is_wide(b)) {
            BOOL (WINAPI *f)(const void *, LPCSTR, void **, UINT *) =
                (BOOL (WINAPI *)(const void *, LPCSTR, void **, UINT *))native("VER",
                                                                              "VerQueryValue");
            void *out = NULL;
            UINT len = 0;
            if (!f || !f(b, path, &out, &len))
                return 0;
            put32(a, ARG(2), win_pe_addr(a->ctx->img, out));
            put32(a, ARG(3), len);
            return 1;
        }
        ver_node v;
        if (!wver_query(b, path, &v))
            return 0;
        uint32_t at = ARG(0) + v.val, len = v.vlen;
        if (v.type) {                   /* text: its ANSI copy after the block */
            uint32_t ansi = ARG(0) + ((n + 3u) & ~3u) + v.val / 2u;
            char *d = (char *)pe_rw(a, ansi, v.vlen + 1u);
            if (!d)
                return 0;
            uint32_t k = 0;
            for (; k < v.vlen; k++) {
                uint16_t c = pe_rd16(b + v.val + 2 * k);
                if (!c)
                    break;
                d[k] = c < 256 ? (char)c : '?';
            }
            d[k] = 0;
            at = ansi;
            len = k + 1u;
        }
        put32(a, ARG(2), at);
        put32(a, ARG(3), len);
        return 1;
    }
    case D_VerLanguageNameA: {
        UINT (WINAPI *f)(UINT, LPSTR, UINT) =
            (UINT (WINAPI *)(UINT, LPSTR, UINT))native("VER", "VerLanguageName");
        uint32_t cap = ARG(2);
        char *d = cap ? (char *)pe_rw(a, ARG(1), cap) : NULL;
        return f && d ? f(ARG(0), d, cap) : 0;
    }
    case D_VerFindFileA: {
        UINT (WINAPI *f)(UINT, LPCSTR, LPCSTR, LPCSTR, LPSTR, UINT *, LPSTR, UINT *) =
            (UINT (WINAPI *)(UINT, LPCSTR, LPCSTR, LPCSTR, LPSTR, UINT *, LPSTR, UINT *))
            native("VER", "VerFindFile");
        UINT *curlen = (UINT *)pe_rw(a, ARG(5), 4), *destlen = (UINT *)pe_rw(a, ARG(7), 4);
        const char *file = pe_str(a, ARG(1));
        const char *windir = ARG(2) ? pe_str(a, ARG(2)) : NULL;
        const char *appdir = ARG(3) ? pe_str(a, ARG(3)) : NULL;
        if (!f || !curlen || !destlen || !file || a->bad)
            return 0;
        char *cur = (char *)pe_rw(a, ARG(4), *curlen ? *curlen : 1u);
        char *dest = (char *)pe_rw(a, ARG(6), *destlen ? *destlen : 1u);
        return cur && dest ? f(ARG(0), file, windir, appdir, cur, curlen, dest, destlen) : 0;
    }
    case D_VerInstallFileA: {
        DWORD (WINAPI *f)(UINT, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, UINT *) =
            (DWORD (WINAPI *)(UINT, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, UINT *))
            native("VER", "VerInstallFile");
        UINT *tmplen = (UINT *)pe_rw(a, ARG(7), 4);
        const char *src = pe_str(a, ARG(1)), *dst = pe_str(a, ARG(2));
        const char *sdir = pe_str(a, ARG(3)), *ddir = pe_str(a, ARG(4));
        const char *cdir = ARG(5) ? pe_str(a, ARG(5)) : "";
        if (!f || !tmplen || !src || !dst || !sdir || !ddir || !cdir)
            return 0x00010000u;         /* VIF_CANNOTREADSRC */
        char *tmp = (char *)pe_rw(a, ARG(6), *tmplen ? *tmplen : 1u);
        return tmp ? f(ARG(0), src, dst, sdir, ddir, cdir, tmp, tmplen) : 0x00040000u;
    }
    }
    return 0;
}

/* ---------------------------------------------------------------- the dispatcher */

int win_pe_dll_api(pe_args *a, uint16_t service, uint32_t *eax)
{
    unsigned fn = service - WIN_PE_SERVICE_DLLS;
    if (fn >= D_COUNT)
        return 0;
    switch (fns[fn].module) {
    case PE_ADVAPI32: *eax = advapi(a, fn); break;
    case PE_COMDLG32: *eax = comdlg(a, fn); break;
    case PE_SHELL32: *eax = shell(a, fn); break;
    case PE_LZ32: *eax = lz(a, fn); break;
    case PE_VERSION: *eax = version(a, fn); break;
    default: return 0;
    }
    return 1;
}
#endif
