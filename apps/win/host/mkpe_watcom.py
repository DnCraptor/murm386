#!/usr/bin/env python3
"""Write a Win32 program or DLL built by Open Watcom 2.0 for WINN's Win32
layer (core/pe.c ...), with the compiler's own start-up code; the sources
are here, the files as they came out of the linker (zlib, base64).

    mkpe_watcom.py NAME OUTPUT

OW32 (OW32.EXE, wcc386 -za99 -bt=nt, wlink system nt_win, ow32.rc): KERNEL32,
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
    7 77", "gdi: bitmap 16x16 8, pen 1 FF0000, text 'OW32 - Win32 on
    WINN'"; its bold title line: "font 16/700, extent 42x16, timer 4 3,
    accel 2, chars 3" (Ctrl+T twice, "abc" typed into its subclassed edit
    control); a rectangle, an ellipse, its icon, the
    system's exclamation, its bitmap stretched, the DLL's window "painted
    by the DLL". Ctrl+D: its dialog (a template of its resources), the
    number typed: "dialog: 42". SPACE: Windows 2000's CreateHardLinkA,
    which WINN has not: "OW32.EXE called KERNEL32.CreateHardLinkA, which
    WINN does not have yet." "OW32 fault": "OW32 caused a Divide by Zero
    in module OW32.EXE at OW32+000022FF."

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
#     DrawIcon(dc, r.right - 290, 256, LoadIconA(NULL, IDI_EXCLAMATION));
#     EndPaint(h, &ps);
# }
#
# int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show);
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
#         sprintf(line[12], "posted %u %lX", wp, lp);
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
# wlink system nt_win name OW32.EXE file ow32.o library ow32dll.lib option resource=ow32.res
        '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'
        '94MPZw5dk35HzJEjR2y2lkRbg/7okAkFo4cgn+jdFSnR6i+PD16T9v3ByHLwU++8boXadmu1I81KXbj1dNOge++9'
        'd9AgxTMIRQ3iSjl9LtTQmegl21FO9lqFbOdmst3FsK8+mcE2hcauLXP/h078//cPMotfYSBXJN6ZuCfx6cSjid9J'
        'ujXpx0m/T/owSU/6R9KFJPPkIZOvmDx6Mnigk/Mmr51cMvneyT+c/MvJeya/OvmNyacmfzz508mRU2xTLpty1ZRr'
        'p4ybkjRl5pSMKUum3DKleoo65VdTdk/5eMpnU6Spg6YOnXrN1LFTp0ydN1WZunXq9qn3TN059fGpv5/aOjVm2vhp'
        'SdPSpy2cljdt5bS104qmlU9Tpvmm/XTaL6f9etqT056Ztn/aiWlfTzs/LWH66OmZ02+evn36PdPvm/7A9F9Mf2T6'
        'r6f/bvqz0w9MPzj9lelvTP9g+jfT581YMGPbDP+MHTNenfH6jIsz4mdePXP6zDkzF88snnn/zP+a+fTMF2cenHlk'
        '5mczv57ZM9OcPDr5uuRJyVOSZyevTi5ILk7emPzz5EeTf5P8ZPLTyfuTG5NfSj6afDz53eRPkr9I7kqOmBU964pZ'
        'zlnTZs2ZlTpr4ayls/6v9o40Oqvi+hIggQghCpTIGnuARhpx3pt52yzvhT0qS1ilHMTIkkAMIUCASFk+ISJLoCwR'
        'EDkpRShLKVsRwYM1ICBLDksKNAJFylasgZNS0JRG0jvv+yBfNjnH48/Od+b73sy9b2bunXvvvLlvvpmhNJmOo9Pp'
        'HLqK/pZuox/RQ/QE/ZZ+TyPY06wti2OY9WNDWApLZzNYNlvOVrNdbB87w4rYv1kpi+DNeEsew9P4BJ7Nt/ET/AFv'
        'JKJFJ+GKPmK62CB2i8/FGVEkwpwYp6MT7/R1BjsjnDedDGeqM9PJdrY4p5zLzg3njhPiRrjt3ThXc233dTfZzXDX'
        'uTvdve5n7kn37+4t97YrD2aSxxm2RF1RAhqBxqP56D3UXuVqZ3WQmqSmqOnqTHWhukTNUzeqW9Vj6mn1byANoVpj'
        'ranWQovRfgGHbGsgCynaOG0iSEOutks7ALJwU2vuSUEcSEEf/BoejafgGfhdvBd/ig/jk/gcvoiv4n/hctyQPA3S'
        '8DzpRHSQh1fJaDKerCTbSUfd0B29r56mZ+sL9Fx9tb5OL9J/Y7xn7IUePmwUGkVGhJloTjDPmG2s5rQVRbQffQP4'
        'P41+TK9RzGazZewm+4YpPJJ3AX5+xWc5qmu5g9xh7lJXbk4qaQ9FsagToige9USJaAgaicbB1uSz0bsoBy1Da9A6'
        'tA3tQfvRQXQMnUdX0G10Fz1AD1EDtZHaRH1ORapQE9QJ6lR1sfoP9bZaV4vSmmmtQU/i4PjxHlqiNlIbq83S5moL'
        'taXaKtCW7doZT1vqgI5o2MHdcD+ciifht/A7+h29hZFkfGGcMY6Zl03d6m+9bqVaM6yF1ov2ZDvPTqWczWL3WDs+'
        'ly/k6/lmvpN/zo/yQn6Jf8e/52GiIUhKgkgUI0SGWCMuCuSMcbKclc5m54RT7DxweruHXHnInFyz/Iq6Uz2gPq+9'
        'AX03U/sA+m6fdlg7Db0Xik1oWQJOxBPxVLwIb8W7vL47jy/j6/gufgp6rTl5kVDikv4kjWST9aC9BeQvpJ7eRH9O'
        'j9WRzvUueqI+yNPipfph/aZeAnpbz2hiPGvEGHGGa/QzRhtpxq+NecYiY79xzbhv1Dc/s7KdBc6HIMflTk93uJvn'
        'HnNPuefcCy40GDyNchfvMNQItYGd47uggSgZemsZWKw96Ci6gIpRmBqtdgQJToU+matuUY+r99T/qj21LOD/WuD9'
        'Ia1QuwQUlmgPtLo4ErfCHTACWl/GI/FYoNUH1moezsFLcC5ehY+DpN7GZSClLYmmJ4A1mqJP1/foJ/RrQE2YYRnd'
        'jfHGRmOrUQAWpxQsTmuzvTnATDKnmyvMm2axedcsNX9lNbeVJPgLKLS9u3YcKC026piRpmmOMjPNNSax4q0eVi9r'
        'lLXI+sDabB2zCq2m9lD7nv0svUi/piZ7jY1n01guy2N/ZF+zcN6IYz6QL+C/41+AfeggeoheYpAYI9JFpsgVq8U+'
        'cUAcF7fFf0S5iAJLwZzOTk+wEylOmpPpzHLeceY7i51cZ4Oz3TkCtqK52859wZ3kvuX+wf3GvS95DR5bubVcfRSJ'
        '2iMDZP8SuoFKUIQapTZTO6lEtcFKDFaT1eXqauDzbvUKWIYoLVq7p5VpYd440AoTLHAv3B8Pw1dAbmJJHEGEEItw'
        'Ek+6kQTSiySSgWQquUqKSRlR9DA9Qh8B8pKmZwCnp4Hu54Du5+nr9S36DrD9p/S6xlPA8+8My/zEfMbSrS6gI6lW'
        'hjXVyrU+BN5ttz6yTlpFYNHvWq3tF+wu9hg7055tL7fX2DvsvXYkjaMD6AI6is1nK9gGdpp9xW6wEhbFY/lwngJ2'
        'QmrWUrC9hWB7G4hYwUVP0U+8KXLEUvE+cDbSlbOJeODNEHQdLdRScHuykewiM0G2Wxi6Mcj4k0HN7uYQc6W5G1p5'
        '0WxlJcCIM8WaY620DsJo09X+1D5kx0G/xYgWoqmIFPVFqCjj3/IS/k9+nV/mRVB7AT/C8/knfDe0ZRNfx9fwFXwJ'
        '9Hk2n8mz+EQYI5J5Eh8KctCHJ4CF49zgiHfk7XgbHs2f4Q15GFf4A7AWd9gtdpVdYueB2uPsEPsz2wujzlb2e7YW'
        'xp9ctpjNA2s5nU1hGSyVjWLDYYTqz3qxHiyeUUZYJxbLfs5asZ+xKBbB6rKHtJTepcX0Jr1CL9Cz9CQ9Sg/S/XQP'
        '3UG30PU0D0a/ZTSHzqU+sMaZNJ2OoSPoMDqYJtKX5V+yYPt/uSl3oZ1BsuAJwAf2Yx7JIUvgOWAVWUPWgjXZRLaS'
        'HaTILoFx9rJdZuvmenMHjNeh9LodZyMYgf//yPZTh49L76f6rxp4c5ohMG8O9ZsCn/wvOOw56Kvr7z5fPW8vJsUn'
        'zx+Av+j5vPWZ8NvQPyHyycX10SH+eXdFuSHeXBhVyffvM6b4EmrAl7CkGvDPAn5WDfm7IX9JLeVsqgF/FeDn14Jf'
        'VAP+EcAvqQHfW/sRWpWP/vzoWvJja8m3aslPqCV/SC35Y2rJz6olfx7kXy9UFBLkIos+6/VrRTgH6SCHxq2/wtqU'
        'IHBaEcwjgtJJcGREj+B95770phqPw94v/b6SR6GgSjpc6au8Ck7KrvDprvSCubtMd1F6w8NAfe+6m/ISwMEfrvSE'
        '90Yy5yXA7av0CaR6w319lEGKfPNUT5E75cp9yeKDvTW1OAzLq24yVkMIL68lwuFw4fCAGV4KsQQiHD0cDt6DcJ+k'
        'Sa7HkO/NnxzKnxAkxhNjCERwsEDDypXGENtCdCHOgih9HRDreL4gcEZIh4V0YkiHBsRY/9TQiyGBFvt+yMkGX77A'
        'hS+QyM/Phx+f4nEz0Cp5UR5ISDLAP/L+HQg/9hf8JgXgK9nyY38ry0WAztCfns5aQsP8yumCgsrggoJKCAUniwsq'
        'gYEB+cHgO3eCECQ4GEGCgxD84AoEP/gxwiPwI4RH4ABCBdiPUAH2EILBEqFquip+tfKq1letPVXbW42eqvRW40dV'
        'flXjZ1V+V+uPqv1VrT8r6/ND0Kwy0LBS0LQS0LgroHn5oIG+EE9hwXTMAtPhguloC6ajccB0yB02Q2SU11f8ZqWx'
        'xJG48h5PymQZsixZpixb1iHrknUGBDAKVlj0UMYqacpor03JkO6qjFcmK+lKJoz9XeF7IkB/qXgngSgpAO8G+PB6'
        'FLBSYH2E/ATjdfOGP782hIK97aBkAX6mlx4Nqc7KCK/8TK+8fOWopzn1wNI3haeJRC9X2moMqyRiKtUli+2tDJCr'
        'oyAvXZnkXY2GesdCu703N97tYYAXC6uEJI2zwd5PVsZBnRKPBjj/toenAaYGK4WOAebbj/23vkAZ0cpwKOU4wOZU'
        'ePQ9WBpAO8H6oRDJYmjrKxXe3wA82oOHevCuXltHQv1pQX3fBHJjgIJMr/XpQF0M0JgM35JXk7zUVMj/4dDUo9sH'
        'Niol8OYqBO6M90YzOXKHBka2mJgoz5LJ0frJp3v+D7zvExg='),
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
        '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'),
}

if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkpe_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT')
with open(sys.argv[2], 'wb') as f:
    f.write(zlib.decompress(base64.b64decode(PROGRAMS[sys.argv[1]])))
