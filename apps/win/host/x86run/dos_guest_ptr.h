#ifndef X86RUN_DOS_GUEST_PTR_H
#define X86RUN_DOS_GUEST_PTR_H
#include <stdint.h>
/* the emulator's guest RAM (NO_PAGING: PC_RAM), mapped there by the test */
#define X86RUN_RAM ((uint8_t *)0x11000000u)
static inline void *dos_guest_linear_ptr(uint32_t addr)
{
    return addr < 0x100000 ? X86RUN_RAM + addr : (void *)0;
}
#endif
