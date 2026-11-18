/*
 * commdlg.h - common dialogs (COMMDLG.DLL, a native DLL of WINN's:
 * apps/windll/commdlg): open/save file names, find and replace text,
 * colours, fonts, printing. Native callers call them as they are (WINN
 * loads the DLL for them). A hook (lpfnHook, with its ENABLEHOOK flag) gets
 * the dialog's messages before the dialog, WM_INITDIALOG after it (lParam
 * the structure), and the registered messages below; a template of the
 * caller's (ENABLETEMPLATE, ENABLETEMPLATEHANDLE) is a Win16 or Win32
 * program's (a native caller's are not supported), its controls Windows'
 * (dlgs.h's ids).
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
    LPOFNHOOKPROC lpfnHook;
    LPCSTR    lpTemplateName;
} OPENFILENAME, *LPOPENFILENAME;

#define OFN_READONLY            0x00000001
#define OFN_OVERWRITEPROMPT     0x00000002
#define OFN_HIDEREADONLY        0x00000004
#define OFN_NOCHANGEDIR         0x00000008
#define OFN_SHOWHELP            0x00000010
#define OFN_ENABLEHOOK          0x00000020
#define OFN_ENABLETEMPLATE      0x00000040
#define OFN_ENABLETEMPLATEHANDLE 0x00000080
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

#define CDERR_DIALOGFAILURE     0xFFFF
#define CDERR_STRUCTSIZE        0x0001
#define CDERR_INITIALIZATION    0x0002
#define CDERR_NOTEMPLATE        0x0003
#define CDERR_NOHINSTANCE       0x0004
#define CDERR_LOADSTRFAILURE    0x0005
#define CDERR_FINDRESFAILURE    0x0006
#define CDERR_LOADRESFAILURE    0x0007
#define CDERR_LOCKRESFAILURE    0x0008
#define CDERR_MEMALLOCFAILURE   0x0009
#define CDERR_MEMLOCKFAILURE    0x000A
#define CDERR_NOHOOK            0x000B
#define CDERR_REGISTERMSGFAIL   0x000C
#define PDERR_SETUPFAILURE      0x1001
#define PDERR_PARSEFAILURE      0x1002
#define PDERR_RETDEFFAILURE     0x1003
#define PDERR_LOADDRVFAILURE    0x1004
#define PDERR_GETDEVMODEFAIL    0x1005
#define PDERR_INITFAILURE       0x1006
#define PDERR_NODEVICES         0x1007
#define PDERR_NODEFAULTPRN      0x1008
#define PDERR_DNDMMISMATCH      0x1009
#define PDERR_CREATEICFAILURE   0x100A
#define PDERR_PRINTERNOTFOUND   0x100B
#define CFERR_NOFONTS           0x2001
#define CFERR_MAXLESSTHANMIN    0x2002
#define FNERR_SUBCLASSFAILURE   0x3001
#define FNERR_INVALIDFILENAME   0x3002
#define FNERR_BUFFERTOOSMALL    0x3003
#define FRERR_BUFFERLENGTHZERO  0x4001
#define CCERR_CHOOSECOLORCODES  0x5000

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
#define FR_ENABLEHOOK           0x00000100
#define FR_ENABLETEMPLATE       0x00000200
#define FR_NOUPDOWN             0x00000400
#define FR_NOMATCHCASE          0x00000800
#define FR_NOWHOLEWORD          0x00001000
#define FR_HIDEUPDOWN           0x00004000
#define FR_HIDEMATCHCASE        0x00008000
#define FR_HIDEWHOLEWORD        0x00010000
#define FR_ENABLETEMPLATEHANDLE 0x00002000

#define FINDMSGSTRING           "commdlg_FindReplace"

/* modeless: pass its messages to IsDialogMessage; the owner gets the
 * registered FINDMSGSTRING message with lParam = the FINDREPLACE */
HWND    WINAPI FindText(LPFINDREPLACE fr);
HWND    WINAPI ReplaceText(LPFINDREPLACE fr);

/* ---------------------------------------------------------------- colours */

