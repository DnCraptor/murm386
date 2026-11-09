/* Real -> 16-bit protected -> real trampoline bytecode emitter.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_boot.h"
#include "x86bridge.h"
#include <string.h>

typedef struct {
    uint8_t bytes[WIN_NE_BOOT_CAPACITY];
    size_t n;
    int overflow;
} emitter;
static void b8(emitter *e, uint8_t v)
{
    if (e->n < sizeof(e->bytes)) e->bytes[e->n] = v;
    else e->overflow = 1;
    e->n++;
}
static void b16(emitter *e, uint16_t v)
{
    b8(e, (uint8_t)v); b8(e, (uint8_t)(v >> 8));
}
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void patch16(emitter *e, size_t at, uint16_t v)
{
    e->bytes[at] = (uint8_t)v;
    e->bytes[at + 1] = (uint8_t)(v >> 8);
}

size_t win_ne_boot_make(uint8_t *buffer, size_t capacity,
                        const win_ne_boot_args *a)
{
    if (!buffer || !a || capacity < WIN_NE_BOOT_CAPACITY ||
        (a->pm_segment & 7u) || !a->pm_segment || !a->rm_segment ||
        !a->ne_cs || !a->ne_ss || a->ne_sp < 64u ||
        a->gdt_linear >= 0x1000000u || !a->gdt_limit ||
        ((uint32_t)a->rm_segment << 4) < WIN_NE_BOOT_OFFSET ||
        ((uint32_t)a->rm_segment << 4) +
            (WIN_NE_FAULT_IDT_OFFSET - WIN_NE_BOOT_OFFSET) >= 0x1000000u)
        return 0;

    emitter e = { 0 };
    /* Entry is addressed as RM_SEG:0000, with the thunk allocation aligned
     * to 16 bytes.  CLI, load saved/new GDTR, then enable PE. */
    b8(&e, 0xfa);                              /* cli */
    b8(&e, 0x8c); b8(&e, 0xc8);                /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8);                /* mov ds,ax */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x06); /* sgdt [saved_gdtr] */
    size_t save_ref1 = e.n; b16(&e, WIN_NE_BOOT_SAVED_GDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x0e); /* sidt [saved_idtr] */
    b16(&e, WIN_NE_BOOT_SAVED_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt [new_gdtr] */
    size_t new_ref = e.n; b16(&e, WIN_NE_BOOT_NEW_GDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e); /* lidt [private_idtr] */
    b16(&e, WIN_NE_BOOT_NEW_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0); /* mov eax,cr0 */
    b8(&e, 0x0c); b8(&e, 0x01);                /* or al,1 */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0); /* mov cr0,eax */
    b8(&e, 0xea);                              /* jmp far pm:selector */
    size_t pm_ref = e.n; b16(&e, 0); b16(&e, a->pm_segment);
    size_t pm_start = e.n;

    b8(&e, 0xb8); b16(&e, a->ne_ss);          /* mov ax, stack/data selector */
    b8(&e, 0x8e); b8(&e, 0xd0);               /* mov ss,ax */
    b8(&e, 0xbc); b16(&e, a->ne_sp);          /* mov sp, ne_sp */
    b8(&e, 0x8e); b8(&e, 0xd8);               /* mov ds,ax */
    b8(&e, 0x8e); b8(&e, 0xc0);               /* mov es,ax */
    b8(&e, 0x9a); b16(&e, a->ne_ip);          /* call far ne_cs:ne_ip */
    b16(&e, a->ne_cs);
    b8(&e, 0x89); b8(&e, 0xc3);               /* mov bx,ax (return LO) */
    b8(&e, 0x89); b8(&e, 0xd1);               /* mov cx,dx (return HI) */
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0); /* mov eax,cr0 */
    b8(&e, 0x24); b8(&e, 0xfe);               /* and al,~PE */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0); /* mov cr0,eax */
    b8(&e, 0xea);                              /* jmp far rm:offset */
    size_t rm_ref = e.n; b16(&e, 0); b16(&e, a->rm_segment);
    size_t rm_start = e.n;

    b8(&e, 0x8c); b8(&e, 0xc8);               /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8);               /* mov ds,ax */
    b8(&e, 0x8e); b8(&e, 0xd0);               /* mov ss,ax */
    b8(&e, 0x8e); b8(&e, 0xc0);               /* mov es,ax */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e);/* lidt [saved_idtr] */
    b16(&e, WIN_NE_BOOT_SAVED_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16);/* lgdt [saved_gdtr] */
    size_t save_ref2 = e.n; b16(&e, WIN_NE_BOOT_SAVED_GDTR_OFFSET);
    b8(&e, 0xba); b16(&e, WINX86_BRIDGE_PORT); /* mov dx,port */
    b8(&e, 0xb8); b16(&e, WINX86_BRIDGE_EXIT);/* mov ax,exit */
    b8(&e, 0xef);                              /* out dx,ax */
    b8(&e, 0xeb); b8(&e, 0xfe);                /* jmp $ (should never run) */

    size_t saved = WIN_NE_BOOT_SAVED_GDTR_OFFSET;
    size_t fresh = WIN_NE_BOOT_NEW_GDTR_OFFSET;

    /* A Win16 API thunk CALL FARs the shared gateway. The gateway saves
     * the guest register/segment state on the Win16 stack, drops CR0.PE,
     * and issues OUT from real mode. The ARM handler may now call BIOS/DOS.
     * On return the gateway re-enters PE and restores the Win16 stack. */
    if (e.n > WIN_NE_BOOT_GATEWAY_OFFSET - WIN_NE_BOOT_OFFSET)
        return 0;
    while (e.n < WIN_NE_BOOT_GATEWAY_OFFSET - WIN_NE_BOOT_OFFSET)
        b8(&e, 0xcc);
    /* CS points at the full thunk segment while PE=1, so scratch offsets
     * carry WIN_NE_BOOT_OFFSET. In RM, DS points at this boot image alone. */
