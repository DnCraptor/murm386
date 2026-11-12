#!/usr/bin/env python3
"""
gen_ne_sigs.py - how a Win16 program's call reaches each native API function
whose arguments can be converted without knowing more about it.

For every KERNEL, GDI and USER function WINN implements (core/exports.c),
its prototype in include/ gives, argument by argument, what the Win16
caller pushed and what the native function takes:

  w  a word (UINT, WORD, WPARAM, handles), zero-extended
  i  a signed word (int, BOOL), sign-extended
  I  an HINSTANCE: the program's (its DGROUP selector) is its task's
  d  a dword (LONG, DWORD, LPARAM, COLORREF)
  s  LPCSTR: a far pointer to a string (selector 0: a number, as
     MAKEINTRESOURCE / an atom; 0:0 NULL)
  c  LPCSTR followed by its length (-1: up to the NUL)
  S  LPSTR followed by its size: a buffer the function fills
  B  const void FAR * followed by its size: bytes in
  b  void FAR * followed by its size: bytes the function fills
  q  int FAR * (LPINT, LPBOOL): a word the function sets
  R  const RECT FAR *, in;   r  RECT FAR *, in and out
  p  POINT FAR *, in and out
  A  const POINT FAR * followed by their count

and the result: v (nothing), w, i (AX), d (DX:AX). A function with anything
else (other structures, callbacks, raw buffers, guest memory: Global*,
Local*) is left out; WINN has its own code for the few it needs
(core/ne_guest.c's table, ne_guest_user.c).

Writes core/ne_sigs.h. Run after gen_exports.py.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# not for this generic way: guest memory, callbacks hidden in other types,
# what ends the program, and those with code of their own
SKIP = {
    'FatalExit', 'Yield', 'GetVersion', 'GetCurrentTask', 'GetNumTasks',
    'lstrcpy', 'lstrcat', 'lstrlen', 'lstrcpyn', 'GetModuleHandle',
    'WinExec', 'LoadLibrary', 'FreeLibrary',
    'MessageBox', 'PostQuitMessage', 'BeginPaint', 'EndPaint', 'CreateWindow',
    'CreateWindowEx', 'RegisterClass', 'DefWindowProc', 'GetMessage',
    'PeekMessage', 'TranslateMessage', 'DispatchMessage', 'LoadCursor', 'LoadIcon',
    'LoadMenu', 'LoadString', 'LoadAccelerators', 'TranslateAccelerator',
    'DialogBox', 'DialogBoxParam', 'CreateDialog', 'CreateDialogParam',
    'EndDialog', 'SetDlgItemText', 'GetDlgItemText', 'SetDlgItemInt',
    'GetDlgItemInt', 'TextOut', 'GetStockObject', 'SendMessage',
    'SendDlgItemMessage', 'PostMessage', 'CallWindowProc', 'SetWindowLong',
    'GetWindowLong', 'SetClassLong', 'GetClassLong', 'SetTimer', 'KillTimer',
    'GetWindowWord', 'SetWindowWord', 'GetClassWord', 'SetClassWord',
    'IsDialogMessage', 'DefDlgProc', 'GetDlgItem', 'DestroyWindow',
    'ShowWindow', 'UpdateWindow', 'InvalidateRect',
    # guest memory and structures: ne_guest_call.c
    'LocalAlloc', 'LocalReAlloc', 'LocalFree', 'LocalLock', 'LocalUnlock', 'LocalSize',
    'GlobalAlloc', 'GlobalReAlloc', 'GlobalFree', 'GlobalLock', 'GlobalUnlock', 'GlobalSize',
    'MapWindowPoints', 'EnumWindows', 'EnumChildWindows', 'GetTextMetrics', 'GetObject',
    'CreateFontIndirect', 'CreatePenIndirect', 'CreateBrushIndirect', 'PtInRect',
    'WindowFromPoint', 'ChildWindowFromPoint', 'AnsiUpper', 'AnsiLower', 'AnsiNext',
    'AnsiPrev', 'wsprintf', 'wvsprintf',
    # text, bitmaps, classes, huge files: ne_guest_call.c
    'ExtTextOut', 'GetTextExtentPoint', 'TabbedTextOut', 'GetTabbedTextExtent', 'GetTextFace',
    'CreateBitmap', 'GetBitmapBits', 'SetBitmapBits', 'GetClassInfo', '_hread', '_hwrite',
    # palettes, a DC, the system colours, MDI: ne_guest_kernel.c
    'CreateDC', 'CreatePalette', 'GetPaletteEntries', 'SetPaletteEntries',
    'GetSystemPaletteEntries', 'SetSysColors', 'TranslateMDISysAccel',
    # strings changed in place, the keys (256 bytes): ne_guest_user.c
    'AnsiUpperBuff', 'AnsiLowerBuff', 'GetKeyboardState', 'SetKeyboardState',
    # points and widths by the count, a global block's DIB: ne_guest_kernel.c
    'LPtoDP', 'DPtoLP', 'GetCharWidth', 'CreateDIBPatternBrush',
    # the clipboard's data: global blocks of the program's and native ones
    'SetClipboardData', 'GetClipboardData', 'CloseClipboard', 'EmptyClipboard',
    # a spec changed in place, a selection's text (no size): ne_guest_call.c
    'DlgDirList', 'DlgDirListComboBox', 'DlgDirSelect', 'DlgDirSelectComboBox',
    # a metafile's bits in global blocks, its records to a procedure: ne_guest_kernel.c
    'GetMetaFileBits', 'SetMetaFileBits', 'SetMetaFileBitsBetter',
    # its structures (ints of a word, LOGFONT), its strings: ne_guest_more.c
    'SystemParametersInfo', 'SetDeskWallPaper',
    # the program's resources, structures of words (ne_guest_more.c)
    'LoadImage', 'GetIconInfo', 'DrawTextEx', 'ChildWindowFromPointEx', 'GetDCHook',
    'GetTextExtentEx',
}

COUNTS = re.compile(r'^(n|c|cb|cch|count|len|nCount|cbString|size)$')

WORDS = {'UINT', 'WORD', 'WPARAM', 'ATOM', 'BYTE', 'HFILE', 'char'}
SIGNED = {'int', 'BOOL', 'short'}
DWORDS = {'LONG', 'DWORD', 'LPARAM', 'COLORREF', 'LRESULT'}


def ordinals():
    table = {}
    for line in open(os.path.join(HERE, 'win16.ord')):
        line = line.split('#')[0].split()
        if len(line) == 3 and line[2] not in table:
            table[line[2]] = (line[0], int(line[1]))
    return table


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
    """the code of one type, or None"""
    t = ' '.join(t.replace('*', ' * ').split())
    const = t.startswith('const ')
    if const:
        t = t[6:]
    ptr = t.count('*')
    base = t.replace('*', '').strip()
    if base in ('LPCSTR',) or (base == 'char' and ptr == 1 and const):
        return 's'
    if base == 'LPCVOID' or (base == 'void' and ptr == 1 and const):
        return 'B'
    if base == 'LPVOID' or (base == 'void' and ptr == 1):
        return 'b'
    if base in ('LPINT', 'LPBOOL') or (base in ('int', 'BOOL') and ptr == 1 and not const):
        return 'q'
    if base in ('LPSTR',) or (base == 'char' and ptr == 1):
        return 'S'
    if base in ('LPCRECT',) or (base == 'RECT' and ptr == 1 and const):
        return 'R'
    if base in ('LPRECT',) or (base == 'RECT' and ptr == 1):
        return 'r'
    if base == 'POINT' and ptr == 1 and const:
        return 'A'
    if base in ('LPPOINT', 'LPSIZE') or (base in ('POINT', 'SIZE') and ptr == 1):
        return 'p'                      # a SIZE: two ints as well
    if ptr:
        return None
    if base == 'HINSTANCE':
        return 'I'
    if base in SIGNED:
        return 'i'
    if base in DWORDS:
        return 'd'
    if base in WORDS or (base.startswith('H') and base.isupper()):
        return 'w'
    return None


def result(t):
    t = t.strip()
    if t == 'void':
        return 'v'
    k = kind(t)
    if k in ('w', 'I'):
        return 'w'
    if k in ('i', 'd'):
        return k
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
        # drop the parameter name
        m = re.match(r'(.*?[\s\*])(\w+)$', p)
        t = m.group(1) if m and m.group(1).strip() not in ('', 'const') else p
        k = kind(t)
        if k is None:
            return None
        nxt = re.match(r'(.*?[\s\*])(\w+)$', types[i + 1]) if i + 1 < len(types) else None
        sized = nxt and kind(nxt.group(1)) in ('w', 'i')
        if k in ('S', 'A', 'B', 'b') and not sized:
            return None                 # a size or count must follow
        if k == 's' and sized and COUNTS.match(nxt.group(2)):
            k = 'c'                     # a string with its length
        codes.append(k)
    return r + ':' + ''.join(codes)


def main():
    ords, impl, protos = ordinals(), implemented(), prototypes()
    rows, left = [], []
    for name in sorted(impl):
        if name in SKIP or name not in ords or ords[name][0] not in ('KERNEL', 'GDI', 'USER'):
            continue
        if name not in protos:
            left.append(name + ' (no prototype)')
            continue
        sig = signature(*protos[name])
        if not sig:
            left.append(name)
            continue
        mod, o = ords[name]
        rows.append((mod, o, name, sig))
    out = ['/* Generated by sdk/gen_ne_sigs.py - do not edit.',
           ' * Win16 functions a program calls through the generic marshaller:',
           ' * module, ordinal, name, "result:arguments" (see the script). */',
           '']
    for mod, o, name, sig in sorted(rows):
        out.append('    { M_%s, %d, "%s", "%s" },' % (mod, o, name, sig))
    open(os.path.join(ROOT, 'core', 'ne_sigs.h'), 'w').write('\n'.join(out) + '\n')
    print('%d functions; left out: %s' % (len(rows), ', '.join(left)))


main()
