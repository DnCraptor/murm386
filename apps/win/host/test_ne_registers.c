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
    assert(g.info.segment_count == 2 && g.thunk_count == 1);
    assert(g.thunk_services[0] == 0x1001);
    assert(g.cs == 0x10 && g.ss == 0x18);
    const uint8_t *code = memory + g.segments[1].linear;
    unsigned calls = 0;
    assert(win_ne_entry_calls(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count, &calls));
    assert(calls == 1);
    /* The FAR import operand is now a thunk selector:offset. */
    assert(code[17] == 0x9a && le16(code+18) == 0);
    assert(le16(code+20) == g.thunk_selector);
    assert(code[22] == 0x89 && code[24] == 0x5d && code[25] == 0xcb);
    assert(!strcmp((const char *)memory + g.segments[2].linear+0x20,
                   "Hello from register-based Win16 NE"));
    assert(!strcmp((const char *)memory + g.segments[2].linear+0x80,
                   "NE registers"));
    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne register/frame fixture: PASS");
    return 0;
}
