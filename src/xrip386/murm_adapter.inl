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
    XLAT_NOTE_FOREIGN();    /* a nested guest call may have consumed it */
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

static bool bios_escape_impl(uint8_t id, bool leaf)
{
    (void)leaf;
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
#if XLAT
    const unsigned scope = bios_xlat_leaf_scope(active_cpu, id);
    if (leaf && scope == BIOS_XLAT_UNKNOWN)
        ++xlat_stats.bios_write_leaf;
    const uint32_t flags_linear = ((uint32_t)segregs[regss] << 4) +
                                  (uint16_t)dwordregs[regsp] + 4u;
    const bool flags_ram = fast_memory_size >= 2u && flags_linear <= fast_memory_size - 2u;
    const bool flags_umb = flags_linear >= 0xc0000u && flags_linear < 0xfffffu &&
        fast_memory_size && far_direct_at(flags_linear, 1u) == fast_memory + flags_linear;
    const bool cursor = scope == BIOS_XLAT_CURSOR && fast_memory_size >= 0x463u;
    const uint32_t cursor_linear = XRIP_AH == 1 ? 0x460u :
        (XRIP_AH == 2 || XRIP_AH == 0x0e) ?
        0x450u + ((uint32_t)XRIP_BH & 7u) * 2u : 0x462u;
    const unsigned cursor_bytes = XRIP_AH == 5 ? 1u : 2u;
    const bool flags_local = (flags_ram || flags_umb) &&
        (scope == BIOS_XLAT_STACK_FLAGS ||
         (scope == BIOS_XLAT_KBD_READ && fast_memory_size >= 0x41cu) ||
         (scope == BIOS_XLAT_CLOCK && fast_memory_size >= 0x471u));
    const unsigned clock_bytes = XRIP_AH == 0 ? 1u : 5u;
    const bool local = scope == BIOS_XLAT_READONLY || scope == BIOS_XLAT_VRAM || cursor || flags_local;
    if (flags_local && flags_umb)
        ++xlat_stats.bios_flags_umb;
    if (!local)
        XLAT_NOTE_FOREIGN();
#endif
    const uint32_t saved_ip = firstip, saved_esp = first_esp;
    active_cpu->ip = active_cpu->next_ip = firstip;
    const bool iret = rp2350_bios_handler(active_cpu, id);
#if XLAT
    if (!local)
        XLAT_NOTE_FOREIGN();    /* may have written after a nested call */
    else if (flags_local) {
        /* KBD_READ either patches FLAGS while waiting or advances the BDA
         * head when a key is available. Report only the path actually taken. */
        if (scope == BIOS_XLAT_KBD_READ && iret)
            xlat_note_write_n(0x41au, 2u);
        else {
            xlat_note_write_n(flags_linear, 2u);
            if (scope == BIOS_XLAT_CLOCK)
                xlat_note_write_n(clock_bytes == 1u ? 0x470u : 0x46cu, clock_bytes);
        }
    } else if (cursor) {
        xlat_note_write_n(cursor_linear, cursor_bytes);
        ++xlat_stats.bios_cursor_local;
    }
#endif
    XRIP_IP = active_cpu->next_ip;
    firstip = saved_ip;
    first_esp = saved_esp;
    if (iret)
        interrupt_return(false);
    return true;
}

static bool bios_escape(uint8_t id)
{
    return bios_escape_impl(id, false);
}

