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
static void b32(emitter *e, uint32_t v)
{
    b16(e, (uint16_t)v); b16(e, (uint16_t)(v >> 16));
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
        a->gdt_linear >= 0x1000000u || !a->gdt_limit)
        return 0;

    emitter e = { 0 };
    /* Entry is addressed as RM_SEG:0000, with the thunk allocation aligned
     * to 16 bytes.  CLI, load saved/new GDTR, then enable PE. */
    b8(&e, 0xfa);                              /* cli */
    b8(&e, 0x8c); b8(&e, 0xc8);                /* mov ax,cs */
    b8(&e, 0x8e); b8(&e, 0xd8);                /* mov ds,ax */
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x06); /* sgdt [saved_gdtr] */
    size_t save_ref1 = e.n; b16(&e, 0);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt [new_gdtr] */
    size_t new_ref = e.n; b16(&e, 0);
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
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16);/* lgdt [saved_gdtr] */
    size_t save_ref2 = e.n; b16(&e, 0);
    b8(&e, 0xba); b16(&e, WINX86_BRIDGE_PORT); /* mov dx,port */
    b8(&e, 0xb8); b16(&e, WINX86_BRIDGE_EXIT);/* mov ax,exit */
    b8(&e, 0xef);                              /* out dx,ax */
    b8(&e, 0xeb); b8(&e, 0xfe);                /* jmp $ (should never run) */

    size_t saved = e.n;
    for (int i = 0; i < 6; i++) b8(&e, 0);
    size_t fresh = e.n;
    b16(&e, a->gdt_limit); b32(&e, a->gdt_linear);

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
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt [saved_gdtr] */
    b16(&e, (uint16_t)saved);
    b8(&e, 0xba); b16(&e, WINX86_BRIDGE_PORT); /* mov dx,port */
    b8(&e, 0xa1); b16(&e, WIN_NE_BOOT_SVC_OFFSET); /* mov ax,[svc] */
    b8(&e, 0xef); /* OUT: return to the owning ARM WINN thread, in RM */
    b8(&e, 0xa3); b16(&e, WIN_NE_BOOT_RESULT_AX_OFFSET);
    b8(&e, 0x89); b8(&e, 0x16); b16(&e, WIN_NE_BOOT_RESULT_DX_OFFSET);
    b8(&e, 0x0f); b8(&e, 0x01); b8(&e, 0x16); /* lgdt [new_gdtr] */
    b16(&e, (uint16_t)fresh);
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
    return e.n;
}
