/* 8086 string operations: bounded DS/ES, DF, CX and selector preflight. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static const uint16_t kernel[] = { 3 };
static int verify(const uint8_t *p, size_t n, size_t limit)
{
    unsigned calls = 0;
    return win_ne_entry_calls_data(p, n, 0, 8, kernel, 1,
                                    limit, &calls) && calls == 1;
}
/* A zero-argument native KERNEL.GetVersion makes each otherwise standalone
 * instruction sequence a valid guest entry, terminated by RETF. */
#define API 0x9a,0x00,0x00,0x08,0x00,0xcb
#define ACCEPT(x,limit) assert(verify((x), sizeof(x), (limit)))
#define REJECT(x,limit) assert(!verify((x), sizeof(x), (limit)))

int main(int argc, char **argv)
{
    assert(argc == 2);
    /* Complete fixture: NE loader relocations are replaced by the same
     * synthetic selector:offset used by the host tests. The code and
     * relocation locations come from the *generated binary*, not constants. */
    uint8_t image[1024], code[256];
    FILE *f = fopen(argv[1], "rb");
    assert(f != NULL);
    assert(fread(image, 1, sizeof image, f) == sizeof image);
    assert(fgetc(f) == EOF);
    assert(!fclose(f));
    assert(r16(image) == 0x5a4d && r16(image+0x100) == 0x454e);
    const size_t n = r16(image + 0x100 + 0x42);
    assert(n && n < 240);
    memcpy(code, image+0x200, n);
    const unsigned fixups = r16(image+0x200+n);
    assert(fixups == 2);
    for (unsigned i = 0; i < fixups; ++i) {
        const uint8_t *r = image+0x200+n+2+8*i;
        unsigned at = r16(r+2);
        assert(r[0] == 3 && r[1] == 1 && at >= 1 && at+4 <= n);
        assert(code[at-1] == 0x9a);
        code[at] = 0; code[at+1] = 0;
        code[at+2] = 8; code[at+3] = 0;
    }
    unsigned calls = 0;
    const uint16_t ui[] = { 0x1001 };
    assert(win_ne_entry_calls_data(code,n,0,8,ui,1,0xc0,&calls) && calls == 1);
    assert(!strcmp((const char *)image+0x390, "STRINGS: OK"));
    assert(!strcmp((const char *)image+0x360, "String operations: FAILED"));
    assert(!strcmp((const char *)image+0x3a0, "WINN string ops"));

    /* Both string pointers are statically known; DF is explicitly clear. */
    const uint8_t copy[] = {
        0xfc, 0xbe,0x20,0x00, 0xbf,0x30,0x00,
        0xb9,0x04,0x00, 0xf3,0xa4, API
    };
    ACCEPT(copy,0x34);
    REJECT(copy,0x33); /* exclusive end: last destination at 0x33 */
    uint8_t changed[sizeof copy];
    memcpy(changed,copy,sizeof copy); changed[0] = 0x90;
    REJECT(changed,0x40); /* no CLD/STD => DF unknown */
    memcpy(changed,copy,sizeof copy); changed[2] = 0x3f;
    REJECT(changed,0x40); /* source exceeds limit */
    memcpy(changed,copy,sizeof copy); changed[5] = 0x3f;
    REJECT(changed,0x40); /* destination exceeds limit */
    memcpy(changed,copy,sizeof copy); changed[8] = 65;
    REJECT(changed,0xc0); /* exceeding REP budget */
    memcpy(changed,copy,sizeof copy); changed[10] = 0xf2;
    REJECT(changed,0xc0); /* REPNE MOVSB not admitted */
    memcpy(changed,copy,sizeof copy); changed[11] = 0x90;
    REJECT(changed,0xc0); /* REP NOP forbidden */

    /* CX=0: no accesses, not even for unknown SI/DI. */
    const uint8_t zero[] = { 0xfc, 0xb9,0,0, 0xf3,0xa5, API };
    ACCEPT(zero,0);
    /* Bare MOVSW requires two addressable bytes at each operand. */
    const uint8_t words[] = {
        0xfc,0xbe,0x3e,0,0xbf,0x20,0,0xa5,API
    };
    ACCEPT(words,0x40);
    REJECT(words,0x3f);

    /* Backward COPY is verified over the complete range, not just the
     * first byte. The restoration of CLD does not change SI/DI. */
    const uint8_t backward[] = {
        0xfd,0xbe,0x24,0,0xbf,0x34,0,
        0xb9,0x05,0,0xf3,0xa4,0xfc,API
    };
    ACCEPT(backward,0x35);
    uint8_t bad_back[sizeof backward];
    memcpy(bad_back,backward,sizeof backward); bad_back[2] = 3;
    REJECT(bad_back,0x40); /* underflow after four backward steps */

    /* REPNE SCAS and REPE CMPS may terminate early, so their pointers
     * MUST lose exact numeric provenance before later indexed DS reads. */
    const uint8_t scan[] = {
        0xfc,0xbf,0x20,0,0xb9,0x04,0,0xf2,0xae,
        0x8a,0x05, API /* MOV AL,[DI] unsafe: final DI unknown */
    };
    REJECT(scan,0x40);
    const uint8_t compare[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xb9,0x04,0,
        0xf3,0xa6,0x8a,0x04,API /* SI unknown after REPE */
    };
    REJECT(compare,0x40);
    const uint8_t compare_safe[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xb9,0x04,0,
        0xf3,0xa6,0x90,0x90,API
    };
    ACCEPT(compare_safe,0x40);
    const uint8_t compare_bad[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xb9,0x04,0,
        0xf2,0xa6,0x90,0x90,API
    };
    ACCEPT(compare_bad,0x40); /* REPNE CMPS is also bounded */

    /* LODS invalidates the entire AX value including previously proven
     * pointer provenance. A subsequent MOV BX,AX cannot address DS. */
    const uint8_t load_unknown[] = {
        0xfc,0xb8,0x20,0,0xbe,0x10,0,0xac,
        0x89,0xc3,0x8b,0x07,API
    };
    REJECT(load_unknown,0x40);
    uint8_t known[sizeof load_unknown];
    memcpy(known,load_unknown,sizeof known);known[7] = 0x90;
    ACCEPT(known,0x40);

    /* Verify every byte/word form, single and prefixed, and both directions. */
    const uint8_t single_movsb[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xa4,API
    };
    const uint8_t single_cmpsw[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xa7,API
    };
    const uint8_t single_stosw[] = {0xfc,0xbf,0x30,0,0xab,API};
    const uint8_t single_lodsw[] = {0xfc,0xbe,0x20,0,0xad,API};
    const uint8_t single_scasw[] = {0xfc,0xbf,0x30,0,0xaf,API};
    const uint8_t repeated_lodsw[] = {
        0xfc,0xbe,0x20,0,0xb9,0x03,0,0xf3,0xad,API
    };
    const uint8_t repeated_stosw[] = {
        0xfc,0xbf,0x30,0,0xb9,0x03,0,0xf3,0xab,API
    };
    const uint8_t repeated_scasw[] = {
        0xfc,0xbf,0x30,0,0xb9,0x03,0,0xf2,0xaf,API
    };
    ACCEPT(single_movsb,0x40);
    ACCEPT(single_cmpsw,0x40);
    ACCEPT(single_stosw,0x40);
    ACCEPT(single_lodsw,0x40);
    ACCEPT(single_scasw,0x40);
    ACCEPT(repeated_lodsw,0x40);
    ACCEPT(repeated_stosw,0x40);
    ACCEPT(repeated_scasw,0x40);
    REJECT(repeated_scasw,0x34); /* final word ends at 0x35 */
    const uint8_t rep_unknown_cx[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xf3,0xa4,API
    };
    REJECT(rep_unknown_cx,0x40);
    const uint8_t other_prefix[] = {
        0xfc,0xbe,0x20,0,0xbf,0x30,0,0xf2,0xa4,API
    };
    REJECT(other_prefix,0x40);

    puts("NE string instructions, bounds, DF, REP and full fixture: PASS");
    return 0;
}
