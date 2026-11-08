/*
 * windows.h - native Win16-style API for murm386 EZ applications.
 *
 * Names, constants and semantics follow the Windows 3.x SDK. Types use the
 * native ARM sizes (int/UINT are 32 bit), handles are 16-bit values like in
 * Win16 so they can be handed to x86 Win16 code unchanged later on.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINDOWS_H
#define MURM_WINDOWS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- types */

#define FAR
#define NEAR
#define PASCAL
#define CDECL
#define WINAPI
#define CALLBACK
#define CONST const
#define VOID void

typedef int             BOOL;
typedef uint8_t         BYTE;
typedef uint16_t        WORD;
typedef uint32_t        DWORD;
typedef int32_t         LONG;
typedef unsigned int    UINT;
typedef int             INT;
typedef char            CHAR;
typedef BYTE           *LPBYTE;
typedef char           *LPSTR;
typedef const char     *LPCSTR;
typedef void           *LPVOID;
typedef const void     *LPCVOID;
typedef WORD           *LPWORD;
typedef DWORD          *LPDWORD;
typedef int            *LPINT;
typedef LONG           *LPLONG;
typedef BOOL           *LPBOOL;
typedef UINT            WPARAM;
typedef LONG            LPARAM;
typedef LONG            LRESULT;
typedef DWORD           COLORREF;
typedef COLORREF       *LPCOLORREF;

typedef WORD            HANDLE;
typedef HANDLE          HWND;
typedef HANDLE          HDC;
typedef HANDLE          HGDIOBJ;
typedef HANDLE          HPEN;
typedef HANDLE          HBRUSH;
typedef HANDLE          HFONT;
typedef HANDLE          HBITMAP;
typedef HANDLE          HRGN;
typedef HANDLE          HPALETTE;
typedef HANDLE          HMENU;
typedef HANDLE          HICON;
typedef HANDLE          HCURSOR;
typedef HANDLE          HACCEL;
typedef HANDLE          HINSTANCE;
typedef HANDLE          HMODULE;
typedef HANDLE          HGLOBAL;
typedef HANDLE          HLOCAL;
typedef HANDLE          HTASK;
typedef HANDLE          GLOBALHANDLE;
typedef HANDLE          LOCALHANDLE;
typedef WORD            ATOM;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

#define LOWORD(l)        ((WORD)(DWORD)(l))
#define HIWORD(l)        ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#define LOBYTE(w)        ((BYTE)(w))
#define HIBYTE(w)        ((BYTE)(((WORD)(w) >> 8) & 0xFF))
#define MAKELONG(lo, hi) ((LONG)(((WORD)(lo)) | (((DWORD)(WORD)(hi)) << 16)))
#define MAKEWORD(lo, hi) ((WORD)(((BYTE)(lo)) | (((WORD)(BYTE)(hi)) << 8)))
#define MAKELPARAM(lo, hi) ((LPARAM)MAKELONG(lo, hi))
#define MAKELRESULT(lo, hi) ((LRESULT)MAKELONG(lo, hi))
#define MAKEINTRESOURCE(i) ((LPCSTR)(uintptr_t)(WORD)(i))
#define MAKEINTATOM(i)     ((LPCSTR)(uintptr_t)(WORD)(i))
#define IS_INTRESOURCE(p)  (((uintptr_t)(p) >> 16) == 0)

typedef struct tagPOINT { int x, y; } POINT, *LPPOINT, *PPOINT;
typedef struct tagSIZE  { int cx, cy; } SIZE, *LPSIZE;
typedef struct tagRECT  { int left, top, right, bottom; } RECT, *LPRECT;
typedef const RECT *LPCRECT;

#define MAKEPOINT(l) (*((POINT *)&(l)))  /* not usable: native POINT is 2x int */

/* ---------------------------------------------------------------- colors */

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | \
                                  (((DWORD)(BYTE)(b)) << 16)))
#define PALETTERGB(r, g, b) (0x02000000UL | RGB(r, g, b))
#define PALETTEINDEX(i)     ((COLORREF)(0x01000000UL | (DWORD)(WORD)(i)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((WORD)(c)) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))

/* ---------------------------------------------------------------- KERNEL */

