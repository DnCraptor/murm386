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
UINT    WINAPI AnsiUpperBuff(LPSTR s, UINT n);
UINT    WINAPI AnsiLowerBuff(LPSTR s, UINT n);
BOOL    WINAPI IsCharAlpha(char c);
BOOL    WINAPI IsCharAlphaNumeric(char c);
BOOL    WINAPI IsCharUpper(char c);
BOOL    WINAPI IsCharLower(char c);

ATOM    WINAPI AddAtom(LPCSTR name);
ATOM    WINAPI FindAtom(LPCSTR name);
ATOM    WINAPI DeleteAtom(ATOM a);
UINT    WINAPI GetAtomName(ATOM a, LPSTR buf, int size);
ATOM    WINAPI GlobalAddAtom(LPCSTR name);
ATOM    WINAPI GlobalFindAtom(LPCSTR name);
ATOM    WINAPI GlobalDeleteAtom(ATOM a);
UINT    WINAPI GlobalGetAtomName(ATOM a, LPSTR buf, int size);

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
#define DRIVERVERSION   0
#define TECHNOLOGY      2
#define HORZSIZE        4
#define VERTSIZE        6
#define NUMBRUSHES      16
#define NUMPENS         18
#define NUMFONTS        22
#define CURVECAPS       28
#define LINECAPS        30
#define POLYGONALCAPS   32
#define TEXTCAPS        34
#define CLIPCAPS        36
#define ASPECTX         40
#define ASPECTY         42
#define ASPECTXY        44
#define COLORRES        108
#define DT_RASDISPLAY   1
#define CP_RECTANGLE    1
#define CC_CIRCLES      0x0001
#define CC_PIE          0x0002
#define CC_CHORD        0x0004
#define CC_ELLIPSES     0x0008
#define CC_WIDE         0x0010
#define CC_INTERIORS    0x0080
#define CC_ROUNDRECT    0x0100
#define LC_POLYLINE     0x0002
#define LC_WIDE         0x0010
#define LC_STYLED       0x0020
#define LC_INTERIORS    0x0080
#define PC_POLYGON      0x0001
#define PC_RECTANGLE    0x0002
#define PC_SCANLINE     0x0008
#define PC_WIDE         0x0010
#define PC_INTERIORS    0x0080

/* the mapping modes */
#define MM_TEXT         1
#define MM_LOMETRIC     2
#define MM_HIMETRIC     3
#define MM_LOENGLISH    4
#define MM_HIENGLISH    5
#define MM_TWIPS        6
#define MM_ISOTROPIC    7
#define MM_ANISOTROPIC  8

#define FLOODFILLBORDER  0
#define FLOODFILLSURFACE 1
#define BLACKONWHITE    1
#define WHITEONBLACK    2
#define COLORONCOLOR    3

/* DIBs */
#define BI_RGB          0
#define DIB_RGB_COLORS  0
#define DIB_PAL_COLORS  1
#define CBM_INIT        0x00000004

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

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth, biHeight;
    WORD  biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG  biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPCOREHEADER {
    DWORD bcSize;
    WORD  bcWidth, bcHeight, bcPlanes, bcBitCount;
} BITMAPCOREHEADER, *LPBITMAPCOREHEADER;

