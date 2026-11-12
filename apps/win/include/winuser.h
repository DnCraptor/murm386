/*
 * winuser.h - USER part of the native Win16-style API (included by windows.h).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINUSER_H
#define MURM_WINUSER_H

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL    (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void    (CALLBACK *TIMERPROC)(HWND, UINT, UINT, DWORD);
typedef BOOL    (CALLBACK *WNDENUMPROC)(HWND, LPARAM);
typedef WNDPROC FARPROC;

/* modules: native DLLs (ddi/windll.h); GetProcAddress takes a name or an
 * ordinal (MAKEINTRESOURCE) */
HINSTANCE WINAPI LoadLibrary(LPCSTR name);
void    WINAPI FreeLibrary(HINSTANCE h);
FARPROC WINAPI GetProcAddress(HINSTANCE h, LPCSTR name);

typedef struct tagMSG {
    HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam;
    DWORD time; POINT pt;
} MSG, *LPMSG;

typedef struct tagWNDCLASS {
    UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
    LPCSTR lpszMenuName, lpszClassName;
} WNDCLASS, *LPWNDCLASS;

typedef struct tagCREATESTRUCT {
    LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent;
    int cy, cx, y, x; LONG style; LPCSTR lpszName, lpszClass; DWORD dwExStyle;
} CREATESTRUCT, *LPCREATESTRUCT;

/* hooks: a modal loop's message (WH_MSGFILTER, WH_SYSMSGFILTER: MSGF_),
 * a key (WH_KEYBOARD: its virtual key, its flags), a message retrieved
 * (WH_GETMESSAGE), one sent (WH_CALLWNDPROC), the computer-based-training
 * one (WH_CBT: HCBT_), the mouse's (WH_MOUSE: the message, a
 * MOUSEHOOKSTRUCT), the shell's (WH_SHELL: HSHELL_, the window) */
#define WH_MSGFILTER      (-1)
#define WH_JOURNALRECORD  0
#define WH_JOURNALPLAYBACK 1
#define WH_KEYBOARD       2
#define WH_GETMESSAGE     3
#define WH_CALLWNDPROC    4
#define WH_CBT            5
#define WH_SYSMSGFILTER   6
#define WH_MOUSE          7
#define WH_HARDWARE       8
#define WH_DEBUG          9
#define WH_SHELL          10
#define HC_ACTION         0
#define HC_GETNEXT        1
#define HC_SKIP           2
#define HC_NOREMOVE       3
#define HCBT_MOVESIZE     0
#define HCBT_MINMAX       1
#define HCBT_QS           2
#define HCBT_CREATEWND    3
#define HCBT_DESTROYWND   4
#define HCBT_ACTIVATE     5
#define HCBT_CLICKSKIPPED 6
#define HCBT_KEYSKIPPED   7
#define HCBT_SYSCOMMAND   8
#define HCBT_SETFOCUS     9
#define MSGF_DIALOGBOX    0
#define MSGF_MESSAGEBOX   1
#define MSGF_MENU         2
#define MSGF_MOVE         3
#define MSGF_SIZE         4
#define MSGF_SCROLLBAR    5
#define MSGF_NEXTWINDOW   6
#define MSGF_MAINLOOP     8
#define HSHELL_WINDOWCREATED       1
#define HSHELL_WINDOWDESTROYED     2
#define HSHELL_ACTIVATESHELLWINDOW 3
typedef struct tagMOUSEHOOKSTRUCT {
    POINT pt; HWND hwnd; UINT wHitTestCode; DWORD dwExtraInfo;
} MOUSEHOOKSTRUCT, *LPMOUSEHOOKSTRUCT;
typedef struct tagCBTACTIVATESTRUCT {
    BOOL fMouse; HWND hWndActive;
} CBTACTIVATESTRUCT, *LPCBTACTIVATESTRUCT;
typedef struct tagCWPSTRUCT {
    LPARAM lParam; WPARAM wParam; UINT message; HWND hwnd;
} CWPSTRUCT, *LPCWPSTRUCT;
typedef struct tagCBT_CREATEWND {
    LPCREATESTRUCT lpcs; HWND hwndInsertAfter;
} CBT_CREATEWND, *LPCBT_CREATEWND;

