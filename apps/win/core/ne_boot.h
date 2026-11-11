/* 16-bit x86 trampoline emitter for native WINN's NE execution probe.
 * This is guest machine code, not ARM code.  It must run in an existing
 * 386 real-mode DOS context and return to that context before signalling
 * WINX86_BRIDGE_EXIT.  No CPU emulator changes are needed.
 */
#ifndef WINN_NE_BOOT_H
#define WINN_NE_BOOT_H
#include <stddef.h>
#include <stdint.h>

/* The thunk segment (4 KiB, one DOS block, aliased by several
 * descriptors): 128 API thunks of 16 bytes, the trampoline, the DOS and
 * callback stubs, the private IDT and its fault stubs, and the ring 0 area
 * (the TSS and the stack WINN's code runs on).
 *
 * The program runs in ring 3 (CPL 3, IOPL 0): its segments are DPL 3, and
 * it cannot switch modes, load descriptor tables, CLI or OUT. What it
 * calls goes to ring 0 through call gates (its thunks, its entry's and its
 * window procedures' return), interrupt gates (INT 20h, 21h, the yield
 * vector) and exceptions; the CPU then switches to the TSS's SS0:SP0, a
 * stack it cannot reach, so even a guest with a broken SS:SP faults in
 * good order. */
#define WIN_NE_THUNK_SEGMENT_BYTES 8448u
#define WIN_NE_BOOT_OFFSET 6144u
#define WIN_NE_BOOT_CAPACITY 384u
/* Offsets in the thunk segment, from WIN_NE_BOOT_OFFSET on: the boot entry
 * (Real Mode, at RM_SEG:0000), the exit (ring 0, the target of the exit
 * gate), the gateway every call goes through (ring 0). */
#define WIN_NE_BOOT_EXIT_OFFSET (WIN_NE_BOOT_OFFSET + 96u)
#define WIN_NE_BOOT_GATEWAY_OFFSET (WIN_NE_BOOT_OFFSET + 160u)
/* Offsets in the boot image: the gateway's scratch words, then the
 * pseudo-descriptors */
#define WIN_NE_BOOT_SCRATCH_OFFSET 336u
#define WIN_NE_BOOT_SVC_OFFSET 336u
#define WIN_NE_BOOT_CALL_SP_OFFSET 338u
#define WIN_NE_BOOT_PM_SS_OFFSET 340u
#define WIN_NE_BOOT_PM_SP_OFFSET 342u
#define WIN_NE_BOOT_RESULT_AX_OFFSET 344u
#define WIN_NE_BOOT_RESULT_DX_OFFSET 346u
#define WIN_NE_BOOT_SCRATCH_BYTES  16u

/* 6-byte pseudo-descriptors in RM boot memory. GDT and IDT are restored
 * before *every* OUT from real mode, including the exception path. */
#define WIN_NE_BOOT_SAVED_GDTR_OFFSET 352u
#define WIN_NE_BOOT_NEW_GDTR_OFFSET   358u
#define WIN_NE_BOOT_SAVED_IDTR_OFFSET 364u
#define WIN_NE_BOOT_NEW_IDTR_OFFSET   370u

/* After the trampoline: the DOS calls (INT 20h, INT 21h) as API calls
 * through the gateway, the yield vector's stub, the callback entry and
 * return, the ring 3 stubs the gates are called from, the callback's
 * words. */
#define WIN_NE_DOS_STUB_OFFSET     (WIN_NE_BOOT_OFFSET + WIN_NE_BOOT_CAPACITY)
#define WIN_NE_DOS_STUB_BYTES       256u
#define WIN_NE_INT20_OFFSET        WIN_NE_DOS_STUB_OFFSET
#define WIN_NE_INT21_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 3u)
#define WIN_NE_YIELD_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 16u)
/* A callback (USER calling the program's window procedure): Real Mode
 * entry at CALLBACK_OFFSET (in the trampoline's RM segment: minus
 * WIN_NE_BOOT_OFFSET); it enters protected mode, loads the TSS, and from
 * the ring 0 stack at CB_R0SP returns into the CB_PROC in ring 3 with
 * SS:SP, DS, DI and CX from the CB_ words (the arguments and the return address,
 * the CBRET stub, on that stack already). The procedure's return comes
 * through the callback-return gate to CBRET (ring 0), which reports
 * WINX86_BRIDGE_RETURN from Real Mode with the result DX:AX in CX:BX. */