typedef struct tagRGBQUAD {
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;

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
DWORD   WINAPI GetBrushOrg(HDC hdc);
BOOL    WINAPI GetBrushOrgEx(HDC hdc, LPPOINT pt);
COLORREF WINAPI GetNearestColor(HDC hdc, COLORREF c);

/* the mapping modes */
int     WINAPI SetMapMode(HDC hdc, int mode);
int     WINAPI GetMapMode(HDC hdc);
DWORD   WINAPI SetWindowExt(HDC hdc, int x, int y);
DWORD   WINAPI SetViewportExt(HDC hdc, int x, int y);
DWORD   WINAPI GetWindowExt(HDC hdc);
DWORD   WINAPI GetViewportExt(HDC hdc);
BOOL    WINAPI SetWindowExtEx(HDC hdc, int x, int y, LPSIZE old);
BOOL    WINAPI SetViewportExtEx(HDC hdc, int x, int y, LPSIZE old);
BOOL    WINAPI GetWindowExtEx(HDC hdc, LPSIZE sz);
BOOL    WINAPI GetViewportExtEx(HDC hdc, LPSIZE sz);
DWORD   WINAPI ScaleViewportExt(HDC hdc, int xn, int xd, int yn, int yd);
DWORD   WINAPI ScaleWindowExt(HDC hdc, int xn, int xd, int yn, int yd);
BOOL    WINAPI ScaleViewportExtEx(HDC hdc, int xn, int xd, int yn, int yd, LPSIZE old);
BOOL    WINAPI ScaleWindowExtEx(HDC hdc, int xn, int xd, int yn, int yd, LPSIZE old);
BOOL    WINAPI SetWindowOrgEx(HDC hdc, int x, int y, LPPOINT old);
BOOL    WINAPI SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT old);
BOOL    WINAPI GetWindowOrgEx(HDC hdc, LPPOINT pt);
BOOL    WINAPI GetViewportOrgEx(HDC hdc, LPPOINT pt);
DWORD   WINAPI OffsetWindowOrg(HDC hdc, int x, int y);
DWORD   WINAPI OffsetViewportOrg(HDC hdc, int x, int y);
BOOL    WINAPI OffsetWindowOrgEx(HDC hdc, int x, int y, LPPOINT old);
BOOL    WINAPI OffsetViewportOrgEx(HDC hdc, int x, int y, LPPOINT old);
BOOL    WINAPI LPtoDP(HDC hdc, LPPOINT pts, int n);
BOOL    WINAPI DPtoLP(HDC hdc, LPPOINT pts, int n);
DWORD   WINAPI GetCurrentPosition(HDC hdc);
BOOL    WINAPI GetCurrentPositionEx(HDC hdc, LPPOINT pt);
DWORD   WINAPI GetDCOrg(HDC hdc);
int     WINAPI SetStretchBltMode(HDC hdc, int mode);
int     WINAPI GetStretchBltMode(HDC hdc);
int     WINAPI SetTextCharacterExtra(HDC hdc, int n);
int     WINAPI GetTextCharacterExtra(HDC hdc);
int     WINAPI SetTextJustification(HDC hdc, int extra, int count);
BOOL    WINAPI UnrealizeObject(HGDIOBJ obj);
HDC     WINAPI CreateIC(LPCSTR driver, LPCSTR device, LPCSTR output, const void *init);
HBITMAP WINAPI CreateDiscardableBitmap(HDC hdc, int w, int h);
DWORD   WINAPI SetBitmapDimension(HBITMAP bmp, int x, int y);
DWORD   WINAPI GetBitmapDimension(HBITMAP bmp);
BOOL    WINAPI SetBitmapDimensionEx(HBITMAP bmp, int x, int y, LPSIZE old);
BOOL    WINAPI GetBitmapDimensionEx(HBITMAP bmp, LPSIZE sz);
UINT    WINAPI GetNearestPaletteIndex(HPALETTE pal, COLORREF c);

/* DIBs: a header (BITMAPINFO, its colours after it) and the bits apart */
int     WINAPI SetDIBitsToDevice(HDC hdc, int x, int y, int w, int h, int sx, int sy,
                                 UINT start, UINT lines, const void *bits,
                                 const BITMAPINFO *bi, UINT usage);
int     WINAPI StretchDIBits(HDC hdc, int x, int y, int w, int h, int sx, int sy, int sw,
                             int sh, const void *bits, const BITMAPINFO *bi, UINT usage,
                             DWORD rop);
HBITMAP WINAPI CreateDIBitmap(HDC hdc, const BITMAPINFOHEADER *bih, DWORD init,
                              const void *bits, const BITMAPINFO *bi, UINT usage);
