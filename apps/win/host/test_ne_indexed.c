/* Test a real NE image with register-indexed DS writes/reads and imports. */
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
    assert(g.thunk_services[0] == 0x1001); /* USER.MessageBox */
    assert(g.thunk_services[1] == 3);      /* KERNEL.GetVersion */
    assert(g.cs == 0x10 && g.ss == 0x18 && g.sp == 0x100);
    const uint8_t *code = memory + g.segments[1].linear;
    unsigned calls = 0;
    assert(win_ne_entry_calls_data(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count,
               g.sp - 64u, &calls) && calls == 1);
    assert(code[0] == 0xbb && le16(code+1) == 0x9c);
    assert(code[3] == 0x83 && code[4] == 0xc3 && code[5] == 2);
    assert(code[9] == 0x89 && code[10] == 0x47);
    assert(code[15] == 0x8b && code[16] == 0x44);
    /* NE import records must have resolved both far-call pointers. */
    assert(code[0x39] == 0x9a);
    assert(le16(code+0x3a) == 0x0000);
    assert(le16(code+0x3c) == g.thunk_selector);
    assert(code[0x40] == 0x9a);
    assert(le16(code+0x41) == WIN_NE_THUNK_BYTES);
    assert(le16(code+0x43) == g.thunk_selector);
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x20,
                   "Indexed Win16 DATA accesses: OK"));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x80,
                   "NE indexed addressing"));
    /* The word at DATA[00A0] requires a fully addressable range [A0,A2). */
    assert(!win_ne_entry_calls_data(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count,
               0xa1, &calls));
    assert(!win_ne_entry_calls(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count, &calls));
    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne 16-bit indexed addressing fixture: PASS");
    return 0;
}
