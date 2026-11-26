/* Derived from xrip/pico-286, 572890a. See LICENSE.pico-286. */
#include "platform.h"
#if XLAT
#include "../config_save.h"
#endif

static CPU *active_cpu;
cpu_state_t cpu_state; // cpu.h
#define useseg cpu_state.useseg
#define ea cpu_state.ea
#define firstip cpu_state.firstip
#define first_esp cpu_state.first_esp
#define flags_result cpu_state.flags_result
#define flags_dst cpu_state.flags_dst
#define flags_src cpu_state.flags_src
#define flags_op cpu_state.flags_op
#define flags_cf cpu_state.flags_cf
#define disp16 cpu_state.disp16
#define mode cpu_state.mode
#define reg cpu_state.reg
#define rm cpu_state.rm
#define reptype cpu_state.reptype
#define segoverride cpu_state.segoverride
#define opsize32 cpu_state.opsize32
#define addrsize32 cpu_state.addrsize32
#define code32 cpu_state.code32
#define code32_word cpu_state.code32_word
#define stack32 cpu_state.stack32
#define retired cpu_state.retired
static uint8_t cpl; // current privilege level: 0 in real mode, 3 in V86 mode
// MOV SS, POP SS and STI let one more instruction run before an IRQ (so MOV SS; MOV SP is safe)
static bool halted;
static bool pending_trap;
static uint8_t protected_mode;
static uint16_t segregs[6];
#define segment_base cpu_state.segment_base

#if XLAT
#include "../fdos/xlat/xlat.h"
/* Taken branches to a translated loop head end the burst (xrip_step_cpu
 * enters the translated code). Armed only while that may succeed. */
static bool xlat_armed;
static bool xlat_cut;
#define XLAT_CUT_PENDING() xlat_cut
#else
#define XLAT_CUT_PENDING() false
#endif

/* Cache all four decoder defaults (REP=0, override=0, two copies of
 * code32) when CS's default size changes, not on every x86 instruction. */
static __time_critical_func() inline void set_code32(uint8_t value) {
    code32 = value;
    code32_word = ((uint32_t)value << 16) | ((uint32_t)value << 24);
}

static inline x86_flags_t *flags_now(void) {
    if (flags_op)
        resolve_flags();
    return &x86_flags;
}

static __time_critical_func() inline void load_segment(uint8_t index, uint16_t selector) {
    if (unlikely(protected_mode)) {
        load_segment_protected(index, selector);
        return;
    }
    segregs[index] = selector;
    segment_base[index] = (uint32_t)selector << 4;
}

#define portin(port) active_cpu->cb.io_read8(active_cpu->cb.io, (port))
#define portin16(port) active_cpu->cb.io_read16(active_cpu->cb.io, (port))
#if XLAT
#define XLAT_NOTE_FOREIGN() (xlat_dirty = true)   /* a device or native code may write guest RAM */
extern bool pc_xlat_safe_out(void *o, uint16_t port, bool word);
static __time_critical_func() inline void xlat_portout8(CPU *cpu, uint16_t port, uint8_t value)
{
    const bool foreign = !pc_xlat_safe_out(cpu->cb.io, port, false);
    if (foreign) XLAT_NOTE_FOREIGN();
    cpu->cb.io_write8(cpu->cb.io, port, value);
    if (foreign) XLAT_NOTE_FOREIGN();
}
static __time_critical_func() inline void xlat_portout16(CPU *cpu, uint16_t port, uint16_t value)
{
    const bool foreign = !pc_xlat_safe_out(cpu->cb.io, port, true);
    if (foreign) XLAT_NOTE_FOREIGN();
    cpu->cb.io_write16(cpu->cb.io, port, value);
    if (foreign) XLAT_NOTE_FOREIGN();
}
#define portout(port, value) xlat_portout8(active_cpu, (port), (value))
#define portout16(port, value) xlat_portout16(active_cpu, (port), (value))
#else
#define XLAT_NOTE_FOREIGN() ((void)0)
#define portout(port, value) active_cpu->cb.io_write8(active_cpu->cb.io, (port), (value))
#define portout16(port, value) active_cpu->cb.io_write16(active_cpu->cb.io, (port), (value))
#endif
// The memory accesses of the CPU, by linear address. Conventional RAM is reached inline; the memory
// from 1 MB directly too (cpu.h), from a small function; the rest goes through read86/write86.
// A word or dword is one load or store: the Cortex-M33 and the host take it at any address.
static __time_critical_func() inline bool in_fast_memory(uint32_t address, uint32_t last_byte) {
    return likely(address < fast_memory_size && address + last_byte < fast_memory_size);
}

// Wide accesses need only one inclusive upper-bound check. The subtraction checks
// the size before evaluating the address, so neither comparison can wrap.
static __time_critical_func() inline bool in_fast_memory_wide(uint32_t address, uint32_t width) {
    return likely(fast_memory_size >= width && address <= fast_memory_size - width);
}

static __time_critical_func() inline uint8_t *extended_memory_at(uint32_t address, uint32_t last_byte) {
    const uint32_t offset = address - HMA_START;
    return offset < extended_memory_size && offset + last_byte < extended_memory_size && a20_enabled
               ? extended_memory + offset
               : NULL;
}

/* Upper memory C0000-FFFFF is direct PSRAM outside the VGA aperture.
 * Do not bypass the EMS page frame in configurations which enable it.
 * The conventional RAM path gets no extra branch; paging clears
 * fast_memory_size and therefore disables this shortcut. */
static __time_critical_func() inline uint8_t *far_direct_at(uint32_t address, uint32_t last_byte) {
    uint8_t *direct = extended_memory_at(address, last_byte);
    if (direct) return direct;
#if defined(NO_PAGING)
    /* Unsigned range check also rejects addresses below C0000. */
    if (fast_memory_size && phys_mem_size >= 0x100000u &&
        (address - 0xC0000u) < (0x40000u - last_byte)) {
#if EMULATE_LTEMS
        if (EMS_WINDOW(address) || EMS_WINDOW(address + last_byte))
            return NULL;
#endif
        return fast_memory + address;
    }
#endif
    return NULL;
}

static __time_critical_func() __attribute__((noinline)) uint8_t mem_read8_far(uint32_t address) {
    const uint8_t *direct = far_direct_at(address, 0);
    return direct ? *direct : read86(address);
}

static __time_critical_func() __attribute__((noinline)) uint16_t mem_read16_far(uint32_t address) {
    const uint8_t *direct = far_direct_at(address, 1);
    if (!direct) return readw86(address);
    uint16_t value;
    memcpy(&value, direct, 2);
    return value;
}

static __time_critical_func() __attribute__((noinline)) uint32_t mem_read32_far(uint32_t address) {
    const uint8_t *direct = far_direct_at(address, 3);
    if (!direct) return readdw86(address);
    uint32_t value;
    memcpy(&value, direct, 4);
    return value;
}

#if XLAT
/* A write past fast memory: xlat_note_write() for translated code there
 * (upper memory, HMA: xlat_code_direct), or for a word that ends in fast
 * memory. */
#define XLAT_NOTE_FAR(address) \
    do { if ((address) < (XLAT_CODE_MAP_BYTES * 8u << XLAT_CODE_BLOCK_SHIFT)) xlat_note_write(address); } while (0)
#else
#define XLAT_NOTE_FAR(address) ((void)0)
#endif

static __time_critical_func() __attribute__((noinline)) void mem_write8_far(uint32_t address, uint8_t value) {
    uint8_t *direct = far_direct_at(address, 0);
    XLAT_NOTE_FAR(address);
    if (direct) *direct = value;
    else write86(address, value);
}

static __time_critical_func() __attribute__((noinline)) void mem_write16_far(uint32_t address, uint16_t value) {
    uint8_t *direct = far_direct_at(address, 1);
    XLAT_NOTE_FAR(address);
    if (direct) memcpy(direct, &value, 2);
    else writew86(address, value);
}

static __time_critical_func() __attribute__((noinline)) void mem_write32_far(uint32_t address, uint32_t value) {
    uint8_t *direct = far_direct_at(address, 3);
    XLAT_NOTE_FAR(address);
    if (direct) memcpy(direct, &value, 4);
    else writedw86(address, value);
}

static __time_critical_func() inline uint8_t mem_read8(uint32_t address) {
    return in_fast_memory(address, 0) ? fast_memory[address] : mem_read8_far(address);
}

static __time_critical_func() inline uint16_t mem_read16(uint32_t address) {
    if (!in_fast_memory_wide(address, 2)) return mem_read16_far(address);
    uint16_t value;
    memcpy(&value, fast_memory + address, 2);
    return value;
}

static __time_critical_func() inline uint32_t mem_read32(uint32_t address) {
    if (!in_fast_memory_wide(address, 4)) return mem_read32_far(address);
    uint32_t value;
    memcpy(&value, fast_memory + address, 4);
    return value;
}

#if XLAT
/* A write into translated code (xlat.h): the regions are compared again. */
#define XLAT_NOTE_WRITE(address) xlat_note_write(address)
#else
#define XLAT_NOTE_WRITE(address) ((void)0)
#endif

static __time_critical_func() inline void mem_write8(uint32_t address, uint8_t value) {
    if (in_fast_memory(address, 0)) { fast_memory[address] = value; XLAT_NOTE_WRITE(address); }
    else mem_write8_far(address, value);
}

static __time_critical_func() inline void mem_write16(uint32_t address, uint16_t value) {
    if (in_fast_memory_wide(address, 2)) { memcpy(fast_memory + address, &value, 2); XLAT_NOTE_WRITE(address); }
    else mem_write16_far(address, value);
}

static __time_critical_func() inline void mem_write32(uint32_t address, uint32_t value) {
    if (in_fast_memory_wide(address, 4)) { memcpy(fast_memory + address, &value, 4); XLAT_NOTE_WRITE(address); }
    else mem_write32_far(address, value);
}

#define CR0_PE 0x00000001u
#define CR0_ET 0x00000010u // a 387 is present
#define CR0_PG 0x80000000u
static uint32_t cr0 = CR0_ET;
uint16_t oldsp;


static const uint8_t __not_in_flash("cpu.regt") byteregtable[8] = {
    regal, regcl, regdl, regbl, regah, regch, regdh, regbh
};

uint8_t nestlev;
uint16_t saveip, savecs, oper1, oper2, res16, temp16, dummy, stacksize, frametemp;



