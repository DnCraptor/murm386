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

OW32X (OW32X.EXE, wcc386 -za99 -bt=nt, wlink system nt_win): structured
    exceptions (Open Watcom's _try, _except, _finally): "div: caught
    C0000094", "raise: E0000001 43", "finally: 1", "continue: 1234" (INT 3,
    a filter's EXCEPTION_CONTINUE_EXECUTION with the CONTEXT's EIP and EAX
    changed), "cli: C0000096", "deep: E0000002"; U its top-level filter's
    EXCEPTION_EXECUTE_HANDLER (it ends), Z none: "OW32X caused a Divide by
    Zero in module OW32X.EXE at OW32X+00001357."

OW32W (OW32W.EXE, ..., winmm): WINMM's devices on HOSTWAVE.DLL and
    HOSTMIDI.DLL (it names them in SYSTEM.INI): "caps 0 'SB16 (murm386)'
    00000FFF ch 2, query 0, open 0 (1)", "function: done 2, position 8820
    (type 4)", "mmio: 11025 Hz 8 bits, data 4410", "mci: open 0 id 2, length
    400 (0), play 0 notify 1, close 0", "midi: 1 devs, caps 0 'Host MIDI' 16
    voices, open 0, note 0", "window: done 1, header ours"; T a multimedia
    timer: "timer: ticks (11)"
OW32H (OW32H.EXE, ..., ow32h.rc, comdlg32): COMDLG32's hooks and templates,
    as OWHOOK's (its FILEHOOK, FINDHOOK, PRINTHOOK): "open: 1 cust 1234
    init 1 ok 2 sel 1 preview 1 help 1 'C:\\HOSTWIN\\README.TXT'", "color: 1
    FF0000 ok FF0000", "font: 1 'System', got 1 'System'", "find: 'hooked'
    next cust 5678, closed", "print: 1 init 1 setup 1 copies 2"
