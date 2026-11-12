/*
 * commdlg.c - COMMDLG.DLL, WINN's: Windows 3.1's common dialogs. The file
 * dialogs (Open, Save As: the file name, the files of the directory, its
 * subdirectories ([..] goes up), the list of file types; the directory
 * shown is remembered between calls, starting in the Windows directory),
 * Find and Replace (modeless: the owner gets FINDMSGSTRING's message),
 * Color (the 48 basic colours, 16 custom ones, defined by red, green, blue
 * or hue, saturation, luminosity), Font (the faces of the system's fonts,
 * their styles and sizes, the effects, a sample), Print (WINN has no
 * printers: no default printer, as Windows without one says), GetFileTitle
 * and CommDlgExtendedError. Hooks and templates are not supported (their
 * flags are left out).
 *
 * A native DLL (ddi/windll.h): its functions for native callers (WINN's
 * user/commdlg.c loads it for them) and Win16 programs' calls of them, the
 * program's structures read before and written back after. The Find
 * dialog of a Win16 program keeps a native FINDREPLACE of its own; the
 * messages it sends bring the program's own up to date (ne_lparam).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "commdlg.h"
#include "windll.h"

#define NE (__win_dll_ne)

static DWORD last_error;

/* the data of the dialog being made, until its WM_INITDIALOG keeps it
 * (DWL_USER): each dialog has its own, though several tasks have theirs */
static void *pending;

static void *dlg_data(HWND dlg) { return (void *)(intptr_t)GetWindowLong(dlg, DWL_USER); }

static void dlg_keep(HWND dlg)
{
    SetWindowLong(dlg, DWL_USER, (LONG)(intptr_t)pending);
    pending = NULL;
}

