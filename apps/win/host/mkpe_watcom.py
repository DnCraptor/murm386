#!/usr/bin/env python3
"""Write a Win32 program or DLL built by Open Watcom 2.0 for WINN's Win32
layer (core/pe.c ...), with the compiler's own start-up code; the sources
are here, the files as they came out of the linker (zlib, base64).

    mkpe_watcom.py NAME OUTPUT

OW32 (OW32.EXE, wcc386 -za99 -bt=nt, wlink system nt_win, ow32.rc,
    winmm.lib): KERNEL32,
    its DLL, USER32 and GDI32 as its window shows them: "kernel: ver
    C3B60004, mod 1 OW32.EXE, heap 4, virt 2, env 9 'forty two', tls 1
    1234", "time 29.8.1997 5 2:14 231D, cp 1252/437, wide 7 Watcom, cmp 1
    0", "strings: MIXED CASE, wsprintf -12|   ab|beef|Z|70000", "files:
    size 20 'line two' 'line 3', found 1, attr 20, del 1, dir 1", "paths:
    cwd C:\\, full C:\\X.TXT X.TXT", "modules: proc 1, none 0 127, dll 1
    same 1, add 5 30 300, attached 103", "dll: data 'the DLL's data', 'a
    string of the DLL's', self 1, OW32DLL.DLL, free 1", "user: 'edit me'
    2-4, list 3 'one' sel 2, combo 'green' 1, check 1", "data 5A5A, super
    1, children 5, 'a string of its own'", "create: 'OW32 - Win32'
    'Ow32Class' param 1234 inst 1", "icons 1 1, accel 1, menu 2", "posted
    7 77; mm 1 1, 306, 'An unknown error has occ'" (WINMM: timeGetTime,
    timeGetDevCaps, mciSendStringA, mciGetErrorStringA), "gdi: bitmap 16x16 8, pen 1 FF0000, text 'OW32 - Win32 on
    WINN'"; its bold title line: "font 16/700, extent 42x16, timer 4 3,
    accel 2, chars 3" (Ctrl+T twice, "abc" typed into its subclassed edit
    control); a rectangle, an ellipse, its icon, the
    system's exclamation, its bitmap stretched, the DLL's window "painted
    by the DLL", a DIB section it wrote (four colours), WinG's bitmap
    (stripes). File, Count: Ctrl+T's; File, Exit; About: a message box. Ctrl+D: its dialog (a template of its resources), the
    number typed: "dialog: 42". SPACE: Windows 2000's CreateHardLinkA,
    which WINN has not: "OW32.EXE called KERNEL32.CreateHardLinkA, which
    WINN does not have yet." "OW32 fault": "OW32 caused a Divide by Zero
    in module OW32.EXE at OW32+00002720."

OW32DLL (OW32DLL.DLL, wcc386 -bd, wlink system nt_dll, ow32dll.rc): its
    DllMain, exports by name and ordinal, data, a string resource, a window
    class of its own
"""
import base64
import sys
import zlib