typedef struct tagCHOOSECOLOR {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HWND      hInstance;
    COLORREF  rgbResult;            /* in with CC_RGBINIT, out */
    COLORREF *lpCustColors;         /* 16 of them, in and out */
    DWORD     Flags;
    LPARAM    lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCSTR    lpTemplateName;
} CHOOSECOLOR, *LPCHOOSECOLOR;

#define CC_RGBINIT              0x00000001
#define CC_FULLOPEN             0x00000002
#define CC_PREVENTFULLOPEN      0x00000004
#define CC_SHOWHELP             0x00000008
#define CC_ENABLEHOOK           0x00000010
#define CC_ENABLETEMPLATE       0x00000020
#define CC_ENABLETEMPLATEHANDLE 0x00000040

BOOL    WINAPI ChooseColor(LPCHOOSECOLOR cc);

/* ---------------------------------------------------------------- fonts */

typedef struct tagCHOOSEFONT {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HDC       hDC;                  /* the printer's */
    LOGFONT  *lpLogFont;            /* in with CF_INITTOLOGFONTSTRUCT, out */
    int       iPointSize;           /* out: tenths of a point */
    DWORD     Flags;
    COLORREF  rgbColors;            /* with CF_EFFECTS, in and out */
    LPARAM    lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCSTR    lpTemplateName;
    HINSTANCE hInstance;
    LPSTR     lpszStyle;            /* with CF_USESTYLE: LF_FACESIZE bytes */
    UINT      nFontType;            /* out */
    int       nSizeMin, nSizeMax;   /* with CF_LIMITSIZE: points */
} CHOOSEFONT, *LPCHOOSEFONT;

#define CF_SCREENFONTS          0x00000001
#define CF_PRINTERFONTS         0x00000002
#define CF_BOTH                 (CF_SCREENFONTS | CF_PRINTERFONTS)
#define CF_SHOWHELP             0x00000004
#define CF_ENABLEHOOK           0x00000008
#define CF_ENABLETEMPLATE       0x00000010
#define CF_ENABLETEMPLATEHANDLE 0x00000020
#define CF_INITTOLOGFONTSTRUCT  0x00000040
#define CF_USESTYLE             0x00000080
#define CF_EFFECTS              0x00000100
#define CF_APPLY                0x00000200
#define CF_ANSIONLY             0x00000400
#define CF_NOVECTORFONTS        0x00000800
#define CF_NOSIMULATIONS        0x00001000
#define CF_LIMITSIZE            0x00002000
#define CF_FIXEDPITCHONLY       0x00004000
#define CF_WYSIWYG              0x00008000
#define CF_FORCEFONTEXIST       0x00010000
#define CF_SCALABLEONLY         0x00020000
#define CF_TTONLY               0x00040000
#define CF_NOFACESEL            0x00080000
#define CF_NOSTYLESEL           0x00100000
#define CF_NOSIZESEL            0x00200000

#define SIMULATED_FONTTYPE      0x8000
#define PRINTER_FONTTYPE        0x4000
#define SCREEN_FONTTYPE         0x2000
#define BOLD_FONTTYPE           0x0100
#define ITALIC_FONTTYPE         0x0200
#define REGULAR_FONTTYPE        0x0400

BOOL    WINAPI ChooseFont(LPCHOOSEFONT cf);

/* ---------------------------------------------------------------- printing */

typedef struct tagPD {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HGLOBAL   hDevMode;
    HGLOBAL   hDevNames;
    HDC       hDC;
    DWORD     Flags;
    UINT      nFromPage, nToPage, nMinPage, nMaxPage, nCopies;
    HINSTANCE hInstance;
    LPARAM    lCustData;
    LPOFNHOOKPROC lpfnPrintHook, lpfnSetupHook;
    LPCSTR    lpPrintTemplateName, lpSetupTemplateName;
    HGLOBAL   hPrintTemplate, hSetupTemplate;
} PRINTDLG, *LPPRINTDLG;