int     WINAPI SetDIBits(HDC hdc, HBITMAP bmp, UINT start, UINT lines, const void *bits,
                         const BITMAPINFO *bi, UINT usage);
int     WINAPI GetDIBits(HDC hdc, HBITMAP bmp, UINT start, UINT lines, void *bits,
                         BITMAPINFO *bi, UINT usage);
HBRUSH  WINAPI CreateDIBPatternBrush(HGLOBAL dib, UINT usage);

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF c);
COLORREF WINAPI GetPixel(HDC hdc, int x, int y);
DWORD   WINAPI MoveTo(HDC hdc, int x, int y);
BOOL    WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT old);
BOOL    WINAPI LineTo(HDC hdc, int x, int y);
BOOL    WINAPI Polyline(HDC hdc, const POINT *pts, int n);
BOOL    WINAPI Polygon(HDC hdc, const POINT *pts, int n);
int     WINAPI SetPolyFillMode(HDC hdc, int mode);
int     WINAPI GetPolyFillMode(HDC hdc);
BOOL    WINAPI PolyPolygon(HDC hdc, const POINT *pts, const int *counts, int npoly);
BOOL    WINAPI Arc(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2);
BOOL    WINAPI Chord(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2);
BOOL    WINAPI Pie(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2);
BOOL    WINAPI FloodFill(HDC hdc, int x, int y, COLORREF c);
BOOL    WINAPI ExtFloodFill(HDC hdc, int x, int y, COLORREF c, UINT type);
typedef void (CALLBACK *LINEDDAPROC)(int x, int y, LPARAM lp);
void    WINAPI LineDDA(int x0, int y0, int x1, int y1, LINEDDAPROC proc, LPARAM lp);
BOOL    WINAPI Rectangle(HDC hdc, int l, int t, int r, int b);
int     WINAPI MulDiv(int a, int b, int c);
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
BOOL    WINAPI InvertRgn(HDC hdc, HRGN rgn);
BOOL    WINAPI FrameRgn(HDC hdc, HRGN rgn, HBRUSH br, int w, int h);

BOOL    WINAPI TextOut(HDC hdc, int x, int y, LPCSTR s, int n);
BOOL    WINAPI ExtTextOut(HDC hdc, int x, int y, UINT opts, const RECT *rc,
                          LPCSTR s, UINT n, const int *dx);
DWORD   WINAPI GetTextExtent(HDC hdc, LPCSTR s, int n);
BOOL    WINAPI GetTextExtentPoint(HDC hdc, LPCSTR s, int n, LPSIZE sz);
BOOL    WINAPI GetTextMetrics(HDC hdc, LPTEXTMETRIC tm);
int     WINAPI GetTextFace(HDC hdc, int n, LPSTR face);
BOOL    WINAPI GetCharWidth(HDC hdc, UINT first, UINT last, int *widths);
#define RASTER_FONTTYPE 0x0001
typedef int (CALLBACK *FONTENUMPROC)(const LOGFONT *lf, const TEXTMETRIC *tm, int type,
                                     LPARAM lp);
/* a raster font file's faces (.FON, .FNT) for all to use: how many */
int     WINAPI AddFontResource(LPCSTR file);
BOOL    WINAPI RemoveFontResource(LPCSTR file);
int     WINAPI EnumFonts(HDC hdc, LPCSTR face, FONTENUMPROC proc, LPARAM lp);
int     WINAPI EnumFontFamilies(HDC hdc, LPCSTR family, FONTENUMPROC proc, LPARAM lp);
DWORD   WINAPI GetTabbedTextExtent(HDC hdc, LPCSTR s, int n, int ntabs, const int *tabs);
LONG    WINAPI TabbedTextOut(HDC hdc, int x, int y, LPCSTR s, int n,
                             int ntabs, const int *tabs, int origin);

