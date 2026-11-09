/* What the firmware gives WINN's Win16 path, for the host: the emulated PC
 * (the i386 core of src/i386.c, guest RAM at PC_RAM) in a "DOS" context of
 * its own, and DOS memory blocks for the programs' segments. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "dos-api.h"
#include "dos_guest_ptr.h"
#include "../../core/ne_guest.h"

CPU *cpu_new(int gen, CPU_CB **cb);

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static PC pc;

/* the CPU as DOS left it when WINN started: what every guest run comes
 * back to */
PC *get_PC(void)
{
    if (pc.cpu)
        return &pc;
    if (mmap(X86RUN_RAM, 0x110000, PROT_READ | PROT_WRITE,
             MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != X86RUN_RAM) {
        perror("x86run: guest RAM");
        exit(1);
    }
    CPU_CB *cb;
    CPU *cpu = cpu_new(3, &cb);
    cpu->ext_accessors->set_seg16(cpu, 1, 0xf000);      /* CS */
    cpu->ext_accessors->set_seg16(cpu, 2, 0x1000);      /* SS */
    cpu->gprx[regsp].r16 = 0xfffe;
    cpu->ip = cpu->next_ip = 0x1234;
    pc.cpu = cpu;
    return &pc;
}

/* DOS memory: first fit in 128K..640K, paragraph aligned, not zeroed */
#define BLOCKS 64
static struct { uint32_t at, size; } used[BLOCKS];

static int host_alloc(void *o, uint32_t bytes, uint32_t *linear)
{
    (void)o;
    if (!bytes || bytes > 65536)
        return 0;
    bytes = (bytes + 15) & ~15u;
    uint32_t at = 0x20000;
    for (;;) {
        int clash = 0;
        for (int i = 0; i < BLOCKS; i++)
            if (used[i].size && at < used[i].at + used[i].size && used[i].at < at + bytes) {
                at = used[i].at + used[i].size;
                clash = 1;
            }
        if (!clash)
            break;
    }
    if (at + bytes > 0xa0000)
        return 0;
    for (int i = 0; i < BLOCKS; i++)
        if (!used[i].size) {
            used[i].at = at;
            used[i].size = bytes;
            memset(X86RUN_RAM + at, 0xa5, bytes);
            *linear = at;
            return 1;
        }
    return 0;
}

static void host_free(void *o, uint32_t linear)
{
    (void)o;
    for (int i = 0; i < BLOCKS; i++)
        if (used[i].size && used[i].at == linear)
            used[i].size = 0;
}

static int host_write(void *o, uint32_t linear, const void *data, uint32_t bytes)
{
    (void)o;
    if (linear >= 0x100000 || bytes > 0x100000 - linear)
        return 0;
    memcpy(X86RUN_RAM + linear, data, bytes);
    return 1;
}

const win_ne_guest_io *win_ne_guest_dos_io(void)
{
    static const win_ne_guest_io io = { host_alloc, host_free, host_write };
    return &io;
}