typedef struct tagPAINTSTRUCT {
    HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate;
    BYTE rgbReserved[16];
} PAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct tagMINMAXINFO {
    POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO, *LPMINMAXINFO;

typedef struct tagWINDOWPOS {
    HWND hwnd, hwndInsertAfter; int x, y, cx, cy; UINT flags;
} WINDOWPOS, *LPWINDOWPOS;

typedef struct tagDRAWITEMSTRUCT {
    UINT CtlType, CtlID, itemID, itemAction, itemState;
    HWND hwndItem; HDC hDC; RECT rcItem; DWORD itemData;
} DRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

/* Dialog templates (in memory, DialogBoxIndirect) */
typedef struct {
    DWORD style; BYTE cdit; int x, y, cx, cy;
    LPCSTR menu, klass, caption;
    WORD pointsize; LPCSTR font;
} DLGTEMPLATE_N;
typedef struct {
    int x, y, cx, cy; int id; DWORD style; LPCSTR klass, text;
} DLGITEM_N;

/* ---------------------------------------------------------------- styles */

#define WS_OVERLAPPED       0x00000000UL
#define WS_POPUP            0x80000000UL
#define WS_CHILD            0x40000000UL
#define WS_MINIMIZE         0x20000000UL
#define WS_VISIBLE          0x10000000UL
#define WS_DISABLED         0x08000000UL
#define WS_CLIPSIBLINGS     0x04000000UL
#define WS_CLIPCHILDREN     0x02000000UL
#define WS_MAXIMIZE         0x01000000UL
#define WS_CAPTION          0x00C00000UL
#define WS_BORDER           0x00800000UL
#define WS_DLGFRAME         0x00400000UL
#define WS_VSCROLL          0x00200000UL
#define WS_HSCROLL          0x00100000UL
#define WS_SYSMENU          0x00080000UL
#define WS_THICKFRAME       0x00040000UL
#define WS_GROUP            0x00020000UL
#define WS_TABSTOP          0x00010000UL
#define WS_MINIMIZEBOX      0x00020000UL
#define WS_MAXIMIZEBOX      0x00010000UL
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | \
                             WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW      (WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_CHILDWINDOW      WS_CHILD
#define WS_TILEDWINDOW      WS_OVERLAPPEDWINDOW

#define WS_EX_DLGMODALFRAME 0x00000001UL
#define WS_EX_NOPARENTNOTIFY 0x00000004UL
#define WS_EX_TOPMOST       0x00000008UL

#define CS_VREDRAW          0x0001
#define CS_HREDRAW          0x0002
#define CS_DBLCLKS          0x0008
#define CS_OWNDC            0x0020
#define CS_CLASSDC          0x0040
#define CS_PARENTDC         0x0080
#define CS_SAVEBITS         0x0800
#define CS_GLOBALCLASS      0x4000

#define CW_USEDEFAULT       ((int)0x80000000)

/* Button styles */
#define BS_PUSHBUTTON       0x00L
#define BS_DEFPUSHBUTTON    0x01L
#define BS_CHECKBOX         0x02L
#define BS_AUTOCHECKBOX     0x03L
#define BS_RADIOBUTTON      0x04L
#define BS_3STATE           0x05L
#define BS_AUTO3STATE       0x06L
#define BS_GROUPBOX         0x07L
#define BS_USERBUTTON       0x08L
#define BS_AUTORADIOBUTTON  0x09L
#define BS_OWNERDRAW        0x0BL
#define BS_LEFTTEXT         0x20L

/* Static styles */
#define SS_LEFT             0x00L
#define SS_CENTER           0x01L
#define SS_RIGHT            0x02L
#define SS_ICON             0x03L
#define SS_BLACKRECT        0x04L
#define SS_GRAYRECT         0x05L
#define SS_WHITERECT        0x06L
#define SS_BLACKFRAME       0x07L
#define SS_GRAYFRAME        0x08L
#define SS_WHITEFRAME       0x09L
#define SS_SIMPLE           0x0BL
#define SS_LEFTNOWORDWRAP   0x0CL
#define SS_NOPREFIX         0x80L

/* Edit styles */
#define ES_LEFT             0x0000L
#define ES_CENTER           0x0001L
#define ES_RIGHT            0x0002L
#define ES_MULTILINE        0x0004L
#define ES_UPPERCASE        0x0008L
#define ES_LOWERCASE        0x0010L
#define ES_PASSWORD         0x0020L
#define ES_AUTOVSCROLL      0x0040L
#define ES_AUTOHSCROLL      0x0080L
#define ES_NOHIDESEL        0x0100L
#define ES_READONLY         0x0800L
#define ES_WANTRETURN       0x1000L

/* List box styles */
#define LBS_NOTIFY          0x0001L
#define LBS_SORT            0x0002L
#define LBS_NOREDRAW        0x0004L
#define LBS_MULTIPLESEL     0x0008L
#define LBS_STANDARD        (LBS_NOTIFY | LBS_SORT | WS_VSCROLL | WS_BORDER)

/* Dialog styles */
#define DS_ABSALIGN         0x01L
#define DS_SYSMODAL         0x02L
#define DS_LOCALEDIT        0x20L
#define DS_SETFONT          0x40L
#define DS_MODALFRAME       0x80L
#define DS_NOIDLEMSG        0x100L

/* ---------------------------------------------------------------- messages */

#define WM_NULL             0x0000
#define WM_CREATE           0x0001
#define WM_DESTROY          0x0002
#define WM_MOVE             0x0003
#define WM_SIZE             0x0005
#define WM_ACTIVATE         0x0006
#define WM_SETFOCUS         0x0007
#define WM_KILLFOCUS        0x0008
#define WM_ENABLE           0x000A
#define WM_SETREDRAW        0x000B
#define WM_SETTEXT          0x000C
#define WM_GETTEXT          0x000D
#define WM_GETTEXTLENGTH    0x000E
#define WM_PAINT            0x000F
#define WM_CLOSE            0x0010
#define WM_QUERYENDSESSION  0x0011
#define WM_QUIT             0x0012
#define WM_ERASEBKGND       0x0014
#define WM_SYSCOLORCHANGE   0x0015
#define WM_ENDSESSION       0x0016
#define WM_SHOWWINDOW       0x0018
#define WM_CTLCOLOR         0x0019
#define WM_ACTIVATEAPP      0x001C
#define WM_SETCURSOR        0x0020
#define WM_MOUSEACTIVATE    0x0021
#define WM_GETMINMAXINFO    0x0024
#define WM_DRAWITEM         0x002B
#define WM_SETFONT          0x0030
#define WM_GETFONT          0x0031
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE         0x0081
#define WM_NCDESTROY        0x0082
#define WM_NCCALCSIZE       0x0083
#define WM_NCHITTEST        0x0084
#define WM_NCPAINT          0x0085
#define WM_NCACTIVATE       0x0086
#define WM_GETDLGCODE       0x0087
#define WM_NCMOUSEMOVE      0x00A0
#define WM_NCLBUTTONDOWN    0x00A1
#define WM_NCLBUTTONUP      0x00A2
#define WM_NCLBUTTONDBLCLK  0x00A3
#define WM_KEYFIRST         0x0100
#define WM_KEYDOWN          0x0100
#define WM_KEYUP            0x0101
#define WM_CHAR             0x0102
#define WM_SYSKEYDOWN       0x0104
#define WM_SYSKEYUP         0x0105
#define WM_SYSCHAR          0x0106
#define WM_KEYLAST          0x0108
#define WM_INITDIALOG       0x0110
#define WM_COMMAND          0x0111
#define WM_SYSCOMMAND       0x0112
#define WM_TIMER            0x0113
#define WM_HSCROLL          0x0114
#define WM_VSCROLL          0x0115
#define WM_INITMENU         0x0116
#define WM_INITMENUPOPUP    0x0117
#define WM_MENUSELECT       0x011F
#define WM_ENTERIDLE        0x0121
#define WM_MOUSEFIRST       0x0200
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_LBUTTONDBLCLK    0x0203
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_RBUTTONDBLCLK    0x0206
#define WM_MBUTTONDOWN      0x0207
#define WM_MBUTTONUP        0x0208
#define WM_MBUTTONDBLCLK    0x0209
#define WM_MOUSELAST        0x0209
#define WM_PARENTNOTIFY     0x0210
#define WM_USER             0x0400

/* WM_ACTIVATE */
#define WA_INACTIVE         0
#define WA_ACTIVE           1
#define WA_CLICKACTIVE      2

/* WM_SIZE */
#define SIZE_RESTORED       0
#define SIZE_MINIMIZED      1
#define SIZE_MAXIMIZED      2

/* WM_NCHITTEST */
#define HTERROR             (-2)
#define HTTRANSPARENT       (-1)
#define HTNOWHERE           0
#define HTCLIENT            1
#define HTCAPTION           2
#define HTSYSMENU           3
#define HTSIZE              4
#define HTMENU              5
#define HTHSCROLL           6
#define HTVSCROLL           7
#define HTMINBUTTON         8
#define HTMAXBUTTON         9
#define HTLEFT              10
#define HTRIGHT             11
#define HTTOP               12
#define HTTOPLEFT           13
#define HTTOPRIGHT          14
#define HTBOTTOM            15
#define HTBOTTOMLEFT        16
#define HTBOTTOMRIGHT       17
#define HTBORDER            18

/* WM_SYSCOMMAND */
#define SC_SIZE             0xF000
#define SC_MOVE             0xF010
#define SC_MINIMIZE         0xF020
#define SC_MAXIMIZE         0xF030
#define SC_CLOSE            0xF060
#define SC_RESTORE          0xF120

/* MK_* for mouse messages */
#define MK_LBUTTON          0x0001
#define MK_RBUTTON          0x0002
#define MK_SHIFT            0x0004
#define MK_CONTROL          0x0008
#define MK_MBUTTON          0x0010

/* Button messages / notifications */
#define BM_GETCHECK         (WM_USER + 0)
#define BM_SETCHECK         (WM_USER + 1)
#define BM_GETSTATE         (WM_USER + 2)
#define BM_SETSTATE         (WM_USER + 3)
#define BM_SETSTYLE         (WM_USER + 4)
#define BN_CLICKED          0
#define BST_UNCHECKED       0
#define BST_CHECKED         1
#define BST_INDETERMINATE   2

/* Edit messages / notifications */
#define EM_GETSEL           (WM_USER + 0)
#define EM_SETSEL           (WM_USER + 1)
#define EM_LIMITTEXT        (WM_USER + 21)
#define EM_SETREADONLY      (WM_USER + 31)
#define EN_SETFOCUS         0x0100
#define EN_KILLFOCUS        0x0200
#define EN_CHANGE           0x0300

/* List box messages / notifications */
#define LB_ADDSTRING        (WM_USER + 1)
#define LB_INSERTSTRING     (WM_USER + 2)
#define LB_DELETESTRING     (WM_USER + 3)
#define LB_RESETCONTENT     (WM_USER + 5)
#define LB_SETCURSEL        (WM_USER + 7)
#define LB_GETCURSEL        (WM_USER + 9)
#define LB_GETTEXT          (WM_USER + 10)
#define LB_GETTEXTLEN       (WM_USER + 11)
#define LB_GETCOUNT         (WM_USER + 12)
#define LB_SETTOPINDEX      (WM_USER + 24)
#define LB_GETITEMDATA      (WM_USER + 26)
#define LB_SETITEMDATA      (WM_USER + 27)
#define LB_ERR              (-1)
#define LBN_SELCHANGE       1
#define LBN_DBLCLK          2

/* WM_GETDLGCODE */
#define DLGC_WANTARROWS     0x0001
#define DLGC_WANTTAB        0x0002
#define DLGC_WANTALLKEYS    0x0004
#define DLGC_HASSETSEL      0x0008
#define DLGC_DEFPUSHBUTTON  0x0010
#define DLGC_UNDEFPUSHBUTTON 0x0020
#define DLGC_RADIOBUTTON    0x0040
#define DLGC_WANTCHARS      0x0080
#define DLGC_STATIC         0x0100
#define DLGC_BUTTON         0x2000

/* Control IDs */
#define IDOK                1
#define IDCANCEL            2
#define IDABORT             3
#define IDRETRY             4
#define IDIGNORE            5
#define IDYES               6
#define IDNO                7

/* MessageBox */
#define MB_OK               0x0000
#define MB_OKCANCEL         0x0001
#define MB_ABORTRETRYIGNORE 0x0002
#define MB_YESNOCANCEL      0x0003
#define MB_YESNO            0x0004
#define MB_RETRYCANCEL      0x0005
#define MB_ICONHAND         0x0010
#define MB_ICONSTOP         MB_ICONHAND
#define MB_ICONQUESTION     0x0020
#define MB_ICONEXCLAMATION  0x0030
#define MB_ICONASTERISK     0x0040
#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_DEFBUTTON1       0x0000
#define MB_DEFBUTTON2       0x0100
#define MB_DEFBUTTON3       0x0200
#define MB_APPLMODAL        0x0000
#define MB_SYSTEMMODAL      0x1000
#define MB_TASKMODAL        0x2000

/* ShowWindow */
#define SW_HIDE             0
#define SW_SHOWNORMAL       1
#define SW_NORMAL           1
#define SW_SHOWMINIMIZED    2
#define SW_SHOWMAXIMIZED    3
#define SW_MAXIMIZE         3
#define SW_SHOWNOACTIVATE   4
#define SW_SHOW             5
#define SW_MINIMIZE         6
#define SW_SHOWMINNOACTIVE  7
#define SW_SHOWNA           8
#define SW_RESTORE          9

/* SetWindowPos */
#define SWP_NOSIZE          0x0001
#define SWP_NOMOVE          0x0002
#define SWP_NOZORDER        0x0004
#define SWP_NOREDRAW        0x0008
#define SWP_NOACTIVATE      0x0010
#define SWP_FRAMECHANGED    0x0020
#define SWP_SHOWWINDOW      0x0040
#define SWP_HIDEWINDOW      0x0080
#define HWND_TOP            ((HWND)0)
#define HWND_BOTTOM         ((HWND)1)
#define HWND_TOPMOST        ((HWND)0xFFFF)
#define HWND_NOTOPMOST      ((HWND)0xFFFE)
#define HWND_DESKTOP        ((HWND)0)

/* GetWindow */
#define GW_HWNDFIRST        0
#define GW_HWNDLAST         1
#define GW_HWNDNEXT         2
#define GW_HWNDPREV         3
#define GW_OWNER            4
#define GW_CHILD            5

/* GetWindowLong / Word */
#define GWL_WNDPROC         (-4)
#define GWW_HINSTANCE       (-6)
#define GWW_HWNDPARENT      (-8)
#define GWW_ID              (-12)
#define GWL_STYLE           (-16)
#define GWL_EXSTYLE         (-20)
#define GCL_MENUNAME        (-8)
#define GCW_HBRBACKGROUND   (-10)
#define GCW_HCURSOR         (-12)
#define GCW_HICON           (-14)
#define GCW_HMODULE         (-16)
#define GCW_CBWNDEXTRA      (-18)
#define GCW_CBCLSEXTRA      (-20)
#define GCL_WNDPROC         (-24)
#define GCW_STYLE           (-26)
#define GCW_ATOM            (-32)
#define DWL_MSGRESULT       0
#define DWL_DLGPROC         4
#define DWL_USER            8

/* PeekMessage */
#define PM_NOREMOVE         0x0000
#define PM_REMOVE           0x0001
#define PM_NOYIELD          0x0002

/* RedrawWindow / InvalidateRect */
#define RDW_INVALIDATE      0x0001
#define RDW_INTERNALPAINT   0x0002
#define RDW_ERASE           0x0004
#define RDW_VALIDATE        0x0008
#define RDW_NOERASE         0x0020
#define RDW_ALLCHILDREN     0x0080
#define RDW_UPDATENOW       0x0100
#define RDW_ERASENOW        0x0200
#define RDW_FRAME           0x0400

/* DrawText */
#define DT_TOP              0x0000
#define DT_LEFT             0x0000
#define DT_CENTER           0x0001
#define DT_RIGHT            0x0002
#define DT_VCENTER          0x0004
#define DT_BOTTOM           0x0008
#define DT_WORDBREAK        0x0010
#define DT_SINGLELINE       0x0020
#define DT_EXPANDTABS       0x0040
#define DT_NOCLIP           0x0100
#define DT_CALCRECT         0x0400
#define DT_NOPREFIX         0x0800

/* System colors */
#define COLOR_SCROLLBAR         0
#define COLOR_BACKGROUND        1
#define COLOR_ACTIVECAPTION     2
#define COLOR_INACTIVECAPTION   3
#define COLOR_MENU              4
#define COLOR_WINDOW            5
#define COLOR_WINDOWFRAME       6
#define COLOR_MENUTEXT          7
#define COLOR_WINDOWTEXT        8
#define COLOR_CAPTIONTEXT       9
#define COLOR_ACTIVEBORDER      10
#define COLOR_INACTIVEBORDER    11
#define COLOR_APPWORKSPACE      12
#define COLOR_HIGHLIGHT         13
#define COLOR_HIGHLIGHTTEXT     14
#define COLOR_BTNFACE           15
#define COLOR_BTNSHADOW         16
#define COLOR_GRAYTEXT          17
#define COLOR_BTNTEXT           18
#define COLOR_INACTIVECAPTIONTEXT 19
#define COLOR_BTNHIGHLIGHT      20
#define COLOR_MAX               21

/* System metrics */
#define SM_CXSCREEN         0
#define SM_CYSCREEN         1
#define SM_CXVSCROLL        2
#define SM_CYHSCROLL        3
#define SM_CYCAPTION        4
#define SM_CXBORDER         5
#define SM_CYBORDER         6
#define SM_CXDLGFRAME       7
#define SM_CYDLGFRAME       8
#define SM_CYVTHUMB         9
#define SM_CXHTHUMB         10
#define SM_CXICON           11
#define SM_CYICON           12
#define SM_CXCURSOR         13
#define SM_CYCURSOR         14
#define SM_CYMENU           15
#define SM_CXFULLSCREEN     16
#define SM_CYFULLSCREEN     17
#define SM_MOUSEPRESENT     19
#define SM_SWAPBUTTON       23
#define SM_CXMIN            28
#define SM_CYMIN            29
#define SM_CXSIZE           30
#define SM_CYSIZE           31
#define SM_CXFRAME          32
#define SM_CYFRAME          33
#define SM_CXDOUBLECLK      36
#define SM_CYDOUBLECLK      37
#define SM_CMETRICS         40

/* Virtual keys */
#define VK_LBUTTON      0x01
#define VK_RBUTTON      0x02
#define VK_CANCEL       0x03
#define VK_MBUTTON      0x04
#define VK_BACK         0x08
#define VK_TAB          0x09
#define VK_CLEAR        0x0C
#define VK_RETURN       0x0D
#define VK_SHIFT        0x10
#define VK_CONTROL      0x11
#define VK_MENU         0x12
#define VK_PAUSE        0x13
#define VK_CAPITAL      0x14
#define VK_ESCAPE       0x1B
#define VK_SPACE        0x20
#define VK_PRIOR        0x21
#define VK_NEXT         0x22
#define VK_END          0x23
#define VK_HOME         0x24
#define VK_LEFT         0x25
#define VK_UP           0x26
#define VK_RIGHT        0x27
#define VK_DOWN         0x28
#define VK_SELECT       0x29
#define VK_PRINT        0x2A
#define VK_SNAPSHOT     0x2C
#define VK_INSERT       0x2D
#define VK_DELETE       0x2E
#define VK_HELP         0x2F
#define VK_NUMPAD0      0x60
#define VK_MULTIPLY     0x6A
#define VK_ADD          0x6B
#define VK_SEPARATOR    0x6C
#define VK_SUBTRACT     0x6D
#define VK_DECIMAL      0x6E
#define VK_DIVIDE       0x6F
#define VK_F1           0x70
#define VK_F2           0x71
#define VK_F3           0x72
#define VK_F4           0x73
#define VK_F5           0x74
#define VK_F6           0x75
#define VK_F7           0x76
#define VK_F8           0x77
#define VK_F9           0x78
#define VK_F10          0x79
#define VK_F11          0x7A
#define VK_F12          0x7B
#define VK_NUMLOCK      0x90
#define VK_SCROLL       0x91

/* Stock cursors / icons */
#define IDC_ARROW       MAKEINTRESOURCE(32512)
#define IDC_IBEAM       MAKEINTRESOURCE(32513)
#define IDC_WAIT        MAKEINTRESOURCE(32514)
#define IDC_CROSS       MAKEINTRESOURCE(32515)
#define IDC_UPARROW     MAKEINTRESOURCE(32516)
#define IDC_SIZE        MAKEINTRESOURCE(32640)
#define IDC_ICON        MAKEINTRESOURCE(32641)
#define IDC_SIZENWSE    MAKEINTRESOURCE(32642)
#define IDC_SIZENESW    MAKEINTRESOURCE(32643)
#define IDC_SIZEWE      MAKEINTRESOURCE(32644)
#define IDC_SIZENS      MAKEINTRESOURCE(32645)
#define IDI_APPLICATION MAKEINTRESOURCE(32512)
#define IDI_HAND        MAKEINTRESOURCE(32513)
#define IDI_QUESTION    MAKEINTRESOURCE(32514)
#define IDI_EXCLAMATION MAKEINTRESOURCE(32515)
#define IDI_ASTERISK    MAKEINTRESOURCE(32516)

/* ---------------------------------------------------------------- API */

ATOM    WINAPI RegisterClass(const WNDCLASS *wc);
BOOL    WINAPI UnregisterClass(LPCSTR name, HINSTANCE inst);
BOOL    WINAPI GetClassInfo(HINSTANCE inst, LPCSTR name, LPWNDCLASS wc);
int     WINAPI GetClassName(HWND hwnd, LPSTR buf, int max);
LONG    WINAPI GetClassLong(HWND hwnd, int index);
WORD    WINAPI GetClassWord(HWND hwnd, int index);
LONG    WINAPI SetClassLong(HWND hwnd, int index, LONG v);
WORD    WINAPI SetClassWord(HWND hwnd, int index, WORD v);

HWND    WINAPI CreateWindow(LPCSTR klass, LPCSTR title, DWORD style,
                            int x, int y, int w, int h, HWND parent, HMENU menu,
                            HINSTANCE inst, LPVOID param);
HWND    WINAPI CreateWindowEx(DWORD exstyle, LPCSTR klass, LPCSTR title, DWORD style,
                              int x, int y, int w, int h, HWND parent, HMENU menu,
                              HINSTANCE inst, LPVOID param);
BOOL    WINAPI DestroyWindow(HWND hwnd);
BOOL    WINAPI IsWindow(HWND hwnd);
BOOL    WINAPI IsChild(HWND parent, HWND hwnd);
BOOL    WINAPI IsWindowVisible(HWND hwnd);
BOOL    WINAPI IsWindowEnabled(HWND hwnd);
BOOL    WINAPI IsIconic(HWND hwnd);
BOOL    WINAPI IsZoomed(HWND hwnd);
BOOL    WINAPI EnableWindow(HWND hwnd, BOOL enable);
BOOL    WINAPI ShowWindow(HWND hwnd, int cmd);
BOOL    WINAPI UpdateWindow(HWND hwnd);
BOOL    WINAPI MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint);
BOOL    WINAPI SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL    WINAPI BringWindowToTop(HWND hwnd);
HWND    WINAPI GetParent(HWND hwnd);
HWND    WINAPI SetParent(HWND hwnd, HWND parent);
HWND    WINAPI GetWindow(HWND hwnd, UINT cmd);
HWND    WINAPI GetTopWindow(HWND hwnd);
HWND    WINAPI GetNextWindow(HWND hwnd, UINT cmd);
BOOL    WINAPI AnyPopup(void);
BOOL    WINAPI FlashWindow(HWND hwnd, BOOL invert);
BOOL    WINAPI CloseWindow(HWND hwnd);
BOOL    WINAPI OpenIcon(HWND hwnd);
void    WINAPI ShowOwnedPopups(HWND hwnd, BOOL show);
HWND    WINAPI GetLastActivePopup(HWND hwnd);
BOOL    WINAPI LockWindowUpdate(HWND hwnd);
typedef HANDLE HDWP;
HDWP    WINAPI BeginDeferWindowPos(int n);
HDWP    WINAPI DeferWindowPos(HDWP d, HWND hwnd, HWND after, int x, int y, int cx, int cy,
                              UINT flags);
BOOL    WINAPI EndDeferWindowPos(HDWP d);
HWND    WINAPI SetSysModalWindow(HWND hwnd);
HWND    WINAPI GetSysModalWindow(void);
/* WinHelp */
#define HELP_CONTEXT        0x0001
#define HELP_QUIT           0x0002
#define HELP_INDEX          0x0003
#define HELP_CONTENTS       0x0003
#define HELP_HELPONHELP     0x0004
#define HELP_SETCONTENTS    0x0005
#define HELP_SETINDEX       0x0005
#define HELP_CONTEXTPOPUP   0x0008
#define HELP_FORCEFILE      0x0009
#define HELP_KEY            0x0101
#define HELP_COMMAND        0x0102
#define HELP_PARTIALKEY     0x0105
#define HELP_MULTIKEY       0x0201
#define HELP_SETWINPOS      0x0203
BOOL    WINAPI WinHelp(HWND hwnd, LPCSTR file, UINT cmd, DWORD data);
/* WINN's: WinHelp's request to the running WINHELP.EXE (its window's
 * class), sent as the registered message of this name, lParam pointing
 * to it: the command, its number, the file, its string (a keyword, a
 * macro) */
/* DDE's messages (dde.h): lParam two words, the atoms of the
 * application and topic or item, a status, global memory (GMEM_DDESHARE);
 * DATA's and POKE's memory a DDEDATA: flags (fResponse 0x1000, fRelease
 * 0x2000, fAckReq 0x8000), the format, the data */
#define WM_DDE_FIRST        0x03E0
#define WM_DDE_INITIATE     0x03E0
#define WM_DDE_TERMINATE    0x03E1
#define WM_DDE_ADVISE       0x03E2
#define WM_DDE_UNADVISE     0x03E3
#define WM_DDE_ACK          0x03E4
#define WM_DDE_DATA         0x03E5
#define WM_DDE_REQUEST      0x03E6
#define WM_DDE_POKE         0x03E7
#define WM_DDE_EXECUTE      0x03E8
#define WM_DDE_LAST         0x03E8
#define WINHELP_CLASS_N     "MS_WINHELP"
#define WINHELP_MESSAGE_N   "WinnHelpRequest"
typedef struct {
    UINT cmd;
    DWORD data;
    char file[144];
    char text[256];
} WINHELP_N;
HWND    WINAPI GetDesktopWindow(void);
HWND    WINAPI FindWindow(LPCSTR klass, LPCSTR title);
BOOL    WINAPI EnumWindows(WNDENUMPROC proc, LPARAM lp);
BOOL    WINAPI EnumChildWindows(HWND parent, WNDENUMPROC proc, LPARAM lp);
void    WINAPI CascadeChildWindows(HWND parent, UINT action);   /* the desktop or an MDI client */
void    WINAPI TileChildWindows(HWND parent, UINT action);      /* MDITILE_* */
UINT    WINAPI ArrangeIconicWindows(HWND parent);
HWND    WINAPI WindowFromPoint(POINT pt);
HWND    WINAPI ChildWindowFromPoint(HWND parent, POINT pt);
void    WINAPI GetWindowRect(HWND hwnd, LPRECT rc);
void    WINAPI GetClientRect(HWND hwnd, LPRECT rc);
void    WINAPI ClientToScreen(HWND hwnd, LPPOINT pt);
void    WINAPI ScreenToClient(HWND hwnd, LPPOINT pt);
void    WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT pts, UINT n);
void    WINAPI AdjustWindowRect(LPRECT rc, DWORD style, BOOL menu);
void    WINAPI AdjustWindowRectEx(LPRECT rc, DWORD style, BOOL menu, DWORD exstyle);

