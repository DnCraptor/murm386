/* Restricted Win16 C-compiler ABI: ENTER, saved registers and near calls. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_entry.h"

static const uint16_t kernel[] = { 3 };
#define API 0x9a,0,0,8,0
#define ACCEPT(a) do { unsigned calls=0; assert(win_ne_entry_calls(\
 (a),sizeof(a),0,8,kernel,1,&calls) && calls==1); } while(0)
#define REJECT(a) do { unsigned calls=0; assert(!win_ne_entry_calls(\
 (a),sizeof(a),0,8,kernel,1,&calls)); } while(0)
static uint16_t rd16(const uint8_t *p) {return (uint16_t)(p[0] | p[1]<<8);}
int main(int argc,char **argv)
{
    assert(argc==2);
    const uint8_t standard[]={
      0xc8,0x02,0,0,                /* ENTER 2,0 */
      0x53,0x56,                    /* saved BX,SI */
      0xbb,0x20,0,0xbe,0x30,0,    /* alter BX/SI */
      API,                         /* GetVersion inside frame */
      0x5e,0x5b,0xc9,0xcb};        /* restore SI,BX; LEAVE; RETF */
    ACCEPT(standard);
    const uint8_t three[]={
      0x55,0x8b,0xec,0x53,0x56,0x57,0xbf,0x20,0,
      API,0x5f,0x5e,0x5b,0xc9,0xcb};
    ACCEPT(three);
    const uint8_t locals[]={
      0x55,0x8b,0xec,0x83,0xec,0x02,0x53,
      0xb8,0x34,0x12,0x89,0x46,0xfe,0x8b,0x46,0xfe,
      API,0x5b,0xc9,0xcb};
    ACCEPT(locals);
    const uint8_t nested[]={
      0xc8,0,0,0,0x53,0xe8,0x03,0x00,0x5b,0xc9,0xcb,
      0xc8,0,0,0,API,0xc9,0xc3};
    ACCEPT(nested);
    const uint8_t bad_level[]={0xc8,0,0,1,API,0xc9,0xcb};
    REJECT(bad_level);
    const uint8_t bad_odd[]={0xc8,3,0,0,API,0xc9,0xcb};
    REJECT(bad_odd);
    const uint8_t bad_large[]={0xc8,14,0,0,API,0xc9,0xcb};
    REJECT(bad_large);
    const uint8_t bad_order[]={0xc8,0,0,0,0x53,0x56,API,0x5b,0x5e,0xc9,0xcb};
    REJECT(bad_order);
    const uint8_t bad_duplicate[]={0xc8,0,0,0,0x53,0x53,API,0x5b,0x5b,0xc9,0xcb};
    REJECT(bad_duplicate);
    const uint8_t bad_missing[]={0xc8,0,0,0,0x53,API,0xc9,0xcb};
    REJECT(bad_missing);
    const uint8_t bad_early_ret[]={0xc8,0,0,0,0x53,API,0xcb};
    REJECT(bad_early_ret);
    const uint8_t bad_unbalanced[]={0xc8,0,0,0,0x53,API,0x5e,0xc9,0xcb};
    REJECT(bad_unbalanced);

    uint8_t image[2048],code[512];
    FILE *f=fopen(argv[1],"rb");
    assert(f && fread(image,1,sizeof image,f)==sizeof image);
    assert(fgetc(f)==EOF && fclose(f)==0);
    assert(rd16(image)==0x5a4d && rd16(image+0x100)==0x454e);
    size_t size=rd16(image+0x142);
    assert(size>256 && size<=512 && rd16(image+0x148)==0x60);
    memcpy(code,image+0x200,size);
    for(unsigned i=0;i<260;i++) assert(code[i]==0x90);
    assert(code[260]==0xc8 && code[261]==2 && code[263]==0);
    unsigned count=rd16(image+0x200+size),calls=0;
    const uint16_t ui[]={0x1001};
    assert(count==2);
    for(unsigned i=0;i<count;i++){
        const uint8_t *r=image+0x200+size+2+i*8;
        unsigned off=rd16(r+2);
        assert(r[0]==3 && r[1]==1 && off>0 && off+4<=size);
        assert(code[off-1]==0x9a);
        code[off]=0;code[off+1]=0;code[off+2]=8;code[off+3]=0;
    }
    assert(!strcmp((const char*)image+0x620,"COMPILER ABI: OK"));
    assert(!strcmp((const char*)image+0x660,"COMPILER ABI: FAILED"));
    assert(win_ne_entry_calls_data(code,size,0,8,ui,1,0xc0,&calls)&&calls==1);
    uint8_t changed[512];
    memcpy(changed,code,size);changed[263]=1;
    assert(!win_ne_entry_calls_data(changed,size,0,8,ui,1,0xc0,&calls));
    memcpy(changed,code,size);changed[261]=14;
    assert(!win_ne_entry_calls_data(changed,size,0,8,ui,1,0xc0,&calls));
    puts("Win16 ENTER / callee saves / 512-byte verifier: PASS");
    return 0;
}
