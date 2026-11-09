/* NE near PASCAL word arguments, BP+4/BP+6, and exact RET imm16. */
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
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static int verified(const uint8_t *code, size_t size,
                    const win_ne_guest_image *g, unsigned *calls)
{
    return win_ne_entry_calls_data(code, size, g->ip,
             g->thunk_selector, g->thunk_services, g->thunk_count,
             g->sp - 64u, calls);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    win_ne_guest_image g;
    win_ne_guest_io io = { alloc, dealloc, guest_write };
    assert(win_ne_guest_load(argv[1], &g, &io, NULL) == WIN_NE_OK);
    assert(g.info.segment_count == 2 && g.thunk_count == 1);
    assert(g.thunk_services[0] == 0x1001); /* USER.1 MessageBox */
    assert(g.cs == 0x10 && g.ss == 0x18 && g.sp == 0x100);
    const uint8_t *code = memory + g.segments[1].linear;
    const size_t size = g.segments[1].size;
    unsigned calls = 0;
    assert(verified(code, size, &g, &calls) && calls == 1);
    assert(code[0] == 0x55 && code[1] == 0x8b && code[2] == 0xec);
    assert(code[10] == 0xe8);
    size_t helper = 13u + (int16_t)rd16(code + 11);
    assert(helper < size && helper > 13);
    assert(code[helper] == 0x55 && code[helper+1] == 0x8b && code[helper+2] == 0xec);
    assert(!memcmp(code + helper + 6, "\x8b\x46\x04\x8b\x5e\x06\x01\xd8", 8));
    assert(!memcmp(code + helper + 20, "\xc9\xc2\x04\x00", 4));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x20,
                   "Win16 near PASCAL parameters: OK"));
    assert(!strcmp((const char *)memory + g.segments[2].linear + 0x90,
                   "Win16 near arguments"));

    uint8_t altered[256];
    assert(size <= sizeof altered);
#define REJECT(off, value) do {                             \
        memcpy(altered, code, size);                        \
        altered[(off)] = (uint8_t)(value);                  \
        assert(!verified(altered, size, &g, &calls));         \
    } while (0)
    REJECT(helper + 22, 0x02); /* RET 2 fails to pop both words */
    REJECT(helper + 22, 0x06); /* RET 6 would pop caller state */
    REJECT(helper + 21, 0xc3); /* RET without cleanup forbidden */
    REJECT(helper + 8, 0x02);  /* BP+2 aliases near return IP */
    REJECT(helper + 8, 0x08);  /* BP+8 escapes parameters */
    REJECT(helper + 8, 0x03);  /* misaligned word at BP+3 */
    REJECT(helper + 11, 0x08); /* second parameter BP+8 invalid */
    REJECT(helper + 21, 0x90); /* missing RET */