#define GMEM_FIXED          0x0000
#define GMEM_MOVEABLE       0x0002
#define GMEM_NOCOMPACT      0x0010
#define GMEM_NODISCARD      0x0020
#define GMEM_ZEROINIT       0x0040
#define GMEM_MODIFY         0x0080
#define GMEM_DISCARDABLE    0x0100
#define GMEM_SHARE          0x2000
#define GMEM_DDESHARE       0x2000
#define GHND                (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR                (GMEM_FIXED | GMEM_ZEROINIT)

#define LMEM_FIXED          0x0000
#define LMEM_MOVEABLE       0x0002
#define LMEM_ZEROINIT       0x0040
#define LHND                (LMEM_MOVEABLE | LMEM_ZEROINIT)
#define LPTR                (LMEM_FIXED | LMEM_ZEROINIT)

HGLOBAL WINAPI GlobalAlloc(UINT flags, DWORD bytes);
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL h, DWORD bytes, UINT flags);
HGLOBAL WINAPI GlobalFree(HGLOBAL h);
LPVOID  WINAPI GlobalLock(HGLOBAL h);
BOOL    WINAPI GlobalUnlock(HGLOBAL h);
DWORD   WINAPI GlobalSize(HGLOBAL h);
HLOCAL  WINAPI LocalAlloc(UINT flags, UINT bytes);
HLOCAL  WINAPI LocalReAlloc(HLOCAL h, UINT bytes, UINT flags);
HLOCAL  WINAPI LocalFree(HLOCAL h);
LPVOID  WINAPI LocalLock(HLOCAL h);
BOOL    WINAPI LocalUnlock(HLOCAL h);
UINT    WINAPI LocalSize(HLOCAL h);

DWORD   WINAPI GetTickCount(void);
DWORD   WINAPI GetCurrentTime(void);
DWORD   WINAPI GetVersion(void);
HMODULE WINAPI GetModuleHandle(LPCSTR name);
void    WINAPI FatalExit(int code);
void    WINAPI OutputDebugString(LPCSTR str);
BOOL    WINAPI Yield(void);

int     WINAPI lstrlen(LPCSTR s);
LPSTR   WINAPI lstrcpy(LPSTR d, LPCSTR s);
LPSTR   WINAPI lstrcpyn(LPSTR d, LPCSTR s, int n);
LPSTR   WINAPI lstrcat(LPSTR d, LPCSTR s);
int     WINAPI lstrcmp(LPCSTR a, LPCSTR b);
int     WINAPI lstrcmpi(LPCSTR a, LPCSTR b);
int     WINAPI wsprintf(LPSTR buf, LPCSTR fmt, ...);
LPSTR   WINAPI AnsiUpper(LPSTR s);
LPSTR   WINAPI AnsiLower(LPSTR s);
LPSTR   WINAPI AnsiNext(LPCSTR s);
LPSTR   WINAPI AnsiPrev(LPCSTR start, LPCSTR s);

ATOM    WINAPI AddAtom(LPCSTR name);
ATOM    WINAPI FindAtom(LPCSTR name);
ATOM    WINAPI DeleteAtom(ATOM a);
UINT    WINAPI GetAtomName(ATOM a, LPSTR buf, int size);

/* ---------------------------------------------------------------- GDI */

#define R2_BLACK        1
#define R2_NOTMERGEPEN  2
#define R2_MASKNOTPEN   3
#define R2_NOTCOPYPEN   4
#define R2_MASKPENNOT   5
#define R2_NOT          6
#define R2_XORPEN       7
#define R2_NOTMASKPEN   8
#define R2_MASKPEN      9
#define R2_NOTXORPEN    10
#define R2_NOP          11
#define R2_MERGENOTPEN  12
#define R2_COPYPEN      13
#define R2_MERGEPENNOT  14
#define R2_MERGEPEN     15
#define R2_WHITE        16

