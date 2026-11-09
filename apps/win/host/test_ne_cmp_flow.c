/* Independent exact-FLAGS and conditional-branch checks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../core/ne_cmp_flow.h"

static int signed16(unsigned value)
{
    return value & 0x8000u ? (int)value - 65536 : (int)value;
}
static int signed8(unsigned value)
{
    return value & 0x80u ? (int)value - 256 : (int)value;
}
static void check_pair(unsigned a, unsigned b, unsigned width)
{
    win_ne_flag_proof f = win_ne_cmp(a,b,width,42);
    unsigned mask = width == 8 ? 255u : 65535u;
    int x = width == 8 ? signed8(a&mask) : signed16(a&mask);
    int y = width == 8 ? signed8(b&mask) : signed16(b&mask);
    assert(win_ne_jcc(&f,2,42)==((a&mask)<(b&mask))); /* JB */
    assert(win_ne_jcc(&f,3,42)==((a&mask)>=(b&mask)));/* JAE */
    assert(win_ne_jcc(&f,4,42)==((a&mask)==(b&mask)));/* JZ */
    assert(win_ne_jcc(&f,5,42)==((a&mask)!=(b&mask)));/* JNZ */
    assert(win_ne_jcc(&f,6,42)==((a&mask)<=(b&mask)));/* JBE */
    assert(win_ne_jcc(&f,7,42)==((a&mask)>(b&mask))); /* JA */
    assert(win_ne_jcc(&f,12,42)==(x<y));              /* JL */
    assert(win_ne_jcc(&f,13,42)==(x>=y));             /* JGE */
    assert(win_ne_jcc(&f,14,42)==(x<=y));             /* JLE */
    assert(win_ne_jcc(&f,15,42)==(x>y));              /* JG */
    assert(win_ne_jcc(&f,4,43)==-1); /* not adjacent */
}
int main(void)
{
    for (unsigned a=0;a<256;a++)
        for (unsigned b=0;b<256;b++) check_pair(a,b,8);
    const unsigned sample[]={0,1,0x7fff,0x8000,0xfffe,0xffff,0x100,0x9999};
    for (unsigned a=0;a<sizeof(sample)/sizeof(sample[0]);a++)
        for (unsigned b=0;b<sizeof(sample)/sizeof(sample[0]);b++)
            check_pair(sample[a],sample[b],16);
    win_ne_flag_proof v=win_ne_test(0xffff,0,16,11);
    assert(win_ne_jcc(&v,4,11)==1);
    assert(win_ne_jcc(&v,2,11)==0);
    v=win_ne_test(0x80,0x80,8,11);
    assert(win_ne_jcc(&v,8,11)==1);
    v=win_ne_incdec(1,-1,100);
    assert(win_ne_jcc(&v,4,100)==1);
    assert(win_ne_jcc(&v,2,100)==-1); /* INC/DEC preserve unproven CF */
    v=win_ne_incdec(0x7fff,1,100);
    assert(win_ne_jcc(&v,0,100)==1);
    puts("exact Win16 CMP/TEST/INC/DEC FLAGS and all 16 Jcc: PASS");
    return 0;
}