HRGN    WINAPI CreateRectRgn(int l, int t, int r, int b);
HRGN    WINAPI CreateRectRgnIndirect(const RECT *rc);
HRGN    WINAPI CreateRoundRectRgn(int l, int t, int r, int b, int w, int h);
HRGN    WINAPI CreateEllipticRgn(int l, int t, int r, int b);
HRGN    WINAPI CreateEllipticRgnIndirect(const RECT *rc);
HRGN    WINAPI CreatePolygonRgn(const POINT *pts, int n, int mode);
HRGN    WINAPI CreatePolyPolygonRgn(const POINT *pts, const int *counts, int npoly, int mode);
int     WINAPI OffsetClipRgn(HDC hdc, int x, int y);
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

/* ---------------------------------------------------------------- metafiles */

/* A metafile: the GDI calls made on a DC of CreateMetaFile's, as records
 * (Windows 3.x's), played on another DC */
typedef HANDLE          HMETAFILE;

typedef struct tagMETAHEADER {
    WORD  mtType, mtHeaderSize, mtVersion;
    DWORD mtSize;                   /* in words, as the records' sizes */
    WORD  mtNoObjects;
    DWORD mtMaxRecord;
    WORD  mtNoParameters;
} METAHEADER;                       /* 18 bytes in a file: packed there */

typedef struct tagMETARECORD {
    DWORD rdSize;                   /* in words, these 3 too */
    WORD  rdFunction;
    WORD  rdParm[1];
} METARECORD, *LPMETARECORD;

typedef struct tagHANDLETABLE {
    HGDIOBJ objectHandle[1];
} HANDLETABLE, *LPHANDLETABLE;

typedef struct tagMETAFILEPICT {
    int mm, xExt, yExt;
    HMETAFILE hMF;
} METAFILEPICT, *LPMETAFILEPICT;

#define META_SETBKCOLOR             0x0201
#define META_SETBKMODE              0x0102
#define META_SETMAPMODE             0x0103
#define META_SETROP2                0x0104
#define META_SETRELABS              0x0105
#define META_SETPOLYFILLMODE        0x0106
#define META_SETSTRETCHBLTMODE      0x0107
#define META_SETTEXTCHAREXTRA       0x0108
#define META_SETTEXTCOLOR           0x0209
#define META_SETTEXTJUSTIFICATION   0x020A
#define META_SETWINDOWORG           0x020B
#define META_SETWINDOWEXT           0x020C
#define META_SETVIEWPORTORG         0x020D
#define META_SETVIEWPORTEXT         0x020E
#define META_OFFSETWINDOWORG        0x020F
#define META_SCALEWINDOWEXT         0x0410
#define META_OFFSETVIEWPORTORG      0x0211
#define META_SCALEVIEWPORTEXT       0x0412
#define META_LINETO                 0x0213
#define META_MOVETO                 0x0214
#define META_EXCLUDECLIPRECT        0x0415
#define META_INTERSECTCLIPRECT      0x0416
#define META_ARC                    0x0817
#define META_ELLIPSE                0x0418
#define META_FLOODFILL              0x0419
#define META_PIE                    0x081A
#define META_RECTANGLE              0x041B
#define META_ROUNDRECT              0x061C
#define META_PATBLT                 0x061D
#define META_SAVEDC                 0x001E
#define META_SETPIXEL               0x041F
#define META_OFFSETCLIPRGN          0x0220
#define META_TEXTOUT                0x0521
#define META_BITBLT                 0x0922
#define META_STRETCHBLT             0x0B23
#define META_POLYGON                0x0324
#define META_POLYLINE               0x0325
#define META_ESCAPE                 0x0626
#define META_RESTOREDC              0x0127
#define META_FILLREGION             0x0228
#define META_FRAMEREGION            0x0429
#define META_INVERTREGION           0x012A
#define META_PAINTREGION            0x012B
#define META_SELECTCLIPREGION       0x012C
#define META_SELECTOBJECT           0x012D
#define META_SETTEXTALIGN           0x012E
#define META_DRAWTEXT               0x062F
#define META_CHORD                  0x0830
#define META_SETMAPPERFLAGS         0x0231
#define META_EXTTEXTOUT             0x0A32
#define META_SETDIBTODEV            0x0D33
#define META_SELECTPALETTE          0x0234
#define META_REALIZEPALETTE         0x0035
#define META_ANIMATEPALETTE         0x0436
#define META_SETPALENTRIES          0x0037
#define META_POLYPOLYGON            0x0538
#define META_RESIZEPALETTE          0x0139
#define META_DIBBITBLT              0x0940
#define META_DIBSTRETCHBLT          0x0B41
#define META_DIBCREATEPATTERNBRUSH  0x0142
#define META_STRETCHDIB             0x0F43
#define META_EXTFLOODFILL           0x0548
#define META_DELETEOBJECT           0x01F0
#define META_CREATEPALETTE          0x00F7
#define META_CREATEBRUSH            0x00F8
#define META_CREATEPATTERNBRUSH     0x01F9
#define META_CREATEPENINDIRECT      0x02FA
#define META_CREATEFONTINDIRECT     0x02FB
#define META_CREATEBRUSHINDIRECT    0x02FC
#define META_CREATEBITMAPINDIRECT   0x02FD
#define META_CREATEBITMAP           0x06FE
#define META_CREATEREGION           0x06FF

