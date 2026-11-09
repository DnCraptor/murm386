#!/usr/bin/env python3
"""Emit a real relocatable 16-bit NE with checked DS globals and near helper."""
import struct, sys
if len(sys.argv)!=2:raise SystemExit('usage: mkne_globals.py NEGLOBAL.EXE')
img=bytearray(2048)
def w16(off,n):struct.pack_into('<H',img,off,n)
def w32(off,n):struct.pack_into('<I',img,off,n)
for off,n in [(0,0x5a4d),(4,2),(8,4),(0x0a,0x10),(0x0c,0xffff),(0x10,0x100),(0x18,0x40)]:w16(off,n)
w32(0x3c,0x100)
stub=bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg=b'This program requires WINN. Run WINN NEGLOBAL.EXE\r\n$'
img[0x40:0x40+len(stub)]=stub
img[0x40+len(stub):0x40+len(stub)+len(msg)]=msg
h=0x100
for off,n in [(0,0x454e),(0x16,1),(0x18,0x100),(0x1a,2),(0x1c,2),(0x1e,1),(0x22,0x40),(0x28,0x50),(0x2a,0x54),(0x32,4)]:w16(h+off,n)
img[h+0x36]=2
code=bytearray();labels={};relocs=[];jumps=[];checks=0
def emit(hx):code.extend(bytes.fromhex(hx))
def label(name):
    assert name not in labels
    labels[name]=len(code)
def rel16(op,target):
    p=len(code);code.extend((op,0,0));jumps.append((p,3,target))
def jnz(target):
    global checks
    p=len(code);code.extend((0x0f,0x85,0,0));jumps.append((p,4,target));checks+=1
def jmp(target):
    p=len(code);code.extend((0xeb,0));jumps.append((p,2,target))
def messagebox(text_offset):
    emit('31 c0 50')                  # NULL owner
    emit('ff 76 fe')                  # proven DS selector from BP-2
    emit(f'68 {text_offset:02x} 00')  # text
    emit('ff 76 fe 68 90 00 50')     # DS:caption, MB_OK
    relocs.append(len(code)+1);emit('9a ff ff ff ff')

label('entry')
emit('c8 02 00 00 8c d8 89 46 fe') # ENTER 2,0; save DS to local
# A global word stores a pointer to another checked DATA offset.
emit('c7 06 04 00 b0 00')       # WORD[0004h] = 00b0h
emit('8b 1e 04 00')             # BX = WORD[0004h]
emit('c6 07 46')                # BYTE[BX] = 'F'
emit('b8 00 00 8a 07 3c 46')   # AX=0; AL=[BX]; CMP AL,'F'
jnz('failed')
# An unaligned symbolic read after an overlapping byte update.
emit('c7 06 08 00 34 12')       # WORD[8]=1234h
emit('c6 06 09 00 56')          # BYTE[9]=56h => WORD[8]=5634h
emit('8b 16 08 00 81 fa 34 56')# DX=[8]; CMP DX,5634h
jnz('failed')
# AH can load a verified byte from static DATA, not just AL.
emit('b8 00 33 88 26 0a 00')   # AH=33h -> BYTE[0Ah]
emit('b8 00 00 8a 26 0a 00 3d 00 33') # AH=[0Ah]; CMP AX,3300h
jnz('failed')
# A known DS global pointer is passed as a near PASCAL argument.
emit('c7 06 04 00 b8 00')
emit('ff 36 04 00')             # PUSH WORD [0004h]
rel16(0xe8,'helper')
emit('bb b8 00 b8 00 00 8a 07 3c 42') # AL=[B8h], compare 'B'
jnz('failed')
messagebox(0x20)
jmp('exit')
label('failed');messagebox(0x60)
label('exit');emit('c9 cb')
label('helper')
emit('c8 00 00 00 8b 5e 04') # ENTER 0,0; MOV BX,[BP+4]
emit('c6 07 42 c9 c2 02 00')  # [BX]='B'; LEAVE; RET 2
for p,n,target in jumps:
    delta=labels[target]-(p+n)
    if n==2:
        assert -128<=delta<=127,(p,target,delta)
        code[p+1]=delta&255
    elif n==3:struct.pack_into('<h',code,p+1,delta)
    else:struct.pack_into('<h',code,p+2,delta)
assert checks==4
assert 100<len(code)<512
assert len(code)+2+8*len(relocs)<512
img[0x200:0x200+len(code)]=code
for off,num in [(0x40,0x20),(0x42,len(code)),(0x44,0x100),(0x46,0x100),(0x48,0x40),(0x4a,0x100),(0x4c,1),(0x4e,0x100)]:w16(h+off,num)
img[h+0x54]=4;img[h+0x55:h+0x59]=b'USER'
w16(0x200+len(code),len(relocs))
for i,p in enumerate(relocs):struct.pack_into('<BBHHH',img,0x200+len(code)+2+8*i,3,1,p,1,1)
for off,s in [(0x20,b'DS GLOBALS: OK\0'),(0x60,b'DS GLOBALS: FAILED\0'),(0x90,b'WINN checked DATA variables\0')]:
    img[0x400+off:0x400+off+len(s)]=s
with open(sys.argv[1],'wb') as f:f.write(img)
print('NEGLOBAL.EXE:',len(img),'bytes; code=',len(code),'relocs=',len(relocs),'checks=',checks,'helper=',labels['helper'])