HTASK   WINAPI GetWindowTask(HWND hwnd);
LONG    WINAPI GetWindowLong(HWND hwnd, int index);
LONG    WINAPI SetWindowLong(HWND hwnd, int index, LONG value);
WORD    WINAPI GetWindowWord(HWND hwnd, int index);
WORD    WINAPI SetWindowWord(HWND hwnd, int index, WORD value);
int     WINAPI GetWindowText(HWND hwnd, LPSTR buf, int max);
int     WINAPI GetWindowTextLength(HWND hwnd);
void    WINAPI SetWindowText(HWND hwnd, LPCSTR text);
int     WINAPI GetDlgCtrlID(HWND hwnd);
BOOL    WINAPI SetProp(HWND hwnd, LPCSTR name, HANDLE data);
HANDLE  WINAPI GetProp(HWND hwnd, LPCSTR name);
HANDLE  WINAPI RemoveProp(HWND hwnd, LPCSTR name);
typedef BOOL (CALLBACK *PROPENUMPROC)(HWND hwnd, LPCSTR name, HANDLE data);
int     WINAPI EnumProps(HWND hwnd, PROPENUMPROC proc);

typedef struct tagWINDOWPLACEMENT {
    UINT  length, flags, showCmd;
    POINT ptMinPosition, ptMaxPosition;
    RECT  rcNormalPosition;
} WINDOWPLACEMENT, *LPWINDOWPLACEMENT;
BOOL    WINAPI GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp);
BOOL    WINAPI SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *wp);
void    WINAPI SwitchToThisWindow(HWND hwnd, BOOL restore);
LONG    WINAPI GetMessageExtraInfo(void);
/* ScrollWindowEx */
#define SW_SCROLLCHILDREN   0x0001
#define SW_INVALIDATE       0x0002
#define SW_ERASE            0x0004
int     WINAPI ScrollWindowEx(HWND hwnd, int dx, int dy, const RECT *rc, const RECT *clip,
                              HRGN update, LPRECT update_rc, UINT flags);
