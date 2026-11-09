/* NE byte moves: bounded DS copies, exact SI/DI/CX, AL provenance. */
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
static int checked(const uint8_t *code, size_t size,
                   const win_ne_guest_image *g, size_t limit, unsigned *calls)
{
    return win_ne_entry_calls_data(code, size, g->ip, g->thunk_selector,
                                    g->thunk_services, g->thunk_count,
                                    limit, calls);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    win_ne_guest_image g;
    win_ne_guest_io io = { alloc, dealloc, guest_write };
    assert(win_ne_guest_load(argv[1], &g, &io, NULL) == WIN_NE_OK);
    assert(g.info.segment_count == 2 && g.thunk_count == 1);
    assert(g.thunk_services[0] == 0x1001); /* USER.1 MessageBox */
    const uint8_t *code = memory + g.segments[1].linear;
    size_t size = g.segments[1].size;
    unsigned calls = 0;
    assert(size <= 256u && g.sp >= 64u);
    assert(checked(code, size, &g, g.sp - 64u, &calls) && calls == 1);
    assert(!memcmp(code + 9, "\x8a\x04\x88\x05\x46\x47\xe2\xf8", 8));
    assert(!memcmp(code + 17, "\xa0\x20\x00\xa2\x50\x00", 6));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x70,
                   "Byte loop: OK"));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x90,
                   "Win16 DS byte loop"));
    /* The final source byte is at 0x7d; limit is exclusive. */
    assert(checked(code, size, &g, 0x7eu, &calls) && calls == 1);
    assert(!checked(code, size, &g, 0x7du, &calls));

    uint8_t altered[256];
#define REJECT(off, value) do {                                   \
        memcpy(altered, code, size);                              \
        altered[(off)] = (uint8_t)(value);                        \
        assert(!checked(altered, size, &g, g.sp - 64u, &calls));     \
    } while (0)
    REJECT(1, 0xbf);  /* SI crosses DATA limit during the loop */
    REJECT(4, 0xb9);  /* DI crosses DATA limit during the loop */
    REJECT(7, 65);    /* CX above the bounded LOOP maximum of 64 */
    REJECT(10, 0x24); /* MOV AH,[SI] is not a supported byte register */
    REJECT(12, 0x25); /* MOV [DI],AH is not supported */
    REJECT(18, 0xc0); /* direct byte read outside DATA */
    REJECT(21, 0xc0); /* direct byte write outside DATA */
#undef REJECT

    /* A byte read changes AL, so the previous numeric value of AX cannot
     * continue to authorize an indexed DS pointer through MOV BX,AX. */
    const uint16_t kernel[] = {3};
    const uint8_t unknown_al[] = {
        0xb8,0x20,0x00,                /* MOV AX,20h: exact value */
        0xa0,0x10,0x00,                /* MOV AL,[10h]: AX now unknown */
        0x89,0xc3,                     /* MOV BX,AX */
        0x8b,0x07,                     /* MOV AX,[BX] must fail preflight */
        0x9a,0x00,0x00,0x08,0x00,0xcb  /* GetVersion; RETF */
    };
    assert(!win_ne_entry_calls_data(unknown_al, sizeof unknown_al, 0,
                                     8, kernel, 1, 0xc0, &calls));
    uint8_t known_al[sizeof unknown_al];
    memcpy(known_al, unknown_al, sizeof known_al);
    known_al[3] = 0x90; known_al[4] = 0x90; known_al[5] = 0x90;
    assert(win_ne_entry_calls_data(known_al, sizeof known_al, 0,
                                    8, kernel, 1, 0xc0, &calls) && calls == 1);

    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne bounded DS byte moves / LOOP: PASS");
    return 0;
}
