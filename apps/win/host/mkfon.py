#!/usr/bin/env python3
"""Generate WINNTEST.FON: a raster font file as Windows has them (an NE
file, its RT_FONT resources FNT version 2), the face "Winn Test" at two
sizes, made from WINN's own VGA glyphs (core/sysfont.c, CP866; the
characters 80h to FFh Windows-1251's, as WINN maps them):

    12 pixels: the glyphs' rows 2..15 every 7th left out, proportional
    24 pixels: 1.5 times the 16 rows, each pixel 2 wide (bold, 700)

    mkfon.py OUTPUT.FON
"""
import os
import re
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: mkfon.py OUTPUT.FON')
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'core', 'sysfont.c')).read()
body = src[src.index('{', src.index('win_font8x16')):]
vga = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', body)][:4096]
assert len(vga) == 4096

# Windows-1251 80h..FFh: the CP866 glyph (core/gdi_text.c's table)
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

def glyph_rows(ch):
    g = ch if ch < 0x80 else ansi[ch - 0x80]
    rows = vga[g * 16:g * 16 + 16]          # bit 0 the leftmost
    return [sum(1 << (7 - i) for i in range(8) if r >> i & 1) for r in rows]   # bit 7

def trim(rows):
    bits = 0
    for r in rows:
        bits |= r
    if not bits:
        return rows, 4
    left = 0
    while not bits & (0x80 >> left):
        left += 1
    right = 7
    while not bits & (0x80 >> right):
        right -= 1
    return [(r << left) & 0xff for r in rows], right - left + 2

def fnt(points, height, ascent, weight, make):
    first, last = 0x20, 0xff
    glyphs = [make(c) for c in range(first, last + 1)]   # (width, rows, bits a row)
    glyphs.append(glyphs[0])                              # the extra entry: a space
    hdr = 118
    table = 4 * len(glyphs)
    face = b'Winn Test\0'
    off = hdr + table
    data = bytearray()
    entries = []
    for w, rows, bits in glyphs:
        entries.append((w, off + len(data)))
        for c in range((w + 7) // 8):        # columns of 8 pixels, the rows each
            for r in rows:
                data.append((r >> (bits - 8 * (c + 1))) & 0xff)
    face_at = off + len(data)
    size = face_at + len(face)
    avg = glyphs[ord('x') - first][0]
    maxw = max(g[0] for g in glyphs)
    h = struct.pack('<HI60sHHHHHHHBBBHBHHBHHBBBBHIIIIB',
                    0x200, size, b'(c) murm386 contributors, MIT License', 0, points, 96, 96,
                    ascent, 2 if height == 12 else 4, 0, 0, 0, 0, weight, 0, 0, height,
                    0x21, avg, maxw, first, last, ord('?') - first, 0, 0, 0, face_at, 0, 0, 0)
    assert len(h) == hdr, len(h)
    t = b''.join(struct.pack('<HH', w, o) for w, o in entries)
    return h + t + bytes(data) + face

def small(ch):                               # 12 pixels: rows 2..15, two left out
    rows, w = trim(glyph_rows(ch))
    rows = rows[2:16]
    rows = [r << 8 for i, r in enumerate(rows) if i not in (3, 10)]
    return w, rows, 16

def large(ch):                               # 24 pixels, each pixel 2 wide
    rows, w = trim(glyph_rows(ch))
    out = []
    for i in range(24):
        r = rows[i * 2 // 3]
        wide = 0
        for b in range(8):
            if r & (0x80 >> b):
                wide |= 3 << (14 - 2 * b)
        out.append(wide << 8)
    return 2 * w, out, 24

fonts = [fnt(9, 12, 10, 400, small), fnt(18, 24, 20, 700, large)]

# the NE file: an MZ stub, the NE header, its resource table (the fonts,
# RT_FONT) and resident names (the module's), the fonts
shift = 4
restab = bytearray(struct.pack('<HHHI', shift, 0x8008, len(fonts), 0))
at_entries = len(restab)
restab += bytes(12 * len(fonts)) + struct.pack('<H', 0)
resname = b'\x08WINNTEST\x00\x00\x00'
ne = bytearray(0x40)
def ne16(o, v): struct.pack_into('<H', ne, o, v)
ne16(0, 0x454e)
ne[2] = 5
ne[3] = 10
ne16(0x0c, 0x8000)                          # a library: no code, no data
ne16(0x22, 0x40)                            # no segments
ne16(0x24, 0x40)                            # the resources
ne16(0x26, 0x40 + len(restab))              # the resident names
for o in (0x04, 0x28, 0x2a):                # entries, modules, imports: none
    ne16(o, 0x40 + len(restab) + len(resname))
ne16(0x32, shift)
ne[0x36] = 2                                # Windows
ne16(0x3e, 0x30a)
mz = bytearray(0x80)
struct.pack_into('<HHH', mz, 0, 0x5a4d, 0x80, 1)
struct.pack_into('<H', mz, 8, 4)
struct.pack_into('<H', mz, 0x18, 0x40)
struct.pack_into('<I', mz, 0x3c, 0x80)
head = mz + ne + restab + resname
pos = (len(head) + 15) & ~15
blob = bytearray()
place = []
for f in fonts:
    while (pos + len(blob)) % (1 << shift):
        blob.append(0)
    place.append((pos + len(blob)) >> shift)
    blob += f
for i, f in enumerate(fonts):
    struct.pack_into('<HHHHHH', head, 0xc0 + at_entries + 12 * i, place[i],
                     (len(f) + (1 << shift) - 1) >> shift, 0x0c30, 0x8001 + i, 0, 0)
out = bytes(head) + bytes(pos - len(head)) + bytes(blob)
out += bytes(-len(out) % (1 << shift))      # the last one's alignment too
with open(sys.argv[1], 'wb') as f:
    f.write(out)
print(os.path.basename(sys.argv[1]).upper() + ':', len(out), 'bytes; Winn Test 12 and 24')
