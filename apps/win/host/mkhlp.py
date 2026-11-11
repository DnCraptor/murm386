#!/usr/bin/env python3
"""
mkhlp.py - WINNTEST.HLP, a Windows 3.1 help file for WINHELP.EXE's host
tests: three topics (the contents with a non-scrolling heading, a jump, a
pop-up, a bitmap, a tab, a table row, a macro hotspot; a second topic in
the browse sequence; the pop-up's), their context strings and [MAP]
numbers, titles, keywords, fonts, a bitmap. Written as the help compiler
writes one: an internal directory (a B-tree), |SYSTEM (3.1, LZ77 topic
blocks: here all literals), |TOPIC, |FONT, |CONTEXT, |CTXOMAP, |TTLBTREE,
|KWBTREE, |KWDATA, |bm0.

    python3 mkhlp.py WINNTEST.HLP
"""
import struct
import sys


def u16(v): return struct.pack('<H', v & 0xffff)
def u32(v): return struct.pack('<I', v & 0xffffffff)


def clong(v):
    if -0x4000 <= v < 0x4000:
        return u16(v * 2 + 0x8000)
    return u32(v * 2 + 0x80000000 + 1)


def culong(v):
    return u16(v * 2) if v < 0x8000 else u32(v * 2 + 1)


def cushort(v):
    return bytes([v * 2]) if v < 0x80 else u16(v * 2 + 1)


def cshort(v):
    return bytes([v * 2 + 0x80]) if -0x40 <= v < 0x40 else u16(v * 2 + 0x8000 + 1)


def hlp_hash(s):
    h = 0
    for c in s:
        if 'A' <= c <= 'Z': x = ord(c) - 65 + 17
        elif 'a' <= c <= 'z': x = ord(c) - 97 + 17
        elif '1' <= c <= '9': x = ord(c) - 48
        elif c == '0': x = 10
        elif c == '.': x = 12
        elif c == '_': x = 13
        else: x = 0
        if x: h = (h * 43 + x) & 0xffffffff
    return h


def lz77_literal(data):
    out = bytearray()
    for i in range(0, len(data), 8):
        out.append(0)
        out += data[i:i + 8]
    return bytes(out)


def btree(entries, structure, page_size=2048, directory=False):
    """one leaf page, the root"""
    body = b''.join(entries)
    assert 8 + len(body) <= page_size
    page = u16(page_size - 8 - len(body)) + u16(len(entries)) + u16(0xffff) + u16(0xffff) + body
    page += b'\0' * (page_size - len(page))
    head = (u16(0x293B) + u16(0x0402 if directory else 0x0002) + u16(page_size) +
            structure.encode().ljust(16, b'\0') + u16(0) + u16(0) + u16(0) + u16(0xffff) +
            u16(1) + u16(1) + u32(len(entries)))
    return head + page


# ---------------------------------------------------------------- topics

JUMP, POPUP, FONT, BREAK, END, TAB, IMAGE, ENDLINK, MACRO = range(9)


def para(runs, bits=0, fields=b''):
    """a paragraph: [(text, command, arg)], its strings and commands"""
    strings, cmds, length = bytearray(), bytearray(), 0
    for text, cmd, arg in runs:
        strings += text.encode('latin1') + b'\0'
        length += len(text)
        if cmd == FONT: cmds += b'\x80' + u16(arg)
        elif cmd == BREAK: cmds += b'\x81'
        elif cmd == END: cmds += b'\x82'
        elif cmd == TAB: cmds += b'\x83'
        elif cmd == IMAGE: cmds += b'\x86\x03' + clong(4) + u16(0) + u16(arg)
        elif cmd == ENDLINK: cmds += b'\x89'
        elif cmd == JUMP: cmds += b'\xe3' + u32(hlp_hash(arg))
        elif cmd == POPUP: cmds += b'\xe2' + u32(hlp_hash(arg))
        elif cmd == MACRO: cmds += b'\xc8' + u16(len(arg) + 1) + arg.encode() + b'\0'
        elif cmd is None: cmds += b'\xff'
    info = b'\x00\x80\x00\x00' + u16(bits) + fields
    return info, bytes(cmds), bytes(strings), length


def text_record(paras, table=None):
    """0x20 (one paragraph) or 0x23 (a table row: a paragraph a cell)"""
    if table is None:
        info, cmds, strings, length = paras[0]
        ld1 = info + cmds
    else:
        ld1, strings, length = bytearray(), bytearray(), 0
        ld1 += bytes([len(table)]) + b'\x00' + u16(0)
        for gap, width in table:
            ld1 += u16(gap) + u16(width)
        for c, (info, cmds, s, n) in enumerate(paras):
            ld1 += u16(c) + b'\0\0\0' + info + cmds
            strings += s
            length += n
        ld1 += u16(0xffff)
        ld1, strings = bytes(ld1), bytes(strings)
    body = clong(len(ld1) + 4) + cushort(length) + ld1
    return (0x23 if table else 0x20), body, strings, length


