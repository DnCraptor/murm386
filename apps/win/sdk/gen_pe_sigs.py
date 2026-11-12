#!/usr/bin/env python3
"""
gen_pe_sigs.py - the Win32 functions a program (PE) calls of WINN's.

sdk/win32.ord lists KERNEL32's, USER32's and GDI32's functions with the
bytes of their arguments. A function is WINN's when

  - core/pe_api.c or core/pe_user.c has code of its own for it (SPECIAL
    below: the service is its PEF_ constant), or
  - WINN's native API has it (core/exports.c, its prototype in include/),
    by its own name, or by the ANSI one's without its A, or by its Win16
    name (ALIAS), and each of its arguments can be converted from the
    32-bit one the program pushed without knowing more about it:

  d  a dword as it is (int, UINT, DWORD, LONG, BOOL, WPARAM, LPARAM,
     COLORREF)
  w  a word (WORD, ATOM, handles: WINN's are 16-bit), the low one
  y  a BYTE, the low one;  h  a short, sign-extended
  I  an HINSTANCE: the program's or a DLL's is its task's
  s  LPCSTR: a string, NULL or a number (MAKEINTRESOURCE, an atom)
  P  a pointer to what the function reads or fills, as long as it is
  S  LPSTR followed by its size: a buffer the function fills
  B  a pointer to bytes followed by their count (in or out)
  r  RECT *;  p  POINT * or SIZE *;  q  int *, DWORD *, UINT *
  A  const POINT * followed by their count

  and the result: v (none), d, w (a handle), P (a pointer: the program's
  own back). The program's pointers are the native ones (its memory is
  flat): nothing is copied.

Writes core/pe_sigs.h: the specials first (their constants are their
services), then the rest, by module and name.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

MODULES = ('KERNEL32', 'USER32', 'GDI32')

# the code of pe_api.c and pe_user.c
SPECIAL = '''
KERNEL32: ExitProcess GetCommandLineA GetCommandLineW GetModuleHandleA GetModuleFileNameA
  GetModuleFileNameW GetProcAddress LoadLibraryA LoadLibraryExA FreeLibrary GetVersion
  GetVersionExA GetStartupInfoA GetEnvironmentStrings GetEnvironmentStringsA
  FreeEnvironmentStringsA GetEnvironmentVariableA SetEnvironmentVariableA GetCurrentProcess
  GetCurrentProcessId GetCurrentThread GetCurrentThreadId GetLastError SetLastError
  SetUnhandledExceptionFilter UnhandledExceptionFilter Sleep
  QueryPerformanceCounter QueryPerformanceFrequency GetSystemTime GetLocalTime
  GetSystemTimeAsFileTime FileTimeToSystemTime SystemTimeToFileTime FileTimeToLocalFileTime
  LocalFileTimeToFileTime GetTimeZoneInformation GetACP GetOEMCP GetCPInfo
  MultiByteToWideChar WideCharToMultiByte GetStringTypeA GetStringTypeW LCMapStringA
  LCMapStringW CompareStringA GetStdHandle SetStdHandle GetFileType CreateFileA ReadFile
  WriteFile CloseHandle SetFilePointer GetFileSize FlushFileBuffers SetEndOfFile
  GetFileAttributesA SetFileAttributesA FindFirstFileA FindNextFileA FindClose
  GetFullPathNameA GetCurrentDirectoryA SetCurrentDirectoryA GetDriveTypeA GetLogicalDrives
  GetDiskFreeSpaceA GetTempPathA GetFileTime SetFileTime FileTimeToDosDateTime
  DosDateTimeToFileTime GetProcessHeap HeapCreate HeapDestroy HeapAlloc HeapFree HeapReAlloc
  HeapSize HeapValidate HeapCompact VirtualAlloc VirtualFree VirtualQuery VirtualProtect
  GlobalAlloc GlobalLock GlobalUnlock GlobalFree GlobalSize GlobalReAlloc GlobalHandle
  GlobalFlags LocalAlloc LocalLock LocalUnlock LocalFree LocalSize LocalReAlloc LocalHandle
  LocalFlags TlsAlloc TlsFree TlsGetValue TlsSetValue InitializeCriticalSection
  EnterCriticalSection LeaveCriticalSection DeleteCriticalSection InterlockedIncrement
  InterlockedDecrement InterlockedExchange CreateEventA SetEvent ResetEvent
  WaitForSingleObject CreateMutexA ReleaseMutex SetConsoleCtrlHandler FindResourceA
  FindResourceExA LoadResource LockResource SizeofResource FreeResource
  DebugBreak FatalAppExitA GetSystemInfo GlobalMemoryStatus SetErrorMode
  GetSystemDefaultLangID GetUserDefaultLangID GetSystemDefaultLCID GetUserDefaultLCID
  GetThreadLocale GetLocaleInfoA IsDBCSLeadByte IsValidCodePage SetHandleCount
  GetShortPathNameA RaiseException RtlUnwind IsBadReadPtr IsBadWritePtr IsBadCodePtr
  IsBadStringPtrA GetVolumeInformationA DuplicateHandle GetExitCodeProcess TerminateProcess
  CreateThread GetConsoleMode SetConsoleMode GetNumberOfConsoleInputEvents
  FormatMessageA
 
 
  DeleteFileA MoveFileA CopyFileA CreateDirectoryA RemoveDirectoryA
  GetTempFileNameA GetDateFormatA GetTimeFormatA FileTimeToDosDateTime
  CompareFileTime SearchPathA _lopen _lcreat _lread _lwrite _llseek _lclose
  GetSystemTimeAdjustment SetFileApisToOEM AreFileApisANSI
USER32: MessageBoxA RegisterClassA RegisterClassExA UnregisterClassA CreateWindowExA
  DefWindowProcA CallWindowProcA GetMessageA PeekMessageA DispatchMessageA TranslateMessage
  PostMessageA SendMessageA PostThreadMessageA PostQuitMessage BeginPaint EndPaint
  GetWindowLongA SetWindowLongA GetClassLongA SetClassLongA GetClassInfoA GetClassInfoExA
  SetTimer KillTimer LoadIconA LoadCursorA LoadMenuA LoadStringA LoadAcceleratorsA
  LoadBitmapA LoadImageA TranslateAcceleratorA DialogBoxParamA DialogBoxIndirectParamA
  CreateDialogParamA CreateDialogIndirectParamA DefDlgProcA IsDialogMessageA
  SendDlgItemMessageA wsprintfA wvsprintfA CharUpperA CharLowerA CharNextA CharPrevA
  CharUpperBuffA CharLowerBuffA EnumWindows EnumChildWindows GetWindowTextA
  WindowFromPoint ChildWindowFromPoint MapWindowPoints GetWindowPlacement
  SetWindowPlacement
  SystemParametersInfoA
  GetMessagePos MsgWaitForMultipleObjects WaitMessage
GDI32: GetObjectA GetTextMetricsA CreateFontIndirectA TextOutA ExtTextOutA SelectObject BitBlt
  StretchBlt DeleteObject SetDIBColorTable GetDIBColorTable
  GetTextExtentPoint32A GetTextExtentPointA GetStockObject CreateDIBSection
'''

# Win32's names of Win16 functions
ALIAS = {
    'CharNextA': 'AnsiNext', 'CharPrevA': 'AnsiPrev', 'CharToOemA': 'AnsiToOem',
    'OemToCharA': 'OemToAnsi', 'CharToOemBuffA': 'AnsiToOemBuff',
    'OemToCharBuffA': 'OemToAnsiBuff',
    # properties hold 32 bits in Win32
    'SetPropA': 'SetPropEx', 'GetPropA': 'GetPropEx', 'RemovePropA': 'RemovePropEx',
}

# native functions of the name but not for a Win32 program: Win16's files,
# global blocks (the program's are its own memory)
EXCLUDE = {'_hread', '_hwrite', 'SetClipboardData', 'GetClipboardData',
           'CreateDIBPatternBrush'}

# structures of the same layout in Win32 and in WINN: pointers as they are
COMPAT = {'LOGPALETTE', 'PALETTEENTRY', 'BITMAPINFO', 'BITMAPINFOHEADER', 'LOGPEN',
          'LOGBRUSH', 'MINMAXINFO', 'WINDOWPLACEMENT', 'RGBQUAD', 'LPLOGPALETTE',
          'LPPALETTEENTRY', 'LPBITMAPINFO', 'LPBITMAPINFOHEADER', 'LPLOGPEN', 'LPLOGBRUSH',
          'LOGFONT', 'LPLOGFONT', 'ABC', 'LPABC', 'KERNINGPAIR', 'LPKERNINGPAIR'}

WORDS = {'WORD', 'ATOM', 'HFILE'}
SIGNED = {'short'}
BYTES = {'BYTE', 'char'}
DWORDS = {'int', 'UINT', 'BOOL', 'LONG', 'DWORD', 'LPARAM', 'WPARAM', 'COLORREF',
          'LRESULT', 'unsigned', 'long'}
COUNTS = re.compile(r'^(n|c|cb|cch|count|len|nCount|cbString|size|nSize|cbData|nMax|nLen)$')


def win32():
    rows = []
    for line in open(os.path.join(HERE, 'win32.ord')):
        line = line.split('#')[0].split()
        if len(line) == 3:
            rows.append((line[0], line[1], line[2]))
    return rows


def implemented():
    names = set()
    for line in open(os.path.join(ROOT, 'core', 'exports.c')):
        m = re.match(r'\s*\[\d+\] = \(const void \*\)(\w+),', line)
        if m:
            names.add(m.group(1))
    return names


def prototypes():
    protos = {}
    for f in ('windows.h', 'winuser.h'):
        text = open(os.path.join(ROOT, 'include', f)).read()
        text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
        for m in re.finditer(r'^([A-Za-z_][\w \*]*?)\s+WINAPI\s+(\w+)\s*\(([^)]*)\);', text, re.M):
            protos[m.group(2)] = (m.group(1).strip(), m.group(3).strip())
    return protos


def kind(t):
    t = ' '.join(t.replace('*', ' * ').split())
    const = t.startswith('const ')
    if const:
        t = t[6:]
    ptr = t.count('*')
    base = t.replace('*', '').strip()
    if base == 'LPCSTR' or (base == 'char' and ptr == 1 and const):
        return 's'
    if base in ('LPSTR',) or (base == 'char' and ptr == 1):
        return 'S'
    if base in ('LPCVOID', 'LPVOID', 'LPBYTE') or (base in ('void', 'BYTE') and ptr == 1):
        return 'B'
    if base in ('LPINT', 'LPBOOL', 'LPDWORD', 'LPUINT', 'LPLONG') or \
            (base in ('int', 'BOOL', 'DWORD', 'UINT', 'LONG') and ptr == 1):
        return 'q'
    if base in ('LPCRECT', 'LPRECT') or (base == 'RECT' and ptr == 1):
        return 'r'
    if base == 'POINT' and ptr == 1 and const:
        return 'A'
    if base in ('LPPOINT', 'LPSIZE') or (base in ('POINT', 'SIZE') and ptr == 1):
        return 'p'
    if base in COMPAT:
        return 'P'
    if ptr:
        return None
    if base == 'HINSTANCE':
        return 'I'
    if base in DWORDS:
        return 'd'
    if base in SIGNED:
        return 'h'
    if base in BYTES:
        return 'y'
    if base in WORDS or (base.startswith('H') and base.isupper()):
        return 'w'
    return None


def result(t):
    t = t.strip()
    if t == 'void':
        return 'v'
    if t == 'LPSTR':
        return 'P'
    k = kind(t)
    if k in ('w', 'I'):
        return 'w'
    if k in ('d', 'h', 'y'):
        return 'd'
    return None


def signature(ret, params):
    r = result(ret)
    if not r:
        return None
    if params in ('void', ''):
        return r + ':'
    codes = []
    types = [p.strip() for p in params.split(',')]
    for i, p in enumerate(types):
        if p == '...':
            return None
        m = re.match(r'(.*?[\s\*])(\w+)$', p)
        t = m.group(1) if m and m.group(1).strip() not in ('', 'const') else p
        k = kind(t)
        if k is None:
            return None
        nxt = re.match(r'(.*?[\s\*])(\w+)$', types[i + 1]) if i + 1 < len(types) else None
        sized = nxt and kind(nxt.group(1)) in ('d', 'w', 'h')
        if k in ('S', 'B', 'A') and not sized:
            k = 'P' if k != 'A' else None   # read or filled as long as it is
            if k is None:
                return None
        codes.append(k)
    return r + ':' + ''.join(codes)


def main():
    special = {}
    mod = None
    for tok in SPECIAL.split():
        if tok.endswith(':'):
            mod = tok[:-1]
        else:
            special[(mod, tok)] = True
    impl, protos = implemented(), prototypes()
    rows, specials, have = [], [], set()
    for mod, name, size in win32():
        if mod not in MODULES:
            continue
        if (mod, name) in special:
            specials.append((mod, name, size))
            continue
        if name.endswith('W') and name[:-1] + 'A' in [n for _, n, _ in []]:
            continue
        native = None
        for cand in (ALIAS.get(name), name[:-1] if name.endswith('A') else None, name):
            if cand and cand in impl and cand in protos:
                native = cand
                break
        if not native or name.endswith('W') or native in EXCLUDE:
            continue
        sig = signature(*protos[native])
        if not sig or size == 'c' or 4 * (len(sig) - 2) != int(size):
            continue
        rows.append((mod, name, sig, native, size))
        have.add(name)
    missing = sorted(set(special) - set((m, n) for m, n, _ in specials))
    if missing:
        raise SystemExit('specials not in win32.ord: %s' % missing)
    out = ['/* Generated by sdk/gen_pe_sigs.py - do not edit.',
           ' * Win32 functions of WINN\'s: module, name, "result:arguments" (see',
           ' * the script; NULL: pe_api.c\'s or pe_user.c\'s own), the native function,',
           ' * the bytes of the arguments (-1: a C function). The specials first:',
           ' * a PEF_ constant is the service of its function. */',
           '#ifndef WINN_PE_SIGS_H',
           '#define WINN_PE_SIGS_H',
           '']
    specials.sort(key=lambda r: (MODULES.index(r[0]), r[1]))
    out.append('enum {')
    for i, (mod, name, size) in enumerate(specials):
        out.append('    PEF_%s = %d,' % (name, i))
    out.append('    PEF_SPECIALS = %d' % len(specials))
    out.append('};')
    out.append('')
    out.append('#ifdef WIN_PE_SIGS_TABLE')
    out.append('const win_pe_fn win_pe_fns[] = {')
    for mod, name, size in specials:
        out.append('    { PE_%s, "%s", NULL, NULL, %s },' % (mod, name, -1 if size == 'c' else size))
    rows.sort(key=lambda r: (MODULES.index(r[0]), r[1]))
    for mod, name, sig, native, size in rows:
        out.append('    { PE_%s, "%s", "%s", (const void *)%s, %s },' % (mod, name, sig, native, size))
    out.append('};')
    out.append('const unsigned win_pe_nfns = sizeof(win_pe_fns) / sizeof(win_pe_fns[0]);')
    out.append('#endif')
    out.append('#endif')
    open(os.path.join(ROOT, 'core', 'pe_sigs.h'), 'w').write('\n'.join(out) + '\n')
    print('%d specials, %d generic' % (len(specials), len(rows)))


main()
