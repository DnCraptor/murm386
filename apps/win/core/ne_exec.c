/*
 * ne_exec.c - 386 Win16 execution under native WINN.
 *
 * The native WINN thread owns the emulator.  This prepares a staged NE
 * with a DATA stack, enters PE in guest code, yields on API OUT, returns
 * from the guest FAR procedure, exits PE and returns to WINN.  The code is
 * not checked beforehand: protected mode itself keeps it inside its
 * segments, and whatever it does wrong ends in the private IDT, which takes
 * the CPU back to Real Mode and DOS. WINN runs the guest's instructions
 * itself, in bursts of CPU steps; a guest that has run its time slice
 * without calling the API is made to take the IDT's yield vector, which
 * calls the gateway as an API call does: other tasks run, and it goes on
 * (or ends, when its task is told to quit). For that one step the CPU's
 * interrupt controller callback answers with the vector, much as the
 * bridge borrows the OUT callback.
 *
 * Several NE tasks can run: a native API (a modal MessageBox) switches WINN
 * tasks with the CPU in the DOS context, and another task may start or
 * resume its guest meanwhile. Each guest's state while it waits is the Real
 * Mode gateway's, which the public CPU interface captures completely: its
 * registers in invoke_native's frame on that task's stack, its PM stack and
 * descriptor tables in its own image.
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
    /* Both EXIT and recoverable FAULT switch to RM and restore the original
     * GDTR/IDTR before OUT. Segment setters therefore run in Real Mode. */
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

/* Bursts of 4096 instructions a guest runs without calling the API before
 * other tasks get their turn: some 260 thousand instructions. */
#define WIN_NE_EXEC_SLICE 64u

/* The layout the trampoline expects: code and a writable DATA segment that
 * holds the stack. */
static int valid_entry(const win_ne_guest_image *g)
{
    return g->info.cs_index && g->info.ss_index &&
           g->info.cs_index != g->info.ss_index &&
           g->info.segment_count == 2 &&
           g->cs && g->ss && g->sp >= 64 &&
           (g->segments[g->info.ss_index].flags & 1u) == 1u &&
           (g->segments[g->info.cs_index].flags & 1u) == 0u &&
           g->sp <= g->segments[g->info.ss_index].size &&
           g->ip < g->segments[g->info.cs_index].size;
}

typedef struct {
    const win_cpu_snapshot *dos_context;
    win_ne_guest_api_context *api;
    uint32_t scratch_linear;
    uint16_t rm_segment;
} win_ne_native_gate;

/* OUT is now issued by guest code after switching to RM and restoring
 * the original GDTR. Save the transient RM continuation, expose the caller's
 * Win16 stack to the argument marshaller, and run native USER under the
 * original DOS CPU registers. Native modal loops may call int386() or yield.
 * Restore the transient RM gateway before returning to the interpreter. */
static int invoke_native(void *opaque, CPU *cpu, uint16_t service)
{
    win_ne_native_gate *gate = (win_ne_native_gate *)opaque;
    if (!gate || !gate->api || !gate->dos_context ||
        cpu->ext_accessors->get_seg16(cpu, WIN_CS) != gate->rm_segment)
        return 0;

    const uint8_t *saved = (const uint8_t *)dos_guest_linear_ptr(
        gate->scratch_linear + WIN_NE_BOOT_CALL_SP_OFFSET);
    uint16_t call_sp = (uint16_t)(saved[0] | (uint16_t)saved[1] << 8);
    /* CALL FAR gateway pushed CS:IP; skip it so the API marshaller sees the
     * Win16 caller's far return address at [SS:SP], followed by arguments. */
    CPU guest = *cpu;
    guest.gprx[regsp].r16 = (uint16_t)(call_sp + 4u);

    win_cpu_snapshot transient;
    save_cpu(cpu, &transient);
    restore_cpu(cpu, gate->dos_context);
    int handled = win_ne_guest_api(gate->api, &guest, service);
    restore_cpu(cpu, &transient);
    cpu->gprx[regax].r16 = guest.gprx[regax].r16;
    cpu->gprx[regdx].r16 = guest.gprx[regdx].r16;
    return handled;
}

