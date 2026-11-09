#!/usr/bin/env python3
"""One Win16 NE program: shared near helper, bounded short/near Jcc,
DEC/JNZ loops, JCXZ and conditional LOOPE/LOOPNE with unknown AL.
Every failed comparison goes through a real USER.1 MessageBox.
"""
import struct
import sys

if len(sys.argv)!=2:
    raise SystemExit('usage: mkne_bounded_flow.py OUTPUT.EXE')
img=bytearray(0x800)
def w16(a,n): struct.pack_into('<H',img,a,n)
def w32(a,n): struct.pack_into('<I',img,a,n)
for off,num in [(0,0x5a4d),(4,2),(8,4),(0x0a,0x10),(0x0c,0xffff),(0x10,0x100),(0x18,0x40)]:w16(off,num)
w32(0x3c,0x100)
stub=bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg=b'This program requires WINN. Run WINN NEFLOW.EXE\r\n$'
img[0x40:0x40+len(stub)]=stub
img[0x40+len(stub):0x40+len(stub)+len(msg)]=msg
h=0x100
for off,num in [(0,0x454e),(0x16,1),(0x18,0x100),(0x1a,2),(0x1c,2),(0x1e,1),(0x22,0x40),(0x28,0x50),(0x2a,0x54),(0x32,4)]:w16(h+off,num)
img[h+0x36]=2
code=bytearray(); labels={}; jumps=[];fixups=[]; checks=0
def label(name):
    assert name not in labels
    labels[name]=len(code)
def emit(hextext):code.extend(bytes.fromhex(hextext))
def jump8(op,name):
    at=len(code); code.extend((op,0)); jumps.append((at,2,name))
def jump16(op,name):
    at=len(code); code.extend((0x0f,op,0,0)); jumps.append((at,4,name))
def call(name):
    at=len(code);code.extend((0xe8,0,0));jumps.append((at,3,name))
def check():
    global checks
    jump16(0x85,'fail')
    checks+=1
def box(off):
    emit('31 c0 50')
    emit(f'1e 68 {off:02x} 00 1e 68 90 00 50')
    fixups.append(len(code)+1)
    emit('9a ff ff ff ff')

# Helper located before the caller, skipped by the program entry JMP.
jump8(0xeb,'main')
label('helper')
emit('b8 42 00 c3')          # MOV AX,42h; near RET
label('main')
call('helper')
emit('3d 42 00');check()
call('helper')
emit('3d 42 00');check()

emit('bb 00 00')             # BX=0
label('short_loop')
emit('43 83 fb 10')          # INC BX; CMP BX,16
jump8(0x72,'short_loop')    # JB (backward, exactly determined)
emit('83 fb 10');check()

emit('b9 08 00 ba 00 00')   # CX=8, DX=0
label('dec_loop')
emit('42 49')                # INC DX; DEC CX (ZF known)
jump8(0x75,'dec_loop')      # JNZ backward based on DEC CX
emit('83 fa 08');check()

emit('be 60 00')            # SI=0x60
label('near_loop')
emit('46 83 fe 70')          # INC SI; CMP SI,0x70
jump16(0x82,'near_loop')   # 386 JB rel16 backwards
emit('83 fe 70');check()

emit('b9 00 00')            # CX=0
jump8(0xe3,'skip_fail')    # JCXZ forward deterministic
jump8(0xeb,'fail')
label('skip_fail')

# Unknown AL from memory: LOOPE/LOOPNE must validate both outcomes.
emit('fc be b0 00 b9 04 00') # CLD, SI=0x70, CX=4
label('loopne')
emit('ac 84 c0')             # LODSB; TEST AL,AL
jump8(0xe0,'loopne')       # LOOPNE conditional bounded by exact CX
emit('be b0 00 b9 04 00')
label('loope')
emit('ac 84 c0')
jump8(0xe1,'loope')       # LOOPE conditional bounded by exact CX

label('success')
box(0x20)
jump8(0xeb,'end')
label('fail')
box(0x60)
label('end')
emit('cb')

for off,size,target in jumps:
    assert target in labels, target
    disp=labels[target]-(off+size)
    if size==2:
        assert -128<=disp<=127,(off,target,disp)
        code[off+1]=disp&255
    elif size==3:
        assert -32768<=disp<=32767
        struct.pack_into('<h',code,off+1,disp)
    else:
        assert -32768<=disp<=32767
        struct.pack_into('<h',code,off+2,disp)
assert checks==5
assert len(code)<=256, len(code)
assert len(code)+2+len(fixups)*8 <= 0x200
img[0x200:0x200+len(code)]=code
for off,num in [(0x40,0x20),(0x42,len(code)),(0x44,0x100),(0x46,0x100),(0x48,0x40),(0x4a,0x100),(0x4c,1),(0x4e,0x100)]:w16(h+off,num)
img[h+0x54]=4;img[h+0x55:h+0x59]=b'USER'
w16(0x200+len(code),len(fixups))
for i,off in enumerate(fixups):
    struct.pack_into('<BBHHH',img,0x200+len(code)+2+8*i,3,1,off,1,1)
for offset,text in [(0x20,b'BOUNDED FLOW: OK\0'),(0x60,b'BOUNDED FLOW: FAILED\0'),(0x90,b'WINN bounded C loops\0'),(0xb0,b'AB\x00C')]:
    img[0x400+offset:0x400+offset+len(text)]=text
with open(sys.argv[1],'wb') as f:f.write(img)
print('NEFLOW.EXE:',len(img),'bytes, code',len(code),'bytes,',checks,'checks,',len(fixups),'fixups')
