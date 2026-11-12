/* What the firmware gives WINN's Win16 path, for the host: the emulated PC
 * (the i386 core of src/i386.c, guest RAM at PC_RAM) in a "DOS" context of
 * its own, and memory for the programs' segments: extended memory first
 * (as WINN's heap above the HMA has it on the target; WINN_EXT_KB, decimal,
 * at most: 0 none, without it all 8M of it), then DOS memory blocks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "dos-api.h"
#include "dos_guest_ptr.h"
#include "../../core/ne_guest.h"
#include "ne_pool.h"

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
    if (mmap(X86RUN_RAM, X86RUN_EXT_BASE + X86RUN_EXT, PROT_READ | PROT_WRITE,
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

/* DOS memory: first fit in 128K..640K (WINN_DOS_TOP, hex: less, as a
 * smaller DOS has), paragraph aligned, not zeroed */
#define HOST_TOP (getenv("WINN_DOS_TOP") ? (uint32_t)strtoul(getenv("WINN_DOS_TOP"), 0, 16) : 0xa0000u)
#define BLOCKS 512           /* WRITE.EXE alone has 84 segments */
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
    if (at + bytes > HOST_TOP)
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

/* in place: what follows it free (and inside the 640K) */
static int host_resize(void *o, uint32_t linear, uint32_t bytes)
{
    (void)o;
    if (!bytes || bytes > 65536)
        return 0;
    bytes = (bytes + 15) & ~15u;
    int me = -1;
    for (int i = 0; i < BLOCKS; i++)
        if (used[i].size && used[i].at == linear)
            me = i;
    if (me < 0 || linear + bytes > HOST_TOP)
        return 0;
    for (int i = 0; i < BLOCKS; i++)
        if (i != me && used[i].size && linear < used[i].at + used[i].size &&
            used[i].at < linear + bytes)
            return 0;
    if (bytes > used[me].size)
        memset(X86RUN_RAM + linear + used[me].size, 0xa5, bytes - used[me].size);
    used[me].size = bytes;
    return 1;
}

/* ---------------------------------------------------------------- extended */

static ne_pool ext;
static uint32_t dos_used;

static void ext_start(void)
{
    if (ext.base)
        return;
    const char *kb = getenv("WINN_EXT_KB");
    uint32_t size = X86RUN_EXT;
    if (kb && (uint32_t)strtoul(kb, 0, 10) * 1024u < size)
        size = (uint32_t)strtoul(kb, 0, 10) * 1024u;
    ext.base = X86RUN_EXT_BASE;
    ext.size = size;
}

static int host_alloc_any(void *o, uint32_t bytes, uint32_t *linear)
{
    ext_start();
    if (ne_pool_alloc(&ext, bytes, linear)) {
        memset(X86RUN_RAM + *linear, 0xa5, ne_pool_round(bytes));
        return 1;
    }
    if (!host_alloc(o, bytes, linear))
        return 0;
    dos_used += ne_pool_round(bytes);
    return 1;
}

static int host_alloc_low(void *o, uint32_t bytes, uint32_t *linear)
{
    if (!host_alloc(o, bytes, linear))
        return 0;
    dos_used += ne_pool_round(bytes);
    return 1;
}

static void host_free_any(void *o, uint32_t linear)
{
    if (ne_pool_free(&ext, linear))
        return;
    for (int i = 0; i < BLOCKS; i++)
        if (used[i].size && used[i].at == linear)
            dos_used -= used[i].size;
    host_free(o, linear);
}

static int host_resize_any(void *o, uint32_t linear, uint32_t bytes)
{
    int r = ne_pool_resize(&ext, linear, bytes);
    if (r >= 0)
        return r;
    for (int i = 0; i < BLOCKS; i++)
        if (used[i].size && used[i].at == linear) {
            uint32_t was = used[i].size;
            if (!host_resize(o, linear, bytes))
                return 0;
            dos_used = dos_used - was + used[i].size;
            return 1;
        }
    return 0;
}

int win_ne_guest_mapped(uint32_t linear, uint32_t bytes)
{
    return (linear < 0x100000 && bytes <= 0x100000 - linear) ||
           ne_pool_has(&ext, linear, bytes);
}

static int host_write(void *o, uint32_t linear, const void *data, uint32_t bytes)
{
    (void)o;
    if (!win_ne_guest_mapped(linear, bytes))
        return 0;
    memcpy(X86RUN_RAM + linear, data, bytes);
    return 1;
}

void win_ne_guest_mem_info(win_ne_mem_info *m)
{
    memset(m, 0, sizeof(*m));
    m->dos_used = dos_used;
    m->ext_used = ext.used;
    m->ext_size = ext.size;
}

void win_ne_guest_mem_end(void)
{
    memset(&ext, 0, sizeof(ext));
}

const win_ne_guest_io *win_ne_guest_dos_io(void)
{
    static const win_ne_guest_io io = { host_alloc_any, host_free_any, host_write,
                                        host_resize_any, host_alloc_low };
    return &io;
}
