#!/usr/bin/env python3
"""Build NEBYTE.EXE: bounded Win16 DS byte-copy loop with a real DOS MZ stub.

Uses MOV AL,[SI] / MOV [DI],AL, the direct AL forms, and LOOP; the
copied NUL-terminated string is shown through native USER.1 MessageBox.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_bytes.py OUTPUT.EXE')

image = bytearray(0x400)

def w16(at, value):
    struct.pack_into('<H', image, at, value)

def w32(at, value):
    struct.pack_into('<I', image, at, value)

# DOS MZ header and a DOS-safe stub (the latter does not enter the NE image).
w16(0, 0x5A4D)
w16(2, 0)
w16(4, 2)
w16(8, 4)
w16(0x0A, 0x10)
w16(0x0C, 0xFFFF)
w16(0x10, 0x100)
w16(0x18, 0x40)
w32(0x3C, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg = b'This program requires WINN. Run WINN NEBYTE.EXE\r\n$'
image[0x40:0x40 + len(stub)] = stub
image[0x40 + len(stub):0x40 + len(stub) + len(msg)] = msg

h = 0x100
w16(h, 0x454E)
w16(h + 0x16, 1)
w16(h + 0x18, 0x100)
w16(h + 0x1A, 2)
w16(h + 0x1C, 2)
w16(h + 0x1E, 1)
w16(h + 0x22, 0x40)
w16(h + 0x28, 0x50)
w16(h + 0x2A, 0x54)
w16(h + 0x32, 4)
image[h + 0x36] = 2

source = b'Byte loop: OK\0'
assert len(source) <= 64
code = bytearray()

def emit(hexstr):
    code.extend(bytes.fromhex(hexstr))

emit('be 70 00')         # MOV SI,70h - known source in DS
emit('bf 20 00')         # MOV DI,20h - known destination in DS
emit('b9 %02x 00' % len(source))  # MOV CX,source bytes including NUL
loop = len(code)
emit('8a 04')            # MOV AL,[SI]
emit('88 05')            # MOV [DI],AL
emit('46 47')            # INC SI; INC DI
loop_at = len(code)
emit('e2 00')            # LOOP rel8
jump = loop - (loop_at + 2)
assert -128 <= jump < 0
code[loop_at + 1] = jump & 0xff
emit('a0 20 00')         # MOV AL,[20h] (direct byte read)
emit('a2 50 00')         # MOV [50h],AL (direct byte write)
emit('31 c0 50')         # XOR AX,AX; PUSH HWND=0
emit('1e 68 20 00')      # PUSH DS:20h text pointer
emit('1e 68 90 00')      # PUSH DS:90h title pointer
emit('50')               # PUSH MB_OK=0
fixup = len(code) + 1
emit('9a ff ff ff ff')   # CALL FAR USER.1, resolved by NE loader
emit('cb')               # RETF
assert len(code) < 0x80

image[0x200:0x200 + len(code)] = code
w16(h + 0x40, 0x20)       # code segment at 0x200
w16(h + 0x42, len(code))
w16(h + 0x44, 0x100)      # code segment flags
w16(h + 0x46, 0x100)
w16(h + 0x48, 0x30)       # data segment at 0x300
w16(h + 0x4A, 0x100)
w16(h + 0x4C, 1)
w16(h + 0x4E, 0x100)
w16(h + 0x50, 0)
image[h + 0x54] = 4
image[h + 0x55:h + 0x59] = b'USER'
w16(0x200 + len(code), 1)
struct.pack_into('<BBHHH', image, 0x200 + len(code) + 2,
                 3, 1, fixup, 1, 1)
image[0x300 + 0x70:0x300 + 0x70 + len(source)] = source
image[0x300 + 0x90:0x300 + 0x90 + len(b'Win16 DS byte loop\0')] = b'Win16 DS byte loop\0'

with open(sys.argv[1], 'wb') as out:
    out.write(image)
print(f'NEBYTE.EXE: {len(image)} bytes, copy={len(source)} bytes, '
      f'LOOP={loop:04x}, FAR fixup={fixup:04x}')