HDC       WINAPI CreateMetaFile(LPCSTR file);
HMETAFILE WINAPI CloseMetaFile(HDC hdc);
BOOL      WINAPI DeleteMetaFile(HMETAFILE mf);
BOOL      WINAPI PlayMetaFile(HDC hdc, HMETAFILE mf);
HMETAFILE WINAPI GetMetaFile(LPCSTR file);
HMETAFILE WINAPI CopyMetaFile(HMETAFILE mf, LPCSTR file);
BOOL      WINAPI IsValidMetaFile(HMETAFILE mf);
/* the bits in a global block of their own, the metafile gone; the block's
 * bits a metafile, the block freed */
HGLOBAL   WINAPI GetMetaFileBits(HMETAFILE mf);
HMETAFILE WINAPI SetMetaFileBits(HGLOBAL bits);
HMETAFILE WINAPI SetMetaFileBitsBetter(HGLOBAL bits);
typedef int (CALLBACK *MFENUMPROC)(HDC hdc, HANDLETABLE *ht, METARECORD *mr, int n,
                                   LPARAM lp);
BOOL      WINAPI EnumMetaFile(HDC hdc, HMETAFILE mf, MFENUMPROC proc, LPARAM lp);
void      WINAPI PlayMetaFileRecord(HDC hdc, HANDLETABLE *ht, METARECORD *mr, UINT n);

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
#define OF_PARSE        0x0100
#define OF_DELETE       0x0200
#define OF_CREATE       0x1000
#define OF_PROMPT       0x2000
#define OF_EXIST        0x4000
#define OF_REOPEN       0x8000

/* OpenFile's: as Win16 has it (136 bytes) */
typedef struct tagOFSTRUCT {
    BYTE cBytes, fFixedDisk;
    WORD nErrCode;
    BYTE reserved[4];               /* the file's DOS date and time (OF_EXIST) */
    char szPathName[128];
} OFSTRUCT, *LPOFSTRUCT;
HFILE   WINAPI OpenFile(LPCSTR name, OFSTRUCT *of, UINT mode);

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
/* an application file's name for the Program Manager and its icon (what a
 * Win16 program keeps in its resources); 0 if it has none */
int     WINAPI GetAppInfoN(LPCSTR path, LPSTR desc, int size);
HICON   WINAPI ExtractIconN(LPCSTR path);
HICON   WINAPI ExtractIconExN(LPCSTR path, UINT n);     /* n-th; (UINT)-1: the count */
/* a resource of a Win16 file: its type and id (MAKEINTRESOURCE, "#n", a
 * name; NULL: the first of the type), up to cap bytes of it from offset
 * into buf (NULL: none); its size, 0 if there is none */
DWORD   WINAPI GetFileResourceN(LPCSTR path, LPCSTR type, LPCSTR id, DWORD offset, void *buf,
                                DWORD cap);

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