#define SRCCOPY         0x00CC0020UL
#define SRCPAINT        0x00EE0086UL
#define SRCAND          0x008800C6UL
#define SRCINVERT       0x00660046UL
#define SRCERASE        0x00440328UL
#define NOTSRCCOPY      0x00330008UL
#define NOTSRCERASE     0x001100A6UL
#define MERGECOPY       0x00C000CAUL
#define MERGEPAINT      0x00BB0226UL
#define PATCOPY         0x00F00021UL
#define PATPAINT        0x00FB0A09UL
#define PATINVERT       0x005A0049UL
#define DSTINVERT       0x00550009UL
#define BLACKNESS       0x00000042UL
#define WHITENESS       0x00FF0062UL

#define TRANSPARENT     1
#define OPAQUE          2

#define PS_SOLID        0
#define PS_DASH         1
#define PS_DOT          2
#define PS_DASHDOT      3
#define PS_DASHDOTDOT   4
#define PS_NULL         5
#define PS_INSIDEFRAME  6

#define BS_SOLID        0
#define BS_NULL         1
#define BS_HOLLOW       BS_NULL
#define BS_HATCHED      2
#define BS_PATTERN      3

#define HS_HORIZONTAL   0
#define HS_VERTICAL     1
#define HS_FDIAGONAL    2
#define HS_BDIAGONAL    3
#define HS_CROSS        4
#define HS_DIAGCROSS    5

#define WHITE_BRUSH         0
#define LTGRAY_BRUSH        1
#define GRAY_BRUSH          2
#define DKGRAY_BRUSH        3
#define BLACK_BRUSH         4
#define NULL_BRUSH          5
#define HOLLOW_BRUSH        NULL_BRUSH
#define WHITE_PEN           6
#define BLACK_PEN           7
#define NULL_PEN            8
#define OEM_FIXED_FONT      10
#define ANSI_FIXED_FONT     11
#define ANSI_VAR_FONT       12
#define SYSTEM_FONT         13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE     15
#define SYSTEM_FIXED_FONT   16

#define OBJ_PEN         1
#define OBJ_BRUSH       2

#define ERROR           0
#define NULLREGION      1
#define SIMPLEREGION    2
#define COMPLEXREGION   3

#define RGN_AND         1
#define RGN_OR          2
#define RGN_XOR         3
#define RGN_DIFF        4
#define RGN_COPY        5

#define ALTERNATE       1
#define WINDING         2

#define HORZRES         8
#define VERTRES         10
#define BITSPIXEL       12
#define PLANES          14
#define NUMCOLORS       24
#define LOGPIXELSX      88
#define LOGPIXELSY      90
#define SIZEPALETTE     104
#define NUMRESERVED     106
#define RASTERCAPS      38
#define RC_PALETTE      0x0100

#define ETO_OPAQUE      0x0002
#define ETO_CLIPPED     0x0004

#define TA_NOUPDATECP   0x0000
#define TA_UPDATECP     0x0001
#define TA_LEFT         0x0000
#define TA_RIGHT        0x0002
#define TA_CENTER       0x0006
#define TA_TOP          0x0000
#define TA_BOTTOM       0x0008
#define TA_BASELINE     0x0018

#define FW_NORMAL       400
#define FW_BOLD         700
#define ANSI_CHARSET    0
#define OEM_CHARSET     255
#define FIXED_PITCH     1
#define VARIABLE_PITCH  2
#define LF_FACESIZE     32

typedef struct tagLOGPEN {
    UINT lopnStyle; POINT lopnWidth; COLORREF lopnColor;
} LOGPEN, *LPLOGPEN;

typedef struct tagLOGBRUSH {
    UINT lbStyle; COLORREF lbColor; int lbHatch;
} LOGBRUSH, *LPLOGBRUSH;

typedef struct tagLOGFONT {
    int lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet;
    BYTE lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[LF_FACESIZE];
} LOGFONT, *LPLOGFONT;

typedef struct tagTEXTMETRIC {
    int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    int tmAveCharWidth, tmMaxCharWidth, tmWeight;
    BYTE tmItalic, tmUnderlined, tmStruckOut;
    BYTE tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmPitchAndFamily, tmCharSet;
    int tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
} TEXTMETRIC, *LPTEXTMETRIC;