#define PM_SCRATCH(o) ((uint16_t)(WIN_NE_BOOT_OFFSET + (o)))
    b8(&e, 0x2e); b8(&e, 0xa3); b16(&e, PM_SCRATCH(WIN_NE_BOOT_SVC_OFFSET));
    b8(&e, 0x2e); b8(&e, 0x89); b8(&e, 0x26);
    b16(&e, PM_SCRATCH(WIN_NE_BOOT_CALL_SP_OFFSET)); /* mov cs:[call_sp],sp */
    b8(&e, 0x60); /* pusha (16-bit) */
    b8(&e, 0x1e); b8(&e, 0x06); b8(&e, 0x9c); /* push ds,es,flags */
    b8(&e, 0x8c); b8(&e, 0xd0); /* mov ax,ss */
    b8(&e, 0x2e); b8(&e, 0xa3); b16(&e, PM_SCRATCH(WIN_NE_BOOT_PM_SS_OFFSET));
    b8(&e, 0x2e); b8(&e, 0x89); b8(&e, 0x26);
    b16(&e, PM_SCRATCH(WIN_NE_BOOT_PM_SP_OFFSET));
    b8(&e, 0xfa); /* cli */
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0); /* mov eax,cr0 */
    b8(&e, 0x24); b8(&e, 0xfe);                /* and al,~PE */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0); /* mov cr0,eax */
    b8(&e, 0xea); /* jump to real-mode service code */
    size_t callback_rm_ref = e.n; b16(&e, 0); b16(&e, a->rm_segment);
    size_t callback_rm = e.n;

    b8(&e, 0x8c); b8(&e, 0xc8); /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8); /* mov ds,ax */
    b8(&e, 0x8e); b8(&e, 0xd0); /* mov ss,ax */
    b8(&e, 0xbc); b16(&e, 0x00ee); /* no guest pushes in RM callback */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e); /* lidt [saved_idtr] */
    b16(&e, WIN_NE_BOOT_SAVED_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt [saved_gdtr] */
    b16(&e, (uint16_t)saved);
    b8(&e, 0xba); b16(&e, WINX86_BRIDGE_PORT); /* mov dx,port */
    b8(&e, 0xa1); b16(&e, WIN_NE_BOOT_SVC_OFFSET); /* mov ax,[svc] */
    b8(&e, 0xef); /* OUT: return to the owning ARM WINN thread, in RM */
    b8(&e, 0xa3); b16(&e, WIN_NE_BOOT_RESULT_AX_OFFSET);
    b8(&e, 0x89); b8(&e, 0x16); b16(&e, WIN_NE_BOOT_RESULT_DX_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt [new_gdtr] */
    b16(&e, (uint16_t)fresh);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e); /* lidt [private_idtr] */
    b16(&e, WIN_NE_BOOT_NEW_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0);
    b8(&e, 0x0c); b8(&e, 0x01); /* or al,1 */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0);
    b8(&e, 0xea); /* jump to 16-bit PM resume code */
    size_t callback_pm_ref = e.n; b16(&e, 0); b16(&e, a->pm_segment);
    size_t callback_pm = e.n;

    b8(&e, 0x2e); b8(&e, 0xa1); b16(&e, PM_SCRATCH(WIN_NE_BOOT_PM_SS_OFFSET));
    b8(&e, 0x8e); b8(&e, 0xd0); /* mov ss,ax */
    b8(&e, 0x2e); b8(&e, 0x8b); b8(&e, 0x26);
    b16(&e, PM_SCRATCH(WIN_NE_BOOT_PM_SP_OFFSET)); /* mov sp,cs:[saved] */
    b8(&e, 0x9d); b8(&e, 0x07); b8(&e, 0x1f); b8(&e, 0x61); /* popf,es,ds,popa */
    b8(&e, 0x2e); b8(&e, 0xa1); b16(&e, PM_SCRATCH(WIN_NE_BOOT_RESULT_AX_OFFSET));
    b8(&e, 0x2e); b8(&e, 0x8b); b8(&e, 0x16);
    b16(&e, PM_SCRATCH(WIN_NE_BOOT_RESULT_DX_OFFSET));
    b8(&e, 0xcb); /* RETF to the importing Win16 thunk */
