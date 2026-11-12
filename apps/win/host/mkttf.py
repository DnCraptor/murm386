#!/usr/bin/env python3
"""Generate WINNTEST.TTF and WINNTEST.FOT: a TrueType font file as Windows
has them (head, hhea, maxp, OS/2, hmtx, cmap format 4, loca, glyf, name,
post), the face "Winn Test TT", made from WINN's own VGA glyphs
(core/sysfont.c, CP866; the characters 80h to FFh Windows-1251's, as WINN
maps them, in Unicode): each pixel a square of 64 units, 16 rows to the
em of 1024 (13 above the baseline), each glyph as wide as its pixels and
one more; and the .FOT file naming it (an NE file, "FONTRES" in its
non-resident names, its RT_FONT the face and the .TTF file's name), as
WIN.INI's [fonts] has a TrueType font.

    mkttf.py OUTPUT.TTF OUTPUT.FOT
"""
import os
import re
import struct
import sys

if len(sys.argv) != 3:
    raise SystemExit('usage: mkttf.py OUTPUT.TTF OUTPUT.FOT')
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'core', 'sysfont.c')).read()
body = src[src.index('{', src.index('win_font8x16')):]
vga = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', body)][:4096]
assert len(vga) == 4096

text = open(os.path.join(here, '..', 'core', 'gdi_text.c')).read()
tab = text[text.index('ansi_to_oem[128] = {'):text.index('};', text.index('ansi_to_oem[128]'))]
tab = re.sub(r'/\*.*?\*/', '', tab.split('{', 1)[1])
ansi = []
for item in tab.split(','):
    item = item.strip()
    if not item:
        continue
    ansi.append(ord(item[1]) if item.startswith("'") and len(item) == 3
                else ord('\'') if item == "'\\''" else int(item, 0))
assert len(ansi) == 128, len(ansi)

FACE = 'Winn Test TT'
EM, PX, ASC = 1024, 64, 13                  # 16 rows: 13 above the baseline


def glyph_rows(ch):
    g = ch if ch < 0x80 else ansi[ch - 0x80]
    rows = vga[g * 16:g * 16 + 16]          # bit 0 the leftmost
    return [sum(1 << (7 - i) for i in range(8) if r >> i & 1) for r in rows]   # bit 7


def unicode_of(ch):
    if ch < 0x80:
        return ch
    try:
        return ord(bytes([ch]).decode('cp1251'))
    except UnicodeDecodeError:
        return None


# the glyphs: .notdef, then each character (its pixels' runs: rectangles)
glyphs = [([], 512)]                        # (contours, advance)
cmap = {}
for ch in range(0x20, 0x100):
    u = unicode_of(ch)
    if u is None or u in cmap:
        continue
    rows = glyph_rows(ch)
    bits = 0
    for r in rows:
        bits |= r
    left = 0
    right = -1
    if bits:
        while not bits & (0x80 >> left):
            left += 1
        right = 7
        while not bits & (0x80 >> right):
            right -= 1
    contours = []
    for y, r in enumerate(rows):
        x = left
        while x <= right:
            if r & (0x80 >> x):
                x0 = x
                while x <= right and r & (0x80 >> x):
                    x += 1
                X0, X1 = (x0 - left) * PX, (x - left) * PX
                top, bot = (ASC - y) * PX, (ASC - y - 1) * PX
                contours.append([(X0, bot), (X0, top), (X1, top), (X1, bot)])   # clockwise
            else:
                x += 1
    advance = ((right - left + 2) if bits else 4) * PX
    cmap[u] = len(glyphs)
    glyphs.append((contours, advance))


def glyph_data(contours):
    if not contours:
        return b''
    pts = [p for c in contours for p in c]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    out = struct.pack('>hhhhh', len(contours), min(xs), min(ys), max(xs), max(ys))
    end = -1
    for c in contours:
        end += len(c)
        out += struct.pack('>H', end)
    out += struct.pack('>H', 0)             # no instructions
    out += bytes([1]) * len(pts)            # all on the curve, each x and y a word
    px = py = 0
    for x, y in pts:
        out += struct.pack('>h', x - px)
        px = x
    for x, y in pts:
        out += struct.pack('>h', y - py)
        py = y
    return out


