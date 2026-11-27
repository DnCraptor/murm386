/* Real-mode Thumb-2 block translation, following FRANK DOS jit=1
 * (rh1tech/frank-dos fbd471e, src/jit16.inc, Mikhail Matveev, MIT).
 * Blocks use the XRIP register and lazy-flags layout and fall back to its
 * interpreter before any operation they cannot complete.
 */

enum {
    JIT_INSNS = 16,
    JIT_X86_BYTES = JIT_INSNS * 15,
    JIT_ARM_HALFW = JIT_INSNS * 64 + 128,
    JIT_HOT = 32,
    JIT_SLOT_BITS = 9,
    JIT_SLOTS = 1 << JIT_SLOT_BITS,
    JIT_CODE_BYTES = 128 * 1024
};

typedef struct {
    uint32_t cs_base;
    uint32_t position;
    uint16_t ip;
    uint16_t code_size;
    uint8_t source_size;
    uint8_t count;
    uint8_t heat;
    bool valid;
} jit_entry_t;

typedef struct {
    uint32_t cursor;
    jit_entry_t entries[JIT_SLOTS];
    uint16_t work[JIT_ARM_HALFW];
    uint8_t source[JIT_X86_BYTES];
    uint32_t code[JIT_CODE_BYTES / sizeof(uint32_t)];
} jit_cache_t;

typedef struct {
    unsigned next;
    bool full;
} jit_emit_t;

typedef struct {
    const uint8_t *next;
    const uint8_t *limit;
    bool short_read;
} jit_decode_t;

typedef struct {
    uint32_t displacement;
    uint8_t base, index, scale, segment;
    bool wide;
} jit_address_t;

enum {
    JIT_EQ, JIT_NE, JIT_CS, JIT_CC, JIT_MI, JIT_PL, JIT_VS, JIT_VC,
    JIT_HI, JIT_LS, JIT_GE, JIT_LT, JIT_GT, JIT_LE
};

enum { JIT_NO_FLAGS, JIT_ADD_FLAGS, JIT_SUB_FLAGS, JIT_LOGIC_FLAGS,
       JIT_INC_FLAGS, JIT_DEC_FLAGS };

enum {
    JIT_ARG_CPU = 4, JIT_ARG_STATE = 5, JIT_ARG_RAM = 6, JIT_ARG_LIMIT = 7,
    JIT_ARG_START = 8, JIT_ARG_END = 9, JIT_EA = 12,
    JIT_REG_NONE = 8, JIT_SEG_NONE = 6
};

static jit_cache_t *jit_cache;
static uint8_t *jit_sram;
static size_t jit_sram_size;
static uint32_t jit_sram_position;
static bool jit_sram_valid;

void xrip_jit_use_sram(void *memory, size_t capacity)
{
    jit_sram = memory;
    jit_sram_size = capacity;
    jit_sram_valid = false;
}

bool xrip_jit_active(void)
{
    return jit_cache != NULL;
}

/* Called after the disk and MIDI reservations, before sizing guest RAM. */
size_t xrip_jit_cache_size(void)
{
    return (sizeof(jit_cache_t) + 1023u) & ~(size_t)1023u;
}

void xrip_jit_use_cache(void *memory)
{
    jit_cache = memory;
    jit_sram_valid = false;
    if (jit_cache) {
        jit_cache->cursor = 0;
        memset(jit_cache->entries, 0, sizeof(jit_cache->entries));
    }
}

static void jit_half(jit_emit_t *emit, uint16_t insn)
{
    if (emit->next < JIT_ARM_HALFW)
        jit_cache->work[emit->next++] = insn;
    else
        emit->full = true;
}

static void jit_word(jit_emit_t *emit, uint16_t first, uint16_t second)
{
    jit_half(emit, first);
    jit_half(emit, second);
}

static void jit_mov(jit_emit_t *emit, unsigned rd, unsigned reg_m)
{
    if (rd != reg_m)
        jit_half(emit, 0x4600 | ((rd & 8) << 4) | (reg_m << 3) | (rd & 7));
}

static void jit_mov_imm(jit_emit_t *emit, unsigned rd, uint32_t value)
{
    const uint16_t lo = value;
    jit_word(emit, 0xf240 | ((lo >> 1) & 0x400) | (lo >> 12),
             ((lo << 4) & 0x7000) | (rd << 8) | (lo & 0xff));
    if (value >> 16) {
        const uint16_t hi = value >> 16;
        jit_word(emit, 0xf2c0 | ((hi >> 1) & 0x400) | (hi >> 12),
                 ((hi << 4) & 0x7000) | (rd << 8) | (hi & 0xff));
    }
}

