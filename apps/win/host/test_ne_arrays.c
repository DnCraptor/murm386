/* Checked Win16 memory operand ALU, arrays, BP locals and NE integration. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

static const uint16_t kernel[] = {3};
#define API_CALL 0x9a,0,0,8,0
#define API API_CALL,0xcb

static int preflight(const uint8_t *code, size_t count, size_t limit,
                     const uint16_t *ids, unsigned *calls)
{
    return win_ne_entry_calls_data(code, count, 0, 8, ids, 1, limit, calls);
}
#define GOOD(code,lim) do {unsigned calls=0; assert(preflight((code),sizeof(code),(lim),kernel,&calls) && calls==1);} while(0)
#define BAD(code,lim) do {unsigned calls=0; assert(!preflight((code),sizeof(code),(lim),kernel,&calls));} while(0)

static unsigned get16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    /* Exact immediate memory arithmetic and direct CMP feed FLAGS. */
    const uint8_t add8[] = {
        0xc6,0x06,0x20,0,0x01,0x80,0x06,0x20,0,0x01,
        0x80,0x3e,0x20,0,0x02,0x75,0x01,0x90,API
    };
    GOOD(add8,0x21);
    BAD(add8,0x20);

    /* 83 sign-extends an immediate byte in a 16-bit memory calculation. */
    const uint8_t add_sign[] = {
        0xc7,0x06,0x20,0,0x00,0x01,
        0x83,0x2e,0x20,0,0xff,  /* SUB WORD [20],-1 => 0101 */
        0x81,0x3e,0x20,0,0x01,0x01,0x75,0x01,0x90,API
    };
    GOOD(add_sign,0x22);

    /* The address is checked for full WORD width at the DS boundary. */
    const uint8_t oob_word[] = {0xc7,0x06,0x1f,0,0x34,0x12,API};
    BAD(oob_word,0x20);
    GOOD(oob_word,0x21);

    /* Initial DS data is unknown. An ALU transformation cannot forge a
     * trusted pointer for an indirect memory access. */
    const uint8_t unknown[] = {
        0x80,0x06,0x20,0,0x01,0x8b,0x1e,0x20,0,0x8a,0x07,API
    };
    BAD(unknown,0xc0);

    /* Memory CMP with unknown data must not prove backward Jcc. */
    const uint8_t unknown_loop[] = {
        0xbb,0x20,0,0x80,0x3f,0x03,0x72,0xfb,API
    };
    BAD(unknown_loop,0x40);

    /* Verified ADD/TEST/INC and BP local arithmetic. */
    const uint8_t local[] = {
        0xc8,0x02,0,0,0xc7,0x46,0xfe,0x02,0,
        0x83,0x46,0xfe,0x03,0x83,0x7e,0xfe,0x05,
        0x75,0x01,0x90,API_CALL,0xc9,0xcb
    };
    GOOD(local,0);
    const uint8_t saved_reg[] = {
        0x55,0x8b,0xec,0x56,
        0x83,0x46,0xfe,0x01,API_CALL,0x5e,0xc9,0xcb
    };
    BAD(saved_reg,0x100); /* saved SI must not alias BP-local */
    const uint8_t retaddr[] = {
        0x55,0x8b,0xec,0x80,0x46,0x02,0x01,API_CALL,0xc9,0xcb
    };
    BAD(retaddr,0x100); /* never write saved BP or return IP */

    /* Ordinary 16-bit INC memory does not absorb an FF/6 PUSH. */
    const uint8_t inc_word[] = {
        0xc7,0x06,0x10,0,0x03,0,
        0xff,0x06,0x10,0,0x81,0x3e,0x10,0,0x04,0,
        0x75,0x01,0x90,API
    };
    GOOD(inc_word,0x12);

    const uint8_t no_adc[] = {0xc6,0x06,0x10,0,1,0x80,0x16,0x10,0,1,API};
    BAD(no_adc,0x20);  /* ADC /2 requires incoming CF */
    const uint8_t no_mul[] = {0xf6,0x26,0x10,0,API};
    BAD(no_mul,0x20);  /* no unrestricted MUL memory */
    const uint8_t no_test_group1[] = {0xf6,0x0e,0x10,0,1,API};
    BAD(no_test_group1,0x20); /* invalid group-3 /1 */
    const uint8_t invalid_bp_index[] = {0xbd,0x20,0,0x80,0x46,0,1,API};
    BAD(invalid_bp_index,0x40); /* no arbitrary BP/SS memory */

    /* A single 2 KiB relocatable NE fixture executes all families. */
    uint8_t image[2048], code[512];
    FILE *f = fopen(argv[1], "rb");
    assert(f && fread(image,1,sizeof image,f) == sizeof image);
    assert(fgetc(f) == EOF && fclose(f) == 0);
    assert(get16(image) == 0x5a4d && get16(image + 0x100) == 0x454e);
    size_t n = get16(image + 0x142);
    assert(n >= 250 && n <= sizeof code);
    memcpy(code,image+0x200,n);
    assert(get16(image + 0x200+n) == 2);
    const uint16_t ui[] = {0x1001};
    for(unsigned i=0;i<2;i++) {
        const uint8_t *rel=image+0x200+n+2+8*i;
        unsigned at=get16(rel+2);
        assert(rel[0] == 3 && rel[1] == 1 && at>0 && at+4<=n);
        assert(code[at-1] == 0x9a);
        code[at]=0;
        code[at+1]=0;
        code[at+2]=8;
        code[at+3]=0;
    }
    assert(!strcmp((const char*)image+0x420,"MEMORY ARRAYS: OK"));
    assert(!strcmp((const char*)image+0x460,"MEMORY ARRAYS: FAILED"));
    unsigned calls=0;
    assert(preflight(code,n,0xc0,ui,&calls) && calls==1);
    puts("WINN checked arrays / memory ALU / conditional loops: PASS");
    return 0;
}
