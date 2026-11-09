#!/usr/bin/env python3
"""Generate NESTR2.EXE: one integrated Win16 test of 8086 string operations.

The real DOS stub exits. WINN executes protected-mode guest instructions,
checks copied bytes and early-terminating comparisons, then calls USER.1.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_string_ops.py OUTPUT.EXE')

img = bytearray(0x400)
def w16(o, n): struct.pack_into('<H', img, o, n)
def w32(o, n): struct.pack_into('<I', img, o, n)
w16(0, 0x5a4d); w16(4, 2); w16(8, 4); w16(0x0a, 0x10)
w16(0x0c, 0xffff); w16(0x10, 0x100); w16(0x18, 0x40)
w32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg = b'This program requires WINN. Run WINN NESTR2.EXE\r\n$'
img[0x40:0x40+len(stub)] = stub
img[0x40+len(stub):0x40+len(stub)+len(msg)] = msg
h = 0x100
w16(h, 0x454e); w16(h+0x16, 1)
w16(h+0x18, 0x100); w16(h+0x1a, 2)
w16(h+0x1c, 2); w16(h+0x1e, 1)
w16(h+0x22, 0x40); w16(h+0x28, 0x50)
w16(h+0x2a, 0x54); w16(h+0x32, 4)
img[h+0x36] = 2

src = b'STRINGS: OK\x00'
assert len(src) == 12
code = bytearray()
fail_jumps = []
fixups = []
def emit(x):
    code.extend(bytes.fromhex(x))
def jne_bad():
    fail_jumps.append(len(code)); emit('75 00')
def messagebox(offset):
    emit('31 c0 50')               # XOR AX,AX / PUSH hWnd=0
    emit(f'1e 68 {offset:02x} 00') # PUSH DS:offset message
    emit('1e 68 a0 00')            # PUSH DS:0xa0 title
    emit('50')                     # MB_OK
    fixups.append(len(code)+1)
    emit('9a ff ff ff ff')         # imported USER.1

emit('fc')                             # CLD
emit('be 90 00 bf 20 00 b9 0c 00 f3 a4')  # REP MOVSB to 20h
emit('be 90 00 bf 40 00 b9 06 00 f3 a5')  # REP MOVSW to 40h
emit('be 9b 00 bf 3b 00 b9 0c 00 fd f3 a4 fc') # STD reverse MOVSB; CLD
emit('be 20 00 bf 30 00 b9 06 00 f3 a7')  # REPE CMPSW
emit('8b c1 3d 00 00')                # CMP CX,0 via AX
jne_bad()
emit('be 20 00 bf 40 00 b9 0c 00 f3 a6')  # REPE CMPSB
emit('8b c1 3d 00 00')
jne_bad()
emit('bf 20 00 b9 0c 00 31 c0 f2 ae') # REPNE SCASB NUL
emit('8b c7 3d 2c 00')               # DI at 2Ch after 12 bytes
jne_bad()
emit('be 90 00 ac bf 50 00 aa')    # LODSB, STOSB
emit('bf 50 00 ae')                   # SCASB, compares S
jne_bad()
emit('be 90 00 ad bf 52 00 ab')    # LODSW, STOSW
emit('bf 52 00 af')                   # SCASW, compares ST
jne_bad()
emit('31 c0 bf 54 00 b9 04 00 f3 aa') # REP STOSB zero 4 bytes
emit('a0 57 00 a8 ff')               # TEST AL,FF
jne_bad()
emit('31 c0 bf 58 00 b9 02 00 f3 ab') # REP STOSW zero 4 bytes
emit('a1 5a 00 3d 00 00')             # check last zero word
jne_bad()
messagebox(0x20)
end_jump = len(code)
emit('eb 00')
failed = len(code)
messagebox(0x60)
finished = len(code)
emit('cb')
for at in fail_jumps:
    d = failed - (at+2)
    assert 0 < d <= 127, (at, failed, d)
    code[at+1] = d
assert 0 < finished - (end_jump+2) <= 127
code[end_jump+1] = finished-(end_jump+2)
assert len(code) + 2 + len(fixups)*8 <= 0x100, len(code)
img[0x200:0x200+len(code)] = code
w16(h+0x40, 0x20); w16(h+0x42, len(code)); w16(h+0x44, 0x100)
w16(h+0x46, 0x100); w16(h+0x48, 0x30); w16(h+0x4a, 0x100)
w16(h+0x4c, 1); w16(h+0x4e, 0x100)
img[h+0x54] = 4
img[h+0x55:h+0x59] = b'USER'
w16(0x200+len(code), len(fixups))
for i, off in enumerate(fixups):
    struct.pack_into('<BBHHH', img, 0x200+len(code)+2+i*8,
                     3, 1, off, 1, 1)
img[0x300+0x90:0x300+0x90+len(src)] = src
img[0x300+0x60:0x300+0x60+len(b'String operations: FAILED\x00')] = b'String operations: FAILED\x00'
img[0x300+0xa0:0x300+0xa0+len(b'WINN string ops\x00')] = b'WINN string ops\x00'
with open(sys.argv[1], 'wb') as out:
    out.write(img)
print('NESTR2.EXE:',len(img),'bytes; code',len(code),'bytes; checks',len(fail_jumps),
      'fixups',len(fixups))
