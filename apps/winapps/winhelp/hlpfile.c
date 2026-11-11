/*
 * hlpfile.c - a Windows 3.1 help file, read (hlpfile.h).
 *
 * The file: a header (3F 5F 03 00, the directory's place), internal files
 * each after a 9-byte header (reserved and used space, flags), the
 * directory a B-tree of their names. |SYSTEM: the version (3.1: minor 21
 * or more), the flags (4: |TOPIC's blocks LZ77-compressed), the title, the
 * contents' topic, the [CONFIG] macros. |Phrases: the phrases text refers
 * to by number, LZ77-compressed. |TOPIC: 4K blocks (a 12-byte header,
 * then up to 16K once expanded) holding a chain of records: a topic's
 * header (2), its paragraphs (0x20) and table rows (0x23); a TOPICPOS is
 * the block's number * 16K + 12 + the place in it. A topic offset is the
 * block's number * 32K + the characters of the paragraphs before it in
 * the block. |CONTEXT: a context string's hash -> a topic offset;
 * |CTXOMAP the [MAP] numbers'; |TTLBTREE the titles; |KWBTREE and |KWDATA
 * the keywords; |FONT the fonts; |bm<n> the bitmaps.
 *
 * The numbers in a record are "compressed": a byte or a word (bit 0 clear)
 * holding twice the value, a word or a dword (bit 0 set) the same; the
 * signed ones biased by half their range.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "hlpfile.h"

#define BLOCK_IN   0x1000u          /* a |TOPIC block in the file */
#define BLOCK_OUT  0x4000u          /* ... expanded, at most */
#define BLOCK_HEAD 12u

/* ---------------------------------------------------------------- numbers */

long hlp_clong(const uint8_t **p)
{
    long v;
    if (**p & 1) {
        v = (long)((hlp_u32(*p) - 0x80000000ul) / 2);
        *p += 4;
    } else {
        v = ((long)hlp_u16(*p) - 0x8000) / 2;
        *p += 2;
    }
    return v;
}

unsigned long hlp_culong(const uint8_t **p)
{
    unsigned long v;
    if (**p & 1) {
        v = hlp_u32(*p) / 2;
        *p += 4;
    } else {
        v = hlp_u16(*p) / 2u;
        *p += 2;
    }
    return v;
}

int hlp_cshort(const uint8_t **p)
{
    int v;
    if (**p & 1) {
        v = ((int)hlp_u16(*p) - 0x8000) / 2;
        *p += 2;
    } else {
        v = ((int)**p - 0x80) / 2;
        *p += 1;
    }
    return v;
}

unsigned hlp_cushort(const uint8_t **p)
{
    unsigned v;
    if (**p & 1) {
        v = hlp_u16(*p) / 2u;
        *p += 2;
    } else {
        v = **p / 2u;
        *p += 1;
    }
    return v;
}

/* ---------------------------------------------------------------- the file */

static int read_at(hlp_file *h, DWORD at, void *dst, DWORD n)
{
    if (_llseek(h->f, (LONG)at, 0) != (LONG)at)
        return 0;
    return _lread(h->f, dst, (UINT)n) == (UINT)n;
}

/* LZ77 of WinHelp: a flag byte for 8 items, a set bit a word (12 bits back
 * less 1, 4 bits the length less 3), a clear one a byte; into dst (cap
 * bytes): the bytes made */
static DWORD lz77(const uint8_t *src, DWORD n, uint8_t *dst, DWORD cap)
{
    const uint8_t *end = src + n;
    DWORD out = 0;
    while (src < end) {
        unsigned mask = *src++;
        for (int i = 0; i < 8 && src < end; i++, mask >>= 1) {
            if (mask & 1) {
                if (src + 2 > end)
                    return out;
                unsigned code = hlp_u16(src);
                DWORD len = 3u + (code >> 12), back = (code & 0xfffu) + 1u;
                src += 2;
                if (back > out)
                    return out;             /* not a reference into what was made */
                for (DWORD k = 0; k < len && out < cap; k++, out++)
                    dst[out] = dst[out - back];
            } else {
                if (out < cap)
                    dst[out] = *src;
                out++;
                src++;
            }
        }
    }
    return out < cap ? out : cap;
}

