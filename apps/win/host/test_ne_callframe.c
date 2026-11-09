/* NE guest: near CALL/RET, stack locals and checked BP-relative operands. */
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
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static int verified(const uint8_t *code, size_t size,
                    const win_ne_guest_image *g, unsigned *calls)
{
    return win_ne_entry_calls_data(code, size, 0, g->thunk_selector,
            g->thunk_services, g->thunk_count, g->sp - 64u, calls);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    win_ne_guest_image g;
    win_ne_guest_io io = { alloc, dealloc, guest_write };
    assert(win_ne_guest_load(argv[1], &g, &io, NULL) == WIN_NE_OK);
    assert(g.info.segment_count == 2 && g.thunk_count == 1);
    assert(g.thunk_services[0] == 0x1001); /* USER.MessageBox */
    assert(g.cs == 0x10 && g.ss == 0x18 && g.sp == 0x100);
    const uint8_t *code = memory + g.segments[1].linear;
    const size_t size = g.segments[1].size;
    unsigned calls = 0;
    assert(verified(code, size, &g, &calls) && calls == 1);
    assert(code[0] == 0x55 && code[1] == 0x8b && code[2] == 0xec);
    assert(code[3] == 0xe8); /* relative near CALL */
    unsigned helper = 6u + (int16_t)le16(code + 4);
    assert(helper > 6u && helper < size - 22u);
    assert(code[helper] == 0x55 && code[helper + 3] == 0x83);
    assert(code[helper + 4] == 0xec && code[helper + 5] == 4);
    assert(code[helper + 6] == 0xc7 && code[helper + 7] == 0x46 &&
           code[helper + 8] == 0xfe);
    assert(code[helper + 20] == 0xc9 && code[helper + 21] == 0xc3);
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x20,
                   "Win16 near CALL and BP locals: OK"));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x90,
                   "Win16 stack frame"));
    /* Both FAR CALLs target the same allocated USER.1 API thunk. */
    unsigned far_calls = 0;
    for (unsigned i = 0; i + 4 < helper; i++) {
        if (code[i] == 0x9a) {
            assert(le16(code + i + 1) == 0);
            assert(le16(code + i + 3) == g.thunk_selector);
            far_calls++;
        }
    }
    assert(far_calls == 2);

    uint8_t altered[256];
    assert(size <= sizeof altered);
#define REJECT_CHANGE(off, val) do {                               \
        memcpy(altered, code, size);                               \
        altered[(off)] = (uint8_t)(val);                            \
        assert(!verified(altered, size, &g, &calls));                \
    } while (0)
    REJECT_CHANGE(4, 0xff);           /* backwards CALL target */
    REJECT_CHANGE(helper + 5, 0x40);  /* SUB SP,64: stack budget */
    REJECT_CHANGE(helper + 8, 0x02);  /* [BP+2]: return/saved frame */
    REJECT_CHANGE(helper + 8, 0xf8);  /* [BP-8] without allocation */
    REJECT_CHANGE(helper + 20, 0x90); /* no LEAVE => unbalanced near RET */
    REJECT_CHANGE(helper + 21, 0xcb); /* FAR RET within near-call frame */
    REJECT_CHANGE(0, 0x90);           /* main POP BP is unpaired */
#undef REJECT_CHANGE
    /* Local-frame release via ADD SP,4 / POP BP rather than LEAVE. */
    const uint16_t kernel_getversion[] = { 3 };
    const uint8_t addsp[] = {
        0xe8, 0x06, 0x00,             /* CALL helper at +9 */
        0x9a, 0x00, 0x00, 0x08, 0x00, /* FAR CALL KERNEL.3 */
        0xcb,                         /* RETF */
        0x55, 0x8b, 0xec,             /* helper: PUSH BP; MOV BP,SP */
        0x83, 0xec, 0x04,             /* SUB SP,4 */
        0xc7, 0x46, 0xfe, 0x34, 0x12, /* MOV [BP-2],1234h */
        0x83, 0xc4, 0x04,             /* ADD SP,4 */
        0x5d, 0xc3                    /* POP BP; RET near */
    };
    assert(win_ne_entry_calls_data(addsp, sizeof addsp, 0, 8,
                                   kernel_getversion, 1, 0, &calls) && calls == 1);
    uint8_t bad_addsp[sizeof addsp];
    memcpy(bad_addsp, addsp, sizeof bad_addsp);
    bad_addsp[22] = 2; /* ADD SP,2 leaves local space unbalanced */
    assert(!win_ne_entry_calls_data(bad_addsp, sizeof bad_addsp, 0, 8,
                                    kernel_getversion, 1, 0, &calls));

    /* Two nested calls are bounded; a third nested call is rejected. */
    const uint8_t nested[] = {
        0xe8, 0x06, 0x00,             /* CALL helper A at +9 */
        0x9a, 0x00, 0x00, 0x08, 0x00, /* KERNEL.3 */
        0xcb,
        0xe8, 0x01, 0x00,             /* A: CALL B at +13 */
        0xc3,                         /* A: RET */
        0xc3                          /* B: RET */
    };
    assert(win_ne_entry_calls_data(nested, sizeof nested, 0, 8,
                                   kernel_getversion, 1, 0, &calls) && calls == 1);
    const uint8_t recursive_depth[] = {
        0xe8, 0x06, 0x00,             /* CALL A at +9 */
        0x9a, 0x00, 0x00, 0x08, 0x00,
        0xcb,
        0xe8, 0x01, 0x00,             /* A: CALL B at +13 */
        0xc3,
        0xe8, 0x01, 0x00,             /* B: CALL C at +17 */
        0xc3,
        0xc3                          /* C: RET */
    };
    assert(!win_ne_entry_calls_data(recursive_depth, sizeof recursive_depth,
                                    0, 8, kernel_getversion, 1, 0, &calls));

    /* Without an allowed DS region, stack locals are still bounded. */
    assert(win_ne_entry_calls(code, size, 0, g.thunk_selector,
                             g.thunk_services, g.thunk_count, &calls));
    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne near CALL/RET with bounded BP locals: PASS");
    return 0;
}
