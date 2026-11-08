/* Staged NE segment loader and relocation regression tests. No guest CPU or
 * device model is required: selectors and import thunks are supplied by test
 * callbacks, exactly as they will be by the later Win16 execution manager. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../core/ne.h"

#define IMAGE_SIZE 1024
#define NE_BASE 0x80
#define SEGTAB 0x40
#define MODTAB 0x50
#define NAMETAB 0x52
#define SEG1 0x200
#define FIXUPS (SEG1 + 0x40)

static void w16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (unsigned)p[1] << 8);
}
static void w32(uint8_t *p, uint32_t v)
{
    w16(p, v);
    w16(p + 2, v >> 16);
}
static void record(uint8_t *p, unsigned type, unsigned flags,
                   unsigned source, unsigned target1, unsigned target2)
{
    p[0] = (uint8_t)type;
    p[1] = (uint8_t)flags;
    w16(p + 2, source);
    w16(p + 4, target1);
    w16(p + 6, target2);
}
static void image(uint8_t b[IMAGE_SIZE])
{
    memset(b, 0, IMAGE_SIZE);
    w16(b, 0x5a4d);
    w32(b + 0x3c, NE_BASE);
    uint8_t *h = b + NE_BASE;
    w16(h, 0x454e);
    w16(h + 0x14, 0x0000); /* CS:IP */
    w16(h + 0x16, 1);
    w16(h + 0x18, 0x0010); /* SS:SP */
    w16(h + 0x1a, 2);
    w16(h + 0x1c, 2);
    w16(h + 0x1e, 1);
    w16(h + 0x04, 0x70); /* entry table: one movable entry */
    w16(h + 0x06, 9);
    uint8_t *entry = h + 0x70;
    entry[0] = 1;       /* one entry in bundle */
    entry[1] = 0xff;    /* movable entry */
    entry[2] = 1;       /* exported flag */
    w16(entry + 3, 0x3fcd); /* INT 3f */
    entry[5] = 2;       /* segment index */
    w16(entry + 6, 0x12);
    entry[8] = 0;       /* end marker */
    w16(h + 0x22, SEGTAB);
    w16(h + 0x28, MODTAB);
    w16(h + 0x2a, NAMETAB);
    w16(h + 0x32, 4); /* 16-byte file sectors */
    h[0x36] = 2;
    uint8_t *s1 = h + SEGTAB;
    w16(s1, SEG1 >> 4);
    w16(s1 + 2, 0x40);
    w16(s1 + 4, 0x100); /* relocations */
    w16(s1 + 6, 0x50); /* 16 bytes of zero-filled tail */
    uint8_t *s2 = s1 + 8;
    w16(s2, 0x300 >> 4);
    w16(s2 + 2, 0x20);
    w16(s2 + 4, 0x01); /* data segment */
    w16(s2 + 6, 0x80); /* 96 bytes of zero-filled tail */
    w16(h + MODTAB, 0);
    h[NAMETAB] = 6;
    memcpy(h + NAMETAB + 1, "KERNEL", 6);
    h[NAMETAB + 7] = 10;
    memcpy(h + NAMETAB + 8, "GetVersion", 10);
    /* First import contains two linked call sites. */
    w16(b + SEG1 + 0x10, 0x1a);
    w16(b + SEG1 + 0x1a, 0xffff);
    w16(b + SEG1 + 0x22, 0xffff);
    w16(b + SEG1 + 0x38, 0xffff);
    w16(b + SEG1 + 0x2a, 7); /* additive import by name */
    w16(b + SEG1 + 0x30, 1); /* additive segment selector */
    b[SEG1 + 0x34] = 3; /* additive low byte */
    uint8_t *r = b + FIXUPS;
    w16(r, 6);
    r += 2;
    record(r, 3, 1, 0x10, 1, 3); /* KERNEL.3, FAR pointer chain */
    r += 8;
    record(r, 3, 0, 0x22, 2, 0x10); /* internal segment 2 FAR ptr */
    r += 8;
    record(r, 5, 6, 0x2a, 1, 7); /* by name, additive offset */
    r += 8;
    record(r, 2, 4, 0x30, 2, 0x10); /* internal additive selector */
    r += 8;
    record(r, 0, 5, 0x34, 1, 3); /* import additive lowbyte */
    r += 8;
    record(r, 3, 0, 0x38, 0xff, 1); /* internal movable ordinal 1 */
}

