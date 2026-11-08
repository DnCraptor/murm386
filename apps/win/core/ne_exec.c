/*
 * ne_exec.c - restricted 386 Win16 execution under native WINN.
 *
 * The native WINN thread owns the emulator.  This prepares a staged NE
 * with a DATA stack, enters PE in guest code, yields on API OUT, returns
 * from the guest FAR procedure, exits PE and returns to WINN.  The executor
 * accepts ONLY preflighted straight-line Win16 API calls using a bounded
 * PASCAL argument subset; arbitrary NE applications remain disabled.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_exec.h"

#if defined(__arm__) || defined(__thumb__)
#include <stddef.h>
#include <string.h>
#include "dos-api.h"
#include "dos_guest_ptr.h"
#include "ne_guest.h"
#include "ne_guest_api.h"
#include "ne_boot.h"
#include "ne_entry.h"

/* CPU_ext_accessors segment indices, matching native int386x(). */
enum { WIN_ES, WIN_CS, WIN_SS, WIN_DS, WIN_FS, WIN_GS, WIN_SEG_COUNT };

typedef struct {
    gprx_t gpr[8];
    uint16_t seg[WIN_SEG_COUNT];
    uint32_t flags, ip, next_ip;
    int excno;
    uint32_t excerr;
} win_cpu_snapshot;

static void save_cpu(CPU *cpu, win_cpu_snapshot *s)
{
    const CPU_ext_accessors_t *e = cpu->ext_accessors;
    for (unsigned i = 0; i < 8; i++) s->gpr[i] = cpu->gprx[i];
    for (unsigned i = 0; i < WIN_SEG_COUNT; i++)
        s->seg[i] = e->get_seg16(cpu, (uint8_t)i);
    /* Flush 386 lazy flags before the transient Win16 execution. */
    s->flags = e->get_flags(cpu, 0xffffffffu);
    s->ip = cpu->ip;
    s->next_ip = cpu->next_ip;
    s->excno = cpu->excno;
    s->excerr = cpu->excerr;
}

static void restore_cpu(CPU *cpu, const win_cpu_snapshot *s)
{
    CPU_ext_accessors_t *e = cpu->ext_accessors;
    /* Guest code has cleared PE before OUT EXIT. The saved GDTR was also
     * restored in guest code; segment setters therefore run in Real Mode. */
    for (unsigned i = 0; i < WIN_SEG_COUNT; i++)
        e->set_seg16(cpu, (uint8_t)i, s->seg[i]);
    for (unsigned i = 0; i < 8; i++) cpu->gprx[i] = s->gpr[i];
    e->set_flags(cpu, s->flags, ~s->flags);
    cpu->ip = s->ip;
    cpu->next_ip = s->next_ip;
    cpu->prefetch_base = (uint32_t)-1; /* stale prefetch cannot be retained */
    cpu->excno = s->excno;
    cpu->excerr = s->excerr;
}

/* Permit only a straight-line NE entry: NOP, verified FAR imports,
 * then RETF. A real Win16 application's instructions and ABI are still
 * unsupported. Reject them *before* changing CR0.PE: no PM fault handler
 * has been installed for arbitrary code. */
static int valid_basic_entry(const win_ne_guest_image *g, unsigned *calls)
{
    if (!g->info.cs_index || !g->info.ss_index ||
        g->info.cs_index == g->info.ss_index ||
        g->info.segment_count != 2 || g->thunk_count == 0 ||
        !g->cs || !g->ss || g->sp < 16 ||
        (g->segments[g->info.ss_index].flags & 1u) != 1u ||
        (g->segments[g->info.cs_index].flags & 1u) != 0u ||
        g->sp > g->segments[g->info.ss_index].size ||
        g->ip >= g->segments[g->info.cs_index].size)
        return 0;
    const win_ne_guest_segment *code = &g->segments[g->info.cs_index];
    const uint8_t *p = (const uint8_t *)dos_guest_linear_ptr(code->linear);
    return win_ne_entry_calls(p, code->size, g->ip, g->thunk_selector,
                              g->thunk_services, g->thunk_count, calls);
}

static int read_guest(void *opaque, uint32_t linear, void *dst, uint32_t bytes)
{
    (void)opaque;
    if (!dst || linear >= 0x100000u || bytes > 0x100000u - linear)
        return 0;
    memcpy(dst, dos_guest_linear_ptr(linear), bytes);
    return 1;
}

int win_ne_execute_basic(const char *path, uint32_t *value)
{
    PC *pc = get_PC();
    CPU *cpu = pc ? pc->cpu : NULL;
    if (!cpu || cpu->gen < 3 || !cpu->ext_accessors ||
        !cpu->ext_accessors->step || !cpu->ext_accessors->get_seg16 ||
        !cpu->ext_accessors->set_seg16 || !cpu->ext_accessors->get_flags ||
        !cpu->ext_accessors->set_flags)
        return WIN_NE_EXEC_BAD_CPU;

    win_ne_guest_image g;
    int rc = win_ne_guest_load(path, &g, win_ne_guest_dos_io(), NULL);
    if (rc)
        return rc;
    unsigned calls = 0;
    if (!valid_basic_entry(&g, &calls)) {
        rc = WIN_NE_EXEC_BAD_ENTRY;
        goto done;
    }

    uint8_t boot[WIN_NE_BOOT_CAPACITY];
    const uint32_t linear = g.thunk_linear + WIN_NE_BOOT_OFFSET;
    win_ne_boot_args args = {
        .rm_segment = (uint16_t)(linear >> 4),
        .pm_segment = g.thunk_selector,
        .ne_cs = g.cs, .ne_ip = g.ip,
        .ne_ss = g.ss, .ne_sp = g.sp,
        .gdt_limit = g.gdt_limit, .gdt_linear = g.gdt_linear
    };
    if (!win_ne_boot_make(boot, sizeof(boot), &args) ||
        !g.io.write(g.opaque, linear, boot, sizeof(boot))) {
        rc = WIN_NE_EXEC_BAD_BOOT;
        goto done;
    }

    win_cpu_snapshot old;
    save_cpu(cpu, &old);
    /* No device ticks are needed during this tiny guest transaction.
     * Interrupts must not enter the PM guest without a protected-mode IDT. */
    cpu->ext_accessors->set_flags(cpu, 0, 0x200u);
    cpu->ext_accessors->set_seg16(cpu, WIN_CS, args.rm_segment);
    cpu->ip = cpu->next_ip = 0;
    cpu->prefetch_base = (uint32_t)-1;
    win_ne_guest_api_context api = { &g, read_guest, NULL, 0 };
    rc = winx86_run(cpu, win_ne_guest_api, &api, calls + 4u);

    /* Fail closed. When no EXIT trap was observed we cannot prove that the
     * CPU returned to Real Mode. Calling DOS or freeing memory then could
     * corrupt the shared emulator. A hardware reset is required instead. */
    if (rc != WINX86_OK)
        for (;;) { }

    const uint32_t result = ((uint32_t)cpu->gprx[regcx].r16 << 16) |
                             cpu->gprx[regbx].r16;
    restore_cpu(cpu, &old);
    if (value) *value = result;
    rc = api.invalid_args ? WIN_NE_EXEC_BAD_ARGS : WIN_NE_OK;
done:
    win_ne_guest_release(&g);
    return rc;
}
#endif
