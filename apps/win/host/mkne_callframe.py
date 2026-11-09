#!/usr/bin/env python3
"""Generate NECALL.EXE: Win16 near CALL/RET with BP-based stack locals.

The helper writes two local words then returns AX=1234h. The caller checks
its result and displays different native USER.MessageBox text on failure.
A real DOS MZ stub exits with a message rather than running the NE header.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_callframe.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(p, v): struct.pack_into('<H', b, p, v)
def w32(p, v): struct.pack_into('<I', b, p, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg = b'This program requires WINN. Run WINN NECALL.EXE\r\n$'
b[0x40:0x40+len(stub)] = stub
b[0x40+len(stub):0x40+len(stub)+len(msg)] = msg

h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 1)  # 2 segments, 1 USER import
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2

code = bytearray()
fixups = []
def emit(bytecode): code.extend(bytes.fromhex(bytecode))
def api():
    fixups.append(len(code) + 1)
    emit('9a ff ff ff ff')
def messagebox(text_offset):
    emit('31 c0 50')       # XOR AX,AX; PUSH HWND=0
    emit('1e')             # PUSH DS text selector
    emit('68 %02x %02x' % (text_offset & 255, text_offset >> 8))
    emit('1e 68 90 00')   # PUSH DS; PUSH title offset
    emit('50')             # PUSH UINT MB_OK=0
    api()                 # USER.1 MessageBox(HWND,text,title,flags)

emit('55 8b ec')         # PUSH BP; MOV BP,SP
call_at = len(code)
emit('e8 00 00')         # CALL NEAR helper
emit('3d 34 12')         # CMP AX,1234h
jne = len(code)
emit('75 00')            # JNZ bad
messagebox(0x20)        # success
jmp = len(code)
emit('eb 00')            # JMP epilogue
bad = len(code)
messagebox(0x60)        # failure
epilogue = len(code)
emit('8b e5 5d cb')      # MOV SP,BP; POP BP; RETF
helper = len(code)
emit('55 8b ec')         # PUSH BP; MOV BP,SP
emit('83 ec 04')         # SUB SP,4
emit('c7 46 fe 34 12')   # MOV word [BP-2],1234h
emit('8b 46 fe')         # MOV AX,[BP-2]
emit('89 46 fc')         # MOV [BP-4],AX
emit('8b 46 fc')         # MOV AX,[BP-4]
emit('c9 c3')            # LEAVE; RET near

assert 0 < helper-(call_at+3) < 0x8000
struct.pack_into('<h', code, call_at+1, helper-(call_at+3))
assert 0 < bad-(jne+2) < 0x80 and 0 < epilogue-(jmp+2) < 0x80
code[jne+1] = bad-(jne+2)
code[jmp+1] = epilogue-(jmp+2)
assert len(code) < 0x80
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0)
b[h+0x54] = 4; b[h+0x55:h+0x59] = b'USER'
w16(0x200+len(code), len(fixups))
for i, offset in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i,
                     3, 1, offset, 1, 1)
text1=b'Win16 near CALL and BP locals: OK'
text2=b'NE near CALL/BP locals: FAILED'
title=b'Win16 stack frame'
b[0x320:0x320+len(text1)+1] = text1+b'\0'
b[0x360:0x360+len(text2)+1] = text2+b'\0'
b[0x390:0x390+len(title)+1] = title+b'\0'
with open(sys.argv[1], 'wb') as f:
    f.write(b)
print(f'NECALL.EXE: {len(b)} bytes; helper={helper:04x}; 2 x USER.1 fixups')
