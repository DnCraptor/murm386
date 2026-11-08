/*
 * commdlg.c - common dialogs (module COMMDLG): GetOpenFileName,
 * GetSaveFileName, GetFileTitle, CommDlgExtendedError, FindText and
 * ReplaceText.
 *
 * The file dialog has the layout of Windows 3.1: file name, the files of
 * the current directory, its subdirectories ([..] goes up) and the list of
 * file types. The directory it shows is remembered between calls (the
 * current directory of Windows 3.x); it starts in the Windows directory.
 * Hooks and templates are not supported.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"
#include "commdlg.h"

#define IDC_FILE    1152
#define IDC_FILES   1120
#define IDC_PATH    1088
#define IDC_DIRS    1121
#define IDC_TYPES   1136

#define LIST_BYTES  4096

static DWORD last_error;
static char  last_dir[128];

DWORD WINAPI CommDlgExtendedError(void) { return last_error; }

static const char *name_part(const char *p)
{
    const char *b = p;
    for (; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    return b;
}

short WINAPI GetFileTitle(LPCSTR file, LPSTR title, UINT size)
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

typedef struct {
    OPENFILENAME *ofn;
    int   save;
    char  dir[128];                 /* no trailing backslash, except "C:\" */
    char  pattern[64];              /* "*.txt;*.ini" */
} fdlg;

static fdlg *F;                     /* the dialog running (they are modal) */

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

static int exists(const char *path, UINT attr)
{
    static char one[64];
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

static void fill(HWND dlg)
{
    static char names[LIST_BYTES];
    char spec[200];
    HWND files = GetDlgItem(dlg, IDC_FILES), dirs = GetDlgItem(dlg, IDC_DIRS);
    SendMessage(files, LB_RESETCONTENT, 0, 0);
    SendMessage(dirs, LB_RESETCONTENT, 0, 0);
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
        WinDirListAttrN(spec, 0, names, sizeof(names));
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
    WinDirListAttrN(spec, 0x10, names, sizeof(names));
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
    char shown[128];
    lstrcpyn(shown, F->dir, sizeof(shown));
    AnsiLower(shown);
    SetDlgItemText(dlg, IDC_PATH, shown);
}

static void change_dir(HWND dlg, const char *item)
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
    AnsiUpper(F->dir);
    fill(dlg);
}

/* the dialog's answer: 1 done, 0 stay */
static int accept(HWND dlg)
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
            change_dir(dlg, item);
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
        fill(dlg);
        return 0;
    }
    if ((t[0] && t[1] == ':') || t[0] == '\\')
        lstrcpyn(full, t, sizeof(full));
    else
        join(full, sizeof(full), F->dir, t);
    AnsiUpper(full);
    /* a directory: go there */
    if (!has_wild(full) && exists(full, 0x10)) {
        lstrcpyn(F->dir, full, sizeof(F->dir));
        SetDlgItemText(dlg, IDC_FILE, F->pattern);
        fill(dlg);
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
        SetDlgItemText(dlg, IDC_FILE, F->pattern);
        fill(dlg);
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
        fill(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            accept(dlg);
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
                accept(dlg);
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
                    change_dir(dlg, item);
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
                SetDlgItemText(dlg, IDC_FILE, F->pattern);
                fill(dlg);
            }
            return TRUE;
        }
        break;
    }
    (void)lp;
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
    fdlg d, *outer = F;
    memset(&d, 0, sizeof(d));
    d.ofn = o;
    d.save = save;
    lstrcpy(d.pattern, "*.*");
    /* where to start: the initial directory, the one in the name, the last one */
    if (o->lpstrInitialDir && *o->lpstrInitialDir)
        lstrcpyn(d.dir, o->lpstrInitialDir, sizeof(d.dir));
    else if (o->lpstrFile[0] && name_part(o->lpstrFile) > o->lpstrFile) {
        int n = (int)(name_part(o->lpstrFile) - o->lpstrFile);
        lstrcpyn(d.dir, o->lpstrFile, n + 1 < (int)sizeof(d.dir) ? n + 1 : (int)sizeof(d.dir));
        if (n > 1 && d.dir[n - 1] == '\\' && d.dir[n - 2] != ':')
            d.dir[n - 1] = 0;
    } else if (last_dir[0])
        lstrcpy(d.dir, last_dir);
    else
        GetWindowsDirectory(d.dir, sizeof(d.dir));
    AnsiUpper(d.dir);
    static const DLGTEMPLATE_N tpl_open = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 10, 20, 20, 264, 134,
        NULL, NULL, "Open", 0, NULL
    };
    static const DLGTEMPLATE_N tpl_save = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 10, 20, 20, 264, 134,
        NULL, NULL, "Save As", 0, NULL
    };
    F = &d;
    int r = DialogBoxIndirectN(o->hInstance, save ? &tpl_save : &tpl_open, file_items,
                               o->hwndOwner, file_proc);
    F = outer;
    return r == IDOK;
}