/* a B-tree in memory (from its BTREEHEADER): its leaves' entries, in
 * order, to fn (which gives an entry's size, 0 to stop) */
typedef DWORD (*leaf_fn)(const uint8_t *e, const uint8_t *end, void *user);

static void btree_leaves(const uint8_t *bt, DWORD len, leaf_fn fn, void *user)
{
    if (len < 38 || hlp_u16(bt) != 0x293B)
        return;
    unsigned psize = hlp_u16(bt + 4), root = hlp_u16(bt + 26), levels = hlp_u16(bt + 32);
    const uint8_t *pages = bt + 38, *end = bt + len;
    unsigned p = root;
    if (!psize)
        return;
    while (levels-- > 1) {              /* down the first entries to the first leaf */
        const uint8_t *pg = pages + (DWORD)p * psize;
        if (pg + 6 > end)
            return;
        p = hlp_u16(pg + 4);
    }
    for (int guard = 0; p != 0xFFFF && guard < 65536; guard++) {
        const uint8_t *pg = pages + (DWORD)p * psize;
        if (pg + 8 > end)
            return;
        const uint8_t *pend = pg + psize < end ? pg + psize : end;
        unsigned n = hlp_u16(pg + 2);
        const uint8_t *e = pg + 8;
        for (unsigned i = 0; i < n && e < pend; i++) {
            DWORD k = fn(e, pend, user);
            if (!k)
                return;
            e += k;
        }
        p = hlp_u16(pg + 6);
    }
}

static DWORD str_len(const uint8_t *s, const uint8_t *end)
{
    const uint8_t *p = s;
    while (p < end && *p)
        p++;
    return (DWORD)(p - s);
}

typedef struct {
    const char *name;
    DWORD at;
} dir_find;

static DWORD dir_entry(const uint8_t *e, const uint8_t *end, void *user)
{
    dir_find *d = (dir_find *)user;
    DWORD n = str_len(e, end);
    if (e + n + 5 > end)
        return 0;
    if (!strcmp((const char *)e, d->name)) {
        d->at = hlp_u32(e + n + 1);
        return 0;
    }
    return n + 5;
}

DWORD hlp_find(hlp_file *h, const char *name, DWORD *len)
{
    dir_find d = { name, 0 };
    uint8_t head[9];
    btree_leaves(h->dir, h->dir_len, dir_entry, &d);
    if (!d.at || !read_at(h, d.at, head, 9))
        return 0;
    if (len)
        *len = hlp_u32(head + 4);
    return d.at + 9;
}

uint8_t *hlp_load(hlp_file *h, const char *name, DWORD *len)
{
    DWORD n, at = hlp_find(h, name, &n);
    uint8_t *b = at && n < 0x800000ul ? (uint8_t *)malloc(n + 1) : NULL;
    if (b && !read_at(h, at, b, n)) {
        free(b);
        b = NULL;
    }
    if (b) {
        b[n] = 0;
        if (len)
            *len = n;
    }
    return b;
}

/* ---------------------------------------------------------------- |SYSTEM */