static void jit_shift(jit_emit_t *emit, unsigned rd, unsigned reg_m,
                      bool right, unsigned bits)
{
    if (bits)
        jit_word(emit, 0xea4f, (rd << 8) | ((bits & 28) << 10) |
                 ((bits & 3) << 6) | (right ? 0x10 : 0) | reg_m);
    else
        jit_mov(emit, rd, reg_m);
}

enum { JIT_AND = 0, JIT_ORR = 2, JIT_EOR = 4, JIT_ADD = 8, JIT_SUB = 13 };

static void jit_dp(jit_emit_t *emit, unsigned op, unsigned rd, unsigned rn,
                   unsigned reg_m, bool set_flags, unsigned shift)
{
    jit_word(emit, 0xea00 | (op << 5) | (set_flags ? 0x10 : 0) | rn,
             (rd << 8) | ((shift & 28) << 10) | ((shift & 3) << 6) | reg_m);
}

static void jit_compare(jit_emit_t *emit, unsigned rn, unsigned reg_m)
{
    jit_half(emit, 0x4500 | ((rn & 8) << 4) | (reg_m << 3) | (rn & 7));
}

/* Forwards to a local label; all such branches skip at most one emitted
 * operand, a helper call, or a bounds-check exit. */
static unsigned jit_branch(jit_emit_t *emit, unsigned cond)
{
    const unsigned site = emit->next;
    jit_half(emit, 0xd000 | (cond << 8));
    return site;
}

static void jit_bind(jit_emit_t *emit, unsigned site)
{
    const int delta = (int)emit->next - (int)site - 2;
    if (emit->full || delta < -128 || delta > 127) {
        emit->full = true;
        return;
    }
    jit_cache->work[site] |= (uint8_t)delta;
}

static void jit_ls(jit_emit_t *emit, bool load, unsigned width, unsigned rt,
                   unsigned rn, unsigned offset)
{
    if (rt < 8 && rn < 8 && offset < 32 * width && offset % width == 0) {
        const unsigned opcode = width == 1 ? 0x7000 : width == 2 ? 0x8000 : 0x6000;
        jit_half(emit, opcode | (load ? 0x800 : 0) | ((offset / width) << 6) |
                 (rn << 3) | rt);
    } else {
        const unsigned opcode = width == 1 ? 0xf880 : width == 2 ? 0xf8a0 : 0xf8c0;
        jit_word(emit, opcode | (load ? 0x10 : 0) | rn, (rt << 12) | offset);
    }
}

static void jit_gpr(jit_emit_t *emit, bool load, unsigned width, unsigned rt,
                    unsigned index)
{
    const unsigned offset = width == 1 ? byteregtable[index] : index * 4;
    jit_ls(emit, load, width, rt, JIT_ARG_CPU, offset);
}

static void jit_exit(jit_emit_t *emit, uint16_t ip, unsigned count)
{
    jit_mov_imm(emit, 0, ((uint32_t)count << 16) | ip);
    jit_word(emit, 0xe8bd, 0x87f0); /* POP.W {r4-r10,pc} */
}

/* All dynamic accesses are checked before any changes to guest state. A
 * write into the source bytes exits so the scalar decoder sees the new code. */
static void jit_memory_guard(jit_emit_t *emit, unsigned width, bool writing,
                             uint16_t ip, unsigned count)
{
    jit_compare(emit, JIT_EA, JIT_ARG_LIMIT);
    const unsigned in_ram = jit_branch(emit, JIT_CC);
    jit_exit(emit, ip, count);
    jit_bind(emit, in_ram);
    if (writing) {
        jit_compare(emit, JIT_EA, JIT_ARG_END);
        const unsigned above = jit_branch(emit, JIT_CS);
        jit_mov_imm(emit, 2, width);
        jit_dp(emit, JIT_ADD, 2, JIT_EA, 2, false, 0);
        jit_compare(emit, 2, JIT_ARG_START);
        const unsigned below = jit_branch(emit, JIT_LS);
        jit_exit(emit, ip, count);
        jit_bind(emit, above);
        jit_bind(emit, below);
    }
}

static void jit_ram(jit_emit_t *emit, bool load, unsigned width, unsigned rt)
{
    const unsigned opcode = width == 1 ? 0xf800 : width == 2 ? 0xf820 : 0xf840;
    jit_word(emit, opcode | (load ? 0x10 : 0) | JIT_ARG_RAM, (rt << 12) | JIT_EA);
}

static uint32_t jit_fetch(jit_decode_t *decode, unsigned width)
{
    if ((size_t)(decode->limit - decode->next) < width) {
        decode->short_read = true;
        return 0;
    }
    uint32_t value = 0;
    for (unsigned byte = 0; byte < width; ++byte)
        value |= (uint32_t)decode->next[byte] << (8 * byte);
    decode->next += width;
    return value;
}

