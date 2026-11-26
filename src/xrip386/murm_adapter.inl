/* Host side of the pico-286 CPU port. CPU's ABI prefix is shared directly
 * with native BIOS and DOS. The interpreter's hidden state belongs to the
 * one guest CPU instantiated by pc_new(). */
#include "../bios/bios.h"

static bool open_bus(uint32_t address)
{
    return address >= phys_mem_size && !VGA_WINDOW(address);
}

static uint8_t __time_critical_func(physical_read_byte)(uint32_t address)
{
    return open_bus(address) ? 0xff : pload8(address);
}

static void __time_critical_func(physical_write_byte)(uint32_t address, uint8_t value)
{
    if (!open_bus(address))
        pstore8(address, value);
}

/* Split at unaligned accesses so VGA, EMS, and the top of RAM are decoded
 * for each byte when an operand crosses an address-window boundary. */
#define PHYSICAL_WIDE(bits, type)                                               \
static type __time_critical_func(bus_read##bits)(uint32_t address)          \
{                                                                               \
    if (!(address & (sizeof(type) - 1u)))                                       \
        return open_bus(address) ? (type)~0u : pload##bits(address);              \
    type value = 0;                                                             \
    for (unsigned byte = 0; byte < sizeof(type); ++byte)                          \
        value |= (type)physical_read_byte(address + byte) << (byte * 8);         \
    return value;                                                               \
}                                                                               \
static void __time_critical_func(bus_write##bits)(uint32_t address, type value) \
{                                                                               \
    if (!(address & (sizeof(type) - 1u))) {                                     \
        if (!open_bus(address)) pstore##bits(address, value);                    \
        return;                                                                 \
    }                                                                           \
    for (unsigned byte = 0; byte < sizeof(type); ++byte)                          \
        physical_write_byte(address + byte, (uint8_t)(value >> (byte * 8)));     \
}

PHYSICAL_WIDE(16, uint16_t)
PHYSICAL_WIDE(32, uint32_t)
#undef PHYSICAL_WIDE

static bool dispatch_int_hook(uint8_t vector)
{
    cpu_int_hook_t *hook = active_cpu->int_hooks[vector];
    if (!hook || protected_mode)
        return false;
    flags_now();
    XLAT_NOTE_FOREIGN();    /* native code writes guest RAM directly */
    const uint32_t saved_ip = firstip, saved_esp = first_esp;
    active_cpu->ip = active_cpu->next_ip = XRIP_IP;
    const bool handled = hook->handler(active_cpu, hook->opaque);
    XRIP_IP = active_cpu->next_ip;
    firstip = saved_ip;
    first_esp = saved_esp;
    return handled;
}

static __time_critical_func() void real_interrupt(uint8_t vector)
{
    if (dispatch_int_hook(vector))
        return;
    const uint32_t destination = mem_read32(idtr.base + vector * 4u);
    push(makeflagsword());
    push(XRIP_CS);
    push(XRIP_IP);
    load_segment(regcs, destination >> 16);
    XRIP_IP = (uint16_t)destination;
    ifl = tf = false;
}

static bool bios_escape(uint8_t id)
{
    /* Native BIOS stubs live at guest physical addresses, including the
     * sentinel used by nested BIOS and DOS far calls. */
    if (active_cpu->bios || protected_mode)
        return false;
    uint32_t address = CS_BASE + firstip;
    if (cr0 & CR0_PG)
        address = translate(address, false);
    const uint32_t stub = address - NATIVE_BIOS_STUB_PHYS;
    const bool in_table = stub < 256u * NATIVE_BIOS_STUB_SIZE &&
                          stub % NATIVE_BIOS_STUB_SIZE == 0;
    if (in_table ? id != stub / NATIVE_BIOS_STUB_SIZE
                 : address != NATIVE_BIOS_CALLBACK_PHYS || id != 0xff)
        return false;

    flags_now();
    XLAT_NOTE_FOREIGN();    /* native code writes guest RAM directly */
    const uint32_t saved_ip = firstip, saved_esp = first_esp;
    active_cpu->ip = active_cpu->next_ip = firstip;
    const bool iret = rp2350_bios_handler(active_cpu, id);
    XRIP_IP = active_cpu->next_ip;
    firstip = saved_ip;
    first_esp = saved_esp;
    if (iret)
        interrupt_return(false);
    return true;
}

static void reset86(void)
{
    reset_protected_mode();
    memset(active_cpu->gprx, 0, sizeof(active_cpu->gprx));
    memset(segregs, 0, sizeof(segregs));
    memset(segment_base, 0, sizeof(segment_base));
    memset(debug_registers, 0, sizeof(debug_registers));
    cr2 = cr3 = 0;
    flush_tlb();
    flags_op = 0;
    x86_flags.value = 2u;
    supervisor_access = 0;
    active_cpu->intr = false;
    active_cpu->i286_irq_shadow = false;
    active_cpu->native_done = false;
    active_cpu->excno = 0;
    halted = pending_trap = false;
    XRIP_DX = 0x2308;
    load_segment(regcs, 0xf000);
    active_cpu->ip = active_cpu->next_ip = XRIP_IP = 0xfff0;
}

void xrip_reset_cpu(CPU *cpu)
{
    active_cpu = cpu;
    reset86();
}

#if XLAT
#include "../fdos/xlat/xlat.h"
#include "hardware/structs/timer.h"   /* timer_hw->timerawl: time_us_32(), get_uticks() */

static uint8_t xlat_no_irq;   /* stays 0; RAM, read by generated code */
static xlat_rt_t xlat_state;
/* xlat_state is in use: a native handler run by an interpreted instruction
 * may run nested bursts; those stay in XRIP. */
static bool xlat_busy;
/* This burst ran translated code (statistics). */
static bool xlat_burst_native;

/* Translated execution needs real mode with 16-bit code and stack, no trap,
 * no HLT, no IRQ shadow, no pending IRQ with IF set, direct fast memory. */
/* The machine mode translated code runs in: stays so for the burst. */
static __time_critical_func() bool xlat_mode_ok(const CPU *cpu)
{
    return !(protected_mode || (cr0 & CR0_PE) || code32 || stack32 || !fast_memory_size || tf ||
             cpu->native_done || XRIP_IP > 0xffffu) &&
           /* plain real-mode segments: translated code takes selector = base >> 4 */
           segment_base[reges] == (uint32_t)segregs[reges] << 4 &&
           segment_base[regcs] == (uint32_t)segregs[regcs] << 4 &&
           segment_base[regss] == (uint32_t)segregs[regss] << 4 &&
           segment_base[regds] == (uint32_t)segregs[regds] << 4;
}

/* XRIP must run the next instruction first (an IRQ to take, the STI / MOV SS
 * shadow, HLT, a trap): over after an instruction or the ISR. */
static __time_critical_func() bool xlat_moment_ok(const CPU *cpu)
{
    return !(halted || pending_trap || cpu->i286_irq_shadow || (cpu->intr && ifl));
}

static __time_critical_func() bool xlat_state_ok(const CPU *cpu)
{
    return xlat_mode_ok(cpu) && xlat_moment_ok(cpu);
}

/* ES and DS loaded by translated code (rt->seg_base) become XRIP's, and CS
 * after a far CALL / JMP / RETF there (rt->cs). */
static __time_critical_func() inline void xlat_segs_to_xrip(const xlat_rt_t *rt)
{
    if (segregs[regcs] != rt->cs)
        load_segment(regcs, rt->cs);
    if (segment_base[reges] != rt->seg_base[0])
        load_segment(reges, (uint16_t)(rt->seg_base[0] >> 4));
    if (segment_base[regds] != rt->seg_base[3])
        load_segment(regds, (uint16_t)(rt->seg_base[3] >> 4));
}

/* Called by translated code (xlat.h) for an x87 instruction or FWAIT, as
 * xrip_fpu() and op 9B: fpu.c with the operand decoded at translation and
 * its linear address checked by the code. The forms are those fpu.c does
 * without cpu_setexc (no fault_point here). 0: XRIP must do it (#NM). */
uint32_t __time_critical_func(xlat_rt_fpu)(xlat_rt_t *rt, uint32_t ctl, uint32_t addr)
{
    (void)rt;
    if (ctl & XLAT_FPU_WAIT)
        return (cr0 & 0x0a) != 0x0a;
    if (cr0 & 0x0c)
        return 0;
    if (xlat_stack_free() < XLAT_STACK_NEEDED)
        return 0;
    CPU *const cpu = active_cpu;    /* rt->gpr is cpu->gprx: FNSTSW AX */
    if (!cpu->fpu)
        return 1;
    const int op = (int)(ctl & 7u), group = (int)((ctl >> 3) & 7u);
    if (ctl & XLAT_FPU_MEM)
        fpu_exec2(cpu->fpu, cpu, true, op, group, 0, addr);
    else
        fpu_exec1(cpu->fpu, cpu, op, group, (ctl >> 6) & 7u);
    return 1;
}

/* Called by translated code (xlat.h) for IN / OUT and CLI / STI, as the
 * opcodes E4..E7, EC..EF, FA, FB in real mode (no I/O permission check).
 * The port handlers do not touch the CPU registers. STI: the IRQ shadow as
 * XRIP's; the next back edge reads rt->intr. */
uint32_t __time_critical_func(xlat_rt_io)(xlat_rt_t *rt, uint32_t ctl, uint32_t value)
{
    CPU *const cpu = active_cpu;
    if (ctl & (XLAT_IO_CLI | XLAT_IO_STI)) {
        if (ctl & XLAT_IO_STI) {
            rt->eflags |= 0x200u;
            rt->intr = (const volatile uint8_t *)&cpu->intr;
            cpu->i286_irq_shadow = true;
        } else {
            rt->eflags &= ~0x200u;
            rt->intr = &xlat_no_irq;
        }
        return XLAT_IO_DONE;
    }
    if (xlat_stack_free() < XLAT_STACK_NEEDED)
        return 0;   /* the device code may go deep: XRIP does it */
    const uint16_t port = (uint16_t)ctl;
    const bool word = (ctl >> 16 & 0xfu) == 2u;
    if (ctl & XLAT_IO_OUT) {
        if (word)
            portout16(port, (uint16_t)value);
        else
            portout(port, (uint8_t)value);
        return XLAT_IO_DONE;
    }
    return XLAT_IO_DONE | (word ? (uint32_t)portin16(port) : (uint32_t)(uint8_t)portin(port));
}

/* Called by translated code (xlat.h): XRIP executes the one instruction at
 * rt->ip. The registers are in cpu->gprx already; the lazy flags come from
 * and go back to rt. */
uint32_t __time_critical_func(xlat_interp_one)(xlat_rt_t *rt, uint32_t flags)
{
    CPU *const cpu = active_cpu;
    if (xlat_stack_free() < XLAT_STACK_NEEDED)
        return 0;   /* XRIP runs it after the exit, from a shallower stack */
    xlat_stats.interp++;
    if (flags & 4u)
        xlat_stats.side_interp++;
#if XLAT_LOG
    xlat_prof_note((flags & 4u) ? XLAT_PROF_SIDE : XLAT_PROF_INTERP,
                   (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu), rt->seg_base[1] + (rt->ip & 0xffffu),
                   rt->mem, rt->mem_size, 1);
#endif
    XRIP_IP = rt->ip;
    flags_result = rt->lazy_result;
    flags_dst = rt->lazy_dst;
    flags_src = rt->lazy_src;
    flags_op = rt->lazy_op;
    flags_cf = rt->lazy_cf;
    x86_flags.value = rt->eflags;   /* the shift / MUL helpers write the image */
    xlat_segs_to_xrip(rt);          /* MOV ES / DS, LES, LDS, POP ES / DS, far CS */

    void *context[5];
    void **const outer_fault_point = fault_point;
    fault_point = context;
    if (__builtin_setjmp(context))
        deliver_fault();
    else
        run_instructions(1);
    fault_point = outer_fault_point;

    rt->ip = XRIP_IP;
    rt->lazy_result = flags_result;
    rt->lazy_dst = flags_dst;
    rt->lazy_src = flags_src;
    rt->lazy_op = flags_op;
    rt->lazy_cf = flags_cf;
    rt->eflags = x86_flags.value;
    rt->seg_base[0] = segment_base[reges];
    rt->seg_base[2] = segment_base[regss];
    rt->seg_base[3] = segment_base[regds];
    rt->intr = ifl ? (const volatile uint8_t *)&cpu->intr : &xlat_no_irq;
    if (fast_memory_size != rt->mem_size || !xlat_state_ok(cpu))
        return 0;
    if (segregs[regcs] != rt->cs || segment_base[regcs] != rt->seg_base[1]) {
        /* far CALL/RET/JMP, INT into guest code: go on from the dispatcher */
        rt->cs = segregs[regcs];
        rt->seg_base[1] = segment_base[regcs];
        rt->ip &= 0xffffu;
        return 2;
    }
    if ((flags & 1u) && !xlat_region_still_valid(rt))
        return 0;
    return 1;
}

/* Translated code is entered only here, at the start of a burst: one lookup
 * per burst, nothing in run_instructions(). Returns the guest instructions
 * accounted to translated code (0: nothing ran, the state is untouched).
 * DOS programs run in nested bursts (exec_run_process() inside INT 21h), so
 * nested bursts are included; xlat_run() checks the stack before translating. */
static __time_critical_func() uint32_t xlat_enter(CPU *cpu, uint32_t count, uint32_t t0,
                                                  bool at_start)
{
#if XLAT_LOG
#define XLAT_ENTER_NOTE(s_why, c_why) (xlat_stats.enter_why[at_start ? (s_why) : (c_why)]++)
#else
#define XLAT_ENTER_NOTE(s_why, c_why) ((void)0)
#endif
    if (xlat_busy || !xlat_mode_ok(cpu)) {
        XLAT_ENTER_NOTE(xlat_busy ? XLAT_ENTER_S_BUSY : XLAT_ENTER_S_MODE, XLAT_ENTER_C_MODE);
        xlat_armed = false;
        return 0;
    }
    if (!xlat_moment_ok(cpu)) {
        XLAT_ENTER_NOTE(XLAT_ENTER_S_MOMENT, XLAT_ENTER_C_MOMENT);
        /* An IRQ pending at the burst start (the usual case after a timer
         * tick): XRIP takes it, and the first taken branch to a loop head
         * after the IRET comes back here. Disarmed, the whole burst ran in
         * XRIP, the interrupted loop included. */
        xlat_armed = true;
        return 0;
    }
    xlat_rt_t *const rt = &xlat_state;
    rt->mem = fast_memory;
    rt->mem_size = fast_memory_size;
    rt->seg_base[0] = segment_base[reges];
    rt->seg_base[1] = segment_base[regcs];
    rt->seg_base[2] = segment_base[regss];
    rt->seg_base[3] = segment_base[regds];
    rt->budget = (int32_t)count;
    rt->clock = &timer_hw->timerawl;
    /* In a limited mode, never extend an exhausted instruction budget. */
    rt->deadline = config_get_mips_limit() > 0 ? t0 : t0 + XLAT_SLICE_US;
    rt->extra = 0;
    /* With IF clear an IRQ cannot be taken: the back edges need not stop. */
    rt->intr = ifl ? (const volatile uint8_t *)&cpu->intr : &xlat_no_irq;
    rt->gpr = (uint32_t *)cpu->gprx;
    rt->lazy_result = flags_result;
    rt->lazy_dst = flags_dst;
    rt->lazy_src = flags_src;
    rt->lazy_op = flags_op;
    rt->lazy_cf = flags_cf;
    rt->eflags = x86_flags.value;
    rt->cs = segregs[regcs];
    rt->ip = XRIP_IP;
    rt->fn_entry = xlat_fn_root;   /* a hot CALL target XRIP stopped at */
    xlat_fn_root = UINT32_MAX;
    /* No burst cut while translated code runs: the instructions it hands to
     * XRIP (xlat_interp_one) must run, not stop at a marked branch target. */
    xlat_armed = false;
    xlat_cut = false;
    xlat_busy = true;
    const bool ran = xlat_run(rt);
    xlat_busy = false;
    /* Nothing to run at a burst start: the marked loop heads XRIP reaches
     * later may have regions. At a cut (a marked target) without one: no
     * more cuts this burst (each would cost a lookup and a sample). */
    xlat_armed = ran || at_start;
    XLAT_ENTER_NOTE(ran ? XLAT_ENTER_S_RAN : XLAT_ENTER_S_NONE, ran ? XLAT_ENTER_C_RAN : XLAT_ENTER_C_NONE);
#undef XLAT_ENTER_NOTE
    if (!ran)
        return 0;
    /* Segment registers changed by interpreted instructions are XRIP's own
     * state already. The FLAGS image comes back from rt: interpreted
     * instructions keep the two equal, and the shift / MUL / DIV helpers
     * (xlat_rt_shift, xlat_rt_muldiv) write their flags only there. */
    XRIP_IP = rt->ip;
    x86_flags.value = rt->eflags;
    xlat_segs_to_xrip(rt);
    flags_result = rt->lazy_result;
    flags_dst = rt->lazy_dst;
    flags_src = rt->lazy_src;
    flags_op = rt->lazy_op;
    flags_cf = rt->lazy_cf;
    /* count + extra - budget instructions ran (more than count when the
     * burst went on to its deadline); XRIP runs what is left of count. */
    const int32_t used = (int32_t)(count + rt->extra) - rt->budget;
    const uint32_t ran_insns = used > 0 ? (uint32_t)used : 0;
    retired += ran_insns;
    return ran_insns;
}
#endif

void __time_critical_func(xrip_step_cpu)(CPU *cpu, int count)
{
    active_cpu = cpu;
    XRIP_IP = cpu->next_ip;
    if (!(cr0 & CR0_PG)) {
#if !defined(NO_PAGING)
        const bool direct = !ega128_paging_active() && PC_RAM == (uint8_t *)0x11000000u;
#else
        const bool direct = true;
#endif
        fast_memory = PC_RAM;
        fast_memory_size = direct && phys_mem_size >= 0xa0000u ? 0xa0000u : 0;
        extended_memory = PC_RAM + HMA_START;
        extended_memory_size = direct && phys_mem_size > HMA_START
                               ? phys_mem_size - HMA_START : 0;
    }
#if XLAT
#if XLAT_LOG
    static uint32_t burst_seq;   /* changes if this burst runs nested bursts */
    const uint32_t seq0 = ++burst_seq;
    const uint32_t svc0 = xlat_stats.svc_n;
    const unsigned long retired0 = retired;
#endif
    const uint32_t t0 = timer_hw->timerawl;
    xlat_dirty = true;      /* device service, DMA, native code ran since the last burst */
    uint32_t xlat_used = xlat_enter(cpu, (uint32_t)count, t0, true);
    xlat_burst_native = xlat_used != 0;
#endif

    void *context[5];
    void **const outer_fault_point = fault_point;
    fault_point = context;
    if (__builtin_setjmp(context))
        deliver_fault();
#if XLAT
    else {
        /* XRIP runs what is left of the burst. A taken branch to a marked
         * loop head (xlat_entry_map) stops it there: translated code goes
         * on, then XRIP again. Translated code that cannot be entered there
         * disarms the check for the rest of the burst. */
        uint32_t left = (uint32_t)count;
#if XLAT_LOG
        uint32_t xrip_ran = 0;
#endif
        for (;;) {
            left = xlat_used >= left ? 0 : left - xlat_used;
            if (!left)
                break;
            xlat_cut = false;
            const unsigned long r0 = retired;
            run_instructions(left);
            const uint32_t ran = (uint32_t)(retired - r0);
#if XLAT_LOG
            xrip_ran += ran;
#endif
            left = ran >= left ? 0 : left - ran;
            if (!xlat_cut || !left)
                break;
            xlat_stats.cuts++;
            xlat_used = xlat_enter(cpu, left, t0, false);   /* sets xlat_armed */
            if (xlat_used) {
                xlat_stats.cut_entries++;
                xlat_burst_native = true;
            }
        }
        xlat_cut = false;
#if XLAT_LOG
        /* where XRIP ran out (a sample of where its time goes) */
        if (xrip_ran) {
            xlat_prof_note(XLAT_PROF_XRIP, (uint32_t)segregs[regcs] << 16 | (XRIP_IP & 0xffffu),
                           segment_base[regcs] + (XRIP_IP & 0xffffu), fast_memory,
                           fast_memory_size, xrip_ran);
            xlat_prof_page(segment_base[regcs] + (XRIP_IP & 0xffffu), xrip_ran);
            if (xlat_burst_native)
                xlat_stats.xrip_tail += xrip_ran;
        }
#endif
    }
    xlat_armed = false;
#else
    else
        run_instructions((uint32_t)count);
#endif
    fault_point = outer_fault_point;
#if XLAT && XLAT_LOG
    {
        const uint32_t us = timer_hw->timerawl - t0;
        const uint32_t n = (uint32_t)(retired - retired0);
        if (burst_seq != seq0 || xlat_stats.svc_n != svc0) {
            /* nested bursts or device service: a native handler ran guest
             * code or waited (the burst is not CPU time) */
        } else if (xlat_burst_native) {
            xlat_stats.xlat_bursts++;
            xlat_stats.xlat_us += us;
            xlat_stats.xlat_insns += n;
            xlat_stats.xlat_hist[xlat_us_bucket(us)]++;
        } else if (n == (uint32_t)count) {   /* full XRIP bursts only */
            xlat_stats.xrip_bursts++;
            xlat_stats.xrip_us += us;
            xlat_stats.xrip_insns += n;
            xlat_stats.xrip_hist[xlat_us_bucket(us)]++;
        }
    }
#endif
    flags_now();
    cpu->ip = cpu->next_ip = XRIP_IP;
}

static uint8_t get_reg8(CPU *cpu, u8 index)
{
    return ((uint8_t *)cpu->gprx)[byteregtable[index]];
}
static uint16_t get_reg16(const CPU *cpu, u8 index) { return cpu->gprx[index].r16; }
static uint32_t get_reg32(CPU *cpu, u8 index) { return cpu->gprx[index].r32; }
static void set_reg8(CPU *cpu, u8 index, u8 value)
{
    ((uint8_t *)cpu->gprx)[byteregtable[index]] = value;
}
static void set_reg16(CPU *cpu, u8 index, u16 value) { cpu->gprx[index].r16 = value; }
static void set_reg32(CPU *cpu, u8 index, u32 value) { cpu->gprx[index].r32 = value; }
uint16_t xrip_get_seg16(const CPU *cpu, u8 index)
{
    (void)cpu;
    return segregs[index];
}
void xrip_set_seg16(CPU *cpu, u8 index, u16 selector)
{
    (void)cpu;
    load_segment(index, selector);
}
uint32_t xrip_get_flags(CPU *cpu, u32 mask)
{
    flags_now();
    return cpu->flags.value & mask;
}
void xrip_set_cpu_flags(CPU *cpu, uword set, uword clear)
{
    flags_now();
    cpu->flags.value = ((cpu->flags.value | set) & ~clear & cpu->flags_mask) | 2;
}
static void set_flag(CPU *cpu, u32 mask, bool value)
{
    xrip_set_cpu_flags(cpu, value ? mask : 0, value ? 0 : mask);
}
void xrip_enable_fpu(CPU *cpu)
{
    if (cpu->fpu) fpu_delete(cpu->fpu);
    cpu->fpu = fpu_new();
}
static void raise_irq(CPU *cpu) { cpu->intr = true; }
void xrip_set_exception(CPU *cpu, int exception, uword error)
{
    cpu->excno = exception;
    cpu->excerr = error;
    if (fault_point) {
        const bool pushes_error = exception == 8 || (exception >= 10 && exception <= 14);
        raise_fault((uint8_t)exception, pushes_error ? (int32_t)error : NO_ERROR_CODE);
    }
}
void xrip_abort_cpu(CPU *cpu, int code)
{
    (void)cpu;
    panic("XRIP 386 abort %d", code);
}

CPU *cpu_new(int gen, CPU_CB **callbacks)
{
    static CPU_ext_accessors_t accessors = {
        .get_reg8 = get_reg8, .get_reg16 = get_reg16, .get_reg32 = get_reg32,
        .set_reg8 = set_reg8, .set_reg16 = set_reg16, .set_reg32 = set_reg32,
        .get_seg16 = xrip_get_seg16, .set_seg16 = xrip_set_seg16, .get_flags = xrip_get_flags,
        .set_flag = set_flag, .set_flags = xrip_set_cpu_flags, .enable_fpu = xrip_enable_fpu,
        .reset = xrip_reset_cpu, .step = xrip_step_cpu, .raise_irq = raise_irq,
        .setexc = xrip_set_exception, .abort = xrip_abort_cpu,
    };
    (void)gen;
    CPU *cpu = calloc(1, sizeof(*cpu));
    if (!cpu) return NULL;
    cpu->gen = 3;
    cpu->flags_mask = 0x00037fd7u;
    cpu->ext_accessors = &accessors;
    read86 = physical_read_byte;
    readw86 = bus_read16;
    readdw86 = bus_read32;
    write86 = physical_write_byte;
    writew86 = bus_write16;
    writedw86 = bus_write32;
    cpu_set_a20(cpu, 1);
    xrip_reset_cpu(cpu);
    if (callbacks) *callbacks = &cpu->cb;
    return cpu;
}

void cpu_intcall(CPU *cpu, uint8_t vector)
{
    XRIP_IP = cpu->next_ip;
    void *context[5];
    void **const outer_fault_point = fault_point;
    XLAT_NOTE_FOREIGN();    /* native code writes guest RAM directly */
    const uint32_t saved_ip = firstip, saved_esp = first_esp;
    firstip = XRIP_IP;
    first_esp = dwordregs[regsp];
    fault_point = context;
    if (__builtin_setjmp(context))
        deliver_fault();
    else
        external_interrupt(vector);
    fault_point = outer_fault_point;
    firstip = saved_ip;
    first_esp = saved_esp;
    cpu->next_ip = cpu->ip = XRIP_IP;
}

void cpu_save_regs(const CPU *cpu, CPU_regs *regs)
{
    flags_now();
    memcpy(regs->gprx, cpu->gprx, sizeof(regs->gprx));
    regs->flags = cpu->flags;
    regs->es = XRIP_ES;
    regs->ds = XRIP_DS;
    regs->fs = XRIP_FS;
    regs->gs = XRIP_GS;
}

void cpu_restore_regs(CPU *cpu, const CPU_regs *regs)
{
    flags_op = 0;
    cpu->flags = regs->flags;
    load_segment(reges, regs->es);
    load_segment(regds, regs->ds);
    load_segment(regfs, regs->fs);
    load_segment(reggs, regs->gs);
    memcpy(cpu->gprx, regs->gprx, sizeof(cpu->gprx));
}

cpu_int_hook_t *cpu_set_int_hook(CPU *cpu, u8 vector, cpu_int_hook_t *hook)
{
    cpu_int_hook_t *old = cpu->int_hooks[vector];
    cpu->int_hooks[vector] = hook;
    return old;
}

unsigned long cpui386_get_cycle(const CPU *cpu) { (void)cpu; return retired; }

void cpu_set_a20(CPU *cpu, int enabled)
{
    /* Match the platform's existing always-enabled A20 policy. */
    (void)enabled;
    cpu->a20_mask = UINT32_MAX;
    a20_enabled = true;
}
int cpu_get_a20(CPU *cpu) { (void)cpu; return 1; }

#define FPU_MEMORY(bits, type)                                                  \
bool cpu_load##bits(void *cpu, int segment, uword address, type *value)          \
{                                                                              \
    (void)cpu; (void)segment; *value = mem_read##bits(address); return true;      \
}                                                                              \
bool cpu_store##bits(void *cpu, int segment, uword address, type value)          \
{                                                                              \
    (void)cpu; (void)segment; mem_write##bits(address, value); return true;       \
}
FPU_MEMORY(8, uint8_t)
FPU_MEMORY(16, uint16_t)
FPU_MEMORY(32, uint32_t)
#undef FPU_MEMORY

bool bios_15h_E820h(CPU *cpu)
{
    /* Same physical memory map as the existing i386 backend. */
    const uint32_t index = cpu->gprx[regbx].r32;
    if (cpu->gprx[regax].r32 != 0xe820u ||
        cpu->gprx[regdx].r32 != 0x534d4150u ||
        cpu->gprx[regcx].r32 < 20 || index > 2 ||
        (index == 2 && phys_mem_size <= HMA_START)) {
        set_flag(cpu, CF, true);
        return true;
    }
    const uint32_t dst = segment_base[reges] + cpu->gprx[regdi].r16;
    const uint32_t base = index == 0 ? 0 : index == 1 ? 0xa0000u : HMA_START;
    const uint32_t length = index == 0 ? 0xa0000u : index == 1 ? 0x60000u
                                                             : phys_mem_size - HMA_START;
    pstore32(dst, base);
    pstore32(dst + 4, 0);
    pstore32(dst + 8, length);
    pstore32(dst + 12, 0);
    pstore32(dst + 16, index == 1 ? 2 : 1);
    cpu->gprx[regax].r32 = 0x534d4150u;
    cpu->gprx[regbx].r32 = index == 2 ? 0 : index + 1;
    cpu->gprx[regcx].r32 = 20;
    set_flag(cpu, CF, false);
    return true;
}

bool bios_15h_89h(CPU *cpu)
{
    const uint32_t table = segment_base[reges] + cpu->gprx[regsi].r16;
    const table_register_t gdt = {pload32(table + 0x0a) & 0xffffffu,
                                  pload16(table + 0x08)};
    if (!gdt.base || gdt.limit < 0x37) {
        cpu->gprx[regax].r8[1] = 0x86;
        set_flag(cpu, CF, true);
        return true;
    }
    const uint8_t master_mask = portin(0x21), slave_mask = portin(0xa1);
    portout(0x20, 0x11);
    portout(0x21, cpu->gprx[regbx].r8[0]);
    portout(0x21, 4);
    portout(0x21, 1);
    portout(0x21, master_mask);
    portout(0xa0, 0x11);
    portout(0xa1, cpu->gprx[regbx].r8[1]);
    portout(0xa1, 2);
    portout(0xa1, 1);
    portout(0xa1, slave_mask);

    gdtr = gdt;
    idtr = (table_register_t){pload32(table + 0x12) & 0xffffffu, pload16(table + 0x10)};
    const uint32_t return_ip = pop();
    (void)pop(); /* caller's real-mode CS is replaced by selector 30h */
    const uint16_t return_flags = pop();
    set_cr0(cr0 | CR0_PE);
    load_segment(regds, 0x18);
    load_segment(reges, 0x20);
    load_segment(regss, 0x28);
    set_code_segment(0x30, code_descriptor(0x30), 0, return_ip);
    decodeflagsword(return_flags & ~CF);
    cpu->next_ip = cpu->ip = XRIP_IP;
    cpu->gprx[regax].r8[1] = 0;
    return false; /* The native BIOS escape must not run another IRET. */
}

bool cpu_pending_trap(void) { return pending_trap; }
void cpu_pending_trap_set(bool value) { pending_trap = value; }

/* XRIP owns IRQ shadow after the legacy 286 object is removed. */
bool __not_in_flash_func(cpu_irq_shadow)(const CPU *cpu) { return cpu->i286_irq_shadow; }
void __not_in_flash_func(cpu_irq_shadow_set)(CPU *cpu, bool value) { cpu->i286_irq_shadow = value; }