static int read_system(hlp_file *h)
{
    DWORD n;
    uint8_t *s = hlp_load(h, "|SYSTEM", &n);
    if (!s || n < 12 || hlp_u16(s) != 0x036C || hlp_u16(s + 4) != 1) {
        free(s);
        return 0;
    }
    h->minor = hlp_u16(s + 2);
    h->flags = hlp_u16(s + 10);
    h->contents = HLP_NONE;
    if (h->minor <= 16) {               /* 3.0's: the title after the header */
        lstrcpyn(h->title, (const char *)s + 12, sizeof(h->title));
        free(s);
        return 1;
    }
    char *cfg = NULL;
    DWORD cfg_len = 0;
    for (DWORD p = 12; p + 4 <= n;) {
        unsigned type = hlp_u16(s + p), len = hlp_u16(s + p + 2);
        const char *data = (const char *)s + p + 4;
        if (p + 4 + len > n)
            break;
        if (type == 1 && len)
            lstrcpyn(h->title, data, sizeof(h->title) < len + 1 ? sizeof(h->title) : len + 1);
        else if (type == 3 && len == 4)
            h->contents = hlp_u32(s + p + 4);
        else if (type == 4 && len) {    /* a macro of [CONFIG] */
            char *c = (char *)realloc(cfg, cfg_len + len + 1);
            if (c) {
                cfg = c;
                memcpy(cfg + cfg_len, data, len);
                cfg[cfg_len + len] = 0;
                cfg_len += (DWORD)strlen(cfg + cfg_len) + 1;
            }
        }
        p += 4 + len;
    }
    h->config = cfg;
    h->config_len = cfg_len;
    free(s);
    return 1;
}

/* |Phrases (3.1): their number, 0x100, the size expanded, where each
 * starts (from the table), then the text LZ77-compressed */
static void read_phrases(hlp_file *h)
{
    DWORD n;
    uint8_t *s = hlp_load(h, "|Phrases", &n);
    if (!s || n < 8) {
        free(s);
        return;
    }
    unsigned num = hlp_u16(s);
    DWORD size = hlp_u32(s + 4), table = 8, data = table + 2u * (num + 1);
    if (!num || data > n || size > 0x100000ul) {
        free(s);
        return;
    }
    h->phr_off = (uint16_t *)malloc(sizeof(uint16_t) * (num + 1));
    h->phr_text = (uint8_t *)malloc(size + 1);
    if (!h->phr_off || !h->phr_text) {
        free(s);
        return;
    }
    for (unsigned i = 0; i <= num; i++) {
        unsigned o = hlp_u16(s + table + 2 * i);
        h->phr_off[i] = (uint16_t)(o >= 2u * (num + 1) ? o - 2u * (num + 1) : 0);
    }
    h->phr_len = lz77(s + data, n - data, h->phr_text, size);
    h->nphrases = (uint16_t)num;
    free(s);
}

void hlp_expand(hlp_file *h, const uint8_t *src, DWORD n, uint8_t *dst, DWORD size)
{
    const uint8_t *end = src + n;
    DWORD out = 0;
    while (src < end && out < size) {
        if (h->nphrases && *src && *src < 0x10 && src + 1 < end) {
            unsigned code = (unsigned)src[0] << 8 | src[1];
            unsigned i = (code - 0x100u) / 2u;
            src += 2;
            if (i < h->nphrases) {
                DWORD a = h->phr_off[i], b = h->phr_off[i + 1];
                if (b > h->phr_len)
                    b = h->phr_len;
                for (; a < b && out < size; a++)
                    dst[out++] = h->phr_text[a];
            }
            if ((code & 1) && out < size)
                dst[out++] = ' ';
        } else {
            dst[out++] = *src++;
        }
    }
    while (out < size)
        dst[out++] = 0;
}

/* ---------------------------------------------------------------- |TOPIC */