#undef REJECT

    /* Forward a near argument directly to a Win16 API using PUSH [BP+4]. */
    const uint16_t services[] = { 0x10b3, 3 };
    const uint8_t forward[] = {
        0x68,0x00,0x00,                         /* PUSH metric SM_CXSCREEN */
        0xe8,0x06,0x00,                         /* CALL near helper at +12 */
        0x9a,0x10,0x00,0x08,0x00,               /* KERNEL.3 GetVersion */
        0xcb,                                  /* FAR RET */
        0x55,0x8b,0xec,                         /* helper frame */
        0xff,0x76,0x04,                         /* PUSH WORD [BP+4] */
        0x9a,0x00,0x00,0x08,0x00,               /* USER.179 GetSystemMetrics */
        0xc9,0xc2,0x02,0x00                     /* LEAVE; RET 2 */
    };
    assert(win_ne_entry_calls(forward, sizeof forward, 0, 8,
                               services, 2, &calls) && calls == 2);
    uint8_t bad_forward[sizeof forward];
    memcpy(bad_forward, forward, sizeof forward);
    bad_forward[17] = 2; /* [BP+2] would read return IP */
    assert(!win_ne_entry_calls(bad_forward, sizeof bad_forward, 0, 8,
                                services, 2, &calls));

    /* Exact numeric argument provenance permits a bounded DS pointer.
     * An uninitialised AX passed instead must NOT become a known address. */
    const uint16_t kernel[] = {3};
    const uint8_t indexed[] = {
        0x68,0x20,0x00,                         /* PUSH 0x20 */
        0xe8,0x06,0x00,                         /* CALL helper at +12 */
        0x9a,0x00,0x00,0x08,0x00,               /* GetVersion */
        0xcb,
        0x55,0x8b,0xec,                         /* PUSH BP; MOV BP,SP */
        0x8b,0x5e,0x04,                         /* MOV BX,[BP+4] */
        0x8b,0x07,                              /* MOV AX,[BX] */
        0xc9,0xc2,0x02,0x00                     /* LEAVE; RET 2 */
    };
    assert(win_ne_entry_calls_data(indexed, sizeof indexed, 0, 8,
            kernel, 1, 0x80, &calls) && calls == 1);
    uint8_t unknown[sizeof indexed];
    memcpy(unknown, indexed, sizeof indexed);
    unknown[0] = 0x50; unknown[1] = 0x90; unknown[2] = 0x90;
    assert(!win_ne_entry_calls_data(unknown, sizeof unknown, 0, 8,
            kernel, 1, 0x80, &calls));
    assert(!win_ne_entry_calls_data(indexed, sizeof indexed, 0, 8,
            kernel, 1, 0x20, &calls)); /* 0x20 word crosses data_limit */

    /* Two nested near PASCAL calls: parameter is forwarded through BX.
     * Each RET 2 cleans only its own caller's single parameter. */
    const uint8_t nested_args[] = {
        0x68,0x01,0x00,                         /* PUSH 1 */
        0xe8,0x06,0x00,                         /* CALL A at +12 */
        0x9a,0x00,0x00,0x08,0x00,               /* KERNEL.GetVersion */
        0xcb,
        0x55,0x8b,0xec,                         /* A: frame */
        0x8b,0x5e,0x04,                         /* MOV BX,[BP+4] */
        0x53,                                  /* PUSH BX */
        0xe8,0x04,0x00,                         /* CALL B at +26 */
        0xc9,0xc2,0x02,0x00,                    /* A: LEAVE; RET 2 */
        0x55,0x8b,0xec,                         /* B: frame */
        0x8b,0x46,0x04,                         /* MOV AX,[BP+4] */
        0xc9,0xc2,0x02,0x00                     /* B: LEAVE; RET 2 */
    };
    assert(win_ne_entry_calls(nested_args, sizeof nested_args, 0, 8,
                               kernel, 1, &calls) && calls == 1);
    uint8_t bad_nested[sizeof nested_args];
    memcpy(bad_nested, nested_args, sizeof bad_nested);
    bad_nested[34] = 4; /* B cannot consume A's parameter */
    assert(!win_ne_entry_calls(bad_nested, sizeof bad_nested, 0, 8,
                                kernel, 1, &calls));

    /* CDECL: RET leaves two arguments on the stack. A matching ADD SP,4
     * must be the very next instruction; test both SP opcode encodings. */
    const uint8_t cdecl[] = {
        0x55,0x8b,0xec,                         /* main BP frame */
        0x68,0x34,0x12, 0x68,0x21,0x43,        /* PUSH two words */
        0xe8,0x0a,0x00,                         /* CALL helper at 22 */
        0x83,0xc4,0x04,                         /* ADD SP,4 */
        0x9a,0x00,0x00,0x08,0x00,               /* GetVersion */
        0xc9,0xcb,                              /* main LEAVE; RETF */
        0x55,0x8b,0xec,                         /* helper frame */
        0x8b,0x46,0x04, 0x8b,0x5e,0x06,        /* BP+4, BP+6 */
        0x01,0xd8,                              /* ADD AX,BX */
        0xc9,0xc3                               /* LEAVE; plain RET */
    };
    assert(win_ne_entry_calls(cdecl, sizeof cdecl, 0, 8,
                               kernel, 1, &calls) && calls == 1);
    uint8_t bad_cdecl[sizeof cdecl];
#define REJECT_CDECL(off, value) do {                     \
        memcpy(bad_cdecl, cdecl, sizeof cdecl);            \
        bad_cdecl[(off)] = (uint8_t)(value);               \
        assert(!win_ne_entry_calls(bad_cdecl,             \
                    sizeof bad_cdecl, 0, 8, kernel, 1, &calls)); \
    } while (0)
    REJECT_CDECL(14, 0x02); /* too little caller cleanup */
    REJECT_CDECL(14, 0x06); /* too much caller cleanup */
    REJECT_CDECL(12, 0x90); /* cleanup not immediate */
    REJECT_CDECL(13, 0xec); /* SUB SP instead of ADD SP */
    REJECT_CDECL(34, 0x90); /* missing near RET */
#undef REJECT_CDECL

    const uint8_t cdecl_long[] = {
        0x68,0x01,0x00, 0xe8,0x0a,0x00,       /* frameless caller, one arg */
        0x81,0xc4,0x02,0x00,                   /* ADD SP,2 (imm16) */
        0x9a,0x00,0x00,0x08,0x00, 0xcb,        /* GetVersion; RETF */
        0x55,0x8b,0xec, 0x8b,0x46,0x04,        /* BP frame, read arg */
        0xc9,0xc3                              /* LEAVE; plain RET */
    };
    assert(win_ne_entry_calls(cdecl_long, sizeof cdecl_long, 0, 8,
                               kernel, 1, &calls) && calls == 1);

    win_ne_guest_release(&g);
    assert(allocations == 0);
    puts("ne near PASCAL arguments / RET imm16: PASS");
    return 0;
}