BOOL    WINAPI ScrollDC(HDC hdc, int dx, int dy, const RECT *rc, const RECT *clip, HRGN update,
                        LPRECT update_rc);
/* EnableScrollBar */
#define ESB_ENABLE_BOTH     0x0000
#define ESB_DISABLE_BOTH    0x0003
BOOL    WINAPI EnableScrollBar(HWND hwnd, int bar, UINT arrows);
HICON   WINAPI CopyIcon(HINSTANCE inst, HICON icon);
HCURSOR WINAPI CopyCursor(HINSTANCE inst, HCURSOR cursor);
HCURSOR WINAPI CreateCursor(HINSTANCE inst, int hx, int hy, int w, int h, const void *and_bits,
                            const void *xor_bits);
HICON   WINAPI CreateIcon(HINSTANCE inst, int w, int h, BYTE planes, BYTE bpp,
                          const void *and_bits, const void *xor_bits);

LRESULT WINAPI DefWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT WINAPI CallWindowProc(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT WINAPI SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
BOOL    WINAPI PostMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
BOOL    WINAPI PostAppMessage(HTASK task, UINT msg, WPARAM wp, LPARAM lp);
void    WINAPI PostQuitMessage(int code);
BOOL    WINAPI GetMessage(LPMSG msg, HWND hwnd, UINT first, UINT last);
BOOL    WINAPI PeekMessage(LPMSG msg, HWND hwnd, UINT first, UINT last, UINT flags);
BOOL    WINAPI CallMsgFilter(LPMSG msg, int code);
BOOL    WINAPI TranslateMessage(const MSG *msg);
LONG    WINAPI DispatchMessage(const MSG *msg);
BOOL    WINAPI WaitMessage(void);
DWORD   WINAPI GetMessagePos(void);
LONG    WINAPI GetMessageTime(void);
BOOL    WINAPI InSendMessage(void);

HDC     WINAPI GetDC(HWND hwnd);
HDC     WINAPI GetWindowDC(HWND hwnd);
int     WINAPI ReleaseDC(HWND hwnd, HDC hdc);
HDC     WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT ps);
void    WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps);
void    WINAPI InvalidateRect(HWND hwnd, const RECT *rc, BOOL erase);
void    WINAPI InvalidateRgn(HWND hwnd, HRGN rgn, BOOL erase);
void    WINAPI ValidateRect(HWND hwnd, const RECT *rc);
void    WINAPI ValidateRgn(HWND hwnd, HRGN rgn);
BOOL    WINAPI GetUpdateRect(HWND hwnd, LPRECT rc, BOOL erase);
int     WINAPI GetUpdateRgn(HWND hwnd, HRGN rgn, BOOL erase);
int     WINAPI ExcludeUpdateRgn(HDC hdc, HWND hwnd);
BOOL    WINAPI RedrawWindow(HWND hwnd, const RECT *rc, HRGN rgn, UINT flags);
void    WINAPI ScrollWindow(HWND hwnd, int dx, int dy, const RECT *rc, const RECT *clip);