static const char *name_part(const char *p)
{
    const char *b = p;
    for (; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    return b;
}

static DWORD WINAPI cd_error(void) { return last_error; }

static short WINAPI cd_file_title(LPCSTR file, LPSTR title, UINT size)
{
    if (!file)
        return -1;
    const char *b = name_part(file);
    int n = lstrlen(b);
    if (!n)
        return -1;
    if ((UINT)n + 1 > size)
        return (short)(n + 1);      /* the size needed */
    lstrcpy(title, b);
    return 0;
}

/* ---------------------------------------------------------------- file dialog */

#define IDC_FILE    1152
#define IDC_FILES   1120
#define IDC_PATH    1088
#define IDC_DIRS    1121
#define IDC_TYPES   1136

#define LIST_BYTES  4096

static char last_dir[128];

typedef struct {
    OPENFILENAME *ofn;
    int   save;
    char  dir[128];                 /* no trailing backslash, except "C:\" */
    char  pattern[64];              /* "*.txt;*.ini" */
} fdlg;

static int has_wild(const char *s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return 1;
    return 0;
}

static void join(char *out, int size, const char *dir, const char *name)
{
    lstrcpyn(out, dir, size);
    int n = lstrlen(out);
    if (n && out[n - 1] != '\\' && n + 1 < size) {
        out[n++] = '\\';
        out[n] = 0;
    }
    lstrcpyn(out + n, name, size - n);
}

/* a path made full, as the dialog shows and gives it: the drive, the
 * directories from the root ("." and ".." resolved, "/" as "\"), upper
 * case; a directory alone keeps no trailing backslash, except "C:\" */
static void full_path(char *path, int size)
{
    char in[160], out[200], cwd[128];
    lstrcpyn(in, path, sizeof(in));
    for (char *p = in; *p; p++)
        if (*p == '/')
            *p = '\\';
    const char *rest = in;
    int cn = GetCurrentDirectoryN(cwd, sizeof(cwd));
    if (cn < 2 || cwd[1] != ':')
        lstrcpy(cwd, "C:\\");
    if (in[0] && in[1] == ':') {
        out[0] = in[0];
        out[1] = ':';
        out[2] = 0;
        rest = in + 2;
        /* relative to its drive's current directory: the current drive's only */
        if (*rest != '\\' && (cwd[0] | 0x20) == (in[0] | 0x20))
            lstrcpyn(out, cwd, sizeof(out));
    } else {
        lstrcpyn(out, cwd, sizeof(out));
        if (in[0] == '\\')
            out[2] = 0;
    }
    int k = lstrlen(out);
    if (k > 2 && out[k - 1] == '\\')
        out[--k] = 0;
    while (*rest) {
        while (*rest == '\\')
            rest++;
        const char *e = rest;
        while (*e && *e != '\\')
            e++;
        int len = (int)(e - rest);
        if (len == 2 && rest[0] == '.' && rest[1] == '.') {
            while (k > 2 && out[k - 1] != '\\')
                k--;
            if (k > 2)
                k--;
            out[k] = 0;
        } else if (len && !(len == 1 && rest[0] == '.') && k + len + 1 < (int)sizeof(out)) {
            out[k++] = '\\';
            memcpy(out + k, rest, (size_t)len);
            k += len;
            out[k] = 0;
        }
        rest = e;
    }
    if (k <= 2) {
        out[2] = '\\';
        out[3] = 0;
    }
    AnsiUpper(out);
    lstrcpyn(path, out, size);
}

static int exists(const char *path, UINT attr)
{
    char one[64];
    return WinDirListAttrN(path, attr, one, sizeof(one)) > 0;
}

/* "a\b\..\c" style clean-up is not needed: the dialog only ever goes one
 * level down or up */
static void go_up(char *dir)
{
    int n = lstrlen(dir);
    while (n > 0 && dir[n - 1] != '\\')
        n--;
    if (n > 0 && n - 1 > 0 && dir[n - 2] != ':')
        n--;                        /* "C:\A" -> "C:\" keeps its backslash */
    dir[n] = 0;
    if (!dir[0])
        lstrcpy(dir, "\\");
}

static void fill(HWND dlg, fdlg *F)
{
    char *names = (char *)malloc(LIST_BYTES);
    char spec[200];
    HWND files = GetDlgItem(dlg, IDC_FILES), dirs = GetDlgItem(dlg, IDC_DIRS);
    SendMessage(files, LB_RESETCONTENT, 0, 0);
    SendMessage(dirs, LB_RESETCONTENT, 0, 0);
    if (!names)
        return;
    /* files, for each pattern of the type */
    const char *p = F->pattern;
    while (*p) {
        char one[32];
        int k = 0;
        while (*p == ';' || *p == ' ')
            p++;
        while (*p && *p != ';' && k < (int)sizeof(one) - 1)
            one[k++] = *p++;
        one[k] = 0;
        if (!k)
            continue;
        join(spec, sizeof(spec), F->dir, one);
        WinDirListAttrN(spec, 0, names, LIST_BYTES);
        for (const char *n = names; *n; n += lstrlen(n) + 1) {
            char low[16];
            lstrcpyn(low, n, sizeof(low));
            AnsiLower(low);
            SendMessage(files, LB_ADDSTRING, 0, (LPARAM)(intptr_t)low);
        }
    }
    /* directories: [..] first, then the subdirectories in order */
    int len = lstrlen(F->dir);
    int root = len <= 3 && len >= 2 && F->dir[1] == ':';
    if (!root && !(len == 1 && F->dir[0] == '\\'))
        SendMessage(dirs, LB_ADDSTRING, 0, (LPARAM)(intptr_t)"[..]");
    join(spec, sizeof(spec), F->dir, "*.*");
    WinDirListAttrN(spec, 0x10, names, LIST_BYTES);
    int first = (int)SendMessage(dirs, LB_GETCOUNT, 0, 0);
    for (const char *n = names; *n; n += lstrlen(n) + 1) {
        char item[20];
        wsprintf(item, "[%s]", n);
        AnsiLower(item);
        /* sorted insert after [..] */
        int count = (int)SendMessage(dirs, LB_GETCOUNT, 0, 0), at = first;
        char other[20];
        while (at < count) {
            SendMessage(dirs, LB_GETTEXT, (WPARAM)at, (LPARAM)(intptr_t)other);
            if (lstrcmpi(other, item) > 0)
                break;
            at++;
        }
        SendMessage(dirs, LB_INSERTSTRING, (WPARAM)at, (LPARAM)(intptr_t)item);
    }
    free(names);
    char shown[128];
    lstrcpyn(shown, F->dir, sizeof(shown));
    AnsiLower(shown);
    SetDlgItemText(dlg, IDC_PATH, shown);
}

static void change_dir(HWND dlg, fdlg *F, const char *item)
{
    if (!lstrcmp(item, "[..]")) {
        go_up(F->dir);
    } else {
        char name[16];
        int n = lstrlen(item);
        if (n < 3)
            return;
        lstrcpyn(name, item + 1, n - 1 < (int)sizeof(name) ? n - 1 : (int)sizeof(name));
        char d[128];
        join(d, sizeof(d), F->dir, name);
        lstrcpyn(F->dir, d, sizeof(F->dir));
    }
    full_path(F->dir, sizeof(F->dir));
    fill(dlg, F);
}

/* the name box: the pattern, all of it selected, for the next name typed */
static void show_pattern(HWND dlg, fdlg *F)
{
    SetDlgItemText(dlg, IDC_FILE, F->pattern);
    SendDlgItemMessage(dlg, IDC_FILE, EM_SETSEL, 0, MAKELPARAM(0, -1));
}

/* the dialog's answer: 1 done, 0 stay */
static int accept(HWND dlg, fdlg *F)
{
    OPENFILENAME *o = F->ofn;
    char text[128], full[160];
    /* in the directory list: Enter opens the selected directory */
    HWND dirs = GetDlgItem(dlg, IDC_DIRS);
    if (GetFocus() == dirs) {
        int i = (int)SendMessage(dirs, LB_GETCURSEL, 0, 0);
        if (i >= 0) {
            char item[20];
            SendMessage(dirs, LB_GETTEXT, (WPARAM)i, (LPARAM)(intptr_t)item);
            change_dir(dlg, F, item);
        }
        return 0;
    }
    GetDlgItemText(dlg, IDC_FILE, text, sizeof(text));
    char *t = text;
    while (*t == ' ')
        t++;
    int n = lstrlen(t);
    while (n && t[n - 1] == ' ')
        t[--n] = 0;
    if (!n)
        return 0;
    if (has_wild(t) && name_part(t) == t) {
        lstrcpyn(F->pattern, t, sizeof(F->pattern));
        fill(dlg, F);
        return 0;
    }
    if ((t[0] && t[1] == ':') || t[0] == '\\')
        lstrcpyn(full, t, sizeof(full));
    else
        join(full, sizeof(full), F->dir, t);
    full_path(full, sizeof(full));
    /* a directory: go there */
    if (!has_wild(full) && exists(full, 0x10)) {
        lstrcpyn(F->dir, full, sizeof(F->dir));
        show_pattern(dlg, F);
        fill(dlg, F);
        return 0;
    }
    /* a directory and a pattern: go there and use the pattern */
    if (has_wild(full)) {
        char *nm = (char *)name_part(full);
        lstrcpyn(F->pattern, nm, sizeof(F->pattern));
        if (nm > full) {
            nm[-1] = 0;
            if (full[0] && full[1] == ':' && !full[2])
                lstrcat(full, "\\");
            lstrcpyn(F->dir, full, sizeof(F->dir));
        }
        show_pattern(dlg, F);
        fill(dlg, F);
        return 0;
    }
    /* the default extension for a name without one */
    const char *nm = name_part(full);
    if (o->lpstrDefExt && *o->lpstrDefExt && !strchr(nm, '.') &&
        lstrlen(full) + lstrlen(o->lpstrDefExt) + 2 < (int)sizeof(full)) {
        lstrcat(full, ".");
        lstrcat(full, o->lpstrDefExt);
        AnsiUpper(full);
    }
    char msg[256];
    int there = exists(full, 0);
    if (!F->save && (o->Flags & OFN_FILEMUSTEXIST) && !there) {
        wsprintf(msg, "%s\nFile not found.\nPlease verify the correct filename was given.", full);
        MessageBox(dlg, msg, o->lpstrTitle ? o->lpstrTitle : "Open", MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    if (F->save && (o->Flags & OFN_OVERWRITEPROMPT) && there) {
        wsprintf(msg, "%s already exists.\nDo you want to replace it?", full);
        if (MessageBox(dlg, msg, o->lpstrTitle ? o->lpstrTitle : "Save As",
                       MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
            return 0;
    }
    if ((DWORD)lstrlen(full) + 1 > o->nMaxFile) {
        last_error = FNERR_BUFFERTOOSMALL;
        if (o->lpstrFile && o->nMaxFile >= 2) {
            int need = lstrlen(full) + 1;
            o->lpstrFile[0] = (char)(need & 0xFF);
            o->lpstrFile[1] = (char)(need >> 8);
        }
        EndDialog(dlg, IDCANCEL);
        return 1;
    }
    lstrcpy(o->lpstrFile, full);
    nm = name_part(o->lpstrFile);
    o->nFileOffset = (UINT)(nm - o->lpstrFile);
    const char *dot = strchr(nm, '.');
    o->nFileExtension = (UINT)(dot ? dot + 1 - o->lpstrFile : lstrlen(o->lpstrFile));
    if (o->lpstrFileTitle && o->nMaxFileTitle)
        lstrcpyn(o->lpstrFileTitle, nm, (int)o->nMaxFileTitle);
    lstrcpyn(last_dir, F->dir, sizeof(last_dir));
    EndDialog(dlg, IDOK);
    return 1;
}

static BOOL CALLBACK file_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG)
        dlg_keep(dlg);
    fdlg *F = (fdlg *)dlg_data(dlg);
    if (!F)
        return FALSE;
    OPENFILENAME *o = F->ofn;
    switch (msg) {
    case WM_INITDIALOG: {
        if (o->lpstrTitle)
            SetWindowText(dlg, o->lpstrTitle);
        HWND types = GetDlgItem(dlg, IDC_TYPES);
        int k = 0;
        for (const char *f = o->lpstrFilter; f && *f; k++) {
            SendMessage(types, CB_ADDSTRING, 0, (LPARAM)(intptr_t)f);
            f += lstrlen(f) + 1;
            if (k + 1 == (int)(o->nFilterIndex ? o->nFilterIndex : 1))
                lstrcpyn(F->pattern, f, sizeof(F->pattern));
            f += lstrlen(f) + 1;
        }
        if (!k)
            SendMessage(types, CB_ADDSTRING, 0, (LPARAM)(intptr_t)"All Files (*.*)");
        SendMessage(types, CB_SETCURSEL, (WPARAM)(o->nFilterIndex ? o->nFilterIndex - 1 : 0), 0);
        /* the name given, or the pattern */
        const char *init = o->lpstrFile && o->lpstrFile[0] ? o->lpstrFile : NULL;
        if (init && has_wild(name_part(init)))
            lstrcpyn(F->pattern, name_part(init), sizeof(F->pattern));
        SetDlgItemText(dlg, IDC_FILE, init && !has_wild(name_part(init)) ? name_part(init) : F->pattern);
        SendDlgItemMessage(dlg, IDC_FILE, EM_LIMITTEXT, 127, 0);
        SendDlgItemMessage(dlg, IDC_FILE, EM_SETSEL, 0, MAKELPARAM(0, -1));
        fill(dlg, F);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            accept(dlg, F);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_FILES: {
            HWND l = GetDlgItem(dlg, IDC_FILES);
            int i = (int)SendMessage(l, LB_GETCURSEL, 0, 0);
            if (i < 0)
                break;
            char name[20];
            SendMessage(l, LB_GETTEXT, (WPARAM)i, (LPARAM)(intptr_t)name);
            if (HIWORD(lp) == LBN_SELCHANGE)
                SetDlgItemText(dlg, IDC_FILE, name);
            else if (HIWORD(lp) == LBN_DBLCLK) {
                SetDlgItemText(dlg, IDC_FILE, name);
                accept(dlg, F);
            }
            return TRUE;
        }
        case IDC_DIRS:
            if (HIWORD(lp) == LBN_DBLCLK) {
                HWND l = GetDlgItem(dlg, IDC_DIRS);
                int i = (int)SendMessage(l, LB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    char item[20];
                    SendMessage(l, LB_GETTEXT, (WPARAM)i, (LPARAM)(intptr_t)item);
                    change_dir(dlg, F, item);
                }
            }
            return TRUE;
        case IDC_TYPES:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_TYPES, CB_GETCURSEL, 0, 0);
                const char *f = o->lpstrFilter;
                for (int k = 0; f && *f; k++) {
                    f += lstrlen(f) + 1;
                    if (k == i) {
                        lstrcpyn(F->pattern, f, sizeof(F->pattern));
                        break;
                    }
                    f += lstrlen(f) + 1;
                }
                o->nFilterIndex = (DWORD)i + 1;
                show_pattern(dlg, F);
                fill(dlg, F);
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static const DLGITEM_N file_items[] = {
    { 6, 6, 76, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "File &Name:" },
    { 6, 16, 90, 12, IDC_FILE, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 6, 32, 90, 68, IDC_FILES, LBS_SORT | LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "LISTBOX", "" },
    { 110, 6, 92, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Directories:" },
    { 110, 16, 92, 9, IDC_PATH, SS_LEFT | SS_NOPREFIX | WS_VISIBLE, "STATIC", "" },
    { 110, 32, 92, 68, IDC_DIRS, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "LISTBOX", "" },
    { 6, 104, 90, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "List Files of &Type:" },
    { 6, 114, 90, 60, IDC_TYPES, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, "COMBOBOX", "" },
    { 208, 6, 50, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 208, 24, 50, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static BOOL file_dialog(OPENFILENAME *o, int save)
{
    last_error = 0;
    if (!o || o->lStructSize < sizeof(OPENFILENAME) || !o->lpstrFile || !o->nMaxFile) {
        last_error = o ? CDERR_STRUCTSIZE : CDERR_INITIALIZATION;
        return FALSE;
    }
    fdlg *d = (fdlg *)calloc(1, sizeof(fdlg));
    if (!d) {
        last_error = CDERR_MEMALLOCFAILURE;
        return FALSE;
    }
    d->ofn = o;
    d->save = save;
    lstrcpy(d->pattern, "*.*");
    /* where to start: the initial directory, the one in the name, the last one */
    if (o->lpstrInitialDir && *o->lpstrInitialDir)
        lstrcpyn(d->dir, o->lpstrInitialDir, sizeof(d->dir));
    else if (o->lpstrFile[0] && name_part(o->lpstrFile) > o->lpstrFile) {
        int n = (int)(name_part(o->lpstrFile) - o->lpstrFile);
        lstrcpyn(d->dir, o->lpstrFile, n + 1 < (int)sizeof(d->dir) ? n + 1 : (int)sizeof(d->dir));
        if (n > 1 && d->dir[n - 1] == '\\' && d->dir[n - 2] != ':')
            d->dir[n - 1] = 0;
    } else if (last_dir[0])
        lstrcpy(d->dir, last_dir);
    else
        GetWindowsDirectory(d->dir, sizeof(d->dir));
    full_path(d->dir, sizeof(d->dir));
    static const DLGTEMPLATE_N tpl_open = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 10, 20, 20, 264, 134,
        NULL, NULL, "Open", 0, NULL
    };
    static const DLGTEMPLATE_N tpl_save = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 10, 20, 20, 264, 134,
        NULL, NULL, "Save As", 0, NULL
    };
    pending = d;
    int r = DialogBoxIndirectN(o->hInstance, save ? &tpl_save : &tpl_open, file_items,
                               o->hwndOwner, file_proc);
    pending = NULL;
    free(d);
    return r == IDOK;
}

static BOOL WINAPI cd_open(LPOPENFILENAME ofn) { return file_dialog(ofn, 0); }
static BOOL WINAPI cd_save(LPOPENFILENAME ofn) { return file_dialog(ofn, 1); }

/* ---------------------------------------------------------------- find / replace */

#define IDC_WHAT    1152
#define IDC_WITH    1153
#define IDC_WHOLE   1040
#define IDC_CASE    1041
#define IDC_UP      1056
#define IDC_DOWN    1057
#define IDC_REPLACE 1024
#define IDC_REPALL  1025

#define FINDS 4

static UINT find_msg;

typedef struct {
    HWND dlg;                       /* 0: free; -1 being made */
    FINDREPLACE *fr;
    int replace;
} fdata;

static fdata finds[FINDS];          /* modeless: one per open dialog */

static fdata *find_of(HWND dlg)
{
    for (int i = 0; i < FINDS; i++)
        if (finds[i].dlg == dlg)
            return &finds[i];
    return NULL;
}

static void find_notify(HWND dlg, fdata *f, DWORD what)
{
    FINDREPLACE *fr = f->fr;
    GetDlgItemText(dlg, IDC_WHAT, fr->lpstrFindWhat, (int)fr->wFindWhatLen);
    if (f->replace && fr->lpstrReplaceWith)
        GetDlgItemText(dlg, IDC_WITH, fr->lpstrReplaceWith, (int)fr->wReplaceWithLen);
    fr->Flags &= ~(DWORD)(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL | FR_DIALOGTERM |
                          FR_DOWN | FR_WHOLEWORD | FR_MATCHCASE);
    fr->Flags |= what;
    if (IsDlgButtonChecked(dlg, IDC_WHOLE)) fr->Flags |= FR_WHOLEWORD;
    if (IsDlgButtonChecked(dlg, IDC_CASE)) fr->Flags |= FR_MATCHCASE;
    if (f->replace || IsDlgButtonChecked(dlg, IDC_DOWN)) fr->Flags |= FR_DOWN;
    SendMessage(fr->hwndOwner, find_msg, 0, (LPARAM)(intptr_t)fr);
}

static BOOL CALLBACK find_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    fdata *f = find_of(dlg);
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (!f)
            break;
        switch (wp) {
        case IDOK:
            find_notify(dlg, f, FR_FINDNEXT);
            return TRUE;
        case IDC_REPLACE:
            find_notify(dlg, f, FR_REPLACE);
            return TRUE;
        case IDC_REPALL:
            find_notify(dlg, f, FR_REPLACEALL);
            return TRUE;
        case IDCANCEL:
            find_notify(dlg, f, FR_DIALOGTERM);
            DestroyWindow(dlg);
            return TRUE;
        case IDC_WHAT: {
            /* Find Next only with something to find */
            char t[2];
            GetDlgItemText(dlg, IDC_WHAT, t, sizeof(t));
            EnableWindow(GetDlgItem(dlg, IDOK), t[0] != 0);
            if (f->replace) {
                EnableWindow(GetDlgItem(dlg, IDC_REPLACE), t[0] != 0);
                EnableWindow(GetDlgItem(dlg, IDC_REPALL), t[0] != 0);
            }
            return TRUE;
        }
        }
        break;
    case WM_DESTROY:
        if (f)
            f->dlg = 0;
        return FALSE;
    }
    return FALSE;
}

static const DLGITEM_N find_items[] = {
    { 4, 8, 42, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Fi&nd What:" },
    { 47, 7, 128, 12, IDC_WHAT, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "EDIT", "" },
    { 4, 26, 100, 12, IDC_WHOLE, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "Match &Whole Word Only" },
    { 4, 42, 64, 12, IDC_CASE, BS_AUTOCHECKBOX | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Match &Case" },
    { 107, 26, 68, 28, -1, BS_GROUPBOX | WS_GROUP | WS_VISIBLE, "BUTTON", "Direction" },
    { 111, 38, 25, 12, IDC_UP, BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "&Up" },
    { 138, 38, 35, 12, IDC_DOWN, BS_AUTORADIOBUTTON | WS_VISIBLE, "BUTTON", "&Down" },
    { 182, 5, 50, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "&Find Next" },
    { 182, 23, 50, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static const DLGITEM_N replace_items[] = {
    { 4, 9, 48, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Fi&nd What:" },
    { 54, 7, 114, 12, IDC_WHAT, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "EDIT", "" },
    { 4, 26, 48, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Re&place With:" },
    { 54, 24, 114, 12, IDC_WITH, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "EDIT", "" },
    { 5, 46, 104, 12, IDC_WHOLE, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "Match &Whole Word Only" },
    { 5, 62, 59, 12, IDC_CASE, BS_AUTOCHECKBOX | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Match &Case" },
    { 174, 4, 50, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "&Find Next" },
    { 174, 21, 50, 14, IDC_REPLACE, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "&Replace" },
    { 174, 38, 50, 14, IDC_REPALL, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "Replace &All" },
    { 174, 55, 50, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static HWND find_dialog(FINDREPLACE *fr, int replace)
{
    last_error = 0;
    if (!fr || fr->lStructSize < sizeof(FINDREPLACE) || !fr->hwndOwner || !fr->lpstrFindWhat ||
        !fr->wFindWhatLen || (replace && (!fr->lpstrReplaceWith || !fr->wReplaceWithLen))) {
        last_error = fr && fr->lStructSize < sizeof(FINDREPLACE) ? CDERR_STRUCTSIZE :
                     fr && fr->lpstrFindWhat && !fr->wFindWhatLen ? FRERR_BUFFERLENGTHZERO :
                     CDERR_INITIALIZATION;
        return 0;
    }
    if (!find_msg)
        find_msg = RegisterWindowMessage(FINDMSGSTRING);
    fdata *f = NULL;
    for (int i = 0; i < FINDS && !f; i++)
        if (!finds[i].dlg || (finds[i].dlg != (HWND)-1 && !IsWindow(finds[i].dlg)))
            f = &finds[i];
    if (!f) {
        last_error = CDERR_MEMALLOCFAILURE;
        return 0;
    }
    f->fr = fr;
    f->replace = replace;
    static const DLGTEMPLATE_N tpl_find = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE, 9, 30, 52, 236, 62,
        NULL, NULL, "Find", 0, NULL
    };
    static const DLGTEMPLATE_N tpl_replace = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE, 10, 36, 44, 230, 94,
        NULL, NULL, "Replace", 0, NULL
    };
    f->dlg = (HWND)-1;              /* reserved while it is being made */
    HWND dlg = CreateDialogIndirectN(fr->hInstance, replace ? &tpl_replace : &tpl_find,
                                     replace ? replace_items : find_items, fr->hwndOwner, find_proc);
    f->dlg = dlg;
    if (!dlg) {
        last_error = CDERR_DIALOGFAILURE;
        return 0;
    }
    SetDlgItemText(dlg, IDC_WHAT, fr->lpstrFindWhat);
    if (replace)
        SetDlgItemText(dlg, IDC_WITH, fr->lpstrReplaceWith);
    else
        CheckRadioButton(dlg, IDC_UP, IDC_DOWN, (fr->Flags & FR_DOWN) ? IDC_DOWN : IDC_UP);
    CheckDlgButton(dlg, IDC_WHOLE, (fr->Flags & FR_WHOLEWORD) != 0);
    CheckDlgButton(dlg, IDC_CASE, (fr->Flags & FR_MATCHCASE) != 0);
    if (fr->Flags & FR_HIDEWHOLEWORD) ShowWindow(GetDlgItem(dlg, IDC_WHOLE), SW_HIDE);
    if (fr->Flags & FR_NOWHOLEWORD) EnableWindow(GetDlgItem(dlg, IDC_WHOLE), FALSE);
    if (fr->Flags & FR_HIDEMATCHCASE) ShowWindow(GetDlgItem(dlg, IDC_CASE), SW_HIDE);
    if (fr->Flags & FR_NOMATCHCASE) EnableWindow(GetDlgItem(dlg, IDC_CASE), FALSE);
    if (!replace && (fr->Flags & FR_HIDEUPDOWN)) {
        ShowWindow(GetDlgItem(dlg, IDC_UP), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_DOWN), SW_HIDE);
    }
    if (!replace && (fr->Flags & FR_NOUPDOWN)) {
        EnableWindow(GetDlgItem(dlg, IDC_UP), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_DOWN), FALSE);
    }
    EnableWindow(GetDlgItem(dlg, IDOK), fr->lpstrFindWhat[0] != 0);
    if (replace) {
        EnableWindow(GetDlgItem(dlg, IDC_REPLACE), fr->lpstrFindWhat[0] != 0);
        EnableWindow(GetDlgItem(dlg, IDC_REPALL), fr->lpstrFindWhat[0] != 0);
    }
    SendDlgItemMessage(dlg, IDC_WHAT, EM_SETSEL, 0, MAKELPARAM(0, -1));
    SetFocus(GetDlgItem(dlg, IDC_WHAT));
    return dlg;
}

static HWND WINAPI cd_find(LPFINDREPLACE fr) { return find_dialog(fr, 0); }
static HWND WINAPI cd_replace(LPFINDREPLACE fr) { return find_dialog(fr, 1); }

/* ---------------------------------------------------------------- colour */

#define IDC_DEFINE  719             /* Windows' ids */
#define IDC_ADD     712
#define IDC_HUE     703
#define IDC_SAT     704
#define IDC_LUM     705
#define IDC_RED     706
#define IDC_GREEN   707
#define IDC_BLUE    708
#define IDC_SOLID   713

#define HLSMAX 240
#define RGBMAX 255

static const COLORREF basic[48] = {
    0x8080FF, 0x80FFFF, 0x80FF80, 0x80FF00, 0xFFFF80, 0xFF8000, 0xC080FF, 0xFF80FF,
    0x0000FF, 0x00FFFF, 0x00FF80, 0x40FF00, 0xFFFF00, 0xC08000, 0xC08080, 0xFF00FF,
    0x404080, 0x4080FF, 0x00FF00, 0x808000, 0x804000, 0xFF8080, 0x400080, 0x8000FF,
    0x000080, 0x0080FF, 0x008000, 0x408000, 0xFF0000, 0xA00000, 0x800080, 0xFF0080,
    0x000040, 0x004080, 0x004000, 0x404000, 0x800000, 0x400000, 0x400040, 0x800040,
    0x000000, 0x008080, 0x408080, 0x808080, 0x808040, 0xC0C0C0, 0x400040, 0xFFFFFF,
};

typedef struct {
    CHOOSECOLOR *cc;
    COLORREF color, custom[16];
    int sel;                        /* 0..47 basic, 48..63 custom, -1 none */
    int next_custom;                /* where Add to Custom Colors puts it */
    int full, updating;
} cdlg;

/* the grids and the sample, in dialog units */
static const RECT basic_du = { 4, 14, 144, 98 };
static const RECT custom_du = { 4, 112, 144, 140 };
static const RECT sample_du = { 152, 4, 296, 64 };

static void du_rect(HWND dlg, const RECT *du, RECT *px)
{
    *px = *du;
    MapDialogRect(dlg, px);
}

/* cell i (0..63) of the grids */
static void cell_rect(HWND dlg, int i, RECT *r)
{
    RECT a;
    du_rect(dlg, i < 48 ? &basic_du : &custom_du, &a);
    int k = i < 48 ? i : i - 48, rows = i < 48 ? 6 : 2;
    int w = (a.right - a.left) / 8, h = (a.bottom - a.top) / rows;
    r->left = a.left + (k % 8) * w;
    r->top = a.top + (k / 8) * h;
    r->right = r->left + w;
    r->bottom = r->top + h;
}

static int hue_rgb(int n1, int n2, int hue)
{
    if (hue < 0)
        hue += HLSMAX;
    if (hue > HLSMAX)
        hue -= HLSMAX;
    if (hue < HLSMAX / 6)
        return n1 + (((n2 - n1) * hue + HLSMAX / 12) / (HLSMAX / 6));
    if (hue < HLSMAX / 2)
        return n2;
    if (hue < HLSMAX * 2 / 3)
        return n1 + (((n2 - n1) * (HLSMAX * 2 / 3 - hue) + HLSMAX / 12) / (HLSMAX / 6));
    return n1;
}

static COLORREF hls_rgb(int h, int l, int s)
{
    int r, g, b;
    if (!s) {
        r = g = b = l * RGBMAX / HLSMAX;
    } else {
        int m2 = l <= HLSMAX / 2 ? (l * (HLSMAX + s) + HLSMAX / 2) / HLSMAX
                                 : l + s - (l * s + HLSMAX / 2) / HLSMAX;
        int m1 = 2 * l - m2;
        r = (hue_rgb(m1, m2, h + HLSMAX / 3) * RGBMAX + HLSMAX / 2) / HLSMAX;
        g = (hue_rgb(m1, m2, h) * RGBMAX + HLSMAX / 2) / HLSMAX;
        b = (hue_rgb(m1, m2, h - HLSMAX / 3) * RGBMAX + HLSMAX / 2) / HLSMAX;
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r < 0 ? 0 : r, g < 0 ? 0 : g, b < 0 ? 0 : b);
}

static void rgb_hls(COLORREF c, int *h, int *l, int *s)
{
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *l = ((mx + mn) * HLSMAX + RGBMAX) / (2 * RGBMAX);
    if (mx == mn) {
        *s = 0;
        *h = HLSMAX * 2 / 3;
        return;
    }
    if (*l <= HLSMAX / 2)
        *s = ((mx - mn) * HLSMAX + (mx + mn) / 2) / (mx + mn);
    else
        *s = ((mx - mn) * HLSMAX + (2 * RGBMAX - mx - mn) / 2) / (2 * RGBMAX - mx - mn);
    int d = mx - mn;
    int rd = ((mx - r) * (HLSMAX / 6) + d / 2) / d;
    int gd = ((mx - g) * (HLSMAX / 6) + d / 2) / d;
    int bd = ((mx - b) * (HLSMAX / 6) + d / 2) / d;
    if (r == mx)
        *h = bd - gd;
    else if (g == mx)
        *h = HLSMAX / 3 + rd - bd;
    else
        *h = 2 * HLSMAX / 3 + gd - rd;
    if (*h < 0)
        *h += HLSMAX;
    if (*h >= HLSMAX)
        *h -= HLSMAX;
}

static void color_fields(HWND dlg, cdlg *d)
{
    int h, l, s;
    rgb_hls(d->color, &h, &l, &s);
    d->updating = 1;
    SetDlgItemInt(dlg, IDC_HUE, (UINT)h, FALSE);
    SetDlgItemInt(dlg, IDC_SAT, (UINT)s, FALSE);
    SetDlgItemInt(dlg, IDC_LUM, (UINT)l, FALSE);
    SetDlgItemInt(dlg, IDC_RED, GetRValue(d->color), FALSE);
    SetDlgItemInt(dlg, IDC_GREEN, GetGValue(d->color), FALSE);
    SetDlgItemInt(dlg, IDC_BLUE, GetBValue(d->color), FALSE);
    d->updating = 0;
}

static void color_redraw(HWND dlg)
{
    RECT r;
    du_rect(dlg, &sample_du, &r);
    InvalidateRect(dlg, &r, FALSE);
    du_rect(dlg, &basic_du, &r);
    InflateRect(&r, 3, 3);
    InvalidateRect(dlg, &r, FALSE);
    du_rect(dlg, &custom_du, &r);
    InflateRect(&r, 3, 3);
    InvalidateRect(dlg, &r, FALSE);
}

static void color_paint(HWND dlg, cdlg *d)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(dlg, &ps);
    HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    for (int i = 0; i < 64; i++) {
        RECT r, in;
        cell_rect(dlg, i, &r);
        FillRect(dc, &r, face);
        in = r;
        InflateRect(&in, -3, -2);
        if (i == d->sel) {
            RECT f = r;
            InflateRect(&f, -1, 0);
            FrameRect(dc, &f, black);
            InflateRect(&f, -1, -1);
            FrameRect(dc, &f, black);
        }
        FrameRect(dc, &in, black);
        InflateRect(&in, -1, -1);
        HBRUSH b = CreateSolidBrush(i < 48 ? basic[i] : d->custom[i - 48]);
        FillRect(dc, &in, b);
        DeleteObject(b);
    }
    if (d->full) {
        RECT s;
        du_rect(dlg, &sample_du, &s);
        FrameRect(dc, &s, black);
        InflateRect(&s, -1, -1);
        HBRUSH b = CreateSolidBrush(d->color);
        FillRect(dc, &s, b);
        DeleteObject(b);
    }
    DeleteObject(face);
    EndPaint(dlg, &ps);
}

static const int full_ids[] = { IDC_HUE, IDC_SAT, IDC_LUM, IDC_RED, IDC_GREEN, IDC_BLUE,
                                IDC_ADD, IDC_SOLID };

static void color_open_full(HWND dlg, cdlg *d)
{
    RECT w, du = { 0, 0, 300, 0 };
    d->full = 1;
    GetWindowRect(dlg, &w);
    MapDialogRect(dlg, &du);
    RECT cl;
    GetClientRect(dlg, &cl);
    SetWindowPos(dlg, 0, 0, 0, (w.right - w.left) - (cl.right - cl.left) + du.right,
                 w.bottom - w.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (unsigned i = 0; i < sizeof(full_ids) / sizeof(full_ids[0]); i++)
        EnableWindow(GetDlgItem(dlg, full_ids[i]), TRUE);
    EnableWindow(GetDlgItem(dlg, IDC_DEFINE), FALSE);
    color_fields(dlg, d);
    SetFocus(GetDlgItem(dlg, IDC_HUE));
}

static void color_pick(HWND dlg, cdlg *d, int i)
{
    d->sel = i;
    d->color = i < 48 ? basic[i] : d->custom[i - 48];
    if (i >= 48)
        d->next_custom = i - 48;
    color_fields(dlg, d);
    color_redraw(dlg);
}

static void color_edited(HWND dlg, cdlg *d, int id)
{
    if (d->updating)
        return;
    BOOL ok;
    int v = (int)GetDlgItemInt(dlg, id, &ok, FALSE);
    if (!ok)
        return;
    if (id >= IDC_RED) {
        if (v > 255)
            v = 255;
        int r = GetRValue(d->color), g = GetGValue(d->color), b = GetBValue(d->color);
        if (id == IDC_RED) r = v;
        else if (id == IDC_GREEN) g = v;
        else b = v;
        d->color = RGB(r, g, b);
    } else {
        int h, l, s;
        rgb_hls(d->color, &h, &l, &s);
        if (id == IDC_HUE) h = v > HLSMAX - 1 ? HLSMAX - 1 : v;
        else if (id == IDC_SAT) s = v > HLSMAX ? HLSMAX : v;
        else l = v > HLSMAX ? HLSMAX : v;
        d->color = hls_rgb(h, l, s);
    }
    d->sel = -1;
    /* the other fields follow; the one typed in stays as it is */
    d->updating = 1;
    int h, l, s;
    rgb_hls(d->color, &h, &l, &s);
    if (id != IDC_HUE) SetDlgItemInt(dlg, IDC_HUE, (UINT)h, FALSE);
    if (id != IDC_SAT) SetDlgItemInt(dlg, IDC_SAT, (UINT)s, FALSE);
    if (id != IDC_LUM) SetDlgItemInt(dlg, IDC_LUM, (UINT)l, FALSE);
    if (id != IDC_RED) SetDlgItemInt(dlg, IDC_RED, GetRValue(d->color), FALSE);
    if (id != IDC_GREEN) SetDlgItemInt(dlg, IDC_GREEN, GetGValue(d->color), FALSE);
    if (id != IDC_BLUE) SetDlgItemInt(dlg, IDC_BLUE, GetBValue(d->color), FALSE);
    d->updating = 0;
    color_redraw(dlg);
}

static BOOL CALLBACK color_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG)
        dlg_keep(dlg);
    cdlg *d = (cdlg *)dlg_data(dlg);
    if (!d)
        return FALSE;
    switch (msg) {
    case WM_INITDIALOG: {
        for (int i = 0; i < 48; i++)
            if (basic[i] == d->color && d->sel < 0)
                d->sel = i;
        for (int i = 0; i < 16; i++)
            if (d->custom[i] == d->color && d->sel < 0)
                d->sel = 48 + i;
        for (unsigned i = 0; i < 6; i++)
            SendDlgItemMessage(dlg, full_ids[i], EM_LIMITTEXT, 3, 0);
        if ((d->cc->Flags & CC_FULLOPEN) && !(d->cc->Flags & CC_PREVENTFULLOPEN)) {
            color_open_full(dlg, d);
            return FALSE;           /* the focus set */
        }
        /* the basic part: as wide as its left half */
        RECT w, cl, du = { 0, 0, 148, 0 };
        GetWindowRect(dlg, &w);
        GetClientRect(dlg, &cl);
        MapDialogRect(dlg, &du);
        SetWindowPos(dlg, 0, 0, 0, (w.right - w.left) - (cl.right - cl.left) + du.right,
                     w.bottom - w.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (unsigned i = 0; i < sizeof(full_ids) / sizeof(full_ids[0]); i++)
            EnableWindow(GetDlgItem(dlg, full_ids[i]), FALSE);
        if (d->cc->Flags & CC_PREVENTFULLOPEN)
            EnableWindow(GetDlgItem(dlg, IDC_DEFINE), FALSE);
        return TRUE;
    }
    case WM_PAINT:
        color_paint(dlg, d);
        return TRUE;
    case WM_LBUTTONDOWN: {
        POINT pt = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
        for (int i = 0; i < 64; i++) {
            RECT r;
            cell_rect(dlg, i, &r);
            if (PtInRect(&r, pt)) {
                color_pick(dlg, d, i);
                return TRUE;
            }
        }
        return FALSE;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
        RECT r;
        cell_rect(dlg, d->sel, &r);
        if (d->sel >= 0 && PtInRect(&r, pt))
            PostMessage(dlg, WM_COMMAND, IDOK, 0);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            memcpy(d->cc->lpCustColors, d->custom, sizeof(d->custom));
            d->cc->rgbResult = d->color;
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_DEFINE:
            color_open_full(dlg, d);
            return TRUE;
        case IDC_ADD:
            d->custom[d->next_custom] = d->color;
            d->sel = 48 + d->next_custom;
            d->next_custom = (d->next_custom + 1) % 16;
            color_redraw(dlg);
            return TRUE;
        case IDC_HUE: case IDC_SAT: case IDC_LUM: case IDC_RED: case IDC_GREEN: case IDC_BLUE:
            if (HIWORD(lp) == EN_CHANGE)
                color_edited(dlg, d, LOWORD(wp));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

#define EDIT_N (ES_LEFT | WS_BORDER | WS_TABSTOP | WS_VISIBLE)
static const DLGITEM_N color_items[] = {
    { 4, 4, 140, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Basic Colors:" },
    { 4, 102, 140, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Custom Colors:" },
    { 4, 146, 140, 14, IDC_DEFINE, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Define Custom Colors..." },
    { 4, 166, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "OK" },
    { 52, 166, 44, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
    { 152, 66, 144, 9, IDC_SOLID, SS_CENTER | WS_VISIBLE, "STATIC", "Color|Solid" },
    { 152, 84, 24, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "Hu&e:" },
    { 180, 82, 22, 12, IDC_HUE, EDIT_N | WS_GROUP, "EDIT", "" },
    { 152, 100, 24, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "&Sat:" },
    { 180, 98, 22, 12, IDC_SAT, EDIT_N, "EDIT", "" },
    { 152, 116, 24, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "&Lum:" },
    { 180, 114, 22, 12, IDC_LUM, EDIT_N, "EDIT", "" },
    { 218, 84, 40, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "&Red:" },
    { 262, 82, 22, 12, IDC_RED, EDIT_N, "EDIT", "" },
    { 218, 100, 40, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "&Green:" },
    { 262, 98, 22, 12, IDC_GREEN, EDIT_N, "EDIT", "" },
    { 218, 116, 40, 9, -1, SS_RIGHT | WS_VISIBLE, "STATIC", "Bl&ue:" },
    { 262, 114, 22, 12, IDC_BLUE, EDIT_N, "EDIT", "" },
    { 152, 166, 144, 14, IDC_ADD, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Add to Custom Colors" },
};

static BOOL WINAPI cd_color(LPCHOOSECOLOR cc)
{
    last_error = 0;
    if (!cc || cc->lStructSize != sizeof(CHOOSECOLOR)) {
        last_error = cc ? CDERR_STRUCTSIZE : CDERR_INITIALIZATION;
        return FALSE;
    }
    if (!cc->lpCustColors) {
        last_error = CDERR_INITIALIZATION;
        return FALSE;
    }
    cdlg *d = (cdlg *)calloc(1, sizeof(cdlg));
    if (!d) {
        last_error = CDERR_MEMALLOCFAILURE;
        return FALSE;
    }
    d->cc = cc;
    d->color = (cc->Flags & CC_RGBINIT) ? cc->rgbResult & 0xFFFFFFul : 0;
    memcpy(d->custom, cc->lpCustColors, sizeof(d->custom));
    d->sel = -1;
    static const DLGTEMPLATE_N tpl = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        sizeof(color_items) / sizeof(color_items[0]), 4, 12, 300, 184,
        NULL, NULL, "Color", 0, NULL
    };
    pending = d;
    int r = DialogBoxIndirectN(0, &tpl, color_items, cc->hwndOwner, color_proc);
    pending = NULL;
    free(d);
    if (r < 0)
        last_error = CDERR_DIALOGFAILURE;
    return r == IDOK;
}

/* ---------------------------------------------------------------- font */

#define IDC_FACE    1136
#define IDC_STYLE   1137
#define IDC_PTSIZE    1138
#define IDC_COLOR   1139
#define IDC_STRIKE  1040
#define IDC_UNDER   1041
#define IDC_EFFECTS 1072
#define IDC_CLABEL  1093
#define IDC_APPLY   1026
#define IDC_SAMPLE  1073

static const struct { COLORREF c; const char *name; } font_colors[16] = {
    { 0x000000, "Black" }, { 0x000080, "Maroon" }, { 0x008000, "Green" },
    { 0x008080, "Olive" }, { 0x800000, "Navy" }, { 0x800080, "Purple" },
    { 0x808000, "Teal" }, { 0x808080, "Gray" }, { 0xC0C0C0, "Silver" },
    { 0x0000FF, "Red" }, { 0x00FF00, "Lime" }, { 0x00FFFF, "Yellow" },
    { 0xFF0000, "Blue" }, { 0xFF00FF, "Fuchsia" }, { 0xFFFF00, "Aqua" },
    { 0xFFFFFF, "White" },
};

static const char *const style_names[4] = { "Regular", "Italic", "Bold", "Bold Italic" };

#define FACES 32

typedef struct {
    CHOOSEFONT *cf;
    int nfaces;
    struct { char name[LF_FACESIZE]; BYTE charset, pitch; BYTE styles, tt; } face[FACES];
    COLORREF color;
    HFONT sample;
} fntdlg;

/* EnumFontFamilies' faces: those the flags let in */
static int CALLBACK face_enum(const LOGFONT *lf, const TEXTMETRIC *tm, int type, LPARAM lp)
{
    (void)tm;
    fntdlg *d = (fntdlg *)(intptr_t)lp;
    DWORD f = d->cf->Flags;
    int tt = (type & TRUETYPE_FONTTYPE) != 0;
    if ((f & CF_FIXEDPITCHONLY) && (lf->lfPitchAndFamily & 3) != FIXED_PITCH)
        return 1;
    if ((f & CF_ANSIONLY) && lf->lfCharSet != ANSI_CHARSET)
        return 1;
    if ((f & (CF_SCALABLEONLY | CF_TTONLY)) && !tt)
        return 1;                   /* scalable: TrueType (WINN has no vector fonts) */
    for (int i = 0; i < d->nfaces; i++)
        if (!lstrcmpi(d->face[i].name, lf->lfFaceName))
            return 1;
    if (d->nfaces < FACES) {
        lstrcpyn(d->face[d->nfaces].name, lf->lfFaceName, LF_FACESIZE);
        d->face[d->nfaces].charset = lf->lfCharSet;
        d->face[d->nfaces].pitch = lf->lfPitchAndFamily;
        d->face[d->nfaces].styles = (BYTE)((lf->lfItalic ? 1 : 0) | (lf->lfWeight >= FW_BOLD ? 2 : 0));
        d->face[d->nfaces].tt = (BYTE)tt;
        d->nfaces++;
    }
    return 1;
}

typedef struct {
    HWND list;
    int min, max;
} size_ctx;

static int CALLBACK size_enum(const LOGFONT *lf, const TEXTMETRIC *tm, int type, LPARAM lp)
{
    (void)lf; (void)type;
    size_ctx *s = (size_ctx *)(intptr_t)lp;
    HDC dc = GetDC(0);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(0, dc);
    int pt = MulDiv(tm->tmHeight - tm->tmInternalLeading, 72, dpi ? dpi : 96);
    if (pt <= 0 || (s->min && pt < s->min) || (s->max && pt > s->max))
        return 1;
    char t[8];
    wsprintf(t, "%d", pt);
    if (SendMessage(s->list, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(intptr_t)t) >= 0)
        return 1;
    /* in order of size */
    int n = (int)SendMessage(s->list, CB_GETCOUNT, 0, 0), at = 0;
    for (; at < n; at++) {
        char o[8];
        SendMessage(s->list, CB_GETLBTEXT, (WPARAM)at, (LPARAM)(intptr_t)o);
        if (atoi(o) > pt)
            break;
    }
    SendMessage(s->list, CB_INSERTSTRING, (WPARAM)at, (LPARAM)(intptr_t)t);
    return 1;
}

static int cur_face(HWND dlg, fntdlg *d, char *name)
{
    GetDlgItemText(dlg, IDC_FACE, name, LF_FACESIZE);
    for (int i = 0; i < d->nfaces; i++)
        if (!lstrcmpi(d->face[i].name, name))
            return i;
    return -1;
}

static int cur_style(HWND dlg)
{
    char s[24];
    GetDlgItemText(dlg, IDC_STYLE, s, sizeof(s));
    for (int i = 0; i < 4; i++)
        if (!lstrcmpi(s, style_names[i]))
            return i;
    return 0;
}

static int cur_size(HWND dlg)
{
    char s[8];
    GetDlgItemText(dlg, IDC_PTSIZE, s, sizeof(s));
    return atoi(s);
}

/* the dialog's choice as a LOGFONT: 0 when it is not one */
static int font_of(HWND dlg, fntdlg *d, LOGFONT *lf, int *pt)
{
    char name[LF_FACESIZE];
    int f = cur_face(dlg, d, name);
    *pt = cur_size(dlg);
    if (f < 0 || *pt <= 0)
        return 0;
    int st = cur_style(dlg);
    HDC dc = GetDC(0);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(0, dc);
    memset(lf, 0, sizeof(*lf));
    lf->lfHeight = -MulDiv(*pt, dpi ? dpi : 96, 72);
    lf->lfWeight = (st & 2) ? FW_BOLD : FW_NORMAL;
    lf->lfItalic = (BYTE)(st & 1);
    if (d->cf->Flags & CF_EFFECTS) {
        lf->lfUnderline = (BYTE)(IsDlgButtonChecked(dlg, IDC_UNDER) != 0);
        lf->lfStrikeOut = (BYTE)(IsDlgButtonChecked(dlg, IDC_STRIKE) != 0);
    }
    lf->lfCharSet = d->face[f].charset;
    lf->lfOutPrecision = OUT_STRING_PRECIS;
    lf->lfClipPrecision = CLIP_STROKE_PRECIS;
    lf->lfQuality = PROOF_QUALITY;
    lf->lfPitchAndFamily = d->face[f].pitch;
    lstrcpyn(lf->lfFaceName, d->face[f].name, LF_FACESIZE);
    return 1;
}

static void font_sample(HWND dlg, fntdlg *d)
{
    LOGFONT lf;
    int pt;
    if (d->sample)
        DeleteObject(d->sample);
    d->sample = font_of(dlg, d, &lf, &pt) ? CreateFontIndirect(&lf) : 0;
    InvalidateRect(GetDlgItem(dlg, IDC_SAMPLE), NULL, TRUE);
    RECT r = { 104, 82, 254, 116 };
    MapDialogRect(dlg, &r);
    InvalidateRect(dlg, &r, TRUE);
}

static void font_face_changed(HWND dlg, fntdlg *d)
{
    char name[LF_FACESIZE];
    int f = cur_face(dlg, d, name);
    int st = cur_style(dlg);
    char size[8];
    GetDlgItemText(dlg, IDC_PTSIZE, size, sizeof(size));
    HWND styles = GetDlgItem(dlg, IDC_STYLE);
    SendMessage(styles, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 4 && f >= 0; i++)
        if (!(d->cf->Flags & CF_NOSIMULATIONS) || i == d->face[f].styles)
            SendMessage(styles, CB_ADDSTRING, 0, (LPARAM)(intptr_t)style_names[i]);
    if (SendMessage(styles, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)(intptr_t)style_names[st]) < 0)
        SendMessage(styles, CB_SETCURSEL, 0, 0);
    HWND sizes = GetDlgItem(dlg, IDC_PTSIZE);
    SendMessage(sizes, CB_RESETCONTENT, 0, 0);
    if (f >= 0) {
        size_ctx s = { sizes, 0, 0 };
        if (d->cf->Flags & CF_LIMITSIZE) {
            s.min = d->cf->nSizeMin;
            s.max = d->cf->nSizeMax;
        }
        if (d->face[f].tt) {            /* scalable: the sizes Windows offers */
            static const BYTE pts[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72 };
            for (unsigned i = 0; i < sizeof(pts); i++) {
                char t[8];
                if ((s.min && pts[i] < s.min) || (s.max && pts[i] > s.max))
                    continue;
                wsprintf(t, "%d", pts[i]);
                SendMessage(sizes, CB_ADDSTRING, 0, (LPARAM)(intptr_t)t);
            }
        } else {
            HDC dc = GetDC(0);
            EnumFontFamilies(dc, d->face[f].name, size_enum, (LPARAM)(intptr_t)&s);
            ReleaseDC(0, dc);
        }
    }
    /* the size kept when the face has it, else its first */
    if (!size[0] || SendMessage(sizes, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)(intptr_t)size) < 0)
        SendMessage(sizes, CB_SETCURSEL, 0, 0);
    font_sample(dlg, d);
}

static int font_accept(HWND dlg, fntdlg *d)
{
    CHOOSEFONT *cf = d->cf;
    LOGFONT lf;
    int pt;
    char name[LF_FACESIZE];
    if (cur_face(dlg, d, name) < 0) {
        MessageBox(dlg, "There is no font with that name.\nChoose a font from the list of fonts.",
                   "Font", MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dlg, IDC_FACE));
        return 0;
    }
    if (!font_of(dlg, d, &lf, &pt) ||
        ((cf->Flags & CF_LIMITSIZE) && (pt < cf->nSizeMin || pt > cf->nSizeMax))) {
        char msg[80];
        if (cf->Flags & CF_LIMITSIZE)
            wsprintf(msg, "Size must be between %d and %d points.", cf->nSizeMin, cf->nSizeMax);
        else
            lstrcpy(msg, "Size must be a number.");
        MessageBox(dlg, msg, "Font", MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dlg, IDC_PTSIZE));
        return 0;
    }
    *cf->lpLogFont = lf;
    cf->iPointSize = pt * 10;
    int st = cur_style(dlg), f = cur_face(dlg, d, name);
    cf->nFontType = (UINT)(SCREEN_FONTTYPE | ((st & 2) ? BOLD_FONTTYPE : REGULAR_FONTTYPE) |
                           ((st & 1) ? ITALIC_FONTTYPE : 0) |
                           (st != d->face[f].styles ? SIMULATED_FONTTYPE : 0));
    if (cf->Flags & CF_EFFECTS)
        cf->rgbColors = d->color;
    if ((cf->Flags & CF_USESTYLE) && cf->lpszStyle)
        lstrcpyn(cf->lpszStyle, style_names[st], LF_FACESIZE);
    return 1;
}

static BOOL CALLBACK font_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG)
        dlg_keep(dlg);
    fntdlg *d = (fntdlg *)dlg_data(dlg);
    if (!d)
        return FALSE;
    CHOOSEFONT *cf = d->cf;
    switch (msg) {
    case WM_INITDIALOG: {
        HWND faces = GetDlgItem(dlg, IDC_FACE);
        for (int i = 0; i < d->nfaces; i++)
            SendMessage(faces, CB_ADDSTRING, 0, (LPARAM)(intptr_t)d->face[i].name);
        const LOGFONT *lf = (cf->Flags & CF_INITTOLOGFONTSTRUCT) ? cf->lpLogFont : NULL;
        int st = 0;
        if (lf)
            st = (lf->lfItalic ? 1 : 0) | (lf->lfWeight >= FW_BOLD ? 2 : 0);
        if ((cf->Flags & CF_USESTYLE) && cf->lpszStyle)
            for (int i = 0; i < 4; i++)
                if (!lstrcmpi(cf->lpszStyle, style_names[i]))
                    st = i;
        if (!lf || SendMessage(faces, CB_SELECTSTRING, (WPARAM)-1,
                               (LPARAM)(intptr_t)lf->lfFaceName) < 0)
            SendMessage(faces, CB_SETCURSEL, 0, 0);
        SetDlgItemText(dlg, IDC_STYLE, style_names[st]);
        if (lf && lf->lfHeight) {
            HDC dc = GetDC(0);
            int dpi = GetDeviceCaps(dc, LOGPIXELSY);
            ReleaseDC(0, dc);
            int h = lf->lfHeight < 0 ? -lf->lfHeight : lf->lfHeight;
            SetDlgItemInt(dlg, IDC_PTSIZE, (UINT)MulDiv(h, 72, dpi ? dpi : 96), FALSE);
        }
        font_face_changed(dlg, d);
        SetDlgItemText(dlg, IDC_STYLE, style_names[st]);
        if (cf->Flags & CF_EFFECTS) {
            HWND c = GetDlgItem(dlg, IDC_COLOR);
            int sel = 0;
            for (int i = 0; i < 16; i++) {
                SendMessage(c, CB_ADDSTRING, 0, (LPARAM)(intptr_t)font_colors[i].name);
                if (font_colors[i].c == (cf->rgbColors & 0xFFFFFFul))
                    sel = i;
            }
            SendMessage(c, CB_SETCURSEL, (WPARAM)sel, 0);
            d->color = font_colors[sel].c;
            CheckDlgButton(dlg, IDC_UNDER, lf && lf->lfUnderline);
            CheckDlgButton(dlg, IDC_STRIKE, lf && lf->lfStrikeOut);
        } else {
            static const int fx[] = { IDC_EFFECTS, IDC_STRIKE, IDC_UNDER, IDC_CLABEL, IDC_COLOR };
            for (unsigned i = 0; i < sizeof(fx) / sizeof(fx[0]); i++)
                ShowWindow(GetDlgItem(dlg, fx[i]), SW_HIDE);
        }
        if (!(cf->Flags & CF_APPLY))
            ShowWindow(GetDlgItem(dlg, IDC_APPLY), SW_HIDE);
        if (cf->Flags & CF_NOFACESEL) SetDlgItemText(dlg, IDC_FACE, "");
        if (cf->Flags & CF_NOSTYLESEL) SetDlgItemText(dlg, IDC_STYLE, "");
        if (cf->Flags & CF_NOSIZESEL) SetDlgItemText(dlg, IDC_PTSIZE, "");
        font_sample(dlg, d);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(dlg, &ps);
        RECT r = { 104, 82, 254, 116 };
        MapDialogRect(dlg, &r);
        if (d->sample) {
            HGDIOBJ old = SelectObject(dc, d->sample);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, (cf->Flags & CF_EFFECTS) ? d->color : GetSysColor(COLOR_WINDOWTEXT));
            DrawText(dc, "AaBbYyZz", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            if (font_accept(dlg, d))
                EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_APPLY:                 /* a hook's to apply: none */
            return TRUE;
        case IDC_FACE:
            if (HIWORD(lp) == CBN_SELCHANGE || HIWORD(lp) == CBN_EDITCHANGE) {
                if (HIWORD(lp) == CBN_SELCHANGE) {
                    int i = (int)SendDlgItemMessage(dlg, IDC_FACE, CB_GETCURSEL, 0, 0);
                    char name[LF_FACESIZE];
                    if (i >= 0) {
                        SendDlgItemMessage(dlg, IDC_FACE, CB_GETLBTEXT, (WPARAM)i,
                                           (LPARAM)(intptr_t)name);
                        SetDlgItemText(dlg, IDC_FACE, name);
                    }
                }
                font_face_changed(dlg, d);
            }
            return TRUE;
        case IDC_STYLE:
        case IDC_PTSIZE:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int id = LOWORD(wp);
                int i = (int)SendDlgItemMessage(dlg, id, CB_GETCURSEL, 0, 0);
                char t[24];
                if (i >= 0) {
                    SendDlgItemMessage(dlg, id, CB_GETLBTEXT, (WPARAM)i, (LPARAM)(intptr_t)t);
                    SetDlgItemText(dlg, id, t);
                }
                font_sample(dlg, d);
            } else if (HIWORD(lp) == CBN_EDITCHANGE) {
                font_sample(dlg, d);
            }
            return TRUE;
        case IDC_COLOR:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_COLOR, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < 16)
                    d->color = font_colors[i].c;
                font_sample(dlg, d);
            }
            return TRUE;
        case IDC_UNDER:
        case IDC_STRIKE:
            font_sample(dlg, d);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (d->sample)
            DeleteObject(d->sample);
        d->sample = 0;
        return FALSE;
    }
    return FALSE;
}

#define CB_LIST (CBS_SIMPLE | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE)
static const DLGITEM_N font_items[] = {
    { 6, 3, 40, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Font:" },
    { 6, 13, 94, 54, IDC_FACE, CB_LIST | CBS_SORT | WS_GROUP, "COMBOBOX", "" },
    { 108, 3, 44, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "Font St&yle:" },
    { 108, 13, 64, 54, IDC_STYLE, CB_LIST | WS_GROUP, "COMBOBOX", "" },
    { 179, 3, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Size:" },
    { 179, 13, 32, 54, IDC_PTSIZE, CB_LIST | WS_GROUP, "COMBOBOX", "" },
    { 218, 6, 40, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP | WS_VISIBLE, "BUTTON", "OK" },
    { 218, 23, 40, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
    { 218, 40, 40, 14, IDC_APPLY, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Apply" },
    { 6, 72, 84, 64, IDC_EFFECTS, BS_GROUPBOX | WS_GROUP | WS_VISIBLE, "BUTTON", "Effects" },
    { 10, 82, 49, 10, IDC_STRIKE, BS_AUTOCHECKBOX | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Stri&keout" },
    { 10, 94, 51, 10, IDC_UNDER, BS_AUTOCHECKBOX | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Underline" },
    { 10, 108, 30, 9, IDC_CLABEL, SS_LEFT | WS_VISIBLE, "STATIC", "&Color:" },
    { 10, 118, 74, 100, IDC_COLOR, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, "COMBOBOX", "" },
    { 98, 72, 160, 49, IDC_SAMPLE, BS_GROUPBOX | WS_GROUP | WS_VISIBLE, "BUTTON", "Sample" },
};

static BOOL WINAPI cd_font(LPCHOOSEFONT cf)
{
    last_error = 0;
    if (!cf || cf->lStructSize != sizeof(CHOOSEFONT)) {
        last_error = cf ? CDERR_STRUCTSIZE : CDERR_INITIALIZATION;
        return FALSE;
    }
    if (!cf->lpLogFont) {
        last_error = CDERR_INITIALIZATION;
        return FALSE;
    }
    if ((cf->Flags & CF_LIMITSIZE) && cf->nSizeMax < cf->nSizeMin) {
        last_error = CFERR_MAXLESSTHANMIN;
        return FALSE;
    }
    fntdlg *d = (fntdlg *)calloc(1, sizeof(fntdlg));
    if (!d) {
        last_error = CDERR_MEMALLOCFAILURE;
        return FALSE;
    }
    d->cf = cf;
    /* the screen's fonts; the printer's the same (WINN has no printer: its
     * DC, Write's, is the display's) */
    if (cf->Flags & (CF_SCREENFONTS | CF_PRINTERFONTS)) {
        HDC dc = GetDC(0);
        EnumFontFamilies(dc, NULL, face_enum, (LPARAM)(intptr_t)d);
        ReleaseDC(0, dc);
    }
    if (!d->nfaces) {
        free(d);
        MessageBox(cf->hwndOwner, "There are no fonts installed.\n"
                   "Open the Fonts application in the Control Panel to install fonts.",
                   "Font", MB_OK | MB_ICONEXCLAMATION);
        last_error = CFERR_NOFONTS;
        return FALSE;
    }
    static const DLGTEMPLATE_N tpl = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        sizeof(font_items) / sizeof(font_items[0]), 13, 54, 264, 140,
        NULL, NULL, "Font", 0, NULL
    };
    pending = d;
    int r = DialogBoxIndirectN(0, &tpl, font_items, cf->hwndOwner, font_proc);
    pending = NULL;
    free(d);
    if (r < 0)
        last_error = CDERR_DIALOGFAILURE;
    return r == IDOK;
}

/* ---------------------------------------------------------------- print */

static BOOL WINAPI cd_print(LPPRINTDLG pd)
{
    last_error = 0;
    if (!pd || pd->lStructSize != sizeof(PRINTDLG)) {
        last_error = pd ? CDERR_STRUCTSIZE : CDERR_INITIALIZATION;
        return FALSE;
    }
    if (!(pd->Flags & (PD_NOWARNING | PD_RETURNDEFAULT)))
        MessageBox(pd->hwndOwner, "No default printer.\n"
                   "Use the Printers option in Control Panel to install and select "
                   "a default printer.", (pd->Flags & PD_PRINTSETUP) ? "Print Setup" : "Print",
                   MB_OK | MB_ICONEXCLAMATION);
    last_error = PDERR_NODEFAULTPRN;
    return FALSE;
}

/* ================================================================ Win16 programs */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* a string of the program's (0:0 an empty one) */
static int g_str(win_dll_call *c, uint32_t far, char *dst, uint32_t cap)
{
    if (!far) {
        dst[0] = 0;
        return 1;
    }
    return NE->string(c, far, dst, cap);
}

/* a string to the program, cut to cap bytes with its zero */
static int g_out(win_dll_call *c, uint32_t far, const char *s, uint32_t cap)
{
    if (!far || !cap)
        return 0;
    uint32_t n = (uint32_t)lstrlen(s) + 1;
    if (n > cap)
        n = cap;
    if (!NE->write(c, far, s, n - 1))
        return 0;
    return NE->write(c, far + n - 1, "", 1);
}

/* Win16 OPENFILENAME: lStructSize, hwndOwner, hInstance, lpstrFilter,
 * lpstrCustomFilter, nMaxCustFilter, nFilterIndex, lpstrFile, nMaxFile,
 * lpstrFileTitle, nMaxFileTitle, lpstrInitialDir, lpstrTitle, Flags,
 * nFileOffset, nFileExtension, lpstrDefExt, lCustData, lpfnHook,
 * lpTemplateName: 72 bytes (as much as WINN reads: 60) */
static uint16_t ne_file(win_dll_call *c, int save)
{
    uint32_t far = NE->arg32(c, 4);
    uint8_t g[60];
    char filter[512], file[260], title_buf[64], dir[128], caption[64], defext[8];
    if (!NE->read(c, far, g, sizeof(g)))
        return 0;
    /* the filter: pairs of strings up to an empty one */
    uint32_t f = rd32(g + 8), n = 0;
    memset(filter, 0, sizeof(filter));
    if (f) {
        while (n + 2 < sizeof(filter)) {
            if (!NE->read(c, f + n, filter + n, 1))
                return 0;
            if (!filter[n] && n && !filter[n - 1])
                break;
            n++;
        }
    }
    uint32_t file_far = rd32(g + 24), max_file = rd32(g + 28);
    uint32_t title_far = rd32(g + 32), max_title = rd32(g + 36);
    if (!file_far || !g_str(c, file_far, file, sizeof(file)) ||
        !g_str(c, rd32(g + 40), dir, sizeof(dir)) || !g_str(c, rd32(g + 44), caption, sizeof(caption)) ||
        !g_str(c, rd32(g + 56), defext, sizeof(defext)))
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
    BOOL ok = save ? cd_save(&ofn) : cd_open(&ofn);
    wr32(g + 20, ofn.nFilterIndex);
    wr32(g + 48, ofn.Flags);
    wr16(g + 52, (uint16_t)ofn.nFileOffset);
    wr16(g + 54, (uint16_t)ofn.nFileExtension);
    if (!NE->write(c, far, g, sizeof(g)))
        return 0;
    if (ok) {
        g_out(c, file_far, file, max_file);
        if (title_far)
            g_out(c, title_far, title_buf, max_title);
    } else if (last_error == FNERR_BUFFERTOOSMALL && max_file >= 2) {
        NE->write(c, file_far, file, 2);    /* the size it needs */
    }
    return (uint16_t)ok;
}

/* Find and Replace: the program's FINDREPLACE (lStructSize, hwndOwner,
 * hInstance, Flags, lpstrFindWhat, lpstrReplaceWith, wFindWhatLen,
 * wReplaceWithLen, lCustData, lpfnHook, lpTemplateName: 36 bytes) and the
 * native one the dialog keeps, its strings in buffers of its own; each
 * message the dialog sends brings the program's up to date */
typedef struct {
    uint16_t task;                  /* 0: free */
    uint32_t guest;
    FINDREPLACE fr;
    char what[80], with[80];
    uint16_t what_len, with_len;
} ne_find;

static ne_find ne_finds[FINDS];

static uint16_t ne_find_dialog(win_dll_call *c, int replace)
{
    uint32_t far = NE->arg32(c, 4);
    uint8_t g[36];
    ne_find *nf = NULL;
    for (int i = 0; i < FINDS && !nf; i++)
        if (!ne_finds[i].task)
            nf = &ne_finds[i];
    if (!nf || !NE->read(c, far, g, sizeof(g)))
        return 0;
    memset(nf, 0, sizeof(*nf));
    nf->what_len = rd16(g + 20) < sizeof(nf->what) ? rd16(g + 20) : sizeof(nf->what);
    nf->with_len = rd16(g + 22) < sizeof(nf->with) ? rd16(g + 22) : sizeof(nf->with);
    nf->guest = far;
    if (!g_str(c, rd32(g + 12), nf->what, sizeof(nf->what)) ||
        (replace && !g_str(c, rd32(g + 16), nf->with, sizeof(nf->with))))
        return 0;
    FINDREPLACE *fr = &nf->fr;
    fr->lStructSize = sizeof(*fr);
    fr->hwndOwner = rd16(g + 4);
    fr->Flags = rd32(g + 8) & ~(DWORD)(FR_ENABLEHOOK | FR_ENABLETEMPLATE |
                                       FR_ENABLETEMPLATEHANDLE);
    fr->lpstrFindWhat = nf->what;
    fr->lpstrReplaceWith = replace ? nf->with : NULL;
    fr->wFindWhatLen = nf->what_len;
    fr->wReplaceWithLen = nf->with_len;
    nf->task = (uint16_t)GetCurrentTask();
    HWND h = find_dialog(fr, replace);
    if (!h)
        nf->task = 0;
    return (uint16_t)h;
}

/* the Find dialog's message to a Win16 window: the program's FINDREPLACE */
static uint32_t ne_lparam(uint32_t msg, uint32_t lparam)
{
    if (!find_msg || msg != find_msg)
        return 0;
    for (int i = 0; i < FINDS; i++) {
        ne_find *nf = &ne_finds[i];
        if (!nf->task || (uint32_t)(uintptr_t)&nf->fr != lparam)
            continue;
        uint8_t g[36];
        if (NE->task_read(nf->task, nf->guest, g, sizeof(g))) {
            wr32(g + 8, nf->fr.Flags);
            NE->task_write(nf->task, nf->guest, g, sizeof(g));
            uint32_t at = rd32(g + 12);
            uint32_t n = (uint32_t)lstrlen(nf->what) + 1;
            if (at && nf->what_len)
                NE->task_write(nf->task, at, nf->what, n < nf->what_len ? n : nf->what_len);
            at = rd32(g + 16);
            n = (uint32_t)lstrlen(nf->with) + 1;
            if (at && nf->fr.lpstrReplaceWith && nf->with_len)
                NE->task_write(nf->task, at, nf->with, n < nf->with_len ? n : nf->with_len);
        }
        if (nf->fr.Flags & FR_DIALOGTERM)      /* the dialog's last: its end */
            nf->task = 0;
        return nf->guest;
    }
    return 0;
}

/* Win16 CHOOSECOLOR: lStructSize, hwndOwner, hInstance, rgbResult,
 * lpCustColors, Flags, lCustData, lpfnHook, lpTemplateName: 32 bytes */
static uint16_t ne_color(win_dll_call *c)
{
    uint32_t far = NE->arg32(c, 4);
    uint8_t g[32], cust[64];
    if (!NE->read(c, far, g, sizeof(g)))
        return 0;
    if (rd32(g) != 32) {
        last_error = CDERR_STRUCTSIZE;
        return 0;
    }
    uint32_t cust_far = rd32(g + 12);
    if (!cust_far || !NE->read(c, cust_far, cust, sizeof(cust))) {
        last_error = CDERR_INITIALIZATION;
        return 0;
    }
    COLORREF custom[16];
    for (int i = 0; i < 16; i++)
        custom[i] = rd32(cust + 4 * i);
    CHOOSECOLOR cc;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = rd16(g + 4);
    cc.rgbResult = rd32(g + 8);
    cc.lpCustColors = custom;
    cc.Flags = rd32(g + 16) & ~(DWORD)(CC_ENABLEHOOK | CC_ENABLETEMPLATE | CC_ENABLETEMPLATEHANDLE);
    BOOL ok = cd_color(&cc);
    if (ok) {
        wr32(g + 8, cc.rgbResult);
        for (int i = 0; i < 16; i++)
            wr32(cust + 4 * i, custom[i]);
        NE->write(c, far, g, sizeof(g));
        NE->write(c, cust_far, cust, sizeof(cust));
    }
    return (uint16_t)ok;
}

/* Win16 LOGFONT: 5 ints, 8 bytes, the face's 32: 50 bytes */
static void logfont_in(const uint8_t *g, LOGFONT *lf)
{
    memset(lf, 0, sizeof(*lf));
    lf->lfHeight = (int16_t)rd16(g);
    lf->lfWidth = (int16_t)rd16(g + 2);
    lf->lfEscapement = (int16_t)rd16(g + 4);
    lf->lfOrientation = (int16_t)rd16(g + 6);
    lf->lfWeight = (int16_t)rd16(g + 8);
    lf->lfItalic = g[10];
    lf->lfUnderline = g[11];
    lf->lfStrikeOut = g[12];
    lf->lfCharSet = g[13];
    lf->lfOutPrecision = g[14];
    lf->lfClipPrecision = g[15];
    lf->lfQuality = g[16];
    lf->lfPitchAndFamily = g[17];
    memcpy(lf->lfFaceName, g + 18, LF_FACESIZE);
    lf->lfFaceName[LF_FACESIZE - 1] = 0;
}

static void logfont_out(const LOGFONT *lf, uint8_t *g)
{
    wr16(g, (uint16_t)lf->lfHeight);
    wr16(g + 2, (uint16_t)lf->lfWidth);
    wr16(g + 4, (uint16_t)lf->lfEscapement);
    wr16(g + 6, (uint16_t)lf->lfOrientation);
    wr16(g + 8, (uint16_t)lf->lfWeight);
    g[10] = lf->lfItalic;
    g[11] = lf->lfUnderline;
    g[12] = lf->lfStrikeOut;
    g[13] = lf->lfCharSet;
    g[14] = lf->lfOutPrecision;
    g[15] = lf->lfClipPrecision;
    g[16] = lf->lfQuality;
    g[17] = lf->lfPitchAndFamily;
    memset(g + 18, 0, LF_FACESIZE);
    lstrcpyn((char *)g + 18, lf->lfFaceName, LF_FACESIZE);
}

/* Win16 CHOOSEFONT: lStructSize, hwndOwner, hDC, lpLogFont, iPointSize,
 * Flags, rgbColors, lCustData, lpfnHook, lpTemplateName, hInstance,
 * lpszStyle, nFontType, nSizeMin, nSizeMax: 46 bytes */
static uint16_t ne_font(win_dll_call *c)
{
    uint32_t far = NE->arg32(c, 4);
    uint8_t g[46], lg[50];
    char style[LF_FACESIZE];
    if (!NE->read(c, far, g, sizeof(g)))
        return 0;
    if (rd32(g) != 46) {
        last_error = CDERR_STRUCTSIZE;
        return 0;
    }
    uint32_t lf_far = rd32(g + 8), style_far = rd32(g + 36);
    DWORD flags = rd32(g + 14) & ~(DWORD)(CF_ENABLEHOOK | CF_ENABLETEMPLATE |
                                          CF_ENABLETEMPLATEHANDLE);
    if (!lf_far || !NE->read(c, lf_far, lg, sizeof(lg))) {
        last_error = CDERR_INITIALIZATION;
        return 0;
    }
    style[0] = 0;
    if ((flags & CF_USESTYLE) && style_far && !g_str(c, style_far, style, sizeof(style)))
        return 0;
    LOGFONT lf;
    logfont_in(lg, &lf);
    CHOOSEFONT cf;
    memset(&cf, 0, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = rd16(g + 4);
    cf.lpLogFont = &lf;
    cf.Flags = flags;
    cf.rgbColors = rd32(g + 18);
    cf.lpszStyle = style_far ? style : NULL;
    cf.nSizeMin = (int16_t)rd16(g + 42);
    cf.nSizeMax = (int16_t)rd16(g + 44);
    BOOL ok = cd_font(&cf);
    if (ok) {
        logfont_out(&lf, lg);
        NE->write(c, lf_far, lg, sizeof(lg));
        wr16(g + 12, (uint16_t)cf.iPointSize);
        wr32(g + 18, cf.rgbColors);
        wr16(g + 40, (uint16_t)cf.nFontType);
        NE->write(c, far, g, sizeof(g));
        if ((flags & CF_USESTYLE) && style_far)
            g_out(c, style_far, style, LF_FACESIZE);
    }
    return (uint16_t)ok;
}

/* Win16 PRINTDLG: 52 bytes; no printer, nothing written back */
static uint16_t ne_print(win_dll_call *c)
{
    uint8_t g[16];
    if (!NE->read(c, NE->arg32(c, 4), g, sizeof(g)))
        return 0;
    if (rd32(g) != 52) {
        last_error = CDERR_STRUCTSIZE;
        return 0;
    }
    PRINTDLG pd;
    memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = rd16(g + 4);
    pd.Flags = rd32(g + 12);
    return (uint16_t)cd_print(&pd);
}

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    *v = 0;
    switch (ordinal) {
    case 1: case 2:                     /* GetOpenFileName, GetSaveFileName(LPOPENFILENAME) */
        last_error = 0;
        *v = ne_file(c, ordinal == 2);
        return WINDLL_WORD;
    case 5:                             /* ChooseColor(LPCHOOSECOLOR) */
        last_error = 0;
        *v = ne_color(c);
        return WINDLL_WORD;
    case 11: case 12:                   /* FindText, ReplaceText(LPFINDREPLACE) */
        last_error = 0;
        *v = ne_find_dialog(c, ordinal == 12);
        return WINDLL_WORD;
    case 15:                            /* ChooseFont(LPCHOOSEFONT) */
        last_error = 0;
        *v = ne_font(c);
        return WINDLL_WORD;
    case 20:                            /* PrintDlg(LPPRINTDLG) */
        last_error = 0;
        *v = ne_print(c);
        return WINDLL_WORD;
    case 26:                            /* CommDlgExtendedError() */
        *v = last_error;
        return WINDLL_DWORD;
    case 27: {                          /* GetFileTitle(LPCSTR, LPSTR, UINT) */
        char file[260], title[64];
        UINT size = NE->arg16(c, 4);
        if (g_str(c, NE->arg32(c, 10), file, sizeof(file))) {
            short r = cd_file_title(file, title, sizeof(title));
            *v = (uint16_t)r;
            if (!r && !g_out(c, NE->arg32(c, 6), title, size))
                *v = (uint16_t)(lstrlen(title) + 1);
        }
        return WINDLL_WORD;
    }
    case 6: case 7: case 8: case 13: case 14: case 16: case 21: case 22:
        return WINDLL_WORD;             /* the dialogs' procedures: for templates, none */
    }
    return WINDLL_NONE;
}

static void task_end(uint16_t task)
{
    for (int i = 0; i < FINDS; i++)
        if (ne_finds[i].task == task)
            ne_finds[i].task = 0;
}

static const win_dll_export exports[] = {
    { 1, 4, "GetOpenFileName", (const void *)cd_open },
    { 2, 4, "GetSaveFileName", (const void *)cd_save },
    { 5, 4, "ChooseColor", (const void *)cd_color },
    { 6, 10, "FileOpenDlgProc", NULL },
    { 7, 10, "FileSaveDlgProc", NULL },
    { 8, 10, "ColorDlgProc", NULL },
    { 11, 4, "FindText", (const void *)cd_find },
    { 12, 4, "ReplaceText", (const void *)cd_replace },
    { 13, 10, "FindTextDlgProc", NULL },
    { 14, 10, "ReplaceTextDlgProc", NULL },
    { 15, 4, "ChooseFont", (const void *)cd_font },
    { 16, 10, "FormatCharDlgProc", NULL },
    { 20, 4, "PrintDlg", (const void *)cd_print },
    { 21, 10, "PrintDlgProc", NULL },
    { 22, 10, "PrintSetupDlgProc", NULL },
    { 26, 0, "CommDlgExtendedError", (const void *)cd_error },
    { 27, 10, "GetFileTitle", (const void *)cd_file_title },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "COMMDLG",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, NULL, ne_lparam,
};

const win_dll_info *WinDllInit(void) { return &info; }
