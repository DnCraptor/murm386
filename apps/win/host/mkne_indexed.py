#!/usr/bin/env python3
"""Generate NEINDEX.EXE with bounded register-indexed Win16 DATA accesses.

Its DOS stub exits cleanly. The NE code writes 1234h to DATA[00A0] via
[BX+2], then reads it through [SI+4], [BX+SI], and [DI] after LEA. A wrong
result calls KERNEL.GetVersion; the successful path calls USER.MessageBox.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_indexed.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(p, v): struct.pack_into('<H', b, p, v)
def w32(p, v): struct.pack_into('<I', b, p, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NEINDEX.EXE\r\n$'
b[0x40:0x40+len(stub)] = stub
b[0x40+len(stub):0x40+len(stub)+len(message)] = message

h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 2)
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2

code = bytearray()
fixups = []
def emit(hexcode): code.extend(bytes.fromhex(hexcode))
def api(module, ordinal):
    reloc = len(code)+1
    emit('9a ff ff ff ff')
    fixups.append((reloc,module,ordinal))
def jnz():
    pos=len(code)
    emit('75 00')
    return pos

emit('bb 9c 00')           # MOV BX,009Ch
emit('83 c3 02')           # ADD BX,2 -> 009Eh
emit('b8 34 12')           # MOV AX,1234h
emit('89 47 02')           # MOV [BX+2],AX -> DATA[00A0]
emit('be 9c 00')           # MOV SI,009Ch
emit('8b 44 04')           # MOV AX,[SI+4] -> DATA[00A0]
emit('3d 34 12')           # CMP AX,1234h
check1=jnz()
emit('bb 90 00')           # MOV BX,0090h
emit('be 10 00')           # MOV SI,0010h
emit('8b 00')              # MOV AX,[BX+SI] -> DATA[00A0]
emit('3d 34 12')           # CMP AX,1234h
check2=jnz()
emit('8d 38')              # LEA DI,[BX+SI] -> 00A0h
emit('8b 05')              # MOV AX,[DI]
emit('3d 34 12')           # CMP AX,1234h
check3=jnz()
emit('31 c0 50')           # XOR AX,AX; PUSH AX (HWND=0)
emit('1e 68 20 00')        # PUSH DS; PUSH text offset
emit('1e 68 80 00')        # PUSH DS; PUSH title offset
emit('50')                 # PUSH AX (MB_OK=0)
api(2,1)                 # USER.MessageBox
jmp=len(code)
emit('eb 00')              # JMP done
bad=len(code)
api(1,3)                 # KERNEL.GetVersion when check fails
done=len(code)
emit('cb')                 # RETF
for pos in (check1,check2,check3):
    displacement=bad-(pos+2)
    assert 0 < displacement <= 127
    code[pos+1]=displacement
assert 0 < done-(jmp+2) <= 127
code[jmp+1]=done-(jmp+2)
assert len(code)<0x80
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0); w16(h+0x52, 7)
b[h+0x54] = 6; b[h+0x55:h+0x5b] = b'KERNEL'
b[h+0x5b] = 4; b[h+0x5c:h+0x60] = b'USER'
w16(0x200+len(code), len(fixups))
for i,(at,module,ordinal) in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i,
                     3, 1, at, module, ordinal)
text=b'Indexed Win16 DATA accesses: OK'
title=b'NE indexed addressing'
b[0x320:0x320+len(text)+1]=text+b'\0'
b[0x380:0x380+len(title)+1]=title+b'\0'
with open(sys.argv[1],'wb') as f: f.write(b)
print('NEINDEX.EXE:',len(b),'bytes; MessageBox IDOK expected')