#undef PM_SCRATCH
    if (e.overflow || e.n > WIN_NE_BOOT_SCRATCH_OFFSET)
        return 0;
    while (e.n < WIN_NE_BOOT_SCRATCH_OFFSET) b8(&e, 0xcc);
    while (e.n < WIN_NE_BOOT_CAPACITY) b8(&e, 0);
    patch16(&e, callback_rm_ref, (uint16_t)callback_rm);
    patch16(&e, callback_pm_ref, (uint16_t)(WIN_NE_BOOT_OFFSET + callback_pm));
    if (e.n > WIN_NE_BOOT_CAPACITY ||
        WIN_NE_BOOT_OFFSET + pm_start > UINT16_MAX)
        return 0;
    patch16(&e, save_ref1, (uint16_t)saved);
    patch16(&e, save_ref2, (uint16_t)saved);
    patch16(&e, new_ref, (uint16_t)fresh);
    patch16(&e, pm_ref, (uint16_t)(WIN_NE_BOOT_OFFSET + pm_start));
    patch16(&e, rm_ref, (uint16_t)rm_start);
    memset(buffer, 0xcc, capacity);
    memcpy(buffer, e.bytes, e.n);
    put16(buffer + WIN_NE_BOOT_NEW_GDTR_OFFSET, a->gdt_limit);
    buffer[WIN_NE_BOOT_NEW_GDTR_OFFSET + 2] = (uint8_t)a->gdt_linear;
    buffer[WIN_NE_BOOT_NEW_GDTR_OFFSET + 3] = (uint8_t)(a->gdt_linear >> 8);
    buffer[WIN_NE_BOOT_NEW_GDTR_OFFSET + 4] = (uint8_t)(a->gdt_linear >> 16);
    buffer[WIN_NE_BOOT_NEW_GDTR_OFFSET + 5] = (uint8_t)(a->gdt_linear >> 24);
    put16(buffer + WIN_NE_BOOT_NEW_IDTR_OFFSET,
          (uint16_t)(WIN_NE_FAULT_IDT_BYTES - 1u));
    uint32_t idt_base = ((uint32_t)a->rm_segment << 4) -
                        WIN_NE_BOOT_OFFSET + WIN_NE_FAULT_IDT_OFFSET;
    buffer[WIN_NE_BOOT_NEW_IDTR_OFFSET + 2] = (uint8_t)idt_base;
    buffer[WIN_NE_BOOT_NEW_IDTR_OFFSET + 3] = (uint8_t)(idt_base >> 8);
    buffer[WIN_NE_BOOT_NEW_IDTR_OFFSET + 4] = (uint8_t)(idt_base >> 16);
    buffer[WIN_NE_BOOT_NEW_IDTR_OFFSET + 5] = (uint8_t)(idt_base >> 24);
    return e.n;
}

/* The i386 core's existing call_isr() accepts 16-bit interrupt gates (0x86).
 * Every vector uses a separate MOV BX,vector / JMP near common stub. The
 * common handler notes where the exception frame is (its layout differs for
 * error-code vectors; ARM reads it), drops PE, restores the DOS descriptor
 * tables, and reports FAULT to the ARM bridge from real mode. No x86 core
 * changes are required. */
static void gate(uint8_t *idt, unsigned v, uint16_t offset, uint16_t selector)
{
    put16(idt + 8u*v, offset);
    put16(idt + 8u*v + 2u, selector);
    idt[8u*v + 5u] = 0x86;  /* P, DPL0, 16-bit interrupt gate */
}