static void jit_modrm(jit_decode_t *decode, uint8_t modrm, jit_address_t *address)
{
    const unsigned mod = modrm >> 6, rm_field = modrm & 7;
    unsigned segment = regds;
    if (address->wide) {
        address->base = rm_field;
        if (rm_field == 4) {
            const unsigned sib_byte = jit_fetch(decode, 1);
            address->base = sib_byte & 7;
            address->index = (sib_byte >> 3) & 7;
            address->scale = sib_byte >> 6;
            if (address->index == regsp)
                address->index = JIT_REG_NONE;
        }
        if (mod == 0 && address->base == regbp) {
            address->base = JIT_REG_NONE;
            address->displacement = jit_fetch(decode, 4);
        } else if (address->base == regsp || address->base == regbp) {
            segment = regss;
        }
    } else if (mod == 0 && rm_field == 6) {
        address->displacement = jit_fetch(decode, 2);
    } else {
        static const uint8_t bases[8] = {regbx, regbx, regbp, regbp, regsi, regdi, regbp, regbx};
        static const uint8_t indices[8] = {regsi, regdi, regsi, regdi,
                                          JIT_REG_NONE, JIT_REG_NONE, JIT_REG_NONE, JIT_REG_NONE};
        address->base = bases[rm_field];
        address->index = indices[rm_field];
        if (address->base == regbp)
            segment = regss;
    }
    if (mod == 1)
        address->displacement = (uint32_t)(int32_t)(int8_t)jit_fetch(decode, 1);
    else if (mod == 2)
        address->displacement = jit_fetch(decode, address->wide ? 4 : 2);
    if (address->segment == JIT_SEG_NONE)
        address->segment = segment;
}

static void jit_address(jit_emit_t *emit, const jit_address_t *address, bool lea)
{
    const unsigned width = address->wide ? 4 : 2;
    if (address->base == JIT_REG_NONE) {
        jit_mov_imm(emit, JIT_EA, address->wide ? address->displacement :
                                               (uint16_t)address->displacement);
    } else {
        jit_gpr(emit, true, width, JIT_EA, address->base);
        /* Match getea32() for a SIB with no index. */
        if (address->index == JIT_REG_NONE)
            jit_shift(emit, JIT_EA, JIT_EA, false, address->scale);
        if (address->displacement) {
            jit_mov_imm(emit, 2, address->wide ? address->displacement :
                                               (uint16_t)address->displacement);
            jit_dp(emit, JIT_ADD, JIT_EA, JIT_EA, 2, false, 0);
        }
    }
    if (address->index != JIT_REG_NONE) {
        jit_gpr(emit, true, width, 2, address->index);
        jit_dp(emit, JIT_ADD, JIT_EA, JIT_EA, 2, false, address->scale);
    }
    if (!address->wide)
        jit_word(emit, 0xfa1f, 0xfc8c); /* UXTH.W r12,r12 */
    if (!lea) {
        jit_ls(emit, true, 4, 2, JIT_ARG_STATE, JIT_STATE_SEGMENTS + address->segment * 4);
        jit_dp(emit, JIT_ADD, JIT_EA, JIT_EA, 2, false, 0);
    }
}

/* x86 ADD/SUB/XOR/AND/OR/CMP/TEST. Align narrow operands at bit 31 so
 * Thumb ADD/SUB produce the matching N/Z/C/V. Save XRIP's lazy operands
 * for exits and conditions Thumb cannot use directly (e.g. parity). */