HWND    WINAPI SetFocus(HWND hwnd);
HWND    WINAPI GetFocus(void);
HWND    WINAPI SetActiveWindow(HWND hwnd);
HWND    WINAPI GetActiveWindow(void);
HWND    WINAPI SetCapture(HWND hwnd);
void    WINAPI ReleaseCapture(void);
HWND    WINAPI GetCapture(void);
int     WINAPI GetKeyState(int vk);
int     WINAPI GetAsyncKeyState(int vk);
void    WINAPI GetKeyboardState(BYTE *keys);
void    WINAPI SetKeyboardState(const BYTE *keys);

/* GetQueueStatus */
#define QS_KEY              0x0001
#define QS_MOUSEMOVE        0x0002
#define QS_MOUSEBUTTON      0x0004
#define QS_MOUSE            (QS_MOUSEMOVE | QS_MOUSEBUTTON)
#define QS_POSTMESSAGE      0x0008
#define QS_TIMER            0x0010
#define QS_PAINT            0x0020
#define QS_SENDMESSAGE      0x0040
#define QS_INPUT            (QS_MOUSE | QS_KEY)
#define QS_ALLINPUT         0x007F
DWORD   WINAPI GetQueueStatus(UINT flags);
BOOL    WINAPI GetInputState(void);
void    WINAPI ReplyMessage(LRESULT result);

UINT    WINAPI SetTimer(HWND hwnd, UINT id, UINT elapse, TIMERPROC proc);
BOOL    WINAPI KillTimer(HWND hwnd, UINT id);

HCURSOR WINAPI LoadCursor(HINSTANCE inst, LPCSTR name);
HICON   WINAPI LoadIcon(HINSTANCE inst, LPCSTR name);
HCURSOR WINAPI SetCursor(HCURSOR cur);
HCURSOR WINAPI GetCursor(void);
void    WINAPI GetCursorPos(LPPOINT pt);
void    WINAPI SetCursorPos(int x, int y);
int     WINAPI ShowCursor(BOOL show);
BOOL    WINAPI DrawIcon(HDC hdc, int x, int y, HICON icon);
BOOL    WINAPI DestroyIcon(HICON icon);

int     WINAPI GetSystemMetrics(int index);
DWORD   WINAPI GetSysColor(int index);
void    WINAPI SetSysColors(int n, const int *idx, const COLORREF *colors);
HBRUSH  WINAPI GetSysColorBrush(int index);
int     WINAPI DrawText(HDC hdc, LPCSTR s, int n, LPRECT rc, UINT fmt);
void    WINAPI MessageBeep(UINT type);
int     WINAPI MessageBox(HWND owner, LPCSTR text, LPCSTR caption, UINT type);

/* Dialogs */
int     WINAPI DialogBoxIndirectN(HINSTANCE inst, const DLGTEMPLATE_N *tpl,
                                  const DLGITEM_N *items, HWND owner, DLGPROC proc);
HWND    WINAPI CreateDialogIndirectN(HINSTANCE inst, const DLGTEMPLATE_N *tpl,
                                     const DLGITEM_N *items, HWND owner, DLGPROC proc);
BOOL    WINAPI EndDialog(HWND dlg, int result);
LRESULT WINAPI DefDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
BOOL    WINAPI IsDialogMessage(HWND dlg, LPMSG msg);
HWND    WINAPI GetDlgItem(HWND dlg, int id);
void    WINAPI SetDlgItemText(HWND dlg, int id, LPCSTR text);
int     WINAPI GetDlgItemText(HWND dlg, int id, LPSTR buf, int max);
void    WINAPI SetDlgItemInt(HWND dlg, int id, UINT value, BOOL sign);
UINT    WINAPI GetDlgItemInt(HWND dlg, int id, BOOL *ok, BOOL sign);
LRESULT WINAPI SendDlgItemMessage(HWND dlg, int id, UINT msg, WPARAM wp, LPARAM lp);
void    WINAPI CheckDlgButton(HWND dlg, int id, UINT check);
UINT    WINAPI IsDlgButtonChecked(HWND dlg, int id);
void    WINAPI CheckRadioButton(HWND dlg, int first, int last, int check);
HWND    WINAPI GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL prev);
HWND    WINAPI GetNextDlgGroupItem(HWND dlg, HWND ctl, BOOL prev);
DWORD   WINAPI GetDialogBaseUnits(void);
BOOL    WINAPI MapDialogRect(HWND hwnd, LPRECT rc);
/* DlgDirList */
#define DDL_READWRITE       0x0000
#define DDL_READONLY        0x0001
#define DDL_HIDDEN          0x0002
#define DDL_SYSTEM          0x0004
#define DDL_DIRECTORY       0x0010
#define DDL_ARCHIVE         0x0020
#define DDL_POSTMSGS        0x2000
#define DDL_DRIVES          0x4000
#define DDL_EXCLUSIVE       0x8000
int     WINAPI DlgDirList(HWND dlg, LPSTR spec, int id, int id_static, UINT attr);
int     WINAPI DlgDirListComboBox(HWND dlg, LPSTR spec, int id, int id_static, UINT attr);
BOOL    WINAPI DlgDirSelect(HWND dlg, LPSTR out, int id);
BOOL    WINAPI DlgDirSelectEx(HWND dlg, LPSTR out, int size, int id);
BOOL    WINAPI DlgDirSelectComboBox(HWND dlg, LPSTR out, int id);
BOOL    WINAPI DlgDirSelectComboBoxEx(HWND dlg, LPSTR out, int size, int id);