BOOL WINAPI GetOpenFileName(LPOPENFILENAME ofn) { return file_dialog(ofn, 0); }
BOOL WINAPI GetSaveFileName(LPOPENFILENAME ofn) { return file_dialog(ofn, 1); }

/* ---------------------------------------------------------------- find / replace */

#define IDC_WHAT    1152
#define IDC_WITH    1153
#define IDC_WHOLE   1040
#define IDC_CASE    1041
#define IDC_UP      1056
#define IDC_DOWN    1057
#define IDC_REPLACE 1024
#define IDC_REPALL  1025

static UINT find_msg;

typedef struct {
    FINDREPLACE *fr;
    int replace;
} fdata;

static fdata finds[4];              /* modeless: one per open dialog */
static HWND  find_wnd[4];

static fdata *find_of(HWND dlg)
{
    for (int i = 0; i < 4; i++)
        if (find_wnd[i] == dlg)
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
        for (int i = 0; i < 4; i++)
            if (find_wnd[i] == dlg)
                find_wnd[i] = 0;
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
        last_error = CDERR_INITIALIZATION;
        return 0;
    }
    if (!find_msg)
        find_msg = RegisterWindowMessage(FINDMSGSTRING);
    int slot = -1;
    for (int i = 0; i < 4 && slot < 0; i++)
        if (!find_wnd[i] || !IsWindow(find_wnd[i]))
            slot = i;
    if (slot < 0)
        return 0;
    finds[slot].fr = fr;
    finds[slot].replace = replace;
    static const DLGTEMPLATE_N tpl_find = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE, 9, 30, 52, 236, 62,
        NULL, NULL, "Find", 0, NULL
    };
    static const DLGTEMPLATE_N tpl_replace = {
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE, 10, 36, 44, 230, 94,
        NULL, NULL, "Replace", 0, NULL
    };
    find_wnd[slot] = (HWND)-1;      /* reserved while it is being made */
    HWND dlg = CreateDialogIndirectN(fr->hInstance, replace ? &tpl_replace : &tpl_find,
                                     replace ? replace_items : find_items, fr->hwndOwner, find_proc);
    find_wnd[slot] = dlg;
    if (!dlg)
        return 0;
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
    EnableWindow(GetDlgItem(dlg, IDOK), fr->lpstrFindWhat[0] != 0);
    if (replace) {
        EnableWindow(GetDlgItem(dlg, IDC_REPLACE), fr->lpstrFindWhat[0] != 0);
        EnableWindow(GetDlgItem(dlg, IDC_REPALL), fr->lpstrFindWhat[0] != 0);
    }
    SendDlgItemMessage(dlg, IDC_WHAT, EM_SETSEL, 0, MAKELPARAM(0, -1));
    SetFocus(GetDlgItem(dlg, IDC_WHAT));
    return dlg;
}

HWND WINAPI FindText(LPFINDREPLACE fr) { return find_dialog(fr, 0); }
HWND WINAPI ReplaceText(LPFINDREPLACE fr) { return find_dialog(fr, 1); }
