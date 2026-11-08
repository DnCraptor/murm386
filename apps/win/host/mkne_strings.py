#!/usr/bin/env python3
"""Win16 NE fixture: KERNEL.lstrcpy modifies DATA, then USER.MessageBox uses it."""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_strings.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(pos, value): struct.pack_into('<H', b, pos, value)
def w32(pos, value): struct.pack_into('<I', b, pos, value)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NESTR.EXE\r\n$'
b[0x40:0x40 + len(stub)] = stub
b[0x40 + len(stub):0x40 + len(stub) + len(message)] = message

h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 2)
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2

code = bytearray.fromhex('55 8b ec')           # prologue
fixups = []
def emit(text): code.extend(bytes.fromhex(text))
def call_far(module, target, named=False):
    source = len(code) + 1
    emit('9a ff ff ff ff')
    fixups.append((source, module, target, 2 if named else 1))

emit('bb 20 00 1e 53')                         # destination DS:BX (0x20)
emit('be a0 00 1e 56')                         # source DS:SI (0xA0)
call_far(1, 12, True)                          # KERNEL.lstrcpy by name (8 bytes)
emit('31 c0 50')                               # zero HWND
emit('b9 20 00 1e 51')                         # text DS:CX (0x20)
emit('ba 80 00 1e 52')                         # title DS:DX (0x80)
emit('50')                                     # MB_OK
call_far(2, 1)                                 # USER.MessageBox (12 stack bytes)
emit('89 ec 5d cb')                            # epilogue
assert len(code) < 0x40
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0); w16(h+0x52, 7)
b[h+0x54] = 6; b[h+0x55:h+0x5b] = b'KERNEL'
b[h+0x5b] = 4; b[h+0x5c:h+0x60] = b'USER'
b[h+0x60] = 7; b[h+0x61:h+0x68] = b'lstrcpy'
w16(0x200+len(code), len(fixups))
for i, (source, module, target, kind) in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+i*8,
                     3, kind, source, module, target)

text = b'Copied from Win16 DATA segment'
title = b'NE string API'
b[0x3a0:0x3a0+len(text)+1] = text+b'\0'
b[0x380:0x380+len(title)+1] = title+b'\0'
with open(sys.argv[1], 'wb') as f:
    f.write(b)
print('NESTR.EXE:', len(b), 'bytes, expected MessageBox text:', text.decode())