/* ---------------------------------------------------------------- more constants */

#define WM_NEXTDLGCTL       0x0028
#define WM_QUERYOPEN        0x0013
#define WM_CHILDACTIVATE    0x0022

/* clipboard */
#define WM_CUT              0x0300
#define WM_COPY             0x0301
#define WM_PASTE            0x0302
#define WM_CLEAR            0x0303
#define WM_UNDO             0x0304
#define WM_RENDERFORMAT     0x0305
#define WM_RENDERALLFORMATS 0x0306
#define WM_DESTROYCLIPBOARD 0x0307
#define WM_DRAWCLIPBOARD    0x0308
#define WM_PAINTCLIPBOARD   0x0309
#define WM_VSCROLLCLIPBOARD 0x030A
#define WM_SIZECLIPBOARD    0x030B
#define WM_ASKCBFORMATNAME  0x030C
#define WM_CHANGECBCHAIN    0x030D
#define WM_HSCROLLCLIPBOARD 0x030E
#define CF_TEXT             1
#define CF_BITMAP           2
#define CF_METAFILEPICT     3
#define CF_SYLK             4
#define CF_DIF              5
#define CF_TIFF             6
#define CF_OEMTEXT          7
#define CF_DIB              8
#define CF_PALETTE          9
#define CF_PENDATA          10
#define CF_RIFF             11
#define CF_WAVE             12
#define CF_OWNERDISPLAY     0x0080
#define CF_DSPTEXT          0x0081
#define CF_DSPBITMAP        0x0082
#define CF_DSPMETAFILEPICT  0x0083
#define CF_PRIVATEFIRST     0x0200
#define CF_PRIVATELAST      0x02FF
#define CF_GDIOBJFIRST      0x0300
#define CF_GDIOBJLAST       0x03FF

BOOL    WINAPI OpenClipboard(HWND h);
BOOL    WINAPI CloseClipboard(void);
BOOL    WINAPI EmptyClipboard(void);
HWND    WINAPI GetClipboardOwner(void);
HANDLE  WINAPI SetClipboardData(UINT fmt, HANDLE data);
HANDLE  WINAPI GetClipboardData(UINT fmt);
int     WINAPI CountClipboardFormats(void);
UINT    WINAPI EnumClipboardFormats(UINT fmt);
UINT    WINAPI RegisterClipboardFormat(LPCSTR name);
int     WINAPI GetClipboardFormatName(UINT fmt, LPSTR buf, int size);
BOOL    WINAPI IsClipboardFormatAvailable(UINT fmt);
UINT    WINAPI RegisterWindowMessage(LPCSTR name);
void    WINAPI SetDoubleClickTime(UINT ms);
UINT    WINAPI GetDoubleClickTime(void);
BOOL    WINAPI SwapMouseButton(BOOL swap);
BOOL    WINAPI SystemParametersInfo(UINT action, UINT param, LPVOID pv, UINT ini);
#define SPI_GETBEEP             1
#define SPI_SETBEEP             2
#define SPI_GETMOUSE            3
#define SPI_SETMOUSE            4
#define SPI_GETBORDER           5
#define SPI_SETBORDER           6
#define SPI_GETKEYBOARDSPEED    10
#define SPI_SETKEYBOARDSPEED    11
#define SPI_LANGDRIVER          12
#define SPI_ICONHORIZONTALSPACING 13
#define SPI_GETSCREENSAVETIMEOUT 14
#define SPI_SETSCREENSAVETIMEOUT 15
#define SPI_GETSCREENSAVEACTIVE 16
#define SPI_SETSCREENSAVEACTIVE 17
#define SPI_GETGRIDGRANULARITY  18
#define SPI_SETGRIDGRANULARITY  19
#define SPI_SETDESKWALLPAPER    20
#define SPI_SETDESKPATTERN      21
#define SPI_GETKEYBOARDDELAY    22
#define SPI_SETKEYBOARDDELAY    23
#define SPI_ICONVERTICALSPACING 24
#define SPI_GETICONTITLEWRAP    25
#define SPI_SETICONTITLEWRAP    26
#define SPI_GETMENUDROPALIGNMENT 27
#define SPI_SETMENUDROPALIGNMENT 28
#define SPI_SETDOUBLECLKWIDTH   29
#define SPI_SETDOUBLECLKHEIGHT  30
#define SPI_GETICONTITLELOGFONT 31
#define SPI_SETDOUBLECLICKTIME  32
#define SPI_SETMOUSEBUTTONSWAP  33
#define SPI_SETICONTITLELOGFONT 34
#define SPI_GETFASTTASKSWITCH   35
#define SPI_SETFASTTASKSWITCH   36
#define HWND_BROADCAST          ((HWND)0xFFFF)
/* the desktop's bitmap: a .BMP file, (LPCSTR)-1 WIN.INI's again */
BOOL    WINAPI SetDeskWallPaper(LPCSTR file);
#define SPIF_UPDATEINIFILE      0x0001
#define SPIF_SENDWININICHANGE   0x0002
#define WM_WININICHANGE         0x001A

/* MDI */
#define WM_MDICREATE        0x0220
#define WM_MDIDESTROY       0x0221
#define WM_MDIACTIVATE      0x0222
#define WM_MDIRESTORE       0x0223
#define WM_MDINEXT          0x0224
#define WM_MDIMAXIMIZE      0x0225
#define WM_MDITILE          0x0226
#define WM_MDICASCADE       0x0227
#define WM_MDIICONARRANGE   0x0228
#define WM_MDIGETACTIVE     0x0229
#define WM_MDISETMENU       0x0230
#define MDITILE_VERTICAL    0x0000
#define MDITILE_HORIZONTAL  0x0001
#define MDITILE_SKIPDISABLED 0x0002
#define MDIS_ALLCHILDSTYLES 0x0001

typedef struct tagCLIENTCREATESTRUCT {
    HMENU hWindowMenu;          /* "Window" popup: the children are listed in it */
    UINT  idFirstChild;         /* their command ids start here */
} CLIENTCREATESTRUCT, *LPCLIENTCREATESTRUCT;

typedef struct tagMDICREATESTRUCT {
    LPCSTR szClass, szTitle;
    HINSTANCE hOwner;
    int x, y, cx, cy;
    LONG style;
    LPARAM lParam;
} MDICREATESTRUCT, *LPMDICREATESTRUCT;

LRESULT WINAPI DefFrameProc(HWND h, HWND client, UINT msg, WPARAM wp, LPARAM lp);
LRESULT WINAPI DefMDIChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
BOOL    WINAPI TranslateMDISysAccel(HWND client, LPMSG msg);
#define WM_CANCELMODE       0x001F
#define WM_DEADCHAR         0x0103
#define WM_SYSDEADCHAR      0x0107
#define WM_NCRBUTTONDOWN    0x00A4
#define WM_NCRBUTTONUP      0x00A5
#define WM_NCRBUTTONDBLCLK  0x00A6
#define WM_NCMBUTTONDOWN    0x00A7
#define WM_NCMBUTTONUP      0x00A8
#define WM_NCMBUTTONDBLCLK  0x00A9

#define CTLCOLOR_MSGBOX     0
#define CTLCOLOR_EDIT       1
#define CTLCOLOR_LISTBOX    2
#define CTLCOLOR_BTN        3
#define CTLCOLOR_DLG        4
#define CTLCOLOR_SCROLLBAR  5
#define CTLCOLOR_STATIC     6
#define CTLCOLOR_MAX        8

#define MA_ACTIVATE         1
#define MA_ACTIVATEANDEAT   2
#define MA_NOACTIVATE       3

#define DM_GETDEFID         (WM_USER + 0)
#define DM_SETDEFID         (WM_USER + 1)
#define DC_HASDEFID         0x534B

#define BN_PAINT            1
#define BN_HILITE           2
#define BN_UNHILITE         3
#define BN_DISABLE          4
#define BN_DOUBLECLICKED    5