static unsigned jit_alu(jit_emit_t *emit, unsigned op, unsigned width, bool preserve_cf)
{
    const unsigned shift = 32 - width * 8;
    const bool subtract = op == JIT_SUB;
    const bool arithmetic = op == JIT_ADD || subtract;
    const unsigned kind = arithmetic ? (subtract ? FLAGS_SUB : FLAGS_ADD) : FLAGS_LOGIC;
    if (arithmetic) {
        jit_shift(emit, 0, 0, false, shift);
        jit_ls(emit, false, 4, 0, JIT_ARG_STATE, JIT_STATE_FLAGS_DST);
        if (subtract) {
            jit_word(emit, 0xea6f, 0x0201); /* MVN.W r2,r1 */
            jit_shift(emit, 2, 2, false, shift);
            jit_shift(emit, 1, 1, false, shift);
            jit_ls(emit, false, 4, 2, JIT_ARG_STATE, JIT_STATE_FLAGS_SRC);
        } else {
            jit_shift(emit, 1, 1, false, shift);
            jit_ls(emit, false, 4, 1, JIT_ARG_STATE, JIT_STATE_FLAGS_SRC);
        }
        jit_dp(emit, op, 0, 0, 1, true, 0);
    } else {
        jit_dp(emit, op, 0, 0, 1, false, 0);
        if (shift)
            jit_shift(emit, 0, 0, false, shift);
        jit_half(emit, 0x2800); /* CMP r0,#0, for sign and zero */
        jit_ls(emit, false, 4, 0, JIT_ARG_STATE, JIT_STATE_FLAGS_DST);
        jit_ls(emit, false, 4, 0, JIT_ARG_STATE, JIT_STATE_FLAGS_SRC);
    }
    jit_ls(emit, false, 4, 0, JIT_ARG_STATE, JIT_STATE_FLAGS_RESULT);
    if (!preserve_cf) {
        jit_mov_imm(emit, 2, 0);
        if (arithmetic) {
            const unsigned arm_cc = subtract ? JIT_CC : JIT_CS;
            jit_half(emit, 0xbf08 | (arm_cc << 4)); /* IT CC/CS */
            jit_mov_imm(emit, 2, 1);
        }
        jit_ls(emit, false, 1, 2, JIT_ARG_STATE, JIT_STATE_FLAGS_CF);
    }
    jit_mov_imm(emit, 2, (kind << 5) | shift);
    jit_ls(emit, false, 1, 2, JIT_ARG_STATE, JIT_STATE_FLAGS_OP);
    jit_shift(emit, 0, 0, true, shift);
    if (preserve_cf)
        return subtract ? JIT_DEC_FLAGS : JIT_INC_FLAGS;
    return arithmetic ? (subtract ? JIT_SUB_FLAGS : JIT_ADD_FLAGS) : JIT_LOGIC_FLAGS;
}

static unsigned jit_x86_alu(unsigned group)
{
    static const uint8_t ops[8] = { JIT_ADD, JIT_ORR, 0xff, 0xff, JIT_AND, JIT_SUB, JIT_EOR, JIT_SUB };
    return ops[group];
}

/* C helpers are only needed for flags not available in APSR. */
static __attribute__((noinline)) bool jit_condition(unsigned code)
{
    return condition(code);
}

static unsigned jit_branch_condition(jit_emit_t *emit, unsigned cc, unsigned flag_source)
{
    static const int8_t add[8] = {JIT_VS, JIT_CS, JIT_EQ, -1, JIT_MI, -1, JIT_LT, JIT_LE};
    static const int8_t sub[8] = {JIT_VS, JIT_CC, JIT_EQ, JIT_LS, JIT_MI, -1, JIT_LT, JIT_LE};
    static const int8_t logic[8] = {-1, -1, JIT_EQ, JIT_EQ, JIT_MI, -1, JIT_MI, -1};
    const unsigned group = cc >> 1;
    int arm_cc = -1;
    if (flag_source == JIT_ADD_FLAGS || flag_source == JIT_INC_FLAGS)
        arm_cc = add[group];
    else if (flag_source == JIT_SUB_FLAGS || flag_source == JIT_DEC_FLAGS)
        arm_cc = sub[group];
    else if (flag_source == JIT_LOGIC_FLAGS)
        arm_cc = logic[group];
    if (arm_cc >= 0 && !((flag_source == JIT_INC_FLAGS || flag_source == JIT_DEC_FLAGS) &&
                         (group == 1 || group == 3)))
        return (unsigned)arm_cc ^ (cc & 1);
    jit_mov_imm(emit, 0, cc);
    jit_mov_imm(emit, 3, (uintptr_t)jit_condition);
    jit_half(emit, 0x4798); /* BLX r3 */
    jit_half(emit, 0x2800); /* CMP r0,#0 */
    return JIT_NE;
}

static void jit_stack_address(jit_emit_t *emit, unsigned adjustment)
{
    jit_gpr(emit, true, 2, 1, regsp);
    if (adjustment) {
        jit_mov_imm(emit, 2, adjustment);
        jit_dp(emit, JIT_ADD, 1, 1, 2, false, 0);
        jit_half(emit, 0xb289); /* UXTH r1,r1 */
    }
    jit_ls(emit, true, 4, 2, JIT_ARG_STATE, JIT_STATE_SEGMENTS + regss * 4);
    jit_dp(emit, JIT_ADD, JIT_EA, 2, 1, false, 0);
}

static void jit_push(jit_emit_t *emit, unsigned width, uint16_t ip, unsigned count)
{
    jit_stack_address(emit, (uint16_t)-width);
    jit_memory_guard(emit, width, true, ip, count);
    jit_ram(emit, false, width, 0);
    jit_gpr(emit, false, 2, 1, regsp);
}