static const bool __not_in_flash("cpu.pf") parity[0x100] = {
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

// 32-bit addressing (67h): an optional SIB byte, then an 8- or 32-bit displacement.
// EBP or ESP as the base selects SS when there is no segment override.
static uint8_t sib;
static uint32_t disp32;

static __time_critical_func() __attribute__((noinline)) void modregrm32(void) {
    if (mode == 3) return;
    uint8_t base = rm;
    if (rm == 4) {
        sib = getmem8(CS_BASE, XRIP_IP);
        StepIP(1);
        base = sib & 7;
    }
    if (mode == 2 || (mode == 0 && base == 5)) {
        disp32 = mem_read32(CS_BASE + XRIP_IP);
        StepIP(4);
    } else if (mode == 1) {
        disp32 = (uint32_t) (int8_t) getmem8(CS_BASE, XRIP_IP);
        StepIP(1);
    } else {
        disp32 = 0;
    }
    if (!segoverride && (base == 4 || (base == 5 && mode != 0))) {
        useseg = SS_BASE;
    }
}

static __time_critical_func() __attribute__((noinline)) void getea32(uint8_t rmval) {
    uint32_t offset = disp32;
    if (rmval == 4) {
        const uint8_t base = sib & 7, index = (sib >> 3) & 7, scale = sib >> 6;
        const uint32_t base_value = base != 5 || mode != 0 ? dwordregs[base] : 0;
        if (index != 4) offset += base_value + (dwordregs[index] << scale);
        else offset += base_value << scale; // no index: the 386 scales the base
    } else if (rmval != 5 || mode != 0) {
        offset += dwordregs[rmval];
    }
    ea = useseg + offset;
}

static __time_critical_func() void modregrm() {
    register uint8_t addrbyte = getmem8(CS_BASE, XRIP_IP);
    StepIP(1);
    mode = addrbyte >> 6;
    reg = (addrbyte >> 3) & 7;
    rm = addrbyte & 7;
    if (unlikely(addrsize32)) {
        modregrm32();
        if (mode != 3) getea32(rm);
        return;
    }
    switch (mode) {
        case 0:
            if (rm == 6) {
                disp16 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
            } else {
                disp16 = 0;
            }
            if (((rm == 2) || (rm == 3)) && !segoverride) {
                useseg = SS_BASE;
            }
            break;
        case 1:
            disp16 = signext(getmem8(CS_BASE, XRIP_IP));
            StepIP(1);
            if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
                useseg = SS_BASE;
            }
            break;
        case 2:
            disp16 = getmem16(CS_BASE, XRIP_IP);
            StepIP(2);
            if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
                useseg = SS_BASE;
            }
            break;
        default:
            disp16 = 0;
            return; /* mod=3: register operand, no EA */
    }

    /* For memory operands, fuse EA calculation into ModR/M decoding.
     * This avoids a separate getea() call for most 16-bit instructions. */
    uint32_t offset = disp16;
    switch (rm) {
        case 0: offset += XRIP_BX + XRIP_SI; break;
        case 1: offset += XRIP_BX + XRIP_DI; break;
        case 2: offset += XRIP_BP + XRIP_SI; break;
        case 3: offset += XRIP_BP + XRIP_DI; break;
        case 4: offset += XRIP_SI; break;
        case 5: offset += XRIP_DI; break;
        case 6: if (mode != 0) offset += XRIP_BP; break;
        case 7: offset += XRIP_BX; break;
    }
    ea = (offset & 0xFFFFu) + useseg;
}

/* EA is computed once in modregrm(). POP r/m is the only exception:
 * the 32-bit ESP base must reflect the increment performed by POP.
 * Match getea32(): XRIP scales the base when SIB index=4. */
static __time_critical_func() inline void fix_pop_ea(uint32_t old_esp) {
    if (mode < 3 && addrsize32 && rm == 4 && (sib & 7) == 4) {
        uint32_t delta = dwordregs[regsp] - old_esp;
        if (((sib >> 3) & 7) == 4)
            delta <<= sib >> 6;
        ea += delta;
    }
}

// The stack uses SP, or ESP when SS is a 32-bit (B) segment.
static __time_critical_func() INLINE uint32_t stack_top(void) {
    return unlikely(stack32) ? dwordregs[regsp] : XRIP_SP;
}

static __time_critical_func() INLINE void stack_add(int32_t delta) {
    if (unlikely(stack32)) dwordregs[regsp] += delta;
    else XRIP_SP += delta;
}

// LEAVE: the stack pointer goes back to the frame pointer (EBP or BP, by the stack size)
static __time_critical_func() INLINE void stack_to_frame(void) {
    if (unlikely(stack32)) dwordregs[regsp] = dwordregs[regbp];
    else XRIP_SP = XRIP_BP;
}

static __time_critical_func() INLINE void push(uint16_t pushval) {
    stack_add(-2);
    mem_write16(SS_BASE + stack_top(), pushval);
}

static __time_critical_func() INLINE uint16_t pop() {
    const uint16_t value = mem_read16(SS_BASE + stack_top());
    stack_add(2);
    return value;
}

#if XLAT
/* After a CALL (near, r/m, far): a target that became hot (xlat_call_hot)
 * ends the burst there once, marked as a function entry for xlat_run(); a
 * marked target (translated already) ends it as a taken branch does. */
static uint32_t xlat_fn_root = UINT32_MAX;
static __time_critical_func() INLINE void xlat_call_seen(void) {
    if (!xlat_armed)
        return;
    const uint32_t linear = segment_base[regcs] + xrip_ip;
    if (xlat_call_hot(linear)) {
        xlat_cut = true;
        xlat_fn_root = linear;
    } else if (xlat_map_test(linear)) {
        xlat_cut = true;
    }
}
#define XLAT_CALL_SEEN() xlat_call_seen()
/* After a near RET: a return site with a region (a trace built at a hot
 * region exit, xlat_run) ends the burst there, as a taken branch does. */
#define XLAT_RET_SEEN() do { \
        if (xlat_armed && xlat_map_test(segment_base[regcs] + xrip_ip)) xlat_cut = true; \
    } while (0)
#else
#define XLAT_CALL_SEEN() ((void)0)
#define XLAT_RET_SEEN() ((void)0)
#endif

// Near relative jump; with a 16-bit operand size EIP wraps at 64K.
static __time_critical_func() INLINE void jump_relative(int32_t displacement) {
    xrip_ip += displacement;
    if (!opsize32) xrip_ip &= 0xFFFF;
#if XLAT
    if (xlat_armed && xlat_map_test(segment_base[regcs] + xrip_ip))
        xlat_cut = true;
#endif
}

static __time_critical_func() INLINE uint16_t readrm16(uint8_t rmval) {
    if (mode < 3) {
        return mem_read16(ea);
    }
    return getreg16(rmval);
}

static __time_critical_func() INLINE uint8_t readrm8(uint8_t rmval) {
    if (mode < 3) {
        return mem_read8(ea);
    }
    return getreg8(rmval);
}

static __time_critical_func() INLINE void writerm16(uint8_t rmval, uint16_t value) {
    if (mode < 3) {
        mem_write16(ea, value);
    } else {
        putreg16(rmval, value);
    }
}

static __time_critical_func() INLINE void writerm8(uint8_t rmval, uint8_t value) {
    if (mode < 3) {
        mem_write8(ea, value);
    } else {
        putreg8(rmval, value);
    }
}

/* RMW: the memory address was resolved by readrm8/readrm16. */
/* Keep the writeback out of run_instructions (and MOV/POP unchanged). */
static __time_critical_func() __attribute__((noinline)) void writerm8_rmw(uint8_t rmval, uint8_t value) {
    if (mode < 3) mem_write8(ea, value);
    else putreg8(rmval, value);
}

static __time_critical_func() __attribute__((noinline)) void writerm16_rmw(uint8_t rmval, uint16_t value) {
    if (mode < 3) mem_write16(ea, value);
    else putreg16(rmval, value);
}

__time_critical_func() uint16_t makeflagsword(void) {
    return 2 | (flags_now()->value & 0x7FD5); // the 386 keeps IOPL and NT (bits 12-14) in real mode
}

// POPF, IRET, SAHF: bits outside mask keep their values. Only CPL 0 changes IOPL, and only
// CPL <= IOPL changes IF (in real mode CPL is 0).
static __time_critical_func() void set_flags(uint32_t value, uint32_t mask) {
    if (cpl) mask &= ~0x3000u;
    if (cpl > iopl) mask &= ~0x0200u;
    flags_now()->value = (flags_now()->value & ~mask) | (value & mask);
}

static __time_critical_func() INLINE void decodeflagsword(uint16_t x) {
    set_flags(x, 0x7FD5);
}

#define NO_ERROR_CODE (-1)
static void protected_interrupt(uint8_t vector, int32_t error_code, bool software);
// Far transfers (protected.c.inl): in protected mode the selector names a descriptor or a gate
static void far_jump(uint16_t selector, uint32_t offset);
static void far_call(uint16_t selector, uint32_t offset, bool size32);
static void far_return(bool size32, uint16_t release);
static void interrupt_return(bool size32);

static bool bios_escape(uint8_t id);
static void reset86(void);

static __time_critical_func() void real_interrupt(uint8_t vector);

static __time_critical_func() void intcall86(uint8_t vector) {
    if (unlikely(cr0 & CR0_PE))
        protected_interrupt(vector, NO_ERROR_CODE, true);
    else
        real_interrupt(vector);
}

static __time_critical_func() inline void flag_szp8(uint8_t value) {
    x86_flags_t *const f = flags_now();
    const uint32_t szp = (value == 0 ? 0x40u : 0u) |
                         ((uint32_t)value & 0x80u) |
                         ((uint32_t)parity[value] << 2);
    f->value = (f->value & ~0xC4u) | szp;
}

static __time_critical_func() inline void flag_szp16(uint16_t value) {
    x86_flags_t *const f = flags_now();
    const uint32_t szp = (value == 0 ? 0x40u : 0u) |
                         (((uint32_t)value >> 8) & 0x80u) |
                         ((uint32_t)parity[value & 255] << 2);
    f->value = (f->value & ~0xC4u) | szp;
}

// Lazy flags: ADD, SUB, CMP, AND, OR, XOR, TEST, INC and DEC keep CF (flags_cf), the operands and
// the result, shifted so the sign is bit 31 (8 bits: << 24, 16 bits: << 16). SUB keeps the source
// inverted: then OF has one formula, as for an ADD. Jcc, SETcc, LOOPcc and REPcc read the
// flags with get_cf() and the others, with no branch on the kind (the RP2350 has no branch
// predictor); resolve_flags() puts all of them in x86_flags. flags_op is the kind << 5 | the shift.
enum { FLAGS_ADD = 1, FLAGS_SUB, FLAGS_LOGIC };

static __time_critical_func() INLINE void lazy_flags(uint8_t kind, uint8_t bits, uint32_t dst, uint32_t src, uint32_t result) {
    const uint8_t shift = 32 - bits;
    flags_dst = dst << shift;
    flags_src = (kind == FLAGS_SUB ? ~src : src) << shift;
    flags_result = result << shift;
    flags_op = kind << 5 | shift;
}