glyf = b''
loca = []
for contours, adv in glyphs:
    loca.append(len(glyf))
    glyf += glyph_data(contours)
    glyf += bytes(-len(glyf) % 4)
loca.append(len(glyf))
n = len(glyphs)
advmax = max(a for c, a in glyphs)
avg = sum(a for c, a in glyphs[1:]) // (n - 1)

tables = {}
tables[b'head'] = struct.pack('>IIIIHHqqhhhhHHhhh', 0x10000, 0x10000, 0, 0x5F0F3CF5, 0x000B, EM,
                              0, 0, 0, -(16 - ASC) * PX, 8 * PX, ASC * PX, 0, 8, 2, 1, 0)
tables[b'hhea'] = struct.pack('>IhhhHhhhhhh4hhH', 0x10000, ASC * PX, -(16 - ASC) * PX, 0, advmax,
                              0, 0, 8 * PX, 1, 0, 0, 0, 0, 0, 0, 0, n)
tables[b'maxp'] = struct.pack('>IHHHHHHHHHHHHHH', 0x10000, n, 64, 16, 0, 0, 2, 0, 0, 0, 0, 0, 0,
                              0, 0)
tables[b'OS/2'] = (struct.pack('>HhHHHhhhhhhhhhhh', 1, avg, 400, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               64, 0, 8 << 8) +
                   bytes([2, 11, 5, 3, 0, 0, 0, 0, 0, 0]) +              # PANOSE: text, sans
                   struct.pack('>IIII', 3, 0x200, 0, 0) + b'WINN' +
                   struct.pack('>HHHhhhHH', 0x40, 0x20, 0x4FF, ASC * PX, -(16 - ASC) * PX, 0,
                               ASC * PX, (16 - ASC) * PX) +
                   struct.pack('>II', 0x5, 0))                            # Latin 1, Cyrillic
tables[b'hmtx'] = b''.join(struct.pack('>Hh', a, 0) for c, a in glyphs)
tables[b'loca'] = b''.join(struct.pack('>I', o) for o in loca)
tables[b'glyf'] = glyf

# cmap: Windows Unicode, format 4, a segment each character, then FFFFh
codes = sorted(cmap)
segs = [(u, u, (cmap[u] - u) & 0xffff) for u in codes] + [(0xffff, 0xffff, 1)]
sc = len(segs)
search = 1
while search * 2 <= sc:
    search *= 2
f4 = struct.pack('>HHHHHHH', 4, 16 + 8 * sc, 0, 2 * sc, 2 * search, (search.bit_length() - 1),
                 2 * sc - 2 * search)
f4 += b''.join(struct.pack('>H', e) for s, e, d in segs) + b'\0\0'
f4 += b''.join(struct.pack('>H', s) for s, e, d in segs)
f4 += b''.join(struct.pack('>H', d) for s, e, d in segs)
f4 += bytes(2 * sc)
tables[b'cmap'] = struct.pack('>HHHHI', 0, 1, 3, 1, 12) + f4

# name: family, style, full name, PostScript name; Windows' and the Mac's
names = {1: FACE, 2: 'Regular', 4: FACE, 6: 'WinnTestTT'}
recs, strs = [], b''
for plat, enc, lang, encode in ((1, 0, 0, lambda s: s.encode('ascii')),
                                (3, 1, 0x409, lambda s: s.encode('utf-16-be'))):
    for nid, s in names.items():
        b = encode(s)
        recs.append(struct.pack('>HHHHHH', plat, enc, lang, nid, len(b), len(strs)))
        strs += b
tables[b'name'] = struct.pack('>HHH', 0, len(recs), 6 + 12 * len(recs)) + b''.join(recs) + strs
tables[b'post'] = struct.pack('>IIhhIIIII', 0x30000, 0, -128, 64, 0, 0, 0, 0, 0)