OW32D (OW32D.EXE, ...): USER32's DDEML, a server and a client instance of
    its own: "init 0 0, ids 2 3, service 1 'OW32D' 5", "connect 1 (asked 1,
    confirmed 1), same 1", "request 1: 'hello from the server' 22, access
    'hello from the server' 22", "poke 1 'poked text', execute 1
    '[run(1)]'", "info 92: cb 92, topic 1, status 15, requests 1",
    "disconnect 1, uninit 1 1, error 16387"
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
    'OW32X': (
# /* OW32X: structured exceptions of a Win32 program on WINN: a division by
#  * zero, RaiseException and its arguments, __finally, a handler going on
#  * where it was (EXCEPTION_CONTINUE_EXECUTION, the CONTEXT changed), a
#  * privileged instruction, an exception in a called function, the
#  * program's top-level filter; U: an exception none handles. */
# #include <windows.h>
# #include <stdio.h>
# #include <string.h>
#
# static char line[8][120];
# static volatile int zero;
# static int finally_ran, filter_calls;
#
# static int div_filter(DWORD code)
# {
#     return code == EXCEPTION_INT_DIVIDE_BY_ZERO ? EXCEPTION_EXECUTE_HANDLER :
#            EXCEPTION_CONTINUE_SEARCH;
# }
#
# /* past the INT 3 (one byte), and on */
# static int skip_filter(EXCEPTION_POINTERS *p)
# {
#     if (p->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
#         return EXCEPTION_CONTINUE_SEARCH;
#     p->ContextRecord->Eip++;
#     p->ContextRecord->Eax = 1234;
#     return EXCEPTION_CONTINUE_EXECUTION;
# }
#
# static void deep(int n)
# {
#     if (n)
#         deep(n - 1);
#     else
#         RaiseException(0xe0000002, 0, 0, NULL);
# }
#
# static LONG WINAPI top(EXCEPTION_POINTERS *p)
# {
#     filter_calls++;
#     return EXCEPTION_EXECUTE_HANDLER;
# }
#
# static void tests(void)
# {
#     int r = 0;
#     DWORD code = 0;
#     ULONG_PTR arg = 0;
#     _try {
#         r = 10 / zero;
#         strcpy(line[0], "div: not caught");
#     } _except (div_filter(GetExceptionCode())) {
#         sprintf(line[0], "div: caught %08lX", (unsigned long)GetExceptionCode());
#     }
#     _try {
#         ULONG_PTR a[2] = { 42, 43 };
#         RaiseException(0xe0000001, 0, 2, a);
#     } _except (code = GetExceptionCode(),
#                arg = ((EXCEPTION_POINTERS *)GetExceptionInformation())->ExceptionRecord->ExceptionInformation[1],
#                EXCEPTION_EXECUTE_HANDLER) {
#         sprintf(line[1], "raise: %08lX %lu", (unsigned long)code, (unsigned long)arg);
#     }
#     _try {
#         _try {
#             RaiseException(0xe0000003, 0, 0, NULL);
#         } _finally {
#             finally_ran = 1;
#         }
#     } _except (EXCEPTION_EXECUTE_HANDLER) {
#         sprintf(line[2], "finally: %d", finally_ran);
#     }
#     int got = 0;
#     _try {
#         _asm {
#             mov eax, 0
#             int 3
#             mov got, eax
#         }
#     } _except (skip_filter(GetExceptionInformation())) {
#         strcpy(line[3], "continue: not continued");
#     }
#     if (!line[3][0])
#         sprintf(line[3], "continue: %d", got);
#     _try {
#         _asm cli
#         strcpy(line[4], "cli: not caught");
#     } _except (EXCEPTION_EXECUTE_HANDLER) {
#         sprintf(line[4], "cli: %08lX", (unsigned long)GetExceptionCode());
#     }
#     _try {
#         deep(5);
#     } _except (EXCEPTION_EXECUTE_HANDLER) {
#         sprintf(line[5], "deep: %08lX", (unsigned long)GetExceptionCode());
#     }
#     (void)r;
# }
#
# static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
# {
#     switch (m) {
#     case WM_CREATE:
#         tests();
#         return 0;
#     case WM_CHAR:
#         if (wp == 'u') {
#             SetUnhandledExceptionFilter(top);
#             sprintf(line[6], "top filter set");
#             InvalidateRect(h, NULL, TRUE);
#             UpdateWindow(h);
#             RaiseException(0xe0000004, 0, 0, NULL);
#         } else if (wp == 'z') {
#             SetUnhandledExceptionFilter(NULL);
#             zero = 10 / zero;
#         }
#         return 0;
#     case WM_PAINT: {
#         PAINTSTRUCT ps;
#         HDC dc = BeginPaint(h, &ps);
#         for (int i = 0; i < 8; i++)
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
#     wc.lpszClassName = "Ow32xClass";
#     RegisterClass(&wc);
#     HWND h = CreateWindow("Ow32xClass", "OW32X - exceptions", WS_OVERLAPPEDWINDOW, 0, 0, 500,
#                           200, NULL, NULL, inst, NULL);
#     ShowWindow(h, show);
#     MSG m;
#     while (GetMessage(&m, NULL, 0, 0)) {
#         TranslateMessage(&m);
#         DispatchMessage(&m);
#     }
#     return 0;
# }
        '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'
        '73/8L4nrW9A='
),
    'OW32W': (
# /* OW32W: WINMM's devices for a Win32 program on WINN: the wave device's
#  * capabilities, two headers written (a function callback, a window's
#  * MM_WOM_DONE), the position; a WAV file read by mmio (RIFF, fmt, data);
#  * MCI's waveaudio by mciSendCommand (open, status, play with wait,
#  * close); a MIDI note; a multimedia timer. */
# #include <windows.h>
# #include <mmsystem.h>
# #include <stdio.h>
# #include <string.h>
# #include <math.h>
#
# #define RATE 11025
# #define SAMPLES 4410
#
# static char line[8][120];
# static unsigned char pcm[2][SAMPLES];
# static WAVEHDR hdr[2];
# static volatile int fn_done, win_done, opened, ticks;
# static HWND wnd;
# static int notified;
#
# static void CALLBACK wave_proc(HWAVEOUT h, UINT msg, DWORD inst, DWORD p1, DWORD p2)
# {
#     if (msg == WOM_OPEN && inst == 0x1234)
#         opened++;
#     if (msg == WOM_DONE && ((WAVEHDR *)p1)->dwFlags & WHDR_DONE)
#         fn_done++;
# }
#
# static void CALLBACK tick(UINT id, UINT msg, DWORD user, DWORD a, DWORD b)
# {
#     ticks += (int)user;
# }
#
# static void make_wav(void)
# {
#     HFILE f = _lcreat("OW32W.WAV", 0);
#     DWORD n = SAMPLES, fmtlen = 16, riff = 4 + 8 + 16 + 8 + SAMPLES;
#     PCMWAVEFORMAT fmt = { { WAVE_FORMAT_PCM, 1, RATE, RATE, 1 }, 8 };
#     _lwrite(f, "RIFF", 4);
#     _lwrite(f, (char *)&riff, 4);
#     _lwrite(f, "WAVEfmt ", 8);
#     _lwrite(f, (char *)&fmtlen, 4);
#     _lwrite(f, (char *)&fmt, 16);
#     _lwrite(f, "data", 4);
#     _lwrite(f, (char *)&n, 4);
#     _lwrite(f, (char *)pcm[0], SAMPLES);
#     _lclose(f);
# }
#
# static void mmio(void)
# {
#     HMMIO h = mmioOpen("OW32W.WAV", NULL, MMIO_READ);
#     MMCKINFO riff, ck;
#     PCMWAVEFORMAT fmt;
#     LONG got = 0;
#     memset(&fmt, 0, sizeof(fmt));
#     riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
#     if (h && !mmioDescend(h, &riff, NULL, MMIO_FINDRIFF)) {
#         ck.ckid = mmioFOURCC('f', 'm', 't', ' ');
#         if (!mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK)) {
#             mmioRead(h, (HPSTR)&fmt, sizeof(fmt));
#             mmioAscend(h, &ck, 0);
#         }
#         ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
#         if (!mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK))
#             got = (LONG)ck.cksize;
#     }
#     if (h)
#         mmioClose(h, 0);
#     sprintf(line[2], "mmio: %lu Hz %u bits, data %ld", (unsigned long)fmt.wf.nSamplesPerSec,
#             fmt.wBitsPerSample, got);
# }
#
# static void mci(void)
# {
#     MCI_OPEN_PARMS open;
#     MCI_STATUS_PARMS st;
#     MCI_PLAY_PARMS play;
#     memset(&open, 0, sizeof(open));
#     open.lpstrDeviceType = "waveaudio";
#     open.lpstrElementName = "OW32W.WAV";
#     DWORD r = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, (DWORD)&open);
#     memset(&st, 0, sizeof(st));
#     st.dwItem = MCI_STATUS_LENGTH;
#     DWORD rs = r ? r : mciSendCommand(open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)&st);
#     memset(&play, 0, sizeof(play));
#     play.dwCallback = (DWORD)wnd;
#     DWORD rp = r ? r : mciSendCommand(open.wDeviceID, MCI_PLAY, MCI_NOTIFY, (DWORD)&play);
#     MSG m;                                  /* MCI_NOTIFY: MM_MCINOTIFY when it is played */
#     while (!rp && GetMessage(&m, NULL, 0, 0) && m.message != MM_MCINOTIFY)
#         DispatchMessage(&m);
#     notified = !rp && m.message == MM_MCINOTIFY ? (int)m.wParam : -1;
#     DWORD rc = r ? r : mciSendCommand(open.wDeviceID, MCI_CLOSE, 0, 0);
#     sprintf(line[3], "mci: open %lu id %u, length %lu (%lu), play %lu notify %d, close %lu",
#             (unsigned long)r, open.wDeviceID, (unsigned long)st.dwReturn, (unsigned long)rs,
#             (unsigned long)rp, notified, (unsigned long)rc);
# }
#
# static void midi(void)
# {
#     HMIDIOUT h = 0;
#     MIDIOUTCAPS caps;
#     memset(&caps, 0, sizeof(caps));
#     UINT n = midiOutGetNumDevs();
#     MMRESULT c = midiOutGetDevCaps(0, &caps, sizeof(caps));
#     MMRESULT r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
#     MMRESULT s = r ? r : midiOutShortMsg(h, 0x007f3c90);
#     if (!r) {
#         Sleep(100);
#         midiOutShortMsg(h, 0x00003c80);
#         midiOutClose(h);
#     }
#     sprintf(line[4], "midi: %u devs, caps %u '%s' %u voices, open %u, note %u", n, c,
#             caps.szPname, caps.wVoices, r, s);
# }
#
# static void wave(void)
# {
#     HWAVEOUT h = 0;
#     WAVEOUTCAPS caps;
#     WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 1, RATE, RATE, 1, 8, 0 };
#     memset(&caps, 0, sizeof(caps));
#     MMRESULT c = waveOutGetDevCaps(WAVE_MAPPER, &caps, sizeof(caps));
#     MMRESULT q = waveOutOpen(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_FORMAT_QUERY);
#     MMRESULT r = waveOutOpen(&h, WAVE_MAPPER, &wf, (DWORD)wave_proc, 0x1234, CALLBACK_FUNCTION);
#     sprintf(line[0], "caps %u '%s' %08lX ch %u, query %u, open %u (%d)", c, caps.szPname,
#             (unsigned long)caps.dwFormats, caps.wChannels, q, r, opened);
#     if (r)
#         return;
#     for (int i = 0; i < 2; i++) {
#         memset(&hdr[i], 0, sizeof(hdr[i]));
#         hdr[i].lpData = (LPSTR)pcm[i];
#         hdr[i].dwBufferLength = SAMPLES;
#         waveOutPrepareHeader(h, &hdr[i], sizeof(hdr[i]));
#         waveOutWrite(h, &hdr[i], sizeof(hdr[i]));
#     }
#     MMTIME t;
#     t.wType = TIME_BYTES;
#     DWORD start = GetTickCount();
#     while (fn_done < 2 && GetTickCount() - start < 3000) {
#         MSG m;
#         if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
#             DispatchMessage(&m);
#     }
#     waveOutGetPosition(h, &t, sizeof(t));
#     for (int i = 0; i < 2; i++)
#         waveOutUnprepareHeader(h, &hdr[i], sizeof(hdr[i]));
#     waveOutClose(h);
#     sprintf(line[1], "function: done %d, position %lu (type %u)", fn_done,
#             (unsigned long)t.u.cb, t.wType);
# }
#
# /* a window's callback (MM_WOM_DONE) */
# static void wave_window(void)
# {
#     HWAVEOUT h = 0;
#     WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 1, RATE, RATE, 1, 8, 0 };
#     MMRESULT r = waveOutOpen(&h, 0, &wf, (DWORD)wnd, 0, CALLBACK_WINDOW);
#     if (!r) {
#         hdr[0].dwFlags = 0;
#         waveOutPrepareHeader(h, &hdr[0], sizeof(hdr[0]));
#         waveOutWrite(h, &hdr[0], sizeof(hdr[0]));
#     }
#     sprintf(line[5], "window: open %u", r);
# }
#
# static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
# {
#     switch (m) {
#     case MM_WOM_DONE: {
#         WAVEHDR *p = (WAVEHDR *)lp;
#         win_done++;
#         sprintf(line[5], "window: done %d, header %s", win_done, p == &hdr[0] ? "ours" : "?");
#         waveOutUnprepareHeader((HWAVEOUT)wp, p, sizeof(*p));
#         waveOutClose((HWAVEOUT)wp);
#         InvalidateRect(h, NULL, TRUE);
#         return 0;
#     }
#     case WM_CHAR:
#         if (wp == 't') {
#             UINT id = timeSetEvent(50, 10, tick, 1, TIME_PERIODIC);
#             DWORD start = GetTickCount();
#             while (GetTickCount() - start < 600) {
#                 MSG msg;
#                 if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
#                     DispatchMessage(&msg);
#             }
#             timeKillEvent(id);
#             sprintf(line[6], "timer: %s (%d)", ticks >= 5 ? "ticks" : "few", ticks);
#             InvalidateRect(h, NULL, TRUE);
#         }
#         return 0;
#     case WM_PAINT: {
#         PAINTSTRUCT ps;
#         HDC dc = BeginPaint(h, &ps);
#         for (int i = 0; i < 8; i++)
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
#     for (int i = 0; i < SAMPLES; i++) {
#         pcm[0][i] = (unsigned char)(128 + 60 * sin(i * 2 * 3.14159 * 440 / RATE));
#         pcm[1][i] = (unsigned char)(128 + 60 * sin(i * 2 * 3.14159 * 660 / RATE));
#     }
#     memset(&wc, 0, sizeof(wc));
#     wc.lpfnWndProc = wndproc;
#     wc.hInstance = inst;
#     wc.hCursor = LoadCursor(NULL, IDC_ARROW);
#     wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
#     wc.lpszClassName = "Ow32wClass";
#     RegisterClass(&wc);
#     wnd = CreateWindow("Ow32wClass", "OW32W - WINMM", WS_OVERLAPPEDWINDOW, 0, 0, 600, 180,
#                        NULL, NULL, inst, NULL);
#     ShowWindow(wnd, show);
#     UpdateWindow(wnd);
#     WritePrivateProfileString("drivers", "wave", "HOSTWAVE.DLL", "SYSTEM.INI");
#     WritePrivateProfileString("drivers", "midi", "HOSTMIDI.DLL", "SYSTEM.INI");
#     make_wav();
#     wave();
#     mmio();
#     mci();
#     midi();
#     wave_window();
#     InvalidateRect(wnd, NULL, TRUE);
#     MSG m;
#     while (GetMessage(&m, NULL, 0, 0)) {
#         TranslateMessage(&m);
#         DispatchMessage(&m);
#     }
#     return 0;
# }
        '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'
),
    'OW32H': (
# /* OW32H: COMDLG32's hooks and templates from a Win32 program on WINN: Open
#  * with its own template (an extra check box and text) and a hook
#  * (WM_INITDIALOG and its lCustData, LBSELCHSTRING, FILEOKSTRING refused
#  * once, the check box's WM_COMMAND), Help (HELPMSGSTRING to the owner with
#  * its structure); Color with a hook (SETRGBSTRING at WM_INITDIALOG,
#  * COLOROKSTRING); Font with a hook (WM_CHOOSEFONT_GETLOGFONT); Find with
#  * its own template and a hook (the text set at WM_INITDIALOG, FINDMSGSTRING
#  * to the owner); Print with its own template and a hook, Print Setup (its
#  * button) with a hook. */
# #include <windows.h>
# #include <commdlg.h>
# #include <dlgs.h>
# #include <stdio.h>
# #include <string.h>
#
# #define ID_PREVIEW 200
# #define ID_NOTE    201
#
# static char line[6][160];
# static HINSTANCE inst;
# static UINT fileok, lbselch, help, colorok, setrgb, findmsg;
# static OPENFILENAME ofn;
# static char file[MAX_PATH];
# static int inits, oks, sels, previews, helps;
# static LPARAM cust;
# static COLORREF rgb_ok;
# static LOGFONT got;
# static int got_ok;
# static FINDREPLACE fr;
# static char what[64];
# static LPARAM find_cust;
# static HWND find_dlg;
# static int pinit, psetup;
#
# static UINT CALLBACK file_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_INITDIALOG) {
#         inits++;
#         cust = ((OPENFILENAME *)lp)->lCustData;
#         SetDlgItemText(dlg, ID_NOTE, "hooked");
#         return TRUE;
#     }
#     if (msg == fileok) {
#         oks++;
#         strcpy(file, ((OPENFILENAME *)lp)->lpstrFile);
#         if (oks == 1) {
#             SetDlgItemText(dlg, ID_NOTE, "again");
#             return TRUE;
#         }
#         return FALSE;
#     }
#     if (msg == lbselch && wp == lst1)
#         sels++;
#     if (msg == WM_COMMAND && LOWORD(wp) == ID_PREVIEW)
#         previews++;
#     return FALSE;
# }
#
# static UINT CALLBACK color_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_INITDIALOG) {
#         SendMessage(dlg, setrgb, 0, RGB(0, 0, 255));
#         return TRUE;
#     }
#     if (msg == colorok)
#         rgb_ok = ((CHOOSECOLOR *)lp)->rgbResult;
#     return FALSE;
# }
#
# static UINT CALLBACK font_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_INITDIALOG) {
#         got_ok = (int)SendMessage(dlg, WM_CHOOSEFONT_GETLOGFONT, 0, (LPARAM)&got);
#         return TRUE;
#     }
#     return FALSE;
# }
#
# static UINT CALLBACK find_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_INITDIALOG) {
#         find_cust = ((FINDREPLACE *)lp)->lCustData;
#         SetDlgItemText(dlg, edt1, "hooked");
#         return TRUE;
#     }
#     return FALSE;
# }
#
# static UINT CALLBACK print_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_INITDIALOG) {
#         pinit += ((PRINTDLG *)lp)->lCustData == 0x4321;
#         SetDlgItemText(dlg, ID_NOTE, "hooked");
#         return TRUE;
#     }
#     return FALSE;
# }
#
# static UINT CALLBACK setup_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == WM_INITDIALOG) {
#         psetup++;
#         return TRUE;
#     }
#     return FALSE;
# }
#
# static void print(HWND h)
# {
#     PRINTDLG pd;
#     WriteProfileString("devices", "Epson LX-350", "EPSON9,OW32H.PRN");
#     WriteProfileString("windows", "device", "Epson LX-350,EPSON9,OW32H.PRN");
#     memset(&pd, 0, sizeof(pd));
#     pd.lStructSize = sizeof(pd);
#     pd.hwndOwner = h;
#     pd.hInstance = inst;
#     pd.Flags = PD_ENABLEPRINTHOOK | PD_ENABLESETUPHOOK | PD_ENABLEPRINTTEMPLATE |
#                PD_NOSELECTION;
#     pd.nFromPage = 1;
#     pd.nToPage = 1;
#     pd.nMinPage = 1;
#     pd.nMaxPage = 9;
#     pd.nCopies = 1;
#     pd.lCustData = 0x4321;
#     pd.lpfnPrintHook = print_hook;
#     pd.lpfnSetupHook = setup_hook;
#     pd.lpPrintTemplateName = "PRINTHOOK";
#     BOOL ok = PrintDlg(&pd);
#     sprintf(line[4], "print: %d init %d setup %d copies %d", ok, pinit, psetup, pd.nCopies);
# }
#
# static void open_file(HWND h)
# {
#     memset(&ofn, 0, sizeof(ofn));
#     strcpy(file, "*.txt");
#     ofn.lStructSize = sizeof(ofn);
#     ofn.hwndOwner = h;
#     ofn.hInstance = inst;
#     ofn.lpstrFilter = "Text (*.TXT)\0*.txt\0All (*.*)\0*.*\0";
#     ofn.nFilterIndex = 1;
#     ofn.lpstrFile = file;
#     ofn.nMaxFile = sizeof(file);
#     ofn.Flags = OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_SHOWHELP | OFN_HIDEREADONLY;
#     ofn.lCustData = 0x1234;
#     ofn.lpfnHook = file_hook;
#     ofn.lpTemplateName = "FILEHOOK";
#     BOOL ok = GetOpenFileName(&ofn);
#     sprintf(line[0], "open: %d cust %lx init %d ok %d sel %d preview %d help %d '%s'", ok,
#             (long)cust, inits, oks, sels, previews, helps, file);
# }
#
# static void color(HWND h)
# {
#     static COLORREF custom[16];
#     CHOOSECOLOR cc;
#     memset(&cc, 0, sizeof(cc));
#     cc.lStructSize = sizeof(cc);
#     cc.hwndOwner = h;
#     cc.lpCustColors = custom;
#     cc.Flags = CC_ENABLEHOOK | CC_RGBINIT;
#     cc.lpfnHook = color_hook;
#     BOOL ok = ChooseColor(&cc);
#     sprintf(line[1], "color: %d %06lX ok %06lX", ok, (unsigned long)cc.rgbResult,
#             (unsigned long)rgb_ok);
# }
#
# static void font(HWND h)
# {
#     LOGFONT lf;
#     CHOOSEFONT cf;
#     memset(&lf, 0, sizeof(lf));
#     strcpy(lf.lfFaceName, "System");
#     memset(&cf, 0, sizeof(cf));
#     cf.lStructSize = sizeof(cf);
#     cf.hwndOwner = h;
#     cf.lpLogFont = &lf;
#     cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_ENABLEHOOK;
#     cf.lpfnHook = font_hook;
#     BOOL ok = ChooseFont(&cf);
#     sprintf(line[2], "font: %d '%s', got %d '%s'", ok, lf.lfFaceName, got_ok, got.lfFaceName);
# }
#
# static void find(HWND h)
# {
#     memset(&fr, 0, sizeof(fr));
#     strcpy(what, "abc");
#     fr.lStructSize = sizeof(fr);
#     fr.hwndOwner = h;
#     fr.hInstance = inst;
#     fr.Flags = FR_DOWN | FR_ENABLEHOOK | FR_ENABLETEMPLATE;
#     fr.lpstrFindWhat = what;
#     fr.wFindWhatLen = sizeof(what);
#     fr.lCustData = 0x5678;
#     fr.lpfnHook = find_hook;
#     fr.lpTemplateName = "FINDHOOK";
#     find_dlg = FindText(&fr);
#     if (!find_dlg)
#         strcpy(line[3], "find: none");
# }
#
# static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
# {
#     if (msg == help && help) {
#         helps += (OPENFILENAME *)lp == &ofn;
#         return 0;
#     }
#     if (msg == findmsg && findmsg) {
#         FINDREPLACE *f = (FINDREPLACE *)lp;
#         if (f->Flags & FR_DIALOGTERM) {
#             strcat(line[3], ", closed");
#             find_dlg = NULL;
#         } else {
#             sprintf(line[3], "find: '%s' %s cust %lx", f->lpstrFindWhat,
#                     f == &fr && (f->Flags & FR_FINDNEXT) ? "next" : "?", (long)find_cust);
#         }
#         InvalidateRect(h, NULL, TRUE);
#         return 0;
#     }
#     switch (msg) {
#     case WM_CHAR:
#         if (wp == 'o') open_file(h);
#         else if (wp == 'c') color(h);
#         else if (wp == 'f') font(h);
#         else if (wp == 'r') find(h);
#         else if (wp == 'p') print(h);
#         InvalidateRect(h, NULL, TRUE);
#         return 0;
#     case WM_PAINT: {
#         PAINTSTRUCT ps;
#         HDC dc = BeginPaint(h, &ps);
#         for (int i = 0; i < 6; i++)
#             TextOut(dc, 4, 4 + 16 * i, line[i], (int)strlen(line[i]));
#         EndPaint(h, &ps);
#         return 0;
#     }
#     case WM_DESTROY:
#         PostQuitMessage(0);
#         return 0;
#     }
#     return DefWindowProc(h, msg, wp, lp);
# }
#
# int WINAPI WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmd, int show)
# {
#     WNDCLASS wc;
#     MSG m;
#     inst = hinst;
#     fileok = RegisterWindowMessage(FILEOKSTRING);
#     lbselch = RegisterWindowMessage(LBSELCHSTRING);
#     help = RegisterWindowMessage(HELPMSGSTRING);
#     colorok = RegisterWindowMessage(COLOROKSTRING);
#     setrgb = RegisterWindowMessage(SETRGBSTRING);
#     findmsg = RegisterWindowMessage(FINDMSGSTRING);
#     memset(&wc, 0, sizeof(wc));
#     wc.lpfnWndProc = wndproc;
#     wc.hInstance = hinst;
#     wc.hCursor = LoadCursor(NULL, IDC_ARROW);
#     wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
#     wc.lpszClassName = "Ow32hClass";
#     RegisterClass(&wc);
#     HWND h = CreateWindow("Ow32hClass", "OW32H - hooks and templates", WS_OVERLAPPEDWINDOW, 0, 0,
#                           600, 140, NULL, NULL, hinst, NULL);
#     ShowWindow(h, show);
#     UpdateWindow(h);
#     while (GetMessage(&m, NULL, 0, 0)) {
#         if (find_dlg && IsDialogMessage(find_dlg, &m))
#             continue;
#         TranslateMessage(&m);
#         DispatchMessage(&m);
#     }
#     return (int)m.wParam;
# }
# ---- ow32h.rc
# #include <windows.h>
# #include <dlgs.h>
#
# FILEHOOK DIALOG 20, 20, 264, 150
# STYLE WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME
# CAPTION "Open (hooked)"
# FONT 8, "Helv"
# BEGIN
#     LTEXT "File &Name:", stc3, 6, 6, 76, 9
#     EDITTEXT edt1, 6, 16, 90, 12, ES_AUTOHSCROLL
#     LISTBOX lst1, 6, 32, 90, 68, LBS_SORT | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP
#     LTEXT "&Directories:", -1, 110, 6, 92, 9
#     LTEXT "", stc1, 110, 16, 92, 9, SS_NOPREFIX
#     LISTBOX lst2, 110, 32, 92, 68, LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP
#     LTEXT "List Files of &Type:", stc2, 6, 104, 90, 9
#     COMBOBOX cmb1, 6, 114, 90, 36, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
#     LTEXT "Dri&ves:", stc4, 110, 104, 92, 9
#     COMBOBOX cmb2, 110, 114, 92, 68, CBS_DROPDOWNLIST | CBS_SORT | WS_VSCROLL | WS_TABSTOP
#     DEFPUSHBUTTON "OK", IDOK, 208, 6, 50, 14
#     PUSHBUTTON "Cancel", IDCANCEL, 208, 24, 50, 14
#     PUSHBUTTON "&Help", pshHelp, 208, 46, 50, 14
#     CHECKBOX "&Read Only", chx1, 208, 68, 50, 12, BS_AUTOCHECKBOX | WS_TABSTOP
#     CHECKBOX "&Preview", 200, 6, 134, 60, 12, BS_AUTOCHECKBOX | WS_TABSTOP
#     LTEXT "", 201, 110, 136, 92, 9
# END
#
# FINDHOOK DIALOG 30, 73, 236, 62
# STYLE WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE
# CAPTION "Find (hooked)"
# FONT 8, "Helv"
# BEGIN
#     LTEXT "Fi&nd What:", -1, 4, 8, 42, 8
#     EDITTEXT edt1, 47, 7, 128, 12, ES_AUTOHSCROLL | WS_GROUP
#     CHECKBOX "Match &Whole Word Only", chx1, 4, 26, 100, 12, BS_AUTOCHECKBOX | WS_GROUP | WS_TABSTOP
#     CHECKBOX "Match &Case", chx2, 4, 42, 64, 12, BS_AUTOCHECKBOX | WS_TABSTOP
#     GROUPBOX "Direction", -1, 107, 26, 68, 28, WS_GROUP
#     CONTROL "&Up", rad1, "BUTTON", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 111, 38, 25, 12
#     CONTROL "&Down", rad2, "BUTTON", BS_AUTORADIOBUTTON, 138, 38, 35, 12
#     DEFPUSHBUTTON "&Find Next", IDOK, 182, 5, 50, 14, WS_GROUP
#     PUSHBUTTON "Cancel", IDCANCEL, 182, 23, 50, 14
#     PUSHBUTTON "&Help", pshHelp, 182, 45, 50, 14
# END
#
# PRINTHOOK DIALOG 20, 20, 268, 120
# STYLE WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME
# CAPTION "Print (hooked)"
# FONT 8, "Helv"
# BEGIN
#     LTEXT "", stc1, 6, 6, 200, 10, SS_NOPREFIX
#     GROUPBOX "Print Range", -1, 6, 20, 150, 62
#     CONTROL "&All", rad1, "BUTTON", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 12, 31, 100, 10
#     CONTROL "S&election", rad2, "BUTTON", BS_AUTORADIOBUTTON, 12, 43, 100, 10
#     CONTROL "&Pages", rad3, "BUTTON", BS_AUTORADIOBUTTON, 12, 55, 100, 10
#     RTEXT "&From:", -1, 22, 68, 20, 8
#     EDITTEXT edt1, 44, 66, 30, 12, ES_AUTOHSCROLL | WS_GROUP
#     RTEXT "&To:", -1, 78, 68, 14, 8
#     EDITTEXT edt2, 94, 66, 30, 12, ES_AUTOHSCROLL
#     LTEXT "&Copies:", -1, 6, 90, 40, 8
#     EDITTEXT edt3, 48, 88, 30, 12, ES_AUTOHSCROLL
#     CHECKBOX "Print to Fi&le", chx1, 90, 89, 80, 10, BS_AUTOCHECKBOX | WS_TABSTOP
#     DEFPUSHBUTTON "OK", IDOK, 212, 6, 50, 14, WS_GROUP
#     PUSHBUTTON "Cancel", IDCANCEL, 212, 24, 50, 14
#     PUSHBUTTON "&Setup...", psh1, 212, 46, 50, 14
#     LTEXT "", 201, 6, 106, 100, 9
# END
# wcc386 -q -za99 -bt=nt ow32h.c; wrc -q -r -bt=nt ow32h.rc
# wlink system nt_win name OW32H.EXE file ow32h.o library comdlg32.lib option resource=ow32h.res
        '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'
        'O6J09sR99dNm7rtwlJX6MGo6WJQYsHz9BDmMlsR7tVxRUVs44ttpiB34GK6dzfO7IUoeKSJiq5sWz7PJ2TJ4L2Ww'
        'y8eoTVG6bqbWtN1l03pO+qx3Ftlbgpp1jm85YVLL3bxzr6Y5m3d2aBAhfla0tItbJH1gb/m685Su5Qhz5F+iTEpQ'
        'f/l5VHwrVhS0DpDvAUuL7+3ywV+4RDoi9SyZuT/nJe7r3y1sCWgOTOIEQ0sW7+zWNM/jnT2aJE6U75eG7l9O5S2R'
        'egAb1fH17y4czJF670fcwHXA6ocReGFtWbVxSYmwsmatMf2u9JkzoyKNeWL17ILKtWXG+o31QtnaOcb0mtqNdZWr'
        'KgTj9JUzjOOy1FQLdZUPiEJNXb0x0myOnB1pjoydY7TX1AmVNdX1vlnTZxjzNz5QUl82y5hTvXKO0ZIQHz+b8swZ'
        'v150ntFJ3dL7ENFOmyOBE/zS3N8w+dDa8wpXn/OEvQRJQmOeXmfj1PjVGrwM9tXnaJ4PDwkaByMNDSzFdNRKWxau'
        'NQ9CKB187Jj8HOIgTwHSAemo2yX1DkSre7Cd0yENPvyeuF+4K97wvJ8wodlg6zygyxHPLuoc0Mg3Im2p8xM/YVKz'
        'IUcIbq7lc4QApDfXarq2sR9yrnr+JkEnGVpt2hzxHGUT5uYIMySDZNfmSHadZA8WwuPtRsmOmFDEhEv2iFabUfyw'
        '1TbYajMwQAUOIVZamIJNCFHf/uztQ2/+ouZDLkPr3byka57qQa57u9S6/4TcbYsKpe7Xam/ASJun57huLEuWL/XJ'
        'zrGZQcdWUfIKzNY7G8APZ4ef6aKlf+ZJ6RPXNIqQeuUoDKgsUdpKKxGpVzogf4wMrZlD8qN+NFk8Szk7/RyfXJTP'
        'IyGhs85i6VCi+d5dz+MlX0b86qCdjfDbV7OB4llqOvnEsMcz8CkSF8lV6Ik7n/USARJOunf70Ds/M29BZm5U5JyM'
        '3Fwuq66sLLN6fWVdTfXasmohH9JZvao+lZtXJlwd/d2xqdyosDCGKcOG/98pAsPH/nHcRAz1xx7po13ZWOBIYSQ8'
        '8rdTMMR27KraQSPvyySDe3/9FyR0yobyXW88TDNa+U56bz1FGpFdnSyqc1j3zF+USGDe9SaLbO5rDtsD3679cHZS'
        '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'
        'CJyIusxh/8bXeTXaZ/xY9O/f//2//wcpemJj'
),
    'OW32D': (
# /* OW32D: USER32's DDEML from a Win32 program on WINN: a server and a
#  * client instance of its own: the service registered, a conversation,
#  * a request (DdeGetData, DdeAccessData), a poke and an execute (the
#  * server reads them with DdeAccessData and DdeGetData), DdeQueryConvInfo,
#  * string handles, the end. */
# #include <windows.h>
# #include <ddeml.h>
# #include <stdio.h>
# #include <string.h>
#
# static char line[8][160];
# static DWORD srv, cli;
# static HSZ svc, topic, item;
# static int connects, confirms, requests;
# static char poked[64], executed[64];
#
# static HDDEDATA CALLBACK server_cb(UINT type, UINT fmt, HCONV conv, HSZ h1, HSZ h2,
#                                    HDDEDATA data, ULONG_PTR d1, ULONG_PTR d2)
# {
#     switch (type) {
#     case XTYP_CONNECT:
#         connects++;
#         return (HDDEDATA)(DdeCmpStringHandles(h1, topic) == 0 &&
#                           DdeCmpStringHandles(h2, svc) == 0);
#     case XTYP_CONNECT_CONFIRM:
#         confirms++;
#         return 0;
#     case XTYP_REQUEST:
#         requests++;
#         if (!DdeCmpStringHandles(h2, item) && fmt == CF_TEXT)
#             return DdeCreateDataHandle(srv, (LPBYTE)"hello from the server", 22, 0, item,
#                                        CF_TEXT, 0);
#         return 0;
#     case XTYP_POKE: {
#         DWORD n = 0;
#         LPBYTE p = DdeAccessData(data, &n);
#         if (p && n < sizeof(poked))
#             memcpy(poked, p, n);
#         DdeUnaccessData(data);
#         return (HDDEDATA)DDE_FACK;
#     }
#     case XTYP_EXECUTE:
#         DdeGetData(data, (LPBYTE)executed, sizeof(executed), 0);
#         return (HDDEDATA)DDE_FACK;
#     }
#     return 0;
# }
#
# static HDDEDATA CALLBACK client_cb(UINT type, UINT fmt, HCONV conv, HSZ h1, HSZ h2,
#                                    HDDEDATA data, ULONG_PTR d1, ULONG_PTR d2)
# {
#     return 0;
# }
#
# static void dde(void)
# {
#     UINT r1 = DdeInitialize(&srv, server_cb, APPCLASS_STANDARD, 0);
#     UINT r2 = DdeInitialize(&cli, client_cb, APPCMD_CLIENTONLY, 0);
#     svc = DdeCreateStringHandle(srv, "OW32D", CP_WINANSI);
#     topic = DdeCreateStringHandle(srv, "Topic", CP_WINANSI);
#     item = DdeCreateStringHandle(srv, "Item", CP_WINANSI);
#     HDDEDATA ns = DdeNameService(srv, svc, 0, DNS_REGISTER);
#     char name[32] = "";
#     DWORD qn = DdeQueryString(srv, svc, name, sizeof(name), CP_WINANSI);
#     sprintf(line[0], "init %u %u, ids %lu %lu, service %d '%s' %lu", r1, r2, (unsigned long)srv,
#             (unsigned long)cli, ns != 0, name, (unsigned long)qn);
#     HSZ csvc = DdeCreateStringHandle(cli, "OW32D", CP_WINANSI);
#     HSZ ctopic = DdeCreateStringHandle(cli, "Topic", CP_WINANSI);
#     HSZ citem = DdeCreateStringHandle(cli, "Item", CP_WINANSI);
#     HCONV conv = DdeConnect(cli, csvc, ctopic, NULL);
#     sprintf(line[1], "connect %d (asked %d, confirmed %d), same %d", conv != 0, connects,
#             confirms, DdeCmpStringHandles(csvc, svc) == 0);
#     if (!conv)
#         return;
#     DWORD res = 0;
#     HDDEDATA got = DdeClientTransaction(NULL, 0, conv, citem, CF_TEXT, XTYP_REQUEST, 5000, &res);
#     char text[64] = "";
#     DWORD n = got ? DdeGetData(got, (LPBYTE)text, sizeof(text), 0) : 0;
#     DWORD size = 0;
#     LPBYTE p = got ? DdeAccessData(got, &size) : NULL;
#     sprintf(line[2], "request %d: '%s' %lu, access '%s' %lu", got != 0, text, (unsigned long)n,
#             p ? (char *)p : "-", (unsigned long)size);
#     if (got) {
#         DdeUnaccessData(got);
#         DdeFreeDataHandle(got);
#     }
#     HDDEDATA pk = DdeClientTransaction((LPBYTE)"poked text", 11, conv, citem, CF_TEXT, XTYP_POKE,
#                                        5000, &res);
#     HDDEDATA ex = DdeClientTransaction((LPBYTE)"[run(1)]", 9, conv, 0, 0, XTYP_EXECUTE, 5000,
#                                        &res);
#     sprintf(line[3], "poke %d '%s', execute %d '%s'", pk != 0, poked, ex != 0, executed);
#     CONVINFO ci;
#     memset(&ci, 0, sizeof(ci));
#     ci.cb = sizeof(ci);
#     UINT ic = DdeQueryConvInfo(conv, QID_SYNC, &ci);
#     sprintf(line[4], "info %u: cb %lu, topic %d, status %X, requests %d", ic,
#             (unsigned long)ci.cb, DdeCmpStringHandles(ci.hszTopic, ctopic) == 0, ci.wStatus,
#             requests);
#     BOOL d = DdeDisconnect(conv);
#     DdeFreeStringHandle(cli, csvc);
#     DdeFreeStringHandle(cli, ctopic);
#     DdeFreeStringHandle(cli, citem);
#     BOOL u2 = DdeUninitialize(cli);
#     DdeNameService(srv, svc, 0, DNS_UNREGISTER);
#     BOOL u1 = DdeUninitialize(srv);
#     sprintf(line[5], "disconnect %d, uninit %d %d, error %u", d, u1, u2, DdeGetLastError(srv));
# }
#
# static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
# {
#     switch (m) {
#     case WM_PAINT: {
#         PAINTSTRUCT ps;
#         HDC dc = BeginPaint(h, &ps);
#         for (int i = 0; i < 8; i++)
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
#     wc.lpszClassName = "Ow32dClass";
#     RegisterClass(&wc);
#     HWND h = CreateWindow("Ow32dClass", "OW32D - DDEML", WS_OVERLAPPEDWINDOW, 0, 0, 600, 140,
#                           NULL, NULL, inst, NULL);
#     ShowWindow(h, show);
#     dde();
#     InvalidateRect(h, NULL, TRUE);
#     MSG m;
#     while (GetMessage(&m, NULL, 0, 0)) {
#         TranslateMessage(&m);
#         DispatchMessage(&m);
#     }
#     return 0;
# }
# wcc386 -q -za99 -bt=nt ow32d.c
# wlink system nt_win name OW32D.EXE file ow32d.o library user32.lib
        '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'
),
}
if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkpe_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT')
with open(sys.argv[2], 'wb') as f:
    f.write(zlib.decompress(base64.b64decode(PROGRAMS[sys.argv[1]])))