static __time_critical_func() INLINE void add_flags(uint8_t bits, uint32_t dst, uint32_t src) {
    lazy_flags(FLAGS_ADD, bits, dst, src, dst + src);
    flags_cf = flags_result < flags_dst;
}

static __time_critical_func() INLINE void sub_flags(uint8_t bits, uint32_t dst, uint32_t src) {
    flags_cf = dst << (32 - bits) < src << (32 - bits);
    lazy_flags(FLAGS_SUB, bits, dst, src, dst - src);
}

static __time_critical_func() INLINE void logic_flags(uint8_t bits, uint32_t result) { // CF = OF = AF = 0
    lazy_flags(FLAGS_LOGIC, bits, result, result, result);
    flags_cf = 0;
}

static __time_critical_func() INLINE bool get_cf(void) { return likely(flags_op) ? flags_cf : x86_flags.bits.CF; }
static __time_critical_func() INLINE bool get_zf(void) { return likely(flags_op) ? flags_result == 0 : x86_flags.bits.ZF; }
static __time_critical_func() INLINE bool get_sf(void) { return likely(flags_op) ? flags_result >> 31 : x86_flags.bits.SF; }
static __time_critical_func() INLINE bool get_of(void) {
    return likely(flags_op) ? ((flags_result ^ flags_dst) & (flags_result ^ flags_src)) >> 31 : x86_flags.bits.OF;
}

__time_critical_func() void resolve_flags(void) {
    const uint8_t shift = flags_op & 31, kind = flags_op >> 5;
    uint32_t value = get_cf() | parity[(flags_result >> shift) & 0xFF] << 2 | get_zf() << 6 | get_sf() << 7 |
                     get_of() << 11;
    uint32_t carries = flags_dst ^ flags_src ^ flags_result; // bit 4: AF
    if (kind == FLAGS_SUB) carries = ~carries;
    if (kind != FLAGS_LOGIC) value |= (carries >> shift) & 0x10;
    x86_flags.value = (x86_flags.value & ~0x8D5u) | value;
    flags_op = 0;
}

// INC and DEC: like ADD and SUB 1, but CF stays
static __time_critical_func() INLINE uint32_t inc_dec(uint32_t value, bool decrement, uint8_t bits) {
    if (!flags_op) flags_cf = x86_flags.bits.CF;
    const uint32_t result = decrement ? value - 1 : value + 1;
    lazy_flags(decrement ? FLAGS_SUB : FLAGS_ADD, bits, value, 1, result);
    return result;
}

// Condition codes of Jcc and SETcc, by the low four bits of the opcode. Always inline: as a call it
// went to flash.
static __time_critical_func() inline __attribute__((always_inline)) bool condition(uint8_t code) {
    bool result;
    switch ((code & 0x0F) >> 1) {
        case 0: result = get_of(); break;                       // O
        case 1: result = get_cf(); break;                       // B
        case 2: result = get_zf(); break;                       // Z
        case 3: result = get_cf() || get_zf(); break;           // BE
        case 4: result = get_sf(); break;                       // S
        case 5: result = pf; break;                             // P
        case 6: result = get_sf() != get_of(); break;           // L
        default: result = get_zf() || get_sf() != get_of(); break; // LE
    }
    return result != (code & 1); // odd codes are the negations
}

static __time_critical_func() inline void flag_adc8(uint8_t v1, uint8_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint32_t dst = (uint32_t) v1 + (uint32_t) v2 + (uint32_t) v3;
    flag_szp8((uint8_t) dst);
    of = ((dst ^ (uint32_t)v1) & (dst ^ (uint32_t)v2) & 0x80) != 0;
    cf = (dst & 0xFF00) != 0;
    af = (((uint32_t)v1 ^ (uint32_t)v2 ^ dst) & 0x10) != 0;
}

static __time_critical_func() inline void flag_adc16(uint16_t v1, uint16_t v2, uint16_t v3) {
    register uint32_t dst = (uint32_t) v1 + (uint32_t) v2 + (uint32_t) v3;
    flag_szp16((uint16_t) dst);
    of = (((dst ^ (uint32_t)v1) & (dst ^ (uint32_t)v2)) & 0x8000) != 0;
    cf = (dst & 0xFFFF0000) != 0;
    af = (((uint32_t)v1 ^ (uint32_t)v2 ^ dst) & 0x10) != 0;
}

static __time_critical_func() inline uint8_t sbb8(uint8_t v1, uint8_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    register uint32_t dst = (uint32_t)v1 - (uint32_t)v2 - (uint32_t)v3;
    flag_szp8((uint8_t) dst);
    cf = ((dst >> 8) & 1) != 0;
    of = ((dst ^ v1) & (v1 ^ v2) & 0x80) != 0;
    af = ((v1 ^ v2 ^ dst ^ v3) & 0x10) != 0;
    return (uint8_t)dst;
}

static __time_critical_func() inline uint16_t sbb16(uint16_t v1, uint16_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    register uint32_t dst = (uint32_t)v1 - (uint32_t)v2 - (uint32_t)v3;
    flag_szp16((uint16_t) dst);
    cf = ((dst >> 16) & 1) != 0;
    of = ((dst ^ (uint32_t)v1) & (v1 ^ (uint32_t)v2) & 0x8000) != 0;
    af = ((v1 ^ v2 ^ dst ^ v3) & 0x10) != 0;
    return (uint16_t)dst;
}

#define op_adc8() { res8 = oper1b + oper2b + cf; flag_adc8(oper1b, oper2b, cf); }
#define op_adc16() { res16 = oper1 + oper2 + cf; flag_adc16(oper1, oper2, cf); }
#define op_add8() { res8 = oper1b + oper2b; add_flags(8, oper1b, oper2b); }
#define op_add16() { res16 = oper1 + oper2; add_flags(16, oper1, oper2); }
#define op_and8() { res8 = oper1b & oper2b; logic_flags(8, res8); }
#define op_and16() { res16 = oper1 & oper2; logic_flags(16, res16); }
#define op_or8() { res8 = oper1b | oper2b; logic_flags(8, res8); }
#define op_or16() { res16 = oper1 | oper2; logic_flags(16, res16); }
#define op_xor8() { res8 = oper1b ^ oper2b; logic_flags(8, res8); }
#define op_xor16() { res16 = oper1 ^ oper2; logic_flags(16, res16); }
#define op_sub8() { res8 = oper1b - oper2b; sub_flags(8, oper1b, oper2b); }
#define op_sub16() { res16 = oper1 - oper2; sub_flags(16, oper1, oper2); }
#define op_sbb8() { res8 = sbb8(oper1b, oper2b, cf); }
#define op_sbb16() { res16 = sbb16(oper1, oper2, cf); }

static __time_critical_func() uint8_t op_grp2_8(uint8_t cnt, uint8_t oper1b) {
    uint16_t s = oper1b;
    cnt &= 0x1F; // the 386 uses five bits of the count
    if (!cnt) return s;
    switch (reg) {
        case 0: { /* ROL r/m8 */
            const uint8_t rotate = cnt & 7;
            s = (uint8_t)((s << rotate) | (s >> ((8 - rotate) & 7)));
            cf = s & 1;

            of = cf ^ ((s >> 7) & 1);
            break;
        }

        case 1: { /* ROR r/m8 */
            const uint8_t rotate = cnt & 7;
            s = (uint8_t)((s >> rotate) | (s << ((8 - rotate) & 7)));
            cf = s >> 7;

            of = (s >> 7) ^ ((s >> 6) & 1);
            break;
        }

        case 2: { /* RCL r/m8: rotate the 9-bit value [operand:CF] */
            const uint32_t rotate = cnt % 9;
            const uint32_t ring = ((uint32_t)(uint8_t)s << 1) | cf;
            const uint32_t shifted = ((ring << rotate) | (ring >> (9 - rotate))) & 0x1FFu;
            s = shifted >> 1;
            cf = shifted & 1;
            of = cf ^ ((s >> 7) & 1);
            break;
        }

        case 3: { /* RCR r/m8: rotate the 9-bit value [CF:operand] */
            const uint32_t rotate = cnt % 9;
            const uint32_t ring = ((uint32_t)cf << 8) | (uint8_t)s;
            const uint32_t shifted = ((ring >> rotate) | (ring << (9 - rotate))) & 0x1FFu;
            s = shifted & 0xFFu;
            cf = shifted >> 8;
            of = (s >> 7) ^ ((s >> 6) & 1);
            break;
        }

        case 4:
        case 6: /* SHL r/m8 */
            cf = cnt <= 8 ? (s >> (8 - cnt)) & 1 : 0;
            s = ((uint32_t)s << cnt) & 0xFF;

            of = cf ^ (s >> 7);

            flag_szp8((uint8_t) s);
            break;

        case 5: /* SHR r/m8 */
            of = cnt == 1 && (s & 0x80);

            cf = cnt <= 8 ? (s >> (cnt - 1)) & 1 : 0;
            s >>= cnt;

            flag_szp8((uint8_t) s);
            break;

        case 7: /* SAR r/m8 */
            cf = (s >> ((cnt <= 8 ? cnt : 8) - 1)) & 1;
            s = (uint8_t)((int32_t)(int8_t)s >> cnt);

            of = 0;
            flag_szp8((uint8_t) s);
            break;
    }

    return s & 0xFF;
}

static __time_critical_func() uint16_t op_grp2_16(uint8_t cnt) {
    register uint32_t s = oper1;
    cnt &= 0x1F; // the 386 uses five bits of the count
    if (!cnt) return s;
    switch (reg) {
        case 0: { /* ROL r/m16 */
            const uint8_t rotate = cnt & 15;
            s = ((s << rotate) | (s >> ((16 - rotate) & 15))) & 0xFFFF;
            cf = s & 1;

            of = cf ^ ((s >> 15) & 1);
            break;
        }

        case 1: { /* ROR r/m16 */
            const uint8_t rotate = cnt & 15;
            s = ((s >> rotate) | (s << ((16 - rotate) & 15))) & 0xFFFF;
            cf = s >> 15;

            of = (s >> 15) ^ ((s >> 14) & 1);
            break;
        }

        case 2: { /* RCL r/m16: rotate the 17-bit value [operand:CF] */
            const uint32_t rotate = cnt >= 17 ? cnt - 17 : cnt;
            const uint32_t ring = ((uint32_t)(uint16_t)s << 1) | cf;
            const uint32_t shifted = ((ring << rotate) | (ring >> (17 - rotate))) & 0x1FFFFu;
            s = shifted >> 1;
            cf = shifted & 1;
            of = cf ^ ((s >> 15) & 1);
            break;
        }

        case 3: { /* RCR r/m16: rotate the 17-bit value [CF:operand] */
            const uint32_t rotate = cnt >= 17 ? cnt - 17 : cnt;
            const uint32_t ring = ((uint32_t)cf << 16) | (uint16_t)s;
            const uint32_t shifted = ((ring >> rotate) | (ring << (17 - rotate))) & 0x1FFFFu;
            s = shifted & 0xFFFFu;
            cf = shifted >> 16;
            of = (s >> 15) ^ ((s >> 14) & 1);
            break;
        }

        case 4:
        case 6: /* SHL r/m16 */
            cf = cnt <= 16 ? (s >> (16 - cnt)) & 1 : 0;
            s = (s << cnt) & 0xFFFF;

            of = cf ^ (s >> 15);

            flag_szp16((uint16_t) s);
            break;

        case 5: /* SHR r/m16 */
            of = cnt == 1 && (s & 0x8000);

            cf = cnt <= 16 ? (s >> (cnt - 1)) & 1 : 0;
            s >>= cnt;

            flag_szp16((uint16_t) s);
            break;

        case 7: /* SAR r/m16 */
            cf = (s >> ((cnt <= 16 ? cnt : 16) - 1)) & 1;
            s = (uint16_t)((int32_t)(int16_t)s >> cnt);

            of = 0;
            flag_szp16((uint16_t) s);
            break;
    }

    return (uint16_t) s & 0xFFFF;
}


