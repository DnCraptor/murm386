/* The test's x87: murm386's fpu.c (as XRIP's escape and xlat_rt_fpu use it)
 * over the test's guest memory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i386.h"
#include "fpu.h"
#include "fpu_glue.h"

uint8_t *tfpu_mem;
int tfpu_exceptions;
static FPU *fpu;
static CPU cpu;

static void setexc(CPU *c, int excno, uword excerr)
{
    (void)c; (void)excerr;
    printf("x87: exception %d (a form fpu.c rejects)\n", excno);
    tfpu_exceptions++;
}
static CPU_ext_accessors_t acc = {.setexc = setexc};

#define MEMORY(bits, type)                                                       \
bool cpu_load##bits(void *c, int seg, uword addr, type *v)                       \
{ (void)c; (void)seg; memcpy(v, tfpu_mem + addr, sizeof(*v)); return true; }     \
bool cpu_store##bits(void *c, int seg, uword addr, type v)                       \
{ (void)c; (void)seg; tfpu_note_write(addr, sizeof(v)); memcpy(tfpu_mem + addr, &v, sizeof(v)); return true; }
MEMORY(8, u8)
MEMORY(16, u16)
MEMORY(32, u32)

void tfpu_exec(int op, int group, int rm, int mem, uint32_t addr, uint16_t *ax)
{
    if (!fpu) {
        fpu = fpu_new();
        cpu.ext_accessors = &acc;
    }
    cpu.gprx[0].r16 = *ax;
    if (mem)
        fpu_exec2(fpu, &cpu, true, op, group, 0, addr);
    else
        fpu_exec1(fpu, &cpu, op, group, (unsigned)rm);
    *ax = cpu.gprx[0].r16;
}

/* The x87 state to / from buf (94 bytes): FNSAVE (which reinitializes the
 * x87, so FRSTOR after it) / FRSTOR, 16-bit forms */
void tfpu_state(uint8_t *buf, int save)
{
    uint8_t *const m = tfpu_mem;
    uint16_t ax = 0;
    tfpu_mem = buf;
    if (save)
        tfpu_exec(5, 6, 0, 1, 0, &ax);
    tfpu_exec(5, 4, 0, 1, 0, &ax);
    tfpu_mem = m;
}
