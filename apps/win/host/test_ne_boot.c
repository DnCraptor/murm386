/* Verify both the normal RM/PM/RM path and the private fault-IDT path. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_boot.h"
#include "../core/x86bridge.h"

static unsigned r16(const uint8_t *p) { return p[0] | ((unsigned)p[1]<<8); }
static unsigned r32(const uint8_t *p) { return r16(p) | (r16(p+2)<<16); }
static int contains(const uint8_t *p, size_t size, const uint8_t *seq, size_t len)
{
    for (size_t i = 0; i+len <= size; i++)
        if (!memcmp(p+i,seq,len)) return 1;
    return 0;
}
static unsigned count_table_op(const uint8_t *p, size_t size, unsigned modrm, unsigned offset)
{
    unsigned count = 0;
    for (size_t i = 0; i+5 <= size; i++)
        if (p[i] == 0x0f && p[i+1] == 0x01 && p[i+2] == modrm &&
            r16(p+i+3) == offset)
            count++;
    return count;
}
int main(void)
{
    uint8_t boot[WIN_NE_BOOT_CAPACITY], idt[WIN_NE_FAULT_IDT_BYTES];
    uint8_t code[WIN_NE_FAULT_CODE_BYTES], dos[WIN_NE_DOS_STUB_BYTES];
    win_ne_boot_args a = { 0x1234,8,0x10,0x42,0x18,0x100,31,0x6a30 };
    assert(win_ne_boot_make(boot,sizeof(boot),&a) == sizeof(boot));
    assert(boot[0] == 0xfa && boot[1] == 0x8c && boot[2] == 0xc8);
    /* SGDT and SIDT are each executed once at entry; each table is restored
     * before either normal EXIT or a native API OUT. */
    assert(count_table_op(boot,sizeof(boot),0x06,WIN_NE_BOOT_SAVED_GDTR_OFFSET)==1);
    assert(count_table_op(boot,sizeof(boot),0x0e,WIN_NE_BOOT_SAVED_IDTR_OFFSET)==1);
    assert(count_table_op(boot,sizeof(boot),0x16,WIN_NE_BOOT_SAVED_GDTR_OFFSET)==2);
    assert(count_table_op(boot,sizeof(boot),0x1e,WIN_NE_BOOT_SAVED_IDTR_OFFSET)==2);
    assert(count_table_op(boot,sizeof(boot),0x16,WIN_NE_BOOT_NEW_GDTR_OFFSET)==2);
    assert(count_table_op(boot,sizeof(boot),0x1e,WIN_NE_BOOT_NEW_IDTR_OFFSET)==2);
    assert(r16(boot+WIN_NE_BOOT_NEW_GDTR_OFFSET)==a.gdt_limit);
    assert(r32(boot+WIN_NE_BOOT_NEW_GDTR_OFFSET+2)==a.gdt_linear);
    assert(r16(boot+WIN_NE_BOOT_NEW_IDTR_OFFSET)==WIN_NE_IDT_VECTORS*8-1);
    assert(r32(boot+WIN_NE_BOOT_NEW_IDTR_OFFSET+2)==
           ((unsigned)a.rm_segment<<4)-WIN_NE_BOOT_OFFSET+WIN_NE_FAULT_IDT_OFFSET);
    assert(boot[WIN_NE_BOOT_GATEWAY_OFFSET-WIN_NE_BOOT_OFFSET]==0x2e);
    assert(boot[WIN_NE_BOOT_SCRATCH_OFFSET-1]==0xcc);
    assert(boot[WIN_NE_BOOT_SVC_OFFSET]==0);
    assert(win_ne_fault_make(idt,code,&a));
    for (unsigned v=0;v<WIN_NE_FAULT_VECTORS;v++) {
        unsigned pos=6*v;
        assert(r16(idt+v*8)==(v==WIN_NE_YIELD_VECTOR ? WIN_NE_YIELD_OFFSET :
                              WIN_NE_FAULT_CODE_OFFSET+pos));
        assert(r16(idt+v*8+2)==a.pm_segment);
        assert(idt[v*8+4]==0 && idt[v*8+5]==0x86);
        assert(r16(idt+v*8+6)==0);
        assert(code[pos]==0xbb && r16(code+pos+1)==v);
        assert(code[pos+3]==0xe9 && r16(code+pos+4)==
               WIN_NE_FAULT_VECTORS*6-(pos+6));
    }
    /* INT 20h and INT 21h: the DOS stubs, INT 21h a gateway service */
    assert(r16(idt+0x20*8)==WIN_NE_INT20_OFFSET && r16(idt+0x21*8)==WIN_NE_INT21_OFFSET);
    assert(r16(idt+0x21*8+2)==a.pm_segment && idt[0x21*8+5]==0x86);
    assert(win_ne_dos_make(dos,&a));
    const uint8_t int21[]={0x50,0xb8,WIN_NE_SERVICE_INT21&255,WIN_NE_SERVICE_INT21>>8,0x9a,
                           WIN_NE_BOOT_GATEWAY_OFFSET&255,WIN_NE_BOOT_GATEWAY_OFFSET>>8,
                           8,0,0x83,0xc4,0x02,0xcf};
    assert(dos[0]==0xb8 && r16(dos+1)==0x4c00);
    assert(!memcmp(dos+WIN_NE_INT21_OFFSET-WIN_NE_INT20_OFFSET,int21,sizeof(int21)));
    uint8_t yield[sizeof(int21)];
    memcpy(yield,int21,sizeof(int21));
    yield[2]=WIN_NE_SERVICE_YIELD&255; yield[3]=WIN_NE_SERVICE_YIELD>>8;
    assert(!memcmp(dos+WIN_NE_YIELD_OFFSET-WIN_NE_INT20_OFFSET,yield,sizeof(yield)));
    const uint8_t save_frame[]={0x8c,0xd6,0x89,0xe7};    /* si=ss, di=sp */
    assert(!memcmp(code+WIN_NE_FAULT_VECTORS*6,save_frame,sizeof(save_frame)));
    const unsigned common=WIN_NE_FAULT_VECTORS*6+sizeof(save_frame);
    const uint8_t clear_pe[]={0x0f,0x20,0xc0,0x24,0xfe,0x0f,0x22,0xc0};
    assert(code[common]==0xfa &&
           !memcmp(code+common+1,clear_pe,sizeof(clear_pe)));
    assert(code[common+1+sizeof(clear_pe)]==0xea);
    assert(r16(code+common+1+sizeof(clear_pe)+3)==a.rm_segment);
    const uint8_t lidt_old[]={0x0f,0x01,0x1e,
                              WIN_NE_BOOT_SAVED_IDTR_OFFSET&255,
                              WIN_NE_BOOT_SAVED_IDTR_OFFSET>>8};
    const uint8_t lgdt_old[]={0x0f,0x01,0x16,
                              WIN_NE_BOOT_SAVED_GDTR_OFFSET&255,
                              WIN_NE_BOOT_SAVED_GDTR_OFFSET>>8};
    assert(contains(code+common,sizeof(code)-common,lidt_old,sizeof(lidt_old)));
    assert(contains(code+common,sizeof(code)-common,lgdt_old,sizeof(lgdt_old)));
    const uint8_t fault_out[]={0xba,0xe7,0xe7,0xb8,0xfe,0xff,0xef};
    assert(contains(code+common,sizeof(code)-common,fault_out,sizeof(fault_out)));
    assert(!win_ne_fault_make(NULL,code,&a));
    assert(!win_ne_fault_make(idt,NULL,&a));
    a.ne_sp=8; assert(!win_ne_boot_make(boot,sizeof(boot),&a));
    a.ne_sp=0x100; a.gdt_linear=0x1000000;
    assert(!win_ne_boot_make(boot,sizeof(boot),&a));
    puts("ne fault IDT/boot: PASS");
}
