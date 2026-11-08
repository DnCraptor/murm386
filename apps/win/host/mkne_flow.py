#!/usr/bin/env python3
"""Generate NEFLOW.EXE: forward JNZ/JMP, verified DS memory, Win16 API.

The real DOS MZ stub exits normally with a "requires WINN" message.
Both branches are valid FAR API calls, but the memory compare must choose
USER.MessageBox (IDOK=1) rather than KERNEL.GetVersion (07000A03h).
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_flow.py OUTPUT.EXE')
b = bytearray(0x400)
def w16(p, v): struct.pack_into('<H', b, p, v)
def w32(p, v): struct.pack_into('<I', b, p, v)

w16(0, 0x5a4d); w16(2, 0); w16(4, 2)
w16(6, 0); w16(8, 4); w16(0x0a, 0x10); w16(0x0c, 0xffff)
w16(0x0e, 0); w16(0x10, 0x100); w16(0x14, 0); w16(0x16, 0)
w16(0x18, 0x40); w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NEFLOW.EXE\r\n$'
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
def emit(hexstr): code.extend(bytes.fromhex(hexstr))
def api(module, ordinal):
    reloc = len(code) + 1
    emit('9a ff ff ff ff')
    fixups.append((reloc, module, ordinal))

emit('c7 06 a0 00 34 12')                 # MOV WORD PTR DS:[00A0],1234h
emit('a1 a0 00')                          # MOV AX,DS:[00A0]
emit('3d 34 12')                          # CMP AX,1234h
jnz = len(code)
emit('75 00')                             # JNZ bad (must not be taken)
emit('31 c0 50')                          # XOR AX,AX; PUSH AX (owner)
emit('1e 68 20 00')                       # PUSH DS; PUSH text offset
emit('1e 68 80 00')                       # PUSH DS; PUSH title offset
emit('50')                                # PUSH AX (MB_OK)
api(2, 1)                                # USER.MessageBox
jmp = len(code)
emit('eb 00')                             # JMP done (skip failure branch)
bad = len(code)
api(1, 3)                                # KERNEL.GetVersion (should NOT run)
done = len(code)
emit('cb')                                # RETF
assert 0 < bad - (jnz + 2) <= 127
assert 0 < done - (jmp + 2) <= 127
code[jnz+1] = bad - (jnz+2)
code[jmp+1] = done - (jmp+2)
assert len(code) < 0x40
b[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100)
w16(h+0x48, 0x30); w16(h+0x4a, 0x100); w16(h+0x4c, 1)
w16(h+0x4e, 0x100)
w16(h+0x50, 0); w16(h+0x52, 7)
b[h+0x54] = 6; b[h+0x55:h+0x5b] = b'KERNEL'
b[h+0x5b] = 4; b[h+0x5c:h+0x60] = b'USER'
w16(0x200+len(code), len(fixups))
for i, (at, module, ordinal) in enumerate(fixups):
    struct.pack_into('<BBHHH', b, 0x200+len(code)+2+8*i,
                     3, 1, at, module, ordinal)

text = b'Win16 conditional branch and memory: OK'
title = b'NE flow control'
b[0x320:0x320+len(text)+1] = text+b'\0'
b[0x380:0x380+len(title)+1] = title+b'\0'
with open(sys.argv[1], 'wb') as f:
    f.write(b)
print('NEFLOW.EXE:', len(b), 'bytes; Win16 MessageBox IDOK=1 expected')