static void reset86(void)
{
    reset_protected_mode();
    nf_memset(active_cpu->gprx, 0, sizeof(active_cpu->gprx));
    nf_memset(segregs, 0, sizeof(segregs));
    nf_memset(segment_base, 0, sizeof(segment_base));
    nf_memset(debug_registers, 0, sizeof(debug_registers));
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
/* After this many consecutive cuts without a region, XRIP keeps the burst. */
#define XLAT_CUT_MISSES 4u
static uint8_t xlat_cut_misses;

/* Translated execution needs real mode with 16-bit code and stack, no trap,
 * no HLT, no IRQ shadow, no pending IRQ with IF set, direct fast memory. */
/* The machine mode translated code runs in: stays so for the burst. */
static __force_inline bool xlat_real_mode_ok(const CPU *cpu, uint32_t ip)
{
    return !(protected_mode || (cr0 & CR0_PE) || code32 || stack32 || !fast_memory_size || tf ||
             cpu->native_done || ip > 0xffffu) &&
           /* plain real-mode segments: translated code takes selector = base >> 4 */
           segment_base[reges] == (uint32_t)segregs[reges] << 4 &&
           segment_base[regcs] == (uint32_t)segregs[regcs] << 4 &&
           segment_base[regss] == (uint32_t)segregs[regss] << 4 &&
           segment_base[regds] == (uint32_t)segregs[regds] << 4;
}

static __time_critical_func() bool xlat_mode_ok(const CPU *cpu)
{
    return xlat_real_mode_ok(cpu, XRIP_IP);
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

/* Safe I/O does not dirty the translation cache. STI publishes its
 * shadow before returning to XRIP at the following guest instruction. */
uint32_t __time_critical_func(xlat_rt_io)(xlat_rt_t *rt, uint32_t ctl, uint32_t value)
{
    CPU *const cpu = active_cpu;
    if (ctl & (XLAT_IO_CLI | XLAT_IO_STI | XLAT_IO_SET_IF)) {
        const bool enable = (ctl & XLAT_IO_STI) ||
                            ((ctl & XLAT_IO_SET_IF) && value);
        const bool shadow = (ctl & XLAT_IO_STI) && !(rt->eflags & 0x200u);
        rt->eflags = (rt->eflags & ~0x200u) | (enable ? 0x200u : 0u);
        xlat_irq_source(rt, enable ? (const volatile uint8_t *)&cpu->intr : &xlat_no_irq, true);
        if (shadow)
            cpu->i286_irq_shadow = true;
        return XLAT_IO_DONE | ((shadow || *rt->intr) ? XLAT_IO_BOUNDARY : 0u);
    }
    if (xlat_stack_free() < XLAT_STACK_NEEDED)
        return 0;
    const uint16_t port = (uint16_t)ctl;
    const bool word = (ctl >> 16 & 0xfu) == 2u;
    if (ctl & XLAT_IO_OUT) {
        if (!pc_xlat_safe_out(cpu->cb.io, port, word))
            return 0;   /* synchronized XRIP handles DMA etc., no side effects yet */
        if (word)
            cpu->cb.io_write16(cpu->cb.io, port, (uint16_t)value);
        else
            cpu->cb.io_write8(cpu->cb.io, port, (uint8_t)value);
        return XLAT_IO_DONE | (*rt->intr ? XLAT_IO_BOUNDARY : 0u);
    }
    return XLAT_IO_DONE | (word ? (uint32_t)portin16(port) : (uint32_t)(uint8_t)portin(port));
}

/* Decoded scalar reads use precisely the memory functions of readrm8/16/32.
 * They need no GPR spill, segment reload or decoder. */
uint32_t xlat_bus_read(uint32_t address, unsigned bytes)
{
    ++xlat_stats.bus_read;
    return bytes == 1 ? mem_read8(address) : bytes == 2 ? mem_read16(address) : mem_read32(address);
}

/* Only the VGA aperture is admitted by xlat_rt_string_fast. These are
 * exactly the physical bus callbacks used by XRIP for that aperture. */
uint32_t xlat_vga_read(uint32_t address, unsigned bytes)
{
    return bytes == 1 ? read86(address) : bytes == 2 ? readw86(address) : readdw86(address);
}

void xlat_vga_write(uint32_t address, uint32_t value, unsigned bytes)
{
    if (bytes == 1)
        write86(address, (uint8_t)value);
    else if (bytes == 2)
        writew86(address, (uint16_t)value);
    else
        writedw86(address, value);
}

/* xlat.h: code at linear is in the direct guest memory (conventional
 * memory, or upper memory / ROM / HMA where far_direct_at() reaches it at
 * the same offset of fast_memory). */
bool __time_critical_func(xlat_code_direct)(uint32_t linear, uint32_t len)
{
    if (!len || !fast_memory_size)
        return len == 0;
    if (linear < fast_memory_size)
        return linear + len <= fast_memory_size;
    /* Not the system BIOS segment: its native stubs (NATIVE_BIOS_STUB_PHYS)
     * and the callback sentinel (NATIVE_BIOS_CALLBACK_PHYS) are escapes
     * XRIP recognises by address. */
    if (linear < 0x100000u && linear + len > 0xF0000u)
        return false;
    return linear + len > linear && far_direct_at(linear, len - 1u) == fast_memory + linear;
}

/* rt->umb_room: the upper memory far_direct_at() reaches at fast_memory +
 * linear from XLAT_UMB_BASE on, up to the EMS page frame if there is one. */
static uint32_t xlat_umb_room(void)
{
#if defined(NO_PAGING)
    if (!fast_memory_size || phys_mem_size < 0x100000u)
        return 0;
#if EMULATE_LTEMS
    const uint32_t end = EMS_START > XLAT_UMB_BASE ? (uint32_t)EMS_START : 0x100000u;
#else
    const uint32_t end = 0x100000u;
#endif
    return end > XLAT_UMB_BASE + XLAT_UMB_MARGIN ? end - XLAT_UMB_BASE - XLAT_UMB_MARGIN : 0;
#else
    return 0;
#endif
}

/* Shared entry for INT and far-transfer misses: follow at most one
 * actual ROM JMP FAR, then a verified native escape. Never infer the
 * destination from the original INT vector or bypass a guest ISR. */
typedef struct {
    uint16_t cs, ip;
    uint8_t id;
    bool jump;
} xlat_leaf_t;

static bool xlat_leaf_target(CPU *cpu, xlat_leaf_t *leaf)
{
    const uint32_t address = CS_BASE + XRIP_IP;
    if (cpu->bios || !xlat_state_ok(cpu) ||
        address < 0xf0000u || address >= 0x100000u)
        return false;
    leaf->cs = XRIP_CS;
    leaf->ip = (uint16_t)XRIP_IP;
    leaf->jump = getmem8(CS_BASE, XRIP_IP) == 0xea;
    if (leaf->jump) {
        if (XRIP_IP > 0xfffbu || address > 0xffffbu)
            return false;
        leaf->ip = getmem16(CS_BASE, XRIP_IP + 1u);
        leaf->cs = getmem16(CS_BASE, XRIP_IP + 3u);
    }
    const uint32_t base = (uint32_t)leaf->cs << 4;
    const uint32_t stub = base + leaf->ip - NATIVE_BIOS_STUB_PHYS;
    if (stub >= 256u * NATIVE_BIOS_STUB_SIZE ||
        stub % NATIVE_BIOS_STUB_SIZE || leaf->ip > 0xfffdu)
        return false;
    leaf->id = (uint8_t)(stub / NATIVE_BIOS_STUB_SIZE);
    return bios_xlat_can_dispatch(cpu, leaf->id) &&
           getmem8(base, leaf->ip) == 0x0f &&
           getmem8(base, leaf->ip + 1u) == 0xff &&
           getmem8(base, leaf->ip + 2u) == leaf->id;
}

static inline void xlat_prepare_insn(void)
{
    firstip = XRIP_IP;
    first_esp = dwordregs[regsp];
    reptype = segoverride = 0;
    opsize32 = addrsize32 = false;
    useseg = DS_BASE;
    pending_trap = false;
}

static inline void xlat_begin_insn(void)
{
    xlat_prepare_insn();
    ++retired;
}

static unsigned xlat_leaf_run(CPU *cpu, const xlat_leaf_t *leaf)
{
    unsigned steps = 1;
    if (leaf->jump) {
        xlat_begin_insn();
        load_segment(regcs, leaf->cs);
        XRIP_IP = leaf->ip;
        /* A far JMP with IF set does not inhibit an IRQ at its target. */
        if (!xlat_state_ok(cpu))
            return steps;
        ++steps;
    }
    xlat_begin_insn();
    StepIP(3);
    bios_escape_impl(leaf->id, true);
    xlat_stats.int_leaf++;
    return steps;
}

static unsigned __time_critical_func(xlat_direct_int)(CPU *cpu, uint8_t vector, uint32_t len)
{
    xlat_begin_insn();
    StepIP(len);
    const bool hooked = cpu->int_hooks[vector] != NULL;
    intcall86(vector);
    xlat_leaf_t leaf;
    if (!hooked && xlat_leaf_target(cpu, &leaf))
        return 1u + xlat_leaf_run(cpu, &leaf);
    return 1u;
}

static inline void xlat_load_guest(const xlat_rt_t *rt)
{
    XRIP_IP = rt->ip;
    flags_result = rt->lazy_result;
    flags_dst = rt->lazy_dst;
    flags_src = rt->lazy_src;
    flags_op = rt->lazy_op;
    flags_cf = rt->lazy_cf;
    x86_flags.value = rt->eflags;
    xlat_segs_to_xrip(rt);
}

static inline uint32_t xlat_save_guest(xlat_rt_t *rt, CPU *cpu)
{
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
    xlat_irq_source(rt, ifl ? (const volatile uint8_t *)&cpu->intr : &xlat_no_irq, true);
    const bool changed_cs = segregs[regcs] != rt->cs ||
                            segment_base[regcs] != rt->seg_base[1];
    const bool can_continue = fast_memory_size == rt->mem_size && xlat_state_ok(cpu);
    const bool valid = !can_continue || xlat_region_still_valid(rt);
    /* Publish CS even on STOP; the caller must not restore the old CS. */
    rt->cs = segregs[regcs];
    rt->seg_base[1] = segment_base[regcs];
    if (!can_continue || !valid ||
        (rt->wall_deadline && (int32_t)(*rt->clock - rt->wall_deadline) >= 0))
        return 0;
    if (changed_cs) {
        rt->ip &= 0xffffu;
        return 2;
    }
    return 1;
}

/* The trampoline has returned: GPRs, including SP, are already spilled.
 * Nonblocking audited ROM entries can continue in the same xlat_run. */
bool xlat_try_native_escape(xlat_rt_t *rt)
{
    CPU *const cpu = active_cpu;
    if (xlat_stack_free() < XLAT_STACK_NEEDED)
        return false;
    xlat_load_guest(rt);
    xlat_leaf_t leaf;
    if (!xlat_leaf_target(cpu, &leaf))
        return false;
    void *context[5];
    void **const outer_fault_point = fault_point;
    fault_point = context;
    unsigned steps;
    if (__builtin_setjmp(context)) {
        deliver_fault();
        steps = 1;
    } else {
        steps = xlat_leaf_run(cpu, &leaf);
    }
    fault_point = outer_fault_point;
    rt->budget -= (int32_t)steps;
    rt->exit_kind = xlat_save_guest(rt, cpu) ? XLAT_EXIT_BRANCH : XLAT_EXIT_STOP;
    xlat_stats.rom_dispatch++;
    return true;
}

/* The trampoline has already spilled registers. Dispatch a short,
 * dynamically generated INT/RETF without building a region for its stack
 * bytes. Execute RETF only when the actual INT returned to the same CS:IP
 * and the instruction is still CB. A guest ISR or hook is never bypassed. */
bool xlat_try_native_thunk(xlat_rt_t *rt, unsigned kind)
{
    CPU *const cpu = active_cpu;
    const uint32_t key = (uint32_t)rt->cs << 16 | (uint16_t)rt->ip;
    if (!kind || xlat_thunk_kind(rt, key) != kind ||
        xlat_stack_free() < XLAT_STACK_NEEDED)
        return false;
    const uint16_t cs = rt->cs, ip = (uint16_t)rt->ip;
    const uint32_t linear = rt->seg_base[1] + ip;
    const uint8_t vector = kind == 1u ? rt->mem[linear + 1u] : 0;
    if (kind == 1u && vector == 0x21u && ((rt->gpr[0] >> 8) & 0xffu) == 0x4bu)
        return false;  /* EXEC must unwind first */
    xlat_load_guest(rt);
    if (!xlat_state_ok(cpu))
        return false;
    void *context[5];
    void **const outer_fault_point = fault_point;
    fault_point = context;
    if (__builtin_setjmp(context)) {
        deliver_fault();
        --rt->budget;
    } else {
        if (kind == 1u) {
            ++xlat_stats.thunk_int;
            ++xlat_stats.int_direct;
            rt->budget -= (int32_t)xlat_direct_int(cpu, vector, 2u);
        }
        if ((kind == 2u || (XRIP_CS == cs && XRIP_IP == (uint16_t)(ip + 2u) &&
             fast_memory_size == rt->mem_size && rt->budget > 0 && xlat_state_ok(cpu) &&
             (!rt->wall_deadline || (int32_t)(*rt->clock - rt->wall_deadline) < 0) &&
             xlat_region_still_valid(rt) && getmem8(CS_BASE, XRIP_IP) == 0xcbu))) {
            xlat_prepare_insn();
            StepIP(1);
            far_return(false, 0);
            --rt->budget;
            ++xlat_stats.thunk_retf;
        }
    }
    fault_point = outer_fault_point;
    rt->exit_kind = xlat_save_guest(rt, cpu) ? XLAT_EXIT_BRANCH : XLAT_EXIT_STOP;
    return true;
}

/* Decoded string continuation for VGA/MMIO, DF=1, segment wrap and
 * writes to cached source. Reuse XRIP's memory semantics, not its loop
 * or decoder. REP can be interrupted between elements. */
static void xlat_direct_string(xlat_rt_t *rt, CPU *cpu, uint32_t ctl, uint8_t op)
{
    const unsigned len = (ctl >> 8) & 15u;
    const uint8_t bytes = (uint8_t)(1u << ((ctl >> XLAT_XS_SIZE_SHIFT) & 3u));
    const bool rep = (ctl & XLAT_XS_REP) != 0;
    xlat_prepare_insn();
    const uint16_t next = (uint16_t)(XRIP_IP + len);
    useseg = segment_base[(ctl >> XLAT_XS_SEG_SHIFT) & 3u];
    reptype = rep ? 1 : 0;    /* F2 and F3 are identical for MOVS/STOS/OUTS */
    opsize32 = bytes == 4;
    xlat_stats.string_calls++;
    if (rep && !XRIP_CX) {
        XRIP_IP = next;
        return;
    }
    do {
        if (!xlat_state_ok(cpu) ||
            (rt->wall_deadline && (int32_t)(*rt->clock - rt->wall_deadline) >= 0))
            return;
        XRIP_IP = next;
        string_step(op, bytes);
        ++xlat_stats.string_elems;
        --rt->budget;   /* slow-path work, counted by xlat_enter */
        if ((xlat_dirty || xlat_write_hi) && !xlat_region_still_valid(rt))
            return;    /* re-decode modified REP before another element */
        if (!rep || !XRIP_CX) {
            XRIP_IP = next;
            return;
        }
        if (rt->budget_limit && rt->budget <= 0)
            return;
    } while (true);
}

/* Consume the STI shadow with the exact decoded successor (POP DS/ES).
 * Pending IRQs are checked before STI, and after POP, never in between
 * while the shadow is set. */
static void xlat_direct_sti_pop(CPU *cpu, uint32_t ctl)
{
    xlat_prepare_insn();
    StepIP(1);
    if (!ifl)
        cpu->i286_irq_shadow = true;
    ifl = 1;
    if (cpu->intr && !cpu->i286_irq_shadow)
        return;
    cpu->i286_irq_shadow = false;
    xlat_prepare_insn();
    StepIP(1);
    load_segment((ctl & 1u) ? regds : reges, pop());
    ++xlat_stats.shadow_pop;
}

/* Called by translated code: execute one instruction or a native helper.
 * Registers are in cpu->gprx; lazy flags come from and go back to rt. */
uint32_t __time_critical_func(xlat_interp_one)(xlat_rt_t *rt, uint32_t flags)
{
    CPU *const cpu = active_cpu;
    if (xlat_stack_free() < XLAT_STACK_NEEDED)
        return 0;   /* XRIP runs it after the exit, from a shallower stack */
    xlat_load_guest(rt);

    const bool str = (flags & XLAT_XI_STRING) != 0;
    const bool shadow = (flags & XLAT_XI_STI_POP) != 0;
    const bool ordinary = !str && !shadow;
    const bool iret = ordinary && (flags & XLAT_XI_IRET);
    const bool outs = ordinary && (flags & XLAT_XI_OUTS) &&
        pc_xlat_safe_out(cpu->cb.io, XRIP_DX, (flags & XLAT_XI_OUTS_WORD) != 0);
    if ((str || shadow || iret || outs) && !xlat_state_ok(cpu))
        return 0;
    const bool direct = ordinary && (flags & XLAT_XI_INT_MASK) && xlat_state_ok(cpu);
    const uint8_t vector = !direct ? 0 :
        (flags & XLAT_XI_INT3) ? 3 : getmem8(CS_BASE, (uint16_t)(XRIP_IP + 1u));
    /* EXEC can synchronously run a whole child. Unwind the trampoline
     * before dispatch so the child can enter its own JIT normally. */
    if (direct && vector == 0x21 && XRIP_AH == 0x4b)
        return 0;
    if (!direct && !str && !shadow && !iret && !outs) {
        xlat_stats.interp++;
        if (flags & 4u) {
            xlat_stats.side_interp++;
            xlat_note_side(rt);
        }
#if XLAT_LOG
        xlat_prof_note((flags & 4u) ? XLAT_PROF_SIDE : XLAT_PROF_INTERP,
                       (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu), rt->seg_base[1] + (rt->ip & 0xffffu),
                       rt->mem, rt->mem_size, 1);
#endif
    }

    void *context[5];
    void **const outer_fault_point = fault_point;
    fault_point = context;
    if (__builtin_setjmp(context))
        deliver_fault();
    else if (str)
        xlat_direct_string(rt, cpu, flags, (flags & XLAT_XS_STOS) ? 0xaa : 0xa4);
    else if (shadow)
        xlat_direct_sti_pop(cpu, flags);
    else if (outs) {
        /* The decoded forms have 16-bit addresses and no segment override.
         * Keep all elements in one helper; string_step preserves DF, the
         * high halves of SI/CX, I/O ordering and the restart IP. Unsafe
         * ports still use the original single-instruction XRIP path. */
        const uint32_t ctl = (flags & 0xf00u) |
            (((flags >> 8) & 15u) == 2 ? XLAT_XS_REP : 0u) |
            ((flags & XLAT_XI_OUTS_WORD) ? 1u << XLAT_XS_SIZE_SHIFT : 0u) |
            (regds << XLAT_XS_SEG_SHIFT);
        xlat_direct_string(rt, cpu, ctl, 0x6e);
    } else if (iret) {
        /* Same real-mode stack and flag semantics as opcode CF. The
         * common save/dispatch path checks restored TF/IF and pending IRQ. */
        xlat_prepare_insn();
        StepIP(1);
        interrupt_return(false);
    } else if (direct) {
        xlat_stats.int_direct++;
        xlat_direct_int(cpu, vector, (flags & XLAT_XI_INT3) ? 1u : 2u);
    } else
        run_instructions(1);
    fault_point = outer_fault_point;

    return xlat_save_guest(rt, cpu);
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
    if (at_start)
        xlat_cut_misses = 0;
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
    rt->umb_room = xlat_umb_room();
    rt->seg_base[0] = segment_base[reges];
    rt->seg_base[1] = segment_base[regcs];
    rt->seg_base[2] = segment_base[regss];
    rt->seg_base[3] = segment_base[regds];
    const uint32_t initial = count < XLAT_SLICE_CHECK ? count : XLAT_SLICE_CHECK;
    rt->budget = (int32_t)initial;
    rt->clock = &timer_hw->timerawl;
    rt->wall_deadline = (t0 + XLAT_SLICE_US) | 1u;
    rt->budget_limit = config_get_mips_limit() > 0 ? count : 0u;
    /* Limited mode replenishes its remaining credit only in xlat_run. */
    rt->deadline = rt->budget_limit ? t0 : rt->wall_deadline;
    rt->extra = 0;
    /* With IF clear an IRQ cannot be taken: the back edges need not stop. */
    xlat_irq_source(rt, ifl ? (const volatile uint8_t *)&cpu->intr : &xlat_no_irq, false);
    rt->gpr = (uint32_t *)cpu->gprx;
    rt->lazy_result = flags_result;
    rt->lazy_dst = flags_dst;
    rt->lazy_src = flags_src;
    rt->lazy_op = flags_op;
    rt->lazy_cf = flags_cf;
    rt->eflags = x86_flags.value;
    rt->cs = segregs[regcs];
    rt->ip = XRIP_IP;
    rt->fn_key = xlat_fn_key;   /* The target can differ from the current PC after an IRQ. */
    xlat_fn_key = UINT32_MAX;
    /* No burst cut while translated code runs: the instructions it hands to
     * XRIP (xlat_interp_one) must run, not stop at a marked branch target. */
    xlat_armed = false;
    xlat_cut = false;
    xlat_busy = true;
    const bool ran = xlat_run(rt);
    xlat_busy = false;
    /* A successful entry ends the streak of misses. A hot CALL requesting
     * compilation is a sample, so leave room for subsequent marked targets. */
    if (ran)
        xlat_cut_misses = 0;
    else if (!at_start && rt->fn_key == UINT32_MAX)
        xlat_cut_misses++;
    xlat_armed = xlat_cut_misses < XLAT_CUT_MISSES;
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
    /* initial + extra - budget instructions ran (more than count when the
     * burst went on to its deadline); XRIP runs what is left of count. */
    const int32_t used = (int32_t)(initial + rt->extra) - rt->budget;
    const uint32_t ran_insns = used > 0 ? (uint32_t)used : 0;
    retired += ran_insns;
    return ran_insns;
}
#endif

static __force_inline void xrip_bind_memory(void)
{
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

#if XLAT && XLAT_PRECOMPILE
void xrip_precompile_exec(CPU *cpu, uint32_t linear, uint32_t size)
{
    if (!size || xlat_busy || (cr0 & CR0_PG))
        return;
    uint32_t ipsr, primask, basepri;
    __asm volatile("mrs %0, ipsr\n\tmrs %1, primask\n\tmrs %2, basepri"
                   : "=r"(ipsr), "=r"(primask), "=r"(basepri));
    if (ipsr || primask || basepri)
        return;
    xrip_bind_memory();
    if (!xlat_real_mode_ok(cpu, cpu->next_ip))
        return;
    const uint32_t key = (uint32_t)segregs[regcs] << 16 | cpu->next_ip;
    xlat_precompile_image(fast_memory, fast_memory_size, key, linear, size, &timer_hw->timerawl);
}
#endif

void __time_critical_func(xrip_step_cpu)(CPU *cpu, int count)
{
    active_cpu = cpu;
    XRIP_IP = cpu->next_ip;
    if (!(cr0 & CR0_PG))
        xrip_bind_memory();
#if XLAT
    static uint32_t burst_seq;   /* changes if this burst runs nested bursts */
    const uint32_t seq0 = ++burst_seq;
    const unsigned long retired0 = retired;
    const uint32_t compile0 = xlat_stats.translate_us;
#if XLAT_LOG
    const uint32_t svc0 = xlat_stats.svc_n;
    uint32_t burst_xrip_us = 0, burst_xrip_insns = 0;   /* in run_instructions() */
#endif
    const uint32_t t0 = timer_hw->timerawl;
    const bool clean_resume = xlat_clean_resume;
    xlat_clean_resume = false;
    if (!clean_resume || !xlat_mode_ok(cpu))
        xlat_dirty = true;
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
        uint32_t xrip_ran = 0, xrip_us = 0;
#endif
        for (;;) {
            left = xlat_used >= left ? 0 : left - xlat_used;
            xlat_used = 0;
            if (!left || (int32_t)(timer_hw->timerawl - t0) >= (int32_t)XLAT_SLICE_US)
                break;
            xlat_cut = false;
            const unsigned long r0 = retired;
#if XLAT_LOG
            const uint32_t tx = timer_hw->timerawl;
#endif
            const uint32_t chunk = left < XLAT_XRIP_CHECK ? left : XLAT_XRIP_CHECK;
            run_instructions(chunk);
            const uint32_t ran = (uint32_t)(retired - r0);
#if XLAT_LOG
            xrip_ran += ran;
            xrip_us += timer_hw->timerawl - tx;
#endif
            left = ran >= left ? 0 : left - ran;
            if (!left || cpu->native_done || halted ||
                (int32_t)(timer_hw->timerawl - t0) >= (int32_t)XLAT_SLICE_US)
                break;
            if (!xlat_cut) {
                if (!ran)
                    break;
                continue;
            }
            xlat_stats.cuts++;
            xlat_used = xlat_enter(cpu, left, t0, false);   /* sets xlat_armed */
            if (xlat_used) {
                xlat_stats.cut_entries++;
                xlat_burst_native = true;
            }
        }
        /* A compiler checkpoint or native back edge may return because a
         * real IRQ arrived after the CPU burst deadline. Guest registers
         * are synchronized now and fault_point is installed. Deliver it
         * before returning to device polling, even with no burst credit.
         * Do not fetch another guest opcode merely to take the IRQ. */
        if (cpu->intr && ifl && !cpu->i286_irq_shadow && !cpu->native_done && !pending_trap) {
#if XRIP_HW_IRQ_BRIDGE
            xrip_irq_boundary(cpu, xrip_bridge_begin());
#else
            xrip_irq_boundary(cpu, false);
#endif
        }
        xlat_cut = false;
#if XLAT_LOG
        /* Do not charge nested guest execution to the outer caller's PC.
         * Use the same exclusion as the burst totals below. */
        if (xrip_ran && burst_seq == seq0 && xlat_stats.svc_n == svc0) {
            xlat_prof_note(XLAT_PROF_XRIP, (uint32_t)segregs[regcs] << 16 | (XRIP_IP & 0xffffu),
                           segment_base[regcs] + (XRIP_IP & 0xffffu), fast_memory,
                           fast_memory_size, xrip_ran);
            xlat_prof_page(segment_base[regcs] + (XRIP_IP & 0xffffu), xrip_ran);
            if (xlat_burst_native)
                xlat_stats.xrip_tail += xrip_ran;
        }
        burst_xrip_us = xrip_us;
        burst_xrip_insns = xrip_ran;
#endif
    }
    xlat_armed = false;
#else
    else
        run_instructions((uint32_t)count);
#endif
    fault_point = outer_fault_point;
#if XLAT
    const uint32_t us = timer_hw->timerawl - t0;
    const uint32_t n = (uint32_t)(retired - retired0);
    const uint32_t compile_us = xlat_stats.translate_us - compile0;
    if (burst_seq == seq0 && n && us > compile_us) {
        const uint32_t guest_us = us - compile_us;
        /* Limit credit from bursts that ran a slow host or device handler. */
        xlat_compile_credit(guest_us < XLAT_SLICE_US ? guest_us : XLAT_SLICE_US);
    }
#if XLAT_LOG
    {
        if (burst_seq != seq0 || xlat_stats.svc_n != svc0) {
            /* nested bursts or device service: a native handler ran guest
             * code or waited (the burst is not CPU time) */
        } else if (xlat_burst_native) {
            xlat_stats.xlat_bursts++;
            xlat_stats.xlat_us += us;
            xlat_stats.xlat_insns += n;
            xlat_stats.mix_xrip_us += burst_xrip_us;
            xlat_stats.mix_xrip_insns += burst_xrip_insns;
            xlat_stats.xlat_hist[xlat_us_bucket(us)]++;
        } else if (n) {   /* include deadline-limited XRIP bursts */
            xlat_stats.xrip_bursts++;
            xlat_stats.xrip_us += us;
            xlat_stats.xrip_insns += n;
            xlat_stats.xrip_hist[xlat_us_bucket(us)]++;
        }
    }
#endif
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
void xrip_raise_irq(CPU *cpu)
{
#if XLAT && XLAT_EVENT_GATE
    __atomic_store_n(&cpu->intr, true, __ATOMIC_SEQ_CST);
    /* Publish the request before testing the current IF source. The state
     * is static SRAM, so a delayed producer can only cause an extra exit.
     * No guest register or compiler context is changed here. */
    if (__atomic_load_n(&xlat_state.intr, __ATOMIC_SEQ_CST) == (const volatile uint8_t *)&cpu->intr)
        xlat_irq_close(&xlat_state, timer_hw->timerawl);
#else
    cpu->intr = true;
#endif
#if XRIP_HW_IRQ_BRIDGE
    xrip_bridge_raise();
#endif
}
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
        .reset = xrip_reset_cpu, .step = xrip_step_cpu, .raise_irq = xrip_raise_irq,
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
#if XRIP_HW_IRQ_BRIDGE
    xrip_bridge_init();
#endif
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
    XLAT_NOTE_FOREIGN();    /* native hook may have executed nested guest code */
    fault_point = outer_fault_point;
    firstip = saved_ip;
    first_esp = saved_esp;
    cpu->next_ip = cpu->ip = XRIP_IP;
}

void cpu_save_regs(const CPU *cpu, CPU_regs *regs)
{
    flags_now();
    __builtin_memcpy(regs->gprx, cpu->gprx, sizeof(regs->gprx));
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
    __builtin_memcpy(cpu->gprx, regs->gprx, sizeof(cpu->gprx));
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