typedef struct tagBITMAP {
    int bmType, bmWidth, bmHeight, bmWidthBytes;
    BYTE bmPlanes, bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *LPBITMAP;

typedef struct tagPALETTEENTRY {
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagLOGPALETTE {
    WORD palVersion, palNumEntries;
    PALETTEENTRY palPalEntry[1];
} LOGPALETTE, *LPLOGPALETTE;

HDC     WINAPI CreateCompatibleDC(HDC hdc);
HDC     WINAPI CreateDC(LPCSTR driver, LPCSTR device, LPCSTR output, const void *init);
BOOL    WINAPI DeleteDC(HDC hdc);
int     WINAPI SaveDC(HDC hdc);
BOOL    WINAPI RestoreDC(HDC hdc, int saved);
int     WINAPI GetDeviceCaps(HDC hdc, int index);

HGDIOBJ WINAPI GetStockObject(int index);
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ obj);
BOOL    WINAPI DeleteObject(HGDIOBJ obj);
int     WINAPI GetObject(HGDIOBJ obj, int size, LPVOID buf);

HPEN    WINAPI CreatePen(int style, int width, COLORREF color);
HPEN    WINAPI CreatePenIndirect(const LOGPEN *lp);
HBRUSH  WINAPI CreateSolidBrush(COLORREF color);
HBRUSH  WINAPI CreateHatchBrush(int style, COLORREF color);
HBRUSH  WINAPI CreatePatternBrush(HBITMAP bmp);
HBRUSH  WINAPI CreateBrushIndirect(const LOGBRUSH *lb);
HFONT   WINAPI CreateFont(int h, int w, int esc, int orient, int weight,
                          BYTE italic, BYTE underline, BYTE strike, BYTE charset,
                          BYTE outprec, BYTE clipprec, BYTE quality, BYTE pitch,
                          LPCSTR face);
HFONT   WINAPI CreateFontIndirect(const LOGFONT *lf);
HBITMAP WINAPI CreateBitmap(int w, int h, UINT planes, UINT bpp, const void *bits);
HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int w, int h);
LONG    WINAPI SetBitmapBits(HBITMAP bmp, DWORD count, const void *bits);
LONG    WINAPI GetBitmapBits(HBITMAP bmp, LONG count, LPVOID bits);

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF c);
COLORREF WINAPI GetTextColor(HDC hdc);
COLORREF WINAPI SetBkColor(HDC hdc, COLORREF c);
COLORREF WINAPI GetBkColor(HDC hdc);
int     WINAPI SetBkMode(HDC hdc, int mode);
int     WINAPI GetBkMode(HDC hdc);
int     WINAPI SetROP2(HDC hdc, int rop);
int     WINAPI GetROP2(HDC hdc);
UINT    WINAPI SetTextAlign(HDC hdc, UINT align);
UINT    WINAPI GetTextAlign(HDC hdc);
DWORD   WINAPI SetWindowOrg(HDC hdc, int x, int y);
DWORD   WINAPI GetWindowOrg(HDC hdc);
DWORD   WINAPI SetViewportOrg(HDC hdc, int x, int y);
DWORD   WINAPI GetViewportOrg(HDC hdc);
DWORD   WINAPI SetBrushOrg(HDC hdc, int x, int y);
COLORREF WINAPI GetNearestColor(HDC hdc, COLORREF c);

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF c);
COLORREF WINAPI GetPixel(HDC hdc, int x, int y);
DWORD   WINAPI MoveTo(HDC hdc, int x, int y);
BOOL    WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT old);
BOOL    WINAPI LineTo(HDC hdc, int x, int y);
BOOL    WINAPI Polyline(HDC hdc, const POINT *pts, int n);
BOOL    WINAPI Polygon(HDC hdc, const POINT *pts, int n);
int     WINAPI SetPolyFillMode(HDC hdc, int mode);
BOOL    WINAPI Rectangle(HDC hdc, int l, int t, int r, int b);
BOOL    WINAPI RoundRect(HDC hdc, int l, int t, int r, int b, int w, int h);
BOOL    WINAPI Ellipse(HDC hdc, int l, int t, int r, int b);
int     WINAPI FillRect(HDC hdc, const RECT *rc, HBRUSH br);
int     WINAPI FrameRect(HDC hdc, const RECT *rc, HBRUSH br);
void    WINAPI InvertRect(HDC hdc, const RECT *rc);
void    WINAPI DrawFocusRect(HDC hdc, const RECT *rc);
BOOL    WINAPI PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop);
BOOL    WINAPI BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop);
BOOL    WINAPI StretchBlt(HDC dst, int x, int y, int w, int h,
                          HDC src, int sx, int sy, int sw, int sh, DWORD rop);
