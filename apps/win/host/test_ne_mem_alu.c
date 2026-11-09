/* Exact arithmetic FLAGS / values of checked 8/16-bit memory operations. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../core/ne_mem_alu.h"

static unsigned expected_parity(unsigned n)
{
    unsigned p=1u;
    n&=255u;
    while(n){p^=n&1u;n>>=1u;}
    return p;
}

static unsigned expected(unsigned group, unsigned width, unsigned a,
                         unsigned b, unsigned *bits)
{
    unsigned mask = width == 8u ? 255u : 65535u;
    unsigned sign = width == 8u ? 128u : 32768u;
    unsigned v=0;
    a&=mask;b&=mask;
    switch(group) {
    case 0: v=(a+b)&mask; break;
    case 1: v=a|b; break;
    case 4: v=a&b; break;
    case 5: case 7: v=(a-b)&mask; break;
    case 6: v=a^b; break;
    default: assert(0);
    }
    *bits=0;
    if(!v) *bits|=WIN_NE_ZF;
    if(v&sign) *bits|=WIN_NE_SF;
    if(expected_parity(v)) *bits|=WIN_NE_PF;
    if(group==0) {
        if((uint32_t)a+b>mask) *bits|=WIN_NE_CF;
        if((~(a^b)&(a^v)&sign)) *bits|=WIN_NE_OF;
    } else if(group==5 || group==7) {
        if(a<b) *bits|=WIN_NE_CF;
        if(((a^b)&(a^v)&sign)) *bits|=WIN_NE_OF;
    }
    return v;
}
int main(void)
{
    const unsigned groups[]={0,1,4,5,6,7};
    for(unsigned i=0;i<sizeof groups/sizeof groups[0];++i) {
        unsigned group=groups[i];
        for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) {
            unsigned bits=0, value=expected(group,8,a,b,&bits);
            assert(win_ne_mem_value(group,8,a,b)==(group==7?a:value));
            win_ne_flag_proof f=win_ne_mem_flags(group,8,a,b,123);
            assert(f.next_ip==123 && f.mask==WIN_NE_ARITH_FLAGS);
            assert(f.bits==bits);
        }
    }
    const unsigned edges[]={0,1,0x7fff,0x8000,0xffff,0x000f,0xff00};
    for(unsigned i=0;i<sizeof edges/sizeof edges[0];++i)
        for(unsigned j=0;j<sizeof edges/sizeof edges[0];++j)
            for(unsigned k=0;k<sizeof groups/sizeof groups[0];++k) {
                unsigned bits=0, a=edges[i], b=edges[j], group=groups[k];
                unsigned value=expected(group,16,a,b,&bits);
                assert(win_ne_mem_value(group,16,a,b)==(group==7?a:value));
                assert(win_ne_mem_flags(group,16,a,b,42).bits==bits);
            }
    for(unsigned width=8;width<=16;width+=8) {
        unsigned limit=width==8?255u:65535u,sign=width==8?128u:32768u;
        for(unsigned i=0;i<4;i++) {
            unsigned before=i==0?0:i==1?limit:i==2?sign:sign-1u;
            for(int delta=-1;delta<=1;delta+=2) {
                unsigned after=(before+(unsigned)delta)&limit;
                win_ne_flag_proof f=win_ne_mem_incdec(before,delta,width,17);
                assert(f.next_ip==17 && !(f.mask&WIN_NE_CF));
                assert(!!(f.bits&WIN_NE_ZF)==(after==0));
                assert(!!(f.bits&WIN_NE_SF)==!!(after&sign));
                assert(!!(f.bits&WIN_NE_PF)==expected_parity(after));
                assert(!!(f.bits&WIN_NE_OF)==
                       (delta==1?before==sign-1u:before==sign));
            }
        }
    }
    puts("WINN exact memory ALU flags and bounds: PASS");
    return 0;
}
