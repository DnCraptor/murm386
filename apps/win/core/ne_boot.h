/* 16-bit x86 trampoline emitter for native WINN's NE execution probe.
 * This is guest machine code, not ARM code.  It must run in an existing
 * 386 real-mode DOS context and return to that context before signalling
 * WINX86_BRIDGE_EXIT.  No CPU emulator changes are needed.
 */
#ifndef WINN_NE_BOOT_H
#define WINN_NE_BOOT_H
#include <stddef.h>
#include <stdint.h>

/* 64 executable API slots x 16 bytes, followed by the PM trampoline.
 * The full guest thunk segment is 128 slots (2 KiB). */
#define WIN_NE_BOOT_OFFSET 1024u
#define WIN_NE_BOOT_CAPACITY 320u
/* The imported API thunks FAR CALL this common protected/real gateway.
 * Offsets below are relative to the 320-byte boot image, except GATEWAY
 * which is an offset in the encompassing executable thunk segment. */
#define WIN_NE_BOOT_GATEWAY_OFFSET (WIN_NE_BOOT_OFFSET + 128u)
#define WIN_NE_BOOT_SCRATCH_OFFSET 272u
#define WIN_NE_BOOT_SVC_OFFSET 272u
#define WIN_NE_BOOT_CALL_SP_OFFSET 274u
#define WIN_NE_BOOT_PM_SS_OFFSET 276u
#define WIN_NE_BOOT_PM_SP_OFFSET 278u
#define WIN_NE_BOOT_RESULT_AX_OFFSET 280u
#define WIN_NE_BOOT_RESULT_DX_OFFSET 282u

/* 6-byte pseudo-descriptors in RM boot memory. GDT and IDT are restored
 * before *every* OUT from real mode, including the exception path. */
#define WIN_NE_BOOT_SAVED_GDTR_OFFSET 288u
#define WIN_NE_BOOT_NEW_GDTR_OFFSET   294u
#define WIN_NE_BOOT_SAVED_IDTR_OFFSET 300u
#define WIN_NE_BOOT_NEW_IDTR_OFFSET   306u

/* The remaining 768 bytes of the 2 KiB thunk allocation contain an
 * exception IDT and 32 small 16-bit vector stubs + a common RM exit. */
#define WIN_NE_FAULT_IDT_OFFSET    1536u
#define WIN_NE_FAULT_CODE_OFFSET   1792u
#define WIN_NE_FAULT_AREA_BYTES     256u
#define WIN_NE_FAULT_VECTORS         32u
/* The last vector is no CPU exception: WINN raises it itself to stop a
 * guest that has run too long without returning (ne_exec.c). */
#define WIN_NE_TIMEOUT_VECTOR (WIN_NE_FAULT_VECTORS - 1u)


typedef struct {
    uint16_t rm_segment; /* physical boot base >> 4 */
    uint16_t pm_segment; /* selector of executable thunk segment */
    uint16_t ne_cs, ne_ip;
    uint16_t ne_ss, ne_sp; /* data segment used as SS and DS */
    uint16_t gdt_limit;
    uint32_t gdt_linear;
} win_ne_boot_args;

/* Returns bytes emitted (<= WIN_NE_BOOT_CAPACITY), or zero on error. */
size_t win_ne_boot_make(uint8_t *buffer, size_t capacity,
                        const win_ne_boot_args *args);
/* Creates a private IDT and handlers in the already allocated thunk region.
 * A recoverable fault goes through PM -> RM -> WINX86_BRIDGE_FAULT with BX =
 * the vector and SI:DI = SS:SP of its exception frame (selector:offset). */
int win_ne_fault_make(uint8_t idt[WIN_NE_FAULT_AREA_BYTES],
                      uint8_t code[WIN_NE_FAULT_AREA_BYTES],
                      const win_ne_boot_args *args);
#endif