BOOL    WINAPI FillRgn(HDC hdc, HRGN rgn, HBRUSH br);
BOOL    WINAPI PaintRgn(HDC hdc, HRGN rgn);

BOOL    WINAPI TextOut(HDC hdc, int x, int y, LPCSTR s, int n);
BOOL    WINAPI ExtTextOut(HDC hdc, int x, int y, UINT opts, const RECT *rc,
                          LPCSTR s, UINT n, const int *dx);
DWORD   WINAPI GetTextExtent(HDC hdc, LPCSTR s, int n);
BOOL    WINAPI GetTextExtentPoint(HDC hdc, LPCSTR s, int n, LPSIZE sz);
BOOL    WINAPI GetTextMetrics(HDC hdc, LPTEXTMETRIC tm);
int     WINAPI GetTextFace(HDC hdc, int n, LPSTR face);
DWORD   WINAPI GetTabbedTextExtent(HDC hdc, LPCSTR s, int n, int ntabs, const int *tabs);
LONG    WINAPI TabbedTextOut(HDC hdc, int x, int y, LPCSTR s, int n,
                             int ntabs, const int *tabs, int origin);

HRGN    WINAPI CreateRectRgn(int l, int t, int r, int b);
HRGN    WINAPI CreateRectRgnIndirect(const RECT *rc);
void    WINAPI SetRectRgn(HRGN rgn, int l, int t, int r, int b);
int     WINAPI CombineRgn(HRGN dst, HRGN a, HRGN b, int mode);
BOOL    WINAPI EqualRgn(HRGN a, HRGN b);
int     WINAPI OffsetRgn(HRGN rgn, int dx, int dy);
int     WINAPI GetRgnBox(HRGN rgn, LPRECT rc);
BOOL    WINAPI PtInRegion(HRGN rgn, int x, int y);
BOOL    WINAPI RectInRegion(HRGN rgn, const RECT *rc);
int     WINAPI SelectClipRgn(HDC hdc, HRGN rgn);
int     WINAPI IntersectClipRect(HDC hdc, int l, int t, int r, int b);
int     WINAPI ExcludeClipRect(HDC hdc, int l, int t, int r, int b);
int     WINAPI GetClipBox(HDC hdc, LPRECT rc);
BOOL    WINAPI PtVisible(HDC hdc, int x, int y);
BOOL    WINAPI RectVisible(HDC hdc, const RECT *rc);

HPALETTE WINAPI CreatePalette(const LOGPALETTE *lp);
HPALETTE WINAPI SelectPalette(HDC hdc, HPALETTE pal, BOOL bg);
UINT    WINAPI RealizePalette(HDC hdc);
UINT    WINAPI GetPaletteEntries(HPALETTE pal, UINT first, UINT n, LPPALETTEENTRY pe);
UINT    WINAPI SetPaletteEntries(HPALETTE pal, UINT first, UINT n, const PALETTEENTRY *pe);
UINT    WINAPI GetSystemPaletteEntries(HDC hdc, UINT first, UINT n, LPPALETTEENTRY pe);

/* ---------------------------------------------------------------- RECT */

BOOL    WINAPI SetRect(LPRECT rc, int l, int t, int r, int b);
BOOL    WINAPI SetRectEmpty(LPRECT rc);
BOOL    WINAPI CopyRect(LPRECT d, const RECT *s);
BOOL    WINAPI IsRectEmpty(const RECT *rc);
BOOL    WINAPI PtInRect(const RECT *rc, POINT pt);
BOOL    WINAPI OffsetRect(LPRECT rc, int dx, int dy);
BOOL    WINAPI InflateRect(LPRECT rc, int dx, int dy);
BOOL    WINAPI IntersectRect(LPRECT d, const RECT *a, const RECT *b);
BOOL    WINAPI UnionRect(LPRECT d, const RECT *a, const RECT *b);
BOOL    WINAPI SubtractRect(LPRECT d, const RECT *a, const RECT *b);
BOOL    WINAPI EqualRect(const RECT *a, const RECT *b);