#define EM_GETRECT          (WM_USER + 2)
#define EM_SCROLL           (WM_USER + 5)
#define EM_LINESCROLL       (WM_USER + 6)
#define EM_GETMODIFY        (WM_USER + 8)
#define EM_SETMODIFY        (WM_USER + 9)
#define EM_GETLINECOUNT     (WM_USER + 10)
#define EM_LINEINDEX        (WM_USER + 11)
#define EM_LINELENGTH       (WM_USER + 17)
#define EM_REPLACESEL       (WM_USER + 18)
#define EM_GETLINE          (WM_USER + 20)
#define EM_CANUNDO          (WM_USER + 22)
#define EM_UNDO             (WM_USER + 23)
#define EM_LINEFROMCHAR     (WM_USER + 25)
#define EM_SETPASSWORDCHAR  (WM_USER + 28)
#define EM_EMPTYUNDOBUFFER  (WM_USER + 29)
#define EN_UPDATE           0x0400
#define EN_ERRSPACE         0x0500
#define EN_MAXTEXT          0x0501
#define EN_HSCROLL          0x0601
#define EN_VSCROLL          0x0602

#define LB_SETSEL           (WM_USER + 6)
#define LB_GETSEL           (WM_USER + 8)
#define LB_GETTOPINDEX      (WM_USER + 15)
#define LB_FINDSTRING       (WM_USER + 16)
#define LB_GETSELCOUNT      (WM_USER + 17)
#define LB_GETITEMRECT      (WM_USER + 25)
#define LB_ERRSPACE         (-2)
#define LBN_ERRSPACE        (-2)
#define LBN_SELCANCEL       3
#define LBN_SETFOCUS        4
#define LBN_KILLFOCUS       5
#define LBS_OWNERDRAWFIXED  0x0010L
#define LBS_HASSTRINGS      0x0040L
#define LBS_NOINTEGRALHEIGHT 0x0100L
#define LBS_WANTKEYBOARDINPUT 0x0400L

#define SB_HORZ             0
#define SB_VERT             1
#define SB_CTL              2
#define SB_BOTH             3
#define SB_LINEUP           0
#define SB_LINEDOWN         1
#define SB_PAGEUP           2
#define SB_PAGEDOWN         3
#define SB_THUMBPOSITION    4
#define SB_THUMBTRACK       5
#define SB_TOP              6
#define SB_BOTTOM           7
#define SB_ENDSCROLL        8

#define SC_NEXTWINDOW       0xF040
#define SC_PREVWINDOW       0xF050
#define SC_ARRANGE          0xF110
#define SC_KEYMENU          0xF100
#define SC_TASKLIST         0xF130

/* ---------------------------------------------------------------- more API */

BOOL    WINAPI ExitWindows(DWORD reserved, UINT code);

int     WINAPI SetScrollPos(HWND hwnd, int bar, int pos, BOOL redraw);
int     WINAPI GetScrollPos(HWND hwnd, int bar);
void    WINAPI SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL redraw);
void    WINAPI GetScrollRange(HWND hwnd, int bar, LPINT min, LPINT max);
void    WINAPI ShowScrollBar(HWND hwnd, int bar, BOOL show);

void    WINAPI CreateCaret(HWND hwnd, HBITMAP bmp, int w, int h);
void    WINAPI DestroyCaret(void);
void    WINAPI HideCaret(HWND hwnd);
void    WINAPI ShowCaret(HWND hwnd);
void    WINAPI SetCaretPos(int x, int y);
void    WINAPI GetCaretPos(LPPOINT pt);
void    WINAPI SetCaretBlinkTime(UINT ms);
UINT    WINAPI GetCaretBlinkTime(void);


/* ---------------------------------------------------------------- menus */

#define MF_INSERT           0x0000
#define MF_CHANGE           0x0080
#define MF_APPEND           0x0100
#define MF_DELETE           0x0200
#define MF_REMOVE           0x1000
#define MF_BYCOMMAND        0x0000
#define MF_BYPOSITION       0x0400
#define MF_SEPARATOR        0x0800
#define MF_ENABLED          0x0000
#define MF_GRAYED           0x0001
#define MF_DISABLED         0x0002
#define MF_UNCHECKED        0x0000
#define MF_CHECKED          0x0008
#define MF_USECHECKBITMAPS  0x0200
#define MF_STRING           0x0000
#define MF_BITMAP           0x0004
#define MF_OWNERDRAW        0x0100
#define MF_POPUP            0x0010
#define MF_MENUBARBREAK     0x0020
#define MF_MENUBREAK        0x0040
#define MF_UNHILITE         0x0000
#define MF_HILITE           0x0080
#define MF_SYSMENU          0x2000
#define MF_HELP             0x4000
#define MF_MOUSESELECT      0x8000

#define TPM_LEFTBUTTON      0x0000
#define TPM_RIGHTBUTTON     0x0002
#define TPM_LEFTALIGN       0x0000
#define TPM_CENTERALIGN     0x0004
#define TPM_RIGHTALIGN      0x0008

HMENU   WINAPI CreateMenu(void);
HMENU   WINAPI CreatePopupMenu(void);
BOOL    WINAPI DestroyMenu(HMENU menu);
BOOL    WINAPI AppendMenu(HMENU menu, UINT flags, UINT id, LPCSTR text);
BOOL    WINAPI InsertMenu(HMENU menu, UINT pos, UINT flags, UINT id, LPCSTR text);
BOOL    WINAPI ModifyMenu(HMENU menu, UINT pos, UINT flags, UINT id, LPCSTR text);
BOOL    WINAPI DeleteMenu(HMENU menu, UINT pos, UINT flags);
BOOL    WINAPI RemoveMenu(HMENU menu, UINT pos, UINT flags);
HMENU   WINAPI GetMenu(HWND hwnd);
BOOL    WINAPI SetMenu(HWND hwnd, HMENU menu);
void    WINAPI DrawMenuBar(HWND hwnd);
HMENU   WINAPI GetSubMenu(HMENU menu, int pos);
int     WINAPI GetMenuItemCount(HMENU menu);
BOOL    WINAPI IsMenu(HMENU menu);
BOOL    WINAPI ChangeMenu(HMENU menu, UINT pos, LPCSTR text, UINT id, UINT flags);
DWORD   WINAPI GetMenuCheckMarkDimensions(void);
BOOL    WINAPI SetMenuItemBitmaps(HMENU menu, UINT pos, UINT flags, HBITMAP unchecked,
                                  HBITMAP checked);
UINT    WINAPI GetMenuItemID(HMENU menu, int pos);
UINT    WINAPI GetMenuState(HMENU menu, UINT id, UINT flags);
int     WINAPI GetMenuString(HMENU menu, UINT id, LPSTR buf, int max, UINT flags);
BOOL    WINAPI CheckMenuItem(HMENU menu, UINT id, UINT flags);
BOOL    WINAPI EnableMenuItem(HMENU menu, UINT id, UINT flags);
BOOL    WINAPI HiliteMenuItem(HWND hwnd, HMENU menu, UINT id, UINT flags);
HMENU   WINAPI GetSystemMenu(HWND hwnd, BOOL revert);
BOOL    WINAPI TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved,
                              HWND hwnd, const RECT *rc);

/* Accelerators. Win16 loads them from resources only; native programs
 * build a table with CreateAcceleratorTableN. */
#define FVIRTKEY            0x01
#define FNOINVERT           0x02
#define FSHIFT              0x04
#define FCONTROL            0x08
#define FALT                0x10

typedef struct { BYTE fVirt; WORD key; WORD cmd; } ACCEL_N;

HACCEL  WINAPI CreateAcceleratorTableN(const ACCEL_N *a, int n);
BOOL    WINAPI DestroyAcceleratorTableN(HACCEL h);
int     WINAPI TranslateAccelerator(HWND hwnd, HACCEL h, LPMSG msg);

/* ---------------------------------------------------------------- scroll bar control */

#define SBS_HORZ            0x0000L
#define SBS_VERT            0x0001L

/* ---------------------------------------------------------------- combo box */

#define CBS_SIMPLE          0x0001L
#define CBS_DROPDOWN        0x0002L
#define CBS_DROPDOWNLIST    0x0003L
#define CBS_OWNERDRAWFIXED  0x0010L
#define CBS_AUTOHSCROLL     0x0040L
#define CBS_OEMCONVERT      0x0080L
#define CBS_SORT            0x0100L
#define CBS_HASSTRINGS      0x0200L
#define CBS_NOINTEGRALHEIGHT 0x0400L

