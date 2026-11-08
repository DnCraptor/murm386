#!/usr/bin/env python3
"""
mklogo.py - the start-up logo of WINN.EXE from a picture (needs Pillow).

    python3 mklogo.py logo.png [out_dir]

Writes two 8 bpp RLE8 BMP files for the Windows directory:

  WINLOGO.BMP   up to 236 colours, 480 x 360 (fits 640 x 400), for the
                256-colour displays: WINN loads its colours into the free
                middle of the hardware palette while the logo is up
  WINLOG16.BMP  the 16 VGA colours, ordered dither, 640 x 480 (fits
                800 x 600), for the 16-colour displays

WINN shows the one that suits the display (a larger one is scaled down),
centred on its background colour, the colour of the top-left pixel.
"""
import struct
import sys

from PIL import Image

VGA16 = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
    (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
    (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def clean(im):
    """The near-white of the background (compression noise) made white:
    long runs, a small file."""
    def snap(p):
        r, g, b = p
        return (255, 255, 255) if min(p) >= 232 and max(p) - min(p) <= 16 else p
    out = im.copy()
    out.putdata([snap(p) for p in im.getdata()])
    return out


def fit(im, w, h):
    s = min(w / im.width, h / im.height)
    return im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)


def rle8(rows):
    """BI_RLE8 data of the rows, bottom row first as BMP keeps them."""
    out = bytearray()
    for row in reversed(rows):
        i, n = 0, len(row)
        while i < n:
            run = 1
            while i + run < n and run < 255 and row[i + run] == row[i]:
                run += 1
            if run >= 3 or i + run == n:
                out += bytes((run, row[i]))
                i += run
                continue
            # a literal stretch until the next run of three
            j = i
            while j < n and j - i < 255:
                if j + 2 < n and row[j] == row[j + 1] == row[j + 2]:
                    break
                j += 1
            lit = row[i:j]
            if len(lit) < 3:
                for v in lit:
                    out += bytes((1, v))
            else:
                out += bytes((0, len(lit))) + bytes(lit)
                if len(lit) & 1:
                    out += b'\0'
            i = j
        out += b'\0\0'                      # end of line
    out += b'\0\1'                          # end of bitmap
    return bytes(out)


def write_bmp(path, w, h, palette, rows):
    data = rle8(rows)
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in palette)
    off = 14 + 40 + len(pal)
    head = struct.pack('<2sIHHI', b'BM', off + len(data), 0, 0, off)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 8, 1, len(data), 2835, 2835, len(palette), len(palette))
    with open(path, 'wb') as f:
        f.write(head + info + pal + data)
    print('%s: %d x %d, %d colours, %d bytes' % (path, w, h, len(palette), off + len(data)))


def make256(src, path):
    im = clean(fit(src, 480, 360))
    q = im.quantize(colors=236, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat = q.getpalette()[:236 * 3]
    px = q.load()
    used = sorted({px[x, y] for y in range(q.height) for x in range(q.width)})
    remap = {v: k for k, v in enumerate(used)}
    palette = [tuple(flat[3 * v:3 * v + 3]) for v in used]
    rows = [bytes(remap[px[x, y]] for x in range(q.width)) for y in range(q.height)]
    write_bmp(path, q.width, q.height, palette, rows)


def nearest16(r, g, b):
    return min(range(16), key=lambda i: (VGA16[i][0] - r) ** 2 * 3 + (VGA16[i][1] - g) ** 2 * 4 +
               (VGA16[i][2] - b) ** 2 * 2)


def make16(src, path):
    im = clean(fit(src, 640, 480).convert('RGB'))
    px = im.load()
    rows = []
    for y in range(im.height):
        row = bytearray()
        for x in range(im.width):
            r, g, b = px[x, y]
            if (r, g, b) == (255, 255, 255):
                row.append(15)
                continue
            t = (BAYER4[y & 3][x & 3] - 7.5) * 6     # a light ordered dither
            row.append(nearest16(min(255, max(0, int(r + t))), min(255, max(0, int(g + t))),
                                 min(255, max(0, int(b + t)))))
        rows.append(bytes(row))
    write_bmp(path, im.width, im.height, VGA16, rows)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    out = sys.argv[2] if len(sys.argv) > 2 else '.'
    src = Image.open(sys.argv[1]).convert('RGB')
    make256(src, out + '/WINLOGO.BMP')
    make16(src, out + '/WINLOG16.BMP')


if __name__ == '__main__':
    main()