int win_ne_fault_make(uint8_t idt[WIN_NE_FAULT_IDT_BYTES],
                      uint8_t code[WIN_NE_FAULT_CODE_BYTES],
                      const win_ne_boot_args *a)
{
    if (!idt || !code || !a || !a->rm_segment || !a->pm_segment ||
        (a->pm_segment & 7u))
        return 0;
    memset(idt, 0, WIN_NE_FAULT_IDT_BYTES);
    memset(code, 0xcc, WIN_NE_FAULT_CODE_BYTES);
    gate(idt, 0x20, WIN_NE_INT20_OFFSET, a->pm_segment);
    gate(idt, 0x21, WIN_NE_INT21_OFFSET, a->pm_segment);
    const unsigned common = 6u * WIN_NE_FAULT_VECTORS;
    for (unsigned v = 0; v < WIN_NE_FAULT_VECTORS; v++) {
        unsigned pos = 6u * v;
        uint16_t off = (uint16_t)(WIN_NE_FAULT_CODE_OFFSET + pos);
        gate(idt, v, v == WIN_NE_YIELD_VECTOR ? (uint16_t)WIN_NE_YIELD_OFFSET : off,
             a->pm_segment);
        code[pos] = 0xbb; /* mov bx,vector */
        put16(code + pos + 1u, (uint16_t)v);
        code[pos + 3u] = 0xe9; /* jmp near common */
        put16(code + pos + 4u, (uint16_t)(common - (pos + 6u)));
    }
    emitter e = { 0 };
    b8(&e, 0x8c); b8(&e, 0xd6);              /* mov si,ss: the frame, */
    b8(&e, 0x89); b8(&e, 0xe7);              /* mov di,sp  for the report */
    b8(&e, 0xfa);                             /* cli */
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0);/* mov eax,cr0 */
    b8(&e, 0x24); b8(&e, 0xfe);               /* and al,~PE */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0);/* mov cr0,eax */
    b8(&e, 0xea);                            /* far jmp to RM */
    size_t rmref = e.n; b16(&e, 0); b16(&e, a->rm_segment);
    size_t rm = e.n;
    b8(&e, 0x8c); b8(&e, 0xc8);              /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8);              /* mov ds,ax */
    b8(&e, 0x8e); b8(&e, 0xd0);              /* mov ss,ax */
    b8(&e, 0xbc); b16(&e, 0x00ee);          /* sp for RM only */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e); /* lidt saved */
    b16(&e, WIN_NE_BOOT_SAVED_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt saved */
    b16(&e, WIN_NE_BOOT_SAVED_GDTR_OFFSET);
    b8(&e, 0xba); b16(&e, WINX86_BRIDGE_PORT);
    b8(&e, 0xb8); b16(&e, WINX86_BRIDGE_FAULT);
    b8(&e, 0xef);                          /* OUT, from RM */
    b8(&e, 0xeb); b8(&e, 0xfe);            /* no return */
    if (e.overflow || common + e.n > WIN_NE_FAULT_CODE_BYTES)
        return 0;
    patch16(&e, rmref, (uint16_t)(WIN_NE_FAULT_CODE_OFFSET -
                                 WIN_NE_BOOT_OFFSET + common + rm));
    memcpy(code + common, e.bytes, e.n);
    return 1;
}

/* An interrupt as a service: the guest's AX kept above the gateway's
 * return address, AX and DX its results, IRET */
static void int21_like(emitter *e, uint16_t service, uint16_t pm_segment)
{
    b8(e, 0x50);                              /* push ax */
    b8(e, 0xb8); b16(e, service);             /* mov ax,service */
    b8(e, 0x9a); b16(e, WIN_NE_BOOT_GATEWAY_OFFSET); /* call far gateway */
    b16(e, pm_segment);
    b8(e, 0x83); b8(e, 0xc4); b8(e, 0x02);    /* add sp,2 (AX, DX: results) */
    b8(e, 0xcf);                              /* iret */
}