#define CB_GETEDITSEL       (WM_USER + 0)
#define CB_LIMITTEXT        (WM_USER + 1)
#define CB_SETEDITSEL       (WM_USER + 2)
#define CB_ADDSTRING        (WM_USER + 3)
#define CB_DELETESTRING     (WM_USER + 4)
#define CB_DIR              (WM_USER + 5)
#define CB_GETCOUNT         (WM_USER + 6)
#define CB_GETCURSEL        (WM_USER + 7)
#define CB_GETLBTEXT        (WM_USER + 8)
#define CB_GETLBTEXTLEN     (WM_USER + 9)
#define CB_INSERTSTRING     (WM_USER + 10)
#define CB_RESETCONTENT     (WM_USER + 11)
#define CB_FINDSTRING       (WM_USER + 12)
#define CB_SELECTSTRING     (WM_USER + 13)
#define CB_SETCURSEL        (WM_USER + 14)
#define CB_SHOWDROPDOWN     (WM_USER + 15)
#define CB_GETITEMDATA      (WM_USER + 16)
#define CB_SETITEMDATA      (WM_USER + 17)
#define CB_GETDROPPEDCONTROLRECT (WM_USER + 18)
#define CB_GETDROPPEDSTATE  (WM_USER + 23)
#define CB_FINDSTRINGEXACT  (WM_USER + 24)
#define CB_OKAY             0
#define CB_ERR              (-1)
#define CB_ERRSPACE         (-2)

#define CBN_ERRSPACE        (-1)
#define CBN_SELCHANGE       1
#define CBN_DBLCLK          2
#define CBN_SETFOCUS        3
#define CBN_KILLFOCUS       4
#define CBN_EDITCHANGE      5
#define CBN_EDITUPDATE      6
#define CBN_DROPDOWN        7
#define CBN_CLOSEUP         8
#define CBN_SELENDOK        9
#define CBN_SELENDCANCEL    10

/* Application entry: native apps define WinMain; the runtime's main() calls it. */
int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show);

/* ---------------------------------------------------------------- USER, the rest of 3.1's */

#define WM_SYSTIMER         0x0118
#define DCX_WINDOW          0x00000001L
#define DCX_CACHE           0x00000002L
#define DCX_CLIPCHILDREN    0x00000008L
#define DCX_CLIPSIBLINGS    0x00000010L
#define DCX_PARENTCLIP      0x00000020L
#define DCX_EXCLUDERGN      0x00000040L
#define DCX_INTERSECTRGN    0x00000080L
#define DCX_LOCKWINDOWUPDATE 0x00000400L
#define DCX_USESTYLE        0x00010000L
#define SEB_OK              1           /* SysErrorBox's buttons */
#define SEB_CANCEL          2
#define SEB_YES             3
#define SEB_NO              4
#define SEB_RETRY           5
#define SEB_ABORT           6
#define SEB_IGNORE          7
#define SEB_DEFBUTTON       0x8000

/* the clipboard viewers' chain: the one first in it (WM_DRAWCLIPBOARD as
 * the clipboard changes, WM_CHANGECBCHAIN as the chain does) */
HWND    WINAPI SetClipboardViewer(HWND h);
HWND    WINAPI GetClipboardViewer(void);
BOOL    WINAPI ChangeClipboardChain(HWND remove, HWND next);
HWND    WINAPI GetOpenClipboardWindow(void);
int     WINAPI GetPriorityClipboardFormat(UINT *formats, int n);
/* the cursor kept in a rectangle (NULL: the screen) */
void    WINAPI ClipCursor(const RECT *rc);
void    WINAPI GetClipCursor(RECT *rc);
/* a timer of the system's: WM_SYSTIMER */
UINT    WINAPI SetSystemTimer(HWND h, UINT id, UINT elapse, TIMERPROC proc);
BOOL    WINAPI KillSystemTimer(HWND h, UINT id);
DWORD   WINAPI GetTimerResolution(void);    /* microseconds a tick */
BOOL    WINAPI IsUserIdle(void);
void    WINAPI UserYield(void);
BOOL    WINAPI EnableHardwareInput(BOOL enable);   /* the keyboard's and the mouse's */
/* Windows 3.1's own USER functions a program may call (wnd.c, menu.c) */
void    WINAPI EnableOEMLayer(void);
void    WINAPI DisableOEMLayer(void);
void    WINAPI UnloadInstalledDrivers(void);
DWORD   WINAPI IconSize(void);                  /* MAKELONG(width, height) */
int     WINAPI LockMyTask(BOOL lock);
HWND    WINAPI GetNextQueueWindow(HWND h, BOOL previous);  /* its task's next one */
HMENU   WINAPI LookupMenuHandle(HMENU menu, int id);       /* the (sub)menu with id */
/* Windows 95's USER for Win16 programs (wnd.c, menu.c, icon.c, sysmet.c) */
#define WS_EX_TRANSPARENT   0x00000020UL
#define CWP_ALL             0x0000
#define CWP_SKIPINVISIBLE   0x0001
#define CWP_SKIPDISABLED    0x0002
#define CWP_SKIPTRANSPARENT 0x0004
HWND    WINAPI ChildWindowFromPointEx(HWND parent, POINT pt, UINT flags);
LPARAM  WINAPI SetMessageExtraInfo(LPARAM info);
BOOL    WINAPI SetWindowContextHelpID(HWND h, DWORD id);
DWORD   WINAPI GetWindowContextHelpID(HWND h);
BOOL    WINAPI SetMenuContextHelpId(HMENU menu, UINT id);
UINT    WINAPI GetMenuContextHelpId(HMENU menu);
/* a property of 32 bits (two of SetProp's) */
BOOL    WINAPI SetPropEx(HWND h, LPCSTR name, DWORD data);
DWORD   WINAPI GetPropEx(HWND h, LPCSTR name);
DWORD   WINAPI RemovePropEx(HWND h, LPCSTR name);
typedef struct tagDRAWTEXTPARAMS {
    UINT cbSize;
    int  iTabLength, iLeftMargin, iRightMargin; /* in average characters' widths */
    UINT uiLengthDrawn;
} DRAWTEXTPARAMS, *LPDRAWTEXTPARAMS;
int     WINAPI DrawTextEx(HDC dc, LPSTR s, int n, LPRECT rc, UINT fmt, LPDRAWTEXTPARAMS p);
#define IMAGE_BITMAP        0
#define IMAGE_ICON          1
#define IMAGE_CURSOR        2
#define LR_DEFAULTCOLOR     0x0000
#define LR_MONOCHROME       0x0001
#define LR_COPYRETURNORG    0x0004
#define LR_COPYDELETEORG    0x0008
#define LR_LOADFROMFILE     0x0010
#define DI_MASK             0x0001
#define DI_IMAGE            0x0002
#define DI_NORMAL           0x0003
#define DI_COMPAT           0x0004
#define DI_DEFAULTSIZE      0x0008
typedef struct tagICONINFO {
    BOOL    fIcon;
    DWORD   xHotspot, yHotspot;
    HBITMAP hbmMask, hbmColor;          /* the caller's to delete */
} ICONINFO, *LPICONINFO;
/* a bitmap, an icon or a cursor: the system's or a module's, or a file
 * (LR_LOADFROMFILE: .BMP, .ICO, .CUR) */
HANDLE  WINAPI LoadImage(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy, UINT flags);
HANDLE  WINAPI CopyImage(HANDLE h, UINT type, int cx, int cy, UINT flags);
BOOL    WINAPI DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step,
                          HBRUSH flicker, UINT flags);
BOOL    WINAPI GetIconInfo(HICON icon, LPICONINFO info);
BOOL    WINAPI LockInput(HANDLE reserved, HWND h, BOOL lock);
BOOL    WINAPI SetMessageQueue(int size);
DWORD   WINAPI GetSystemDebugState(void);
void    WINAPI EndMenu(void);
BOOL    WINAPI SetSystemMenu(HWND h, HMENU menu);
HWND    WINAPI WindowFromDC(HDC hdc);
HDC     WINAPI GetDCEx(HWND h, HRGN clip, DWORD flags);
HBRUSH  WINAPI GetControlBrush(HWND h, HDC hdc, UINT type);
void    WINAPI FillWindow(HWND parent, HWND h, HDC hdc, HBRUSH brush);
void    WINAPI PaintRect(HWND parent, HWND h, HDC hdc, HBRUSH brush, const RECT *rc);
void    WINAPI RepaintScreen(void);
HWND    WINAPI GetDesktopHwnd(void);
BOOL    WINAPI ExitWindowsExec(LPCSTR exe, LPCSTR params);
UINT    WINAPI SysErrorBox(LPCSTR text, LPCSTR caption, UINT b1, UINT b2, UINT b3);
BOOL    WINAPI DragDetect(HWND h, POINT pt);
void    WINAPI CalcChildScroll(HWND h, UINT bar);
void    WINAPI ScrollChildren(HWND h, UINT msg, WPARAM wp, LPARAM lp);

#endif /* MURM_WINUSER_H */