static int block(hlp_file *h, int b, uint8_t **buf, DWORD *len)
{
    int k, oldest = 0;
    for (k = 0; k < HLP_CACHE; k++)
        if (h->cache[k].buf && h->cache[k].blk == b) {
            h->cache[k].used = ++h->tick;
            *buf = h->cache[k].buf;
            *len = h->cache[k].len;
            return 1;
        }
    if (b < 0 || b >= h->nblocks)
        return 0;
    for (k = 0; k < HLP_CACHE; k++) {
        if (!h->cache[k].buf) {
            oldest = k;
            break;
        }
        if (h->cache[k].used < h->cache[oldest].used)
            oldest = k;
    }
    k = oldest;
    if (!h->cache[k].buf && !(h->cache[k].buf = (uint8_t *)malloc(BLOCK_OUT)))
        return 0;
    DWORD at = (DWORD)b * BLOCK_IN, n = h->topic_len - at < BLOCK_IN ? h->topic_len - at : BLOCK_IN;
    static uint8_t in[BLOCK_IN];
    h->cache[k].blk = -1;
    if (n <= BLOCK_HEAD || !read_at(h, h->topic_at + at, in, n))
        return 0;
    if (h->flags & 4) {
        h->cache[k].len = lz77(in + BLOCK_HEAD, n - BLOCK_HEAD, h->cache[k].buf, BLOCK_OUT);
    } else {
        memcpy(h->cache[k].buf, in + BLOCK_HEAD, n - BLOCK_HEAD);
        h->cache[k].len = n - BLOCK_HEAD;
    }
    h->cache[k].blk = b;
    h->cache[k].used = ++h->tick;
    *buf = h->cache[k].buf;
    *len = h->cache[k].len;
    return 1;
}

DWORD hlp_topic_read(hlp_file *h, DWORD pos, uint8_t *dst, DWORD n)
{
    if (pos < BLOCK_HEAD)
        return 0;
    int b = (int)((pos - BLOCK_HEAD) >> 14);
    DWORD off = (pos - BLOCK_HEAD) & 0x3fffu, got = 0;
    while (got < n) {                   /* a record goes on in the next block */
        uint8_t *buf;
        DWORD len;
        if (!block(h, b, &buf, &len))
            break;
        if (off < len) {
            DWORD k = len - off < n - got ? len - off : n - got;
            memcpy(dst + got, buf + off, k);
            got += k;
        }
        b++;
        off = 0;
    }
    return got;
}

/* every topic's header: its offset and place (the chain of records) */
static int read_pages(hlp_file *h)
{
    DWORD pos = BLOCK_HEAD, chars = 0;
    int blk_last = -1, cap = 0;
    for (long guard = 0; pos != HLP_NONE && guard < 2000000L; guard++) {
        uint8_t r[0x15 + 12];
        DWORD got = hlp_topic_read(h, pos, r, sizeof(r));
        if (got < 0x15)
            break;
        DWORD size = hlp_u32(r), next = hlp_u32(r + 12), ld1 = hlp_u32(r + 16);
        int blk = (int)((pos - BLOCK_HEAD) >> 14);
        if (blk != blk_last) {
            chars = 0;
            blk_last = blk;
        }
        if (r[0x14] == 2) {
            if (h->npages == cap) {
                int ncap = cap ? cap * 2 : 64;
                hlp_page *p = (hlp_page *)realloc(h->pages, sizeof(hlp_page) * (size_t)ncap);
                if (!p)
                    return 0;
                h->pages = p;
                cap = ncap;
            }
            h->pages[h->npages].offset = (DWORD)blk * 0x8000u + chars;
            h->pages[h->npages].pos = pos;
            h->npages++;
        } else if ((r[0x14] == 0x20 || r[0x14] == 0x23) && ld1 > 0x15 && got >= 0x15 + 7) {
            const uint8_t *p = r + 0x15;
            hlp_clong(&p);
            chars += hlp_cushort(&p);
        }
        if (next <= pos || size < 0x15)
            break;                      /* the chain goes on forward */
        pos = next;
    }
    return h->npages > 0;
}

/* ---------------------------------------------------------------- the rest */

typedef struct {
    hlp_file *h;
    DWORD max;
} ctx_fill;

static DWORD ctx_entry(const uint8_t *e, const uint8_t *end, void *user)
{
    hlp_file *h = ((ctx_fill *)user)->h;
    if (e + 8 > end || (DWORD)h->nctx >= ((ctx_fill *)user)->max)
        return 0;
    h->ctx[2 * h->nctx] = hlp_u32(e);
    h->ctx[2 * h->nctx + 1] = hlp_u32(e + 4);
    h->nctx++;
    return 8;
}

