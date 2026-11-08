/* Guest memory/selector staging test with a synthetic NE image. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../core/ne_guest.h"
#include "../core/ne_boot.h"
#include "../core/x86bridge.h"

#define MEM_BYTES 0x90000u
#define IMAGE_BYTES 0x400u
#define NE_BASE 0x80u
#define SEG1 0x200u

static uint8_t guest[MEM_BYTES];
static uint32_t next_linear;
static uint32_t active[256];
static unsigned nactive, alloc_calls, free_calls, write_calls;
static unsigned fail_write, fail_alloc, alloc_attempts;

static void w16(uint8_t *p, uint16_t x)
{
    p[0] = (uint8_t)x;
    p[1] = (uint8_t)(x >> 8);
}
static void w32(uint8_t *p, uint32_t x)
{
    w16(p, (uint16_t)x);
    w16(p + 2, (uint16_t)(x >> 16));
}
static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t r32(const uint8_t *p)
{
    return (uint32_t)r16(p) | ((uint32_t)r16(p + 2) << 16);
}
static void fixture(uint8_t b[IMAGE_BYTES])
{
    memset(b, 0, IMAGE_BYTES);
    w16(b, 0x5a4d); w32(b + 0x3c, NE_BASE);
    uint8_t *h = b + NE_BASE;
    w16(h, 0x454e);              /* NE signature */
    w16(h + 0x14, 0);           /* IP */
    w16(h + 0x16, 1);           /* CS segment 1 */
    w16(h + 0x18, 0x40);        /* SP */
    w16(h + 0x1a, 1);           /* SS segment 1 */
    w16(h + 0x1c, 1);           /* one segment */
    w16(h + 0x1e, 1);           /* KERNEL import */
    w16(h + 0x22, 0x40);        /* segment table */
    w16(h + 0x28, 0x48);        /* module table */
    w16(h + 0x2a, 0x4a);        /* import names */
    w16(h + 0x32, 4);           /* sector alignment = 16 */
    h[0x36] = 2;               /* Windows */
    w16(h + 0x40, SEG1 >> 4); /* segment disk sector */
    w16(h + 0x42, 0x40);        /* 64 bytes in file */
    w16(h + 0x44, 0x100);       /* relocations */
    w16(h + 0x46, 0x80);        /* memory size 128 bytes */
    w16(h + 0x48, 0);           /* module at names + 0 */
    h[0x4a] = 6;
    memcpy(h + 0x4b, "KERNEL", 6);
    h[0x51] = 10;
    memcpy(h + 0x52, "GetVersion", 10);
    b[SEG1] = 0x90;            /* guest NOP */
    w16(b + SEG1 + 0x10, 0xffff); /* offset target fixup chain */
    w16(b + SEG1 + 0x20, 0xffff); /* name target fixup chain */
    uint8_t *r = b + SEG1 + 0x40;
    w16(r, 2); r += 2;
    r[0] = 3; r[1] = 1;        /* FAR import by ordinal */
    w16(r + 2, 0x10); w16(r + 4, 1); w16(r + 6, 3); r += 8;
    r[0] = 3; r[1] = 2;        /* FAR import by name */
    w16(r + 2, 0x20); w16(r + 4, 1); w16(r + 6, 7);
}
static int alloc_block(void *unused, uint32_t size, uint32_t *linear)
{
    (void)unused;
    size = (size + 15u) & ~15u;
    if (++alloc_attempts == fail_alloc ||
        nactive >= 256 || next_linear > MEM_BYTES - size)
        return 0;
    *linear = next_linear;
    active[nactive++] = next_linear;
    next_linear += size;
    alloc_calls++;
    return 1;
}
static void free_block(void *unused, uint32_t linear)
{
    (void)unused;
    assert(nactive);
    for (unsigned i = 0; i < nactive; i++) {
        if (active[i] == linear) {
            active[i] = active[--nactive];
            free_calls++;
            return;
        }
    }
    assert(!"unknown guest allocation");
}
static int write_block(void *unused, uint32_t linear, const void *p, uint32_t size)
{
    (void)unused;
    assert(linear < MEM_BYTES && size <= MEM_BYTES - linear);
    if (++write_calls == fail_write)
        return 0;
    memcpy(guest + linear, p, size);
    return 1;
}
static void reset_guest(void)
{
    memset(guest, 0, sizeof(guest));
    next_linear = 0x30000;
    nactive = alloc_calls = free_calls = write_calls = fail_write = 0;
    fail_alloc = alloc_attempts = 0;
}
static void save_fixture(const char *path, uint8_t *b)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(b, 1, IMAGE_BYTES, f) == IMAGE_BYTES && !fclose(f));
}
static void check_desc(const uint8_t *p, uint32_t base, uint32_t size, int code)
{
    assert(r16(p) == size - 1u);
    assert(r16(p + 2) == (uint16_t)base);
    assert(p[4] == (uint8_t)(base >> 16));
    assert(p[5] == (code ? 0x9a : 0x92));
    assert(p[6] == 0 && p[7] == 0);
}
int main(void)
{
    uint8_t file[IMAGE_BYTES];
    char path[] = "/tmp/winn-ne-guest-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    win_ne_guest_io io = {alloc_block, free_block, write_block};
    win_ne_guest_image img;
    fixture(file);
    save_fixture(path, file);
    reset_guest();
    assert(win_ne_guest_load(path, &img, &io, NULL) == WIN_NE_OK);
    assert(img.info.segment_count == 1 && img.thunk_count == 1);
    assert(img.gdt_limit == 23 && img.cs == 0x10 && img.ss == 0x10);
    assert(img.ip == 0 && img.sp == 0x40 && img.thunk_selector == 0x08);
    assert(nactive == 3 && img.gdt_linear && img.thunk_linear);
    assert(img.segments[1].linear && img.segments[1].size == 0x80);
    const uint8_t *gdt = guest + img.gdt_linear;
    assert(!r32(gdt) && !r32(gdt + 4));
    check_desc(gdt + 8, img.thunk_linear,
               WIN_NE_THUNK_BYTES * WIN_NE_GUEST_MAX_THUNKS, 1);
    check_desc(gdt + 16, img.segments[1].linear, 0x80u, 1);
    const uint8_t *code = guest + img.segments[1].linear;
    assert(code[0] == 0x90 && code[0x7f] == 0);
    assert(r16(code + 0x10) == 0 && r16(code + 0x12) == 0x08);
    assert(r16(code + 0x20) == 0 && r16(code + 0x22) == 0x08);
    const uint8_t stub[9] = {
        0xb8, 0x03, 0x00, 0x9a,
        (uint8_t)WIN_NE_BOOT_GATEWAY_OFFSET,
        (uint8_t)(WIN_NE_BOOT_GATEWAY_OFFSET >> 8),
        0x08, 0x00, 0xcb
    };
    assert(!memcmp(guest + img.thunk_linear, stub, sizeof(stub)));
    assert(guest[img.thunk_linear + WIN_NE_THUNK_BYTES] == 0xcc);
    win_ne_guest_release(&img);
    assert(!nactive && alloc_calls == free_calls && !img.gdt);

    /* Every failure must free all guest allocations. */
    for (unsigned nth = 1; nth <= 3; nth++) {
        reset_guest();
        fail_write = nth;
        assert(win_ne_guest_load(path, &img, &io, NULL) == WIN_NE_IO_ERROR);
        assert(!nactive && alloc_calls == free_calls);
    }
    for (unsigned nth = 1; nth <= 3; nth++) {
        reset_guest();
        fail_alloc = nth;
        int rc = win_ne_guest_load(path, &img, &io, NULL);
        assert(rc == WIN_NE_NO_MEMORY || rc == WIN_NE_UNRESOLVED);
        assert(!nactive && alloc_calls == free_calls);
    }
    reset_guest();
    file[SEG1 + 0x40 + 2 + 6] = 4; /* unsupported KERNEL ordinal */
    save_fixture(path, file);
    assert(win_ne_guest_load(path, &img, &io, NULL) == WIN_NE_UNRESOLVED);
    assert(!nactive && alloc_calls == free_calls);
    unlink(path);
    puts("ne guest: PASS");
    return 0;
}
