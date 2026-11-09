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

/* After the trampoline: the DOS calls (INT 20h, INT 21h) as API calls
 * through the gateway, then the private IDT (the 32 exception vectors and
 * 20h, 21h), then 32 small 16-bit vector stubs + a common RM exit. */
#define WIN_NE_DOS_STUB_OFFSET     1344u
#define WIN_NE_DOS_STUB_BYTES       192u
#define WIN_NE_INT20_OFFSET        WIN_NE_DOS_STUB_OFFSET
#define WIN_NE_INT21_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 3u)
#define WIN_NE_YIELD_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 16u)
/* A callback (USER calling the program's window procedure): Real Mode
 * entry at CALLBACK_OFFSET (in the trampoline's RM segment: minus
 * WIN_NE_BOOT_OFFSET); it enters protected mode, takes SS:SP and DS from
 * the CB_ words (the arguments are on that stack already), CALLs FAR the
 * CB_PROC, and back in Real Mode reports WINX86_BRIDGE_RETURN with the
 * result DX:AX in CX:BX. */
#define WIN_NE_CALLBACK_OFFSET     (WIN_NE_DOS_STUB_OFFSET + 32u)
#define WIN_NE_CB_PROC_OFFSET      (WIN_NE_DOS_STUB_OFFSET + 160u)
#define WIN_NE_CB_SS_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 164u)
#define WIN_NE_CB_SP_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 166u)
#define WIN_NE_CB_DS_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 168u)
/* the gateway's scratch words, saved around a callback (it nests) */
#define WIN_NE_BOOT_SCRATCH_BYTES  16u
#define WIN_NE_FAULT_IDT_OFFSET    1536u
#define WIN_NE_IDT_VECTORS         0x22u
#define WIN_NE_FAULT_IDT_BYTES     (WIN_NE_IDT_VECTORS * 8u)
#define WIN_NE_FAULT_CODE_OFFSET   (WIN_NE_FAULT_IDT_OFFSET + WIN_NE_FAULT_IDT_BYTES)
#define WIN_NE_FAULT_CODE_BYTES    (2048u - WIN_NE_FAULT_CODE_OFFSET)
#define WIN_NE_FAULT_VECTORS         32u
/* The service an INT 21h becomes: AX of the guest is the first word above
 * the gateway's return address, then the interrupt's IP, CS, FLAGS. */
#define WIN_NE_SERVICE_INT21     0x2100u
/* The last vector is no CPU exception: WINN raises it itself when a guest
 * has run its time slice without calling the API (ne_exec.c). Its stub is
 * an INT 21h one with the service WIN_NE_SERVICE_YIELD: other tasks run,
 * then the guest goes on where it was. */
#define WIN_NE_YIELD_VECTOR      (WIN_NE_FAULT_VECTORS - 1u)
#define WIN_NE_SERVICE_YIELD     0x2200u


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
 * the vector and SI:DI = SS:SP of its exception frame (selector:offset).
 * INT 20h and INT 21h go to the DOS stubs (win_ne_dos_make). */
int win_ne_fault_make(uint8_t idt[WIN_NE_FAULT_IDT_BYTES],
                      uint8_t code[WIN_NE_FAULT_CODE_BYTES],
                      const win_ne_boot_args *args);
/* INT 21h: PUSH AX, the service WIN_NE_SERVICE_INT21 through the gateway,
 * IRET with the flags the native side left in the interrupt frame. INT 20h:
 * INT 21h AX=4C00h. The yield vector: the same with WIN_NE_SERVICE_YIELD.
 * Then the callback entry. */
int win_ne_dos_make(uint8_t stub[WIN_NE_DOS_STUB_BYTES],
                    const win_ne_boot_args *args);
#endif
