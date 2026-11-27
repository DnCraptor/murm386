/* Direct-threaded execution from a host instruction pointer, following
 * FRANK DOS fastloop16.inc / fastloop.inc (fbd471e, MIT). XRIP supplies
 * the register, arithmetic-flag, IRQ, and scalar fallback semantics.
 */

static bool fastloop_enabled;

bool xrip_fastloop_active(void)
{
    return fastloop_enabled;
}

void xrip_fastloop_enable(void)
{
    fastloop_enabled = true;
}

#define FX_CODE __attribute__((section(".xrip_fastloop.text")))
#define FX_TABLE __attribute__((section(".xrip_fastloop.table")))
#define FX_INLINE inline __attribute__((always_inline))

static FX_INLINE uint32_t fx_load(const uint8_t *ptr, unsigned width)
{
    if (width == 1) return *ptr;
    if (width == 2) {
        uint16_t value;
        memcpy(&value, ptr, sizeof(value));
        return value;
    }
    uint32_t value;
    memcpy(&value, ptr, sizeof(value));
    return value;
}

static FX_INLINE void fx_store(uint8_t *ptr, unsigned width, uint32_t value)
{
    if (width == 1) {
        *ptr = value;
    } else if (width == 2) {
        const uint16_t word = value;
        memcpy(ptr, &word, sizeof(word));
    } else {
        memcpy(ptr, &value, sizeof(value));
    }
}

static FX_INLINE uint8_t *fx_register(gprx_t *registers, unsigned index, unsigned width)
{
    return (uint8_t *)registers + (width == 1 ? byteregtable[index] : index * 4);
}

static FX_INLINE uint8_t *fx_ram(uint32_t address, unsigned width)
{
    if (in_fast_memory_wide(address, width))
        return fast_memory + address;
    return extended_memory_at(address, width - 1);
}

/* Use XRIP's 16-bit effective address and 32-bit SIB rules. Return the
 * offset in the low word and the count of address bytes in the high word;
 * a register-only operand stays entirely in the threaded handler. */
static FX_CODE __attribute__((noinline))
uint64_t fx_address(gprx_t *registers, const uint8_t *encoded, unsigned modrm,
                    unsigned address_size, int segment, bool offset_only)
{
    const unsigned mod = modrm >> 6, slot = modrm & 7;
    unsigned default_segment = regds;
    uint32_t offset = 0;
    const uint8_t *p = encoded;

    if (address_size == 4) {
        unsigned base_reg = slot, index = regsp, scale = 0;
        if (slot == regsp) {
            const uint8_t sib_byte = *p++;
            base_reg = sib_byte & 7;
            index = (sib_byte >> 3) & 7;
            scale = sib_byte >> 6;
        }
        if (base_reg == regbp && mod == 0) {
            offset = fx_load(p, 4);
            p += 4;
        } else {
            const uint32_t base = registers[base_reg].r32;
            offset = index == regsp ? base << scale : base;
            if (base_reg == regsp || base_reg == regbp)
                default_segment = regss;
        }
        if (index != regsp) offset += registers[index].r32 << scale;
    } else if (slot == 6 && mod == 0) {
        offset = fx_load(p, 2);
        p += 2;
    } else {
        const uint16_t bx = registers[regbx].r16;
        const uint16_t bp = registers[regbp].r16;
        const uint16_t si = registers[regsi].r16;
        const uint16_t di = registers[regdi].r16;
        switch (slot) {
            case 0: offset = bx + si; break;
            case 1: offset = bx + di; break;
            case 2: offset = bp + si; default_segment = regss; break;
            case 3: offset = bp + di; default_segment = regss; break;
            case 4: offset = si; break;
            case 5: offset = di; break;
            case 6: offset = bp; default_segment = regss; break;
            default: offset = bx; break;
        }
    }
    if (mod == 1) {
        offset += (uint32_t)(int32_t)(int8_t)*p++;
    } else if (mod == 2) {
        offset += fx_load(p, address_size);
        p += address_size;
    }
    if (address_size == 2) offset = (uint16_t)offset;
    if (!offset_only)
        offset += segment_base[segment < 0 ? default_segment : (unsigned)segment];
    return (uint64_t)(p - encoded) << 32 | offset;
}

