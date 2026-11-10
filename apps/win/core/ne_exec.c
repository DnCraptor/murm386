/*
 * ne_exec.c - 386 Win16 execution under native WINN.
 *
 * The native WINN thread owns the emulator.  This prepares a staged NE
 * with a DATA stack, enters PE in guest code, yields on API OUT, returns
 * from the guest FAR procedure, exits PE and returns to WINN.  The code is
 * not checked beforehand: whatever it does wrong ends in the private IDT,
 * which takes the CPU back to Real Mode and DOS (the emulated CPU checks
 * no segment limits, so its stores are not kept inside its segments). WINN runs the guest's instructions
 * itself, in bursts of CPU steps; a guest that has run its time slice
 * without calling the API is made to take the IDT's yield vector, which
 * calls the gateway as an API call does: other tasks run, and it goes on
 * (or ends, when its task is told to quit). For that one step the CPU's
 * interrupt controller callback answers with the vector, much as the
 * bridge borrows the OUT callback.
 *
 * The program runs in ring 3 (IOPL 0): what it calls reaches WINN's code
 * in ring 0 through call gates and interrupt gates, and every exception it
 * makes goes to the stack in the ring 0 area its TSS names, so a program
 * with a broken stack still ends in the fault stub, and one that tries to
 * leave protected mode, CLI or OUT faults instead.
 *
 * Several NE tasks can run: a native API (a modal MessageBox) switches WINN
 * tasks with the CPU in the DOS context, and another task may start or
 * resume its guest meanwhile. Each guest's state while it waits is the Real
 * Mode gateway's, which the public CPU interface captures completely: its
 * registers in invoke_native's frame on that task's stack, its ring 0 and
 * ring 3 stacks, its TSS and descriptor tables in its own image (TR is no
 * part of that state: each entry to protected mode loads it).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_exec.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
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

/* What a guest must not have when it enters ring 3: IF (it gets no DOS
 * interrupt in protected mode), IOPL (it may not CLI, OUT or set IF
 * itself), NT (its IRET is no task switch) */
#define WIN_NE_RING3_CLEAR 0x7200u

/* The layout the trampoline expects: code and a writable DATA segment that
 * holds the stack. */
static int valid_entry(const win_ne_guest_image *g)
{
    return g->info.cs_index && g->info.ss_index &&
           g->info.cs_index != g->info.ss_index &&
           g->info.segment_count >= 2 &&
           g->cs && g->ss && g->sp >= 68 &&
           (g->segments[g->info.ss_index].flags & 1u) == 1u &&
           (g->segments[g->info.cs_index].flags & 1u) == 0u &&
           g->sp <= g->segments[g->info.ss_index].size &&
           g->ip < g->segments[g->info.cs_index].size;
}

typedef struct {
    const win_cpu_snapshot *dos_context;
    win_ne_guest_api_context *api;
    uint32_t scratch_linear;
    uint32_t ring0_linear;      /* the TSS, then the ring 0 stack */
    uint16_t rm_segment;
    uint16_t ring0_selector;
} win_ne_native_gate;