def build_topic(topics):
    """topics: [(title, macros, nonscroll paragraphs, scroll records)]; the
    |TOPIC data, the topics' offsets"""
    # the records' places: one block (fits in 16K)
    recs = []                               # (type, ld1, ld2, chars, topic index)
    for t, (title, macros, ns, sc) in enumerate(topics):
        recs.append(['head', t, title, macros, len(ns)])
        for r in ns + sc:
            recs.append(['text', t, r])
    pos, places = 0x0C, []
    sizes = []
    for r in recs:
        if r[0] == 'head':
            ld2 = r[2].encode('latin1') + b'\0' + b''.join(m.encode() + b'\0' for m in r[3])
            size = 0x15 + 28 + len(ld2)
        else:
            typ, body, strings, n = r[2]
            size = 0x15 + len(body) + len(strings)
        places.append(pos)
        sizes.append(size)
        pos += size
    # the topics' offsets: block 0, the characters before them
    chars, offsets, first = 0, {}, {}
    for i, r in enumerate(recs):
        if r[0] == 'head':
            offsets[r[1]] = chars
            first[r[1]] = i
        else:
            chars += r[2][3]
    out = bytearray()
    heads = [i for i, r in enumerate(recs) if r[0] == 'head']
    for i, r in enumerate(recs):
        nxt = places[i + 1] if i + 1 < len(recs) else 0xffffffff
        prv = places[i - 1] if i else 0xffffffff
        if r[0] == 'head':
            t = r[1]
            ld2 = r[2].encode('latin1') + b'\0' + b''.join(m.encode() + b'\0' for m in r[3])
            nscount = r[4]
            nonscroll = places[i + 1] if nscount else 0xffffffff
            scroll = places[i + 1 + nscount] if i + 1 + nscount < len(recs) and \
                recs[i + 1 + nscount][0] == 'text' else 0xffffffff
            later = [h for h in heads if h > i]
            next_topic = places[later[0]] if later else 0xffffffff
            browse_back = offsets[t - 1] if 0 < t < 2 else 0xffffffff
            browse_for = offsets[t + 1] if t == 0 else 0xffffffff
            size = sizes[i]
            topic_size = sum(sizes[i:(later[0] if later else len(recs))])
            ld1 = (u32(topic_size) + u32(browse_back) + u32(browse_for) + u32(t) +
                   u32(nonscroll) + u32(scroll) + u32(next_topic))
            out += u32(size) + u32(len(ld2)) + u32(prv) + u32(nxt) + u32(0x15 + len(ld1)) + b'\x02'
            out += ld1 + ld2
        else:
            typ, body, strings, n = r[2]
            out += u32(sizes[i]) + u32(len(strings)) + u32(prv) + u32(nxt) + \
                u32(0x15 + len(body)) + bytes([typ])
            out += body + strings
    assert len(out) < 0x4000
    head = u32(places[-1]) + u32(0x0C) + u32(places[heads[-1]])
    block = head + lz77_literal(bytes(out))
    assert len(block) <= 0x1000
    return block, offsets


def bitmap():
    """16 x 16, 4 bits, a frame and a diagonal, uncompressed"""
    w = h = 16
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            c = 1 if x in (0, w - 1) or y in (0, h - 1) else (12 if x == y or x == w - 1 - y else 15)
            row.append(c)
        rows.append(bytes((row[i] << 4) | row[i + 1] for i in range(0, w, 2)))
    bits = b''.join(reversed(rows))         # a DIB: the bottom row first
    pal = [(0, 0, 0), (0, 0, 128), (0, 128, 0), (0, 128, 128), (128, 0, 0), (128, 0, 128),
           (128, 128, 0), (192, 192, 192), (128, 128, 128), (0, 0, 255), (0, 255, 0),
           (0, 255, 255), (255, 0, 0), (255, 0, 255), (255, 255, 0), (255, 255, 255)]
    head = b'\x06\x00' + culong(0) + culong(0) + cushort(1) + cushort(4) + culong(w) + \
        culong(h) + culong(16) + culong(0) + culong(len(bits)) + culong(0)
    palette = b''.join(bytes([b, g, r, 0]) for r, g, b in pal)
    pict = len(head) + 8 + len(palette)
    picture = head + u32(pict) + u32(0) + palette + bits
    return u16(0x706C) + u16(1) + u32(8) + picture


