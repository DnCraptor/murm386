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

static void reset_cpu(CPU *cpu)
{
    active_cpu = cpu;
    reset86();
}

static void __time_critical_func(step_cpu)(CPU *cpu, int count)
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

    void *context[5];
    void **const outer_fault_point = fault_point;
    fault_point = context;
    if (__builtin_setjmp(context))
        deliver_fault();
    else
        run_instructions((uint32_t)count);
    fault_point = outer_fault_point;
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
static uint16_t get_seg16(const CPU *cpu, u8 index)
{
    (void)cpu;
    return segregs[index];
}
static void set_seg16(CPU *cpu, u8 index, u16 selector)
{
    (void)cpu;
    load_segment(index, selector);
}
static uint32_t get_flags(CPU *cpu, u32 mask)
{
    flags_now();
    return cpu->flags.value & mask;
}
static void set_cpu_flags(CPU *cpu, uword set, uword clear)
{
    flags_now();
    cpu->flags.value = ((cpu->flags.value | set) & ~clear & cpu->flags_mask) | 2;
}
static void set_flag(CPU *cpu, u32 mask, bool value)
{
    set_cpu_flags(cpu, value ? mask : 0, value ? 0 : mask);
}
static void enable_cpu_fpu(CPU *cpu)
{
    if (cpu->fpu) fpu_delete(cpu->fpu);
    cpu->fpu = fpu_new();
}
static void raise_irq(CPU *cpu) { cpu->intr = true; }
static void set_exception(CPU *cpu, int exception, uword error)
{
    cpu->excno = exception;
    cpu->excerr = error;
    if (fault_point) {
        const bool pushes_error = exception == 8 || (exception >= 10 && exception <= 14);
        raise_fault((uint8_t)exception, pushes_error ? (int32_t)error : NO_ERROR_CODE);
    }
}
static void abort_cpu(CPU *cpu, int code)
{
    (void)cpu;
    panic("XRIP 386 abort %d", code);
}

CPU *cpu_new(int gen, CPU_CB **callbacks)
{
    static CPU_ext_accessors_t accessors = {
        .get_reg8 = get_reg8, .get_reg16 = get_reg16, .get_reg32 = get_reg32,
        .set_reg8 = set_reg8, .set_reg16 = set_reg16, .set_reg32 = set_reg32,
        .get_seg16 = get_seg16, .set_seg16 = set_seg16, .get_flags = get_flags,
        .set_flag = set_flag, .set_flags = set_cpu_flags, .enable_fpu = enable_cpu_fpu,
        .reset = reset_cpu, .step = step_cpu, .raise_irq = raise_irq,
        .setexc = set_exception, .abort = abort_cpu,
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
    reset_cpu(cpu);
    if (callbacks) *callbacks = &cpu->cb;
    return cpu;
}

void cpu_intcall(CPU *cpu, uint8_t vector)
{
    XRIP_IP = cpu->next_ip;
    void *context[5];
    void **const outer_fault_point = fault_point;
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
