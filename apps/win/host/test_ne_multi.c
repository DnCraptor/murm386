/* Verify multiple ordinal imports share a bounded x86 thunk pool. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../core/ne_guest.h"
#include "../core/ne_boot.h"
#include "../core/x86bridge.h"

#define RAM_SIZE 0x90000u
static uint8_t mem[RAM_SIZE];
static uint32_t next_alloc = 0x30000u;
static uint32_t active[256];
static unsigned nactive;

static int allocate(void *opaque, uint32_t bytes, uint32_t *linear)
{
    (void)opaque;
    bytes = (bytes + 15u) & ~15u;
    if (bytes > RAM_SIZE - next_alloc || nactive >= 256)
        return 0;
    *linear = next_alloc;
    active[nactive++] = next_alloc;
    next_alloc += bytes;
    return 1;
}
static void deallocate(void *opaque, uint32_t linear)
{
    (void)opaque;
    for (unsigned i = 0; i < nactive; i++) {
        if (active[i] == linear) {
            active[i] = active[--nactive];
            return;
        }
    }
    assert(!"free unknown allocation");
}
static int write_guest(void *opaque, uint32_t linear, const void *data, uint32_t bytes)
{
    (void)opaque;
    if (linear >= RAM_SIZE || bytes > RAM_SIZE - linear)
        return 0;
    memcpy(mem + linear, data, bytes);
    return 1;
}
static unsigned get_le16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }

int main(int argc, char **argv)
{
    assert(argc == 2 && "usage: test_ne_multi generated/NEAPP.EXE");
    win_ne_guest_io io = { allocate, deallocate, write_guest, NULL, NULL, NULL };
    win_ne_guest_image g;
    assert(win_ne_guest_load(argv[1], &g, &io, NULL) == WIN_NE_OK);
    assert(g.info.segment_count == 2 && g.thunk_count == 3);
    assert(g.thunk_services[0] == 36);
    assert(g.thunk_services[1] == 152);
    assert(g.thunk_services[2] == 3);
    assert(g.cs == (16 | 3) && g.ss == (24 | 3));   /* ring 3 */
    const uint8_t *code = mem + g.segments[1].linear;
    for (unsigned i = 0; i < 3; i++) {
        unsigned off = i * 5;
        assert(code[off] == 0x9a);
        assert(get_le16(code+off+1) == i * WIN_NE_THUNK_BYTES);
        assert(get_le16(code+off+3) == g.thunk3_selector);
        const uint8_t *thunk = mem + g.thunk_linear + i*WIN_NE_THUNK_BYTES;
        assert(thunk[0] == 0xc7 && thunk[1] == 0xc0 && get_le16(thunk+2) == g.thunk_services[i]);
        assert(thunk[4] == 0x9a && get_le16(thunk+5) == 0);       /* the API gate */
        assert(get_le16(thunk+7) == g.api_gate && thunk[9] == 0xcb);
    }
    assert(code[15] == 0xcb);
    assert(mem[g.thunk_linear + WIN_NE_GUEST_ACTIVE_THUNKS*WIN_NE_THUNK_BYTES] == 0xcc);
    win_ne_guest_release(&g);
    assert(nactive == 0);
    puts("ne multi-import: PASS");
    return 0;
}