def main(path):
    contents = ('Contents', [],
                [para([('', FONT, 1), ('WINN Help Test', END, 0), ('', None, 0)])],
                [para([('', FONT, 0), ('Go to the ', JUMP, 'second'), ('second topic', ENDLINK, 0),
                       (', or see a ', POPUP, 'popup'), ('pop-up', ENDLINK, 0),
                       ('.', END, 0), ('', IMAGE, 0), (' A bitmap', END, 0),
                       ('Tab', TAB, 0), ('stop', END, 0),
                       ('', MACRO, 'Search()'), ('Search', ENDLINK, 0), ('', None, 0)]),
                 ])
    contents_records = [text_record([contents[2][0]])]
    contents_scroll = [text_record([contents[3][0]]),
                       text_record([para([('', FONT, 0), ('Cell one', None, 0)]),
                                    para([('', FONT, 2), ('Cell two', None, 0)])],
                                   table=[(10, 110), (10, 110)])]
    second = ('Second topic', [], [],
              [text_record([para([('', FONT, 1), ('Second topic', END, 0),
                                  ('', FONT, 0),
                                  ('This is the second topic of the test file, long enough to '
                                   'be wrapped into lines in the Help window.', END, 0),
                                  ('', FONT, 2), ('Courier text', END, 0), ('', None, 0)],
                                 bits=0x0010, fields=cshort(18))])])
    popup = ('A pop-up', [], [],
             [text_record([para([('', FONT, 0), ("A pop-up's text.", END, 0), ('', None, 0)])])])
    topics = [('Contents', [], contents_records, contents_scroll),
              (second[0], [], [], second[3]), (popup[0], [], [], popup[3])]
    topic, offsets = build_topic(topics)

    titles = ['Contents', 'Second topic', 'A pop-up']
    files = {}
    system = u16(0x036C) + u16(21) + u16(1) + u32(0) + u16(4)
    for typ, data in ((1, b'WINN Help Test\0'), (2, b'Test file\0'), (3, u32(offsets[0])),
                      (4, b'BrowseButtons()\0')):
        system += u16(typ) + u16(len(data)) + data
    files['|SYSTEM'] = system
    files['|TOPIC'] = topic
    faces = [b'Helv', b'Courier']
    face_len = 20
    font_head_len = 8
    face_at = font_head_len
    desc_at = face_at + face_len * len(faces)
    descs = [(0, 20, 3, 0, (0, 0, 0)), (1, 24, 3, 0, (0, 0, 128)), (0, 20, 1, 1, (128, 0, 0))]
    font = u16(len(faces)) + u16(len(descs)) + u16(face_at) + u16(desc_at)
    font += b''.join(f.ljust(face_len, b'\0') for f in faces)
    for attr, hp, fam, face, (r, g, b) in descs:
        font += bytes([attr, hp, fam]) + u16(face) + bytes([r, g, b, 255, 255, 255])
    files['|FONT'] = font
    ctx = sorted([(hlp_hash('contents'), offsets[0]), (hlp_hash('second'), offsets[1]),
                  (hlp_hash('popup'), offsets[2])])
    files['|CONTEXT'] = btree([u32(h) + u32(o) for h, o in ctx], 'L4')
    files['|CTXOMAP'] = u16(2) + u32(1) + u32(offsets[0]) + u32(2) + u32(offsets[1])
    files['|TTLBTREE'] = btree([u32(offsets[i]) + titles[i].encode() + b'\0' for i in range(3)],
                               'Lz')
    words = [('first', [0]), ('pop-up', [2]), ('second', [1]), ('topics', [0, 1])]
    kwdata, entries = b'', []
    for word, ts in words:
        entries.append(word.encode() + b'\0' + u16(len(ts)) + u32(len(kwdata)))
        kwdata += b''.join(u32(offsets[t]) for t in ts)
    files['|KWBTREE'] = btree(entries, 'z2l')
    files['|KWDATA'] = kwdata
    files['|bm0'] = bitmap()

    # the file: the header, the internal files, the directory
    out = bytearray(16)
    places = {}
    for name in sorted(files):
        places[name] = len(out)
        data = files[name]
        out += u32(len(data) + 9) + u32(len(data)) + b'\x04' + data
    names = sorted(places, key=lambda n: n.encode())
    directory = btree([n.encode() + b'\0' + u32(places[n]) for n in names], 'z4', 1024, True)
    dir_at = len(out)
    out += u32(len(directory) + 9) + u32(len(directory)) + b'\x04' + directory
    out[0:16] = u32(0x00035F3F) + u32(dir_at) + u32(0xffffffff) + u32(len(out))
    open(path, 'wb').write(bytes(out))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'WINNTEST.HLP')
