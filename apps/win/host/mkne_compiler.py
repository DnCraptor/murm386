#!/usr/bin/env python3
"""Win16 compiler-ABI fixture: 260-byte prefix, ENTER frame, BX/SI saves,
BP local, conditional branches and real USER.1 MessageBox."""
import sys,struct
if len(sys.argv)!=2:raise SystemExit('usage: mkne_compiler.py OUTPUT.EXE')
b=bytearray(0x800)
def w16(at,x):struct.pack_into('<H',b,at,x)
def w32(at,x):struct.pack_into('<I',b,at,x)
w16(0,0x5a4d);w16(4,2);w16(8,4);w16(10,0x10);w16(12,0xffff)
w16(16,0x100);w16(24,0x40);w32(0x3c,0x100)
stub=bytes.fromhex('0e1fb409ba0e00cd21b8014ccd21')
msg=b'This program requires WINN. Run WINN NECABI.EXE\r\n$'
b[0x40:0x40+len(stub)]=stub;b[0x40+len(stub):0x40+len(stub)+len(msg)]=msg
h=0x100;w16(h,0x454e);w16(h+0x16,1);w16(h+0x18,0x100);w16(h+0x1a,2);w16(h+0x1c,2);w16(h+0x1e,1);w16(h+0x22,0x40);w16(h+0x28,0x50);w16(h+0x2a,0x54);w16(h+0x32,4);b[h+0x36]=2
code=bytearray(b'\x90'*260);fixups=[]
def e(s):code.extend(bytes.fromhex(s))
def msgbox(o):
 e('31 c0 50');e(f'1e 68 {o:02x} 00');e('1e 68 90 00 50');fixups.append(len(code)+1);e('9a ff ff ff ff')
e('c8 02 00 00') # ENTER 2,0
# preserve BX/SI, modify them, store/read BP local
e('53 56 bb 34 12 be 21 00 b8 67 45 89 46 fe 8b 46 fe')
e('3d 67 45') # CMP AX,4567h
jn= len(code);e('0f 85 00 00')
msgbox(0x20)
jmp=len(code);e('eb 00')
bad=len(code);msgbox(0x60)
cleanup=len(code);e('5e 5b c9 cb')
struct.pack_into('<h',code,jn+2,bad-(jn+4))
assert 0<cleanup-(jmp+2)<=127;code[jmp+1]=cleanup-(jmp+2)
assert 256<len(code)<512 and len(code)+2+8*len(fixups)<0x400
b[0x200:0x200+len(code)]=code
w16(h+0x40,0x20);w16(h+0x42,len(code));w16(h+0x44,0x100);w16(h+0x46,0x100)
w16(h+0x48,0x60);w16(h+0x4a,0x100);w16(h+0x4c,1);w16(h+0x4e,0x100)
b[h+0x54]=4;b[h+0x55:h+0x59]=b'USER'
w16(0x200+len(code),len(fixups))
for i,off in enumerate(fixups):struct.pack_into('<BBHHH',b,0x200+len(code)+2+8*i,3,1,off,1,1)
for off,text in [(0x20,b'COMPILER ABI: OK\0'),(0x60,b'COMPILER ABI: FAILED\0'),(0x90,b'WINN C ABI\0')]:b[0x600+off:0x600+off+len(text)]=text
with open(sys.argv[1],'wb') as f:f.write(b)
print('NECABI.EXE',len(b),'code',len(code),'relocs',len(fixups))
