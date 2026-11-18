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

OW32S (OW32S.EXE, wcc386 -za99 -bt=nt, wlink system nt_win, ow32s.rc,
    comdlg32 advapi32 shell32 lz32 version): Win32s' DLLs and functions, as
    its window shows them: "reg: 0 1 42 'hello' values 2, 'OW32S.Document'
    del 0 2, user 'User'", "version: 688 'WINN Win32s test' 10002, English
    (United States)", "lz: 5 'plain' 'OW32S.TXT'; shell: 33 'notepad.exe';
    open 1", "kernel: 10 'a=1,b=two' struct 1 4, '[env]' 'WINN' 5>7, wait 0
    258 0, 'C:\\'", "user: cbt 1, clip 'clip text', pt 1 0, scan 1E vk 41
    'Ab', accel 1", "gdi: fonts 1 'System', fit 3, brush 3 4, dda 20,
    bitmap 1, rgn 0 0 30 20", "getmsg: 3" (a WH_GETMESSAGE hook); O the Open
    dialog ("open: 1 'C:\\HOSTWIN\\README.TXT' 11"), C ChooseColor, F
    FindText (its message: "find: 'abc' next down", "find: closed"), P
    PrintDlg (PD_RETURNDEFAULT: "print: 0" without a default printer)
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
        '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'
),
    'OW32S': (
# /* OW32S: Win32s' DLLs and functions on WINN (ADVAPI32, COMDLG32, SHELL32,
#  * LZ32, VERSION; KERNEL32's, USER32's and GDI32's of Win32s). Its window
#  * shows the results; O: GetOpenFileName, C: ChooseColor, F: FindText,
#  * P: PrintDlg. */
# #include <windows.h>
# #include <commdlg.h>
# #include <shellapi.h>
# #include <lzexpand.h>
# #include <winver.h>
# #include <stdio.h>
# #include <string.h>
#
# static char line[12][160];
# static int cbt_creates, getmsgs;
# static HHOOK cbt, getmsg;
# static UINT find_msg;
# static FINDREPLACE fr;
# static char what[64] = "abc";
# static int fonts;
# static char first_font[40];
# static int dda;
#
# static LRESULT CALLBACK cbt_proc(int code, WPARAM wp, LPARAM lp)
# {
#     if (code == HCBT_CREATEWND) {
#         CBT_CREATEWND *cw = (CBT_CREATEWND *)lp;
#         if (cw->lpcs && cw->lpcs->lpszName && !strcmp(cw->lpcs->lpszName, "hooked"))
#             cbt_creates++;
#     }
#     return CallNextHookEx(cbt, code, wp, lp);
# }
#
# static LRESULT CALLBACK getmsg_proc(int code, WPARAM wp, LPARAM lp)
# {
#     MSG *m = (MSG *)lp;
#     if (m->message == WM_USER + 7)
#         getmsgs += (int)m->wParam;
#     return CallNextHookEx(getmsg, code, wp, lp);
# }
#
# static int CALLBACK font_proc(const LOGFONT *lf, const TEXTMETRIC *tm, DWORD type, LPARAM lp)
# {
#     if (!fonts++)
#         strcpy(first_font, lf->lfFaceName);
#     return 1;
# }
#
# static void CALLBACK dda_proc(int x, int y, LPARAM lp)
# {
#     dda += (int)lp;
# }
#
# static void registry(void)
# {
#     HKEY k, c;
#     DWORD disp, n = 42, type, size, values = 0, subkeys = 0;
#     char s[64], cls[64];
#     LONG r = RegCreateKeyEx(HKEY_CURRENT_USER, "Software\\WINN\\OW32S", 0, NULL, 0, KEY_ALL_ACCESS,
#                             NULL, &k, &disp);
#     RegSetValueEx(k, "num", 0, REG_DWORD, (BYTE *)&n, 4);
#     RegSetValueEx(k, "str", 0, REG_SZ, (BYTE *)"hello", 6);
#     n = 0;
#     size = 4;
#     RegQueryValueEx(k, "num", NULL, &type, (BYTE *)&n, &size);
#     size = sizeof(s);
#     RegQueryValueEx(k, "str", NULL, NULL, (BYTE *)s, &size);
#     RegQueryInfoKey(k, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
#     RegCloseKey(k);
#     RegSetValue(HKEY_CLASSES_ROOT, ".ow32s", REG_SZ, "OW32S.Document", 14);
#     LONG cs = sizeof(cls);
#     RegQueryValue(HKEY_CLASSES_ROOT, ".ow32s", cls, &cs);
#     LONG del = RegDeleteKey(HKEY_CURRENT_USER, "Software\\WINN\\OW32S");
#     LONG gone = RegOpenKey(HKEY_CURRENT_USER, "Software\\WINN\\OW32S", &c);
#     char user[32];
#     DWORD un = sizeof(user);
#     GetUserName(user, &un);
#     sprintf(line[0], "reg: %ld %lu %lu '%s' values %lu, '%s' del %ld %ld, user '%s'", r, disp,
#             n, s, values, cls, del, gone, user);
# }
#
# static void version(void)
# {
#     char path[144];
#     DWORD h, n;
#     static char block[2048];
#     void *p;
#     UINT len;
#     char *desc = "?";
#     DWORD ms = 0;
#     char lang[40];
#     GetModuleFileName(NULL, path, sizeof(path));
#     n = GetFileVersionInfoSize(path, &h);
#     if (n && n <= sizeof(block) && GetFileVersionInfo(path, h, n, block)) {
#         if (VerQueryValue(block, "\\StringFileInfo\\040904E4\\FileDescription", &p, &len))
#             desc = (char *)p;
#         if (VerQueryValue(block, "\\", &p, &len))
#             ms = ((VS_FIXEDFILEINFO *)p)->dwFileVersionMS;
#     }
#     VerLanguageName(0x409, lang, sizeof(lang));
#     sprintf(line[1], "version: %lu '%s' %lX, %s", n, desc, ms, lang);
# }
#
# static void lz_shell(void)
# {
#     OFSTRUCT of;
#     HFILE f = _lcreat("OW32S.TXT", 0);
#     _lwrite(f, "plain text", 10);
#     _lclose(f);
#     char buf[16] = "";
#     INT h = LZOpenFile("OW32S.TXT", &of, OF_READ);
#     INT got = h >= 0 ? LZRead(h, buf, 5) : -1;
#     buf[got > 0 ? got : 0] = 0;
#     if (h >= 0)
#         LZClose(h);
#     char exp[64];
#     GetExpandedName("OW32S.TXT", exp);
#     char exe[144] = "";
#     HINSTANCE fe = FindExecutable("OW32S.TXT", NULL, exe);
#     HFILE o = OpenFile("OW32S.TXT", &of, OF_EXIST);
#     sprintf(line[2], "lz: %d '%s' '%s'; shell: %d '%s'; open %d", got, buf, exp, (int)fe, exe,
#             o != HFILE_ERROR);
# }
#
# static void kernel(void)
# {
#     char sec[128], *p;
#     WritePrivateProfileString("ow32s", "a", "1", "OW32S.INI");
#     WritePrivateProfileString("ow32s", "b", "two", "OW32S.INI");
#     DWORD n = GetPrivateProfileSection("ow32s", sec, sizeof(sec), "OW32S.INI");
#     for (p = sec; *p; p += strlen(p) + 1)
#         p[strlen(p)] = p[strlen(p) + 1] ? ',' : 0;
#     RECT rc = { 1, 2, 3, 4 }, back = { 0 };
#     WritePrivateProfileStruct("ow32s", "rect", &rc, sizeof(rc), "OW32S.INI");
#     BOOL sok = GetPrivateProfileStruct("ow32s", "rect", &back, sizeof(back), "OW32S.INI");
#     char env[64], comp[32];
#     SetEnvironmentVariable("OW32S", "env");
#     ExpandEnvironmentStrings("[%OW32S%]", env, sizeof(env));
#     DWORD cn = sizeof(comp);
#     GetComputerName(comp, &cn);
#     LONG x = 5;
#     LONG old = InterlockedCompareExchange(&x, 7, 5);
#     HANDLE sem = CreateSemaphore(NULL, 1, 2, NULL);
#     DWORD w1 = WaitForSingleObject(sem, 0), w2 = WaitForSingleObject(sem, 0);
#     ReleaseSemaphore(sem, 1, NULL);
#     HANDLE ev = CreateEvent(NULL, TRUE, TRUE, NULL);
#     HANDLE both[2];
#     both[0] = sem;
#     both[1] = ev;
#     DWORD w3 = WaitForMultipleObjects(2, both, TRUE, 0);
#     CloseHandle(sem);
#     CloseHandle(ev);
#     char drives[64];
#     GetLogicalDriveStrings(sizeof(drives), drives);
#     sprintf(line[3], "kernel: %lu '%s' struct %d %ld, '%s' '%s' %ld>%ld, wait %lu %lu %lu, '%s'",
#             n, sec, sok, back.bottom, env, comp, old, x, w1, w2, w3, drives);
# }
#
# static void user(HWND h)
# {
#     cbt = SetWindowsHookEx(WH_CBT, cbt_proc, NULL, GetCurrentThreadId());
#     HWND child = CreateWindow("STATIC", "hooked", WS_CHILD, 0, 0, 10, 10, h, NULL,
#                               GetModuleHandle(NULL), NULL);
#     DestroyWindow(child);
#     UnhookWindowsHookEx(cbt);
#     child = CreateWindow("STATIC", "hooked", WS_CHILD, 0, 0, 10, 10, h, NULL,
#                          GetModuleHandle(NULL), NULL);
#     DestroyWindow(child);
#     getmsg = SetWindowsHookEx(WH_GETMESSAGE, getmsg_proc, NULL, GetCurrentThreadId());
#     PostMessage(h, WM_USER + 7, 3, 0);
#     /* the clipboard */
#     HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
#     strcpy((char *)GlobalLock(g), "clip text");
#     GlobalUnlock(g);
#     OpenClipboard(h);
#     EmptyClipboard();
#     SetClipboardData(CF_TEXT, g);
#     CloseClipboard();
#     char clip[32] = "";
#     OpenClipboard(h);
#     HANDLE c = GetClipboardData(CF_TEXT);
#     if (c)
#         strcpy(clip, (char *)GlobalLock(c));
#     CloseClipboard();
#     RECT r = { 0, 0, 10, 10 };
#     POINT in = { 5, 5 }, out = { 20, 5 };
#     char oem[8];
#     CharToOem("Ab", oem);
#     ACCEL acc = { FVIRTKEY | FCONTROL, 'Q', 77 };
#     HACCEL ha = CreateAcceleratorTable(&acc, 1);
#     sprintf(line[4], "user: cbt %d, clip '%s', pt %d %d, scan %X vk %X '%s', accel %d",
#             cbt_creates, clip, PtInRect(&r, in), PtInRect(&r, out), MapVirtualKey('A', 0),
#             VkKeyScan('a'), oem, ha != NULL);
#     DestroyAcceleratorTable(ha);
# }
#
# static void gdi(HDC dc)
# {
#     POINT pts[6] = { { 300, 300 }, { 340, 300 }, { 320, 330 }, { 360, 300 }, { 400, 300 },
#                      { 380, 330 } };
#     INT counts[2] = { 3, 3 };
#     PolyPolygon(dc, pts, counts, 2);
#     fonts = 0;
#     EnumFontFamilies(dc, NULL, (FONTENUMPROC)font_proc, 0);
#     SIZE sz;
#     int fit = 0;
#     GetTextExtentExPoint(dc, "abcdefghij", 10, 30, &fit, NULL, &sz);
#     POINT old;
#     SetBrushOrgEx(dc, 3, 4, &old);
#     SetBrushOrgEx(dc, 0, 0, &old);
#     dda = 0;
#     LineDDA(0, 0, 10, 0, dda_proc, 2);
#     BYTE bits[8] = { 0xff, 0, 0xff, 0, 0xff, 0, 0xff, 0 };
#     BITMAP bm = { 0, 8, 4, 2, 1, 1, NULL };
#     bm.bmBits = bits;
#     HBITMAP hb = CreateBitmapIndirect(&bm);
#     struct { RGNDATAHEADER h; RECT r[2]; } rd = { { 32, RDH_RECTANGLES, 2, 32, { 0, 0, 30, 20 } },
#                                                   { { 0, 0, 10, 10 }, { 20, 10, 30, 20 } } };
#     HRGN rgn = ExtCreateRegion(NULL, sizeof(rd), (RGNDATA *)&rd);
#     RECT box;
#     GetRgnBox(rgn, &box);
#     sprintf(line[5], "gdi: fonts %d '%s', fit %d, brush %ld %ld, dda %d, bitmap %d, rgn %ld %ld %ld %ld",
#             fonts > 0, first_font, fit, old.x, old.y, dda, hb != NULL, box.left, box.top,
#             box.right, box.bottom);
#     DeleteObject(hb);
#     DeleteObject(rgn);
# }
#
# static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
# {
#     if (m == find_msg && find_msg) {
#         FINDREPLACE *f = (FINDREPLACE *)lp;
#         if (f->Flags & FR_DIALOGTERM)
#             sprintf(line[7], "find: closed");
#         else
#             sprintf(line[7], "find: '%s' %s%s", f->lpstrFindWhat,
#                     f->Flags & FR_FINDNEXT ? "next" : "", f->Flags & FR_DOWN ? " down" : "");
#         InvalidateRect(h, NULL, TRUE);
#         return 0;
#     }
#     switch (m) {
#     case WM_CREATE:
#         registry();
#         version();
#         lz_shell();
#         kernel();
#         user(h);
#         return 0;
#     case WM_USER + 7:
#         sprintf(line[6], "getmsg: %d", getmsgs);
#         UnhookWindowsHookEx(getmsg);
#         InvalidateRect(h, NULL, TRUE);
#         return 0;
#     case WM_CHAR:
#         if (wp == 'o') {
#             OPENFILENAME o;
#             char file[260] = "";
#             memset(&o, 0, sizeof(o));
#             o.lStructSize = sizeof(o);
#             o.hwndOwner = h;
#             o.lpstrFilter = "Text files (*.txt)\0*.TXT\0All files\0*.*\0";
#             o.nFilterIndex = 1;
#             o.lpstrFile = file;
#             o.nMaxFile = sizeof(file);
#             o.Flags = OFN_HIDEREADONLY | OFN_ENABLEHOOK;
#             BOOL ok = GetOpenFileName(&o);
#             sprintf(line[8], "open: %d '%s' %d", ok, file, o.nFileOffset);
#         } else if (wp == 'c') {
#             static COLORREF cust[16];
#             CHOOSECOLOR c;
#             memset(&c, 0, sizeof(c));
#             c.lStructSize = sizeof(c);
#             c.hwndOwner = h;
#             c.rgbResult = RGB(255, 0, 0);
#             c.lpCustColors = cust;
#             c.Flags = CC_RGBINIT;
#             BOOL ok = ChooseColor(&c);
#             sprintf(line[9], "color: %d %06lX", ok, c.rgbResult);
#         } else if (wp == 'f') {
#             memset(&fr, 0, sizeof(fr));
#             fr.lStructSize = sizeof(fr);
#             fr.hwndOwner = h;
#             fr.lpstrFindWhat = what;
#             fr.wFindWhatLen = sizeof(what);
#             fr.Flags = FR_DOWN;
#             FindText(&fr);
#         } else if (wp == 'p') {
#             PRINTDLG p;
#             memset(&p, 0, sizeof(p));
#             p.lStructSize = sizeof(p);
#             p.hwndOwner = h;
#             p.Flags = PD_RETURNDEFAULT;
#             BOOL ok = PrintDlg(&p);
#             DEVNAMES *dn = p.hDevNames ? (DEVNAMES *)GlobalLock(p.hDevNames) : NULL;
#             sprintf(line[10], "print: %d '%s' mode %d", ok,
#                     dn ? (char *)dn + dn->wDeviceOffset : "", p.hDevMode != NULL);
#         }
#         InvalidateRect(h, NULL, TRUE);
#         return 0;
#     case WM_PAINT: {
#         PAINTSTRUCT ps;
#         HDC dc = BeginPaint(h, &ps);
#         gdi(dc);
#         for (int i = 0; i < 12; i++)
#             TextOut(dc, 4, 4 + 16 * i, line[i], (int)strlen(line[i]));
#         EndPaint(h, &ps);
#         return 0;
#     }
#     case WM_DESTROY:
#         PostQuitMessage(0);
#         return 0;
#     }
#     return DefWindowProc(h, m, wp, lp);
# }
#
# int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
# {
#     WNDCLASS wc;
#     memset(&wc, 0, sizeof(wc));
#     wc.lpfnWndProc = wndproc;
#     wc.hInstance = inst;
#     wc.hCursor = LoadCursor(NULL, IDC_ARROW);
#     wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
#     wc.lpszClassName = "Ow32sClass";
#     RegisterClass(&wc);
#     find_msg = RegisterWindowMessage(FINDMSGSTRING);
#     HWND h = CreateWindow("Ow32sClass", "OW32S - Win32s", WS_OVERLAPPEDWINDOW, 0, 0, 630, 420,
#                           NULL, NULL, inst, NULL);
#     ShowWindow(h, show);
#     MSG m;
#     while (GetMessage(&m, NULL, 0, 0)) {
#         if (fr.lStructSize && IsWindow(GetActiveWindow()) && IsDialogMessage(GetActiveWindow(), &m))
#             continue;
#         TranslateMessage(&m);
#         DispatchMessage(&m);
#     }
#     return 0;
# }
# /* ow32s.rc */
# #include <windows.h>
#
# 1 VERSIONINFO
# FILEVERSION 1,2,3,4
# PRODUCTVERSION 1,2,3,4
# FILEOS VOS__WINDOWS32
# FILETYPE VFT_APP
# BEGIN
#     BLOCK "StringFileInfo"
#     BEGIN
#         BLOCK "040904E4"
#         BEGIN
#             VALUE "FileDescription", "WINN Win32s test\0"
#             VALUE "FileVersion", "1.2.3.4\0"
#         END
#     END
#     BLOCK "VarFileInfo"
#     BEGIN
#         VALUE "Translation", 0x409, 1252
#     END
# END
        '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'
        '7HhjHY43PJK+Z+Q8rQcK9+nYgt4GDdc03EroXnOLqr+93t/e6N90+GLYo90LFu4fPFX+/MS9mXeeei78gniIitt0'
        'WPwUcYvYSRx90AlRcox/Ck3f/ikx7NnInjx7cuw5lT1JPeifspz5TzB/K/M3sWcde1azp4s989lTz54V7Hma5dIy'
        'v4Y9E9kzmT03sadFros96Zt1/ilx7BnPnlr2jGbPLPZMZk8Le+rZ8yTLa2R+DXseZPUeZPAXmP9Z9nyKPZ9jzxiW'
        'cgfzd7Lna+y5nT372XOAPfew5072fIs9T8ttYSUY2DOfPSUG/4g9IxnkOPOzD9BO4dhzqtwWBk+XY9lzBoO72HNI'
        'pjl7GuSUMh2YP449jzNM3MwvMf9S5m9mz5MMkiNjwp5DDFIq04o9t8j1sudm9iyS8Wcpf878z7LnU+wZL9OTPXey'
        '5w727GTPfvZ8iT1fY8897PkWe25nz0PsuZS1ZTl7VrBnKXs2smcTe9axJ9/8Pz9ZEvy3wz3qb4b94VOwiOwPgj0G'
        '2HbATgfB/gWwjwC7EAT7LWDHAevcGFQ2YC/AMvBEEKwLsIOA8Q8H/QYJYEvhWoNg7yFc1/zDOB8D/Du4S3BXtyi/'
        'oIH3HLhMuGK4arhGuM1wv4R7AW6nknaP8n4f7+Nwp+HUsMKcBGeAc8LNhyuAq4BbB/co3HNwL8O9BXcMbggu8hGO'
        'i4NLhHPBrYCrh3PDPQr3FNxLcPvgDsOdhLsAFw0rzqlwqXC5cHVwm+CegnsRjqw8D8J9RdaeIuqBux0uS6Q9Eegl'
        'ym0ox3sNXDPcT+F+A7cDrh/uMNw/4C4paaNhfnoDnBFuLlwO3DK4Rrg2uKfgXoHrhTsMdwJuCC4C9qo3wM2AmwuX'
        'C7cMrhpuPdyjcL+C2w73Gtw7cBLcBbhoHKzdAGeGy9ok41GAdxUcD/ewAtuE96/hXoMb2PQ//fz/Uj+nLbw7PefO'
        'wG2j9GzFl5eVkRP4suKYzyympBekuAKpgi4q5RQrnoKM3Dwo9JkfJ4hpFbW1DWVptdW19Sn02zuZuMeUX9bIp+A3'
        'e3C5iT4nmonrVgtKVpel0O+ZuPAlRj69elUK5cV9KL4stZJfXVKXXVNaWV+2kqcy0suqy/iyhSuqWDiEy6gRVmfi'
        '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'
        '+e/u+/8FvWKAbA=='
),
}
if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkpe_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT')
with open(sys.argv[2], 'wb') as f:
    f.write(zlib.decompress(base64.b64decode(PROGRAMS[sys.argv[1]])))
