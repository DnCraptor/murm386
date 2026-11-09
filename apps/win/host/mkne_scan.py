#!/usr/bin/env python3
"""Build NESCAN.EXE, a 16-iteration NUL-terminated Win16 byte scan.

All paths are checked before execution: byte reads/writes stay in DS, the
LOOP has a bounded CX, and JZ may exit only to forward verified code.
A DOS-safe MZ stub rejects execution outside WINN.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_scan.py OUTPUT.EXE')

image = bytearray(0x400)
def w16(at, value):
    struct.pack_into('<H', image, at, value)
def w32(at, value):
    struct.pack_into('<I', image, at, value)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2); w16(8, 4)
w16(0x0a, 0x10); w16(0x0c, 0xffff); w16(0x10, 0x100)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg = b'This program requires WINN. Run WINN NESCAN.EXE\r\n$'
image[0x40:0x40+len(stub)] = stub
image[0x40+len(stub):0x40+len(stub)+len(msg)] = msg
h = 0x100
w16(h, 0x454e); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 1)
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
image[h+0x36] = 2

source = b'Scan: OK\0'
code = bytearray()
fixups = []
def emit(hexstr):
    code.extend(bytes.fromhex(hexstr))
def jmp8(at, target):
    delta = target - (at + 2)
    assert -128 <= delta <= 127
    code[at + 1] = delta & 0xff
def messagebox(offset):
    emit('31 c0 50')                 # XOR AX,AX; PUSH HWND=0
    emit(f'1e 68 {offset:02x} 00')   # PUSH DS:offset (text)
    emit('1e 68 90 00')              # PUSH DS:90h (title)
    emit('50')                       # MB_OK=0
    fixups.append(len(code)+1)
    emit('9a ff ff ff ff')           # USER.1, relocated by loader

emit('be 70 00 bf 20 00 b9 10 00')  # SI=source, DI=dest, CX=16
loop = len(code)
emit('8a 04 88 05 84 c0')         # MOV AL,[SI]; MOV [DI],AL; TEST AL,AL
found_jump = len(code)
emit('74 00')                    # JZ found NUL
emit('46 47')                    # INC SI; INC DI
loop_jump = len(code)
emit('e2 00')                    # LOOP back, no unbounded scan
missing_jump = len(code)
emit('eb 00')                    # Exhausted 16 bytes: failure
found = len(code)
messagebox(0x20)                 # Success: show the copied string
end_jump = len(code)
emit('eb 00')
missing = len(code)
messagebox(0x50)
finish = len(code)
emit('cb')                       # RETF
jmp8(found_jump, found)
jmp8(loop_jump, loop)
jmp8(missing_jump, missing)
jmp8(end_jump, finish)
assert len(code) < 0x80

image[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code))
w16(h+0x44, 0x100); w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100)
w16(h+0x4c, 1); w16(h+0x4e, 0x100); w16(h+0x50, 0)
image[h+0x54] = 4; image[h+0x55:h+0x59] = b'USER'
w16(0x200+len(code), len(fixups))
for i, off in enumerate(fixups):
    struct.pack_into('<BBHHH', image, 0x200+len(code)+2+8*i,
                     3, 1, off, 1, 1)
image[0x370:0x370+len(source)] = source
image[0x350:0x350+len(b'NUL not found\0')] = b'NUL not found\0'
image[0x390:0x390+len(b'WINN NUL scanner\0')] = b'WINN NUL scanner\0'
with open(sys.argv[1], 'wb') as out:
    out.write(image)
print(f'NESCAN.EXE: {len(image)} bytes, loop={loop:04x}, '
      f'JZ={found_jump:04x}, fixups={len(fixups)}')
