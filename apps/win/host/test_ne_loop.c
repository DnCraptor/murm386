/* Validate NELOOP staging, backward LOOP and live API relocations. */
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
    const uint8_t *code = memory + g.segments[1].linear;
    unsigned calls = 0;
    assert(win_ne_entry_calls_data(code, g.segments[1].size, g.ip,
               g.thunk_selector, g.thunk_services, g.thunk_count,
               g.sp - 64u, &calls) && calls == 1);
    assert(code[0] == 0xb9 && le16(code + 1) == 5);
    assert(code[5] == 0x40 && code[6] == 0xe2 && (int8_t)code[7] == -3);
    assert(code[8] == 0x3d && le16(code + 9) == 5); /* CMP AX,5 */
    /* Direct NE relocation points to the first thunk (USER.MessageBox). */
    unsigned off = 0;
    for (unsigned i = 0; i < 64; i++)
        if (code[i] == 0x9a && le16(code + i + 3) == g.thunk_selector) {
            off = le16(code + i + 1);
            break;
        }
    assert(off == 0);
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x20,
                   "Win16 bounded backward LOOP: OK"));
    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne bounded LOOP fixture: PASS");
    return 0;
}
