#!/usr/bin/env python3
"""Generate NEFAULT.EXE: KERNEL.GetVersion followed by terminal UD2.

The DOS MZ stub exits normally with a "requires WINN" message.
The Win16 NE entry raises #UD after one native API call; WINN's private IDT
should return to real mode and report exception vector 6 without a reset.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_fault.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(p, v): struct.pack_into('<H', b, p, v)
def w32(p, v): struct.pack_into('<I', b, p, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NEFAULT.EXE\r\n$'
b[0x40:0x40 + len(stub)] = stub
b[0x40 + len(stub):0x40 + len(stub) + len(message)] = message

h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)          # entry CS:IP
w16(h+0x18, 0x100); w16(h+0x1a, 2)     # stack SS:SP
w16(h+0x1c, 2); w16(h+0x1e, 1)         # 2 segments, 2 import modules
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2

code = bytearray()
fixups = []
def emit(hexstr): code.extend(bytes.fromhex(hexstr))
# First enter native KERNEL.GetVersion through the existing RM API gateway,
# then raise the intentionally invalid instruction #UD inside Win16 PM.
reloc = len(code) + 1
emit('9a ff ff ff ff')
fixups.append((reloc, 1, 3))
emit('0f 0b')                         # UD2, never reaches RETF
assert len(code) < 0x40
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0); w16(h+0x52, 7)
b[h+0x54] = 6; b[h+0x55:h+0x5b] = b'KERNEL'
# No second import module
w16(0x200+len(code), len(fixups))
for i, (at, module, ordinal) in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i,
                     3, 1, at, module, ordinal)

text = b'Win16 #UD recovery: FAIL (should not show)'
title = b'NE fault probe'
b[0x320:0x320+len(text)+1] = text+b'\0'
b[0x380:0x380+len(title)+1] = title+b'\0'
with open(sys.argv[1], 'wb') as f:
    f.write(b)
print('NEFAULT.EXE:', len(b), 'bytes; #UD expected and DOS context must resume')
