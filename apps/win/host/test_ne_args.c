/* NE imported 16-bit Pascal signatures, name imports and adjusted thunks. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../core/ne_guest.h"
#include "../core/ne_entry.h"
#include "../core/ne_boot.h"
#include "../core/x86bridge.h"

#define RAM_SIZE 0x90000u
static uint8_t mem[RAM_SIZE];
static uint32_t next_alloc = 0x30000u;
static uint32_t active[16];
static unsigned nactive;

static int alloc(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
    bytes = (bytes + 15u) & ~15u;
    if (bytes > RAM_SIZE - next_alloc || nactive >= 16) return 0;
    *linear = next_alloc;
    active[nactive++] = next_alloc;
    next_alloc += bytes;
    return 1;
}
static void release(void *opaque, uint32_t linear)
{
    (void)opaque;
    for (unsigned i = 0; i < nactive; i++)
        if (active[i] == linear) {
            active[i] = active[--nactive];
            return;
        }
    assert(!"unknown allocation");
}
static int copy_guest(void *opaque, uint32_t linear, const void *bytes, uint32_t len)
{
    (void)opaque;
    if (linear >= RAM_SIZE || len > RAM_SIZE - linear) return 0;
    memcpy(mem + linear, bytes, len);
    return 1;
}
static unsigned read16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    win_ne_guest_image img;
    win_ne_guest_io io = { alloc, release, copy_guest };
    assert(win_ne_guest_load(argv[1], &img, &io, NULL) == WIN_NE_OK);
    assert(img.info.segment_count == 2 && img.thunk_count == 3);
    assert(img.ss == 24 && img.cs == 16 && img.sp == 0x100);
    assert(img.thunk_services[0] == 0x10b4);
    assert(img.thunk_services[1] == 0x10b3);
    assert(img.thunk_services[2] == 90);
    const uint8_t *gdt = mem + img.gdt_linear + img.thunk_selector;
    assert(read16(gdt) == WIN_NE_GUEST_MAX_THUNKS * WIN_NE_THUNK_BYTES - 1u);
    assert(mem[img.thunk_linear + WIN_NE_BOOT_OFFSET] == 0xcc);

    for (unsigned i = 0; i < 3; ++i) {
        const uint8_t *t = mem + img.thunk_linear + i * WIN_NE_THUNK_BYTES;
        assert(t[0] == 0xba && read16(t+1) == WINX86_BRIDGE_PORT);
        assert(t[3] == 0xb8 && read16(t+4) == img.thunk_services[i]);
        assert(t[6] == 0xef && t[7] == 0xca);
        assert(read16(t+8) == (i == 2 ? 4 : 2));
        assert(t[10] == 0xcc && t[WIN_NE_THUNK_BYTES-1] == 0xcc);
    }
    const uint8_t *p = mem + img.segments[1].linear;
    const uint8_t expected[] = {
        0x68, 0x05, 0x00, 0x9a, 0, 0, 8, 0,
        0x68, 0, 0, 0x9a, 16, 0, 8, 0,
        0x1e, 0x68, 0x10, 0, 0x9a, 32, 0, 8, 0,
        0xcb
    };
    assert(!memcmp(p,expected,sizeof expected));
    unsigned calls = 0;
    assert(win_ne_entry_calls(p,img.segments[1].size,img.ip,
              img.thunk_selector,img.thunk_services,img.thunk_count,&calls));
    assert(calls == 3);
    assert(!strcmp((const char *)mem + img.segments[2].linear+0x10,"Win16 API args"));
    win_ne_guest_release(&img);
    assert(nactive == 0);
    puts("ne checked arguments/thunks: PASS");
    return 0;
}