static void jit_pop(jit_emit_t *emit, unsigned width, unsigned increment,
                    uint16_t ip, unsigned count)
{
    jit_stack_address(emit, 0);
    jit_memory_guard(emit, width, false, ip, count);
    jit_ram(emit, true, width, 0);
    jit_mov_imm(emit, 2, (uint16_t)(width + increment));
    jit_dp(emit, JIT_ADD, 1, 1, 2, false, 0);
    jit_gpr(emit, false, 2, 1, regsp);
}

/* Return false for instructions whose full semantics belong to XRIP. */
static bool jit_instruction(jit_emit_t *emit, jit_decode_t *decode, uint16_t ip,
                            unsigned count, unsigned *flag_source, bool *ends)
{
    const uint8_t *start = decode->next;
    unsigned width = 2;
    jit_address_t address = {
        .base = JIT_REG_NONE, .index = JIT_REG_NONE, .segment = JIT_SEG_NONE
    };
    unsigned opcode;
    while (true) {
        opcode = jit_fetch(decode, 1);
        if (opcode == 0x66) width = 4;
        else if (opcode == 0x67) address.wide = true;
        else if (opcode == 0x26) address.segment = reges;
        else if (opcode == 0x2e) address.segment = regcs;
        else if (opcode == 0x36) address.segment = regss;
        else if (opcode == 0x3e) address.segment = regds;
        else if (opcode == 0x64) address.segment = regfs;
        else if (opcode == 0x65) address.segment = reggs;
        else break;
    }
    const unsigned incoming_flags = *flag_source;
    *flag_source = JIT_NO_FLAGS;
    unsigned cc = 16;
    if (opcode == 0x0f) {
        const unsigned second = jit_fetch(decode, 1);
        if (second < 0x80 || second > 0x8f || width != 2) return false;
        cc = second & 15;
    } else if (opcode >= 0x70 && opcode <= 0x7f) {
        if (width != 2) return false;
        cc = opcode & 15;
    }
    if (cc < 16) {
        const int displacement = opcode == 0x0f ? (int16_t)jit_fetch(decode, 2) :
                                                  (int8_t)jit_fetch(decode, 1);
        const uint16_t next_ip = ip + (decode->next - start);
        const unsigned cond = jit_branch_condition(emit, cc, incoming_flags);
        const unsigned not_taken = jit_branch(emit, cond ^ 1);
        jit_exit(emit, next_ip + displacement, count + 1);
        jit_bind(emit, not_taken);
        jit_exit(emit, next_ip, count + 1);
        *ends = true;
        return true;
    }
    if (opcode >= 0xe0 && opcode <= 0xe3) {
        if (width != 2) return false;
        const int displacement = (int8_t)jit_fetch(decode, 1);
        const uint16_t next_ip = ip + (decode->next - start);
        const unsigned counter_width = address.wide ? 4 : 2;
        jit_gpr(emit, true, counter_width, 0, regcx);
        if (opcode != 0xe3) {
            jit_half(emit, 0x3801); /* SUBS r0,#1 */
            if (counter_width == 2) jit_half(emit, 0xb280); /* UXTH r0,r0 */
            jit_gpr(emit, false, counter_width, 0, regcx);
        }
        jit_half(emit, 0x2800);
        const unsigned counter_exit = jit_branch(emit, opcode == 0xe3 ? JIT_NE : JIT_EQ);
        unsigned flag_exit = 0;
        if (opcode < 0xe2) {
            const unsigned cond = jit_branch_condition(emit, opcode == 0xe0 ? 5 : 4, JIT_NO_FLAGS);
            flag_exit = jit_branch(emit, cond ^ 1);
        }
        jit_exit(emit, next_ip + displacement, count + 1);
        jit_bind(emit, counter_exit);
        if (opcode < 0xe2) jit_bind(emit, flag_exit);
        jit_exit(emit, next_ip, count + 1);
        *ends = true;
        return true;
    }
    if (opcode == 0xe8 || opcode == 0xe9 || opcode == 0xeb) {
        if (width != 2) return false;
        const int displacement = opcode == 0xeb ? (int8_t)jit_fetch(decode, 1) :
                                                  (int16_t)jit_fetch(decode, 2);
        const uint16_t next_ip = ip + (decode->next - start);
        if (opcode == 0xe8) {
            jit_mov_imm(emit, 0, next_ip);
            jit_push(emit, 2, ip, count);
        }
        jit_exit(emit, next_ip + displacement, count + 1);
        *ends = true;
        return true;
    }
    if (opcode == 0xc2 || opcode == 0xc3) {
        if (width != 2) return false;
        const unsigned adjustment = opcode == 0xc2 ? jit_fetch(decode, 2) : 0;
        jit_pop(emit, 2, adjustment, ip, count);
        jit_mov_imm(emit, 2, (uint32_t)(count + 1) << 16);
        jit_dp(emit, JIT_ORR, 0, 0, 2, false, 0);
        jit_word(emit, 0xe8bd, 0x87f0);
        *ends = true;
        return true;
    }
    if (opcode >= 0x50 && opcode <= 0x5f) {
        if (opcode >= 0x58) {
            jit_pop(emit, width, 0, ip, count);
            jit_gpr(emit, false, width, 0, opcode & 7);
        } else {
            jit_gpr(emit, true, width, 0, opcode & 7);
            jit_push(emit, width, ip, count);
        }
        return true;
    }
    if (opcode == 0x68 || opcode == 0x6a) {
        const uint32_t value = opcode == 0x68 ? jit_fetch(decode, width) :
                                (uint32_t)(int32_t)(int8_t)jit_fetch(decode, 1);
        jit_mov_imm(emit, 0, value);
        jit_push(emit, width, ip, count);
        return true;
    }
    if (opcode >= 0x90 && opcode <= 0x97) {
        if (opcode != 0x90) {
            jit_gpr(emit, true, width, 0, regax);
            jit_gpr(emit, true, width, 1, opcode & 7);
            jit_gpr(emit, false, width, 0, opcode & 7);
            jit_gpr(emit, false, width, 1, regax);
        }
        return true;
    }

    unsigned group = 0, alu = 0xff, dst = 0, src = 0;
    bool memory = false, memory_dst = false, immediate = false, writes = true;
    bool increment = false, exchange = false, lea = false;
    uint32_t value = 0;
    if (opcode < 0x40 && (opcode & 7) < 6) {
        group = opcode >> 3;
        alu = jit_x86_alu(group);
        if (alu == 0xff) return false;
        writes = group != 7;
        if (!(opcode & 1)) width = 1;
        if (opcode & 4) {
            immediate = true;
            value = jit_fetch(decode, width);
        } else {
            const uint8_t modrm = jit_fetch(decode, 1);
            memory = modrm < 0xc0;
            const unsigned rm_field = modrm & 7, reg_field = (modrm >> 3) & 7;
            if (memory) jit_modrm(decode, modrm, &address);
            memory_dst = !(opcode & 2);
            dst = memory_dst ? rm_field : reg_field;
            src = memory_dst ? reg_field : rm_field;
        }
    } else if (opcode >= 0x40 && opcode <= 0x4f) {
        dst = opcode & 7;
        alu = opcode < 0x48 ? JIT_ADD : JIT_SUB;
        immediate = increment = true;
        value = 1;
    } else if (opcode >= 0xb0 && opcode <= 0xbf) {
        dst = opcode & 7;
        if (opcode < 0xb8) width = 1;
        immediate = true;
        value = jit_fetch(decode, width);
    } else if (opcode >= 0xa0 && opcode <= 0xa3) {
        if (!(opcode & 1)) width = 1;
        address.displacement = jit_fetch(decode, address.wide ? 4 : 2);
        if (address.segment == JIT_SEG_NONE) address.segment = regds;
        memory = true;
        memory_dst = opcode & 2;
    } else if (opcode == 0xa8 || opcode == 0xa9) {
        if (opcode == 0xa8) width = 1;
        alu = JIT_AND;
        writes = false;
        immediate = true;
        value = jit_fetch(decode, width);
    } else {
        if (!((opcode >= 0x80 && opcode <= 0x8b) || opcode == 0x8d ||
              opcode == 0xc6 || opcode == 0xc7 || opcode == 0xfe || opcode == 0xff))
            return false;
        const uint8_t modrm = jit_fetch(decode, 1);
        const unsigned rm_field = modrm & 7, reg_field = (modrm >> 3) & 7;
        memory = modrm < 0xc0;
        if (memory) jit_modrm(decode, modrm, &address);
        dst = rm_field;
        src = reg_field;
        memory_dst = true;
        if (opcode <= 0x83) {
            if (opcode == 0x80 || opcode == 0x82) width = 1;
            alu = jit_x86_alu(reg_field);
            if (alu == 0xff) return false;
            writes = reg_field != 7;
            immediate = true;
            value = opcode == 0x83 ? (uint32_t)(int32_t)(int8_t)jit_fetch(decode, 1) :
                                     jit_fetch(decode, width);
        } else if (opcode <= 0x8b) {
            if (!(opcode & 1)) width = 1;
            if (opcode <= 0x85) {
                alu = JIT_AND;
                writes = false;
            } else if (opcode <= 0x87) {
                exchange = true;
            } else if (opcode & 2) {
                memory_dst = false;
                dst = reg_field;
                src = rm_field;
            }
        } else if (opcode == 0x8d) {
            if (!memory) return false;
            dst = reg_field;
            lea = true;
        } else if (opcode == 0xc6 || opcode == 0xc7) {
            if (reg_field != 0) return false;
            if (opcode == 0xc6) width = 1;
            immediate = true;
            value = jit_fetch(decode, width);
        } else {
            if (reg_field > 1) return false;
            if (opcode == 0xfe) width = 1;
            alu = reg_field == 0 ? JIT_ADD : JIT_SUB;
            immediate = increment = true;
            value = 1;
        }
    }

    if (memory)
        jit_address(emit, &address, lea);
    if (lea) {
        jit_gpr(emit, false, width, JIT_EA, dst);
        return true;
    }
    if (memory)
        jit_memory_guard(emit, width, memory_dst && writes, ip, count);
    if (alu != 0xff || exchange) {
        if (memory && memory_dst) jit_ram(emit, true, width, 0);
        else jit_gpr(emit, true, width, 0, dst);
        if (immediate) jit_mov_imm(emit, 1, value);
        else if (memory && !memory_dst) jit_ram(emit, true, width, 1);
        else jit_gpr(emit, true, width, 1, src);
        if (exchange) {
            jit_gpr(emit, false, width, 0, src);
            jit_mov(emit, 0, 1);
        } else {
            *flag_source = jit_alu(emit, alu, width, increment);
        }
    } else if (immediate) {
        jit_mov_imm(emit, 0, value);
    } else if (memory && !memory_dst) {
        jit_ram(emit, true, width, 0);
    } else {
        jit_gpr(emit, true, width, 0, src);
    }
    if (writes) {
        if (memory && memory_dst) jit_ram(emit, false, width, 0);
        else jit_gpr(emit, false, width, 0, dst);
    }
    return true;
}

