/* Validate bounded register-based Win16 NE relocation and BP frame. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../core/ne_guest.h"
#include "../core/ne_entry.h"

#define MEM_SIZE 0x90000u
static uint8_t memory[MEM_SIZE];
static uint32_t next_alloc = 0x30000u;
static unsigned allocations;
static int alloc(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
    bytes = (bytes + 15u) & ~15u;
    if (bytes > MEM_SIZE - next_alloc) return 0;
    *linear = next_alloc;
    next_alloc += bytes;
    allocations++;
    return 1;
}
static void dealloc(void *opaque, uint32_t linear)
{
    (void)opaque;
    assert(linear && allocations);
    allocations--;
}
static int guest_write(void *opaque, uint32_t linear, const void *src, uint32_t len)
{
    (void)opaque;
    if (linear > MEM_SIZE || len > MEM_SIZE - linear) return 0;
    memcpy(memory + linear, src, len);
    return 1;
}
static unsigned le16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
int main(int argc, char **argv)
{
    assert(argc == 2);
    win_ne_guest_image g;
    win_ne_guest_io io = {alloc, dealloc, guest_write};
    assert(win_ne_guest_load(argv[1], &g, &io, NULL) == WIN_NE_OK);
    assert(g.info.segment_count == 2 && g.thunk_count == 2);
    assert(g.thunk_services[0] == 0x1001);
    assert(g.thunk_services[1] == 3);
    assert(g.cs == 0x10 && g.ss == 0x18);
    const uint8_t *code = memory + g.segments[1].linear;
    unsigned calls = 0;
    assert(win_ne_entry_calls_data(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count,
               g.sp - 64u, &calls));
    assert(calls == 1);
    /* Code starts by writing/reading DS:[00A0], comparing and branching. */
    assert(code[0] == 0xc7 && code[1] == 0x06 && le16(code + 2) == 0xa0);
    assert(code[6] == 0xa1 && le16(code + 7) == 0xa0);
    assert(code[12] == 0x75);
    unsigned jnz_target = 14u + code[13];
    assert(code[jnz_target] == 0x9a);
    assert(le16(code + jnz_target + 1) == WIN_NE_THUNK_BYTES);
    assert(le16(code + jnz_target + 3) == g.thunk_selector);
    assert(!strcmp((const char *)memory + g.segments[2].linear+0x20,
                   "Win16 conditional branch and memory: OK"));
    assert(!strcmp((const char *)memory + g.segments[2].linear+0x80,
                   "NE flow control"));
    assert(!win_ne_entry_calls(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count, &calls));
    assert(!win_ne_entry_calls_data(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count,
               0x40, &calls));
    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne forward control-flow / direct memory fixture: PASS");
    return 0;
}
