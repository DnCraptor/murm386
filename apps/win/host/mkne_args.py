#!/usr/bin/env python3
"""Generate NEARGS.EXE with FAR-PASCAL imports and an executable DOS stub.

The entry runs GetSysColor(5), GetSystemMetrics(0), lstrlen(DS:0x10).
The last call returns the known length in AX, with the parameter stack
completely cleaned by the thunks' RETF imm16 instructions.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_args.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(at, v): struct.pack_into('<H', b, at, v)
def w32(at, v): struct.pack_into('<I', b, at, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg = b'This program requires WINN. Run WINN NEARGS.EXE\r\n$'
b[0x40:0x40+len(stub)] = stub
b[0x40+len(stub):0x40+len(stub)+len(msg)] = msg
h = 0x100
w16(h, 0x454e)
w16(h+0x14, 0); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 2)
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
b[h+0x36] = 2

# Code segment: disk offset 0x200; data/stack: disk offset 0x300.
code = bytearray()
fixups = []
def push_word(v): code.extend((0x68, v & 0xff, v >> 8))
def far_call(module, target, is_name=False):
    off = len(code)
    code.extend((0x9a, 0xff, 0xff, 0, 0))
    fixups.append((off+1,module,target,2 if is_name else 1))

push_word(5); far_call(2,180)     # GetSysColor(int)
push_word(0); far_call(2,179)     # GetSystemMetrics(int)
code.append(0x1e)                # PUSH DS
push_word(0x10)                  # PUSH 16-bit string offset
far_call(1,12,True)              # lstrlen(16:16), via import name
code.append(0xcb)               # RETF
assert len(code) < 0x40
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x20); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0); w16(h+0x52, 7)
b[h+0x54] = 6; b[h+0x55:h+0x5b] = b'KERNEL'
b[h+0x5b] = 4; b[h+0x5c:h+0x60] = b'USER'
b[h+0x60] = 7; b[h+0x61:h+0x68] = b'lstrlen'
for i, (source, module, target, kind) in enumerate(fixups):
    assert kind in (1, 2)
    if i == 0: w16(0x200+len(code),len(fixups))
    struct.pack_into('<BBHHH',b,0x200+len(code)+2+i*8,3,kind,source,module,target)
# A zero-terminated string at offset 0x10 of the DATA segment.
text = b'Win16 API args'
b[0x310:0x310+len(text)+1] = text+b'\0'
with open(sys.argv[1],'wb') as f: f.write(b)
print('NEARGS.EXE:', len(b),'bytes, expected lstrlen=',len(text))
