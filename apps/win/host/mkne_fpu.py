#!/usr/bin/env python3
"""Generate NEFPU.EXE: coprocessor instructions as Microsoft C's emulator
code has them, the interrupts INT 34h to 3Dh, each with its OS fixup (the
loader makes them the instructions again), and WIN87EM's __FPMATH:

    __FPMATH 0 (initialize); FILD 2, FIADD ES:3, FIADD SS:5, FIADD DS:7,
    FIADD CS:100, FISTP (WAIT); FILD that, __FPMATH 7 (pop it into DX:AX)

and the result, 117, the exit code (INT 21h 4Ch).

    mkne_fpu.py NEFPU.EXE
"""
import os
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_fpu.py OUTPUT.EXE')
name = os.path.basename(sys.argv[1]).upper().encode()
b = bytearray(0x400)
def w16(p, v): struct.pack_into('<H', b, p, v)
def w32(p, v): struct.pack_into('<I', b, p, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN ' + name + b'\r\n$'
b[0x40:0x40 + len(stub)] = stub
b[0x40 + len(stub):0x40 + len(stub) + len(message)] = message

h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)          # entry CS:IP
w16(h+0x18, 0x100); w16(h+0x1a, 2)     # stack SS:SP
w16(h+0x1c, 2); w16(h+0x1e, 1)         # 2 segments, 1 import module
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x52); w16(h+0x32, 4)
b[h+0x36] = 2

code = bytearray()
fixups = []                             # (source type, flags, at, a, b)
def emit(hexstr): code.extend(bytes.fromhex(hexstr))
def fpmath(fn):                         # mov bx,fn; call far WIN87EM.1
    emit('bb %02x 00 9a' % fn)
    fixups.append((3, 1, len(code), 1, 1))
    emit('ff ff 00 00')
def esc(osfix, hexstr):                 # an emulator interrupt and its OS fixup
    fixups.append((5, 7, len(code), osfix, 0))
    if osfix <= 4:                      # an override's ESC byte: a second one
        fixups.append((5, 7, len(code) + 1, osfix, 0))
    emit(hexstr)

emit('8c d0 8e d8 8e c0')               # mov ax,ss; mov ds,ax; mov es,ax
fpmath(0)
esc(5, 'cd 3b 06 00 00')                # fild word [0]          2
esc(4, 'cd 3c de 06 02 00')             # fiadd word es:[2]      3
esc(2, 'cd 3c 5e 06 04 00')             # fiadd word ss:[4]      5
esc(1, 'cd 3c 1e 06 06 00')             # fiadd word ds:[6]      7
cs_const = len(code) + 4
esc(3, 'cd 3c 9e 06 00 00')             # fiadd word cs:[k]      100
esc(5, 'cd 3b 1e 08 00')                # fistp word [8]
esc(6, 'cd 3d')                         # fwait
esc(5, 'cd 3b 06 08 00')                # fild word [8]
fpmath(7)                               # DX:AX
emit('b4 4c cd 21')                     # exit, AL the result
k = len(code)
struct.pack_into('<H', code, cs_const, k)
emit('64 00')                           # k: 100
assert len(code) < 0x80
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 1)                          # module WIN87EM
b[h+0x52] = 0
b[h+0x53] = 7; b[h+0x54:h+0x5b] = b'WIN87EM'
w16(0x200+len(code), len(fixups))
for i, rec in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i, *rec)
assert 0x200 + len(code) + 2 + 8 * len(fixups) <= 0x300
b[0x300:0x308] = struct.pack('<4H', 2, 3, 5, 7)
with open(sys.argv[1], 'wb') as f:
    f.write(b)
print(name.decode() + ':', len(b), 'bytes; exit code 117 expected')