#define WIN_NE_CALLBACK_OFFSET     (WIN_NE_DOS_STUB_OFFSET + 32u)
#define WIN_NE_CBRET_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 120u)
#define WIN_NE_EXIT_STUB_OFFSET    (WIN_NE_DOS_STUB_OFFSET + 168u)  /* ring 3 */
#define WIN_NE_CBRET_STUB_OFFSET   (WIN_NE_DOS_STUB_OFFSET + 176u)  /* ring 3 */
#define WIN_NE_CB_PROC_OFFSET      (WIN_NE_DOS_STUB_OFFSET + 192u)
#define WIN_NE_CB_SS_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 196u)
#define WIN_NE_CB_SP_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 198u)
#define WIN_NE_CB_DS_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 200u)
#define WIN_NE_CB_R0SP_OFFSET      (WIN_NE_DOS_STUB_OFFSET + 202u)
/* DI and CX for the procedure: a DLL's entry (LibEntry) takes its
 * instance and its local heap's size there */
#define WIN_NE_CB_DI_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 204u)
#define WIN_NE_CB_CX_OFFSET        (WIN_NE_DOS_STUB_OFFSET + 206u)
#define WIN_NE_FAULT_IDT_OFFSET    (WIN_NE_DOS_STUB_OFFSET + WIN_NE_DOS_STUB_BYTES)
#define WIN_NE_IDT_VECTORS         0x34u
#define WIN_NE_FAULT_IDT_BYTES     (WIN_NE_IDT_VECTORS * 8u)
#define WIN_NE_FAULT_CODE_OFFSET   (WIN_NE_FAULT_IDT_OFFSET + WIN_NE_FAULT_IDT_BYTES)
#define WIN_NE_FAULT_CODE_BYTES    384u
#define WIN_NE_FAULT_VECTORS         32u
/* WIN87EM's __FPMATH (its functions by BX), after the fault code: ring 3
 * code a thunk jumps to (ne_guest.c) */
#define WIN_NE_FPMATH_OFFSET       (WIN_NE_FAULT_CODE_OFFSET + WIN_NE_FAULT_CODE_BYTES)
#define WIN_NE_FPMATH_BYTES        96u
/* The ring 0 area: a 16-bit TSS (SP0 at 2, SS0 at 4), then the stack, up
 * to the end of the segment; its own data selector, DPL 0 */
#define WIN_NE_RING0_OFFSET        (WIN_NE_BOOT_OFFSET + 1536u)
#define WIN_NE_RING0_BYTES         (WIN_NE_THUNK_SEGMENT_BYTES - WIN_NE_RING0_OFFSET)
#define WIN_NE_TSS_BYTES           44u
/* The service an INT 21h becomes: AX of the guest is the first word above
 * the gateway's return address, then the interrupt's IP, CS, FLAGS. */
#define WIN_NE_SERVICE_INT21     0x2100u
/* The last vector is no CPU exception: WINN raises it itself when a guest
 * has run its time slice without calling the API (ne_exec.c). Its stub is
 * an INT 21h one with the service WIN_NE_SERVICE_YIELD: other tasks run,
 * then the guest goes on where it was. */
#define WIN_NE_YIELD_VECTOR      (WIN_NE_FAULT_VECTORS - 1u)
#define WIN_NE_SERVICE_YIELD     0x2200u
/* The software interrupts a program may call: the BIOS's (10h, 11h, 12h,
 * 16h, 1Ah: vectors no CPU exception of WINN's guests uses) and, but for
 * 20h and 21h, those from 22h to the IDT's end (DOS's others, the
 * multiplex 2Fh, DPMI's 31h, the mouse's 33h): their gates DPL 3, the
 * vector pushed, then as INT 21h with this service; the frame: AX, the
 * vector, IP, CS, FLAGS. */
#define WIN_NE_SERVICE_BIOS      0x2300u
#define WIN_NE_BIOS_VECTOR(v)    ((v) == 0x10u || (v) == 0x11u || (v) == 0x12u || \
                                  (v) == 0x16u || (v) == 0x1au || \
                                  ((v) >= 0x22u && (v) < WIN_NE_IDT_VECTORS))


typedef struct {
    uint16_t rm_segment; /* physical boot base >> 4 */
    uint16_t pm_segment; /* the thunk segment as ring 0 code */
    uint16_t ne_cs, ne_ip; /* ring 3 (RPL 3) */
    uint16_t ne_ss, ne_sp; /* data segment used as SS and DS; at SS:SP the
                            * return address of the entry (the exit stub) */
    uint16_t gdt_limit;
    uint32_t gdt_linear;
    uint16_t tss;          /* the TSS's selector */
    uint16_t ring0;        /* the ring 0 area's data selector (SS0) */
    uint16_t exit_gate, cbret_gate; /* call gates, DPL 3 */
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