// A protected-mode fault goes back to exec86() with longjmp; exec86() puts back EIP and ESP
// of the instruction and delivers the exception through the IDT. The GCC builtins need no
// library code (newlib's setjmp pulls in the C++ unwinder on the RP2350).
static void **fault_point;
static uint8_t fault_vector;
static int32_t fault_error;

static __time_critical_func() __attribute__((noreturn)) void raise_fault(uint8_t vector, int32_t error_code) {
    fault_vector = vector;
    fault_error = error_code;
    if (fault_point)
        __builtin_longjmp(fault_point, 1);
    panic("XRIP 386 exception outside interpreter");
}

// PUSHF, POPF (and INT n, IRET) in V86 mode need IOPL 3, else #GP(0)
static __time_critical_func() INLINE void check_v86_iopl(void) {
    if (unlikely(v86) && iopl < 3) raise_fault(13, 0);
}

// A fault returns to the instruction that caused it

static __time_critical_func() void cpu_fault(const uint8_t vector) {
    if (cr0 & CR0_PE) raise_fault(vector, NO_ERROR_CODE);
    xrip_ip = firstip;
    intcall86(vector);
}

static __time_critical_func() inline void op_div8(uint16_t valdiv, uint8_t divisor) {
    if (divisor == 0 || valdiv / divisor > 0xFF) {
        printf("[op_div8] %d / %d\n", valdiv, divisor);
        cpu_fault(0);
        return;
    }

    XRIP_AH = (uint8_t) (valdiv % divisor);
    XRIP_AL = (uint8_t) (valdiv / divisor);
}

static __time_critical_func() inline void op_idiv8(uint16_t valdiv, int8_t divisor) {
    if (divisor == 0) {
        printf("[op_idiv8] %d / 0\n", valdiv);
        cpu_fault(0);
        return;
    }
    int16_t dividend = (int16_t) valdiv;
    int16_t quotient  = dividend / divisor;
    int16_t remainder = dividend % divisor;
    if (quotient < -128 || quotient > 127) {
        printf("[op_idiv8] %d / %d overflow\n", dividend, divisor);
        cpu_fault(0);
        return;
    }
    XRIP_AL = (uint8_t)quotient;
    XRIP_AH = (uint8_t)remainder;
}

static __time_critical_func() inline void op_div16(uint32_t valdiv, uint16_t divisor) {
    if (divisor == 0 || valdiv / divisor > 0xFFFF) {
//        XRIP_DX = 0;
//        XRIP_AX = 0xFFFF;
//        printf("[op_div16] %d / %d\n", valdiv, divisor);
        cpu_fault(0);
        return;
    }

    XRIP_DX = (uint16_t) (valdiv % divisor);
    XRIP_AX = (uint16_t) (valdiv / divisor);
}

static __time_critical_func() inline void op_idiv16(uint32_t valdiv, uint16_t divisor) {
    int32_t dividend = (int32_t)valdiv;
    int16_t divisor_signed = (int16_t)divisor;
    if (divisor_signed == 0) {
        printf("[op_idiv16] %d / 0\n", dividend);
        cpu_fault(0);
        return;
    }
    int32_t quotient  = dividend / divisor_signed;
    int32_t remainder = dividend % divisor_signed;
    if (quotient < -32768 || quotient > 32767) {
        printf("[op_idiv16] %d / %d overflow\n", dividend, divisor_signed);
        cpu_fault(0);
        return;
    }
    XRIP_AX = (uint16_t)quotient;
    XRIP_DX = (uint16_t)remainder;
}


static __time_critical_func() void op_grp3_16() {
    switch (reg) {
        case 0:
        case 1: /* TEST */
            logic_flags(16, oper1 & getmem16(CS_BASE, XRIP_IP));
            StepIP(2);
            break;

        case 2: /* NOT */
            res16 = ~oper1;
            break;

        case 3: /* NEG */
            res16 = (~oper1) + 1;
            sub_flags(16, 0, oper1);
            if (res16) {
                cf = 1;
            } else {
                cf = 0;
            }
            break;

        case 4: {
            /* MUL */
            register uint32_t temp1 = (uint32_t) oper1 * (uint32_t) XRIP_AX;
            XRIP_AX = temp1 & 0xFFFF;
            XRIP_DX = temp1 >> 16;
            flag_szp16((uint16_t) temp1);
            if (XRIP_DX) {
                flags_now()->value |= FLAG_CF_OF_MASK;
            } else {
                flags_now()->value &= ~FLAG_CF_OF_MASK;
            }
#ifdef XRIP_CLEAR_ZF_ON_MUL
            zf = 0;
#endif
            break;
        }
        case 5: {
            /* IMUL */
            register int32_t temp1 = (int32_t)(int16_t)XRIP_AX * (int32_t)(int16_t)oper1;
			int16_t truncated = (int16_t)temp1;
            XRIP_AX = truncated; /* into register ax */
            XRIP_DX = (uint16_t)(temp1 >> 16); /* into register dx */
            if (temp1 != (int32_t)truncated) {
                flags_now()->value |= FLAG_CF_OF_MASK;
            } else {
                flags_now()->value &= ~FLAG_CF_OF_MASK;
            }
#ifdef XRIP_CLEAR_ZF_ON_MUL
            zf = 0;
#endif
            break;
        }
        case 6: /* DIV */
            op_div16(((uint32_t) XRIP_DX << 16) + XRIP_AX, oper1);
            break;

        case 7: /* DIV */
            op_idiv16(((uint32_t) XRIP_DX << 16) + XRIP_AX, oper1);
            break;
    }
}

static __time_critical_func() void op_grp5() {
    switch (reg) {
        case 0: /* INC Ev */
            writerm16(rm, inc_dec(oper1, false, 16));
            break;

        case 1: /* DEC Ev */
            writerm16(rm, inc_dec(oper1, true, 16));
            break;

        case 2: /* CALL Ev */
            push(xrip_ip);
            xrip_ip = oper1;
            XLAT_CALL_SEEN();
            break;

        case 3: /* CALL Mp */
            far_call(mem_read16(ea + 2), mem_read16(ea), false);
            XLAT_CALL_SEEN();
            break;

        case 4: /* JMP Ev */
            xrip_ip = oper1;
            break;

        case 5: /* JMP Mp */
            far_jump(mem_read16(ea + 2), mem_read16(ea));
            break;

        case 6: /* PUSH Ev */
            push(oper1);
            break;
    }
}

#include "cpu386.c.inl"

#include "protected.c.inl"

static __time_critical_func() void op_0f() {
    const uint8_t opcode = getmem8(CS_BASE, XRIP_IP);
    StepIP(1);
    if (!op_0f_system(opcode) && !op_0f_386(opcode)) cpu_fault(6);
}

static void xrip_fpu(uint8_t opcode) {
    XLAT_NOTE_FOREIGN();    /* fpu.c stores through cpu_store*, not mem_write* */
    if (cr0 & 0x0c)
        cpu_fault(7);
    else {
        modregrm();
        if (!active_cpu->fpu)
            return;
        if (mode == 3) {
            fpu_exec1(active_cpu->fpu, active_cpu, opcode - 0xd8, reg, rm);
        } else {
            fpu_exec2(active_cpu->fpu, active_cpu, !opsize32, opcode - 0xd8, reg, 0, ea);
        }
    }
}

// Prefix bytes: segment overrides, 66h, 67h, LOCK, REPNE, REP
static const bool __not_in_flash("cpu.prefix") is_prefix[256] = {
    [0x26] = 1, [0x2E] = 1, [0x36] = 1, [0x3E] = 1, [0x64] = 1, [0x65] = 1, [0x66] = 1, [0x67] = 1,
    [0xF0] = 1, [0xF2] = 1, [0xF3] = 1,
};

// The instruction loop. It has no setjmp of its own: a function with setjmp is compiled with
// fewer optimizations, so that is in exec86() around it.
#if defined(NO_PAGING)
/* Startup copies this section into RAM_4_EXT, below the core 0 stack. */
#define XRIP_INTERPRETER_CODE __attribute__((section(".ram_4_ext_code.xrip_interpreter")))
#else
/* Paging can use RAM_4_EXT for the guest page cache. Execute this large
 * loop from flash so it does not compete with the startup heap in RAM. */
