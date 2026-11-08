#!/usr/bin/env python3
"""Generate a tiny NE for the restricted WINN WinExec route.

DOS MZ stub prints an error and exits when invoked outside WINN.
The NE entry calls KERNEL.36, KERNEL.152, KERNEL.3, then RETF.
It does not represent a general Win16 GUI application.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: mkne_winexec.py OUTPUT.EXE")
b = bytearray(0x400)

def w16(at, value):
    struct.pack_into("<H", b, at, value)

def w32(at, value):
    struct.pack_into("<I", b, at, value)

# Proper DOS MZ header: its entry point is the stub at file offset 0x40.
w16(0, 0x5A4D)
w16(2, 0)           # last page is full
w16(4, 2)           # two 512-byte pages
w16(6, 0)           # no DOS fixups
w16(8, 4)           # 4 header paragraphs: image starts at 0x40
w16(0x0A, 0x10)     # minimum paragraphs
w16(0x0C, 0xFFFF)   # maximum paragraphs
w16(0x0E, 0)        # SS
w16(0x10, 0x100)    # SP
w16(0x14, 0)        # IP
w16(0x16, 0)        # CS
w16(0x18, 0x40)     # empty relocation table
w32(0x3C, 0x100)    # NE header absolute offset

# PUSH CS; POP DS; MOV AH,09; MOV DX,000E; INT 21;
# MOV AX,4C01; INT 21. Text is DOS AH=09 '$'-terminated.
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NEAPP.EXE\r\n$'
b[0x40:0x40+len(stub)] = stub
b[0x40+len(stub):0x40+len(stub)+len(message)] = message

h = 0x100
w16(h, 0x454E)
w16(h + 0x14, 0)          # NE IP = 0
w16(h + 0x16, 1)          # CS segment 1
w16(h + 0x18, 0x100)      # SS:SP = segment 2:0x100
w16(h + 0x1A, 2)
w16(h + 0x1C, 2)          # two segments
w16(h + 0x1E, 1)          # one imported module
w16(h + 0x22, 0x40)       # segment table
w16(h + 0x28, 0x50)       # module references
w16(h + 0x2A, 0x52)       # imported names
w16(h + 0x32, 4)          # 16-byte sectors
b[h + 0x36] = 2           # Windows target

w16(h + 0x40, 0x20)       # code at file 0x200
w16(h + 0x42, 16)         # disk length: three CALL FARs and RETF
w16(h + 0x44, 0x100)      # code + relocations
w16(h + 0x46, 0x40)       # code memory length 64
w16(h + 0x48, 0)          # uninitialised stack/data
w16(h + 0x4A, 0)
w16(h + 0x4C, 1)          # data segment
w16(h + 0x4E, 0x100)      # 256 bytes stack
w16(h + 0x50, 0)          # imported name table +0
b[h + 0x52] = 6
b[h + 0x53:h + 0x59] = b'KERNEL'

# FAR CALL (4-byte relocation), repeated 3 times, then RETF.
for index, ordinal in enumerate((36, 152, 3)):
    at = 0x200 + index * 5
    b[at] = 0x9A
    w16(at + 1, 0xFFFF)  # terminator of the fixup chain
    w16(at + 3, 0)
b[0x20F] = 0xCB
w16(0x210, 3)
for index, ordinal in enumerate((36, 152, 3)):
    b[0x212 + index * 8:0x21A + index * 8] = struct.pack(
        '<BBHHH', 3, 1, index * 5 + 1, 1, ordinal)

with open(sys.argv[1], 'wb') as f:
    f.write(b)
print('Wrote', sys.argv[1], '(1024 bytes; DOS MZ stub + Win16 NE)')