static int read_guest(void *opaque, uint32_t linear, void *dst, uint32_t bytes)
{
    (void)opaque;
    if (!dst || linear >= 0x100000u || bytes > 0x100000u - linear)
        return 0;
    memcpy(dst, dos_guest_linear_ptr(linear), bytes);
    return 1;
}

/* A run that ended in the trampoline's Real Mode code after its OUT: the
 * DOS descriptor tables are back, the snapshot can be restored. A guest
 * cannot get there by an OUT of its own: it cannot load that CS. */
static int back_in_rm(CPU *cpu, const win_ne_native_gate *gate, int rc)
{
    return (rc == WINX86_OK || rc == WINX86_FAULT || rc == WINX86_BAD_SERVICE) &&
           cpu->ext_accessors->get_seg16(cpu, WIN_CS) == gate->rm_segment;
}

/* The interrupt controller's answer for the one step that takes the yield
 * vector: it puts the real callback back at once (the guest may call the
 * API in the same run, and DOS must see its own interrupts), and keeps an
 * interrupt that was pending for DOS. */
static CPU *yield_cpu;
static int (*yield_saved)(void *);
static bool yield_pending;

static int yield_irq(void *pic)
{
    (void)pic;
    yield_cpu->cb.pic_read_irq = yield_saved;
    yield_cpu->intr = yield_pending;
    return (int)WIN_NE_YIELD_VECTOR;
}

/* The guest has run its slice without calling the API: in its own code it
 * is in protected mode with the private IDT and takes the yield vector,
 * whose stub calls the gateway as an API call does (other tasks run, then
 * it goes on where it was). The trampoline switches modes and tables, so a
 * guest found there runs on out of it first. Returns how the run that
 * follows ends. */
static int preempt(CPU *cpu, win_ne_native_gate *gate, uint16_t thunk_selector)
{
    CPU_ext_accessors_t *e = cpu->ext_accessors;
    uint16_t cs = e->get_seg16(cpu, WIN_CS);
    if (cs == gate->rm_segment || cs == thunk_selector)
        return winx86_run(cpu, invoke_native, gate, 1u);
    yield_cpu = cpu;
    yield_saved = cpu->cb.pic_read_irq;
    yield_pending = cpu->intr;
    cpu->cb.pic_read_irq = yield_irq;
    cpu->intr = true;
    e->set_flags(cpu, 0x200u, 0);       /* the yield service clears it again */
    int rc = winx86_run(cpu, invoke_native, gate, WIN_NE_EXEC_SLICE);
    if (cpu->cb.pic_read_irq == yield_irq) {    /* not taken (cannot be) */
        cpu->cb.pic_read_irq = yield_saved;
        cpu->intr = yield_pending;
    }
    return rc;
}

static int is_error_code_vector(int v)
{
    return v == 8 || (v >= 10 && v <= 14) || v == 17;
}

/* CS:IP from the exception frame at SI:DI (see win_ne_fault_make) */
static void fault_where(const win_ne_guest_image *g, CPU *cpu, win_ne_fault *f)
{
    f->vector = cpu->gprx[regbx].r16;
    f->cs = f->ip = f->segment = 0;
    uint16_t ss = cpu->gprx[regsi].r16;
    uint32_t sp = cpu->gprx[regdi].r16 + (is_error_code_vector(f->vector) ? 2u : 0u);
    for (uint16_t i = 1; i <= g->info.segment_count; i++) {
        const win_ne_guest_segment *s = &g->segments[i];
        uint8_t fr[4];
        if (s->selector != ss || sp + 4u > s->size ||
            !read_guest(NULL, s->linear + sp, fr, 4))
            continue;
        f->ip = (uint16_t)(fr[0] | fr[1] << 8);
        f->cs = (uint16_t)(fr[2] | fr[3] << 8);
        break;
    }
    for (uint16_t i = 1; f->cs && i <= g->info.segment_count; i++)
        if ((g->segments[i].selector & ~3u) == (f->cs & ~3u))
            f->segment = i;
}

