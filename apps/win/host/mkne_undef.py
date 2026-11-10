#!/usr/bin/env python3
"""Generate NEUNDEF.EXE: imports WINN has not, as Windows' "undefined
dynalinks": USER.600 (not called) and MMSYSTEM.2 (sndPlaySound, a module
WINN has nothing of). The program loads; its call of MMSYSTEM.2 ends it,
the call named ("MMSYSTEM.2").

    mkne_undef.py NEUNDEF.EXE
"""
import os
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_undef.py OUTPUT.EXE')
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
w16(h+0x1c, 2); w16(h+0x1e, 2)         # 2 segments, 2 import modules
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2

code = bytearray()
fixups = []
def far_call(module, ordinal):
    code.extend(b'\x9a')
    fixups.append((3, 1, len(code), module, ordinal))
    code.extend(b'\xff\xff\x00\x00')
code.extend(bytes.fromhex('eb 05'))     # jmp over the call never made
far_call(1, 600)                        # USER.600
code.extend(bytes.fromhex('6a 00 6a 00 6a 00'))  # sndPlaySound(NULL, 0)
far_call(2, 2)                          # MMSYSTEM.2
code.extend(bytes.fromhex('b8 01 4c cd 21'))     # not reached: exit 1
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
imp = b'\x00\x04USER\x08MMSYSTEM'
w16(h+0x50, 1); w16(h+0x52, 6)          # the modules' names
b[h+0x54:h+0x54+len(imp)] = imp
w16(0x200+len(code), len(fixups))
for i, rec in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i, *rec)
with open(sys.argv[1], 'wb') as f:
    f.write(b)
print(name.decode() + ':', len(b), 'bytes; ends calling MMSYSTEM.2')
