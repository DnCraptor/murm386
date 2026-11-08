/*
 * commdlg.h - common dialogs (COMMDLG): open/save file names, find text.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_COMMDLG_H
#define MURM_COMMDLG_H

#include "windows.h"

typedef UINT (CALLBACK *LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagOFN {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HINSTANCE hInstance;
    LPCSTR    lpstrFilter;          /* "Text Files (*.TXT)\0*.txt\0...\0\0" */
    LPSTR     lpstrCustomFilter;
    DWORD     nMaxCustFilter;
    DWORD     nFilterIndex;         /* 1-based */
    LPSTR     lpstrFile;            /* in: initial name, out: full path */
    DWORD     nMaxFile;
    LPSTR     lpstrFileTitle;       /* out: name without the directory */
    DWORD     nMaxFileTitle;
    LPCSTR    lpstrInitialDir;
    LPCSTR    lpstrTitle;
    DWORD     Flags;
    UINT      nFileOffset;
    UINT      nFileExtension;
    LPCSTR    lpstrDefExt;
    LPARAM    lCustData;
    LPOFNHOOKPROC lpfnHook;         /* not supported */
    LPCSTR    lpTemplateName;       /* not supported */
} OPENFILENAME, *LPOPENFILENAME;

#define OFN_READONLY            0x00000001
#define OFN_OVERWRITEPROMPT     0x00000002
#define OFN_HIDEREADONLY        0x00000004
#define OFN_NOCHANGEDIR         0x00000008
#define OFN_SHOWHELP            0x00000010
#define OFN_ENABLEHOOK          0x00000020
#define OFN_ENABLETEMPLATE      0x00000040
#define OFN_NOVALIDATE          0x00000100
#define OFN_ALLOWMULTISELECT    0x00000200
#define OFN_EXTENSIONDIFFERENT  0x00000400
#define OFN_PATHMUSTEXIST       0x00000800
#define OFN_FILEMUSTEXIST       0x00001000
#define OFN_CREATEPROMPT        0x00002000
#define OFN_NOREADONLYRETURN    0x00008000
#define OFN_NOTESTFILECREATE    0x00010000

BOOL    WINAPI GetOpenFileName(LPOPENFILENAME ofn);
BOOL    WINAPI GetSaveFileName(LPOPENFILENAME ofn);
short   WINAPI GetFileTitle(LPCSTR file, LPSTR title, UINT size);
DWORD   WINAPI CommDlgExtendedError(void);

#define CDERR_STRUCTSIZE        0x0001
#define CDERR_INITIALIZATION    0x0002
#define FNERR_BUFFERTOOSMALL    0x3003

typedef struct tagFINDREPLACE {
    DWORD     lStructSize;
    HWND      hwndOwner;            /* gets FINDMSGSTRING messages */
    HINSTANCE hInstance;
    DWORD     Flags;
    LPSTR     lpstrFindWhat;
    LPSTR     lpstrReplaceWith;
    UINT      wFindWhatLen;
    UINT      wReplaceWithLen;
    LPARAM    lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCSTR    lpTemplateName;
} FINDREPLACE, *LPFINDREPLACE;

#define FR_DOWN                 0x00000001
#define FR_WHOLEWORD            0x00000002
#define FR_MATCHCASE            0x00000004
#define FR_FINDNEXT             0x00000008
#define FR_REPLACE              0x00000010
#define FR_REPLACEALL           0x00000020
#define FR_DIALOGTERM           0x00000040
#define FR_SHOWHELP             0x00000080
#define FR_NOUPDOWN             0x00000400
#define FR_NOMATCHCASE          0x00000800
#define FR_NOWHOLEWORD          0x00001000
#define FR_HIDEUPDOWN           0x00004000
#define FR_HIDEMATCHCASE        0x00008000
#define FR_HIDEWHOLEWORD        0x00010000

#define FINDMSGSTRING           "commdlg_FindReplace"

/* modeless: pass its messages to IsDialogMessage; the owner gets the
 * registered FINDMSGSTRING message with lParam = the FINDREPLACE */
HWND    WINAPI FindText(LPFINDREPLACE fr);
HWND    WINAPI ReplaceText(LPFINDREPLACE fr);

#endif /* MURM_COMMDLG_H */
