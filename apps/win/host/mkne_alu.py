#!/usr/bin/env python3
"""Generate NEALU.EXE: many Win16 ALU/shift/byte-reg/near-Jcc cases in one NE.

The NE entry checks each calculated value and reports FAILED at the first
mismatch. The MZ DOS stub never executes NE code outside WINN.
"""
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkne_alu.py OUTPUT.EXE')
img = bytearray(0x500)
def u16(o,n): struct.pack_into('<H', img, o, n)
def u32(o,n): struct.pack_into('<I', img, o, n)
u16(0,0x5a4d);u16(4,2);u16(8,4);u16(0x0a,0x10);u16(0x0c,0xffff)
u16(0x10,0x100);u16(0x18,0x40);u32(0x3c,0x100)
stub=bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg=b'This program requires WINN. Run WINN NEALU.EXE\r\n$'
img[0x40:0x40+len(stub)]=stub
img[0x40+len(stub):0x40+len(stub)+len(msg)]=msg
h=0x100
u16(h,0x454e);u16(h+0x16,1);u16(h+0x18,0x100);u16(h+0x1a,2)
u16(h+0x1c,2);u16(h+0x1e,1);u16(h+0x22,0x40);u16(h+0x28,0x50)
u16(h+0x2a,0x54);u16(h+0x32,4);img[h+0x36]=2
code=bytearray()
fixups=[]; failure_jumps=[]
def emit(h): code.extend(bytes.fromhex(h))
def jne_fail():
    off=len(code);emit('0f 85 00 00');failure_jumps.append(off)
def messagebox(o):
    emit('31 c0 50')
    emit('1e');emit(f'68 {o:02x} 00')
    emit('1e 68 90 00 50')
    fixups.append(len(code)+1);emit('9a ff ff ff ff')

# Exact register ALU immediates: AND, OR, XOR, CMP.
emit('b8 34 12 81 e0 f0 0f 81 c8 01 00 81 f0 20 00 3d 11 02');jne_fail()
# SHL/SHR (imm and 1) / SHL by CL and CMP r16,imm8.
emit('bb 01 00 c1 e3 04 d1 eb b9 02 00 d3 e3 83 fb 20');jne_fail()
# Signed arithmetic shift, 0x8000 -> 0xc000.
emit('ba 00 80 d1 fa 81 fa 00 c0');jne_fail()
# NOT/NEG on exact 16-bit SI and negative arithmetic.
emit('be 34 12 f7 d6 f7 de 81 fe 35 12');jne_fail()
# MOV AL/AH partial registers preserve the untouched half of AX.
emit('b8 00 12 b0 34 b4 56 3d 34 56');jne_fail()
# MOV 8-bit reg, OR/AND/XOR with AL and by-CL shifts.
emit('bb 00 00 b3 0f b7 10 8a c3 0c 10 24 1f 34 0f 3d 10 56');jne_fail()
emit('c0 e0 01 d0 e8 b9 01 00 d2 e8 3d 08 56');jne_fail()
# XCHG short AX,BX; verify both independent destinations.
emit('b8 34 12 bb 78 56 93 3d 78 56');jne_fail()
emit('81 fb 34 12');jne_fail()
# XCHG AH,AL and 8-bit boolean register-register.
emit('b8 12 34 86 e0 3d 34 12');jne_fail()
emit('b8 10 00 b9 01 00 08 c8 20 c8 30 c8 3d 00 00');jne_fail()
# NOT/NEG 8-bit and separate CMP AL,imm8.
emit('b8 01 00 f6 d0 f6 d8 3c 02');jne_fail()
# Checked DS arithmetic address and byte load: indexed memory access.
emit('bb 40 00 81 e3 ff 00 c6 07 7f 8a 07 3c 7f');jne_fail()
# TEST AX,imm16 and TEST AL,imm8; flags-only operations.
emit('f7 c0 00 ff a8 80')
# Zero-argument KERNEL.GetVersion would add an import; test only USER.1 here.
messagebox(0x20)
end_jmp=len(code);emit('eb 00')
failed=len(code);messagebox(0x60)
finished=len(code);emit('cb')
for at in failure_jumps:
    delta=failed-(at+4)
    assert 0 < delta <= 0x7fff,(at,delta)
    struct.pack_into('<h',code,at+2,delta)
assert 0 < finished-(end_jmp+2) <=127
code[end_jmp+1]=finished-(end_jmp+2)
assert len(code) < 256, len(code)
assert len(code)+2+8*len(fixups)<=512,(len(code),len(fixups))
img[0x200:0x200+len(code)]=code
u16(h+0x40,0x20);u16(h+0x42,len(code));u16(h+0x44,0x100)
u16(h+0x46,0x100);u16(h+0x48,0x40);u16(h+0x4a,0x100)
u16(h+0x4c,1);u16(h+0x4e,0x100)
img[h+0x54]=4;img[h+0x55:h+0x59]=b'USER'
u16(0x200+len(code),len(fixups))
for i,off in enumerate(fixups):
    struct.pack_into('<BBHHH',img,0x200+len(code)+2+i*8,3,1,off,1,1)
for off,t in [(0x20,b'ALU: OK\0'),(0x60,b'Win16 ALU: FAILED\0'),(0x90,b'WINN ALU and flow\0')]:
    img[0x400+off:0x400+off+len(t)]=t
with open(sys.argv[1],'wb') as f:f.write(img)
print(f'NEALU.EXE: {len(img)} bytes, code {len(code)}, asserts {len(failure_jumps)}, imports {len(fixups)}')