#define XRIP_INTERPRETER_CODE
#endif
static XRIP_INTERPRETER_CODE __attribute__((noinline)) void run_instructions(uint32_t execloops) {
    /* step_cpu() fixes active_cpu for this entire interpreter burst.
     * Reuse that CPU pointer rather than reloading the global after
     * every opcode helper call. Limit the alias to this function. */
    CPU *const run_cpu = active_cpu;
#define active_cpu run_cpu
    for (uint32_t loopcount = 0; loopcount < execloops; loopcount++) {
        if (active_cpu->native_done || XLAT_CUT_PENDING())
            break;
        if (unlikely(XRIP_IP > 0xffffu) && !protected_mode) XRIP_IP &= 0xffffu;
        if (unlikely(active_cpu->intr && ifl && !active_cpu->i286_irq_shadow)) {
            uint32_t irq_state = save_and_disable_interrupts();
            active_cpu->intr = false;
            int vector = active_cpu->cb.pic_read_irq(active_cpu->cb.pic);
            restore_interrupts(irq_state);
            halted = false;
            firstip = XRIP_IP;
            first_esp = dwordregs[regsp];
            external_interrupt((uint8_t)vector);
        } else if (unlikely(halted)) {
            break;
        }
        active_cpu->i286_irq_shadow = false;
        ++retired;
        pending_trap = tf;
#if defined(__ARM_ARCH_8M_MAIN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        /* Aligned word access: reset REP, segment override and both operand
         * sizes with one SRAM store. No C call or unaligned transfer. */
        uint32_t decoder_defaults;
        __asm volatile(
            "ldr %0, [%1, #68]\n\t"
            "str %0, [%1, #56]"
            : "=&r"(decoder_defaults) : "r"(&cpu_state) : "memory");
#else
        reptype = 0;
        segoverride = 0;
        opsize32 = addrsize32 = code32;
#endif
        useseg = DS_BASE;
        firstip = XRIP_IP;
        first_esp = dwordregs[regsp];
        register uint8_t opcode = getmem8(CS_BASE, XRIP_IP);
        StepIP(1);

        while (unlikely(is_prefix[opcode])) { // one table load for an instruction without prefixes
            switch (opcode) {
                /* segment prefix check */
                case 0x2E: /* segment XRIP_CS */
                    useseg = CS_BASE;
                    segoverride = 1;
                    break;

                case 0x3E: /* segment XRIP_DS */
                    useseg = DS_BASE;
                    segoverride = 1;
                    break;

                case 0x26: /* segment XRIP_ES */
                    useseg = ES_BASE;
                    segoverride = 1;
                    break;

                case 0x36: /* segment XRIP_SS */
                    useseg = SS_BASE;
                    segoverride = 1;
                    break;

                case 0x64: /* segment FS */
                    useseg = FS_BASE;
                    segoverride = 1;
                    break;

                case 0x65: /* segment GS */
                    useseg = GS_BASE;
                    segoverride = 1;
                    break;

                case 0x66: /* operand size: the other one than the CS default */
                    opsize32 = !code32;
                    break;

                case 0x67: /* address size */
                    addrsize32 = !code32;
                    break;

                case 0xF0: /* LOCK (блокировка шины, для атомарных операций) */
                    /// TODO:
                    break;

                case 0xF2: /* REPNE/REPNZ */
                    reptype = 2;
                    break;

                /* repetition prefix check */
                case 0xF3: /* REP/REPE/REPZ */
                    reptype = 1;
                    break;
            }
            opcode = getmem8(CS_BASE, XRIP_IP);
            StepIP(1);
        }

        register uint8_t res8;
        register uint8_t oper1b;
        register uint8_t oper2b;
        if (likely(!(opsize32 | addrsize32)) || !exec386(opcode))
        switch (opcode) {
            case 0x0: /* 00 ADD Eb Gb */
                modregrm();
                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_add8();
                writerm8_rmw(rm, res8);
                break;

            case 0x1: /* 01 ADD Ev Gv */
                modregrm();
                {
                    register uint32_t oper1 = readrm16(rm);
                    register uint32_t oper2 = getreg16(reg);
                    op_add16();
                    writerm16_rmw(rm, res16);
                }
                break;

            case 0x2: /* 02 ADD Gb Eb */
                modregrm();
                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_add8();
                putreg8(reg, res8);
                break;

            case 0x3: {
                /* 03 ADD Gv Ev */
                modregrm();
                register uint32_t oper1 = getreg16(reg);
                register uint32_t oper2 = readrm16(rm);
                op_add16();
                putreg16(reg, res16);
                break;
            }
            case 0x4: /* 04 ADD XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_add8();
                XRIP_AL = res8;
                break;

            case 0x5: {
                /* 05 ADD eAX Iv */
                register uint32_t oper1 = XRIP_AX;
                register uint32_t oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_add16();
                XRIP_AX = res16;
                break;
            }
            case 0x6: /* 06 PUSH XRIP_ES */
                push(XRIP_ES);
                break;

            case 0x7: /* 07 POP XRIP_ES */
                load_segment(reges, pop());
                break;

            case 0x8: /* 08 OR Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_or8();
                writerm8_rmw(rm, res8
                );
                break;

            case 0x9: /* 09 OR Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_or16();
                writerm16_rmw(rm, res16
                );
                break;

            case 0xA: /* 0A OR Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_or8();
                putreg8(reg, res8
                );
                break;

            case 0xB: /* 0B OR Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_or16();
                /*                if ((oper1 == 0xF802) && (oper2 == 0xF802)) {
                                    sf = 0;    *//* cheap hack to make Wolf 3D think we're a 286 so it plays */ /*
                }*/

                putreg16(reg, res16);
                break;

            case 0xC: /* 0C OR XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_or8();
                XRIP_AL = res8;
                break;

            case 0xD: /* 0D OR eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_or16();
                XRIP_AX = res16;
                break;

            case 0xE: /* 0E PUSH XRIP_CS */
                push(XRIP_CS);
                break;

#ifdef XRIP_8086 //only the 8086/8088 does this.
            case 0xF: //0F POP CS
                load_segment(regcs, pop());
                break;
#else
            case 0x0F:
                op_0f();
                break;
#endif

            case 0x10: /* 10 ADC Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_adc8();
                writerm8_rmw(rm, res8);
                break;

            case 0x11: /* 11 ADC Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_adc16();
                writerm16_rmw(rm, res16);
                break;

            case 0x12: /* 12 ADC Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_adc8();
                putreg8(reg, res8);
                break;

            case 0x13: /* 13 ADC Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_adc16();
                putreg16(reg, res16
                );
                break;

            case 0x14: /* 14 ADC XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_adc8();
                XRIP_AL = res8;
                break;

            case 0x15: /* 15 ADC eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_adc16();
                XRIP_AX = res16;
                break;

            case 0x16: /* 16 PUSH XRIP_SS */
                push(XRIP_SS);
                break;

            case 0x17: /* 17 POP XRIP_SS */
                load_segment(regss, pop());
                active_cpu->i286_irq_shadow = true;
                pending_trap = false;
                break;

            case 0x18: /* 18 SBB Eb Gb */
                modregrm();
                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_sbb8();
                writerm8_rmw(rm, res8);
                break;

            case 0x19: /* 19 SBB Ev Gv */
                modregrm();
                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_sbb16();
                writerm16_rmw(rm, res16);
                break;

            case 0x1A: /* 1A SBB Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_sbb8();
                putreg8(reg, res8
                );
                break;

            case 0x1B: /* 1B SBB Gv Ev */
                modregrm();
                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_sbb16();
                putreg16(reg, res16);
                break;

            case 0x1C: /* 1C SBB XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_sbb8();
                XRIP_AL = res8;
                break;

            case 0x1D: /* 1D SBB eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_sbb16();
                XRIP_AX = res16;
                break;

            case 0x1E: /* 1E PUSH XRIP_DS */
                push(XRIP_DS);
                break;

            case 0x1F: /* 1F POP XRIP_DS */
                load_segment(regds, pop());
                break;

            case 0x20: /* 20 AND Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_and8();
                writerm8_rmw(rm, res8);
                break;

            case 0x21: /* 21 AND Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_and16();
                writerm16_rmw(rm, res16
                );
                break;

            case 0x22: /* 22 AND Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_and8();
                putreg8(reg, res8
                );
                break;

            case 0x23: /* 23 AND Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_and16();
                putreg16(reg, res16
                );
                break;

            case 0x24: /* 24 AND XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_and8();
                XRIP_AL = res8;
                break;

            case 0x25: /* 25 AND eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_and16();
                XRIP_AX = res16;
                break;

            case 0x27: { /* 27 DAA: the second step tests the old AL and CF */
                const uint8_t old_al = XRIP_AL;
                af = (XRIP_AL & 0x0F) > 9 || af;
                if (af) XRIP_AL += 6;
                cf = old_al > 0x99 || cf;
                if (cf) XRIP_AL += 0x60;
                flag_szp8(XRIP_AL);
                break;
            }

            case 0x28: /* 28 SUB Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_sub8();
                writerm8_rmw(rm, res8
                );
                break;

            case 0x29: {
                /* 29 SUB Ev Gv */
                modregrm();
                register uint32_t oper1 = readrm16(rm);
                register uint32_t oper2 = getreg16(reg);
                register uint32_t dst = oper1 - oper2;
                flag_szp16((uint16_t) dst);
                cf = (dst & 0xFFFF0000) != 0;
                of = ((dst ^ oper1) & (oper1 ^ oper2) & 0x8000) != 0;
                af = ((oper1 ^ oper2 ^ dst) & 0x10) != 0;
                writerm16_rmw(rm, (uint16_t) dst);
                break;
            }
            case 0x2A: /* 2A SUB Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_sub8();
                putreg8(reg, res8
                );
                break;

            case 0x2B: /* 2B SUB Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_sub16();
                putreg16(reg, res16
                );
                break;

            case 0x2C: /* 2C SUB XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_sub8();
                XRIP_AL = res8;
                break;

            case 0x2D: /* 2D SUB eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_sub16();
                XRIP_AX = res16;
                break;

            case 0x2F: { /* 2F DAS: the second step tests the old AL and CF */
                const uint8_t old_al = XRIP_AL;
                const uint8_t old_cf = cf;
                cf = 0;
                if ((XRIP_AL & 0x0F) > 9 || af) {
                    cf = old_cf || XRIP_AL < 6;
                    XRIP_AL -= 6;
                    af = 1;
                } else {
                    af = 0;
                }
                if (old_al > 0x99 || old_cf) {
                    XRIP_AL -= 0x60;
                    cf = 1;
                }
                flag_szp8(XRIP_AL);
                break;
            }

            case 0x30: /* 30 XOR Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_xor8();
                writerm8_rmw(rm, res8
                );
                break;

            case 0x31: /* 31 XOR Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_xor16();
                writerm16_rmw(rm, res16
                );
                break;

            case 0x32: /* 32 XOR Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_xor8();
                putreg8(reg, res8
                );
                break;

            case 0x33: /* 33 XOR Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_xor16();
                putreg16(reg, res16
                );
                break;

            case 0x34: /* 34 XOR XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                op_xor8();
                XRIP_AL = res8;
                break;

            case 0x35: /* 35 XOR eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                op_xor16();
                XRIP_AX = res16;
                break;

            case 0x37: /* 37 AAA ASCII */
                if (((XRIP_AL & 0xF) > 9) || (af == 1)) {
                    XRIP_AX = XRIP_AX + 0x106;
                    flags_now()->value |= FLAG_CF_AF_MASK;
                } else {
                    flags_now()->value &= ~FLAG_CF_AF_MASK;
                }

                XRIP_AL = XRIP_AL & 0xF;
                break;

            case 0x38: /* 38 CMP Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                sub_flags(8, oper1b, oper2b
                );
                break;

            case 0x39: /* 39 CMP Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                sub_flags(16, oper1, oper2
                );
                break;

            case 0x3A: /* 3A CMP Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                sub_flags(8, oper1b, oper2b
                );
                break;

            case 0x3B: /* 3B CMP Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                sub_flags(16, oper1, oper2
                );
                break;

            case 0x3C: /* 3C CMP XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                sub_flags(8, oper1b, oper2b
                );
                break;

            case 0x3D: /* 3D CMP eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                sub_flags(16, oper1, oper2
                );
                break;

            case 0x3F: /* 3F AAS ASCII */
                if (((XRIP_AL & 0xF) > 9) || (af == 1)) {
                    XRIP_AX = XRIP_AX - 6;
                    XRIP_AH = XRIP_AH - 1;
                    flags_now()->value |= FLAG_CF_AF_MASK;
                } else {
                    flags_now()->value &= ~FLAG_CF_AF_MASK;
                }

                XRIP_AL = XRIP_AL & 0xF;
                break;

            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: /* INC r16 */
                putreg16(opcode & 7, inc_dec(getreg16(opcode & 7), false, 16));
                break;

            case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: /* DEC r16 */
                putreg16(opcode & 7, inc_dec(getreg16(opcode & 7), true, 16));
                break;

            case 0x50: /* 50 PUSH eAX */
                push(XRIP_AX);
                break;

            case 0x51: /* 51 PUSH eCX */
                push(XRIP_CX);
                break;

            case 0x52: /* 52 PUSH eDX */
                push(XRIP_DX);
                break;

            case 0x53: /* 53 PUSH eBX */
                push(XRIP_BX);
                break;

            case 0x54: /* 54 PUSH eSP */
                push(XRIP_SP);
                break;

            case 0x55: /* 55 PUSH eBP */
                push(XRIP_BP);
                break;

            case 0x56: /* 56 PUSH eSI */
                push(XRIP_SI);
                break;

            case 0x57: /* 57 PUSH eDI */
                push(XRIP_DI);
                break;

            case 0x58: /* 58 POP eAX */
                XRIP_AX = pop();
                break;

            case 0x59: /* 59 POP eCX */
                XRIP_CX = pop();
                break;

            case 0x5A: /* 5A POP eDX */
                XRIP_DX = pop();
                break;

            case 0x5B: /* 5B POP eBX */
                XRIP_BX = pop();
                break;

            case 0x5C: /* 5C POP eSP */
                XRIP_SP = pop();
                break;

            case 0x5D: /* 5D POP eBP */
                XRIP_BP = pop();
                break;

            case 0x5E: /* 5E POP eSI */
                XRIP_SI = pop();
                break;

            case 0x5F: /* 5F POP eDI */
                XRIP_DI = pop();
                break;