static FX_CODE __attribute__((noinline))
uint32_t fx_alu(unsigned op, unsigned width, uint32_t a, uint32_t b)
{
    const unsigned bits = width * 8;
    switch (op) {
        case 0: add_flags(bits, a, b); return a + b;
        case 1: logic_flags(bits, a | b); return a | b;
        case 4: logic_flags(bits, a & b); return a & b;
        case 5: case 7: sub_flags(bits, a, b); return a - b;
        case 6: logic_flags(bits, a ^ b); return a ^ b;
        case 2: {
            if (width == 4) return alu32(2, a, b);
            const bool carry = get_cf();
            if (width == 1) flag_adc8(a, b, carry);
            else flag_adc16(a, b, carry);
            return a + b + carry;
        }
        default: {
            if (width == 4) return alu32(3, a, b);
            const bool carry = get_cf();
            return width == 1 ? sbb8(a, b, carry) : sbb16(a, b, carry);
        }
    }
}

static FX_CODE __attribute__((noinline))
unsigned fastloop_run(CPU *const cpu, unsigned budget)
{
    /* Real-mode 16-bit code and flat protected-mode 32-bit code. Guest
     * paging, segment-changing instructions, traps and shadow go to XRIP. */
    if (!fast_memory_size || (cr0 & CR0_PG) || cpu->i286_irq_shadow ||
        cpu->flags.bits.TF)
        return 0;
    const unsigned default_width = code32 ? 4 : 2;
    if (code32) {
        if (!protected_mode || CS_BASE || SS_BASE || DS_BASE || !stack32)
            return 0;
    } else if ((cr0 & CR0_PE) || protected_mode || stack32) {
        return 0;
    }

    const uint32_t cs = CS_BASE;
    const uint32_t linear = cs + XRIP_IP;
    if (linear < cs)
        return 0;
    const uint8_t *source;
    uint32_t room;
    if (linear < fast_memory_size) {
        source = fast_memory + linear;
        room = fast_memory_size - linear;
    } else {
        source = extended_memory_at(linear, 0);
        if (!source) return 0;
        room = extended_memory_size - (linear - HMA_START);
    }

    /* Code pointers stay inside one directly mapped RAM interval. A
     * backward jump before this interval remaps at the next fast entry. */
    const uintptr_t host_base = (uintptr_t)source - XRIP_IP;
    const uint32_t region_start = linear < HMA_START ? 0 : HMA_START;
    const uintptr_t window_start = host_base + (region_start > cs ? region_start - cs : 0);
    if (!code32 && room > 0x10000u - XRIP_IP)
        room = 0x10000u - XRIP_IP;
    if (room < 15)
        return 0;
    const uintptr_t window_length = (uintptr_t)source + room - 14 - window_start;

#define ALU &&alu_rm, &&alu_rm, &&alu_rm, &&alu_rm, &&alu_acc, &&alu_acc
#define F &&finish
#define F4 F, F, F, F
#define F8 F4, F4
#define J4 &&jcc, &&jcc, &&jcc, &&jcc
#define IMM4 &&move_imm, &&move_imm, &&move_imm, &&move_imm
#define U4 &&incdec, &&incdec, &&incdec, &&incdec
#define PUSH4 &&push_reg, &&push_reg, &&push_reg, &&push_reg
#define POP4 &&pop_reg, &&pop_reg, &&pop_reg, &&pop_reg
#define SWAP4 &&swap_ax, &&swap_ax, &&swap_ax, &&swap_ax
    static const void *const targets[256] FX_TABLE = {
        /* 00 */ ALU, F, F, ALU, F, &&escape,
        /* 10 */ ALU, F, F, ALU, F, F,
        /* 20 */ ALU, &&prefix, F, ALU, &&prefix, F,
        /* 30 */ ALU, &&prefix, F, ALU, &&prefix, F,
        /* 40 */ U4, U4, U4, U4,
        /* 50 */ PUSH4, PUSH4, POP4, POP4,
        /* 60 */ F4, &&prefix, &&prefix, &&prefix, &&prefix,
                 &&push_imm, &&imul_imm, &&push_imm, &&imul_imm, F4,
        /* 70 */ J4, J4, J4, J4,
        /* 80 */ &&alu_group, &&alu_group, &&alu_group, &&alu_group,
                 &&test_rm, &&test_rm, &&swap_rm, &&swap_rm,
                 &&move_rm, &&move_rm, &&move_rm, &&move_rm,
                 &&move_seg, &&load_ea, F, F,
        /* 90 */ SWAP4, SWAP4, &&extend, &&extend, F, F, F, F, F, F,
        /* a0 */ &&moffs, &&moffs, &&moffs, &&moffs, F4, &&test_imm, &&test_imm, F, F, F4,
        /* b0 */ IMM4, IMM4, IMM4, IMM4,
        /* c0 */ &&shift, &&shift, &&return_near, &&return_near, F, F,
                 &&move_rm_imm, &&move_rm_imm, F, &&leave_frame, F, F, F4,
        /* d0 */ &&shift, &&shift, &&shift, &&shift, F, F, F, F, F8,
        /* e0 */ &&loop_count, &&loop_count, &&loop_count, &&loop_count, F4,
                 &&call_rel, &&jump_rel, F, &&jump_rel, F4,
        /* f0 */ F8, &&carry_flag, &&carry_flag, F, F, &&direction, &&direction,
                 &&incdec_rm, &&group5
    };
#undef SWAP4
#undef POP4
#undef PUSH4
#undef U4
#undef IMM4
#undef J4
#undef F8
#undef F4
#undef F
#undef ALU

    uintptr_t cursor = (uintptr_t)source;
    const uint8_t *p;
    uint8_t op;
    unsigned left = budget, word_size, address_size;
    int segment;
    gprx_t *const registers = cpu->gprx;
    const uint32_t stack_mask = stack32 ? UINT32_MAX : UINT16_MAX;
    const bool allow_irq = cpu->flags.bits.IF;
    const volatile CPU *const irq_cpu = cpu;

#define NEXT() do { cursor = (uintptr_t)p; --left; goto next; } while (0)
#define JUMP(target, width) do { \
    const uint32_t new_ip = (target); \
    cursor = host_base + ((width) == 2 ? (uint16_t)new_ip : new_ip); \
    --left; \
    goto next; \
} while (0)
#define WIDTH() ((op & 1) ? word_size : 1)
#define MODRM_PTR(ptr, width) do { \
    if (modrm >= 0xc0) { \
        (ptr) = fx_register(registers, modrm & 7, (width)); \
    } else { \
        const uint64_t address = fx_address(registers, p, modrm, address_size, segment, false); \
        p += address >> 32; \
        (ptr) = fx_ram((uint32_t)address, (width)); \
        if (!(ptr)) goto finish; \
    } \
} while (0)
#define PUSH(value) do { \
    const uint32_t old_sp = registers[regsp].r32; \
    const uint32_t next_sp = (old_sp - word_size) & stack_mask; \
    uint8_t *const dest = fx_ram(SS_BASE + next_sp, word_size); \
    if (!dest) goto finish; \
    fx_store(dest, word_size, (value)); \
    registers[regsp].r32 = (old_sp & ~stack_mask) | next_sp; \
} while (0)
#define POP(value, base_sp, adjust) do { \
    const uint32_t sp = (base_sp) & stack_mask; \
    const uint8_t *const src = fx_ram(SS_BASE + sp, word_size); \
    if (!src) goto finish; \
    (value) = fx_load(src, word_size); \
    registers[regsp].r32 = (registers[regsp].r32 & ~stack_mask) | \
                           ((sp + word_size + (adjust)) & stack_mask); \
} while (0)

