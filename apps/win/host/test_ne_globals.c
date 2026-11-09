/* Path-local Win16 DATA writes, globals and call argument provenance. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

static const uint16_t version_id[] = { 3 };
static int verify(const uint8_t *p, size_t size, unsigned limit,
                  const uint16_t *services, unsigned *calls)
{
    return win_ne_entry_calls_data(p,size,0,8,services,1,limit,calls);
}
#define API 0x9a,0,0,8,0,0xcb
#define YES(arr,limit) do { unsigned count=0; \
    assert(verify(arr,sizeof arr,limit,version_id,&count) && count==1); } while(0)
#define NO(arr,limit) do { unsigned count=0; \
    assert(!verify(arr,sizeof arr,limit,version_id,&count)); } while(0)
static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1]<<8); }

int main(int argc, char **argv)
{
    assert(argc==2);
    /* Immediate write -> global pointer reload -> checked indirect store. */
    const uint8_t pointer[]={
       0xc7,0x06,4,0,0xb0,0,0x8b,0x1e,4,0,0xc6,0x07,0x46,
       0xb8,0,0,0x8a,0x07,API
    };
    YES(pointer,0xb1);
    NO(pointer,0xb0);
    /* Partial writes must update both byte aliases of an existing word. */
    const uint8_t overlap[]={
       0xc7,0x06,8,0,0x34,0x12,0xc6,0x06,9,0,0x56,
       0x8b,0x16,8,0,0x81,0xfa,0x34,0x56,
       0x75,1,0x90,API
    };
    YES(overlap,0x20);
    /* The first read of initial DATA cannot establish a trusted pointer. */
    const uint8_t unknown[]={0x8b,0x1e,4,0,0x8a,0x07,API};
    NO(unknown,0xc0);
    /* Imported APIs may write through arbitrary caller-supplied pointers. */
    const uint8_t invalidated[]={
       0xc7,0x06,4,0,0xb0,0,
       0x9a,0,0,8,0,
       0x8b,0x1e,4,0,0x8a,0x07,API
    };
    NO(invalidated,0xc0);
    /* REP MOVS can modify DATA: any prior symbolic proof is discarded. */
    const uint8_t string_write[]={
       0xc7,0x06,4,0,0xb0,0,
       0xbe,0xb8,0,0xbf,0xb9,0,0xb9,1,0,0xfc,0xf3,0xa4,
       0x8b,0x1e,4,0,0x8a,0x07,API
    };
    NO(string_write,0xc0);
    /* Byte source may be any normal register half (AH here). */
    const uint8_t ah[]={
       0xb8,0,0x33,0x88,0x26,10,0,
       0xb8,0,0,0x8a,0x26,10,0,0x3d,0,0x33,
       0x75,1,0x90,API
    };
    YES(ah,0x20);
    /* Verify the bounded recursive forward-Jcc preflight. */
    uint8_t branches[80];
    for(unsigned i=0;i<21;i++) {
        branches[3*i]=0x75;branches[3*i+1]=1;branches[3*i+2]=0x90;
    }
    const uint8_t tail[]={API};
    memcpy(branches+63,tail,sizeof tail);
    unsigned calls=0;
    assert(!verify(branches,63+sizeof tail,0,version_id,&calls));

    uint8_t img[2048], code[512];
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(img,1,sizeof img,f)==sizeof img);
    assert(fgetc(f)==EOF && fclose(f)==0);
    assert(rd16(img)==0x5a4d && rd16(img+0x100)==0x454e);
    size_t n=rd16(img+0x142);
    assert(n>100 && n<=sizeof code);
    memcpy(code,img+0x200,n);
    assert(rd16(img+0x200+n)==2);
    for(unsigned i=0;i<2;i++) {
        const uint8_t *r=img+0x200+n+2+8*i;
        unsigned pos=rd16(r+2);
        assert(r[0]==3 && r[1]==1 && pos && pos+4<=n);
        assert(code[pos-1]==0x9a);
        code[pos]=code[pos+1]=code[pos+3]=0;code[pos+2]=8;
    }
    assert(!strcmp((const char*)img+0x420,"DS GLOBALS: OK"));
    assert(!strcmp((const char*)img+0x460,"DS GLOBALS: FAILED"));
    const uint16_t user_box[]={0x1001};
    assert(verify(code,n,0xc0,user_box,&calls) && calls==1);
    assert(code[9]==0xc7 && code[13]==0xb0); /* first global pointer */
    uint8_t altered[512];
    memcpy(altered,code,n);altered[13]=0xc0; /* out-of-DATA pointer */
    assert(!verify(altered,n,0xc0,user_box,&calls));
    memcpy(altered,code,n);
    assert(code[n-10]==0x8b); /* helper BP+4 */
    puts("WINN checked DS globals / near helper / branch budget: PASS");
    return 0;
}
