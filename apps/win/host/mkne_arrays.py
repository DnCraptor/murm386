#!/usr/bin/env python3
"""Win16 MZ/NE regression for checked arrays, structs and memory ALU."""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: mkne_arrays.py NEARRAY.EXE")
image = bytearray(2048)

def u16(offset, value):
    struct.pack_into('<H', image, offset, value)

def u32(offset, value):
    struct.pack_into('<I', image, offset, value)

for off, value in ((0, 0x5a4d), (4, 2), (8, 4), (10, 0x10),
                   (12, 0xffff), (16, 0x100), (24, 0x40)):
    u16(off, value)
u32(0x3c, 0x100)
stub = bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
message = b'This program requires WINN. Run WINN NEARRAY.EXE\r\n$'
image[0x40:0x40+len(stub)] = stub
image[0x40+len(stub):0x40+len(stub)+len(message)] = message
h = 0x100
for off, value in ((0, 0x454e), (0x16, 1), (0x18, 0x100),
                   (0x1a, 2), (0x1c, 2), (0x1e, 1), (0x22, 0x40),
                   (0x28, 0x50), (0x2a, 0x54), (0x32, 4)):
    u16(h+off, value)
image[h+0x36] = 2

code = bytearray()
labels = {}
fixups = []
branches = []
checks = 0

def emit(data):
    code.extend(bytes.fromhex(data))

def label(name):
    assert name not in labels, name
    labels[name] = len(code)

def jump(opcode, target, size):
    pos = len(code)
    code.extend(opcode)
    code.extend(bytes(size))
    branches.append((pos+len(opcode), size, target))

def bad(op='0f85'):
    global checks
    jump(bytes.fromhex(op), 'failed', 2)
    checks += 1

def messagebox(text):
    emit('31 c0 50')                 # PUSH window=NULL
    emit('ff 76 fe')                 # proven DS selector in BP-2
    emit(f'68 {text:02x} 00')        # text offset
    emit('ff 76 fe 68 90 00 50')    # DS:caption, MB_OK
    fixups.append(len(code)+1)
    emit('9a ff ff ff ff')

label('entry')
emit('c8 04 00 00 8c d8 89 46 fe')  # ENTER 4,0; save DS selector
# Word array at DS:10,12, indexed by BX+SI.
emit('c7 06 10 00 00 10 c7 06 12 00 00 20')
emit('bb 10 00 be 02 00')            # BX=0x10 SI=2
emit('83 00 05')                     # ADD WORD [BX+SI],+5
emit('81 38 05 20')                  # CMP WORD [BX+SI],2005h
bad()
emit('b8 05 00 01 00')               # AX=5, ADD WORD [BX+SI],AX
emit('ba 0a 20 39 10')               # DX=200Ah; CMP [BX+SI],DX
bad()
emit('b8 0a 20 3b 00')               # AX=200Ah; CMP AX,[BX+SI]
bad()
emit('b8 01 00 03 00 3d 0b 20')      # AX=1; ADD AX,[BX+SI]; CMP AX,200Bh
bad()
# Array element indexed with a signed disp8, plus TEST of memory.
emit('c6 06 16 00 08 80 68 04 01')   # BYTE[16]=8; SUB BYTE[BX+SI+4],1
emit('f6 40 04 07')                  # TEST BYTE[BX+SI+4],7 (ZF=0)
bad('0f84')                          # fail if ZF=1
emit('80 3e 16 00 07')               # CMP BYTE[16],7
bad()
# Field of a packed struct, with 83 sign extension, AND/OR/XOR.
emit('c7 06 18 00 00 f0 83 0e 18 00 0f') # OR WORD[18],0Fh
emit('81 26 18 00 0f 0f')           # AND WORD[18],0F0Fh
emit('83 36 18 00 03')              # XOR WORD[18],3
emit('81 3e 18 00 0c 00')           # CMP WORD[18],000Ch
bad()
# A genuine backward C loop whose FLAGS are proven from memory CMP.
emit('c6 06 1a 00 00')              # BYTE[1A]=0
label('loop')
emit('fe 06 1a 00')                 # INC BYTE[1A]
emit('80 3e 1a 00 03')              # CMP BYTE[1A],3
jump(bytes.fromhex('72'), 'loop', 1) # JB loop
emit('80 3e 1a 00 03')
bad()
# NOT and NEG have different FLAGS semantics; check resulting value.
emit('c7 06 1c 00 0f 0f')
emit('f7 16 1c 00')                 # NOT WORD[1C] -> F0F0
emit('f7 1e 1c 00')                 # NEG WORD[1C] -> 0F10
emit('81 3e 1c 00 10 0f')
bad()
# TEST WORD [mem],reg: expected ZF, no mutation.
emit('b8 01 00 85 06 1c 00')        # [1C]&AX == 0
bad('0f85')
# BP-local memory ALU: SP and saved-DS slot are not addressable by this path.
emit('c7 46 fc 04 00')              # WORD[BP-4]=4
emit('fe 46 fc')                    # INC BYTE[BP-4] -> 5
emit('83 7e fc 05')                 # CMP WORD [BP-4],5
bad()
# Nested helper modifies the same checked DS element; shadow survives RET.
emit('bb 1e 00 c6 07 00')           # BX=1Eh; BYTE[BX]=0
jump(bytes.fromhex('e8'), 'helper', 2)
jump(bytes.fromhex('e8'), 'helper', 2)
emit('80 3f 02')                    # CMP BYTE[BX],2
bad()
messagebox(0x20)
jump(bytes.fromhex('eb'), 'end', 1)
label('failed')
messagebox(0x60)
label('end')
emit('c9 cb')                       # LEAVE; RETF
label('helper')
emit('80 07 01 c3')                 # ADD BYTE[BX],1; RET

for pos, size, target in branches:
    delta = labels[target] - (pos+size)
    if size == 1:
        assert -128 <= delta <= 127, (pos,target,delta)
        struct.pack_into('<b', code, pos, delta)
    else:
        assert -32768 <= delta <= 32767
        struct.pack_into('<h', code, pos, delta)
assert checks >= 11, checks
assert len(code) < 512-2-8*len(fixups), len(code)
image[0x200:0x200+len(code)] = code
for off, val in ((0x40, 0x20), (0x42, len(code)),
                 (0x44, 0x100), (0x46, 0x100), (0x48, 0x40),
                 (0x4a, 0x100), (0x4c, 1), (0x4e, 0x100)):
    u16(h+off, val)
image[h+0x54] = 4
image[h+0x55:h+0x59] = b'USER'
u16(0x200+len(code), len(fixups))
for i, pos in enumerate(fixups):
    struct.pack_into('<BBHHH', image, 0x200+len(code)+2+8*i,
                     3, 1, pos, 1, 1)
for off, val in ((0x20,b'MEMORY ARRAYS: OK\0'),
                 (0x60,b'MEMORY ARRAYS: FAILED\0'),
                 (0x90,b'WINN checked arrays / structures\0')):
    image[0x400+off:0x400+off+len(val)] = val
with open(sys.argv[1],'wb') as f:
    f.write(image)
print('NEARRAY code',len(code),'relocs',len(fixups),'checks',checks,
      'loop',labels['loop'],'helper',labels['helper'])