static int assign_ok(void *opaque, uint16_t index, uint16_t flags,
                     uint32_t size, uint16_t *selector)
{
    int *counter = (int *)opaque;
    (*counter)++;
    assert(index == 1 || index == 2);
    assert(flags == (index == 1 ? 0x100 : 1));
    assert(size == (index == 1 ? 0x50u : 0x80u));
    *selector = (uint16_t)(0x10 + 8 * index);
    return 1;
}
static int assign_fail(void *opaque, uint16_t index, uint16_t flags,
                       uint32_t size, uint16_t *selector)
{
    (void)opaque; (void)index; (void)flags; (void)size; (void)selector;
    return 0;
}
static int imports_seen;
static int resolve_ok(void *opaque, const win_ne_import *imp,
                      uint16_t *selector, uint16_t *offset)
{
    (void)opaque;
    assert(!strcmp(imp->module, "KERNEL"));
    assert((imp->ordinal == 3 && !imp->procedure[0]) ||
           (imp->ordinal == 0 && !strcmp(imp->procedure, "GetVersion")));
    imports_seen++;
    *selector = 0xA000;
    *offset = 0x0100;
    return 1;
}
static int resolve_fail(void *opaque, const win_ne_import *imp,
                        uint16_t *selector, uint16_t *offset)
{
    (void)opaque; (void)imp; (void)selector; (void)offset;
    return 0;
}
static void save(const char *path, const uint8_t *bytes)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(bytes, 1, IMAGE_SIZE, f) == IMAGE_SIZE);
    assert(!fclose(f));
}

int main(void)
{
    char path[] = "/tmp/winn-ne-image-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    uint8_t b[IMAGE_SIZE];
    image(b);
    save(path, b);

    win_ne_image img;
    int assigned = 0;
    imports_seen = 0;
    assert(win_ne_load_image(path, &img, assign_ok, resolve_ok, &assigned) == WIN_NE_OK);
    assert(assigned == 2 && imports_seen == 3);
    assert(img.info.segment_count == 2);
    assert(img.segments[1].selector == 0x18 && img.segments[1].size == 0x50);
    assert(img.segments[2].selector == 0x20 && img.segments[2].size == 0x80);
    assert(r16(img.segments[1].data + 0x10) == 0x0100);
    assert(r16(img.segments[1].data + 0x12) == 0xA000);
    assert(r16(img.segments[1].data + 0x1a) == 0x0100);
    assert(r16(img.segments[1].data + 0x1c) == 0xA000);
    assert(r16(img.segments[1].data + 0x22) == 0x0010);
    assert(r16(img.segments[1].data + 0x24) == 0x0020);
    assert(r16(img.segments[1].data + 0x2a) == 0x0107);
    assert(r16(img.segments[1].data + 0x30) == 0x0021);
    assert(img.segments[1].data[0x34] == 3);
    assert(r16(img.segments[1].data + 0x38) == 0x0012);
    assert(r16(img.segments[1].data + 0x3a) == 0x0020);
    for (unsigned i = 0x40; i < 0x50; i++)
        assert(img.segments[1].data[i] == 0);
    for (unsigned i = 0x20; i < 0x80; i++)
        assert(img.segments[2].data[i] == 0);
    win_ne_free_image(&img);
    assert(!img.segments && !img.info.segment_count);

    assigned = 0;
    assert(win_ne_load_image(path, &img, assign_ok, resolve_fail, &assigned) == WIN_NE_UNRESOLVED);
    assert(!img.segments && !img.info.segment_count);
    assert(win_ne_load_image(path, &img, assign_fail, resolve_ok, &assigned) == WIN_NE_UNRESOLVED);
    assert(!img.segments);
    assert(win_ne_load_image(path, &img, NULL, resolve_ok, &assigned) == WIN_NE_BAD_IMAGE);

    image(b);
    b[FIXUPS + 2] = 0x06; /* unsupported source type */
    save(path, b);
    assert(win_ne_load_image(path, &img, assign_ok, resolve_ok, &assigned) == WIN_NE_UNSUPPORTED);
    image(b);
    b[FIXUPS + 2 + 8 + 4] = 0xff; /* internal entry ordinal 0x10 */
    w16(b + FIXUPS + 2 + 8 + 6, 0x10);
    save(path, b);
    assert(win_ne_load_image(path, &img, assign_ok, resolve_ok, &assigned) == WIN_NE_UNRESOLVED);
    image(b);
    w16(b + SEG1 + 0x10, 0x1a);
    w16(b + SEG1 + 0x1a, 0x10); /* cycle */
    save(path, b);
    assert(win_ne_load_image(path, &img, assign_ok, resolve_ok, &assigned) == WIN_NE_BAD_IMAGE);
    image(b);
    w16(b + SEG1 + 0x1a, 0x4f); /* 4-byte FAR source would cross segment boundary */
    save(path, b);
    assert(win_ne_load_image(path, &img, assign_ok, resolve_ok, &assigned) == WIN_NE_BAD_IMAGE);

    unlink(path);
    puts("ne image: PASS");
    return 0;
}
