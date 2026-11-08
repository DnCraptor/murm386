#ifndef TEST_WINN_DOS_GUEST_PTR_H
#define TEST_WINN_DOS_GUEST_PTR_H
#include <stdint.h>
extern uint8_t test_guest_ram[0x100000];
static inline void *dos_guest_linear_ptr(uint32_t addr)
{
    return addr < 0x100000 ? test_guest_ram + addr : (void *)0;
}
#endif
