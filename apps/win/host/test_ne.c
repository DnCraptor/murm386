/* Synthetic Win16 NE image: one segment, ordinal and name imports.
 * Regression-only; WINN itself never launches an NE test application. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../core/ne.h"

static void w16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, unsigned v)
{
    w16(p, v);
    w16(p + 2, v >> 16);
}

static void image(uint8_t b[768])
{
    memset(b, 0, 768);
    w16(b, 0x5a4d);              /* DOS MZ */
    w32(b + 0x3c, 0x80);        /* NE header offset */
    uint8_t *h = b + 0x80;
    w16(h, 0x454e);             /* NE */
    w16(h + 0x14, 0x12);        /* IP */
    w16(h + 0x16, 1);           /* CS: index 1 */
    w16(h + 0x18, 0x100);       /* SP */
    w16(h + 0x1a, 1);           /* SS index */
    w16(h + 0x1c, 1);           /* segment count */
    w16(h + 0x1e, 1);           /* module count */
    w16(h + 0x22, 0x40);        /* segment table */
    w16(h + 0x28, 0x48);        /* module reference table */
    w16(h + 0x2a, 0x4a);        /* imported names */
    w16(h + 0x32, 4);           /* 16 bytes per sector */
    h[0x36] = 2;                /* Windows */
    uint8_t *seg = h + 0x40;
    w16(seg, 0x20);             /* at file 0x200 */
    w16(seg + 2, 0x30);         /* 48 bytes on disk */
    w16(seg + 4, 0x100);        /* relocation records */
    w16(seg + 6, 0x30);
    w16(h + 0x48, 0);           /* module name at import table +0 */
    h[0x4a] = 6;
    memcpy(h + 0x4b, "KERNEL", 6);
    h[0x51] = 10;
    memcpy(h + 0x52, "GetVersion", 10);
    uint8_t *r = b + 0x230;
    w16(r, 2);                 /* two reloc records */
    r += 2;
    r[0] = 3;                 /* far address */
    r[1] = 1;                 /* import ordinal */
    w16(r + 2, 0x10);
    w16(r + 4, 1);            /* module index */
    w16(r + 6, 3);            /* KERNEL.3 */
    r += 8;
    r[0] = 3;
    r[1] = 2;                 /* import name */
    w16(r + 2, 0x18);
    w16(r + 4, 1);
    w16(r + 6, 7);            /* GetVersion (offset after KERNEL) */
}

static int seen;
static int check(void *opaque, const win_ne_import *imp)
{
    assert(opaque == &seen);
    assert(imp->segment_index == 1 && imp->module_index == 1);
    assert(!strcmp(imp->module, "KERNEL"));
    assert(imp->source_type == 3);
    if (seen++ == 0) {
        assert(imp->source_offset == 0x10 && imp->ordinal == 3);
        assert(imp->flags == 1 && !imp->procedure[0]);
    } else {
        assert(imp->source_offset == 0x18 && imp->ordinal == 0);
        assert(imp->flags == 2 && !strcmp(imp->procedure, "GetVersion"));
    }
    return 0;
}
static int cancel(void *opaque, const win_ne_import *imp)
{
    (void)opaque; (void)imp;
    return 1;
}
static int check_file(const char *path, const uint8_t *b, size_t count,
                      win_ne_info *out)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(b, 1, count, f) == count);
    assert(!fclose(f));
    return win_ne_scan(path, out, check, &seen);
}
int main(void)
{
    uint8_t b[768];
    char path[] = "/tmp/winn-ne-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    win_ne_info info;
    image(b);
    seen = 0;
    assert(check_file(path, b, sizeof(b), &info) == WIN_NE_OK);
    assert(seen == 2 && info.cs_index == 1 && info.ip == 0x12);
    assert(info.ss_index == 1 && info.sp == 0x100 && info.target_os == 2);
    assert(info.segment_count == 1 && info.module_count == 1);
    assert(info.import_records == 2 && info.fixup_records == 2);
    assert(win_ne_scan(path, &info, cancel, NULL) == WIN_NE_CANCELLED);
    assert(!info.import_records);  /* no partial output on failure */

    image(b);
    w16(b + 0x232 + 4, 2); /* invalid module index */
    seen = 0;
    assert(check_file(path, b, sizeof(b), &info) == WIN_NE_BAD_IMAGE);
    image(b);
    w16(b + 0x80 + 0x1c, 4097); /* unreasonable segment count */
    assert(check_file(path, b, sizeof(b), &info) == WIN_NE_BAD_IMAGE);
    image(b);
    assert(check_file(path, b, 0x231, &info) == WIN_NE_BAD_IMAGE);
    image(b);
    b[0] = 0;
    assert(check_file(path, b, sizeof(b), &info) == WIN_NE_BAD_IMAGE);
    assert(win_ne_scan("/path/that/does/not/exist/NE.EXE", &info, NULL, NULL) == WIN_NE_NO_FILE);
    unlink(path);
    puts("ne: PASS");
    return 0;
}