#define PD_ALLPAGES             0x00000000
#define PD_SELECTION            0x00000001
#define PD_PAGENUMS             0x00000002
#define PD_NOSELECTION          0x00000004
#define PD_NOPAGENUMS           0x00000008
#define PD_COLLATE              0x00000010
#define PD_PRINTTOFILE          0x00000020
#define PD_PRINTSETUP           0x00000040
#define PD_NOWARNING            0x00000080
#define PD_RETURNDC             0x00000100
#define PD_RETURNIC             0x00000200
#define PD_RETURNDEFAULT        0x00000400
#define PD_SHOWHELP             0x00000800
#define PD_ENABLEPRINTHOOK      0x00001000
#define PD_ENABLESETUPHOOK      0x00002000
#define PD_ENABLEPRINTTEMPLATE  0x00004000
#define PD_ENABLESETUPTEMPLATE  0x00008000
#define PD_ENABLEPRINTTEMPLATEHANDLE 0x00010000
#define PD_ENABLESETUPTEMPLATEHANDLE 0x00020000
#define PD_USEDEVMODECOPIES     0x00040000
#define PD_DISABLEPRINTTOFILE   0x00080000
#define PD_HIDEPRINTTOFILE      0x00100000

/* no default printer: FALSE, CommDlgExtendedError PDERR_NODEFAULTPRN
 * (and Windows' message unless PD_NOWARNING or PD_RETURNDEFAULT) */
BOOL    WINAPI PrintDlg(LPPRINTDLG pd);

/* ---------------------------------------------------------------- hooks */

/* the registered messages (RegisterWindowMessage) a hook gets: the file
 * chosen (lParam the OPENFILENAME; nonzero: the dialog stays), a file
 * list's selection (wParam the list's id, lParam MAKELPARAM(index,
 * CD_LBSELCHANGE)), the colour chosen (lParam the CHOOSECOLOR; nonzero:
 * the dialog stays); the owner gets HELPMSGSTRING's as Help is pressed
 * (wParam the dialog, lParam the structure); SETRGBSTRING's sets the Color
 * dialog's colour (lParam the RGB), WM_CHOOSEFONT_GETLOGFONT gives the
 * Font dialog's (lParam a LOGFONT) */
#define FILEOKSTRING            "commdlg_FileNameOK"
#define LBSELCHSTRING           "commdlg_LBSelChangedNotify"
#define SHAREVISTRING           "commdlg_ShareViolation"
#define HELPMSGSTRING           "commdlg_help"
#define COLOROKSTRING           "commdlg_ColorOK"
#define SETRGBSTRING            "commdlg_SetRGBColor"
#define CD_LBSELNOITEMS         (-1)
#define CD_LBSELCHANGE          0
#define CD_LBSELSUB             1
#define CD_LBSELADD             2
#define WM_CHOOSEFONT_GETLOGFONT (WM_USER + 1)

/* WINN's: a Win16 or Win32 program's hooks and templates, for COMMDLG's
 * next call (the core's; the DLL's own for its Win16 callers). The flags
 * of the structure have none of them then: hooks and templates say which
 * the program has (bit 0; bit 1 the Print Setup's of PrintDlg). */
typedef struct CDHOSTN CDHOSTN;
struct CDHOSTN {
    /* the hook called: its answer. WM_INITDIALOG's lParam and the
     * registered messages' are native the structure (native) */
    UINT (*hook)(const CDHOSTN *h, int which, HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
    /* the program's template made the dialog with proc: as
     * DialogBoxIndirectN (CreateDialogIndirectN modeless) */
    int (*dialog)(const CDHOSTN *h, int which, HWND owner, DLGPROC proc, int modeless);
    /* the program's structure brought up to date from native's: its
     * address (a Win16 program's far pointer); a registered message's
     * lParam (DLL's ne_lparam) to the program's windows */
    DWORD (*guest)(const CDHOSTN *h, LPARAM native);
    /* a LOGFONT given the program at its address */
    BOOL (*logfont)(const CDHOSTN *h, DWORD at, const LOGFONT *lf);
    DWORD hooks, templates;
    const void *native;             /* COMMDLG's: the native structure */
    void *opaque, *opaque2;
    DWORD data[12];
};
void    WINAPI CommDlgHostN(const CDHOSTN *h);

#endif /* MURM_COMMDLG_H */
