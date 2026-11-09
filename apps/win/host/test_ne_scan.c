/* A bounded Win16 NUL scanner and byte CMP/TEST preflight guards. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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

int main(int argc, char **argv)
{
    assert(argc == 2);
    win_ne_guest_image g;
    win_ne_guest_io io = { alloc, dealloc, guest_write };
    assert(win_ne_guest_load(argv[1], &g, &io, NULL) == WIN_NE_OK);
    assert(g.info.segment_count == 2 && g.thunk_count == 1);
    assert(g.thunk_services[0] == 0x1001); /* USER.1 MessageBox */
    assert(g.sp >= 64u);
    const uint8_t *code = memory + g.segments[1].linear;
    size_t size = g.segments[1].size;
    assert(size < 128u);
    assert(!memcmp(code + 9, "\x8a\x04\x88\x05\x84\xc0\x74", 7));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x70,
                   "Scan: OK"));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x90,
                   "WINN NUL scanner"));
    unsigned calls = 0;
#define CHECK(p, len, limit) win_ne_entry_calls_data((p), (len), 0,  \
        g.thunk_selector, g.thunk_services, g.thunk_count, (limit), &calls)
    assert(CHECK(code, size, g.sp - 64u) && calls == 1);
    /* No unbounded NUL walk: all 16 possible source/dest accesses fit
     * exactly into the exclusive [0,0x80) DATA range. */
    assert(CHECK(code, size, 0x80u) && calls == 1);
    assert(!CHECK(code, size, 0x7fu));

    uint8_t altered[128];
#define REJECT(off, value) do {                             \
        memcpy(altered, code, size);                        \
        altered[(off)] = (uint8_t)(value);                   \
        assert(!CHECK(altered, size, g.sp - 64u));          \
    } while (0)
    REJECT(1, 0xb1);   /* SI + 15 crosses the verified DATA range */
    REJECT(4, 0xb1);   /* DI + 15 crosses the verified DATA range */
    REJECT(7, 65);     /* LOOP count exceeds the static budget */
    REJECT(14, 0x00);  /* TEST [BX+SI],AL must not access unknown memory */
    REJECT(16, 0);     /* JZ +0 is not an accepted forward branch */
    REJECT(16, 3);     /* JZ would branch into a LOOP immediate */
    REJECT(20, 0xff); /* backward target inside LOOP itself */
#undef REJECT

    /* All accepted comparisons are FLAGS-only. 80 /7 is the only byte
     * memory comparison: it must use an exactly known bounded DS address. */
    const uint16_t kernel[] = { 3 };
    const uint8_t flags[] = {
        0xbe,0x20,0x00,             /* MOV SI,20h */
        0x3c,0x00,                  /* CMP AL,0 */
        0xa8,0xff,                  /* TEST AL,0xff */
        0x84,0xc0,                  /* TEST AL,AL */
        0x80,0x3c,0x00,             /* CMP byte ptr [SI],0 */
        0x9a,0x00,0x00,0x08,0x00,   /* KERNEL.GetVersion */
        0xcb
    };
    assert(win_ne_entry_calls_data(flags, sizeof flags, 0, 8,
                                    kernel, 1, 0x21, &calls) && calls == 1);
    assert(!win_ne_entry_calls_data(flags, sizeof flags, 0, 8,
                                    kernel, 1, 0x20, &calls));
    uint8_t changed[sizeof flags];
    memcpy(changed, flags, sizeof flags);
    changed[8] = 0x00; /* 84 00 is memory TEST, not register TEST */
    assert(!win_ne_entry_calls_data(changed, sizeof changed, 0, 8,
                                    kernel, 1, 0x21, &calls));
    memcpy(changed, flags, sizeof flags);
    changed[10] = 0x2c; /* 80 /5 (SUB [SI],imm8) is not CMP */
    assert(!win_ne_entry_calls_data(changed, sizeof changed, 0, 8,
                                    kernel, 1, 0x21, &calls));
    memcpy(changed, flags, sizeof flags);
    changed[10] = 0xfc; /* 80 /7 with register operand, not DS memory */
    assert(!win_ne_entry_calls_data(changed, sizeof changed, 0, 8,
                                    kernel, 1, 0x21, &calls));
    assert(!win_ne_entry_calls_data(flags, sizeof flags - 1, 0, 8,
                                    kernel, 1, 0x21, &calls));

    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne bounded NUL scanning / byte CMP and TEST: PASS");
    return 0;
}
