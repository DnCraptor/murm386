/* WINN NE C-style bounded loops and nonrecursive backward near helpers. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

static const uint16_t getversion[] = {3};
static int check(const uint8_t *p,size_t len,size_t limit,unsigned *calls)
{
    return win_ne_entry_calls_data(p,len,0,8,getversion,1,limit,calls);
}
#define API 0x9a,0,0,8,0,0xcb
#define ACCEPT(arr,limit) do { unsigned calls=0; \
    assert(check(arr,sizeof(arr),limit,&calls) && calls==1); } while(0)
#define DENY(arr,limit) do { unsigned calls=0; \
    assert(!check(arr,sizeof(arr),limit,&calls)); } while(0)
static unsigned rd16(const uint8_t *p){return (unsigned)p[0] | ((unsigned)p[1]<<8);}

int main(int argc,char **argv)
{
    assert(argc==2);
    uint8_t img[2048],code[512];
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(img,1,sizeof img,f)==sizeof img);
    assert(fgetc(f)==EOF && fclose(f)==0);
    assert(rd16(img)==0x5a4d && rd16(img+0x100)==0x454e);
    assert(rd16(img+0x140)==0x20); /* code sector */
    size_t n=rd16(img+0x142);
    assert(n>100 && n<=512);
    memcpy(code,img+0x200,n);
    const uint16_t ui[]={0x1001};
    unsigned fixups=rd16(img+0x200+n);
    assert(fixups==2);
    for(unsigned i=0;i<fixups;i++) {
        uint8_t *r=img+0x200+n+2+8*i;
        unsigned at=rd16(r+2);
        assert(r[0]==3 && r[1]==1 && at>=1 && at+4<=n);
        assert(code[at-1]==0x9a);
        code[at]=code[at+1]=code[at+3]=0;
        code[at+2]=8;
    }
    assert(!strcmp((const char*)img+0x420,"BOUNDED FLOW: OK"));
    assert(!strcmp((const char*)img+0x460,"BOUNDED FLOW: FAILED"));
    unsigned calls=0;
    assert(win_ne_entry_calls_data(code,n,0,8,ui,1,0xc0,&calls) && calls==1);
    assert(code[0]==0xeb && code[2]==0xb8 && code[5]==0xc3);
    assert(code[6]==0xe8 && (int16_t)rd16(code+7)<0); /* backward helper */

    const uint8_t short_cmp[]={0xbb,0,0,0x43,0x83,0xfb,4,0x72,0xfa,API};
    ACCEPT(short_cmp,0);
    const uint8_t near_cmp[]={0xbb,0,0,0x43,0x83,0xfb,4,0x0f,0x82,0xf8,0xff,API};
    ACCEPT(near_cmp,0);
    const uint8_t dec_cx[]={0xb9,3,0,0x49,0x75,0xfd,API};
    ACCEPT(dec_cx,0);
    const uint8_t inc_zf[]={0xb9,0xff,0xff,0x41,0x75,0xfd,API};
    ACCEPT(inc_zf,0); /* CX wraps to zero and exits */
    const uint8_t jc_zero[]={0xb9,0,0,0xe3,2,0xeb,0,API};
    ACCEPT(jc_zero,0); /* deterministic forward JCXZ */

    const uint8_t no_proof[]={0xbb,0x20,0,0x8b,0x07,
                               0x3d,0,0,0x75,0xfb,API};
    DENY(no_proof,0x40); /* loaded AX is not an exact comparison source */
    const uint8_t infinite[]={0xbb,0,0,0x43,0x83,0xfb,0x7f,0x72,0xfa,API};
    DENY(infinite,0); /* >64 backwards steps cannot be accepted */
    const uint8_t jump_back[]={0xbb,0,0,0x43,0x83,0xfb,3,0xeb,0xfa,API};
    DENY(jump_back,0); /* arbitrary backward JMP always rejected */
    const uint8_t bad_modrm[]={0xbb,0,0,0x43,0x83,0x3f,4,0x72,0xfa,API};
    DENY(bad_modrm,0x40); /* CMP [BX],imm is not a proven flag input */
    const uint8_t recursion[]={0xeb,6,0xe8,0xfd,0xff,0xc3,0x90,0x90,
                                0xe8,0xf7,0xff,API};
    DENY(recursion,0); /* recursive direct CALL to active helper */
    const uint8_t jcc_middle[]={0xbb,0,0,0x43,0x83,0xfb,3,0x72,0xfc,API};
    DENY(jcc_middle,0); /* backedge into immediate of INC/CMP */
    const uint8_t unknown_zf_big[]={0xbe,0xb0,0,0xb9,17,0,0xfc,
                                     0xac,0x84,0xc0,0xe0,0xfb,API};
    DENY(unknown_zf_big,0xc0);
    const uint8_t unknown_zf_small[]={0xbe,0xb0,0,0xb9,4,0,0xfc,
                                       0xac,0x84,0xc0,0xe0,0xfb,API};
    ACCEPT(unknown_zf_small,0xc0); /* both early exit and 4 iterations safe */
    DENY(unknown_zf_small,0xb2); /* last reachable DS read at SI=0xb3 */
    const uint8_t known_zero_cmp[]={0xb9,1,0,0x83,0xf9,1,0x74,0xfb,API};
    DENY(known_zero_cmp,0); /* ZF always true, backedge never terminates */

    puts("WINN bounded C Jcc / LOOPcc / JCXZ / near helpers: PASS");
    return 0;
}