int win_ne_dos_make(uint8_t stub[WIN_NE_DOS_STUB_BYTES], const win_ne_boot_args *a)
{
    if (!stub || !a || !a->pm_segment || (a->pm_segment & 7u) || !a->rm_segment)
        return 0;
    memset(stub, 0xcc, WIN_NE_DOS_STUB_BYTES);
    emitter e = { 0 };
    b8(&e, 0xb8); b16(&e, 0x4c00);            /* INT 20h: mov ax,4C00h */
    if (e.n != WIN_NE_INT21_OFFSET - WIN_NE_INT20_OFFSET)
        return 0;
    int21_like(&e, WIN_NE_SERVICE_INT21, a->pm_segment);
    while (e.n < WIN_NE_YIELD_OFFSET - WIN_NE_INT20_OFFSET)
        b8(&e, 0xcc);
    if (e.n != WIN_NE_YIELD_OFFSET - WIN_NE_INT20_OFFSET)
        return 0;
    int21_like(&e, WIN_NE_SERVICE_YIELD, a->pm_segment);

    /* the callback: from Real Mode (CS the trampoline's segment) to PM */
    while (e.n < WIN_NE_CALLBACK_OFFSET - WIN_NE_INT20_OFFSET)
        b8(&e, 0xcc);
    b8(&e, 0xfa);                              /* cli */
    b8(&e, 0x8c); b8(&e, 0xc8);                /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8);                /* mov ds,ax */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16);  /* lgdt [new_gdtr] */
    b16(&e, WIN_NE_BOOT_NEW_GDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e);  /* lidt [private_idtr] */
    b16(&e, WIN_NE_BOOT_NEW_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0);  /* mov eax,cr0 */
    b8(&e, 0x0c); b8(&e, 0x01);                /* or al,1 */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0);  /* mov cr0,eax */
    b8(&e, 0xea);                              /* jmp far pm */
    b16(&e, (uint16_t)(WIN_NE_INT20_OFFSET + e.n + 4u)); b16(&e, a->pm_segment);
    b8(&e, 0x2e); b8(&e, 0x8e); b8(&e, 0x16);  /* mov ss,cs:[cb_ss] */
    b16(&e, WIN_NE_CB_SS_OFFSET);
    b8(&e, 0x2e); b8(&e, 0x8b); b8(&e, 0x26);  /* mov sp,cs:[cb_sp] */
    b16(&e, WIN_NE_CB_SP_OFFSET);
    b8(&e, 0x2e); b8(&e, 0xa1);                /* mov ax,cs:[cb_ds] */
    b16(&e, WIN_NE_CB_DS_OFFSET);
    b8(&e, 0x8e); b8(&e, 0xd8);                /* mov ds,ax (AX too: the */
    b8(&e, 0x8e); b8(&e, 0xc0);                /* mov es,ax  prologue's DS) */
    b8(&e, 0x2e); b8(&e, 0xff); b8(&e, 0x1e);  /* call far cs:[cb_proc] */
    b16(&e, WIN_NE_CB_PROC_OFFSET);
    b8(&e, 0x89); b8(&e, 0xc3);                /* mov bx,ax */
    b8(&e, 0x89); b8(&e, 0xd1);                /* mov cx,dx */
    b8(&e, 0xfa);                              /* cli */
    b8(&e, 0x0f); b8(&e, 0x20); b8(&e, 0xc0);  /* mov eax,cr0 */
    b8(&e, 0x24); b8(&e, 0xfe);                /* and al,~PE */
    b8(&e, 0x0f); b8(&e, 0x22); b8(&e, 0xc0);  /* mov cr0,eax */
    b8(&e, 0xea);                              /* jmp far rm */
    b16(&e, (uint16_t)(WIN_NE_INT20_OFFSET + e.n + 4u - WIN_NE_BOOT_OFFSET));
    b16(&e, a->rm_segment);
    b8(&e, 0x8c); b8(&e, 0xc8);                /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8);                /* mov ds,ax */
    b8(&e, 0x8e); b8(&e, 0xd0);                /* mov ss,ax */
    b8(&e, 0xbc); b16(&e, 0x00ee);             /* no pushes in RM */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x1e);  /* lidt [saved_idtr] */
    b16(&e, WIN_NE_BOOT_SAVED_IDTR_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16);  /* lgdt [saved_gdtr] */
    b16(&e, WIN_NE_BOOT_SAVED_GDTR_OFFSET);
    b8(&e, 0xba); b16(&e, WINX86_BRIDGE_PORT); /* mov dx,port */
    b8(&e, 0xb8); b16(&e, WINX86_BRIDGE_RETURN); /* mov ax,return */
    b8(&e, 0xef);                              /* out dx,ax */
    b8(&e, 0xeb); b8(&e, 0xfe);                /* no return */
    if (e.overflow || e.n > WIN_NE_CB_PROC_OFFSET - WIN_NE_INT20_OFFSET)
        return 0;
    memcpy(stub, e.bytes, e.n);
    return 1;
}
