#!/usr/bin/env python3
"""Create a tiny synthetic NE file for the non-executing WINN /nestage check.

Usage: python3 apps/win/host/mkne_stage.py NECHECK.EXE
The file has two relocations (KERNEL.3 and KERNEL.GetVersion). It is not an
independently runnable Windows application; it tests guest-memory staging.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: mkne_stage.py OUTPUT.EXE")
b = bytearray(0x400)
def w16(off, value):
    struct.pack_into("<H", b, off, value)
def w32(off, value):
    struct.pack_into("<I", b, off, value)
w16(0, 0x5A4D)
w32(0x3C, 0x80)
h = 0x80
w16(h, 0x454E)
w16(h+0x14, 0)
w16(h+0x16, 1)
w16(h+0x18, 0x40)
w16(h+0x1A, 1)
w16(h+0x1C, 1)
w16(h+0x1E, 1)
w16(h+0x22, 0x40)
w16(h+0x28, 0x48)
w16(h+0x2A, 0x4A)
w16(h+0x32, 4)
b[h+0x36] = 2
w16(h+0x40, 0x20)
w16(h+0x42, 0x40)
w16(h+0x44, 0x100)
w16(h+0x46, 0x80)
b[h+0x4A] = 6
b[h+0x4B:h+0x51] = b"KERNEL"
b[h+0x51] = 10
b[h+0x52:h+0x5C] = b"GetVersion"
b[0x200] = 0x90
w16(0x210, 0xFFFF)
w16(0x220, 0xFFFF)
w16(0x240, 2)
b[0x242:0x24A] = struct.pack("<BBHHH", 3, 1, 0x10, 1, 3)
b[0x24A:0x252] = struct.pack("<BBHHH", 3, 2, 0x20, 1, 7)
with open(sys.argv[1], "wb") as f:
    f.write(b)
print("Wrote", sys.argv[1], "(1024 bytes)")