def checksum(b):
    b += bytes(-len(b) % 4)
    return sum(struct.unpack('>%dI' % (len(b) // 4), b)) & 0xffffffff


tags = sorted(tables)
off = 12 + 16 * len(tags)
directory = struct.pack('>IHHHH', 0x10000, len(tags), 16 * 8, 4, 16 * len(tags) - 16 * 8)
data = b''
for t in tags:
    b = tables[t]
    directory += struct.pack('>4sIII', t, checksum(b), off + len(data), len(b))
    data += b + bytes(-len(b) % 4)
ttf = directory + data
with open(sys.argv[1], 'wb') as f:
    f.write(ttf)

# the .FOT: an NE library, its non-resident names "FONTRES 100,96,96 : face",
# one RT_FONT: a font header (no glyphs) whose device name is the .TTF file
ttf_name = os.path.basename(sys.argv[1]).upper().encode()
hdr = bytearray(118)
struct.pack_into('<H', hdr, 0, 0x200)
hdr[66] = 3                                 # dfType: a vector font's bits (none here)
struct.pack_into('<H', hdr, 68, 12)         # points
struct.pack_into('<HH', hdr, 70, 96, 96)
dev = len(hdr)
face_at = dev + len(ttf_name) + 1
struct.pack_into('<I', hdr, 101, dev)       # dfDevice: the TrueType file
struct.pack_into('<I', hdr, 105, face_at)   # dfFace
font = bytes(hdr) + ttf_name + b'\0' + FACE.encode() + b'\0'
font = bytearray(font)
struct.pack_into('<I', font, 2, len(font))  # dfSize
shift = 4
restab = bytearray(struct.pack('<HHHI', shift, 0x8008, 1, 0))
at_entry = len(restab)
restab += bytes(12) + struct.pack('<H', 0)
resname = b'\x08WINNTEST\x00\x00\x00'
desc = ('FONTRES 100,96,96 : ' + FACE).encode()
nonres = bytes([len(desc)]) + desc + b'\0\0\0'
ne = bytearray(0x40)
struct.pack_into('<H', ne, 0, 0x454e)
ne[2], ne[3] = 5, 10
struct.pack_into('<H', ne, 0x0c, 0x8000)    # a library
struct.pack_into('<H', ne, 0x22, 0x40)      # no segments
struct.pack_into('<H', ne, 0x24, 0x40)      # the resources
struct.pack_into('<H', ne, 0x26, 0x40 + len(restab))
for o in (0x04, 0x28, 0x2a):
    struct.pack_into('<H', ne, o, 0x40 + len(restab) + len(resname))
struct.pack_into('<H', ne, 0x32, shift)
ne[0x36] = 2
struct.pack_into('<H', ne, 0x3e, 0x30a)
mz = bytearray(0x80)
struct.pack_into('<HHH', mz, 0, 0x5a4d, 0x80, 1)
struct.pack_into('<H', mz, 8, 4)
struct.pack_into('<H', mz, 0x18, 0x40)
struct.pack_into('<I', mz, 0x3c, 0x80)
head = mz + ne + restab + resname
struct.pack_into('<I', head, 0x80 + 0x2c, len(head))             # the non-resident names
struct.pack_into('<H', head, 0x80 + 0x20, len(nonres))
head += nonres
pos = (len(head) + 15) & ~15
struct.pack_into('<HHHHHH', head, 0x80 + 0x40 + at_entry, pos >> shift,
                 (len(font) + 15) >> shift, 0x0c30, 0x8001, 0, 0)
fot = bytes(head) + bytes(pos - len(head)) + bytes(font)
fot += bytes(-len(fot) % 16)
with open(sys.argv[2], 'wb') as f:
    f.write(fot)
print(os.path.basename(sys.argv[1]).upper() + ':', len(ttf), 'bytes,',
      os.path.basename(sys.argv[2]).upper() + ':', len(fot), 'bytes;', FACE, len(glyphs), 'glyphs')