static void guest_put16(uint32_t linear, uint16_t v)
{
    uint8_t *p = (uint8_t *)dos_guest_linear_ptr(linear);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t guest_get16(uint32_t linear)
{
    const uint8_t *p = (const uint8_t *)dos_guest_linear_ptr(linear);
    return (uint16_t)(p[0] | p[1] << 8);
}

/* The ring 0 stack at the gateway's entry (CALL_SP): its return address,
 * then what called it. Through a call gate, from ring 3 (a thunk): the
 * caller's SP, SS (the CPU's stack switch); the caller's far return
 * address and its arguments are on that stack. From a DOS or the yield
 * stub (ring 0, in an interrupt from ring 3): the guest's AX, the
 * interrupt's IP, CS, FLAGS, SP, SS. Gives the ring 3 SS:SP, 0 if there
 * is none (a frame not of these). */
enum { GW_CS = 2, GW_SP3 = 4, GW_SS3 = 6, GW_INT_CS = 8, GW_INT_SP3 = 12, GW_INT_SS3 = 14,
       GW_INT_END = 16 };
#define WIN_NE_RING0_FLOOR (WIN_NE_TSS_BYTES + 64u)

static uint16_t gateway_entry(const win_ne_native_gate *gate, uint16_t *ss3, uint16_t *sp3)
{
    uint16_t call_sp = guest_get16(gate->scratch_linear + WIN_NE_BOOT_CALL_SP_OFFSET);
    const uint32_t at = gate->ring0_linear + call_sp;
    *ss3 = *sp3 = 0;
    if (call_sp < WIN_NE_RING0_FLOOR || call_sp > WIN_NE_RING0_BYTES - GW_SS3 - 2u)
        return 0;
    if ((guest_get16(at + GW_CS) & 3u) == 3u) {
        *sp3 = guest_get16(at + GW_SP3);
        *ss3 = guest_get16(at + GW_SS3);
    } else if (call_sp <= WIN_NE_RING0_BYTES - GW_INT_END &&
               (guest_get16(at + GW_INT_CS) & 3u) == 3u) {
        *sp3 = guest_get16(at + GW_INT_SP3);
        *ss3 = guest_get16(at + GW_INT_SS3);
    }
    return call_sp;
}

/* what a callback into the running program needs (win_ne_guest_api_context
 * exec) */
typedef struct {
    win_ne_native_gate gate;
    const win_ne_guest_image *g;
} ne_run;

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

    /* the API marshaller sees the Win16 caller's far return address at
     * SS:SP of its ring 3 stack, followed by the arguments; the gateway's
     * registers and an interrupt's frame on the ring 0 stack */
    win_ne_regs guest;
    memcpy(guest.gprx, cpu->gprx, sizeof(guest.gprx));
    uint16_t call_sp = gateway_entry(gate, &guest.ss, &guest.gprx[regsp].r16);
    if (!call_sp)
        return 0;
    guest.frame = gate->ring0_linear + call_sp + 4u;
    guest.ds = guest_get16(guest.frame - 22u);          /* as the gateway saved it */

    win_cpu_snapshot transient;
    save_cpu(cpu, &transient);
    restore_cpu(cpu, gate->dos_context);
    int bad_before = gate->api->invalid_args;
    int handled = win_ne_guest_api(gate->api, &guest, service);
    if (gate->api->invalid_args && !bad_before && !gate->api->invalid_call)
        gate->api->invalid_call = service;
    /* a fault or an exit in a callback this call made ends the program */
    if (handled == 1 && (gate->api->dead || gate->api->exited || gate->api->stopped))
        handled = WINX86_SERVICE_EXIT;
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
static int yield_vector;
static int (*yield_saved)(void *);
static bool yield_pending;

static int yield_irq(void *pic)
{
    (void)pic;
    yield_cpu->cb.pic_read_irq = yield_saved;
    yield_cpu->intr = yield_pending;
    return yield_vector;
}

/* The guest has run its slice without calling the API: in its own code it
 * is in protected mode with the private IDT and takes the yield vector,
 * whose stub calls the gateway as an API call does (other tasks run, then
 * it goes on where it was). The trampoline switches modes and tables, so a
 * guest found there runs on out of it first. Returns how the run that
 * follows ends. */
static void raise_vector(CPU *cpu, int vector)
{
    yield_cpu = cpu;
    yield_vector = vector;
    yield_saved = cpu->cb.pic_read_irq;
    yield_pending = cpu->intr;
    cpu->cb.pic_read_irq = yield_irq;
    cpu->intr = true;
    cpu->ext_accessors->set_flags(cpu, 0x200u, 0); /* the stub's frame clears it */
}

static void raise_done(CPU *cpu)
{
    if (cpu->cb.pic_read_irq == yield_irq) {    /* not taken (cannot be) */
        cpu->cb.pic_read_irq = yield_saved;
        cpu->intr = yield_pending;
    }
}

static int preempt(CPU *cpu, win_ne_native_gate *gate)
{
    /* only the program's code is ring 3: WINN's own (the trampoline, the
     * stubs, the gateway) runs on to a point where it can be */
    uint16_t cs = cpu->ext_accessors->get_seg16(cpu, WIN_CS);
    if (cs == gate->rm_segment || (cs & 3u) != 3u)
        return winx86_run(cpu, invoke_native, gate, 1u);
    raise_vector(cpu, (int)WIN_NE_YIELD_VECTOR);
    int rc = winx86_run(cpu, invoke_native, gate, WIN_NE_EXEC_SLICE);
    raise_done(cpu);
    return rc;
}

static int is_error_code_vector(int v)
{
    return v == 8 || (v >= 10 && v <= 14) || v == 17;
}

/* CS:IP from the exception frame at SI:DI (see win_ne_fault_make): on the
 * ring 0 stack, where the CPU puts it for a fault in ring 3 (and in ring 0) */
static void fault_where(const win_ne_guest_image *g, const win_ne_native_gate *gate,
                        CPU *cpu, win_ne_fault *f)
{
    f->vector = cpu->gprx[regbx].r16;
    f->cs = f->ip = f->segment = 0;
    uint32_t sp = cpu->gprx[regdi].r16 + (is_error_code_vector(f->vector) ? 2u : 0u);
    uint8_t fr[4];
    if ((cpu->gprx[regsi].r16 & ~3u) != gate->ring0_selector || sp + 4u > WIN_NE_RING0_BYTES ||
        !read_guest(NULL, gate->ring0_linear + sp, fr, 4))
        return;
    f->ip = (uint16_t)(fr[0] | fr[1] << 8);
    f->cs = (uint16_t)(fr[2] | fr[3] << 8);
    for (uint16_t i = 1; i <= g->info.segment_count; i++)
        if ((g->segments[i].selector & ~3u) == (f->cs & ~3u))
            f->segment = i;
}

/* Run the guest until it ends where its CPU state can be given back: its
 * exit, its entry's or a callback's return, a fault, a call WINN has not;
 * turns for the other tasks in between. An OUT of its own in protected
 * mode ends nothing. */
static int run_guest(CPU *cpu, win_ne_native_gate *gate)
{
    int rc = winx86_run(cpu, invoke_native, gate, WIN_NE_EXEC_SLICE);
    while (!back_in_rm(cpu, gate, rc) && rc != WINX86_BAD_CONTEXT)
        rc = preempt(cpu, gate);
    return rc;
}

/* EXIT, RETURN, the private-IDT FAULT trap and a refused API call are all
 * in Real Mode with the DOS GDTR/IDTR back (TR is not: Real Mode has no use
 * for it, and every entry to protected mode loads it again). run_guest
 * ends elsewhere only when the bridge refuses the CPU, which it cannot do
 * for one it has run; and the program, in ring 3, cannot leave protected
 * mode, load tables, halt the CPU or forge the bridge's OUT itself (each a
 * #GP that ends it through the fault stub). Were the CPU anywhere else, in
 * a mode and with tables the public CPU interface cannot restore, DOS
 * could not run again. */
static int take_back(CPU *cpu, const win_ne_native_gate *gate, int rc)
{
    if (!back_in_rm(cpu, gate, rc))
        for (;;) { }
    return rc;
}

/* USER calls the program's window procedure (selector:offset proc) while
 * the program waits in the gateway, in an API call or its turn's end: the
 * arguments and the return address (the callback-return stub) go below
 * the ring 3 stack it waits on, the callback entry runs it in ring 3, the
 * result comes back in Real Mode. Its calls (through the gates, ring 0)
 * go on the ring 0 stack below the gateway's frame: the TSS's SP0 and the
 * gateway's scratch words are kept around it. A fault in it ends the
 * program (dead, at its next return). */
/* the ring 3 stack the program waits on (in its innermost call) */
static const win_ne_guest_segment *guest_stack(const ne_run *run, uint16_t *ss, uint16_t *sp)
{
    const win_ne_guest_image *g = run->g;
    if (!gateway_entry(&run->gate, ss, sp))
        return NULL;
    for (uint16_t i = 1; i <= g->info.segment_count; i++)
        if ((g->segments[i].flags & 1u) && (g->segments[i].selector & ~3u) == (*ss & ~3u))
            return &g->segments[i];
    return NULL;
}

uint32_t win_ne_blob_far(win_ne_guest_api_context *ctx, uint16_t len)
{
    ne_run *run = ctx ? (ne_run *)ctx->exec : NULL;
    uint16_t ss, sp;
    if (!run || !guest_stack(run, &ss, &sp))
        return 0;
    return (uint32_t)ss << 16 | (uint16_t)((sp - len) & ~1u);
}

uint32_t win_ne_callback(win_ne_guest_api_context *ctx, uint32_t proc, uint16_t hwnd,
                         uint16_t msg, uint16_t wp, uint32_t lp)
{
    const uint16_t words[5] = { hwnd, msg, wp, (uint16_t)(lp >> 16), (uint16_t)lp };
    return win_ne_call(ctx, proc, words, 5, NULL, 0);
}

static uint32_t call_with(win_ne_guest_api_context *ctx, uint32_t proc, const uint16_t *words,
                          int n, void *blob, uint16_t len, uint16_t ds, uint16_t di, uint16_t cx);

uint32_t win_ne_call(win_ne_guest_api_context *ctx, uint32_t proc, const uint16_t *words,
                     int n, void *blob, uint16_t len)
{
    return call_with(ctx, proc, words, n, blob, len, 0, 0, 0);
}

/* The DLLs loaded and not yet started: their entries (LibEntry: DS its
 * DGROUP, DI its instance, CX its local heap's size), those they need
 * first. One that refuses (AX 0) stays loaded, as nothing undoes it. */
void win_ne_dll_init(win_ne_guest_api_context *ctx)
{
    win_ne_guest_image *g = ctx ? (win_ne_guest_image *)ctx->image : NULL;
    for (unsigned round = 0; g && round < WIN_NE_GUEST_DLLS; round++) {
        int next = -1;
        for (int i = 0; i < g->dll_count; i++)
            if (g->dlls[i].count && !g->dlls[i].initialized &&
                (next < 0 || g->dlls[i].order < g->dlls[next].order))
                next = i;
        if (next < 0)
            return;
        win_ne_guest_dll *d = &g->dlls[next];
        d->initialized = 1;
        uint16_t inst = win_ne_guest_dll_instance(g, next);
        if (d->cs)
            call_with(ctx, (uint32_t)d->cs << 16 | d->ip, NULL, 0, NULL, 0, inst, inst,
                      d->heap_size);
    }
}

static uint32_t call_with(win_ne_guest_api_context *ctx, uint32_t proc, const uint16_t *words,
                          int n, void *blob, uint16_t len, uint16_t ds, uint16_t di, uint16_t cx)
{
    PC *pc = get_PC();
    ne_run *run = ctx ? (ne_run *)ctx->exec : NULL;
    if (!pc || !pc->cpu || !run || ctx->dead || ctx->exited || ctx->stopped || n < 0 || n > 16)
        return 0;
    CPU *cpu = pc->cpu;
    const win_ne_guest_image *g = run->g;
    const uint32_t boot = run->gate.scratch_linear;
    const uint32_t thunks = g->thunk_linear;
    const uint32_t tss = run->gate.ring0_linear;
    uint16_t ss, top;
    const win_ne_guest_segment *stack = guest_stack(run, &ss, &top);
    uint16_t at = (uint16_t)((top - len) & ~1u), args = (uint16_t)(at - 2u * (unsigned)n);
    uint16_t sp = (uint16_t)(args - 4u);
    /* the ring 0 stack below the gateway's frame: room for the procedure's
     * calls (a gate's and an interrupt's frame, the gateway's registers,
     * an exception's) and theirs */
    uint16_t r0sp = guest_get16(boot + WIN_NE_BOOT_PM_SP_OFFSET);
    if (!stack || sp < 64u || sp > args || at > top || top > stack->size ||
        r0sp < WIN_NE_RING0_FLOOR + 64u || r0sp > WIN_NE_RING0_BYTES)
        return 0;
    uint8_t scratch[WIN_NE_BOOT_SCRATCH_BYTES];
    memcpy(scratch, dos_guest_linear_ptr(boot + WIN_NE_BOOT_SCRATCH_OFFSET), sizeof(scratch));
    const uint16_t sp0 = guest_get16(tss + 2u);
    /* the blob above the arguments, PASCAL: the first word pushed highest;
     * below them the return address, the callback-return stub */
    if (len)
        memcpy(dos_guest_linear_ptr(stack->linear + at), blob, len);
    for (int i = 0; i < n; i++)
        guest_put16(stack->linear + args + 2u * (unsigned)(n - 1 - i), words[i]);
    guest_put16(stack->linear + sp, (uint16_t)WIN_NE_CBRET_STUB_OFFSET);
    guest_put16(stack->linear + sp + 2u, g->thunk3_selector);
    guest_put16(thunks + WIN_NE_CB_PROC_OFFSET, (uint16_t)proc);
    guest_put16(thunks + WIN_NE_CB_PROC_OFFSET + 2, (uint16_t)(proc >> 16));
    guest_put16(thunks + WIN_NE_CB_SS_OFFSET, ss);
    guest_put16(thunks + WIN_NE_CB_SP_OFFSET, sp);
    guest_put16(thunks + WIN_NE_CB_DS_OFFSET, ds ? ds :
                g->segments[g->info.auto_data ? g->info.auto_data : g->info.ss_index].selector);
    guest_put16(thunks + WIN_NE_CB_DI_OFFSET, di);
    guest_put16(thunks + WIN_NE_CB_CX_OFFSET, cx);
    guest_put16(thunks + WIN_NE_CB_R0SP_OFFSET, r0sp);
    guest_put16(tss + 2u, r0sp);

    win_cpu_snapshot here;
    save_cpu(cpu, &here);
    win_ne_native_gate gate = run->gate;
    gate.dos_context = &here;
    cpu->ext_accessors->set_flags(cpu, 0, WIN_NE_RING3_CLEAR);
    cpu->ext_accessors->set_seg16(cpu, WIN_CS, run->gate.rm_segment);
    cpu->ip = cpu->next_ip = WIN_NE_CALLBACK_OFFSET - WIN_NE_BOOT_OFFSET;
    cpu->prefetch_base = (uint32_t)-1;
    int rc = take_back(cpu, &gate, run_guest(cpu, &gate));
    uint32_t result = 0;
    if (rc == WINX86_FAULT) {
        fault_where(g, &gate, cpu, &ctx->fault);
        ctx->dead = 1;
    } else if (rc == WINX86_OK && !ctx->exited && !ctx->stopped) {
        result = ((uint32_t)cpu->gprx[regcx].r16 << 16) | cpu->gprx[regbx].r16;
    } else if (rc == WINX86_BAD_SERVICE) {
        ctx->dead = 1;                  /* a call WINN has not, in the procedure */
    }
    restore_cpu(cpu, &here);
    guest_put16(tss + 2u, sp0);
    memcpy(dos_guest_linear_ptr(boot + WIN_NE_BOOT_SCRATCH_OFFSET), scratch, sizeof(scratch));
    if (len)                            /* what the procedure left in it */
        memcpy(blob, dos_guest_linear_ptr(stack->linear + at), len);
    return result;
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
    const uint32_t ring0 = g.thunk_linear + WIN_NE_RING0_OFFSET;
    /* the entry's return address on its stack: the exit stub (ring 3) */
    const uint16_t sp = (uint16_t)(g.sp - 4u);
    const win_ne_guest_segment *stack = &g.segments[g.info.ss_index];
    win_ne_boot_args args = {
        .rm_segment = (uint16_t)(linear >> 4),
        .pm_segment = g.thunk_selector,
        .ne_cs = g.cs, .ne_ip = g.ip,
        .ne_ss = g.ss, .ne_sp = sp,
        .gdt_limit = g.gdt_limit, .gdt_linear = g.gdt_linear,
        .tss = g.tss_selector, .ring0 = g.ring0_selector,
        .exit_gate = g.exit_gate, .cbret_gate = g.cbret_gate
    };
    /* the TSS: ring 0's stack (SS0:SP0) the CPU switches to, the whole
     * ring 0 area; nothing else of it is used (no task switch) */
    uint8_t tss[WIN_NE_TSS_BYTES];
    memset(tss, 0, sizeof(tss));
    tss[2] = (uint8_t)WIN_NE_RING0_BYTES;
    tss[3] = (uint8_t)(WIN_NE_RING0_BYTES >> 8);
    tss[4] = (uint8_t)g.ring0_selector;
    tss[5] = (uint8_t)(g.ring0_selector >> 8);
    const uint8_t exit_ret[4] = {
        (uint8_t)WIN_NE_EXIT_STUB_OFFSET, (uint8_t)(WIN_NE_EXIT_STUB_OFFSET >> 8),
        (uint8_t)g.thunk3_selector, (uint8_t)(g.thunk3_selector >> 8)
    };
    uint8_t fault_idt[WIN_NE_FAULT_IDT_BYTES];
    uint8_t fault_code[WIN_NE_FAULT_CODE_BYTES];
    uint8_t dos[WIN_NE_DOS_STUB_BYTES];
    if (!win_ne_boot_make(boot, sizeof(boot), &args) ||
        !win_ne_fault_make(fault_idt, fault_code, &args) ||
        !win_ne_dos_make(dos, &args) ||
        !write_psp(&g, cmdline) ||
        !g.io.write(g.opaque, ring0, tss, sizeof(tss)) ||
        !g.io.write(g.opaque, stack->linear + sp, exit_ret, sizeof(exit_ret)) ||
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
    cpu->ext_accessors->set_flags(cpu, 0, WIN_NE_RING3_CLEAR);
    cpu->ext_accessors->set_seg16(cpu, WIN_CS, args.rm_segment);
    cpu->ip = cpu->next_ip = 0;
    cpu->prefetch_base = (uint32_t)-1;
    win_ne_guest_api_context api;
    memset(&api, 0, sizeof(api));
    api.image = &g;
    api.read = read_guest;
    api.path = path;
    api.show = show;
    api.yield = yield;
    ne_run run = {
        .gate = { .dos_context = &old, .api = &api, .scratch_linear = linear,
                  .ring0_linear = ring0, .rm_segment = args.rm_segment,
                  .ring0_selector = g.ring0_selector },
        .g = &g
    };
    api.exec = &run;
    win_ne_user_start(&api);
    rc = take_back(cpu, &run.gate, run_guest(cpu, &run.gate));

    const uint32_t result = ((uint32_t)cpu->gprx[regcx].r16 << 16) |
                             cpu->gprx[regbx].r16;
    int guest_fault = rc == WINX86_FAULT || api.dead;
    if (rc == WINX86_FAULT)
        fault_where(&g, &run.gate, cpu, &r->fault);
    else if (api.dead)
        r->fault = api.fault;           /* in a callback */
    restore_cpu(cpu, &old);
    win_ne_user_end(&api);
    r->exited = api.exited;
    r->stopped = api.stopped;
    r->value = api.exited ? api.exit_code : guest_fault ? 0 : result;
    r->unsupported = api.unsupported;
    r->invalid_call = api.invalid_call;
    rc = guest_fault ? WIN_NE_EXEC_FAULT :
         rc == WINX86_BAD_SERVICE && api.unsupported ? WIN_NE_EXEC_UNSUPPORTED :
         rc == WINX86_BAD_SERVICE || api.invalid_args ? WIN_NE_EXEC_BAD_ARGS : WIN_NE_OK;
done:
    win_ne_guest_release(&g);
    return rc;
}
#endif
