#ifndef X86RUN_DOS_GUEST_PTR_H
#define X86RUN_DOS_GUEST_PTR_H
#include <stdint.h>
/* the emulator's guest RAM (NO_PAGING: PC_RAM), mapped there by the test:
 * the first 1M and 64K, then the extended memory the programs' segments
 * have (ne_host.c, X86RUN_EXT bytes) */
#define X86RUN_RAM ((uint8_t *)0x11000000u)
#define X86RUN_EXT_BASE 0x110000u
#define X86RUN_EXT (8u << 20)
static inline void *dos_guest_linear_ptr(uint32_t addr)
{
    return addr < X86RUN_EXT_BASE + X86RUN_EXT ? X86RUN_RAM + addr : (void *)0;
}
#endif
