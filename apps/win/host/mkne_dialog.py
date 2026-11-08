#!/usr/bin/env python3
"""Generate a restricted Win16 NE that calls USER.1 MessageBox.

Its DOS MZ stub prints a message and exits cleanly when run outside WINN.
The NE entry pushes HWND, FAR text, FAR caption, flags (PASCAL order).
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_dialog.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(pos, val): struct.pack_into('<H', b, pos, val)
def w32(pos, val): struct.pack_into('<I', b, pos, val)

# DOS MZ stub, including a valid PSP-relative DS = CS setup.
w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NEDIALOG.EXE\r\n$'
b[0x40:0x40 + len(stub)] = stub
b[0x40 + len(stub):0x40 + len(stub) + len(message)] = message

h = 0x100
w16(h, 0x454e)                # NE
w16(h+0x14, 0); w16(h+0x16, 1)   # entry CS:IP
w16(h+0x18, 0x100); w16(h+0x1a, 2)  # stack SS:SP
w16(h+0x1c, 2); w16(h+0x1e, 1)   # two segments, one module
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2                 # target Windows

code = bytearray()
def push(v): code.extend((0x68, v & 255, (v >> 8) & 255))
push(0)                  # HWND owner
code.append(0x1e); push(0x20)  # DS:0x20 text
code.append(0x1e); push(0x80)  # DS:0x80 caption
push(0)                  # MB_OK
far_source = len(code) + 1
code.extend((0x9a, 0xff, 0xff, 0xff, 0xff))
code.append(0xcb)        # RETF
assert len(code) < 0x40
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0xA0); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0); w16(h+0x52, 5) # USER name table offset after modref
b[h+0x54] = 4; b[h+0x55:h+0x59] = b'USER'
w16(0x200+len(code), 1)
struct.pack_into('<BBHHH', b, 0x200+len(code)+2, 3, 1, far_source, 1, 1)

text = b'Hello from Win16 NE'
title = b'NE dialog'
b[0x320:0x320+len(text)+1] = text+b'\0'
b[0x380:0x380+len(title)+1] = title+b'\0'

with open(sys.argv[1], 'wb') as f:
    f.write(b)
print('NEDIALOG.EXE:',len(b),'bytes, expected return IDOK=1')
