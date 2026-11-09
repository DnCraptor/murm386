/* WINN NE ALU / byte-register / shift / near conditional control guards. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

static const uint16_t getversion[] = { 3 };
static uint16_t word16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static int verify(const uint8_t *p, size_t n, size_t limit, unsigned *calls)
{
    return win_ne_entry_calls_data(p,n,0,8,getversion,1,limit,calls);
}
#define GETVERSION 0x9a,0,0,8,0,0xcb
#define OK(arr,limit) do { unsigned n = 0; assert(verify((arr),sizeof(arr),(limit),&n) && n==1); } while(0)
#define NO(arr,limit) do { unsigned n = 0; assert(!verify((arr),sizeof(arr),(limit),&n)); } while(0)
int main(int argc,char **argv)
{
    assert(argc==2);
    /* Verify all imports and control-flow paths from the real binary. */
    uint8_t img[1280], code[256];
    FILE *f=fopen(argv[1],"rb");
    assert(f);
    assert(fread(img,1,sizeof(img),f)==sizeof(img));
    assert(fgetc(f)==EOF && fclose(f)==0);
    assert(word16(img)==0x5a4d && word16(img+0x100)==0x454e);
    assert(word16(img+0x100+0x48)==0x40);
    size_t n=word16(img+0x100+0x42);
    assert(n>100 && n<=256);
    memcpy(code,img+0x200,n);
    unsigned relocs=word16(img+0x200+n);
    assert(relocs==2);
    for (unsigned i=0;i<relocs;i++) {
        const uint8_t *r=img+0x200+n+2+8*i;
        size_t at=word16(r+2);
        assert(r[0]==3 && r[1]==1 && at>0 && at+4<=n);
        assert(code[at-1]==0x9a);
        code[at]=0;code[at+1]=0;code[at+2]=8;code[at+3]=0;
    }
    assert(!strcmp((const char*)img+0x420,"ALU: OK"));
    assert(!strcmp((const char*)img+0x460,"Win16 ALU: FAILED"));
    unsigned calls=0;
    const uint16_t ui[]={0x1001};
    assert(win_ne_entry_calls_data(code,n,0,8,ui,1,0xc0,&calls)&&calls==1);
    /* Negative control: rewriting first near Jcc to backwards must reject. */
    size_t near=0;
    while(near+3<n && !(code[near]==0x0f && code[near+1]==0x85))near++;
    assert(near+3<n);
    uint8_t changed[256];
    memcpy(changed,code,n);changed[near+2]=0xf0;changed[near+3]=0xff;
    assert(!win_ne_entry_calls_data(changed,n,0,8,ui,1,0xc0,&calls));
    /* Bad NEALU import selector: fails as an untrusted thunk. */
    memcpy(changed,code,n);
    size_t at=word16(img+0x200+n+2+2);
    changed[at+2]=0x18;
    assert(!win_ne_entry_calls_data(changed,n,0,8,ui,1,0xc0,&calls));

    /* Track exact ALU results to prove or reject subsequent DS addresses. */
    const uint8_t and_addr[]={0xbe,0x20,0x10,0x81,0xe6,0xff,0,0x8a,0x04,GETVERSION};
    OK(and_addr,0x21);
    const uint8_t or_bad[]={0xbe,0x20,0,0x83,0xce,0x40,0x8a,0x04,GETVERSION};
    NO(or_bad,0x40);
    const uint8_t xor_bad[]={0xbe,0x20,0,0x81,0xf6,0,1,0x8a,0x04,GETVERSION};
    NO(xor_bad,0x50);
    const uint8_t cmp_imm[]={0xbe,0x20,0,0x83,0xfe,0x20,0x8a,0x04,GETVERSION};
    OK(cmp_imm,0x21); /* CMP does not corrupt SI's numeric value. */
    const uint8_t byte_imm[]={0xb8,0,0,0xb0,0x20,0x89,0xc3,0x8a,0x07,GETVERSION};
    OK(byte_imm,0x21);
    const uint8_t byte_hi[]={0xb8,0,0,0xb4,0x10,0x89,0xc3,0x8a,0x07,GETVERSION};
    NO(byte_hi,0x30);
    const uint8_t byte_or[]={0xb8,0x20,0,0x0c,0x02,0x89,0xc3,0x8a,0x07,GETVERSION};
    OK(byte_or,0x23);
    const uint8_t byte_unknown[]={0xb8,0x20,0,0x8a,0xc3,0x89,0xc3,0x8a,0x07,GETVERSION};
    NO(byte_unknown,0x50);
    const uint8_t reg_unknown[]={0xbe,0x20,0,0x21,0xce,0x8a,0x04,GETVERSION};
    NO(reg_unknown,0x50); /* AND SI,CX where CX was not tracked. */
    const uint8_t shift_good[]={0xbe,0x10,0,0xc1,0xe6,1,0x8a,0x04,GETVERSION};
    OK(shift_good,0x21);
    const uint8_t shift_unknown[]={0xbe,0x10,0,0xd3,0xe6,0x8a,0x04,GETVERSION};
    NO(shift_unknown,0x50); /* Shift by unknown CL taints pointer. */
    const uint8_t shift_large[]={0xbe,0x10,0,0xc1,0xe6,16,0x8a,0x04,GETVERSION};
    NO(shift_large,0x50); /* Conservative count >= operand width. */
    const uint8_t byte_xchg[]={0xb8,0x20,0,0x86,0xe0,0x89,0xc3,0x8a,0x07,GETVERSION};
    NO(byte_xchg,0x50); /* AX=0x2000 after AH/AL swap, not 0x0020. */
    const uint8_t xchg_good[]={0xb8,0x20,0,0xbb,0,0,0x93,0x8a,0x07,GETVERSION};
    OK(xchg_good,0x21); /* BX now has the original AX value. */
    const uint8_t xchg_sp[]={0xb8,0x20,0,0x94,GETVERSION};
    NO(xchg_sp,0x50);
    const uint8_t adc_bad[]={0xbe,0x20,0,0x83,0xd6,1,GETVERSION};
    NO(adc_bad,0x50); /* ADC depends on untracked carry. */
    const uint8_t div_bad[]={0xb8,2,0,0xf7,0xf3,GETVERSION};
    NO(div_bad,0x50); /* DIV not enabled. */
    const uint8_t mem_shift_bad[]={0xbe,0x20,0,0xd1,0x24,GETVERSION};
    NO(mem_shift_bad,0x50);
    const uint8_t mem_not_bad[]={0xbe,0x20,0,0xf7,0x14,GETVERSION};
    NO(mem_not_bad,0x50);
    const uint8_t near_jcc[]={0x3d,0,0,0x0f,0x85,5,0,
                              0x9a,0,0,8,0,0x9a,0,0,8,0,0xcb};
    assert(verify(near_jcc,sizeof near_jcc,0,&calls) && calls==2);
    uint8_t bad_jcc[sizeof near_jcc];
    memcpy(bad_jcc,near_jcc,sizeof near_jcc);bad_jcc[5]=1;
    NO(bad_jcc,0);
    memcpy(bad_jcc,near_jcc,sizeof near_jcc);bad_jcc[5]=0xfa;bad_jcc[6]=0xff;
    NO(bad_jcc,0);
    /* Registry of common grouping forms and truncation: no implicit memory. */
    const uint8_t testf6[]={0xb8,0x20,0,0xf6,0xc0,0xff,0x89,0xc3,0x8a,0x07,GETVERSION};
    OK(testf6,0x21);
    const uint8_t testf7[]={0xb8,0x20,0,0xf7,0xc0,0xff,0xff,0x89,0xc3,0x8a,0x07,GETVERSION};
    OK(testf7,0x21);
    const uint8_t truncated[]={0xb8,0x20,0,0xc1,0xe0};
    NO(truncated,0x50);
    puts("WINN NE ALU / near Jcc / byte register / bounds: PASS");
    return 0;
}