next:
    if (!left || cursor - window_start >= window_length || (allow_irq && irq_cpu->intr))
        goto finish;
    p = (const uint8_t *)cursor;
    word_size = address_size = default_width;
    segment = -1;
    op = *p++;
    goto *targets[op];

prefix:
    if ((uintptr_t)p - cursor > 4) goto finish;
    switch (op) {
        case 0x26: segment = reges; break;
        case 0x2e: segment = regcs; break;
        case 0x36: segment = regss; break;
        case 0x3e: segment = regds; break;
        case 0x64: segment = regfs; break;
        case 0x65: segment = reggs; break;
        case 0x66: word_size = 6 - default_width; break;
        case 0x67: address_size = 6 - default_width; break;
    }
    op = *p++;
    goto *targets[op];

alu_rm: {
    const unsigned width = WIDTH();
    const unsigned modrm = *p++;
    uint8_t *other;
    MODRM_PTR(other, width);
    uint8_t *const arg = fx_register(registers, (modrm >> 3) & 7, width);
    uint8_t *const dst = (op & 2) ? arg : other;
    const uint8_t *const src = (op & 2) ? other : arg;
    const unsigned group = op >> 3;
    const uint32_t value = fx_alu(group, width, fx_load(dst, width), fx_load(src, width));
    if (group != 7) fx_store(dst, width, value);
    NEXT();
}
alu_acc: {
    const unsigned width = WIDTH();
    uint8_t *const dst = fx_register(registers, regax, width);
    const unsigned group = op >> 3;
    const uint32_t value = fx_alu(group, width, fx_load(dst, width), fx_load(p, width));
    p += width;
    if (group != 7) fx_store(dst, width, value);
    NEXT();
}
alu_group: {
    const unsigned width = (op & 1) ? word_size : 1;
    const unsigned modrm = *p++;
    uint8_t *dst;
    MODRM_PTR(dst, width);
    const unsigned group = (modrm >> 3) & 7;
    const uint32_t rhs = op == 0x83 ? (uint32_t)(int32_t)(int8_t)*p : fx_load(p, width);
    p += op == 0x83 ? 1 : width;
    const uint32_t value = fx_alu(group, width, fx_load(dst, width), rhs);
    if (group != 7) fx_store(dst, width, value);
    NEXT();
}
test_rm: {
    const unsigned width = WIDTH();
    const unsigned modrm = *p++;
    uint8_t *other;
    MODRM_PTR(other, width);
    const uint8_t *const arg = fx_register(registers, (modrm >> 3) & 7, width);
    logic_flags(width * 8, fx_load(arg, width) & fx_load(other, width));
    NEXT();
}
test_imm: {
    const unsigned width = WIDTH();
    logic_flags(width * 8, fx_load(fx_register(registers, regax, width), width) & fx_load(p, width));
    p += width;
    NEXT();
}
swap_rm: {
    const unsigned width = WIDTH();
    const unsigned modrm = *p++;
    uint8_t *other;
    MODRM_PTR(other, width);
    uint8_t *const arg = fx_register(registers, (modrm >> 3) & 7, width);
    const uint32_t value = fx_load(other, width);
    fx_store(other, width, fx_load(arg, width));
    fx_store(arg, width, value);
    NEXT();
}
move_rm: {
    const unsigned width = WIDTH();
    const unsigned modrm = *p++;
    uint8_t *other;
    MODRM_PTR(other, width);
    uint8_t *const arg = fx_register(registers, (modrm >> 3) & 7, width);
    if (op & 2) fx_store(arg, width, fx_load(other, width));
    else fx_store(other, width, fx_load(arg, width));
    NEXT();
}
move_imm: {
    const unsigned width = (op & 8) ? word_size : 1;
    fx_store(fx_register(registers, op & 7, width), width, fx_load(p, width));
    p += width;
    NEXT();
}
move_rm_imm: {
    const unsigned width = WIDTH();
    const unsigned modrm = *p++;
    if (modrm & 0x38) goto finish;
    uint8_t *dst;
    MODRM_PTR(dst, width);
    fx_store(dst, width, fx_load(p, width));
    p += width;
    NEXT();
}
move_seg: {
    const unsigned modrm = *p++;
    const unsigned seg = (modrm >> 3) & 7;
    if (seg > reggs) goto finish;
    const unsigned width = modrm >= 0xc0 ? word_size : 2;
    uint8_t *dst;
    MODRM_PTR(dst, width);
    fx_store(dst, width, segregs[seg]);
    NEXT();
}
load_ea: {
    const unsigned modrm = *p++;
    if (modrm >= 0xc0) goto finish;
    const uint64_t address = fx_address(registers, p, modrm, address_size, segment, true);
    p += address >> 32;
    fx_store(fx_register(registers, (modrm >> 3) & 7, word_size), word_size, (uint32_t)address);
    NEXT();
}
moffs: {
    const unsigned width = WIDTH();
    const uint32_t base = segment_base[segment < 0 ? regds : (unsigned)segment];
    uint8_t *const other = fx_ram(base + fx_load(p, address_size), width);
    if (!other) goto finish;
    p += address_size;
    uint8_t *const acc = fx_register(registers, regax, width);
    if (op & 2) fx_store(other, width, fx_load(acc, width));
    else fx_store(acc, width, fx_load(other, width));
    NEXT();
}
swap_ax: {
    if (op != 0x90) {
        uint8_t *const a = fx_register(registers, regax, word_size);
        uint8_t *const b = fx_register(registers, op & 7, word_size);
        const uint32_t value = fx_load(b, word_size);
        fx_store(b, word_size, fx_load(a, word_size));
        fx_store(a, word_size, value);
    }
    NEXT();
}
incdec: {
    uint8_t *const dst = fx_register(registers, op & 7, word_size);
    fx_store(dst, word_size, inc_dec(fx_load(dst, word_size), op & 8, word_size * 8));
    NEXT();
}
incdec_rm: {
    const unsigned width = op == 0xfe ? 1 : word_size;
    const unsigned modrm = *p++;
    const unsigned group = (modrm >> 3) & 7;
    if (group > 1) goto finish;
    uint8_t *dst;
    MODRM_PTR(dst, width);
    fx_store(dst, width, inc_dec(fx_load(dst, width), group != 0, width * 8));
    NEXT();
}
group5: {
    const unsigned group = (*p >> 3) & 7;
    if (group <= 1) goto incdec_rm;
    if (group != 2 && group != 4 && group != 6) goto finish;
    const unsigned modrm = *p++;
    uint8_t *src;
    MODRM_PTR(src, word_size);
    const uint32_t value = fx_load(src, word_size);
    if (group == 6) {
        PUSH(value);
        NEXT();
    }
    if (group == 2) PUSH((uint32_t)((uintptr_t)p - host_base));
    JUMP(value, word_size);
}
shift: {
    const unsigned width = WIDTH();
    const unsigned modrm = *p++;
    uint8_t *dst;
    MODRM_PTR(dst, width);
    const uint8_t count = op < 0xd0 ? *p++ : op & 2 ? registers[regcx].r8[0] : 1;
    reg = (modrm >> 3) & 7;
    uint32_t value = fx_load(dst, width);
    if (width == 4) {
        value = shift32(value, count);
    } else if (width == 2) {
        oper1 = value;
        value = op_grp2_16(count);
    } else {
        value = op_grp2_8(count, value);
    }
    fx_store(dst, width, value);
    NEXT();
}
imul_imm: {
    const unsigned modrm = *p++;
    uint8_t *src;
    MODRM_PTR(src, word_size);
    const uint32_t lhs = fx_load(src, word_size);
    const uint32_t rhs = op == 0x6b ? (uint32_t)(int32_t)(int8_t)*p : fx_load(p, word_size);
    p += op == 0x6b ? 1 : word_size;
    uint32_t value;
    if (word_size == 4) {
        value = imul32(lhs, rhs);
    } else {
        const int32_t product = (int32_t)(int16_t)lhs * (int16_t)rhs;
        x86_flags_t *const flags = flags_now();
        flags->value = (flags->value & ~FLAG_CF_OF_MASK) |
                       (product != (int16_t)product ? FLAG_CF_OF_MASK : 0);
        value = (uint32_t)product;
    }
    fx_store(fx_register(registers, (modrm >> 3) & 7, word_size), word_size, value);
    NEXT();
}
extend: {
    if (word_size == 4) {
        if (op == 0x98) registers[regax].r32 = (uint32_t)(int32_t)(int16_t)registers[regax].r16;
        else registers[regdx].r32 = (uint32_t)((int32_t)registers[regax].r32 >> 31);
    } else {
        if (op == 0x98) registers[regax].r16 = (int8_t)registers[regax].r8[0];
        else registers[regdx].r16 = (int16_t)registers[regax].r16 >> 15;
    }
    NEXT();
}
push_reg: {
    const uint32_t value = fx_load(fx_register(registers, op & 7, word_size), word_size);
    PUSH(value);
    NEXT();
}
push_imm: {
    const uint32_t value = op == 0x6a ? (uint32_t)(int32_t)(int8_t)*p : fx_load(p, word_size);
    p += op == 0x6a ? 1 : word_size;
    PUSH(value);
    NEXT();
}
pop_reg: {
    uint32_t value;
    POP(value, registers[regsp].r32, 0);
    fx_store(fx_register(registers, op & 7, word_size), word_size, value);
    NEXT();
}
leave_frame: {
    uint32_t value;
    POP(value, registers[regbp].r32, 0);
    fx_store(fx_register(registers, regbp, word_size), word_size, value);
    NEXT();
}
call_rel: {
    const uint32_t displacement = fx_load(p, word_size);
    p += word_size;
    const uint32_t return_ip = (uintptr_t)p - host_base;
    PUSH(return_ip);
    JUMP(return_ip + displacement, word_size);
}
return_near: {
    const unsigned adjust = op == 0xc2 ? fx_load(p, 2) : 0;
    uint32_t value;
    POP(value, registers[regsp].r32, adjust);
    JUMP(value, word_size);
}
jump_rel: {
    const uint32_t displacement = op == 0xeb ? (uint32_t)(int32_t)(int8_t)*p : fx_load(p, word_size);
    p += op == 0xeb ? 1 : word_size;
    JUMP((uint32_t)((uintptr_t)p - host_base) + displacement, word_size);
}
escape: {
    const unsigned secondary = *p++;
    if (secondary >= 0x80 && secondary <= 0x8f) {
        const uint32_t displacement = fx_load(p, word_size);
        p += word_size;
        if (condition(secondary & 15))
            JUMP((uint32_t)((uintptr_t)p - host_base) + displacement, word_size);
        NEXT();
    }
    if (secondary != 0xb6 && secondary != 0xb7 && secondary != 0xbe && secondary != 0xbf)
        goto finish;
    const unsigned width = (secondary & 1) + 1;
    const unsigned modrm = *p++;
    uint8_t *src;
    MODRM_PTR(src, width);
    uint32_t value = fx_load(src, width);
    if (secondary & 8)
        value = (uint32_t)(width == 1 ? (int32_t)(int8_t)value : (int32_t)(int16_t)value);
    fx_store(fx_register(registers, (modrm >> 3) & 7, word_size), word_size, value);
    NEXT();
}
jcc: {
    const int32_t displacement = (int8_t)*p++;
    if (condition(op & 15))
        JUMP((uint32_t)((uintptr_t)p - host_base) + displacement, word_size);
    NEXT();
}
loop_count: {
    const int32_t displacement = (int8_t)*p++;
    uint8_t *const counter = fx_register(registers, regcx, address_size);
    uint32_t value = fx_load(counter, address_size);
    bool taken;
    if (op == 0xe3) {
        taken = value == 0;
    } else {
        --value;
        if (address_size == 2) value = (uint16_t)value;
        fx_store(counter, address_size, value);
        taken = value && (op == 0xe2 || get_zf() == (op == 0xe1));
    }
    if (taken) JUMP((uint32_t)((uintptr_t)p - host_base) + displacement, word_size);
    NEXT();
}
carry_flag:
    cf = op & 1;
    NEXT();
direction:
    cpu->flags.bits.DF = op & 1;
    NEXT();

finish:
    XRIP_IP = (uint32_t)(cursor - host_base);
    retired += budget - left;
    return budget - left;

#undef POP
#undef PUSH
#undef MODRM_PTR
#undef WIDTH
#undef JUMP
#undef NEXT
}

#undef FX_INLINE
#undef FX_TABLE
#undef FX_CODE
