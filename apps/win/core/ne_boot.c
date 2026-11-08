/* Real -> 16-bit protected -> real trampoline bytecode emitter.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_boot.h"
#include "x86bridge.h"
#include <string.h>

typedef struct { uint8_t bytes[WIN_NE_BOOT_CAPACITY]; size_t n; } emitter;
static void b8(emitter *e, uint8_t v) { e->bytes[e->n++] = v; }
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
        !a->ne_cs || !a->ne_ss || a->ne_sp < 16u ||
        a->gdt_linear >= 0x1000000u || !a->gdt_limit)
        return 0;

    emitter e = { { 0 }, 0 };
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