#ifndef XRIP_8086
            case 0x60: /* 60 PUSHA (80186+) */
                oldsp = XRIP_SP;
                push(XRIP_AX);
                push(XRIP_CX);
                push(XRIP_DX);
                push(XRIP_BX);
                push(oldsp);
                push(XRIP_BP);
                push(XRIP_SI);
                push(XRIP_DI);
                break;

            case 0x61: /* 61 POPA (80186+) */
                XRIP_DI = pop();
                XRIP_SI = pop();
                XRIP_BP = pop();
                stack_add(2);
                XRIP_BX = pop();
                XRIP_DX = pop();
                XRIP_CX = pop();
                XRIP_AX = pop();
                break;

            case 0x62: { /* 62 BOUND Gv, Ma */
                modregrm();
                if (mode == 3) { // a register has no address
                    cpu_fault(6);
                    break;
                }
                const int16_t index = (int16_t) getreg16(reg);
                if (index < (int16_t) mem_read16(ea) || index > (int16_t) mem_read16(ea + 2)) {
                    cpu_fault(5); // returns to the BOUND instruction
                }
                break;
            }
            case 0x63: /* 63 ARPL Ew, Gw */
                adjust_rpl();
                break;

            case 0x68: /* 68 PUSH Iv (80186+) */
                push(getmem16(CS_BASE, XRIP_IP)
                );
                StepIP(2);
                break;

            case 0x69: {
                /* 69 IMUL Gv Ev Iv (80186+) */
                modregrm();
                register int32_t temp1 = (int32_t)(int16_t)readrm16(rm);
                register int32_t temp2 = (int32_t)(int16_t)getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                temp1 *= temp2;
                putreg16(reg, (int16_t)temp1);
                if (temp1 != (int32_t)(int16_t)temp1) {
                    flags_now()->value |= FLAG_CF_OF_MASK;
                } else {
                    flags_now()->value &= ~FLAG_CF_OF_MASK;
                }
                break;
            }
            case 0x6A: /* 6A PUSH Ib (80186+) */
                push((uint16_t) signext(getmem8(CS_BASE, XRIP_IP)));
                StepIP(1);
                break;

            case 0x6B: {
                /* 6B IMUL Gv Eb Ib (80186+) */
                modregrm();
                register int32_t temp1 = (int32_t)(int16_t)readrm16(rm);
                register int32_t temp2 = (int32_t)(int16_t)signext(getmem8(CS_BASE, XRIP_IP));
                StepIP(1);
                temp1 *= temp2;
				putreg16(reg, (int16_t)temp1);
                if (temp1 != (int32_t)(int16_t)temp1) {
                    flags_now()->value |= FLAG_CF_OF_MASK;
                } else {
                    flags_now()->value &= ~FLAG_CF_OF_MASK;
                }
                break;
            }
            case 0x6C: /* 6C INSB */
            case 0x6D: /* 6D INSW */
            case 0x6E: /* 6E OUTSB */
            case 0x6F: /* 6F OUTSW */
                string_step(opcode, opcode & 1 ? 2 : 1);
                break;
#endif

            // Jcc Jb: one case for each, so condition() gets a constant and needs no table jump