static void read_contexts(hlp_file *h)
{
    DWORD n;
    uint8_t *b = hlp_load(h, "|CONTEXT", &n);
    if (!b || n < 38) {
        free(b);
        return;
    }
    DWORD total = hlp_u32(b + 34);
    if (total && total < 0x100000ul && (h->ctx = (DWORD *)malloc(8 * total)) != NULL) {
        ctx_fill f = { h, total };     /* no more than it said */
        h->nctx = 0;
        btree_leaves(b, n, ctx_entry, &f);
    }
    free(b);
}

static void read_fonts(hlp_file *h)
{
    DWORD n;
    uint8_t *b = hlp_load(h, "|FONT", &n);
    if (!b || n < 8) {
        free(b);
        return;
    }
    unsigned faces = hlp_u16(b), descs = hlp_u16(b + 2), face_at = hlp_u16(b + 4),
             desc_at = hlp_u16(b + 6);
    unsigned flen = faces && desc_at > face_at ? (desc_at - face_at) / faces : 0;
    if (!descs || desc_at + 11u * descs > n || !(h->fonts = (hlp_font *)calloc(descs, sizeof(hlp_font)))) {
        free(b);
        return;
    }
    for (unsigned i = 0; i < descs; i++) {
        const uint8_t *d = b + desc_at + 11u * i;
        hlp_font *f = &h->fonts[i];
        unsigned face = hlp_u16(d + 3);
        f->attr = d[0];
        f->half_points = d[1];
        f->family = d[2];
        f->color = RGB(d[5], d[6], d[7]);
        if (face < faces && flen) {
            unsigned k = flen < sizeof(f->face) - 1 ? flen : sizeof(f->face) - 1;
            memcpy(f->face, b + face_at + face * flen, k);
            f->face[k] = 0;
        } else {
            lstrcpy(f->face, "Helv");
        }
    }
    h->nfonts = (int)descs;
    free(b);
}

hlp_file *hlp_open(const char *path)
{
    OFSTRUCT of;
    uint8_t head[16];
    hlp_file *h = (hlp_file *)calloc(1, sizeof(hlp_file));
    if (!h)
        return NULL;
    h->f = OpenFile(path, &of, OF_READ);
    if (h->f == HFILE_ERROR) {
        free(h);
        return NULL;
    }
    lstrcpyn(h->path, of.szPathName[0] ? of.szPathName : path, sizeof(h->path));
    for (int k = 0; k < HLP_CACHE; k++)
        h->cache[k].blk = -1;
    uint8_t fh[9];
    DWORD dir, dlen = 0;
    if (!read_at(h, 0, head, 16) || hlp_u32(head) != 0x00035F3Ful ||
        !read_at(h, (dir = hlp_u32(head + 4)), fh, 9) || (dlen = hlp_u32(fh + 4)) < 38 ||
        dlen > 0x100000ul || !(h->dir = (uint8_t *)malloc(dlen)) ||
        !read_at(h, dir + 9, h->dir, dlen)) {
        hlp_close(h);
        return NULL;
    }
    h->dir_len = dlen;
    if (!read_system(h) || (h->flags & 8)) {    /* 2K blocks: Windows 95's compiler */
        hlp_close(h);
        return NULL;
    }
    read_phrases(h);
    h->topic_at = hlp_find(h, "|TOPIC", &h->topic_len);
    h->nblocks = (int)((h->topic_len + BLOCK_IN - 1) / BLOCK_IN);
    if (!h->topic_at || !read_pages(h)) {
        hlp_close(h);
        return NULL;
    }
    read_fonts(h);
    read_contexts(h);
    if (!h->title[0])
        lstrcpy(h->title, "Help");
    return h;
}

void hlp_close(hlp_file *h)
{
    if (!h)
        return;
    if (h->f != HFILE_ERROR)
        _lclose(h->f);
    for (int k = 0; k < HLP_CACHE; k++)
        free(h->cache[k].buf);
    for (int k = 0; k < HLP_BITMAPS; k++)
        hlp_bitmap_free(&h->bitmaps[k]);
    free(h->dir);
    free(h->phr_off);
    free(h->phr_text);
    free(h->fonts);
    free(h->pages);
    free(h->ctx);
    free(h->config);
    free(h);
}