static void jit_compile(jit_entry_t *entry, uint32_t linear, uint16_t ip)
{
    if (jit_cache->cursor > UINT32_MAX - JIT_CODE_BYTES) {
        const jit_entry_t saved = *entry;
        xrip_jit_use_cache(jit_cache);
        *entry = saved;
    }
    uint32_t available = fast_memory_size - linear;
    if (available > 0xffffu - ip) available = 0xffffu - ip;
    if (available > JIT_X86_BYTES) available = JIT_X86_BYTES;
    memcpy(jit_cache->source, fast_memory + linear, available);
    jit_emit_t emit = {0};
    jit_word(&emit, 0xe92d, 0x47f0); /* PUSH.W {r4-r10,lr}, keeps BLX stack aligned */
    jit_mov(&emit, JIT_ARG_CPU, 0);
    jit_mov(&emit, JIT_ARG_STATE, 1);
    jit_mov(&emit, JIT_ARG_START, 2);
    jit_mov(&emit, JIT_ARG_END, 3);
    jit_ls(&emit, true, 4, JIT_ARG_RAM, JIT_ARG_STATE, JIT_STATE_RAM);
    jit_ls(&emit, true, 4, JIT_ARG_LIMIT, JIT_ARG_STATE, JIT_STATE_RAM_SIZE);
    jit_half(&emit, 0x3f03); /* SUBS r7,#3, conservative bound for unaligned dwords */

    const uint8_t *current = jit_cache->source;
    const uint8_t *end = current + available;
    unsigned count = 0, flag_source = JIT_NO_FLAGS;
    bool ends = false;
    while (current < end && count < JIT_INSNS && !ends) {
        const unsigned saved = emit.next;
        jit_decode_t decode = {
            .next = current, .limit = end - current > 15 ? current + 15 : end
        };
        const uint16_t next_ip = ip + (current - jit_cache->source);
        if (!jit_instruction(&emit, &decode, next_ip, count, &flag_source, &ends) ||
            decode.short_read || emit.full) {
            emit.next = saved;
            emit.full = ends = false;
            break;
        }
        current = decode.next;
        ++count;
    }

    uint8_t source_size = current - jit_cache->source;
    if (count < 2) {
        /* A single instruction cannot amortize a native call and guard. */
        count = 0;
        source_size = available < 15 ? available : 15;
    } else if (!ends)
        jit_exit(&emit, ip + source_size, count);
    if (emit.full)
        return;

    const uint16_t code_size = count ? emit.next * sizeof(uint16_t) : 0;
    const unsigned size = (code_size + source_size + 3u) & ~3u;
    uint32_t position = jit_cache->cursor;
    unsigned offset = position & (JIT_CODE_BYTES - 1);
    if (size > JIT_CODE_BYTES - offset) {
        position += JIT_CODE_BYTES - offset;
        offset = 0;
    }
    uint8_t *code = (uint8_t *)jit_cache->code + offset;
    memcpy(code, jit_cache->work, code_size);
    memcpy(code + code_size, jit_cache->source, source_size);
    /* RP2350 uses the same writable cached QMI alias for data and fetch.
     * Complete the stores and synchronize instruction fetch before use. */
    __asm volatile("dsb sy\n\tisb sy" ::: "memory");
    jit_cache->cursor = position + size;
    entry->position = position;
    entry->code_size = code_size;
    entry->source_size = source_size;
    entry->count = count;
}