/* ---------------------------------------------------------------- profiles */

int     WINAPI GetPrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR def,
                                       LPSTR buf, int size, LPCSTR file);
UINT    WINAPI GetPrivateProfileInt(LPCSTR section, LPCSTR key, int def, LPCSTR file);
int     WINAPI GetProfileString(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR buf, int size);
UINT    WINAPI GetProfileInt(LPCSTR section, LPCSTR key, int def);
BOOL    WINAPI WritePrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file);
BOOL    WINAPI WriteProfileString(LPCSTR section, LPCSTR key, LPCSTR value);
UINT    WINAPI GetWindowsDirectory(LPSTR buf, UINT size);
UINT    WINAPI GetDriveType(int drive);     /* 0 = A: */
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED     3
#define DRIVE_REMOTE    4
UINT    WINAPI GetSystemDirectory(LPSTR buf, UINT size);

/* ---------------------------------------------------------------- files */

typedef int HFILE;
#define HFILE_ERROR     ((HFILE)-1)
#define OF_READ         0x0000
#define OF_WRITE        0x0001
#define OF_READWRITE    0x0002

HFILE   WINAPI _lopen(LPCSTR path, int mode);
HFILE   WINAPI _lcreat(LPCSTR path, int attr);
HFILE   WINAPI _lclose(HFILE f);
UINT    WINAPI _lread(HFILE f, void *buf, UINT n);
UINT    WINAPI _lwrite(HFILE f, const void *buf, UINT n);
LONG    WINAPI _hread(HFILE f, void *buf, LONG n);
LONG    WINAPI _hwrite(HFILE f, const void *buf, LONG n);
LONG    WINAPI _llseek(HFILE f, LONG off, int origin);

/* Native extensions: the files matching spec as NUL-separated names ending
 * with a second NUL (returns how many), and whether a file is a Windows
 * application. */
/* The local date and time of day. */
typedef struct {
    WORD wYear, wMonth, wDayOfWeek, wDay;   /* month 1-12, day of week 0 = Sunday */
    WORD wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME_N;
void    WINAPI GetLocalTimeN(SYSTEMTIME_N *t);
BOOL    WINAPI SetLocalTimeN(const SYSTEMTIME_N *t);

int     WINAPI WinDirListN(LPCSTR spec, LPSTR buf, int size);
int     WINAPI WinDirListAttrN(LPCSTR spec, UINT attr, LPSTR buf, int size); /* attr 0x10: subdirectories */
BOOL    WINAPI IsWinAppN(LPCSTR path);

/* WinFindFilesN entries: DOS attributes, packed DOS date and time */
typedef struct {
    char  name[13];
    BYTE  attr;             /* 0x01 read-only, 0x02 hidden, 0x04 system, 0x10 directory, 0x20 archive */
    WORD  time, date;
    DWORD size;
} WINFIND_N;
int     WINAPI WinFindFilesN(LPCSTR spec, UINT attr, WINFIND_N *out, int max);
BOOL    WINAPI DeleteFileN(LPCSTR path);
BOOL    WINAPI MoveFileN(LPCSTR from, LPCSTR to);
BOOL    WINAPI CreateDirectoryN(LPCSTR path);
BOOL    WINAPI RemoveDirectoryN(LPCSTR path);
BOOL    WINAPI GetDiskFreeSpaceN(int drive, DWORD *free_bytes, DWORD *total_bytes);
int     WINAPI GetCurrentDirectoryN(LPSTR buf, int size);

/* ---------------------------------------------------------------- tasks */

UINT    WINAPI WinExec(LPCSTR cmdline, UINT show);
HTASK   WINAPI GetCurrentTask(void);
BOOL    WINAPI IsTask(HTASK task);
UINT    WINAPI GetNumTasks(void);

/* Native extension: description of the display driver, returns colours. */
UINT    WINAPI GetDisplayDriverInfoN(LPSTR buf, int size);

#include "winuser.h"

#ifdef __cplusplus
}
#endif

#endif /* MURM_WINDOWS_H */