int hlp_page_of(hlp_file *h, DWORD offset)
{
    int best = -1;
    if (offset == HLP_NONE)
        return -1;
    for (int i = 0; i < h->npages; i++)
        if (h->pages[i].offset <= offset && (best < 0 || h->pages[i].offset >= h->pages[best].offset))
            best = i;
    return best;
}

/* WinHelp's hash of a context string: base 43 of its characters' values */
DWORD hlp_hash(const char *s)
{
    DWORD hash = 0;
    for (; *s; s++) {
        char c = *s;
        unsigned x = 0;
        if (c >= 'A' && c <= 'Z')
            x = (unsigned)(c - 'A') + 17;
        else if (c >= 'a' && c <= 'z')
            x = (unsigned)(c - 'a') + 17;
        else if (c >= '1' && c <= '9')
            x = (unsigned)(c - '0');
        else if (c == '0')
            x = 10;
        else if (c == '.')
            x = 12;
        else if (c == '_')
            x = 13;
        if (x)
            hash = hash * 43u + x;
    }
    return hash;
}

DWORD hlp_context(hlp_file *h, DWORD hash)
{
    for (int i = 0; i < h->nctx; i++)
        if (h->ctx[2 * i] == hash)
            return h->ctx[2 * i + 1];
    return HLP_NONE;
}

/* |CTXOMAP: their number, then a number and a topic offset each */
DWORD hlp_context_number(hlp_file *h, DWORD id)
{
    DWORD n, r = HLP_NONE;
    uint8_t *b = hlp_load(h, "|CTXOMAP", &n);
    if (b && n >= 2) {
        unsigned count = hlp_u16(b);
        for (unsigned i = 0; i < count && 2 + 8u * i + 8 <= n; i++)
            if (hlp_u32(b + 2 + 8 * i) == id) {
                r = hlp_u32(b + 2 + 8 * i + 4);
                break;
            }
    }
    free(b);
    return r;
}

typedef struct {
    DWORD offset;
    char *out;
    int cap, found;
} title_find;

static DWORD title_entry(const uint8_t *e, const uint8_t *end, void *user)
{
    title_find *t = (title_find *)user;
    if (e + 4 > end)
        return 0;
    DWORD n = str_len(e + 4, end);
    if (hlp_u32(e) == t->offset) {
        lstrcpyn(t->out, (const char *)e + 4, t->cap);
        t->found = 1;
        return 0;
    }
    return n + 5;
}

void hlp_page_title(hlp_file *h, int page, char *out, int cap)
{
    out[0] = 0;
    if (page < 0 || page >= h->npages || cap < 2)
        return;
    DWORD n;
    uint8_t *b = hlp_load(h, "|TTLBTREE", &n);
    title_find t = { h->pages[page].offset, out, cap, 0 };
    if (b)
        btree_leaves(b, n, title_entry, &t);
    free(b);
    if (t.found)
        return;
    /* from the topic's header: LinkData2's first string */
    uint8_t r[0x15];
    if (hlp_topic_read(h, h->pages[page].pos, r, sizeof(r)) < sizeof(r))
        return;
    DWORD size = hlp_u32(r), len2 = hlp_u32(r + 4), ld1 = hlp_u32(r + 16);
    if (size > 0x10000ul || ld1 > size || len2 > 0x10000ul)
        return;
    uint8_t *rec = (uint8_t *)malloc(size), *text = (uint8_t *)malloc(len2 + 1);
    if (rec && text && hlp_topic_read(h, h->pages[page].pos, rec, size) == size) {
        hlp_expand(h, rec + ld1, size - ld1, text, len2);
        text[len2] = 0;
        lstrcpyn(out, (const char *)text, cap);
    }
    free(rec);
    free(text);
}

/* ---------------------------------------------------------------- keywords */

typedef struct {
    hlp_keyword *list;
    int n, cap;
} kw_list;

