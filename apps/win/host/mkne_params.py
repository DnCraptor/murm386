#!/usr/bin/env python3
"""NEPARAM.EXE (PASCAL) or NECDECL.EXE (CDECL) near arguments.

A DOS MZ stub exits safely. The Win16 body calls a near C-style helper
with two stack parameters. Either the callee cleans both via RET 4 or
the caller uses ADD SP,4 after plain RET. Both use USER.1 MessageBox.
"""
import struct
import sys

if len(sys.argv) == 2:
    cdecl = False
    output = sys.argv[1]
elif len(sys.argv) == 3 and sys.argv[1] == '--cdecl':
    cdecl = True
    output = sys.argv[2]
else:
    raise SystemExit('usage: mkne_params.py [--cdecl] OUTPUT.EXE')
b = bytearray(0x400)
def w16(p, v): struct.pack_into('<H', b, p, v)
def w32(p, v): struct.pack_into('<I', b, p, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg = (b'This program requires WINN. Run WINN NECDECL.EXE\r\n$'
       if cdecl else
       b'This program requires WINN. Run WINN NEPARAM.EXE\r\n$')
b[0x40:0x40+len(stub)] = stub
b[0x40+len(stub):0x40+len(stub)+len(msg)] = msg

h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 1)
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2
code = bytearray()
fixups = []
def emit(hexstr): code.extend(bytes.fromhex(hexstr))
def messagebox(text_offset):
    emit('31 c0 50')   # XOR AX,AX ; PUSH HWND=0
    emit('1e')         # PUSH DS (text selector)
    emit(f'68 {text_offset:02x} 00')
    emit('1e 68 90 00') # title selector:offset
    emit('50')          # MB_OK=0
    fixups.append(len(code) + 1)
    emit('9a ff ff ff ff')

emit('55 8b ec')        # MAIN: PUSH BP ; MOV BP,SP
emit('b8 34 12 50')     # MOV AX,1234h ; PUSH AX (first argument)
emit('68 21 43')        # PUSH 4321h (second argument, at BP+4)
call_at = len(code)
emit('e8 00 00')        # CALL helper
if cdecl:
    emit('83 c4 04')    # ADD SP,4 (exact caller cleanup after RET)
emit('3d 55 55')        # CMP AX,5555h (1234h+4321h)
jne_at = len(code)
emit('75 00')           # JNE failure
messagebox(0x20)        # successful call (result IDOK = 1)
jmp_at = len(code)
emit('eb 00')
bad = len(code)
messagebox(0x60)
epilogue = len(code)
emit('8b e5 5d cb')     # MOV SP,BP ; POP BP ; RETF
helper = len(code)
emit('55 8b ec')        # HELPER: PUSH BP ; MOV BP,SP
emit('83 ec 02')        # SUB SP,2 local
a_arg4 = len(code)
emit('8b 46 04')        # MOV AX,[BP+4] -> 4321h
a_arg6 = len(code)
emit('8b 5e 06')        # MOV BX,[BP+6] -> 1234h
emit('01 d8')           # ADD AX,BX -> 5555h
emit('89 46 fe')        # MOV [BP-2],AX
emit('8b 46 fe')        # MOV AX,[BP-2]
emit('c9 c3' if cdecl else 'c9 c2 04 00') # LEAVE; RET or RET 4

assert 0 < helper-(call_at+3) < 0x8000
struct.pack_into('<h', code, call_at+1, helper-(call_at+3))
assert 0 < bad-(jne_at+2) <= 127
assert 0 < epilogue-(jmp_at+2) <= 127
code[jne_at+1] = bad-(jne_at+2)
code[jmp_at+1] = epilogue-(jmp_at+2)
assert len(code) < 0x80
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0)
b[h+0x54] = 4; b[h+0x55:h+0x59] = b'USER'
w16(0x200+len(code), len(fixups))
for i, off in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i, 3, 1, off, 1, 1)

good = (b'Win16 near CDECL parameters: OK' if cdecl
        else b'Win16 near PASCAL parameters: OK')
fail = (b'NE near CDECL parameters: FAILED' if cdecl
        else b'NE near PASCAL parameters: FAILED')
title = b'Win16 near arguments'
b[0x320:0x320+len(good)+1] = good+b'\0'
b[0x360:0x360+len(fail)+1] = fail+b'\0'
b[0x390:0x390+len(title)+1] = title+b'\0'
with open(output, 'wb') as f:
    f.write(b)
print(f'{"NECDECL" if cdecl else "NEPARAM"}.EXE: {len(b)} bytes, '
      f'helper={helper:04x}, BP+4={a_arg4:04x}, BP+6={a_arg6:04x}')
