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
#define WIN_NE_BOOT_CAPACITY 256u

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
#endif