static DWORD kw_entry(const uint8_t *e, const uint8_t *end, void *user)
{
    kw_list *k = (kw_list *)user;
    DWORD n = str_len(e, end);
    if (e + n + 7 > end)
        return 0;
    if (k->n == k->cap) {
        int ncap = k->cap ? k->cap * 2 : 128;
        hlp_keyword *l = (hlp_keyword *)realloc(k->list, sizeof(hlp_keyword) * (size_t)ncap);
        if (!l)
            return 0;
        k->list = l;
        k->cap = ncap;
    }
    hlp_keyword *w = &k->list[k->n++];
    lstrcpyn(w->word, (const char *)e, sizeof(w->word));
    w->count = hlp_u16(e + n + 1);
    w->at = hlp_u32(e + n + 3);
    return n + 7;
}

int hlp_keywords(hlp_file *h, hlp_keyword **out)
{
    DWORD n;
    uint8_t *b = hlp_load(h, "|KWBTREE", &n);
    kw_list k = { NULL, 0, 0 };
    if (b)
        btree_leaves(b, n, kw_entry, &k);
    free(b);
    *out = k.list;
    return k.n;
}

int hlp_keyword_topics(hlp_file *h, const hlp_keyword *k, DWORD *out, int cap)
{
    DWORD n, at = hlp_find(h, "|KWDATA", &n);
    int count = k->count < cap ? k->count : cap;
    uint8_t b[4];
    if (!at)
        return 0;
    for (int i = 0; i < count; i++) {
        DWORD o = k->at + 4u * (DWORD)i;
        if (o + 4 > n || !read_at(h, at + o, b, 4))
            return i;
        out[i] = hlp_u32(b);
    }
    return count;
}

/* ---------------------------------------------------------------- bitmaps */

void hlp_bitmap_free(hlp_bitmap *b)
{
    free(b->info);
    free(b->bits);
    memset(b, 0, sizeof(*b));
}

/* RLE: a byte n (bit 7: n & 7F bytes as they are follow; else one byte
 * repeated n times) */
static DWORD unrle(const uint8_t *src, DWORD n, uint8_t *dst, DWORD cap)
{
    const uint8_t *end = src + n;
    DWORD out = 0;
    while (src < end) {
        unsigned c = *src++;
        unsigned k = c & 0x7f;
        if (c & 0x80) {
            for (unsigned i = 0; i < k && src < end; i++, src++)
                if (out < cap)
                    dst[out++] = *src;
        } else if (src < end) {
            for (unsigned i = 0; i < k; i++)
                if (out < cap)
                    dst[out++] = *src;
            src++;
        }
    }
    return out;
}

/* packing: 0 none, 1 RLE, 2 LZ77, 3 LZ77 then RLE; into a malloc'd block
 * of size bytes */
static uint8_t *unpack(const uint8_t *src, DWORD n, DWORD size, int packing)
{
    uint8_t *dst = (uint8_t *)calloc(1, size ? size : 1), *tmp;
    if (!dst)
        return NULL;
    switch (packing) {
    case 0:
        memcpy(dst, src, n < size ? n : size);
        break;
    case 1:
        unrle(src, n, dst, size);
        break;
    case 2:
        lz77(src, n, dst, size);
        break;
    case 3:
        tmp = (uint8_t *)malloc(size * 2 + 0x1000);
        if (!tmp) {
            free(dst);
            return NULL;
        }
        unrle(tmp, lz77(src, n, tmp, size * 2 + 0x1000), dst, size);
        free(tmp);
        break;
    default:
        free(dst);
        return NULL;
    }
    return dst;
}

/* a picture's data (SHG, MRB): "lp" or "lP", their number, where each is;
 * the first of type 5 (DDB) or 6 (DIB) */
