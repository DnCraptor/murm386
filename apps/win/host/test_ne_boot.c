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
    win_ne_boot_args a = { .rm_segment = 0x1234, .pm_segment = 8, .ne_cs = 0x13, .ne_ip = 0x42,
                           .ne_ss = 0x1b, .ne_sp = 0x100, .gdt_limit = 31, .gdt_linear = 0x6a30,
                           .tss = 0x60, .ring0 = 0x68, .exit_gate = 0x53, .cbret_gate = 0x5b };
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
    /* ring 0: TR loaded at the entry and at every return from Real Mode; the
     * ring 0 stack, then a far return into ring 3 (SS:SP, CS:IP pushed) */
    const uint8_t ltr[]={0xb8,0x60,0x00,0x0f,0x00,0xd8};
    unsigned ltrs=0;
    for (size_t i=0;i+sizeof(ltr)<=sizeof(boot);i++)
        ltrs+=!memcmp(boot+i,ltr,sizeof(ltr));
    assert(ltrs==2);
    const uint8_t enter3[]={0xb8,0x68,0x00,0x8e,0xd0,0xbc,WIN_NE_RING0_BYTES&255,WIN_NE_RING0_BYTES>>8,
                            0x68,0x1b,0x00,0x68,0x00,0x01,0x68,0x13,0x00,0x68,0x42,0x00};
    assert(contains(boot,sizeof(boot),enter3,sizeof(enter3)));
    /* the exit gate's target: the result to CX:BX, Real Mode, EXIT */
    const uint8_t exit0[]={0x89,0xc3,0x89,0xd1,0xfa};
    assert(!memcmp(boot+WIN_NE_BOOT_EXIT_OFFSET-WIN_NE_BOOT_OFFSET,exit0,sizeof(exit0)));
    const uint8_t exit_out[]={0xba,0xe7,0xe7,0xb8,0xff,0xff,0xef};
    assert(contains(boot,sizeof(boot),exit_out,sizeof(exit_out)));
    assert(boot[WIN_NE_BOOT_SCRATCH_OFFSET-1]==0xcc);
    assert(boot[WIN_NE_BOOT_SVC_OFFSET]==0);
    assert(win_ne_fault_make(idt,code,&a));
    for (unsigned v=0;v<WIN_NE_FAULT_VECTORS;v++) {
        unsigned pos=6*v;
        assert(r16(idt+v*8)==(v==WIN_NE_YIELD_VECTOR ? WIN_NE_YIELD_OFFSET :
                              WIN_NE_FAULT_CODE_OFFSET+pos));
        assert(r16(idt+v*8+2)==a.pm_segment);
        assert(r16(idt+v*8+6)==0);
        if (WIN_NE_BIOS_VECTOR(v)) {        /* DPL 3: push vector; jmp the BIOS stub */
            assert(idt[v*8+4]==0 && idt[v*8+5]==0xe6);
            assert(code[pos]==0x6a && code[pos+1]==v && code[pos+2]==0xe9);
            unsigned to=pos+5+r16(code+pos+3);
            const uint8_t bios[]={0x50,0xb8,WIN_NE_SERVICE_BIOS&255,WIN_NE_SERVICE_BIOS>>8,0x9a,
                                  WIN_NE_BOOT_GATEWAY_OFFSET&255,WIN_NE_BOOT_GATEWAY_OFFSET>>8,
                                  8,0,0x83,0xc4,0x04,0xcf};
            assert(to+sizeof(bios)<=WIN_NE_FAULT_CODE_BYTES && !memcmp(code+to,bios,sizeof(bios)));
            continue;
        }
        assert(idt[v*8+4]==0 && idt[v*8+5]==0x86);  /* DPL 0: no INT n from ring 3 */
        assert(code[pos]==0xbb && r16(code+pos+1)==v);
        assert(code[pos+3]==0xe9 && r16(code+pos+4)==
               WIN_NE_FAULT_VECTORS*6-(pos+6));
    }
    /* INT 20h and INT 21h: the DOS stubs, INT 21h a gateway service */
    assert(r16(idt+0x20*8)==WIN_NE_INT20_OFFSET && r16(idt+0x21*8)==WIN_NE_INT21_OFFSET);
    assert(r16(idt+0x21*8+2)==a.pm_segment && idt[0x21*8+5]==0xe6);  /* DPL 3 */
    assert(idt[0x20*8+5]==0xe6);
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
    /* the ring 3 stubs call the gates; the callback goes to ring 3 through
     * the ring 0 stack at CB_R0SP; its return reports RETURN */
    const uint8_t exit_stub[]={0x9a,0,0,0x53,0}, cbret_stub[]={0x9a,0,0,0x5b,0};
    assert(!memcmp(dos+WIN_NE_EXIT_STUB_OFFSET-WIN_NE_INT20_OFFSET,exit_stub,sizeof(exit_stub)));
    assert(!memcmp(dos+WIN_NE_CBRET_STUB_OFFSET-WIN_NE_INT20_OFFSET,cbret_stub,sizeof(cbret_stub)));
    const uint8_t cb_r0[]={0xb8,0x68,0x00,0x8e,0xd0,0x2e,0x8b,0x26,
                           WIN_NE_CB_R0SP_OFFSET&255,WIN_NE_CB_R0SP_OFFSET>>8};
    assert(contains(dos,sizeof(dos),cb_r0,sizeof(cb_r0)));
    const uint8_t cbret0[]={0x89,0xc3,0x89,0xd1,0xfa};
    assert(!memcmp(dos+WIN_NE_CBRET_OFFSET-WIN_NE_INT20_OFFSET,cbret0,sizeof(cbret0)));
    const uint8_t ret_out[]={0xba,0xe7,0xe7,0xb8,0xfd,0xff,0xef};
    assert(contains(dos,sizeof(dos),ret_out,sizeof(ret_out)));
    a.tss=0; assert(!win_ne_dos_make(dos,&a)); assert(!win_ne_boot_make(boot,sizeof(boot),&a));
    a.tss=0x60;
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
