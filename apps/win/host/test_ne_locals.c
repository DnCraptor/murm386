/* WINN BP-local pointers, callee saves, stack provenance and NE fixture. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

#define API 0x9a,0,0,8,0
#define API_RET API,0xcb
static unsigned rd16(const uint8_t *p){return (unsigned)p[0] | ((unsigned)p[1]<<8);}
static const uint16_t getversion[]={3};
static int verify(const uint8_t *p,size_t len,size_t limit,unsigned *calls)
{
    return win_ne_entry_calls_data(p,len,0,8,getversion,1,limit,calls);
}
#define YES(arr,limit) do {unsigned c=0;assert(verify(arr,sizeof(arr),limit,&c) && c==1);}while(0)
#define NO(arr,limit) do {unsigned c=0;assert(!verify(arr,sizeof(arr),limit,&c));}while(0)
int main(int argc,char **argv)
{
    assert(argc==2);
    /* A bounded BP local is a usable exact DS pointer after a reload. */
    const uint8_t local_ptr[]={
        0xc8,2,0,0,0xc7,0x46,0xfe,0x20,0,0x8b,0x5e,0xfe,
        0x8a,0x07,API,0xc9,0xcb
    };
    YES(local_ptr,0x21);
    NO(local_ptr,0x20); /* last byte was outside the proven DS region */
    /* Saved SI at BP-2. Only BP-4, explicitly allocated, can be used. */
    const uint8_t saved_first[]={
        0x55,0x8b,0xec,0x56,0x83,0xec,2,
        0xc7,0x46,0xfc,0x20,0,0x8b,0x76,0xfc,0x8a,0x04,
        API,0x83,0xc4,2,0x5e,0xc9,0xcb
    };
    YES(saved_first,0x21);
    const uint8_t saved_alias[]={
        0x55,0x8b,0xec,0x56,0xc7,0x46,0xfe,0x20,0,API,0x5e,0xc9,0xcb
    };
    NO(saved_alias,0x50); /* cannot corrupt saved SI through BP-2 */
    const uint8_t cross_alias[]={
        0x55,0x8b,0xec,0x56,0x83,0xec,2,
        0xc7,0x46,0xfd,0x20,0,API,0x83,0xc4,2,0x5e,0xc9,0xcb
    };
    NO(cross_alias,0x50); /* word at BP-3 spans saved SI and a local */
    const uint8_t arg_alias[]={
        0x55,0x8b,0xec,0x68,0x20,0,
        0xc7,0x46,0xfe,0x20,0,API,0xc9,0xcb
    };
    NO(arg_alias,0x50); /* PUSH arguments aren't local allocations */
    const uint8_t bad_addsp[]={
        0x55,0x8b,0xec,0x56,0x83,0xc4,2,API,0x5e,0xc9,0xcb
    };
    NO(bad_addsp,0x50); /* ADD SP must not discard a saved register */
    /* No value is assigned to BP-2: read must not become a proven pointer. */
    const uint8_t unknown_local[]={
        0xc8,2,0,0,0x8b,0x5e,0xfe,0x8a,0x07,API,0xc9,0xcb
    };
    NO(unknown_local,0x50);
    /* Immediate local bytes can be reconstituted as a known word. */
    const uint8_t bytes[]={
        0xc8,2,0,0,0xc7,0x46,0xfe,0x00,0,
        0xc6,0x46,0xfe,0x20,0x8b,0x5e,0xfe,0x8a,0x07,
        API,0xc9,0xcb
    };
    YES(bytes,0x21);
    /* Native Win16 FAR selector stored in a BP local survives MOV/PUSH. */
    const uint16_t lstrlen[]={90};
    const uint8_t selector[]={
        0xc8,2,0,0,0x8c,0xd8,0x89,0x46,0xfe,
        0xff,0x76,0xfe,0x68,0x20,0,
        API,0xc9,0xcb
    };
    unsigned c=0;
    assert(win_ne_entry_calls_data(selector,sizeof selector,0,8,lstrlen,1,0,&c)&&c==1);
    /* Partial overwrite of the selector local may never forge ARG_DS. */
    const uint8_t forged[]={
        0xc8,2,0,0,0x8c,0xd8,0x89,0x46,0xfe,
        0xb0,0x18,0x88,0x46,0xfe,
        0xff,0x76,0xfe,0x68,0x20,0,
        API,0xc9,0xcb
    };
    assert(!win_ne_entry_calls_data(forged,sizeof forged,0,8,lstrlen,1,0,&c));
    /* A near function's BP locals cannot erase its caller's provenance. */
    const uint8_t nested[]={
        0xc8,2,0,0,0xc7,0x46,0xfe,0x20,0,
        0xe8,0x0c,0, /* helper at offset 24 (after CALL FAR / LEAVE / RETF) */
        0x8b,0x5e,0xfe,0x8a,0x07,API,0xc9,0xcb,
        0xc8,2,0,0,0xc7,0x46,0xfe,0x99,0,0xc9,0xc3
    };
    /* The caller's one local stays intact after helper return. */
    YES(nested,0x21);
    /* Real 2K NE fixture: both USER.1 relocs must pass the verifier. */
    uint8_t image[2048],code[512];
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(image,1,sizeof image,f)==sizeof image);
    assert(fgetc(f)==EOF && fclose(f)==0);
    assert(rd16(image)==0x5a4d && rd16(image+0x100)==0x454e);
    size_t n=rd16(image+0x142);
    assert(n>100 && n<=512);
    memcpy(code,image+0x200,n);
    const uint16_t ui[]={0x1001};
    unsigned fixups=rd16(image+0x200+n);
    assert(fixups==2);
    for(unsigned i=0;i<fixups;i++){
        const uint8_t *r=image+0x200+n+2+8*i;
        unsigned at=rd16(r+2);
        assert(r[0]==3 && r[1]==1 && at>=1 && at+4<=n);
        assert(code[at-1]==0x9a);
        code[at]=code[at+1]=code[at+3]=0;code[at+2]=8;
    }
    assert(!strcmp((const char*)image+0x420,"FRAME LOCALS: OK"));
    assert(!strcmp((const char*)image+0x460,"FRAME LOCALS: FAILED"));
    assert(win_ne_entry_calls_data(code,n,0,8,ui,1,0xc0,&c) && c==1);
    puts("WINN symbolic BP locals / NE integration / safe DS ranges: PASS");
    return 0;
}