static inline bool jit_source_matches(const uint8_t *saved, const uint8_t *guest, unsigned length)
{
    while (length >= 4) {
        uint32_t a, b;
        memcpy(&a, saved, sizeof(a));
        memcpy(&b, guest, sizeof(b));
        if (a != b) return false;
        saved += 4;
        guest += 4;
        length -= 4;
    }
    while (length--) {
        if (*saved++ != *guest++) return false;
    }
    return true;
}

/* A rejected target or an ineligible CPU context should not make the
 * scalar loop pay for a cache probe at each taken branch. */
#define JIT_DEFER UINT32_MAX

static __attribute__((noinline)) unsigned jit_run(uint32_t budget, bool *may_compile)
{
    if (budget < 2 || protected_mode || (cr0 & (CR0_PE | CR0_PG)) || code32 || stack32 ||
        tf || active_cpu->i286_irq_shadow || CS_BASE > 0xffff0u)
        return JIT_DEFER;
    const uint16_t ip = XRIP_IP;
    const uint32_t linear = CS_BASE + ip;
    if (linear >= fast_memory_size || ip == 0xffffu)
        return JIT_DEFER;
    const uint32_t hash = (linear ^ (linear >> 8) ^ (CS_BASE >> 4)) * 0x9e3779b1u;
    jit_entry_t *entry = &jit_cache->entries[hash >> (32 - JIT_SLOT_BITS)];
    if (!entry->valid || entry->cs_base != CS_BASE || entry->ip != ip) {
        *entry = (jit_entry_t){.cs_base = CS_BASE, .ip = ip, .heat = 1, .valid = true};
        return 0;
    }
    if (entry->source_size) {
        if (jit_cache->cursor - entry->position > JIT_CODE_BYTES) {
            entry->source_size = 0;
        } else {
            const unsigned offset = entry->position & (JIT_CODE_BYTES - 1);
            const uint8_t *code = (const uint8_t *)jit_cache->code + offset;
            const unsigned count = entry->count;
            if (count > budget)
                return JIT_DEFER;
            if (entry->source_size <= fast_memory_size - linear &&
                jit_source_matches(code + entry->code_size, fast_memory + linear, entry->source_size)) {
                if (!count) return JIT_DEFER;
                typedef uint32_t (*jit_function)(CPU *, cpu_state_t *, uint32_t, uint32_t);
                bool resident = jit_sram_valid && jit_sram_position == entry->position;
                jit_function block = (jit_function)((uintptr_t)(resident ? jit_sram : code) | 1u);
                if (!flags_op) flags_cf = x86_flags.bits.CF;
                const volatile CPU *const irq_cpu = active_cpu;
                const bool allow_irq = ifl;
                const uint32_t end = linear + entry->source_size;
                unsigned done = 0;
                /* A self-loop can reuse verified code. Move it into the
                 * idle fast-loop overlay only after it actually repeats. */
                do {
                    const uint32_t result = block(active_cpu, &cpu_state, linear, end);
                    const unsigned step = result >> 16;
                    done += step;
                    XRIP_IP = (uint16_t)result;
                    if (step != count || XRIP_IP != ip || count > budget - done ||
                        (allow_irq && irq_cpu->intr))
                        break;
                    if (!resident && done >= count * 2 && budget - done >= count * 4 &&
                        jit_sram && entry->code_size <= jit_sram_size) {
                        memcpy(jit_sram, code, entry->code_size);
                        __asm volatile("dsb sy\n\tisb sy" ::: "memory");
                        jit_sram_position = entry->position;
                        jit_sram_valid = resident = true;
                        block = (jit_function)((uintptr_t)jit_sram | 1u);
                    }
                } while (!(allow_irq && irq_cpu->intr));
                retired += done;
                return done;
            }
            entry->source_size = 0;
            entry->heat = 0;
        }
    }
    if (entry->heat < JIT_HOT) ++entry->heat;
    if (entry->heat == JIT_HOT && *may_compile) {
        *may_compile = false;
        jit_compile(entry, linear, ip);
        if (!entry->count && entry->source_size)
            return JIT_DEFER;
    }
    return 0;
}
