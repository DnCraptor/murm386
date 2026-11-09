#!/usr/bin/env python3
"""Generate Win16 NE integration fixture for symbolic stack locals and frames."""
import struct,sys
if len(sys.argv)!=2:raise SystemExit('usage: mkne_locals.py NELOCAL.EXE')
img=bytearray(2048)
def w16(off,val):struct.pack_into('<H',img,off,val)
def w32(off,val):struct.pack_into('<I',img,off,val)
for off,n in [(0,0x5a4d),(4,2),(8,4),(0x0a,0x10),(0x0c,0xffff),(0x10,0x100),(0x18,0x40)]:w16(off,n)
w32(0x3c,0x100)
stub=bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg=b'This program requires WINN. Run WINN NELOCAL.EXE\r\n$'
img[0x40:0x40+len(stub)]=stub
img[0x40+len(stub):0x40+len(stub)+len(msg)]=msg
h=0x100
for off,n in [(0,0x454e),(0x16,1),(0x18,0x100),(0x1a,2),(0x1c,2),(0x1e,1),(0x22,0x40),(0x28,0x50),(0x2a,0x54),(0x32,4)]:w16(h+off,n)
img[h+0x36]=2
code=bytearray();labels={};jumps=[];fixups=[];checks=0
def emit(s):code.extend(bytes.fromhex(s))
def label(x):
    assert x not in labels
    labels[x]=len(code)
def rel16(op,x):
    p=len(code);code.extend((op,0,0));jumps.append((p,3,x))
def jne(x):
    global checks
    p=len(code);code.extend((0x0f,0x85,0,0));jumps.append((p,4,x));checks+=1
def jmp(x):
    p=len(code);code.extend((0xeb,0));jumps.append((p,2,x))
def box(off):
    emit('31 c0 50')  # owner = 0
    emit('ff 76 fc')  # PUSH WORD [BP-4] = preserved DS selector
    emit(f'68 {off:02x} 00')
    emit('ff 76 fc 68 90 00 50')
    fixups.append(len(code)+1);emit('9a ff ff ff ff')
# Three two-byte local slots at BP-2/-4/-6. MessageBox uses 12 stack
# bytes; ENTER 6 reserves 8, staying within the 20-byte budget.
label('entry');emit('c8 06 00 00')
emit('b8 20 00 89 46 fe') # [BP-2] = DS pointer 0x20
emit('8c d8 89 46 fc') # [BP-4] = proven DS selector
# Forward helper must not overwrite the caller's local provenance.
rel16(0xe8,'helper')
emit('8b 5e fe 8a 07 3c 46') # BX=local pointer; AL='F'
jne('fail')
# Symbolic value tracking through low/high bytes of a local word.
emit('b8 34 12 89 46 fa b0 78 88 46 fa') # [BP-6] = 0x1278
emit('b9 00 00 8a 4e fa 88 4e fb')       # CL=0x78, [BP-5]=CL
emit('8b 56 fa 81 fa 78 78')             # DX must equal 0x7878
jne('fail')
# Generalized DS byte MOV: AH,[BX] (not only AL).
emit('8a 27')
# Success path and common epilogue.
box(0x20);jmp('finish')
label('fail');box(0x60)
label('finish');emit('c9 cb')
# Near helper with saved SI BEFORE local allocation. BP-2 is saved SI,
# the only actual local is BP-4. Returning restores caller's locals.
label('helper')
emit('55 8b ec 56 83 ec 02')
emit('c7 46 fc 20 00 8b 76 fc 8a 04')
emit('83 c4 02 5e c9 c3')
for p,n,target in jumps:
    v=labels[target]-(p+n)
    if n==2:
        assert -128<=v<=127,(p,target,v)
        code[p+1]=v&255
    elif n==3:struct.pack_into('<h',code,p+1,v)
    else:struct.pack_into('<h',code,p+2,v)
assert checks==2 and len(code)<512
assert len(code)+2+8*len(fixups)<512
img[0x200:0x200+len(code)]=code
for off,num in [(0x40,0x20),(0x42,len(code)),(0x44,0x100),(0x46,0x100),(0x48,0x40),(0x4a,0x100),(0x4c,1),(0x4e,0x100)]:w16(h+off,num)
img[h+0x54]=4;img[h+0x55:h+0x59]=b'USER'
w16(0x200+len(code),len(fixups))
for i,p in enumerate(fixups):struct.pack_into('<BBHHH',img,0x200+len(code)+2+8*i,3,1,p,1,1)
for off,s in [(0x20,b'FRAME LOCALS: OK\0'),(0x60,b'FRAME LOCALS: FAILED\0'),(0x90,b'WINN checked local variables\0')]:
    img[0x400+off:0x400+off+len(s)]=s
with open(sys.argv[1],'wb') as f:f.write(img)
print('NELOCAL.EXE:',len(img),'bytes, code=',len(code),'fixups=',len(fixups),'checks=',checks,'helper=',labels['helper'])