int hlp_picture(const uint8_t *data, DWORD n, hlp_bitmap *out)
{
    memset(out, 0, sizeof(*out));
    if (n < 8 || (hlp_u16(data) != 0x506C && hlp_u16(data) != 0x706C))
        return 0;
    unsigned count = hlp_u16(data + 2);
    for (unsigned i = 0; i < count && 4 + 4 * i + 4 <= n; i++) {
        DWORD at = hlp_u32(data + 4 + 4 * i);
        if (at + 2 > n)
            continue;
        const uint8_t *p = data + at, *end = data + n;
        int type = p[0], packing = p[1];
        if (type != 5 && type != 6)
            continue;
        p += 2;
        if (end - p < 40)
            return 0;
        hlp_culong(&p);                 /* pixels a meter */
        hlp_culong(&p);
        unsigned planes = hlp_cushort(&p), bits = hlp_cushort(&p);
        long w = (long)hlp_culong(&p), h = (long)hlp_culong(&p);
        unsigned long colors = hlp_culong(&p);
        hlp_culong(&p);                 /* the important colours */
        DWORD csize = hlp_culong(&p);
        hlp_culong(&p);                 /* the hot spots' size */
        DWORD pict = hlp_u32(p), pal_n;
        p += 8;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || planes != 1 ||
            (bits != 1 && bits != 4 && bits != 8 && bits != 24) || at + pict + csize > n)
            return 0;
        pal_n = bits <= 8 ? (colors && colors < (1ul << bits) ? colors : 1ul << bits) : 0;
        if (type == 5)
            pal_n = bits <= 8 ? 1ul << bits : 0;
        BITMAPINFO *bi = (BITMAPINFO *)calloc(1, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
        if (!bi)
            return 0;
        bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi->bmiHeader.biWidth = w;
        bi->bmiHeader.biHeight = h;
        bi->bmiHeader.biPlanes = 1;
        bi->bmiHeader.biBitCount = (WORD)bits;
        bi->bmiHeader.biClrUsed = pal_n;
        DWORD stride = (DWORD)((w * (long)bits + 31) / 32 * 4);
        DWORD size = stride * (DWORD)h;
        bi->bmiHeader.biSizeImage = size;
        if (type == 6) {
            for (DWORD c = 0; c < pal_n && p + 4 <= end; c++, p += 4) {
                bi->bmiColors[c].rgbBlue = p[0];
                bi->bmiColors[c].rgbGreen = p[1];
                bi->bmiColors[c].rgbRed = p[2];
            }
            out->bits = unpack(data + at + pict, csize, size, packing);
        } else {                        /* a DDB: rows of words, from the top; black and white */
            DWORD dstride = (DWORD)((w * (long)bits + 15) / 16 * 2);
            uint8_t *ddb = unpack(data + at + pict, csize, dstride * (DWORD)h, packing);
            out->bits = ddb ? (uint8_t *)calloc(1, size) : NULL;
            if (out->bits)
                for (long y = 0; y < h; y++)
                    memcpy(out->bits + (DWORD)(h - 1 - y) * stride, ddb + (DWORD)y * dstride,
                           dstride < stride ? dstride : stride);
            free(ddb);
            for (DWORD c = 0; c < pal_n; c++) {
                uint8_t v = (uint8_t)(c * 255u / (pal_n - 1u));
                bi->bmiColors[c].rgbBlue = bi->bmiColors[c].rgbGreen = bi->bmiColors[c].rgbRed = v;
            }
        }
        if (!out->bits) {
            free(bi);
            return 0;
        }
        out->info = bi;
        out->w = (int)w;
        out->h = (int)h;
        return 1;
    }
    return 0;
}

const hlp_bitmap *hlp_bitmap_of(hlp_file *h, int index)
{
    char name[16];
    if (index < 0 || index >= HLP_BITMAPS)
        return NULL;
    hlp_bitmap *b = &h->bitmaps[index];
    if (b->bits)
        return b;
    wsprintf(name, "|bm%d", index);
    DWORD n;
    uint8_t *data = hlp_load(h, name, &n);
    int ok = data && hlp_picture(data, n, b);
    free(data);
    return ok ? b : NULL;
}
