/* The rest of the firmware the i386 core links against, for test_ne_run:
 * guest RAM at PC_RAM (mapped by the test), no FPU, no BIOS, no clock. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../../../src/i386.h"
#include "dos_guest_ptr.h"

void *g_pc;
unsigned long phys_mem_size = 0x100000;

uint8_t iomem_read8(void *m, uint32_t a) { (void)m; return X86RUN_RAM[a & 0xfffff]; }
uint16_t iomem_read16(void *m, uint32_t a) { (void)m; uint16_t v; memcpy(&v, X86RUN_RAM + (a & 0xfffff), 2); return v; }
uint32_t iomem_read32(void *m, uint32_t a) { (void)m; uint32_t v; memcpy(&v, X86RUN_RAM + (a & 0xfffff), 4); return v; }
void iomem_write8(void *m, uint32_t a, uint8_t v) { (void)m; X86RUN_RAM[a & 0xfffff] = v; }
void iomem_write16(void *m, uint32_t a, uint16_t v) { (void)m; memcpy(X86RUN_RAM + (a & 0xfffff), &v, 2); }
void iomem_write32(void *m, uint32_t a, uint32_t v) { (void)m; memcpy(X86RUN_RAM + (a & 0xfffff), &v, 4); }

void *fpu_new(void) { return NULL; }
bool fpu_exec1(void *f, void *c, int op, int g, unsigned i)
{ (void)f; (void)c; (void)op; (void)g; (void)i; return false; }
bool fpu_exec2(void *f, void *c, bool o, int op, int g, int s, uint32_t a)
{ (void)f; (void)c; (void)o; (void)op; (void)g; (void)s; (void)a; return false; }
uint64_t time_us_64(void) { return 0; }
bool rp2350_bios_handler(CPU *cpu, uint8_t intnum) { (void)cpu; (void)intnum; return false; }