#define JCC_SHORT(op) case op: { \
                const int8_t displacement = (int8_t) getmem8(CS_BASE, XRIP_IP); \
                StepIP(1); \
                if (condition(op)) jump_relative(displacement); \
                break; \
            }
            JCC_SHORT(0x70) JCC_SHORT(0x71) JCC_SHORT(0x72) JCC_SHORT(0x73)
            JCC_SHORT(0x74) JCC_SHORT(0x75) JCC_SHORT(0x76) JCC_SHORT(0x77)
            JCC_SHORT(0x78) JCC_SHORT(0x79) JCC_SHORT(0x7A) JCC_SHORT(0x7B)
            JCC_SHORT(0x7C) JCC_SHORT(0x7D) JCC_SHORT(0x7E) JCC_SHORT(0x7F)

            case 0x80:
            case 0x82: /* 80/82 GRP1 Eb Ib */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                switch (reg) {
                    case 0:
                        op_add8();
                        break;
                    case 1:
                        op_or8();
                        break;
                    case 2:
                        op_adc8();
                        break;
                    case 3:
                        op_sbb8();
                        break;
                    case 4:
                        op_and8();
                        break;
                    case 5:
                        op_sub8();
                        break;
                    case 6:
                        op_xor8();
                        break;
                    case 7:
                        sub_flags(8, oper1b, oper2b
                        );
                        break;
                    default:
                        break; /* to avoid compiler warnings */
                }

                if (reg < 7) {
                    writerm8_rmw(rm, res8
                    );
                }
                break;

            case 0x81: /* 81 GRP1 Ev Iv */
            case 0x83: /* 83 GRP1 Ev Ib */
                modregrm();

                oper1 = readrm16(rm);
                if (opcode == 0x81) {
                    oper2 = getmem16(CS_BASE, XRIP_IP);
                    StepIP(2);
                } else {
                    oper2 = signext(getmem8(CS_BASE, XRIP_IP));
                    StepIP(1);
                }

                switch (reg) {
                    case 0:
                        op_add16();
                        break;
                    case 1:
                        op_or16();
                        break;
                    case 2:
                        op_adc16();
                        break;
                    case 3:
                        op_sbb16();
                        break;
                    case 4:
                        op_and16();
                        break;
                    case 5:
                        op_sub16();
                        break;
                    case 6:
                        op_xor16();
                        break;
                    case 7:
                        sub_flags(16, oper1, oper2
                        );
                        break;
                    default:
                        break; /* to avoid compiler warnings */
                }

                if (reg < 7) {
                    writerm16_rmw(rm, res16
                    );
                }
                break;

            case 0x84: /* 84 TEST Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                logic_flags(8, oper1b
                          & oper2b);
                break;

            case 0x85: /* 85 TEST Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                logic_flags(16, oper1
                           & oper2);
                break;

            case 0x86: /* 86 XCHG Gb Eb: the memory address may use the register, so write it last */
                modregrm();
                oper1b = readrm8(rm);
                writerm8_rmw(rm, getreg8(reg));
                putreg8(reg, oper1b);
                break;

            case 0x87: /* 87 XCHG Gv Ev: the memory address may use the register, so write it last */
                modregrm();
                oper1 = readrm16(rm);
                writerm16_rmw(rm, getreg16(reg));
                putreg16(reg, oper1);
                break;

            case 0x88: /* 88 MOV Eb Gb */
                modregrm();

                writerm8(rm, getreg8(reg)
                );
                break;

            case 0x89: /* 89 MOV Ev Gv */
                modregrm();

                writerm16(rm, getreg16(reg)
                );
                break;

            case 0x8A: /* 8A MOV Gb Eb */
                modregrm();

                putreg8(reg, readrm8(rm)
                );
                break;

            case 0x8B: /* 8B MOV Gv Ev */
                modregrm();

                putreg16(reg, readrm16(rm)
                );
                break;

            case 0x8C: /* 8C MOV Ew Sw */
                modregrm();
                if (reg > reggs) { // six segment registers
                    cpu_fault(6);
                    break;
                }
                writerm16(rm, getsegreg(reg));
                break;

            case 0x8D: /* 8D LEA Gv M */
                modregrm();
                if (mode == 3) { // a register has no address
                    cpu_fault(6);
                    break;
                }
                putreg16(reg, ea
                         -
                         useseg
                );
                break;

            case 0x8E: /* 8E MOV Sw Ew */
                modregrm();
                if (reg > reggs || reg == regcs) { // CS is loaded only by far jumps, calls and returns
                    cpu_fault(6);
                    break;
                }
                load_segment(reg, readrm16(rm));
                if (reg == regss) {
                    active_cpu->i286_irq_shadow = true;
                    pending_trap = false;
                }
                break;

            case 0x8F: /* 8F POP Ev */
                modregrm();
                if (reg) { // only /0 is defined
                    cpu_fault(6);
                    break;
                }

                /* With 32-bit addressing, POP [ESP+disp] uses ESP after
                 * the pop. Re-evaluate just this exceptional EA. */
                {
                    const uint32_t old_esp = dwordregs[regsp];
                    const uint16_t popped = pop();
                    fix_pop_ea(old_esp);
                    writerm16(rm, popped);
                }
                break;

            case 0x90: /* 90 NOP */
                break;

            case 0x91: /* 91 XCHG eCX eAX */
                oper1 = XRIP_CX;
                XRIP_CX = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x92: /* 92 XCHG eDX eAX */
                oper1 = XRIP_DX;
                XRIP_DX = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x93: /* 93 XCHG eBX eAX */
                oper1 = XRIP_BX;
                XRIP_BX = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x94: /* 94 XCHG eSP eAX */
                oper1 = XRIP_SP;
                XRIP_SP = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x95: /* 95 XCHG eBP eAX */
                oper1 = XRIP_BP;
                XRIP_BP = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x96: /* 96 XCHG eSI eAX */
                oper1 = XRIP_SI;
                XRIP_SI = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x97: /* 97 XCHG eDI eAX */
                oper1 = XRIP_DI;
                XRIP_DI = XRIP_AX;
                XRIP_AX = oper1;
                break;

            case 0x98: /* 98 CBW */
                if ((XRIP_AL & 0x80) == 0x80) {
                    XRIP_AH = 0xFF;
                } else {
                    XRIP_AH = 0;
                }
                break;

            case 0x99: /* 99 CWD */
                if ((XRIP_AH & 0x80) == 0x80) {
                    XRIP_DX = 0xFFFF;
                } else {
                    XRIP_DX = 0;
                }
                break;

            case 0x9A: /* 9A CALL Ap */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                far_call(oper2, oper1, false);
                XLAT_CALL_SEEN();
                break;

            case 0x9B: /* 9B WAIT */
                if ((cr0 & 0x0a) == 0x0a) cpu_fault(7);
                /// TODO:
                break;

            case 0x9C: /* 9C PUSHF */
                check_v86_iopl();
                push(makeflagsword());
                break;

            case 0x9D: /* 9D POPF */
                check_v86_iopl();
                decodeflagsword(pop()); // the 386 keeps IOPL and NT: this is how programs tell it from a 286
                break;

            case 0x9E: /* 9E SAHF */
                decodeflagsword((makeflagsword() & 0xFF00) | XRIP_AH);
                break;

            case 0x9F: /* 9F LAHF */
                XRIP_AH = makeflagsword() & 0xFF;
                break;

            case 0xA0: /* A0 MOV XRIP_AL Ob */
                XRIP_AL = getmem8(useseg, getmem16(CS_BASE, XRIP_IP));
                StepIP(2);
                break;

            case 0xA1: /* A1 MOV eAX Ov */
                oper1 = getmem16(useseg, getmem16(CS_BASE, XRIP_IP));
                StepIP(2);
                XRIP_AX = oper1;
                break;

            case 0xA2: /* A2 MOV Ob XRIP_AL */
                putmem8(useseg, getmem16(CS_BASE, XRIP_IP), XRIP_AL);
                StepIP(2);
                break;

            case 0xA3: /* A3 MOV Ov eAX */
                putmem16(useseg, getmem16(CS_BASE, XRIP_IP), XRIP_AX);
                StepIP(2);
                break;

            case 0xA4: /* A4 MOVSB */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                putmem8(ES_BASE, XRIP_DI, getmem8(useseg, XRIP_SI)
                );
                if (df) {
                    XRIP_SI = XRIP_SI - 1;
                    XRIP_DI = XRIP_DI - 1;
                } else {
                    XRIP_SI = XRIP_SI + 1;
                    XRIP_DI = XRIP_DI + 1;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xA5: /* A5 MOVSW */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                putmem16(ES_BASE, XRIP_DI, getmem16(useseg, XRIP_SI)
                );
                if (df) {
                    XRIP_SI = XRIP_SI - 2;
                    XRIP_DI = XRIP_DI - 2;
                } else {
                    XRIP_SI = XRIP_SI + 2;
                    XRIP_DI = XRIP_DI + 2;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xA6: /* A6 CMPSB */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                oper1b = getmem8(useseg, XRIP_SI);
                oper2b = getmem8(ES_BASE, XRIP_DI);
                if (df) {
                    XRIP_SI = XRIP_SI - 1;
                    XRIP_DI = XRIP_DI - 1;
                } else {
                    XRIP_SI = XRIP_SI + 1;
                    XRIP_DI = XRIP_DI + 1;
                }

                sub_flags(8, oper1b, oper2b
                );
                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                if ((reptype == 1) && !get_zf()) {
                    break;
                } else if ((reptype == 2) && (get_zf())) {
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xA7: /* A7 CMPSW */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                oper1 = getmem16(useseg, XRIP_SI);
                oper2 = getmem16(ES_BASE, XRIP_DI);
                if (df) {
                    XRIP_SI = XRIP_SI - 2;
                    XRIP_DI = XRIP_DI - 2;
                } else {
                    XRIP_SI = XRIP_SI + 2;
                    XRIP_DI = XRIP_DI + 2;
                }

                sub_flags(16, oper1, oper2
                );
                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                if ((reptype == 1) && !get_zf()) {
                    break;
                }

                if ((reptype == 2) && (get_zf())) {
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xA8: /* A8 TEST XRIP_AL Ib */
                oper1b = XRIP_AL;
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                logic_flags(8, oper1b
                          & oper2b);
                break;

            case 0xA9: /* A9 TEST eAX Iv */
                oper1 = XRIP_AX;
                oper2 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                logic_flags(16, oper1
                           & oper2);
                break;

            case 0xAA: /* AA STOSB */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                putmem8(ES_BASE, XRIP_DI, XRIP_AL
                );
                if (df) {
                    XRIP_DI = XRIP_DI - 1;
                } else {
                    XRIP_DI = XRIP_DI + 1;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xAB: /* AB STOSW */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                putmem16(ES_BASE, XRIP_DI, XRIP_AX
                );
                if (df) {
                    XRIP_DI = XRIP_DI - 2;
                } else {
                    XRIP_DI = XRIP_DI + 2;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xAC: /* AC LODSB */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                XRIP_AL = getmem8(useseg, XRIP_SI);
                if (df) {
                    XRIP_SI = XRIP_SI - 1;
                } else {
                    XRIP_SI = XRIP_SI + 1;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xAD: /* AD LODSW */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                oper1 = getmem16(useseg, XRIP_SI);
                XRIP_AX = oper1;
                if (df) {
                    XRIP_SI = XRIP_SI - 2;
                } else {
                    XRIP_SI = XRIP_SI + 2;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xAE: /* AE SCASB */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                oper1b = XRIP_AL;
                oper2b = getmem8(ES_BASE, XRIP_DI);
                sub_flags(8, oper1b, oper2b
                );
                if (df) {
                    XRIP_DI = XRIP_DI - 1;
                } else {
                    XRIP_DI = XRIP_DI + 1;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                if ((reptype == 1) && !get_zf()) {
                    break;
                } else if ((reptype == 2) && (get_zf())) {
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xAF: /* AF SCASW */
                if (
                    reptype && (XRIP_CX
                                == 0)) {
                    break;
                }

                oper1 = XRIP_AX;
                oper2 = getmem16(ES_BASE, XRIP_DI);
                sub_flags(16, oper1, oper2
                );
                if (df) {
                    XRIP_DI = XRIP_DI - 2;
                } else {
                    XRIP_DI = XRIP_DI + 2;
                }

                if (reptype) {
                    XRIP_CX = XRIP_CX - 1;
                }

                if ((reptype == 1) && !get_zf()) {
                    break;
                } else if ((reptype == 2) && (get_zf())) {
                    //did i fix a typo bug? this used to be & instead of &&
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                XRIP_IP = firstip;
                break;

            case 0xB0: /* B0 MOV XRIP_AL Ib */
                XRIP_AL = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB1: /* B1 MOV XRIP_CL Ib */
                XRIP_CL = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB2: /* B2 MOV XRIP_DL Ib */
                XRIP_DL = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB3: /* B3 MOV XRIP_BL Ib */
                XRIP_BL = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB4: /* B4 MOV XRIP_AH Ib */
                XRIP_AH = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB5: /* B5 MOV XRIP_CH Ib */
                XRIP_CH = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB6: /* B6 MOV XRIP_DH Ib */
                XRIP_DH = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB7: /* B7 MOV XRIP_BH Ib */
                XRIP_BH = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                break;

            case 0xB8: /* B8 MOV eAX Iv */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                XRIP_AX = oper1;
                break;

            case 0xB9: /* B9 MOV eCX Iv */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                XRIP_CX = oper1;
                break;

            case 0xBA: /* BA MOV eDX Iv */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                XRIP_DX = oper1;
                break;

            case 0xBB: /* BB MOV eBX Iv */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                XRIP_BX = oper1;
                break;

            case 0xBC: /* BC MOV eSP Iv */
                XRIP_SP = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                break;

            case 0xBD: /* BD MOV eBP Iv */
                XRIP_BP = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                break;

            case 0xBE: /* BE MOV eSI Iv */
                XRIP_SI = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                break;

            case 0xBF: /* BF MOV eDI Iv */
                XRIP_DI = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                break;

            case 0xC0: /* C0 GRP2 byte imm8 (80186+) */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                writerm8_rmw(rm, op_grp2_8(oper2b, oper1b));
                break;

            case 0xC1: /* C1 GRP2 word imm8 (80186+) */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                writerm16_rmw(rm, op_grp2_16((uint8_t) oper2)
                );
                break;

            case 0xC2: /* C2 RET Iw */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                XRIP_IP = pop();
                stack_add(oper1);
                XLAT_RET_SEEN();
                break;

            case 0xC3: /* C3 RET */
                XRIP_IP = pop();
                XLAT_RET_SEEN();
                break;

            case 0xC4: /* C4 LES Gv Mp */
                modregrm();
                if (mode == 3) { // a register has no address
                    cpu_fault(6);
                    break;
                }
                putreg16(reg, mem_read16(ea));
                load_segment(reges, mem_read16(ea + 2));
                break;

            case 0xC5: /* C5 LDS Gv Mp */
                modregrm();
                if (mode == 3) { // a register has no address
                    cpu_fault(6);
                    break;
                }
                putreg16(reg, mem_read16(ea));
                load_segment(regds, mem_read16(ea + 2));
                break;

            case 0xC6: /* C6 MOV Eb Ib */
                modregrm();
                if (reg) { // only /0 is defined
                    cpu_fault(6);
                    break;
                }

                writerm8(rm, getmem8(CS_BASE, XRIP_IP)
                );
                StepIP(1);
                break;

            case 0xC7: /* C7 MOV Ev Iv */
                modregrm();
                if (reg) { // only /0 is defined
                    cpu_fault(6);
                    break;
                }

                writerm16(rm, getmem16(CS_BASE, XRIP_IP)
                );
                StepIP(2);
                break;

            case 0xC8: /* C8 ENTER (80186+) */
                stacksize = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                nestlev = getmem8(CS_BASE, XRIP_IP) & 0x1F;
                StepIP(1);
                push(XRIP_BP);
                frametemp = XRIP_SP;
                if (nestlev) {
                    for (
                        temp16 = 1;
                        temp16 < nestlev;
                        ++temp16) {
                        XRIP_BP = XRIP_BP - 2;
                        push(getmem16(SS_BASE, XRIP_BP)); // outer frame pointer
                    }

                    push(frametemp); //XRIP_SP);
                }

                XRIP_BP = frametemp;
                XRIP_SP = XRIP_SP - stacksize;

                break;

            case 0xC9: /* C9 LEAVE (80186+) */
                stack_to_frame();
                XRIP_BP = pop();
                break;

            case 0xCA: /* CA RETF Iw */
                far_return(false, getmem16(CS_BASE, XRIP_IP));
                break;

            case 0xCB: /* CB RETF */
                far_return(false, 0);
                break;

            case 0xCC: /* CC INT 3 */
                intcall86(3);
                break;

            case 0xCD: /* CD INT Ib */
                oper1b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                intcall86(oper1b);
                break;

            case 0xCE: /* CE INTO */
                if (of) {
                    intcall86(4);
                }
                break;

            case 0xCF: /* CF IRET */
                interrupt_return(false);


                /*
                 * if (net.enabled) net.canrecv = 1;
                 */
                break;

            case 0xD0: /* D0 GRP2 Eb 1 */
                modregrm();

                oper1b = readrm8(rm);
                writerm8_rmw(rm, op_grp2_8(1, oper1b));
                break;

            case 0xD1: /* D1 GRP2 Ev 1 */
                modregrm();

                oper1 = readrm16(rm);
                writerm16_rmw(rm, op_grp2_16(1));
                break;

            case 0xD2: /* D2 GRP2 Eb XRIP_CL */
                modregrm();

                oper1b = readrm8(rm);
                writerm8_rmw(rm, op_grp2_8(XRIP_CL, oper1b));
                break;

            case 0xD3: /* D3 GRP2 Ev XRIP_CL */
                modregrm();

                oper1 = readrm16(rm);
                writerm16_rmw(rm, op_grp2_16(XRIP_CL)
                );
                break;

            case 0xD4: /* D4 AAM I0 */
                oper1 = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                if (!oper1) {
                    cpu_fault(0);
                    break;
                } /* division by zero */

                XRIP_AH = (XRIP_AL / oper1) & 255;
                XRIP_AL = (XRIP_AL % oper1) & 255;
                flag_szp8(XRIP_AL);
                break;

            case 0xD5: /* D5 AAD I0 */
                oper1 = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                XRIP_AL = (XRIP_AH * oper1 + XRIP_AL) & 255;
                XRIP_AH = 0;
                flag_szp8(XRIP_AL);
                break;

            case 0xD6: /* D6 XLAT on V20/V30, SALC on 8086/8088 */
#ifndef XRIP_NO_SALC
                XRIP_AL = XRIP_FL_CF ? 0xFF : 0x00;
                break;
#endif

            case 0xD7: /* D7 XLAT */
                XRIP_AL = mem_read8(useseg + (uint16_t) (XRIP_BX + XRIP_AL)); // wraps in the segment
                break;

            case 0xD8:
            case 0xD9:
            case 0xDA:
            case 0xDB:
            case 0xDC:
            case 0xDE:
            case 0xDD:
            case 0xDF: /* escape to x87 FPU */
                xrip_fpu(opcode);
                break;

            case 0xE0: /* E0 LOOPNZ Jb */
                temp16 = signext(getmem8(CS_BASE, XRIP_IP));
                StepIP(1);
                XRIP_CX = XRIP_CX - 1;
                if ((XRIP_CX) && !get_zf()) {
                    jump_relative((int16_t) temp16);
                }
                break;

            case 0xE1: /* E1 LOOPZ Jb */
                temp16 = signext(getmem8(CS_BASE, XRIP_IP));
                StepIP(1);
                XRIP_CX = XRIP_CX - 1;
                if (XRIP_CX && (get_zf())) {
                    jump_relative((int16_t) temp16);
                }
                break;

            case 0xE2: /* E2 LOOP Jb */
                temp16 = signext(getmem8(CS_BASE, XRIP_IP));
                StepIP(1);
                XRIP_CX = XRIP_CX - 1;
                if (XRIP_CX) {
                    jump_relative((int16_t) temp16);
                }
                break;

            case 0xE3: /* E3 JCXZ Jb */
                temp16 = signext(getmem8(CS_BASE, XRIP_IP));
                StepIP(1);
                if (!XRIP_CX) {
                    jump_relative((int16_t) temp16);
                }
                break;

            case 0xE4: /* E4 IN XRIP_AL Ib */
                oper1b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                XRIP_AL = port_read_sized(oper1b, 1);
                break;

            case 0xE5: /* E5 IN eAX Ib */
                oper1b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                XRIP_AX = port_read_sized(oper1b, 2);
                break;

            case 0xE6: /* E6 OUT Ib XRIP_AL */
                oper1b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                port_write_sized(oper1b, XRIP_AL, 1);
                break;

            case 0xE7: /* E7 OUT Ib eAX */
                oper1b = getmem8(CS_BASE, XRIP_IP);
                StepIP(1);
                port_write_sized(oper1b, XRIP_AX, 2);
                break;

            case 0xE8: /* E8 CALL Jv */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                push(XRIP_IP);
                jump_relative((int16_t) oper1);
                XLAT_CALL_SEEN();
                break;

            case 0xE9: /* E9 JMP Jv */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                jump_relative((int16_t) oper1);
                break;

            case 0xEA: /* EA JMP Ap */
                oper1 = getmem16(CS_BASE, XRIP_IP);
                StepIP(2);
                oper2 = getmem16(CS_BASE, XRIP_IP);
                far_jump(oper2, oper1);
                break;

            case 0xEB: /* EB JMP Jb */
                oper1 = signext(getmem8(CS_BASE, XRIP_IP));
                StepIP(1);
                jump_relative((int16_t) oper1);
                break;

            case 0xEC: /* EC IN XRIP_AL regdx */
                XRIP_AL = port_read_sized(XRIP_DX, 1);
                break;

            case 0xED: /* ED IN eAX regdx */
                XRIP_AX = port_read_sized(XRIP_DX, 2);
                break;

            case 0xEE: /* EE OUT regdx XRIP_AL */
                port_write_sized(XRIP_DX, XRIP_AL, 1);
                break;

            case 0xEF: /* EF OUT regdx eAX */
                port_write_sized(XRIP_DX, XRIP_AX, 2);
                break;

            case 0xF0: /* F0 LOCK */
                break;

            case 0xF4: /* F4 HLT */
                if (cpl) raise_fault(13, 0); // privileged
                halted = true;
                break;

            case 0xF5: /* F5 CMC */
                if (!cf) {
                    cf = 1;
                } else {
                    cf = 0;
                }
                break;

            case 0xF6: /* F6 GRP3a Eb */
                modregrm();
                oper1b = readrm8(rm);
                oper1 = signext(oper1b);
                switch (reg) {
                    case 0:
                    case 1: /* TEST */
                        logic_flags(8, oper1b & getmem8(CS_BASE, XRIP_IP));
                        StepIP(1);
                        break;

                    case 2: /* NOT */
                        res8 = ~oper1b;
                        break;

                    case 3: /* NEG */
                        res8 = (~oper1b) + 1;
                        sub_flags(8, 0, oper1b);
                        if (res8 == 0) {
                            cf = 0;
                        } else {
                            cf = 1;
                        }
                        break;

                    case 4: {
                        /* MUL */
                        register uint32_t temp1 = (uint32_t) oper1b * (uint32_t) XRIP_AL;
                        XRIP_AX = temp1 & 0xFFFF;
                        flag_szp8((uint8_t) temp1);
                        if (XRIP_AH) {
                            flags_now()->value |= FLAG_CF_OF_MASK;
                        } else {
                            flags_now()->value &= ~FLAG_CF_OF_MASK;
                        }
#ifdef XRIP_CLEAR_ZF_ON_MUL
                        zf = 0;
#endif
                        break;
                    }
                    case 5: {
                        /* IMUL */
                        oper1 = signext(oper1b);
                        register int32_t temp1 = (int32_t)(int8_t)signext(XRIP_AL);
                        register int32_t temp2 = (int32_t)(int8_t)oper1;
						temp1 *= temp2;
						int16_t result = (int16_t)temp1;
						int8_t truncated = (int8_t)result;
						if (result != (int16_t)truncated) {
							flags_now()->value |= FLAG_CF_OF_MASK; // CF=OF=1
						} else {
							flags_now()->value &= ~FLAG_CF_OF_MASK; // CF=OF=0
						}
						XRIP_AL = truncated;
						XRIP_AH = (uint8_t)(result >> 8);
#ifdef XRIP_CLEAR_ZF_ON_MUL
                        zf = 0;
#endif
                        break;
                    }
                    case 6: /* DIV */
                        op_div8(XRIP_AX, oper1b);
                        break;

                    case 7: /* IDIV */
                        op_idiv8(XRIP_AX, oper1b);
                        break;
                }

                if ((reg > 1) && (reg < 4)) {
                    writerm8(rm, res8
                    );
                }
                break;

            case 0xF7: /* F7 GRP3b Ev */
                modregrm();

                oper1 = readrm16(rm);
                op_grp3_16();
                if ((reg > 1) && (reg < 4)) {
                    writerm16(rm, res16
                    );
                }
                break;

            case 0xF8: /* F8 CLC */
                cf = 0;
                break;

            case 0xF9: /* F9 STC */
                cf = 1;
                break;

            case 0xFA: /* FA CLI */
                if (unlikely(cpl > iopl)) raise_fault(13, 0);
                ifl = 0;
                break;

            case 0xFB: /* FB STI */
                if (unlikely(cpl > iopl)) raise_fault(13, 0);
                if (!ifl)
                    active_cpu->i286_irq_shadow = true;
                ifl = 1;
                break;

            case 0xFC: /* FC CLD */
                df = 0;
                break;

            case 0xFD: /* FD STD */
                df = 1;
                break;

            case 0xFE: /* FE GRP4 Eb */
                modregrm();
                writerm8_rmw(rm, inc_dec(readrm8(rm), reg != 0, 8));
                break;

            case 0xFF: /* FF GRP5 Ev */
                modregrm();

                oper1 = readrm16(rm);
                op_grp5();
                break;

            default:
#ifdef XRIP_ALLOW_ILLEGAL_OP_EXCEPTION
                intcall86(6); /* trip invalid opcode exception. this occurs on the 80186+, 8086/8088 CPUs treat them as NOPs. */
                /* technically they aren't exactly like NOPs in most cases, but for our pursoses, that's accurate enough. */
                printf("[CPU] Invalid opcode 0x%02x exception at %04X:%04X\r\n", opcode, XRIP_CS, firstip);
#endif
                break;
        }
        if (pending_trap && tf) {
            active_cpu->i286_irq_shadow = false;
            external_interrupt(1);
        }
    }
#undef active_cpu
}

#include "murm_adapter.inl"