/* The PSP: INT 20h at 0 as in DOS, the command line at 81h, its length
 * at 80h, ending with a NUL (Windows) */
static int write_psp(const win_ne_guest_image *g, const char *cmdline)
{
    uint8_t psp[WIN_NE_GUEST_PSP_BYTES];
    memset(psp, 0, sizeof(psp));
    psp[0] = 0xcd;
    psp[1] = 0x20;
    size_t n = cmdline ? strlen(cmdline) : 0;
    if (n > 125)
        n = 125;
    psp[0x80] = (uint8_t)n;
    if (n)
        memcpy(psp + 0x81, cmdline, n);
    return g->io.write(g->opaque, g->psp.linear, psp, sizeof(psp));
}

int win_ne_execute(const char *path, const char *cmdline, int show, int (*yield)(void),
                   win_ne_result *r)
{
    win_ne_result none;
    if (!r)
        r = &none;
    memset(r, 0, sizeof(*r));
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
    if (!valid_entry(&g)) {
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
    uint8_t fault_idt[WIN_NE_FAULT_IDT_BYTES];
    uint8_t fault_code[WIN_NE_FAULT_CODE_BYTES];
    uint8_t dos[WIN_NE_DOS_STUB_BYTES];
    if (!win_ne_boot_make(boot, sizeof(boot), &args) ||
        !win_ne_fault_make(fault_idt, fault_code, &args) ||
        !win_ne_dos_make(dos, &args) ||
        !write_psp(&g, cmdline) ||
        !g.io.write(g.opaque, linear, boot, sizeof(boot)) ||
        !g.io.write(g.opaque, g.thunk_linear + WIN_NE_DOS_STUB_OFFSET, dos, sizeof(dos)) ||
        !g.io.write(g.opaque, g.thunk_linear + WIN_NE_FAULT_IDT_OFFSET,
                    fault_idt, sizeof(fault_idt)) ||
        !g.io.write(g.opaque, g.thunk_linear + WIN_NE_FAULT_CODE_OFFSET,
                    fault_code, sizeof(fault_code))) {
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
    win_ne_guest_api_context api = { &g, read_guest, NULL, 0, path, show, 0, 0, 0, yield, 0 };
    win_ne_native_gate gate = {
        .dos_context = &old, .api = &api,
        .scratch_linear = linear,
        .rm_segment = args.rm_segment
    };
    /* until it ends: its exit, its entry's RETF, a fault, a call WINN has
     * not; an OUT of its own in protected mode ends nothing */
    rc = winx86_run(cpu, invoke_native, &gate, WIN_NE_EXEC_SLICE);
    while (!back_in_rm(cpu, &gate, rc) && rc != WINX86_BAD_CONTEXT)
        rc = preempt(cpu, &gate, g.thunk_selector);

    /* EXIT, the private-IDT FAULT trap and a refused API call are all in
     * Real Mode with the DOS GDTR/IDTR back. Anything else leaves the CPU
     * in a mode and tables the public CPU interface cannot restore: DOS
     * cannot run again. */
    if (!back_in_rm(cpu, &gate, rc))
        for (;;) { }

    const uint32_t result = ((uint32_t)cpu->gprx[regcx].r16 << 16) |
                             cpu->gprx[regbx].r16;
    int guest_fault = rc == WINX86_FAULT;
    if (guest_fault)
        fault_where(&g, cpu, &r->fault);
    restore_cpu(cpu, &old);
    r->exited = api.exited;
    r->stopped = api.stopped;
    r->value = api.exited ? api.exit_code : guest_fault ? 0 : result;
    r->unsupported = api.unsupported;
    rc = guest_fault ? WIN_NE_EXEC_FAULT :
         rc == WINX86_BAD_SERVICE && api.unsupported ? WIN_NE_EXEC_UNSUPPORTED :
         rc == WINX86_BAD_SERVICE || api.invalid_args ? WIN_NE_EXEC_BAD_ARGS : WIN_NE_OK;
done:
    win_ne_guest_release(&g);
    return rc;
}
#endif
