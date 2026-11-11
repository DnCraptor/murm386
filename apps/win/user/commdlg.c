/*
 * commdlg.c - the common dialogs for native applications: COMMDLG.DLL's
 * (apps/windll/commdlg), loaded for the task at its first call of one and
 * kept until its end, as Windows loads a program's DLLs. An application
 * imports them from WINN as it imports USER's functions; without the DLL
 * they fail (CommDlgExtendedError: CDERR_INITIALIZATION).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include "user.h"
#include "commdlg.h"
#include "windll.h"

typedef void (*dll_fn)(void);

/* the DLL's function of that name for this task; NULL without it */
static dll_fn dll_proc(const char *name)
{
    int s = win_dll_use("COMMDLG");
    const win_dll_export *e = s >= 0 ? win_dll_export_of(s, 0, name) : NULL;
    return e && e->native ? (dll_fn)(uintptr_t)e->native : NULL;
}

typedef BOOL (WINAPI *ofn_fn)(LPOPENFILENAME);
typedef HWND (WINAPI *fr_fn)(LPFINDREPLACE);
typedef BOOL (WINAPI *cc_fn)(LPCHOOSECOLOR);
typedef BOOL (WINAPI *cf_fn)(LPCHOOSEFONT);
typedef BOOL (WINAPI *pd_fn)(LPPRINTDLG);
typedef DWORD (WINAPI *err_fn)(void);
typedef short (WINAPI *title_fn)(LPCSTR, LPSTR, UINT);

BOOL WINAPI GetOpenFileName(LPOPENFILENAME ofn)
{
    ofn_fn f = (ofn_fn)dll_proc("GetOpenFileName");
    return f ? f(ofn) : FALSE;
}

BOOL WINAPI GetSaveFileName(LPOPENFILENAME ofn)
{
    ofn_fn f = (ofn_fn)dll_proc("GetSaveFileName");
    return f ? f(ofn) : FALSE;
}

HWND WINAPI FindText(LPFINDREPLACE fr)
{
    fr_fn f = (fr_fn)dll_proc("FindText");
    return f ? f(fr) : 0;
}

HWND WINAPI ReplaceText(LPFINDREPLACE fr)
{
    fr_fn f = (fr_fn)dll_proc("ReplaceText");
    return f ? f(fr) : 0;
}

BOOL WINAPI ChooseColor(LPCHOOSECOLOR cc)
{
    cc_fn f = (cc_fn)dll_proc("ChooseColor");
    return f ? f(cc) : FALSE;
}

BOOL WINAPI ChooseFont(LPCHOOSEFONT cf)
{
    cf_fn f = (cf_fn)dll_proc("ChooseFont");
    return f ? f(cf) : FALSE;
}

BOOL WINAPI PrintDlg(LPPRINTDLG pd)
{
    pd_fn f = (pd_fn)dll_proc("PrintDlg");
    return f ? f(pd) : FALSE;
}

DWORD WINAPI CommDlgExtendedError(void)
{
    err_fn f = (err_fn)dll_proc("CommDlgExtendedError");
    return f ? f() : CDERR_INITIALIZATION;
}

short WINAPI GetFileTitle(LPCSTR file, LPSTR title, UINT size)
{
    title_fn f = (title_fn)dll_proc("GetFileTitle");
    return f ? f(file, title, size) : -1;
}