PROGRAMS = {
    'OW32': (
# /* OW32: a Win32 program (Open Watcom, wcl386 -bt=nt -l=nt_win) for WINN's
#  * Win32 layer: KERNEL32 (the process, memory, heaps, files, time, strings,
#  * modules), a DLL of its own (imported and loaded), USER32 (a class of its
#  * own, controls and their messages, subclassing, timers, resources: menu,
#  * accelerators, strings, icon, cursor, a dialog), GDI32 (shapes, fonts,
#  * text). Its findings are painted in its window; Ctrl+D runs the dialog,
#  * SPACE calls a function WINN has not, "fault" on the command line divides
#  * by zero. */
# #include <windows.h>
# #include <stdio.h>
# #include <stdlib.h>
# #include <string.h>
# #include <mmsystem.h>
#
# int WINAPI Add(int a, int b);
# int WINAPI Attached(void);
# const char * WINAPI DllData(void);
# int WINAPI DllString(char *buf, int n);
# int WINAPI DllIsSelf(HINSTANCE h);
# int WINAPI DllRegister(void);
# BOOL WINAPI CreateHardLinkA(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES);   /* Windows 2000's */
#
# #define IDM_ABOUT 101
# #define IDM_COUNT 102
# #define IDM_DIALOG 103
# #define IDD_EDIT 201
# #define IDD_LIST 202
#
# static HINSTANCE inst;
# static char line[16][100];
# static int timer_msgs, timer_procs, accel_cmds, sub_chars, enum_count;
# static WNDPROC old_edit;
# static HWND edit, list, check, combo, dllwin;
# static HICON icon;
# static char title[32];
# static HCURSOR cursor;
# static void dibs(HDC dc, int x, int y);
#
# /* ---------------------------------------------------------------- KERNEL32 */
#
# static void kernel(void)
# {
#     char path[MAX_PATH], env[64];
#     HANDLE heap = GetProcessHeap();
#     char *p = HeapAlloc(heap, HEAP_ZERO_MEMORY, 100);
#     int heap_ok = p && !p[99];
#     p = HeapReAlloc(heap, 0, p, 5000);
#     heap_ok += p && HeapSize(heap, 0, p) >= 5000;
#     HeapFree(heap, 0, p);
#     char *m = malloc(70000);                    /* the C library's own heap */
#     heap_ok += m != NULL;
#     if (m) {
#         memset(m, 'x', 70000);
#         m = realloc(m, 140000);
#         heap_ok += m && m[69999] == 'x';
#         free(m);
#     }
#     BYTE *v = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#     MEMORY_BASIC_INFORMATION mbi;
#     int virt = v && !((DWORD)v & 4095) && VirtualQuery(v + 100, &mbi, sizeof(mbi)) == sizeof(mbi) &&
#                mbi.AllocationBase == v && mbi.State == MEM_COMMIT;
#     if (v) {
#         v[65535] = 1;
#         virt += VirtualFree(v, 0, MEM_RELEASE);
#     }
#     GetModuleFileNameA(NULL, path, sizeof(path));
#     const char *file = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
#     SetEnvironmentVariableA("OW32VAR", "forty two");
#     int envn = GetEnvironmentVariableA("OW32VAR", env, sizeof(env));
#     DWORD tls = TlsAlloc();
#     TlsSetValue(tls, (LPVOID)0x1234);
#     SYSTEMTIME st = { 1997, 8, 0, 29, 2, 14, 30, 0 }, st2;
#     FILETIME ft;
#     WORD dd, dt;
#     SystemTimeToFileTime(&st, &ft);
#     FileTimeToSystemTime(&ft, &st2);
#     FileTimeToDosDateTime(&ft, &dd, &dt);
#     WCHAR w[16];
#     char back[16];
#     int wn = MultiByteToWideChar(CP_ACP, 0, "Watcom", -1, w, 16);
#     WideCharToMultiByte(CP_ACP, 0, w, -1, back, sizeof(back), NULL, NULL);
#     char up[16] = "mixed Case";
#     CharUpperA(up);
#     sprintf(line[0], "kernel: ver %08lX, mod %d %s, heap %d, virt %d, env %d '%s', tls %d %lX",
#             GetVersion(), GetModuleHandleA(NULL) == inst, file, heap_ok, virt, envn, env,
#             tls < 64, (DWORD)TlsGetValue(tls));
#     char ws[64];
#     wsprintfA(ws, "%d|%5s|%-4x|%c|%lu", -12, "ab", 0xbeef, 'Z', 70000UL);
#     sprintf(line[1], "time %d.%d.%d %d %d:%02d %04X, cp %u/%u, wide %d %s, cmp %d %d",
#             st2.wDay, st2.wMonth, st2.wYear, st2.wDayOfWeek, st2.wHour, st2.wMinute, dd,
#             GetACP(), GetOEMCP(), wn, back, CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
#             "abc", -1, "ABD", -1), lstrcmpiA("Hello", "HELLO"));
#     sprintf(line[2], "strings: %s, wsprintf %s", up, ws);
# }
#
# static void files(void)
# {
#     char buf[64], cwd[MAX_PATH], full[MAX_PATH], *part;
#     DWORD n = 0, size;
#     HANDLE f = CreateFileA("OW32.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
#     WriteFile(f, "line one\r\nline two\r\n", 20, &n, NULL);
#     CloseHandle(f);
#     f = CreateFileA("OW32.TXT", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
#     size = GetFileSize(f, NULL);
#     SetFilePointer(f, 10, NULL, FILE_BEGIN);
#     memset(buf, 0, sizeof(buf));
#     ReadFile(f, buf, 8, &n, NULL);
#     CloseHandle(f);
#     FILE *c = fopen("OW32.TXT", "a");           /* the C library: CreateFile too */
#     fprintf(c, "line %d\n", 3);
#     fclose(c);
#     c = fopen("OW32.TXT", "r");
#     char l3[32] = "";
#     fgets(l3, sizeof(l3), c);
#     fgets(l3, sizeof(l3), c);
#     fgets(l3, sizeof(l3), c);
#     fclose(c);
#     l3[strcspn(l3, "\r\n")] = 0;
#     WIN32_FIND_DATAA fd;
#     int exes = 0;
#     HANDLE h = FindFirstFileA("OW32*.*", &fd);
#     if (h != INVALID_HANDLE_VALUE) {
#         do
#             exes++;
#         while (FindNextFileA(h, &fd));
#         FindClose(h);
#     }
#     DWORD attr = GetFileAttributesA("OW32.TXT");
#     int del = DeleteFileA("OW32.TXT") && GetFileAttributesA("OW32.TXT") == 0xffffffff &&
#               GetLastError() == ERROR_FILE_NOT_FOUND;
#     int dir = CreateDirectoryA("OW32DIR", NULL) &&
#               (GetFileAttributesA("OW32DIR") & FILE_ATTRIBUTE_DIRECTORY) &&
#               RemoveDirectoryA("OW32DIR");
#     GetCurrentDirectoryA(sizeof(cwd), cwd);
#     GetFullPathNameA("X.TXT", sizeof(full), full, &part);
#     sprintf(line[3], "files: size %lu '%s' '%s', found %d, attr %lX, del %d, dir %d",
#             size, buf, l3, exes, attr, del, dir);
#     sprintf(line[4], "paths: cwd %s, full %s %s", cwd, full, part);
# }
#
# static void modules(void)
# {
#     char s[64], d[64];
#     FARPROC mb = GetProcAddress(GetModuleHandleA("USER32"), "MessageBoxA");
#     FARPROC none = GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "NoSuchFunction");
#     DWORD err = GetLastError();
#     HMODULE dll = LoadLibraryA("OW32DLL.DLL");
#     int (WINAPI *add)(int, int) = (int (WINAPI *)(int, int))GetProcAddress(dll, "Add");
#     int (WINAPI *add1)(int, int) = (int (WINAPI *)(int, int))GetProcAddress(dll, (LPCSTR)1);
#     DllString(s, sizeof(s));
#     GetModuleFileNameA(dll, d, sizeof(d));
#     int r1 = Add(2, 3), r2 = add ? add(10, 20) : -1, r3 = add1 ? add1(100, 200) : -1;
#     sprintf(line[5], "modules: proc %d, none %d %lu, dll %d same %d, add %d %d %d, attached %d",
#             mb != NULL, none != NULL, err, dll != NULL, dll == GetModuleHandleA("OW32DLL"),
#             r1, r2, r3, Attached());
#     sprintf(line[6], "dll: data '%s', '%s', self %d, %s, free %d", DllData(), s, DllIsSelf(dll),
#             strrchr(d, '\\') ? strrchr(d, '\\') + 1 : d, FreeLibrary(dll));
# }
#
# /* ---------------------------------------------------------------- USER32 */
#
# static LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_CHAR)
#         sub_chars++;
#     return CallWindowProcA(old_edit, h, msg, wp, lp);
# }
#
# static VOID CALLBACK timer_proc(HWND h, UINT msg, UINT_PTR id, DWORD time)
# {
#     timer_procs++;
#     if (timer_procs == 3)
#         KillTimer(h, id);
# }
#
# static BOOL CALLBACK enum_proc(HWND h, LPARAM lp)
# {
#     enum_count += (int)lp;
#     return TRUE;
# }
#
# static void controls(HWND h)
# {
#     char text[64], lb[32], cb[32];
#     edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "edit me", WS_CHILD | WS_VISIBLE | WS_BORDER,
#                            10, 300, 150, 24, h, (HMENU)301, inst, NULL);
#     list = CreateWindowA("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
#                          170, 300, 100, 60, h, (HMENU)302, inst, NULL);
#     check = CreateWindowA("BUTTON", "check", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
#                           280, 300, 80, 20, h, (HMENU)303, inst, NULL);
#     combo = CreateWindowA("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
#                           370, 300, 100, 80, h, (HMENU)304, inst, NULL);
#     DllRegister();
#     dllwin = CreateWindowA("Ow32DllClass", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
#                            480, 300, 150, 24, h, (HMENU)305, inst, NULL);
#     old_edit = (WNDPROC)SetWindowLongA(edit, GWL_WNDPROC, (LONG)edit_proc);
#     SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)"one");
#     SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)"two");
#     SendMessageA(list, LB_INSERTSTRING, 0, (LPARAM)"zero");
#     SendMessageA(list, LB_SETCURSEL, 2, 0);
#     SendMessageA(list, LB_GETTEXT, 1, (LPARAM)lb);
#     SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"red");
#     SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"green");
#     SendMessageA(combo, CB_SETCURSEL, 1, 0);
#     SendMessageA(combo, CB_GETLBTEXT, 1, (LPARAM)cb);
#     SendMessageA(check, BM_SETCHECK, BST_CHECKED, 0);
#     SendMessageA(edit, EM_SETSEL, 2, 4);
#     DWORD s0 = 0, s1 = 0;
#     SendMessageA(edit, EM_GETSEL, (WPARAM)&s0, (LPARAM)&s1);
#     GetWindowTextA(edit, text, sizeof(text));
#     SetWindowLongA(h, GWL_USERDATA, 0x5a5a);
#     WNDCLASSA wc;
#     int sup = GetClassInfoA(NULL, "BUTTON", &wc) && wc.lpfnWndProc != NULL;
#     EnumChildWindows(h, enum_proc, 1);
#     char str[64];
#     LoadStringA(inst, 2, str, sizeof(str));
#     sprintf(line[7], "user: '%s' %lu-%lu, list %ld '%s' sel %ld, combo '%s' %ld, check %ld",
#             text, s0, s1, SendMessageA(list, LB_GETCOUNT, 0, 0), lb,
#             SendMessageA(list, LB_GETCURSEL, 0, 0), cb, SendMessageA(combo, CB_GETCURSEL, 0, 0),
#             SendMessageA(check, BM_GETCHECK, 0, 0));
#     sprintf(line[8], "data %lX, super %d, children %d, '%s'", GetWindowLongA(h, GWL_USERDATA),
#             sup, enum_count, str);
# }
#
# static BOOL CALLBACK dialog_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
# {
#     switch (msg) {
#     case WM_INITDIALOG:
#         SetDlgItemTextA(h, IDD_EDIT, (const char *)lp);
#         SendDlgItemMessageA(h, IDD_LIST, LB_ADDSTRING, 0, (LPARAM)"from the dialog");
#         return TRUE;
#     case WM_COMMAND:
#         if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
#             BOOL ok;
#             UINT n = GetDlgItemInt(h, IDD_EDIT, &ok, FALSE);
#             EndDialog(h, LOWORD(wp) == IDOK && ok ? (int)n : -1);
#             return TRUE;
#         }
#         break;
#     }
#     return FALSE;
# }
#
# static void paint(HWND h)
# {
#     PAINTSTRUCT ps;
#     HDC dc = BeginPaint(h, &ps);
#     RECT r;
#     GetClientRect(h, &r);
#     HBRUSH br = CreateSolidBrush(RGB(0, 128, 255));
#     HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
#     HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
#     Rectangle(dc, r.right - 100, 252, r.right - 10, 292);
#     Ellipse(dc, r.right - 200, 252, r.right - 110, 292);
#     SelectObject(dc, ob);
#     SelectObject(dc, op);
#     DeleteObject(br);
#     DeleteObject(pen);
#     LOGFONTA lf;
#     memset(&lf, 0, sizeof(lf));
#     lf.lfHeight = -16;
#     lf.lfWeight = FW_BOLD;
#     strcpy(lf.lfFaceName, "System");
#     HFONT font = CreateFontIndirectA(&lf);
#     HGDIOBJ of = SelectObject(dc, font);
#     TEXTMETRICA tm;
#     SIZE sz;
#     GetTextMetricsA(dc, &tm);
#     GetTextExtentPoint32A(dc, "Win32", 5, &sz);
#     char t[96];
#     SetTextColor(dc, RGB(128, 0, 0));
#     SetBkMode(dc, TRANSPARENT);
#     sprintf(t, "font %ld/%ld, extent %ldx%ld, timer %d %d, accel %d, chars %d",
#             tm.tmHeight, tm.tmWeight, sz.cx, sz.cy, timer_msgs, timer_procs, accel_cmds, sub_chars);
#     TextOutA(dc, 10, 4, t, (int)strlen(t));
#     SelectObject(dc, of);
#     DeleteObject(font);
#     SetTextColor(dc, RGB(0, 0, 0));
#     for (int i = 0; i < 16; i++)
#         if (line[i][0])
#             TextOutA(dc, 6, 24 + 16 * i, line[i], (int)strlen(line[i]));
#     HBITMAP bmp = LoadBitmapA(inst, "OWBMP");
#     BITMAP bm;
#     memset(&bm, 0, sizeof(bm));
#     GetObjectA(bmp, sizeof(bm), &bm);
#     HDC mem = CreateCompatibleDC(dc);
#     HGDIOBJ old = SelectObject(mem, bmp);
#     StretchBlt(dc, r.right - 340, 256, 32, 32, mem, 0, 0, 16, 16, SRCCOPY);
#     SelectObject(mem, old);
#     DeleteDC(mem);
#     DeleteObject(bmp);
#     LOGPEN lp;
#     HPEN p3 = CreatePen(PS_DASH, 1, RGB(0, 0, 255));
#     GetObjectA(p3, sizeof(lp), &lp);
#     DeleteObject(p3);
#     sprintf(t, "gdi: bitmap %ldx%ld %d, pen %u %lX, text '%s'", bm.bmWidth, bm.bmHeight,
#             bm.bmBitsPixel, lp.lopnStyle, lp.lopnColor, title);
#     TextOutA(dc, 6, 24 + 16 * 13, t, (int)strlen(t));
#     DrawIcon(dc, r.right - 250, 256, icon);
#     dibs(dc, r.right - 420, 256);
#     DrawIcon(dc, r.right - 290, 256, LoadIconA(NULL, IDI_EXCLAMATION));
#     EndPaint(h, &ps);
# }
#
# int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show);
#
# /* a DIB section written by the program, then WinG's bitmap (WING32 by
#  * GetProcAddress): blitted at x, y and x + 40, y */
# static void dibs(HDC dc, int x, int y)
# {
#     struct { BITMAPINFOHEADER h; RGBQUAD c[256]; } bi;
#     memset(&bi, 0, sizeof(bi));
#     bi.h.biSize = sizeof(bi.h);
#     bi.h.biWidth = 32;
#     bi.h.biHeight = -32;
#     bi.h.biPlanes = 1;
#     bi.h.biBitCount = 8;
#     for (int i = 0; i < 256; i++) {
#         bi.c[i].rgbRed = (BYTE)(i & 0xe0);
#         bi.c[i].rgbGreen = (BYTE)((i & 0x1c) << 3);
#         bi.c[i].rgbBlue = (BYTE)((i & 3) << 6);
#     }
#     BYTE *bits = NULL;
#     HBITMAP b = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
#     HDC mem = CreateCompatibleDC(dc);
#     if (b && bits) {
#         for (int r = 0; r < 32; r++)
#             for (int c = 0; c < 32; c++)
#                 bits[r * 32 + c] = (BYTE)(r < 16 ? (c < 16 ? 0xe0 : 0x1c) : (c < 16 ? 0x03 : 0xfc));
#         HGDIOBJ old = SelectObject(mem, b);
#         BitBlt(dc, x, y, 32, 32, mem, 0, 0, SRCCOPY);
#         SelectObject(mem, old);
#         DeleteObject(b);
#     }
#     DeleteDC(mem);
#     HMODULE wing = LoadLibraryA("WING32.DLL");
#     HDC (WINAPI *create_dc)(void) = (HDC (WINAPI *)(void))GetProcAddress(wing, "WinGCreateDC");
#     BOOL (WINAPI *recommend)(BITMAPINFO *) =
#         (BOOL (WINAPI *)(BITMAPINFO *))GetProcAddress(wing, "WinGRecommendDIBFormat");
#     HBITMAP (WINAPI *create_bmp)(HDC, BITMAPINFO *, void **) =
#         (HBITMAP (WINAPI *)(HDC, BITMAPINFO *, void **))GetProcAddress(wing, "WinGCreateBitmap");
#     BOOL (WINAPI *blt)(HDC, int, int, int, int, HDC, int, int) =
#         (BOOL (WINAPI *)(HDC, int, int, int, int, HDC, int, int))GetProcAddress(wing, "WinGBitBlt");
#     if (create_dc && recommend && create_bmp && blt) {
#         HDC wdc = create_dc();
#         recommend((BITMAPINFO *)&bi);
#         bi.h.biWidth = 32;
#         bi.h.biHeight = -32;
#         BYTE *wb = NULL;
#         HBITMAP wbmp = create_bmp(wdc, (BITMAPINFO *)&bi, (void **)&wb);
#         if (wbmp && wb) {
#             for (int i = 0; i < 32 * 32; i++)
#                 wb[i] = (BYTE)((i / 32 + i % 32) & 8 ? 0xff : 0x00);
#             HGDIOBJ old = SelectObject(wdc, wbmp);
#             blt(dc, x + 40, y, 32, 32, wdc, 0, 0);
#             SelectObject(wdc, old);
#             DeleteObject(wbmp);
#         }
#         DeleteDC(wdc);
#     }
# }
#
# static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
# {
#     switch (msg) {
#     case WM_CREATE: {
#         CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
#         sprintf(line[9], "create: '%s' '%s' param %lX inst %d",
#                 cs->lpszName, cs->lpszClass, (DWORD)cs->lpCreateParams, cs->hInstance == inst);
#         controls(h);
#         SendMessageA(h, WM_SETTEXT, 0, (LPARAM)"OW32 - Win32 on WINN");
#         GetWindowTextA(h, title, sizeof(title));
#         PostMessageA(h, WM_USER + 1, 7, 0x77);
#         SetTimer(h, 1, 100, NULL);
#         SetTimer(h, 2, 150, timer_proc);
#         return 0;
#     }
#     case WM_USER + 1:
#         {
#             DWORD t0 = timeGetTime();
#             TIMECAPS tc;
#             char err[64] = "";
#             int caps = timeGetDevCaps(&tc, sizeof(tc)) == 0 && tc.wPeriodMin == 1;
#             mciGetErrorStringA(MCIERR_INVALID_DEVICE_ID, err, sizeof(err));
#             char ret[32] = "";
#             DWORD mr = mciSendStringA("open nosuchdevice", ret, sizeof(ret), NULL);
#             sprintf(line[12], "posted %u %lX; mm %d %d, %lu, '%.24s'", wp, lp, t0 != 0, caps,
#                     mr, err);
#         }
#         return 0;
#     case WM_GETMINMAXINFO:
#         ((MINMAXINFO *)lp)->ptMinTrackSize.x = 200;
#         return 0;
#     case WM_TIMER:
#         if (wp == 1 && ++timer_msgs == 4) {
#             KillTimer(h, 1);
#             InvalidateRect(h, NULL, TRUE);
#         }
#         return 0;
#     case WM_COMMAND:
#         if (LOWORD(wp) == IDM_COUNT) {
#             accel_cmds++;
#             InvalidateRect(h, NULL, TRUE);
#         } else if (LOWORD(wp) == 2) {           /* File, Exit */
#             DestroyWindow(h);
#         } else if (LOWORD(wp) == IDM_ABOUT) {
#             MessageBoxA(h, "OW32: a Win32 program on WINN", "About OW32", MB_OK | MB_ICONINFORMATION);
#         } else if (LOWORD(wp) == IDM_DIALOG) {
#             int r = DialogBoxParamA(inst, "OWDIALOG", h, dialog_proc, (LPARAM)"12");
#             sprintf(line[11], "dialog: %d", r);
#             InvalidateRect(h, NULL, TRUE);
#         } else if (LOWORD(wp) == 304 && HIWORD(wp) == CBN_SELCHANGE) {
#             sprintf(line[14], "combo changed %ld", SendMessageA(combo, CB_GETCURSEL, 0, 0));
#             InvalidateRect(h, NULL, TRUE);
#         }
#         return 0;
#     case WM_CHAR:
#         if (wp == ' ') {                        /* a function WINN has not */
#             CreateHardLinkA("A", "B", NULL);
#         }
#         return 0;
#     case WM_PAINT:
#         paint(h);
#         return 0;
#     case WM_DESTROY:
#         PostQuitMessage(5);
#         return 0;
#     }
#     return DefWindowProcA(h, msg, wp, lp);
# }
#
# int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
# {
#     inst = hi;
#     if (!strcmp(cmd, "fault")) {
#         volatile int zero = 0;
#         return 10 / zero;
#     }
#     kernel();
#     files();
#     modules();
#     icon = LoadIconA(hi, "OWICON");
#     cursor = LoadCursorA(hi, MAKEINTRESOURCE(1));
#     WNDCLASSEXA wc;
#     memset(&wc, 0, sizeof(wc));
#     wc.cbSize = sizeof(wc);
#     wc.lpfnWndProc = wnd_proc;
#     wc.cbWndExtra = 8;
#     wc.hInstance = hi;
#     wc.hIcon = icon;
#     wc.hCursor = cursor;
#     wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
#     wc.lpszMenuName = "OWMENU";
#     wc.lpszClassName = "Ow32Class";
#     if (!RegisterClassExA(&wc))
#         return 1;
#     HWND h = CreateWindowExA(0, "Ow32Class", "OW32 - Win32", WS_OVERLAPPEDWINDOW,
#                              0, 0, 640, 420, NULL, NULL, hi, (LPVOID)0x1234);
#     sprintf(line[10], "icons %d %d, accel %d, menu %d", icon != NULL, cursor != NULL,
#             LoadAcceleratorsA(hi, "OWACCEL") != NULL, GetMenuItemCount(GetMenu(h)));
#     ShowWindow(h, SW_SHOWMAXIMIZED);
#     UpdateWindow(h);
#     HACCEL acc = LoadAcceleratorsA(hi, "OWACCEL");
#     MSG m;
#     while (GetMessageA(&m, NULL, 0, 0)) {
#         if (!TranslateAcceleratorA(h, acc, &m)) {
#             TranslateMessage(&m);
#             DispatchMessageA(&m);
#         }
#     }
#     return (int)m.wParam;
# }
#
# ---- ow32.rc
# #include <windows.h>
#
# OWICON ICON ow32.ico
# 1 CURSOR ow32.cur
#
# OWMENU MENU
# BEGIN
#     POPUP "&File"
#     BEGIN
#         MENUITEM "&Count\tCtrl+T", 102
#         MENUITEM "&Dialog...\tCtrl+D", 103
#         MENUITEM SEPARATOR
#         MENUITEM "E&xit", 2
#     END
#     MENUITEM "&About", 101
# END
#
# OWACCEL ACCELERATORS
# BEGIN
#     "^T", 102
#     "^D", 103
# END
#
# STRINGTABLE
# BEGIN
#     2 "a string of its own"
# END
#
# OWDIALOG DIALOG 20, 20, 160, 80
# STYLE DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
# CAPTION "OW32 Dialog"
# FONT 8, "MS Sans Serif"
# BEGIN
#     LTEXT "Number:", -1, 6, 8, 40, 10
#     EDITTEXT 201, 50, 6, 50, 12
#     LISTBOX 202, 6, 24, 94, 40
#     DEFPUSHBUTTON "OK", IDOK, 108, 6, 46, 14
#     PUSHBUTTON "Cancel", IDCANCEL, 108, 24, 46, 14
# END
# OWBMP BITMAP ow32.bmp
#
# ---- mkres.py (ow32.ico, ow32.cur, ow32.bmp)
# # ow32.ico (a yellow diamond, red edged) and ow32.cur (an arrow-ish
# # triangle, its hot spot at 3,5): 32 x 32
# import struct
# pal = [(0,0,0),(0,0,128),(0,128,0),(0,128,128),(128,0,0),(128,0,128),(128,128,0),
#        (192,192,192),(128,128,128),(0,0,255),(0,255,0),(0,255,255),(255,0,0),
#        (255,0,255),(255,255,0),(255,255,255)]
# def dib(bits, pal_, pix, inside):
#     n = len(pal_)
#     stride = (32 * bits + 31) // 32 * 4
#     bih = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, bits, 0, stride * 32 + 128, 0, 0, 0, 0)
#     p = b''.join(bytes([b, g, r, 0]) for r, g, b in pal_)
#     xor = bytearray(stride * 32)
#     andm = bytearray(128)
#     for y in range(32):
#         for x in range(32):
#             yy = 31 - y
#             if not inside(x, yy):
#                 andm[y * 4 + x // 8] |= 0x80 >> (x & 7)
#                 c = 0
#             else:
#                 c = pix(x, yy)
#             if bits == 4:
#                 xor[y * stride + x // 2] |= c if x & 1 else c << 4
#             else:
#                 if c:
#                     xor[y * stride + x // 8] |= 0x80 >> (x & 7)
#     return bih + p + bytes(xor) + bytes(andm)
# ico = dib(4, pal, lambda x, y: 12 if abs(x - 15.5) + abs(y - 15.5) > 12 else 14,
#           lambda x, y: abs(x - 15.5) + abs(y - 15.5) <= 15)
# open('ow32.ico', 'wb').write(struct.pack('<HHH', 0, 1, 1) +
#      struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(ico), 22) + ico)
# cur = dib(1, [(0, 0, 0), (255, 255, 255)], lambda x, y: 1 if x > 4 and x < y - 2 else 0,
#           lambda x, y: x <= y and y < 24 and x < 16)
# open('ow32.cur', 'wb').write(struct.pack('<HHH', 0, 2, 1) +
#      struct.pack('<BBBBHHII', 32, 32, 2, 0, 3, 5, len(cur), 22) + cur)
# # ow32.bmp: 16 x 16, 16 colours, quarters red, green, blue, yellow
# w = h = 16
# bih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 4, 0, 8 * h, 0, 0, 0, 0)
# p = b''.join(bytes([b, g, r, 0]) for r, g, b in pal)
# rows = b''
# for y in range(h):
#     yy = h - 1 - y
#     row = bytearray(8)
#     for x in range(w):
#         c = (12 if x < 8 else 10) if yy < 8 else (9 if x < 8 else 14)
#         row[x // 2] |= c if x & 1 else c << 4
#     rows += bytes(row)
# data = bih + p + rows
# open('ow32.bmp', 'wb').write(b'BM' + struct.pack('<IHHI', 14 + len(data), 0, 0, 14 + 40 + 64) + data)
#
# wcc386 -q -za99 -bt=nt ow32.c; wrc -q -r -bt=nt ow32.rc
# wlink system nt_win name OW32.EXE file ow32.o library ow32dll.lib library winmm.lib option resource=ow32.res
        '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'
),
    'OW32DLL': (
# /* OW32DLL.DLL: a Win32 DLL of the test's: its entry counts the processes
#  * attached, its functions (by name, by ordinal), its data, its string
#  * resource, a window class whose procedure is the DLL's */
# #include <windows.h>
# #include <string.h>
#
# static HINSTANCE self;
# static int attached, calls;
# static char data[32] = "the DLL's data";
#
# BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
# {
#     if (reason == DLL_PROCESS_ATTACH) {
#         self = h;
#         attached++;
#     }
#     return TRUE;
# }
#
# int WINAPI Add(int a, int b) { calls++; return a + b; }
# int WINAPI Attached(void) { return attached * 100 + calls; }
# const char * WINAPI DllData(void) { return data; }
# int WINAPI DllString(char *buf, int n) { return LoadStringA(self, 1, buf, n); }
# int WINAPI DllIsSelf(HINSTANCE h) { return h == self && GetModuleHandleA("OW32DLL") == self; }
#
# static LRESULT CALLBACK dllproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_PAINT) {
#         PAINTSTRUCT ps;
#         HDC dc = BeginPaint(h, &ps);
#         TextOutA(dc, 2, 2, "painted by the DLL", 18);
#         EndPaint(h, &ps);
#         return 0;
#     }
#     return DefWindowProcA(h, msg, wp, lp);
# }
#
# int WINAPI DllRegister(void)
# {
#     WNDCLASSA wc;
#     memset(&wc, 0, sizeof(wc));
#     wc.lpfnWndProc = dllproc;
#     wc.hInstance = self;
#     wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
#     wc.lpszClassName = "Ow32DllClass";
#     return RegisterClassA(&wc) != 0;
# }
#
# ---- ow32dll.rc
# STRINGTABLE
# BEGIN
#     1 "a string of the DLL's"
# END
#
# wcc386 -q -bt=nt -bd -s ow32dll.c; wrc -q -r -bt=nt ow32dll.rc
# system nt_dll initinstance terminstance
# name OW32DLL.DLL
# file ow32dll.o
# option resource=ow32dll.res
# option implib=ow32dll.lib
# option quiet
# export Add.1='_Add@8'
# export Attached.2='_Attached@0'
# export DllData.3='_DllData@0'
# export DllString.4='_DllString@8'
# export DllIsSelf.5='_DllIsSelf@4'
# export DllRegister.6='_DllRegister@0'
        'eNrtfQtYU1e28ElIICAYVKKoWGNLW62PJgFCQhIIT6X1wUtAq1YqQbAINJyIdmwbG6gcjmltx3ac1rmjxXZsxzvj'
        'TB+XYh9gLKDttEhta7UPta+D6QO1VURr/rX2PgkPO71z7zf/d+//f3P07Oy99t5rv9Zee+219j4sWOpkJAzDyOD1'
        '+RimhaGPlfnPHye8o6e2jmZeDP3btBbJ/L9NKyivqFXD/xJ1UUVVaXVdrXphgbp0Q1XJ2opV6sqKqrvBucteYt8Q'
        'ERZLceRkMsx8STBz7Mcf1/jxnmQenDZKIh3LLIRAGIUVjwEnEt6VYu3QL2Wg7qNIvMyfuXswIGWCBiurFvP7f3Yy'
        'zDMMxbdEDr9tFN81zz6GiQ9m/ulP6uKCRfh7/wKxQguHNiJQ5ZVzKkpL2BKxjth2rEve8HTQG20Zc/MWLc5hmNeu'
        'B0CNiKv42nRzbBQfI/YBafOya9JZ59htldWrBvuAUcC74pp0aXPstXZ/skAfll2Lj/nX87/yedv701Z4AmHXxliF'
        'xKHkM2JlzQqnlfHJGXBbkEUcCIeQDEIYGZQRqzigYO4m0aVBBO5pmVUDLkQrcviCWEXeGgkfgUhyfaqTJVYG0mNW'
        'kxxhjsnlMkjtU1VCjAhS0HJkjLYNnMIi1zdWyJDj6lc65nAnc/iNsTlFPlU3ZFgztjwccq+RrpHm+FRqgEA8xH0M'
        'Pm2bdzwUX5zHz48tzs3h2djiQp/qGMS4DlrvXHEgksl3fTN9/3QoStvDnRSM4OmEej0VaWV2YT24jNhIAMQE04jY'
        'qTUEvU8lAI6yhjbl423Kl9tcB6ff4WFyuY9yms61n1Y0fUAc4Smcd0s8vfnQlCDAuKjGVqUuKmFXVa9Vp9+aPnNm'
        'nE6d56iaXVCx1qau3VDL2tbOUadX12ywV6wuZ9XTV81QD8tSXcXaK+5ysNX2WrVOo9HN1ml0+jnqnGo7W1FdVTs0'
        'a/oMdf6Gu0pqbbPU2VWr5qi1RoNhNuaZMzjeow6xtxoi9wSxoY2R1vbDimzH+dz2XqlwHcQtcZ0KYsc0Rmaz4Y01'
        'kmw2GOIba6SereSBnKv3TGUVXKTbKst2/IDZ2KRsdgYXyeXIsrkcBZcTzkYbctRcDkCiABLN5cS4rWrHJ25rn9sa'
        'SRLSdBAipakoNjZC/JWTX89gfd8Za2UWjrMyGnjnRFmZ/MKixTCiUUAL0XxlbIzLF1R3F9fvi23YCSkXFxUKTdDx'
        'rbCSMZynBRmrcAkALst8GERG2VAEAeFrYI6tShz/NuEEAAjZCY/CUrNrJyTT9ggvhcJyGBGOxKBCEHds+Z0r7oAZ'
        'oO0XJkK6hjb2OyxsigTLCcTuwhzWZnRd/ZL7x3N9Ah/BMPVt7CjtiUAqWtxYCSn/sgKxOYbGc+e5AzTNdVJS1OdC'
        'Qug1qVqwAcLb6KwOC7SQvR6q5VOhH9KrSCuFi9jUITmxd4TH0FkLtfOpGmto1y0ivQYE3hDu8/lIJwr7EebPiYlu'
        'oV1L6rcidHgkLbj3qasoSpAH5m84MoNwnI3hucKOyz4fpB3kNwbGIfH4Ij1ulCaYrfm5eYWNHa+/bgemsxcc7mzj'
        'OWPHuklOg5Qd1bReYhiwy7j2xgFXW7AFE9m/2nQJfx3yFUuX3OFxp0qF56DuHRKp9w13hprJzy1s9DR+8PoHiPB1'
        'ivD4cIR1AYQfiAjxl41KukdSK0dEnYg1H7B6z66AMobxz9sz8xZmzo/TzcmYP5/JsttsmVXrKuzVVWttVWw+zNyq'
        '1bWpzFwbey3456GpgJN2hRsHCAZnN/So/DsEAOc95BkaCZDcwiKuk/u0OTpAqkLoaCSs5p2PInfdiKzxwNM/QUS7'
        'EFnW7EP6bivbhb+bTuMwNUvuQVD7gGLnTxQImJvlBNjY3ai6CsDmUAjuQhivQm/7SUVod3ME+JwWzCRTup6EXC02'
        'lZXJ0faUW1VYm0sB/g68kyUVAWIO8fU0Y0y5DEioPAbqvYbZr5BiU0shTat6E5bcKe+CNBgrLIH2dMqPQhCjfKoc'
        'SCV8AUDuQFN0cyQkcV6+nr1Z29MUeeHBIzdaGSmrGxqYZfX2tfwKatRbAmNoTYpmRzkNDBtsdRqud/yI3iNW7zv1'
        'Pexhq7e1+SCpdEEJzh9lPYo2wnNKLKuMj26OgsLKXFDaNOhDXiq8rCTzM4GG9tBQouuA1PutO0daZprEKstcSQwb'
        'CiDwXO84S4LdmOIt4n0TvX9tfYx0WDfOWp8jlEyuO1cs8dA+mu6yMoVDu2hWPaTu65S/DekxUtBHYBcdgyBGBbID'
        'fcB8Klq8CzsJ2MF0PgJb0HCIVXJd2hPCVGzYFAS5LDgkjEO1/E6cRsABgzBOhRm9h8oXQPXIoF47nKE8yVrk69F+'
        'y81GL0URmM9AoB3ch0L6JAYGmuYHjjkArM/6EzCZk6S/kciFkzFQZJ9QAT8t07DjvwdmQbkTZVmfQfjOFQHk7oLr'
        'Ga5dmAiJXBacAAx7s8uylTREqT2Ry03ZSZBbsc8ou/epMNp7WoiPGVwCjpFi6nFmDM8ET75tbUlNebXdpnZUgUB8'
        't61UfdcGdR1M2tXq6roqm51ZXFVyV6VNzVarSyohRQkLy3ogExG2h07ZkfMX+d2ubW5cL6z3qe8+Fu2ukWzyxD8E'
        'nZSA4Hyfyok9doywt9Zoxr9a3RBDebVD2YrhlrSJQOAPA5BXbYd8abt2gNt+Wio8CbBmDIxIe1cgLUmaxi3beoUj'
        '4eFlDq0w34b11V5wWbBuzP3R+7Gm+Vk+lQHFK0+0iVR647c0J+G/NP12kn6m9gQpjI8KynN5ZDQbhWQEXckSHoaV'
        'wZSAaTd+yE8hlT7hiEBktBZ8D6y1Qjmkot1BWqPG1mybDLEzF3O1ChhJQLqcJODbWgq2QOyzAa5Kw8ESf9h1Utl+'
        'UiZnEeocET6J6VmoRG/ZYH4S7hgMl0JKb7bYSXk0PLS8DIyfMRiP4d6HBuPrAV8glj+B/bWY8/hUxTgCHaZ0BZvk'
        '+pWMcUxviYecwsdI7BgeLbTD9OlMRw7BcEE0FgU96AWfyoy5oS/Sw5cPHcS8Xy7P9atwZt14KFPZsAuWcL42fCFX'
        'G66sb4YAxUPWZ59qJancrpwawgv2kSkNvCDKuT6fcUyABDsG6Uf4aBJSqrdXiJyMnuGxuXmEi4Rz3cK0KRDdvXQJ'
        'lS14lRqWF8E8kaF85lM+AgEd1jwJ14iNhhzfx4g5BvkBpAxzrs+TKOvTQCgi9eWnYD1dl32sChwQ3m8k0iGBYhV8'
        'qj2kARfkDC5o7HFxTas9gnnedl2W1nXS1P6U3q+g1XvB2yknYHiWEm7LH8rNc1kIzCHXtgFQmAyRXDtU6xx3mHfi'
        'Oi70TyLCXDjXJSyAWJIul4/AbLBTerFE5OAApfON85CedvX7RClH7HNA2sfvO4edRRFPnYw8dhimhgvs2w0n2C7o'
        '9ekQvGZGU2byBFSDQnPz/GUpGyaI7RqSnIz/Jguya01MQOoS9MBBeSJ9dMoNQIiItBkpkn8QK9YpR+bKXAfjeQXG'
        'kycsNp9PwJIKaT075QWYZDL6isEXSXzLwHdyEvpWgq+P+FjwrSSx68FXQ3zbwLeV+LYjlhj07QDfPpKjFEUHElsO'
        'PivxVWILiK8GiZj4NoLPSXxOLI34/C2lHQfdmldIWEgrcgigZIh2NzEBdrKVg7U9ksKNhx1nyXSjQeHsgM8nVE6i'
        'whkCaiaJ/IQmIKxD9CKXaK2ZGAhjX74+GOZU24i7nbgEklBK3HLiVhIXWybcjqXeBBMYWuARNzhDRFiiscsQNXTz'
        'UUM3n2roiBqv0m4rKQVvldpRa4OFLymMCQsrKLcN26KCqg/2tHZHFYtbWlHBpy6tttWqq6pZda2jpgY2q+oSli1Z'
        'VQ4yrnotroxseUmVurrKpq6xV6+y1daSBfTnawJJ2UF85SXrbJCy1lZTYsfFFpdYdW1NySqbuqzarrZBIWJZsFaL'
        'yOeEMYP7C04Uq3a6rUQiQZGKQnY/TBY/ImMxauj2PY8Ax5Gjywh5QLmtmIdAhXQMav2JhPiJOPUIhnIEIOIvAM8+'
        'ydJhWwbPCP0L1qfdJN8Do1IXumtPgCUhLXJdvGofCu4npQ09jliBmUiW/BOOm1pQGyicmogSIdJqICP3HncAVshn'
        'oxjGe5RAjV21Ku4ACAKS49ZOKbIq1wGZsct+jpuCuLmIPYSxDcMyYn3gp2AE10FrRUPanoYT6yZo2/gIxCJpdxl9'
        '7I1prjaZ8bjdazxeO4WbMohYRNBNJhGn9X7GdR/pl3RbaRqhH9ca4t2lAQ5sPGaP4bqh0YAmdih+FaBP8/ZC+8oI'
        'F8Ec3vc5LRT5aCu2zNu0FZaTXaTPTks7ZaS5S4cxsCHxvEy61APyKUijH7j6peytsGV3TMtb86NPtRN1UgR/7XUk'
        'OXeci98KSxbUHzj3F3lrLng/ylvzg/fofiyCRhD+D/1Fu2mN+M+nmoVrRjuR1SbzqpYalLXID/ce7R7uGJFVVC9i'
        '1K69tHbC6bG4CCKsGUFc+FZ3mqT5xUCn0lwCj+UfgMUw7+o13Not9qjwBIgKLRcgpRCDPP9H9I1F3w8Bxi/y99w8'
        'WFzW+4X7XbAOsDsA+Y8/EeR0PSoiXQLNmeey7CVKB6P2hMuCVWPun6K9QJrAm4OLXB2ydLrumKZg7MYvSZTwBylh'
        'gui/c4W43/i1FPcbiNj73BD6a8ZBGLK+w/iJsyWEkqtHeOUK9oBJji1dJxdum4CI9gToefiMGzn/YHuymHuT+4D7'
        'jO6VcZNsYZx2VQvutwS3SpSyxy3HDVV+IchW5Shekt0ZJslZTBkG9NYNRYVrfPlrJGskPtUW7MI3gVT3A2Np+Nah'
        '0raRPAB5jih+aKivrDPj/qtQUCxQNAH9DH84INwEse5iCWS+AjMbxQlHMNdFxn8mMoWuQSYj8T9B+EiuecLChz+G'
        'wCMCiskz72ceGr+dPI0/82Cs5H9aoU077P0JpMMkoPQSDo2jXcbKhasT/J1VZvH52OvKLE5fbTD4mbqxoNK94KQa'
        'g2AiFXm0bR6CDsdfRtkyLrJ8xO5HcROtrF8NqYwf1Zl4iaTL6KmN4w5DcWtgj8aRJKaI6UCStWN3zULFSW6Usbs2'
        'mFNhALbBfobdxWfLYEMeDngU/BjJYeN79hZ+niIAIqjtz/AEF0DNQ6CPgEhJckcCMJwClQ+ugznN36YYCb4TwRGx'
        'FMuEoTGZGJMNGRy9gxULyAc2WP5rWbtjFeqbYWVVa9ZrxEe9qgTkglI1rOSOqlKbvayyuk4NTgmLy3xNdUUVq7at'
        'X2WrwaywBmdmFFsGc4cxqVWAm7WtttnVpRXrKkpRxFDfa7NXq+tKatW2qlXVIFTY7FgCyA+lpXaUEwYRAMZ/rHrq'
        'UltVtX1tSaW6usZmL6kqHVHJsGGVTMsZUkn1P9wD1ev+8w7IB7HkbnWpY22Nenp+flJmfs6Mf7gNtSTv3yllWAvm'
        '5quHdXNmWvGwFqWOxPXP7m0Yy1qM94/mL9R1Nq2Krao07B/u6YqqdSWVFaV0MEnKXxrO7P/ecFZU2daXrGLVUJTD'
        '9osF5OcM6+1fLsBuK4MurloFhay1gSC8Qc1kpg4fnvyRw5c1f8Tw1dihhyth3pQOK4kM43rbKgf7C2OYOQy7mska'
        'EU4fEc4YEcYdAaBZnJ+ZJ2q9QY+dChVYZ6M2f6bOXsGyNuyaYU2fQzYOYqOB1ipLiTh/lw16ICN7eAuh+yuhfTBh'
        '/+vt8w+NuqzCVllK9XVDu/O/OOH+HmMbRgOIEbYaq+0la4dNpECaoQVAcTAKw1s81H9ts4AeS8NWlVRhf60Cg2BF'
        'lcOGTU0fTjiivq3cMAtXmEY0hZ6ApWt0wwm/0gMZe3n6rOEa3JO+HtxVDE0zKG+h+vYz7SG/wpxxDLhrQpsmNUbX'
        'n3DEwkISw/U3+Njx3AHXaWV202NMKopQssZcCRR8BuUZ52UNK8+ypnkPO++VrHf8yJ3FwGt+fR7Y8zZ9g5sv/tXY'
        'KPxJ5B+QOS3//iiKlaO1bZsOYiQamGSwE0OwRIiG9QrQnxZSYGVHCfVYhyyW4YMtYMZuU7p+B1KZxU2836N3Cnob'
        'LoK39fFZaMnlV4QLsYBjDeOuj0X0OW4wWQvtSuoLz1nzA5WBc/Y752BXduGG8AjJiyUJX1+9JnP9tZlpPsxwMUQK'
        'KgK2stU1k5T/ezxQIaCKrfWLGRSyGyGPIOTV2RSyFyH3IWTtdArZg5C7EVInpsEDHMIShDTQlv1+B0LmAuRiiISW'
        'OVYs4UWMmY1p08RaHETIZIScuoVC8PSPEIqQZjFNG7EfoqJ+jFjCqwj5EiG/FtN0IeS9n7BMGZY5r3WamHY7xryC'
        'ac+Kabch5A8IkYhp8OyB8DhCjoo13YIQ10/X9LF69C/2MbZXWf8wZG6tIphelxC7J/8GaagOEJZd7PSBYuvf0ETs'
        'JG0GK8hEiNiPtn7uHNfXPjD14mXYbY9uPTdLjL905ZqaPBHxCzUhhbChWa5OmevqvPt5IkwOpWPlQx8BxPJroMva'
        'qZbHCHm+AJALc6dJlfU8NsAFDejNg4opH2rGtD9honpsh+UJ4v0zereRrDhorfcBjfROgwzrii2PANhxR+tZaGDv'
        'aABZ5CQddnRr3kz/DLgemtW6XJwQ0cI4CLruj2Ict7VqxZb3foBJfoBQbyf63sAyXkPfbET9Avqux4o+hz41xv4e'
        'fVsw9nH05fsxbcHQC1B2730EJ+aqRV8WpeTrhF9fRoqCUO8duJ/rb90Mfj2PtqKTvcVXB/WT5XRO7iZ7LkcQ7Epz'
        'aIjsT6+LszJPwfs0vG3w7oV3M7wvw0v4Y6HrGwW/DA4QsLExF9JlwcoGBzKiIHmtz5fi6g9Wbq6AgYLzBN06mPQP'
        'QlyHfC9hO6dGE6XzmUjKdCKcFoTDmYInURt9knInJodbFivj2FiwQ7QQZTFqFRRrfNSiBBplh0ILZ0cUK+6gem7q'
        'vZA1TcqGtDYBwPtIK5KAt7HVhT+bWtFY0FuHBvl69K1FXwP6VqHcnjW9jFe0bsJTV5sufTTgCGttxLi5EIeAswDA'
        'KdGrFwH9AHgEATeKgB/9gCgANCmcl044ljbNkzhPaZyXIh3yVjdSJiNRNswDT5NVEqsxa5QNJuy0hepNp9CEzGfF'
        'tp+S3ojesvbeCAqUHHpP2AN6+DGEnrGAF2AUnZc+Y3d5/+wagGmIZgWXJeYe7MT6/RB4hdgyuvhxPlX0PVZqS+0H'
        'FL/HhP0wN54gniBlvRs8XJc4Mj5VDLFEi8PxO4hLdV0Kv/+l3jLwlvFZak2b84sHyrgstb/rAxoGVL9R8yM3r0Db'
        'pu0pBZKTcF2dqbI+IIBSTjqoU0F9RB5NzFsL0JxGA53WAoxf6uFBLQY5RkBHbE+BANWocloTAwxEhlwkFgj4YAla'
        'xWLxfFRTBNohguDMxIV2p2O8nNGABuEEK+WCGi44bnAdVKOyCaLYSL+hIkTOqCWM9zgkhZ/3uPFiIixP+ecDnEd4'
        'U0ZMDlKiapl3wufzb/BgNqB5Fg5CtVudRoYNSvPCQUxSs7zCEcxNhK+Rwk8kcD7QXcRGR4QNS8P191acDuiU8nD6'
        'HtlyXHiJmMZBaQE7ZC/PdfVmnKZHRIh9uJ17D8ba3NDDZgj70e7cAaaRA+jphExTYFMtl9EAeyscjjks6BVIJ8Kd'
        'fcgpiEUYJuapIGL1Bp3I3D60qlD4OUhXh6houkFrLchYFfeiHhol2tm1NbZVFWWgeSYW2wICBFUzqpx/PomoT87N'
        'AwNyyFdQ3iGoaiiRiEDB4BlmG2sHsJyAqcVyfCKwudOo9aT6NBDGiFoPbXCg8CTaBTbUlCpTNiArgI25ckjG53AS'
        'bAgHSr/Pb8ARjWVKnBUdg3bePj0kvxfLyVVwffyTuAQJA7DUdaYSRQ13j2IXZizsyMqT8MRu1ZGVzxRxD2FKapiB'
        '0cXJ0UcVdTy0UNlwF3CK3jLkPqRcPlUhxPuL/uOQon0/DdcuEnt3IYzuEdpi1I0xrZHkhJGAqx3Ko47R2hNioUSt'
        'CuN3MYJQz3TuNgVnl/Gpi9r7o7m08F3YU6AzRFc6LEu3kPEdDMkwPLQi+XTCY+k8ybmfdHcPe53pNhnMrnvDGVbB'
        'pymEIEDAw5wDXP3f4vmA9QEVOz8WBITuQcgw/TLFTaoG5BrB5yg60vIlPNb/vJhlKD8B86RKrMEUPmyw+HooE4q+'
        'D38OAwF8vpSqBofpo7Q9nVYykMIz5AgDsRm7vhOFeDK/e5/yBc5iifVDHZYQj51+gKp7wITq8cvjsL7iue41SLtt'
        '9BCMuC+YF9D/BrRIKn4MnxrrOjOLv0dhioRDfGeBQ3m1bStEzVauTNhLZfUvYdp2CX8Gwz1J0ymmIfpXUL52Fs08'
        'lL2POfenRU3BjSFmho1sypK4OqSNcyWuTqmZcQh3kvlL9HF4LlSGtKastyEH6z9at9K9flSZ6+t+ly+yVv5GJDk3'
        'hc1EcUzbY7Kgzm1dwaAODd4V/JIojgCM5+3jgC/1CXhmGilQ2VAJGExTMFfdHNCMed/FdUNGpwAXQdR5pA+8L2sP'
        'DQs/y5FcsNkxx8IRMkA6OZwcCpqCVem1o7K+Tygi9MyeApikdwVaEMRxCBTiX2VknjwcJbI+5ciWevxLiywUJxnm'
        'HARFoM4yEBpNld15VC9eqwS7R/09g3YPMDqT8xm5NN4uBWV0O01xSspFSfyKaNLfaPpZRrgAryJJTko3DcSDh72Z'
        'OwLGhy4hYYBYuQ85LNQ0lUUatTKg4OcjsBQu4kUgdcFwVTSJv8tPQXD751KhFLZn3jd4C0l2Hpaj/hmXoRiIknQJ'
        'vzkKGSKwZC5BQ9Sng5hH8BdaUdIQC9bQEUITewTdGZ9vF5YP1miIwMjmQWSD55WgG4gVqr12uonUunYO9xbUA2bo'
        'rdDSE3DfgKB3vmPlooK4LiQJz5HLkmNW4V1omPfwK8QkhHCSzp9InIA/c/6nyGXZSUWX3xBejoeogMPIW2ssSBTa'
        'C044KBZBd9/WLPw5P6PbIaemey9Ky53k+EURbF9OSiV9YCYTjl4R+3gBOQjIWVrQ7UZM413tMm7JlfSmSGtjvq++'
        'h5yBc5zhzncS5T3EQvd3cRHbcaxi3gNT0dxRjNjbXL9wRgBIQgDyc+0RxoejpZmwQMcMPmLHo0MOnT0P+bkpCOIT'
        'sFLADlQBK5AbI1WY09vF9Wl9wkYEkMrTNDAdyuDMKCuzWr0XZoAZcYR8lQvHIYGXxLWd+9PFI9lc150jLCrIbz4i'
        'HIdvhwPM559p6nKeCjq/e5C/oEHoQ+230HXL3cWjnJ/3g6I9n0+Lcl0G3rIfeYvRU5fGL4rk54Yb31qX5q5T85HG'
        '9+qs3KLIGcd9c6NdA5H2THd4NLconBvDw4nnaC5XxhcruCUKXMBul7nLZU6FhOvlupYTmYSfO117yPsfxiO1Uu4I'
        'b1UY+xz/zi2K8n7iWxSD+YvCwSCAWXOvyUrP132AvLAcB2DelQsSCbuSH+M8fdUtGwNyu6M4KJIbY8oKdwRxC8MB'
        'HRTApcEZbJlvIWLnkyQf8hON7yobjgCCoEncJNNCTFsYDqv0wmgI1CpMKyLXBXErIk0ropSbX2dEMQsalxVubFe6'
        'cANlmier+wtsRozt9uf5FTF8VrS2x/0rycWL3AHjsdpZEJ5xTNJjPFYnJ5ZaND24DD52HO4s6h0IiXZ+flVy7DaH'
        'QLDaUwHzg8swAjoEvLmD3izq9X4ihnFr485SQ2OMR5Sbp6NwsoFujbytvqwYLhV7Dvtvnqx3Bs7NFVF3+g/yarS6'
        'uPgEfaLBWHLXqlJb2eryijV3V66tqq65x17LOtbVrd9w71D6AfVbRVUs/lQ72FhmKxCjxDHetV4Ga1YZbw0rs0Qy'
        '9tAyy1imLkjT5tknGXF+DuVFOF6KR2wsjch92CiXZQvx3IoxxUSm8MBRAsy2JghMyBIcWWe5JhV5lQbW5GbM6O0a'
        'mkaKaazliYNptpADvAGE/vVd+BV0wC5EQO3Vwt0YxsR0npw4PXK/wrW7+kNYuatf5lDBmh7+t62Eqb1Jfgj3A+4Y'
        'JC5Y84OKxdWqIKiYxI7gDzB/A+hCedn7iMW/rPEZ3cVLR/TXyPTbhqdfNjI9VFfogiYZD7AybdsSlB45CPo7YCtu'
        'sGBPyerQHkiumbTgTSmh1EelT/ZbGv4CLw0MSNmPXQNBjqk0YQhGmMSEjiA/yNvjv7aC7aPZVSQVzRGKZy4AlRxR'
        'heFJg1DYw7IyOBri0bQNbwDsJ5yWftwRs4qd+Iu0ICnvS8Nx3UbNzF93yDFG4o//+fyO0X7P9XgGBSlqGJZTFAs5'
        'oILVEM+vkab0fhVYqIi83IKXuoRUXLZRncGSRsCQBDlGuS5LHFPwBBucxsRUILMewsMGxH/Y95738xZ6PC5wPgLp'
        'SbJR4+oPf2BmpzwcjtpHZsBC8zmK/zhcTeARXoKy4NZEjPANFnoMcregxCN8B4cYWvBWH27zIn1iHGCenAlnkOGN'
        '/QffgP4Z2yeU4UYlF1s3MKpukS/28a4MPAMHGodE2C1BG9lJcAwQzw8GsZG0nV3CKVxiu3zvCQpSWYdceOAqPc0H'
        'tSU6zDdQHvhusANA3oLVsVUR6F2+h1Kye6O02P1XnAuuA4pnnnENlDu+6pSjDqQ7DQUWoiPJJGvgyPlB+gtKB5lO'
        'wP0WqR0lQusVf5DQofbKyMMwbsSNx6trMtG3BXxtxDcGylNnIE9wWq4QIgoCYzi9L5CX87Y3uKgwOyNzkV+BNYRl'
        'uAQZz8RehR+PyD9QWG+n4jj3OXTbAFFFsFLtIVHq7QKpNyAfQTHD6NmnUtgHD4Dk4X0r7j0tdBsD4CUeiJcNjy+E'
        'qTkJZDUlL+UjMK4ZHS4Mjk2pYIHnFXwCZuUiiKvxHuWDeFJGMzrcuBFHMHIXm+Qo2bPpJjluFhwxnIrsInocY6nM'
        'Ty36dCvBz0a7v1FwhGiF5t10WxfELVVA4aOWAzOaQjYJL3H5suViMXjGYFh5eEAJU6E+gQ0xHqhdiJembpPBmg0i'
        '1HwuV9GZhndZmc40co/VnabuTMNj4QyXFsulTYd9MS/tTIuhgzGLS5vVmSGdhSToTpu1FKV8WDWhj57xPs+pSHX+'
        'PLTDsXxe4U4dFdsPh8RuhsnaZgbPdXxEpJ0cJpNxXbFXuXa5D3QqqFbrAxg96spJBzeLAbaUm7fGWo4XXPnIPDwM'
        '9WrJUJ6Vx+Qhechihd1nYTKdFP7m565yIApQqMligZI4KXCVz1F5qJB0pkWRlrXJfGnRwopPgR1QIpTR/QxFtyvK'
        'TpjcKJelDUjsqiOYbOBAlUzO40LMWUAtPICc+eDQ62bI74Cv3aeCblKJuyHJ4aalkgseK0v23I2fOd+1Nt4rEcbK'
        'CR176WZgvkSixuj0O7Oy52femb0wa5GFGRIYcv4QBEzXN5Ety7OB630ro2dTlPV4bxkUcMr611AnkkRZTBWUAaeI'
        'OrjDM97hTnLnhfsAQGRQ0Kr3dMjiGeGtEDG52yGBfUAfd1YwjMw195pcj2OuW8RcYMcg+ZiR+ZTX5FsA+fA2F6js'
        '+oW2Y6ic4PoBh3DwHPjP9iZBj7bkYMvSFTgqkcvvXDHsACRpv9Yn7njYfG2P9hCcB4aAWzaK18Bx5cl8Au48nGYd'
        '4wiGzUia9zOeEdiP8foVqInuBg9lb2e/9/maxU2G9g2y6SI5XZYdBHe2toduPUAQ5qNBgzoRss/6mByA9H4JFMTV'
        'XGnGDN6/QVePC+D9LeLdQfFGULz94u4n2u9LTVu4KG9B6nx1QWbeguyFqQXZixZCwbiagaHUp0IPuT/Xkng7KMIu'
        'nPErYXLJ6ANH5D5zp+Ek61a6Gvx7eXfuFT7MeeqqsUv50JsAnNHljgnjQlwmH1t0IQhU8GgL4tMJhePU5eP4uxWu'
        '9QoGzgp11xXwCkm78XDtfH4ebEwcwXA0iJsXbkqszeLDY7kKBbdAJtH55sUApEByfkYioHU8RxZqmAPk7JmrP1Lp'
        'QksDuThJLA3ALLAAx++9z3FnZypdlyLtfwmaCpOHG8VBRe5WcLCxWS7jQyRSMsUIHv+UJIEZ7ZhJy3mcb0m4Se4Y'
        'qfOQhNSeGxWo+hSx6iqfNdqdLhMeOo4LqR+Dvw4feE+S+5/cl65vwvnMKODl62V8jUIoguTcgiviJSDHWKLUdLUF'
        'xV6ZeZI7BVqJGLrMwoAaxYPw2jbum+XUHMdPURNRFrafcw6CMExCm8gVgy5hVjfRX73h7eTISXt35g9C19f+a0lw'
        '8XTxFWH9JaxtAN+w9Usc7je5TwklmYiSFS5vlL5L2NDtwh+hgBa8EClMQ10f7uNTuOmwVYEbx2tk+Zs6kNGW75sP'
        'F882fcX0FSITjSfXLyDltBzuIpcOpqatRPVOO8xDVTAeQOf1AskbP0IKFuPweCqu80Lc14O3phSINMN/70BofBcN'
        'nhRC1aXk2Jsqiqyb6Ab0htecV8zNE34L2hrQHi4roSLUtsCKMKhP6uQTyMS9oKzH3RXyPzuS9viGHrqR5S4By9dQ'
        'SUEOx0i9X8BYqKkRQwdRmeAVamTk8m2XkIihu8CZ4XH0QOxmosiDiPsJe/W+ALBxMlQVWhx/Bf9qmV97gVaCYfwp'
        'lzuCdweNh9kw7oAV1CWOPtxGDNEfQMefYOUYE347XhAZC5eBrN4frE1haY3zfLc7vvQKXF9AisgzazaGm40PKJSv'
        't7kEzVIP6QwhhNTQXLJRZq64P9K8Bn7t9yvMtZD2XjFt0VJPy43EgDSUoAg9RQHLlnHvN86PjQQ5JAoYMux1mqT+'
        '44shad4+TuoVOoCxM+bZyoZX0IoJAUkaHPNoOI4rTZKGGo5haXNeXk85C2H1GFP/Bc2ggH1eLB7/aP9aqnxdKtyK'
        'vLDDeEy55QRu+aNiHTIsu+FbEHIWM86NsZEMO+FiO2JhlZsu4a9jLIBBqT0KfhSwun2NvG40ucDmhQUdsyjrX8aU'
        'mExZj1XF14liYRRSrHmmsj4D9/RWcJyXi5X1BehJ0jjC9qMk3Ys3c/fjZ0J6s3E9vyzdOM51OfaB0aCwUdbPxpxa'
        'ZGmoHhX+dAYtNQHcTWkS83qHAu5g9kZAGnMx+0OvDKfpIyjtrJNjH4AxUPnntyRnQScRnNb7I4ghBJrWewYtllTX'
        'J6LD0AOBkNiiOABe/CgAHXl/V3tImH51cIOaS2yggoqAyHwSlzP/fSK8r042qhMEjVQ82BoCNJ3/PlYEb0/5hl7b'
        '8csboDBvtSDmO6SiJU0GaiYlSUxZA1ng3pzxoztTIjyBWrd2WGYg3ceclzsnHPwGlqciCdfdIZvBCOv8SAqtwhpM'
        '28G9CSEdd1l4ChJ2yBIZXCLOCPdhwgMoa72AOlxB2Is/3UNK7CL8YkQ9tN/C7W2nXx4VEqnMpvA3De07YtsdoRD4'
        'kCiUSeshOWozQ+gNKg9OC2EPvUUfAQeQMEYqxiyl/cu144QHLJtosjC4Cw1fckiVhB74GQaXiwKrpqexIFYCr5Q7'
        'LazBmuDXIY6Sr1AIz9OWiDmhPqQOsUPqwIYAwaHRJQ0dV3/qRoWrf+kDQa42tYfc9zogqMh3BdwZk6VC2NekcWhb'
        'mwDi0c109nz5NSVk0l3pglxKDiU4voP+zAaVrfd9ugi8PjSZvz+hHiaiRE1wtUmJJxhcC15OBr+6zLVeyrDT8H4d'
        'qfTG0VSM4qI2ERIcMgoLgwKMPSDfUenmPe40LD0LyckIYS4k65DJcD1ddpWsp9EQ3SUsCiKdMgq8R8kQej8Br5Ek'
        'YWWwaVL4N4dEGMAmYP2hQcDPaSpqVoXwr9HqTy5CXPqJxHhJDLnPsH24IWtohcn+H8kosEsn55UAgl2ZfFW8XPEM'
        'hSGZHP4JzyBYWBnX7f0Uwk006g3wov1VuJeGb4LsSrJgbdzNE402LFXhT8KN20fRLiVlqKIA6V82xF7X5HwN4mMl'
        'pKZgb9O29aJYPmR9aue6L7zpgxuThxvnSdrPKBojl3gaw/xtIuprMLByn2ixEywN367ToV1/+n6qkQHr0wY5PcAS'
        'yXUtEJZhazqkc6HvjxJhq6wzGI3lxP+6lG5K2O/R6g6G7mQx6/tQ7eshJ2b7bOT5Bmqeg5WDJ5yLV+JReTc5X1XG'
        'S8osFsahh+vyxLPMeI6d40Jb2RI+RHgEZCFuwJUC1g6Q0cH6mnYF7sg7zru+l8FhKu9L8O0V9k28ay8BO4IW6lLG'
        'eejV+1gIkPMnIAEcdbVL8Vb9Y5j8914wMhHq7Z9xrL1faiUmNrTNIHBGP4D8EP/9Hb+9wqFtwfVF4D4XTSnR8A2N'
        'aO4YR+I7MzaRzzURbY+fdkCEvEzo8n75PDK6nosn3FG/I7vzLlDtCBGfUVyOlkGt0HB9jp/dy5BBEVu6sIeqlcgI'
        'NzFUgDhKqMxP0lbPUH2t9kRTNDKdGR6YHsfI7GmSCE/TafFdU7YEjlWewmyw/M1oH2qcouOHywU/BekUNpQwjLOR'
        'evkF5MqDGmf0AEdju4TXBuhkMIB/0wAh/NngfRYvHR4ZIAtDt7AdQx5wZnQo6/GwIEXuelMGm4puYQPF4VhiHGA9'
        'nDBTToojSXB0kDgWwv2rDqdZisbxTPjeyTt4YYxUodcFhyCAXNplvIRLveIyArlwhxe6rvrY2WlNQemNabht7Ba+'
        'uEQqt4OYW905sJDRNoh7DJyIM4R0KG8eOduVxn3aNLlxqc+9QuL9lnuLGsNc38oCNe8liskuIYYYPpEfoedAr3CV'
        '3IRF2jrTPiC9phiyHvMROymFzaNazZtPiRSm1V7gNMSK585RuM1joKctlF/oQPLeSchOQsjOfyCDkJEXDKZCGiW8'
        'kHniwtpx8VN3oYRcpJN0u0GH1CU88rFIfaNINjEhfL1I6r5Xxp0HApV0Srq5AeErGOVeFpJszRN36kPp8dhPlB7z'
        '8/ZLyF0mbU/zHvy+yqvEurrbTi8ACydfBgonTeba6XrTKd8jRiInwCtTyv841n5ZAXdvlC+fINhneOSIJt2amir8'
        '/mVcyZbSbuvCm08eYTOR8XwOOHY0j17GXuK6fNUBe6AaCHq/d13+SVmPO2rX5SuO5P1TUZtCTqQkPwa7ngWAcf+U'
        'AOx3CDMCbLBitP67YWXygrTskJMzKu7Mb3LgYuh8euYwjAgXh8jkzTwnxJAaSRwRtArNmN8bzE3BX23boeH3P/3s'
        'ERSDKthHTMJ5CignwsouJ6xXPGkgLrPec8IZQrlsMJEczuD6dplYfNuNx9ZJuY/WSMihLNCzKXZhiTn07hnkeB1w'
        'BoPoVcYFk+0f4e6WEmKeuhfMUy5B7QnYQz1gMJkJCsabQeM4A24hHVh3g6TLbfe51/u2HP8p+OEDCxcd6T1f9sy4'
        'Q1lzz+8OucIdQ3RbjnOfQVwusaTiB50gisb4bsTO8t1YQ9x5xN1O3KPEXUbcDOJuJW48cV8l7jbiaojbRtyDxG0h'
        '7m7idhG3kbhbiFtO3PXEnUXcncT9grjTiUuONN24l7h7iLuDuPuIu5K4ZuIWE7eeuAbiOolrJW4BcXOIW0pclrgb'
        'iSsQ9xhxPyaumrjdxK0k7smS/w0fxctZOejvg+8CquEtHgIbgPB0/F5gySAs5C74zGLJz+Mbexf9vQ5+Z8Frhvc2'
        'eJfCa4e3Cd7t8D4L7z54X4X3HXhPwnsBXhl8cScK3lvgtcJbAO9KePEjgw/CuxXevfAehPcovGfgHYB3bCmUB68Z'
        '3vnwroF3HbxOeB+Gd0cpfquDYbrg/QDer0ppPc/hLxwjGg2vGt5Z8CbBO8/2/2d75mZki9d2hn25ash1HglTYFvP'
        'LnKwqfh1zfTK6lrbPLh/UmmDTzumw0FI1pa5Dr5XBbFBYjgL7iKlwuEpGqJnJxmQWjNslTbWlg7XgSpWlVTm28ht'
        'G/iAZSbejhkJZkLg6k4Fm0Mv7MPnJjHkxxVKLhml5+DnQMGXnpNdVVYNX/5Ef/XatVA9+GAA1CF8BKSIiSAQhx3u'
        'XInYsgHfaIRitQs21NgYJYbml9SymXZ7tR0/xwnhBdWljkrStIUlawE1M+ZaaBGM0yCU9hKkHIewRZkLsL5R6MdG'
        'pdJbPQyjQkg+Wyp2KjMew4U2ey3phQlMdhV0DNxvu/eanotm5tvg4wfX9NxEZn51San4tQQofxKzwFHJVqRtgMGo'
        'LoJrlenlJXZmMpNnq3WsDYxPDHzbCPqqqra60pbO2itpfezMFIQP+TBZYYm9As/MpjLXYcyQfpqK4SEtUWN4cVU5'
        'CZYGLmJBb8F4M9OYgsra1EryVdHr0Y9fS2NuQB92AF6zY2IxlO8P3cj8XWTMTUxhhZ11lFSKGG/2hwnW6f5QrsNm'
        '3wBCBVMEnUbGDWg6zba6oiqnBC5x4edKsXMW18BVQkLRGbYyeo2NjBl+RjWzqlRMKyf9TD/Ulgp0vADGs2S1La16'
        'fSpQbx5grUXCriypJZ9x+3/igRvhzKKiOB3O/BpsJr0wx8KFNgQtqoOoykrSJkYE3lyrHvIN2REPCi374ENh/90X'
        'H/zOif8l69JU69/7/PA//+mLHVaYYsSFdrQ+SsTP3Er/SUXiUUSf+EjEPpT+zC9eZMPfJAn9lY8I468//8hfjN8m'
        'pn+cfqmXPCsXwJdu4N2a5W+2hBHzPLAYHvhNiYPn+eefvyU2+zG8Njc1pvOemN/+9rcT2SOZEyCJapzpL+Mei80e'
        'OwYeiFfi8/DnhtEr0jdEvP322+HhHk14q/D0qFml00YBnrDmGksY97tjoSvSfhUKKEP3ffyeAj4foKiPTFNA/pCN'
        'fe0hmzdvDglx5IRAUSGBIVA74UAksBmsq3rnYnDDNEgbUzU4aFNzksGN6jb/62PD/71n+k5K28HiGw/h+fCWwivy'
        'CCIhzAE7QQq8C+DNh7ca3h/hrYF0LLz3wdsA7yPwPrWT0pQUOKkc+Cssg0yq+HUeBjhLBnIS+M0Gtl9Zhj4/H0U/'
        '5bX/Gpn/iw+M2xUY9BjN9XCHOFUzT7NQU6Ip1zg0GzUPaho1L2haNW9q3tX0akZpI7Wx2tna5dqdut26Pbq9usO6'
        'Xl1k3JS4G+IWxS2Oeyvuwzh5fE78b+J3xL8afzBeiP82PiJBlfB0QlfCOwnHE04mfJ3Ql9CfEKSP0Efrr9ffrJ+l'
        '1+nN+jT9Qn2hfpn+Ln2Ffp3eqXfrH9M/qf83/XP6f9e/oG/Xd+s/0Qv6MYkTEuckbk50J+5O3Jf4UuJrie2Jbyd2'
        'J36V2JcYZAg1TDXcZJhp0MJnTDIMiwz3GDYZNhuaDYcMPYZPDV8YzhrCjOOME40NxieMO4zNxkPGo8bjxq+NPxqZ'
        'JHlSZNKEJEOSNWleUm5SeZI96YGk+qRtSTuSPElHk04lnU0KMU0wTTNpTammXFOJ6R7TJtNvTX80HTJ9aDprCjXr'
        'zRZzhrnI7DA/Zd5r/tJ82Sy3RFjGW2ZYbrUkWayWAgtveczyB0uL5X3LxxavZVyyPjkpeX5yXvIdyauT705el3xf'
        'cn1yU/LDyduStyf/W/LTyXuS9yb/Nfnl5FeT25I7kg8ndycfTT6e/GnyF8lC8nfJ55L7k68kS1OCU8JTIlPGp0xK'
        'UafEpsSnGFNSUopSVqaUpdSlbErZnLIl5bGU36TsSHk65Q8pe1P+mtKS8nrKwZTDKd0pH6R8nHI2hX5gHr9xeFkT'
        'pI3QRmknaq+DEb5VG69N1qZrF2kLtMu0m7UPa7drd2oPaDu1n2ivaKU6hS5Cd5Nuji5el6a7Q1eiW617QNeke0T3'
        'tO5V3VHdD7rxcTFAEYvjVsXdE7c+zhm3Je6xuJ1xz8Tti3s57lScKl4fb42/PX5x/Kr4ivia+I3xj8Tvin8JKOat'
        '+GPxn8SHJ8QlZCYUJygTb01MSCxOXJFoS7Qnrk98HsZ5jOGo5bjlvGXAsij5N8l/gr6YmDI15caUW1IKU5anVKU4'
        'ocW7U15O6Uo5nfIDtg8uzmUAbQdrojW3wK3FQqDrezVbgKLbNR9oBjSTtBna27W/0cKpdC2cNddG6a7XzdLpdVbd'
        'bboC3QpdpQ5057qHdU/qXtC9ouvUdUP7juk+1p3UfaETdJPiboyzxM2PWxZ3d9yD8dvj34jviH87PjhhMrTAmlCY'
        '0JjweMIbCdP00/WZ+vn6JYkrEx2Efn+d+KfErsRPEiWGcMMUww0Gq6HYsMGw3fCawWN4y3DScNHwk0FqvMVoNuYb'
        'i43Vxnrj34zngU5jkvRJx5O+SvouyZcUY4o3JZkyTLeZ8kyNQI3Pmv5iOmjqMX1u+sYUZB5tnmHWmpPMy80V5mqz'
        '07zV/EfzX8z/Ye4xf2j+2PyF+Xvzj+ZL5qtmmUVlmWrRW1ZYNlu2WX5n+dzSa5kMVGlLXp98f/Ljyc8m04/341GI'
        'ddqN2gbtVu3z2n3al7TvaI9qP9Ke0l7QBuvidEmkv5y6Rt0+XYtuW9yOuGfj9sJovxp3NO6TuG/izsVdiXPG18d3'
        'Qf90xx+Fcf44/mT8F8Ahvonvi/8hfkLCgwlbEnYk7Ex4NuH5BKk+WB+uj9SP108CLlGhv1//tP4V/QH9p/rP9T/o'
        'B/TBiaMSYxNnJcYnJidmJN6WmJN4R2J14uOJVxNvMdQYPjScMsQY0433GguSNiQ9lPR4UlvSu0nvJ51MYkxwmN1U'
        'BDP4FVOd+R3zB+Ze8w8wV8fBLL0H5udLlk7LDxafJSw5Jvmm5FnJumRD8tLkDcnvpDCwj0O1dbR2t/b2uJvjv4xf'
        'nWBPeCxhhf5LqI8K6PTXiZMMNwLvqTNsMzxp2G+4Yphu1BoLjauMdcanjXuNNwBvWZCUn1QDfOXVpLeSziRdTQo1'
        'jTPNhhrlm+4w/c101HSvhbVUWcotd1mWWQotOZbbLBmWZIvBorPMstxkUVsmWaIsoy0Ki9RyxXzB3Gc+AyP5qfkY'
        'jOrb5g5zm7nV/KL5T+Y95qfNO8xPwJg3mevN95vXm+3mSnOZeaV5qRn/LgRe4i3QFGuWaVZqSmE+VGpqNCx8RGKj'
        'xqmpB36/RbNNs13Tptuh2a3p0P1J97ZujLZe26N7UcfoZDD3W3X/WjL/f3sG//aKhMh90eLeaihcQtnpNXB8kKaK'
        'YUdx2zCsKlAZqhnYNTJ2poKpYlZDqBr+NIgaIOWgooJPyzDz4d/NkOZfz//U838A58EfeA=='
),
}
if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkpe_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT')
with open(sys.argv[2], 'wb') as f:
    f.write(zlib.decompress(base64.b64decode(PROGRAMS[sys.argv[1]])))
