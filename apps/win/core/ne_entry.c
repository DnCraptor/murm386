/* Bounded Win16 entry verifier for the shared DOS/386 CPU.
 *
 * This is deliberately NOT a general NE sandbox. Only a small 16-bit
 * instruction subset is accepted. All reachable paths must terminate in
 * RETF. Forward branches, counted LOOP and bounded direct near CALL/RET
 * are accepted; unresolved control-flow transfers are rejected.
 *
 * In particular, DATA accesses must stay below the reserved Win16 stack.
 * Register-derived DS addresses require exact values. BP-relative locals
 * and positive BP parameters must lie within verified stack slots;
 * indirect control transfers,
 * segment overrides and unrestricted SS accesses are rejected. A symbolic
 * stack tracks the FAR selectors passed to imported Win16 APIs.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_entry.h"
#include "ne_guest.h"

#define ENTRY_WINDOW 512u
#define ENTRY_VISIT_LIMIT 2048u
#define ENTRY_MAX_LOOP_CX 64u
/* The NE stack starts four bytes below initial SP (FAR CALL from boot).
 * Reserve headroom for Win16 API argument thunks, the PM/RM gateway and
 * the private fault handler. This counts all local frames and API args. */
#define ENTRY_STACK_BUDGET 20u
#define ENTRY_NEAR_CALL_DEPTH 2u

static unsigned args_for(uint16_t id)
{
    switch (id) {
    case 3: case 36: case 152: case 0x100d: return 0;
    case 90: return 2;
    case 88: case 89: return 4;
    case 0x1001: return 6;
    case 0x10b3: case 0x10b4: return 1;
    default: return 0xffu;
    }
}

enum { ARG_WORD = 1, ARG_DS = 2 };
enum { FRAME_NONE, FRAME_NEED_BP, FRAME_ACTIVE, FRAME_NEED_POP, FRAME_DONE };
enum { REG_AX, REG_CX, REG_DX, REG_BX, REG_SP, REG_BP, REG_SI, REG_DI };

typedef struct {
    const uint8_t *code;
    size_t entry, end;
    size_t data_limit; /* Exclusive, relative to DS; 0 forbids memory ops. */
    uint16_t thunk_selector;
    const uint16_t *service_ids;
    unsigned thunk_count;
    unsigned max_calls;
    unsigned visited;
    unsigned returns;
    /* Instruction boundaries are global to every path. Branching into the
     * middle of an immediate (or decoding overlapping instructions) fails. */
    uint8_t decoded[ENTRY_WINDOW]; /* 0=unknown, 1=opcode, 2=operand */
} verifier;

typedef struct {
    unsigned depth, phase;
    uint8_t reg[3], tag[3], known[3];
    uint16_t value[3];
} frame_saves;

typedef struct {
    frame_saves caller_saves;
    size_t return_at;
    unsigned stack_before, frame_before, bp_before;
    unsigned arg_words;
    /* BP+4 is the last word pushed before CALL; BP+6 the one before it.
     * The callee's own API arguments must not alias these near parameters. */
    uint8_t arg_tags[8], arg_known[8];
    uint16_t arg_values[8];
} near_call;

typedef struct {
    size_t at;
    unsigned calls, depth, frame;
    unsigned stack_bytes, bp_bytes, near_depth;
    unsigned cleanup_bytes; /* CDECL RET: exact caller ADD SP required next. */
    int direction; /* 0: unknown DF; +1: CLD; -1: STD. */
    frame_saves saves;
    near_call near[ENTRY_NEAR_CALL_DEPTH];
    uint8_t stack[8], stack_known[8];
    uint16_t stack_values[8];
    uint8_t regs[8];
    /* Exact 16-bit register values are tracked independently of the Win16
     * FAR-selector provenance tags. Unknown values cannot address memory. */
    uint16_t number[8];
    uint8_t number_known[8];
} path_state;

/* 8086 byte-register encoding: AL,CL,DL,BL,AH,CH,DH,BH.
 * A byte update cannot preserve a DS-selector provenance tag. */
static unsigned parent8(unsigned reg) { return reg & 3u; }
static unsigned byte8(const path_state *s, unsigned reg)
{
    unsigned n = s->number[parent8(reg)];
    return reg & 4u ? (n >> 8) & 0xffu : n & 0xffu;
}
static void set8(path_state *s, unsigned reg, unsigned value, int known)
{
    unsigned parent = parent8(reg);
    if (s->number_known[parent] && known) {
        unsigned shift = reg & 4u ? 8u : 0u;
        s->number[parent] = (uint16_t)((s->number[parent] &
            (uint16_t)~(0xffu << shift)) | ((value & 0xffu) << shift));
    } else {
        s->number_known[parent] = 0;
    }
    s->regs[parent] = ARG_WORD;
}
static unsigned alu(unsigned group, unsigned dst, unsigned src, unsigned mask)
{
    switch (group) {
    case 0: return (dst + src) & mask; /* ADD */
    case 1: return (dst | src) & mask; /* OR */
    case 4: return (dst & src) & mask; /* AND */
    case 5: return (dst - src) & mask; /* SUB */
    case 6: return (dst ^ src) & mask; /* XOR */
    default: return dst & mask;
    }
}
static int alu_group(unsigned group)
{
    return group == 0 || group == 1 || group == 4 || group == 5 ||
           group == 6 || group == 7;
}
static unsigned shifted(unsigned group, unsigned value,
                        unsigned count, unsigned width)
{
    unsigned mask = width == 8 ? 0xffu : 0xffffu;
    value &= mask;
    if (count == 0) return value;
    if (group == 4) return (value << count) & mask; /* SHL */
    if (group == 5) return value >> count; /* SHR */
    /* Portable arithmetic right shift, including negative 8/16-bit values. */
    unsigned result = value >> count;
    if (value & (1u << (width - 1u)))
        result |= mask ^ ((1u << (width - count)) - 1u);
    return result & mask; /* SAR */
}

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static int ordinary_reg(unsigned reg)
{
    return reg != REG_SP && reg != REG_BP;
}

static int stack_push(path_state *s, unsigned bytes)
{
    if (bytes > ENTRY_STACK_BUDGET - s->stack_bytes) return 0;
    s->stack_bytes += bytes;
    return 1;
}

/* A local word/byte must be entirely below the current BP, in space
 * explicitly reserved with SUB SP,imm. Addresses above BP could alias the
 * caller's return address, saved BP, or PASCAL arguments. */
static int local_ea(const path_state *s, const uint8_t *p, size_t remain,
                    unsigned width, size_t *bytes)
{
    if (s->frame != FRAME_ACTIVE || remain < 3 ||
        (p[1] & 7u) != 6u) return 0;
    unsigned mod = p[1] >> 6;
    int displacement;
    if (mod == 1) {
        displacement = (int8_t)p[2];
        *bytes = 3;
    } else if (mod == 2 && remain >= 4) {
        displacement = (int16_t)read16(p + 2);
        *bytes = 4;
    } else return 0;
    if (displacement >= 0 || -displacement < (int)width ||
        (unsigned)(-displacement) > s->stack_bytes - s->bp_bytes)
        return 0;
    return 1;
}

/* Positive BP offsets in a near callee refer to the actual words pushed
 * by its caller. BP+0 is saved BP, BP+2 is return IP; neither is data.
 * Only word-aligned, in-range parameters are readable. This is deliberately
 * NOT a general SS memory access primitive. */
static int near_arg_ea(const path_state *s, const uint8_t *p, size_t remain,
                       size_t *bytes, unsigned *index)
{
    if (s->frame != FRAME_ACTIVE || !s->near_depth || remain < 3u ||
        (p[1] & 7u) != 6u)
        return 0;
    unsigned mod = p[1] >> 6;
    int displacement;
    if (mod == 1u) {
        displacement = (int8_t)p[2];
        *bytes = 3u;
    } else if (mod == 2u && remain >= 4u) {
        displacement = (int16_t)read16(p + 2);
        *bytes = 4u;
    } else {
        return 0;
    }
    if (displacement < 4 || (displacement & 1))
        return 0;
    *index = (unsigned)(displacement - 4) / 2u;
    return *index < s->near[s->near_depth - 1].arg_words;
}

static int mark(verifier *v, size_t at, size_t bytes)
{
    if (at < v->entry || at >= v->end || bytes > v->end - at || !bytes)
        return 0;
    size_t off = at - v->entry;
    if (v->decoded[off] == 2)
        return 0;
    for (size_t i = 1; i < bytes; i++)
        if (v->decoded[off + i] == 1)
            return 0;
    v->decoded[off] = 1;
    for (size_t i = 1; i < bytes; i++)
        v->decoded[off + i] = 2;
    return 1;
}

/* The entire DS region below the reserved stack must be addressable. */
static int direct_data(const verifier *v, unsigned offset, unsigned width)
{
    return v->data_limit >= width && offset <= v->data_limit - width;
}

/* Decode a 16-bit data effective address. Register-indirect access is
 * permitted only with exact values for every base/index register, and only
 * when the full operand stays in the already validated DATA region. BP-based
 * forms default to SS and are intentionally rejected. No segment/address
 * override prefix is part of the accepted instruction subset. */
static int data_ea(const verifier *v, const path_state *s,
                   const uint8_t *p, size_t remain, unsigned width,
                   size_t *bytes, unsigned *address)
{
    if (remain < 2 || (p[1] & 0xc0) == 0xc0)
        return 0;
    unsigned mod = p[1] >> 6, rm = p[1] & 7u;
    size_t len = 2;
    int32_t addr = 0;
    if (mod == 0 && rm == 6) { /* direct disp16 */
        if (remain < 4) return 0;
        addr = read16(p + 2);
        len += 2;
    } else {
        unsigned r1 = 8u, r2 = 8u;
        switch (rm) {
        case 0: r1 = REG_BX; r2 = REG_SI; break;
        case 1: r1 = REG_BX; r2 = REG_DI; break;
        case 4: r1 = REG_SI; break;
        case 5: r1 = REG_DI; break;
        case 7: r1 = REG_BX; break;
        default: return 0; /* BP+SI, BP+DI, BP (SS) */
        }
        if (!s->number_known[r1] ||
            (r2 < 8u && !s->number_known[r2]))
            return 0;
        addr = s->number[r1];
        if (r2 < 8u) addr += s->number[r2];
        if (mod == 1) {
            if (remain < len + 1) return 0;
            addr += (int8_t)p[len++];
        } else if (mod == 2) {
            if (remain < len + 2) return 0;
            addr += (int16_t)read16(p + len);
            len += 2;
        }
    }
    /* Also reject 16-bit address wraparound; no unintended access at 0000. */
    if (addr < 0 || addr > 0xffff || !direct_data(v, (unsigned)addr, width))
        return 0;
    *bytes = len;
    *address = (unsigned)addr;
    return 1;
}

/* Verify an entire 16-bit string instruction's footprint before entering PE.
 * SI reads use DS; DI reads/writes use ES, which the WINN PM bootstrap sets
 * to the same checked data selector as DS. The gateway saves/restores ES.
 * For backward direction, verify the first and last access (no wraparound).
 * Every access between those endpoints is monotonic and stays in DATA. */
static int string_span(const verifier *v, const path_state *s,
                       unsigned reg, unsigned count, unsigned width)
{
    if (!count) return 1; /* REP with CX=0 does not dereference pointers. */
    if (!s->number_known[reg]) return 0;
    int32_t first = s->number[reg];
    int32_t last = first + s->direction * (int32_t)(count - 1u) * (int32_t)width;
    if (last < 0 || last > 0xffff ||
        (uint32_t)last + width > 65536u) return 0;
    return direct_data(v, (unsigned)first, width) &&
           direct_data(v, (unsigned)last, width);
}

static int forward_target(const verifier *v, size_t at, size_t length,
                          int displacement, size_t *target)
{
    /* Backward/zero-displacement branches may loop, so reject them even
     * when the target itself is a valid instruction boundary. */
    if (displacement <= 0 || at + length > v->end)
        return 0;
    size_t dst = at + length + (unsigned)displacement;
    if (dst >= v->end)
        return 0;
    *target = dst;
    return 1;
}

/* Every path carries its own simulated registers and argument stack.
 * The verifier explores both outcomes of every conditional branch. */
static int check_path(verifier *v, path_state s)
{
    while (s.at < v->end) {
        if (++v->visited > ENTRY_VISIT_LIMIT)
            return 0;
        size_t at = s.at;
        const uint8_t *p = v->code + at;
        size_t remaining = v->end - at;
        uint8_t op = p[0];

        /* A near CDECL callee returns with its arguments still on the
         * stack. Only an immediately following, exact caller ADD SP is
         * accepted. No NOP, branch, FAR API, or other stack operation may
         * intervene: the symbolic argument stack was transferred to the
         * callee at CALL, so allowing arbitrary operations here would
         * misrepresent the real stack. This also supports frameless callers. */
        if (s.cleanup_bytes) {
            size_t length = op == 0x83 ? 3u : op == 0x81 ? 4u : 0u;
            if (!length || remaining < length || p[1] != 0xc4 || s.depth ||
                (unsigned)(op == 0x83 ? (int8_t)p[2] :
                                         read16(p + 2)) != s.cleanup_bytes ||
                s.stack_bytes < s.cleanup_bytes ||
                !mark(v, at, length))
                return 0;
            s.stack_bytes -= s.cleanup_bytes;
            s.cleanup_bytes = 0;
            s.at += length;
            continue;
        }

        if (s.saves.phase && op != 0x53 && op != 0x56 &&
            op != 0x57 &&
            !((op == 0x83 || op == 0x81) && remaining >= 2u &&
              p[1] == 0xec))
            s.saves.phase = 0;
        if (op == 0x90) { /* NOP */
            if (!mark(v, at, 1)) return 0;
            s.at++;
            continue;
        }
        if (s.frame == FRAME_NEED_BP) {
            if (remaining < 2 ||
                !((op == 0x8b && p[1] == 0xec) ||
                  (op == 0x89 && p[1] == 0xe5)) || !mark(v, at, 2))
                return 0;
            s.frame = FRAME_ACTIVE;
            s.bp_bytes = s.stack_bytes;
            s.saves.phase = 1;
            s.at += 2;
            continue;
        }
        if (s.frame == FRAME_NEED_POP ||
            (s.frame == FRAME_ACTIVE && op == 0x5d &&
             s.depth == 0 && s.stack_bytes == s.bp_bytes)) {
            if (op != 0x5d || s.stack_bytes < 2u ||
                !mark(v, at, 1)) return 0;
            s.stack_bytes -= 2u;
            s.frame = FRAME_DONE;
            s.at++;
            continue;
        }
        if (s.frame == FRAME_DONE && op != 0xcb && op != 0xc3 &&
            op != 0xc2)
            return 0;
        /* CLD/STD make the string direction explicit. Never assume the
         * incoming guest FLAGS have DF cleared. */
        if (op == 0xfc || op == 0xfd) {
            if (!mark(v, at, 1)) return 0;
            s.direction = op == 0xfc ? 1 : -1;
            s.at++;
            continue;
        }

        /* 8086 MOVS, CMPS, STOS, LODS, SCAS (byte/word), with bounded
         * REP / REPE / REPNE. No operand-size/segment overrides or unknown
         * pointers. Every possible iteration is checked before execution.
         * REPE/REPNE on comparisons may exit early on runtime FLAGS:
         * their final SI/DI/CX are therefore treated as unknown values. */
        unsigned prefixed = op == 0xf3 || op == 0xf2;
        uint8_t sop = prefixed && remaining >= 2u ? p[1] : op;
        if (sop >= 0xa4 && sop <= 0xaf && sop != 0xa8 && sop != 0xa9) {
            const unsigned compare = sop == 0xa6 || sop == 0xa7 ||
                                     sop == 0xae || sop == 0xaf;
            const unsigned use_si = sop <= 0xa7 || sop == 0xac || sop == 0xad;
            const unsigned use_di = sop <= 0xa7 || sop == 0xaa || sop == 0xab ||
                                    sop == 0xae || sop == 0xaf;
            const unsigned width = (sop & 1u) ? 2u : 1u;
            const unsigned len = prefixed ? 2u : 1u;
            if (!s.direction || remaining < len ||
                (op == 0xf2 && !compare)) return 0;
            unsigned count = 1u;
            if (prefixed) {
                if (!s.number_known[REG_CX] ||
                    s.number[REG_CX] > ENTRY_MAX_LOOP_CX) return 0;
                count = s.number[REG_CX];
            }
            if ((use_si && !string_span(v, &s, REG_SI, count, width)) ||
                (use_di && !string_span(v, &s, REG_DI, count, width)) ||
                !mark(v, at, len)) return 0;
            const unsigned variable = prefixed && compare && count != 0u;
            if (count) {
                int32_t delta = s.direction * (int32_t)(count * width);
                if (use_si) {
                    s.regs[REG_SI] = ARG_WORD;
                    if (variable) s.number_known[REG_SI] = 0;
                    else s.number[REG_SI] = (uint16_t)(s.number[REG_SI] + delta);
                }
                if (use_di) {
                    s.regs[REG_DI] = ARG_WORD;
                    if (variable) s.number_known[REG_DI] = 0;
                    else s.number[REG_DI] = (uint16_t)(s.number[REG_DI] + delta);
                }
                if (sop == 0xac || sop == 0xad) { /* LODS changes AL/AX. */
                    s.regs[REG_AX] = ARG_WORD;
                    s.number_known[REG_AX] = 0;
                }
            }
            if (prefixed) {
                s.regs[REG_CX] = ARG_WORD;
                s.number_known[REG_CX] = !variable;
                if (!variable) s.number[REG_CX] = 0;
            }
            s.at += len;
            continue;
        }

        if (op == 0xc9) { /* LEAVE = MOV SP,BP; POP BP */
            if (s.frame != FRAME_ACTIVE || s.depth || s.saves.depth ||
                s.bp_bytes < 2u || !mark(v, at, 1)) return 0;
            s.stack_bytes = s.bp_bytes - 2u;
            s.frame = FRAME_DONE;
            s.at++;
            continue;
        }
        if (op == 0xc3 || op == 0xc2) { /* RET / RET imm16 (near) */
            size_t length = op == 0xc2 ? 3u : 1u;
            if (!s.near_depth || s.depth || s.saves.depth || s.stack_bytes < 2u ||
                remaining < length ||
                (s.frame != FRAME_NONE && s.frame != FRAME_DONE) ||
                !mark(v, at, length)) return 0;
            near_call c = s.near[--s.near_depth];
            /* The immediate is a byte count, not a word count. Reject
             * partial/excessive PASCAL cleanup. Plain RET is CDECL: its
             * caller must immediately perform the exact ADD SP,bytes. */
            unsigned pop = op == 0xc2 ? read16(p + 1) : 0u;
            if ((op == 0xc2 && pop != c.arg_words * 2u) ||
                s.stack_bytes != c.stack_before + 2u ||
                c.stack_before < pop) return 0;
            s.stack_bytes = c.stack_before - pop;
            s.cleanup_bytes = op == 0xc3 ? c.arg_words * 2u : 0u;
            s.bp_bytes = c.bp_before;
            s.frame = c.frame_before;
            s.saves = c.caller_saves;
            s.at = c.return_at;
            continue;
        }
        if (op == 0x0f && remaining >= 4 &&
            p[1] >= 0x80 && p[1] <= 0x8f) { /* 386 near Jcc rel16 */
            size_t target;
            if (!forward_target(v, at, 4, (int16_t)read16(p + 2), &target) ||
                !mark(v, at, 4)) return 0;
            path_state taken = s;
            taken.at = target;
            if (!check_path(v, taken)) return 0;
            s.at = at + 4;
            continue;
        }
        /* UD2 is an unconditionally faulting 386 instruction. Permit it
         * solely as a terminal test path after an imported API call: this
         * tests the private IDT recovery without enabling arbitrary code. */
        if (op == 0x0f && remaining >= 2 && p[1] == 0x0b) {
            if (!s.calls || s.depth ||
                (s.frame != FRAME_NONE && s.frame != FRAME_DONE) ||
                !mark(v, at, 2)) return 0;
            if (s.calls > v->max_calls) v->max_calls = s.calls;
            v->returns++;
            return 1;
        }
        if (op == 0xcb) { /* RETF */
            if (!s.calls || s.depth || s.near_depth || s.saves.depth ||
                s.stack_bytes ||
                (s.frame != FRAME_NONE && s.frame != FRAME_DONE) ||
                !mark(v, at, 1))
                return 0;
            if (s.calls > v->max_calls)
                v->max_calls = s.calls;
            v->returns++;
            return 1;
        }
        if (op == 0x55 && s.frame == FRAME_NONE &&
            (s.near_depth || !s.calls) && !s.depth) { /* PUSH BP */
            if (!stack_push(&s, 2u) || !mark(v, at, 1)) return 0;
            s.frame = FRAME_NEED_BP;
            s.at++;
            continue;
        }
        /* ENTER imm16,0 is a compiler-generated BP prologue. The dynamic
         * nesting operand is never accepted. Local space is bounded. */
        if (op == 0xc8 && remaining >= 4u) {
            unsigned locals = read16(p + 1);
            if (s.frame != FRAME_NONE || s.depth || p[3] ||
                (s.near_depth == 0u && s.calls) ||
                locals > 12u || (locals & 1u) ||
                !stack_push(&s, 2u + locals) || !mark(v, at, 4u))
                return 0;
            s.frame = FRAME_ACTIVE;
            s.bp_bytes = s.stack_bytes - locals;
            s.saves = (frame_saves){0};
            s.saves.phase = 1;
            s.at += 4u;
            continue;
        }
        if (s.frame == FRAME_ACTIVE && remaining >= 2 &&
            ((op == 0x89 && p[1] == 0xec) ||
             (op == 0x8b && p[1] == 0xe5))) { /* MOV SP,BP */
            if (s.depth || s.saves.depth || !mark(v, at, 2)) return 0;
            s.stack_bytes = s.bp_bytes;
            s.frame = FRAME_NEED_POP;
            s.at += 2;
            continue;
        }

        /* CALL rel16. Pending PUSH words become this procedure's formal
         * parameters rather than Win16 API arguments. The return IP is
         * counted separately. RET imm16 cleans PASCAL args, plain RET
         * requires an exact caller ADD SP for CDECL. */
        if (op == 0xe8) {
            if (remaining < 3 ||
                s.near_depth == ENTRY_NEAR_CALL_DEPTH ||
                !mark(v, at, 3)) return 0;
            int32_t target = (int32_t)at + 3 + (int16_t)read16(p + 1);
            if (target <= (int32_t)(at + 2) ||
                target >= (int32_t)v->end ||
                !stack_push(&s, 2u)) return 0;
            near_call *c = &s.near[s.near_depth++];
            c->return_at = at + 3;
            c->stack_before = s.stack_bytes - 2u;
            c->frame_before = s.frame;
            c->bp_before = s.bp_bytes;
            c->caller_saves = s.saves;
            c->arg_words = s.depth;
            for (unsigned i = 0; i < c->arg_words; i++) {
                unsigned src = c->arg_words - 1u - i;
                c->arg_tags[i] = s.stack[src];
                c->arg_known[i] = s.stack_known[src];
                c->arg_values[i] = s.stack_values[src];
            }
            s.depth = 0; /* Callee now owns its formal arguments. */
            s.frame = FRAME_NONE;
            s.bp_bytes = 0;
            s.saves = (frame_saves){0};
            s.at = (size_t)target;
            continue;
        }

        /* Reserve/reclaim small, known-size local space. Never let a
         * stack adjustment consume a return address or another frame. */
        if ((op == 0x83 || op == 0x81) && remaining >= 3 &&
            (p[1] == 0xec || p[1] == 0xc4)) { /* SUB/ADD SP,imm */
            size_t length = op == 0x83 ? 3u : 4u;
            if (s.frame != FRAME_ACTIVE || s.depth || remaining < length ||
                !mark(v, at, length)) return 0;
            int amount = op == 0x83 ? (int8_t)p[2] : (int)read16(p + 2);
            if (amount <= 0 || (amount & 1) || amount > 12)
                return 0;
            if (p[1] == 0xec) {
                if (!stack_push(&s, (unsigned)amount)) return 0;
            } else {
                s.saves.phase = 0;
                if ((unsigned)amount > s.stack_bytes - s.bp_bytes)
                    return 0;
                s.stack_bytes -= (unsigned)amount;
            }
            s.at += length;
            continue;
        }

        /* 16-bit LOOP is safe with an exact, bounded CX. This is a
         * deterministic branch: LOOP decrements CX before testing it.
         * The instruction subset cannot modify code or change address size.
         * Every taken iteration is visited, so resetting CX inside the loop
         * is rejected by the global instruction-visit budget. */
        if (op == 0xe2) { /* LOOP rel8, backwards only */
            if (remaining < 2 || !s.number_known[REG_CX] ||
                !s.number[REG_CX] ||
                s.number[REG_CX] > ENTRY_MAX_LOOP_CX || (int8_t)p[1] >= 0)
                return 0;
            int32_t target = (int32_t)at + 2 + (int8_t)p[1];
            if (target < (int32_t)v->entry || target >= (int32_t)at ||
                !mark(v, at, 2))
                return 0;
            s.number[REG_CX]--;
            s.at = s.number[REG_CX] ? (size_t)target : at + 2;
            continue;
        }

        /* Forward-only 16-bit branches: 70..7F Jcc rel8, EB JMP rel8,
         * E9 JMP rel16. All paths must independently pass validation. */
        if ((op >= 0x70 && op <= 0x7f) || op == 0xeb || op == 0xe9) {
            size_t length = op == 0xe9 ? 3u : 2u, target;
            if (remaining < length) return 0;
            int displacement = length == 3 ? (int16_t)read16(p + 1) : (int8_t)p[1];
            if (!forward_target(v, at, length, displacement, &target) ||
                !mark(v, at, length))
                return 0;
            if (op >= 0x70 && op <= 0x7f) {
                path_state taken = s;
                taken.at = target;
                if (!check_path(v, taken)) return 0;
                s.at = at + length; /* fall-through also mandatory */
            } else {
                s.at = target;
            }
            continue;
        }

        /* CMP/TEST alter FLAGS but not register values or provenance.
         * ModRM register-register byte TEST is safe; memory access from
         * TEST remains excluded. This enables a bounded NUL scanner with
         * TEST AL,AL / JZ without permitting an arbitrary guest pointer. */
        if (op == 0x3d || op == 0xa9) { /* CMP/TEST AX,imm16 */
            if (remaining < 3 || !mark(v, at, 3)) return 0;
            s.at += 3;
            continue;
        }
        if (op == 0x3c || op == 0xa8) { /* CMP/TEST AL,imm8 */
            if (remaining < 2 || !mark(v, at, 2)) return 0;
            s.at += 2;
            continue;
        }
        if (op == 0x39 || op == 0x3b || op == 0x85 ||
            op == 0x84) { /* CMP/TEST register-register (8/16 bit) */
            if (remaining < 2 || (p[1] & 0xc0) != 0xc0 ||
                !mark(v, at, 2)) return 0;
            s.at += 2;
            continue;
        }
        if (op == 0x80 && remaining >= 2 &&
            (p[1] & 0xc0u) != 0xc0u &&
            (p[1] & 0x38u) == 0x38u) { /* CMP r/m8,imm8: /7 */
            size_t ea_len;
            unsigned addr;
            if (!data_ea(v, &s, p, remaining, 1u, &ea_len, &addr) ||
                remaining < ea_len + 1u ||
                !mark(v, at, ea_len + 1u)) return 0;
            s.at += ea_len + 1u;
            continue;
        }

        /* Absolute 16-bit DS references. Indexed references are checked
         * separately using exact base/index values. */
        if (op == 0xa1 || op == 0xa3) { /* MOV AX,[disp16] / MOV [disp16],AX */
            if (remaining < 3 || !direct_data(v, read16(p + 1), 2) ||
                !mark(v, at, 3)) return 0;
            if (op == 0xa1) {
                s.regs[REG_AX] = ARG_WORD;
                s.number_known[REG_AX] = 0;
            }
            s.at += 3;
            continue;
        }
        /* Byte-sized accesses are confined to the same checked DATA
         * region as word MOV. AL is the only accepted byte register:
         * other 8-bit halves would require tracking their parent words.
         * A load changes AL, invalidating both the numeric value and
         * selector provenance previously assigned to the entire AX. */
        if (op == 0xa0 || op == 0xa2) { /* MOV AL,[moffs16] / [moffs16],AL */
            if (remaining < 3 || !direct_data(v, read16(p + 1), 1u) ||
                !mark(v, at, 3)) return 0;
            if (op == 0xa0) {
                s.regs[REG_AX] = ARG_WORD;
                s.number_known[REG_AX] = 0;
            }
            s.at += 3;
            continue;
        }
        if ((op == 0x8a || op == 0x88) && remaining >= 2 &&
            (p[1] & 0xc0u) == 0xc0u) { /* MOV r8,r8 */
            unsigned reg = (p[1] >> 3) & 7u, rm = p[1] & 7u;
            unsigned dst = op == 0x8a ? reg : rm, src = op == 0x8a ? rm : reg;
            int known = s.number_known[parent8(src)];
            unsigned value = known ? byte8(&s, src) : 0u;
            if (!mark(v, at, 2)) return 0;
            set8(&s, dst, value, known);
            s.at += 2;
            continue;
        }
        if (op == 0x8a || op == 0x88) { /* MOV AL,r/m8 / MOV r/m8,AL */
            size_t ea_len;
            unsigned addr;
            if (remaining < 2 || (p[1] & 0x38u) != 0u ||
                !data_ea(v, &s, p, remaining, 1u, &ea_len, &addr) ||
                !mark(v, at, ea_len)) return 0;
            if (op == 0x8a) {
                s.regs[REG_AX] = ARG_WORD;
                s.number_known[REG_AX] = 0;
            }
            s.at += ea_len;
            continue;
        }
        if (op == 0xc6 || op == 0xc7) { /* MOV [known DS/local ea],imm */
            size_t ea_len;
            unsigned addr;
            size_t imm_len = op == 0xc6 ? 1u : 2u;
            if (remaining < 2 || (p[1] & 0x38u) != 0 ||
                !(local_ea(&s, p, remaining, (unsigned)imm_len, &ea_len) ||
                  data_ea(v, &s, p, remaining, (unsigned)imm_len,
                          &ea_len, &addr)) ||
                remaining < ea_len + imm_len ||
                !mark(v, at, ea_len + imm_len)) return 0;
            s.at += ea_len + imm_len;
            continue;
        }

        /* PUSH WORD [BP+4..] forwards a checked near parameter, retaining
         * its Win16 FAR-selector provenance and known numeric value. */
        if (op == 0xff && remaining >= 2 &&
            (p[1] & 0x38u) == 0x30u) { /* FF /6 */
            size_t ea_len;
            unsigned index;
            if (s.depth == sizeof(s.stack) ||
                !near_arg_ea(&s, p, remaining, &ea_len, &index) ||
                !stack_push(&s, 2u) || !mark(v, at, ea_len)) return 0;
            const near_call *c = &s.near[s.near_depth - 1];
            s.stack[s.depth] = c->arg_tags[index];
            s.stack_known[s.depth] = c->arg_known[index];
            s.stack_values[s.depth] = c->arg_values[index];
            s.depth++;
            s.at += ea_len;
            continue;
        }

        /* Standard 16-bit DS addressing, not arbitrary guest pointers.
         * LEA computes the checked address; MOV can read/write a word.
         * BP+4.. in a near callee reads checked, known call arguments. */
        if (op == 0x8d || op == 0x8b || op == 0x89) {
            if (remaining >= 2 && (p[1] & 0xc0u) != 0xc0u) {
                unsigned reg = (p[1] >> 3) & 7u, addr;
                size_t ea_len;
                if (!ordinary_reg(reg)) return 0;
                unsigned index;
                if (op == 0x8b &&
                    near_arg_ea(&s, p, remaining, &ea_len, &index)) {
                    if (!mark(v, at, ea_len)) return 0;
                    const near_call *c = &s.near[s.near_depth - 1];
                    s.regs[reg] = c->arg_tags[index];
                    s.number_known[reg] = c->arg_known[index];
                    s.number[reg] = c->arg_values[index];
                    s.at += ea_len;
                    continue;
                }
                if (op != 0x8d && local_ea(&s, p, remaining, 2u, &ea_len)) {
                    if (!mark(v, at, ea_len)) return 0;
                    if (op == 0x8b) {
                        s.regs[reg] = ARG_WORD;
                        s.number_known[reg] = 0;
                    }
                    s.at += ea_len;
                    continue;
                }
                if (!data_ea(v, &s, p, remaining, op == 0x8d ? 1u : 2u,
                             &ea_len, &addr) || !mark(v, at, ea_len)) return 0;
                if (op != 0x89) {
                    s.regs[reg] = ARG_WORD;
                    s.number_known[reg] = op == 0x8d;
                    if (op == 0x8d) s.number[reg] = (uint16_t)addr;
                }
                s.at += ea_len;
                continue;
            }
        }

        /* Immediate ALU. Register operands only. CMP leaves provenance
         * untouched; any other arithmetic discards selector provenance. */
        if (op == 0x80 || op == 0x81 || op == 0x83) {
            unsigned width = op == 0x80 ? 8u : 16u;
            size_t length = op == 0x81 ? 4u : 3u;
            if (remaining < length || (p[1] & 0xc0u) != 0xc0u ||
                !alu_group((p[1] >> 3) & 7u) || !mark(v, at, length))
                return 0;
            unsigned group = (p[1] >> 3) & 7u, reg = p[1] & 7u;
            unsigned imm = op == 0x81 ? read16(p + 2) :
                           op == 0x83 ? (uint16_t)(int16_t)(int8_t)p[2] : p[2];
            if (width == 16) {
                if (!ordinary_reg(reg)) return 0;
                if (group != 7) {
                    if (s.number_known[reg])
                        s.number[reg] = (uint16_t)alu(group, s.number[reg], imm, 0xffffu);
                    s.regs[reg] = ARG_WORD;
                }
            } else if (group != 7) {
                unsigned value = s.number_known[parent8(reg)] ?
                    alu(group, byte8(&s,reg), imm, 0xffu) : 0u;
                set8(&s, reg, value, s.number_known[parent8(reg)]);
            }
            s.at += length;
            continue;
        }
        /* MOV r8,imm8 preserves the other byte only if it was already known. */
        if (op >= 0xb0 && op <= 0xb7) {
            if (remaining < 2 || !mark(v, at, 2)) return 0;
            set8(&s, op - 0xb0, p[1], 1);
            s.at += 2;
            continue;
        }
        /* AL-only immediate operations; CMP/TEST are handled earlier. */
        if (op == 0x04 || op == 0x0c || op == 0x24 ||
            op == 0x2c || op == 0x34) {
            if (remaining < 2 || !mark(v, at, 2)) return 0;
            unsigned group = op == 0x04 ? 0 : op == 0x0c ? 1 :
                             op == 0x24 ? 4 : op == 0x2c ? 5 : 6;
            unsigned value = s.number_known[REG_AX] ?
                alu(group, byte8(&s,0), p[1], 0xffu) : 0u;
            set8(&s, 0, value, s.number_known[REG_AX]);
            s.at += 2;
            continue;
        }
        /* Group 3: TEST, NOT, NEG on registers (never memory, SP or BP). */
        if (op == 0xf6 || op == 0xf7) {
            unsigned width = op == 0xf6 ? 8u : 16u;
            if (remaining < 2 || (p[1] & 0xc0u) != 0xc0u) return 0;
            unsigned group = (p[1] >> 3) & 7u, reg = p[1] & 7u;
            if (group != 0 && group != 2 && group != 3) return 0;
            size_t length = group == 0 ? (width == 8 ? 3u : 4u) : 2u;
            if (remaining < length || (width == 16 && !ordinary_reg(reg)) ||
                !mark(v, at, length)) return 0;
            if (group != 0) {
                if (width == 16) {
                    if (s.number_known[reg])
                        s.number[reg] = group == 2 ? (uint16_t)~s.number[reg] :
                                        (uint16_t)-s.number[reg];
                    s.regs[reg] = ARG_WORD;
                } else {
                    unsigned val = s.number_known[parent8(reg)] ?
                        (group == 2 ? ~byte8(&s,reg) : -byte8(&s,reg)) & 0xffu : 0u;
                    set8(&s,reg,val,s.number_known[parent8(reg)]);
                }
            }
            s.at += length;
            continue;
        }
        /* Group 2: SHL/SHR/SAR, imm8, 1, or CL. All register-only.
         * For unknown or >=width CL shifts, numeric provenance is discarded. */
        if (op == 0xc0 || op == 0xc1 || op == 0xd0 || op == 0xd1 ||
            op == 0xd2 || op == 0xd3) {
            if (remaining < 2 || (p[1] & 0xc0u) != 0xc0u) return 0;
            unsigned group = (p[1] >> 3) & 7u, reg = p[1] & 7u;
            unsigned width = (op == 0xc0 || op == 0xd0 || op == 0xd2) ? 8u : 16u;
            size_t length = op == 0xc0 || op == 0xc1 ? 3u : 2u;
            if ((group != 4 && group != 5 && group != 7) ||
                (width == 16 && !ordinary_reg(reg)) ||
                remaining < length || !mark(v, at, length)) return 0;
            int known_count = 1;
            unsigned count = op == 0xc0 || op == 0xc1 ? p[2] & 31u :
                             op == 0xd0 || op == 0xd1 ? 1u : 0u;
            if (op == 0xd2 || op == 0xd3) {
                known_count = s.number_known[REG_CX];
                if (known_count) count = s.number[REG_CX] & 31u;
            }
            int known_value = (width == 8 ? s.number_known[parent8(reg)] :
                              s.number_known[reg]);
            if (width == 8) {
                unsigned value = known_count && count < width && known_value ?
                    shifted(group,byte8(&s,reg),count,width) : 0u;
                set8(&s,reg,value,known_count && count < width && known_value);
            } else {
                if (known_count && count < width && known_value)
                    s.number[reg] = (uint16_t)shifted(group,s.number[reg],count,width);
                else s.number_known[reg] = 0;
                s.regs[reg] = ARG_WORD;
            }
            s.at += length;
            continue;
        }


        if (op == 0x1e) { /* PUSH DS */
            if (s.depth == sizeof(s.stack) || !stack_push(&s, 2u) ||
                !mark(v, at, 1)) return 0;
            s.stack[s.depth] = ARG_DS;
            s.stack_known[s.depth] = 0;
            s.depth++;
            s.at++;
            continue;
        }
        if (op == 0x68 || op == 0x6a) { /* PUSH immediate */
            size_t length = op == 0x68 ? 3u : 2u;
            if (remaining < length || s.depth == sizeof(s.stack) ||
                !stack_push(&s, 2u) || !mark(v, at, length)) return 0;
            s.stack[s.depth] = ARG_WORD;
            s.stack_known[s.depth] = 1;
            s.stack_values[s.depth] = op == 0x68 ? read16(p + 1) :
                                      (uint16_t)(int16_t)(int8_t)p[1];
            s.depth++;
            s.at += length;
            continue;
        }
        if (op >= 0xb8 && op <= 0xbf) { /* MOV r16,imm16 */
            unsigned reg = op - 0xb8;
            if (remaining < 3 || !ordinary_reg(reg) || !mark(v, at, 3)) return 0;
            s.regs[reg] = ARG_WORD;
            s.number_known[reg] = 1;
            s.number[reg] = read16(p + 1);
            s.at += 3;
            continue;
        }
        /* Early BX/SI/DI pushes in a BP frame are callee-save slots,
         * not pending native API arguments. Require reverse-order POPs. */
        if (s.frame == FRAME_ACTIVE && s.saves.phase &&
            (op == 0x53 || op == 0x56 || op == 0x57)) {
            unsigned reg = op - 0x50;
            if (s.depth || s.saves.depth >= 3u) return 0;
            for (unsigned i = 0; i < s.saves.depth; ++i)
                if (s.saves.reg[i] == reg) return 0;
            if (!stack_push(&s, 2u) || !mark(v, at, 1u)) return 0;
            unsigned n = s.saves.depth++;
            s.saves.reg[n] = (uint8_t)reg;
            s.saves.tag[n] = s.regs[reg];
            s.saves.known[n] = s.number_known[reg];
            s.saves.value[n] = s.number[reg];
            s.at++;
            continue;
        }
        if (s.saves.phase && op != 0x83 && op != 0x81)
            s.saves.phase = 0;
        if (op >= 0x50 && op <= 0x57) { /* PUSH register */
            unsigned reg = op - 0x50;
            if (s.depth == sizeof(s.stack) || !ordinary_reg(reg) ||
                !stack_push(&s, 2u) || !mark(v, at, 1)) return 0;
            s.stack[s.depth] = s.regs[reg];
            s.stack_known[s.depth] = s.number_known[reg];
            s.stack_values[s.depth] = s.number[reg];
            s.depth++;
            s.at++;
            continue;
        }
        if (s.frame == FRAME_ACTIVE && s.saves.depth && !s.depth &&
            op == 0x58 + s.saves.reg[s.saves.depth - 1u]) {
            if (s.stack_bytes < s.bp_bytes + 2u ||
                !mark(v, at, 1u)) return 0;
            unsigned n = --s.saves.depth;
            unsigned reg = s.saves.reg[n];
            s.regs[reg] = s.saves.tag[n];
            s.number_known[reg] = s.saves.known[n];
            s.number[reg] = s.saves.value[n];
            s.stack_bytes -= 2u;
            s.saves.phase = 0;
            s.at++;
            continue;
        }
        if (op >= 0x58 && op <= 0x5f) { /* POP register, not BP/SP */
            unsigned reg = op - 0x58;
            if (!s.depth || !ordinary_reg(reg) || s.stack_bytes < 2u ||
                !mark(v, at, 1)) return 0;
            s.stack_bytes -= 2u;
            s.depth--;
            s.regs[reg] = s.stack[s.depth];
            s.number_known[reg] = s.stack_known[s.depth];
            s.number[reg] = s.stack_values[s.depth];
            s.at++;
            continue;
        }
        if (op == 0x01 || op == 0x03 || op == 0x09 || op == 0x0b ||
            op == 0x21 || op == 0x23 || op == 0x29 || op == 0x2b ||
            op == 0x31 || op == 0x33) {
            if (remaining < 2 || (p[1] & 0xc0u) != 0xc0u ||
                !mark(v, at, 2)) return 0;
            unsigned reg = (p[1] >> 3) & 7u, rm = p[1] & 7u;
            unsigned dst = (op & 2u) ? reg : rm, src = (op & 2u) ? rm : reg;
            unsigned group = (op & 0xf8u) == 0x00 ? 0 :
                             (op & 0xf8u) == 0x08 ? 1 :
                             (op & 0xf8u) == 0x20 ? 4 :
                             (op & 0xf8u) == 0x28 ? 5 : 6;
            if (!ordinary_reg(dst) || !ordinary_reg(src)) return 0;
            if (dst == src && (group == 5 || group == 6)) {
                s.number[dst] = 0;
                s.number_known[dst] = 1;
            } else if (s.number_known[dst] && s.number_known[src]) {
                s.number[dst] = (uint16_t)alu(group,s.number[dst],s.number[src],0xffffu);
            } else s.number_known[dst] = 0;
            s.regs[dst] = ARG_WORD;
            s.at += 2;
            continue;
        }
        /* Byte-register ALU: no memory form, no arithmetic on a segment selector. */
        if ((op == 0x00 || op == 0x02 || op == 0x08 || op == 0x0a ||
             op == 0x20 || op == 0x22 || op == 0x28 || op == 0x2a ||
             op == 0x30 || op == 0x32 || op == 0x38 || op == 0x3a) &&
            remaining >= 2) {
            if ((p[1] & 0xc0u) != 0xc0u || !mark(v, at, 2)) return 0;
            unsigned r = (p[1] >> 3) & 7u, rm = p[1] & 7u;
            unsigned dst = (op & 2u) ? r : rm, src = (op & 2u) ? rm : r;
            unsigned group = (op & 0xf8u) == 0x00 ? 0 :
                             (op & 0xf8u) == 0x08 ? 1 :
                             (op & 0xf8u) == 0x20 ? 4 :
                             (op & 0xf8u) == 0x28 ? 5 :
                             (op & 0xf8u) == 0x30 ? 6 : 7;
            if (group != 7) {
                int known = s.number_known[parent8(dst)] && s.number_known[parent8(src)];
                unsigned val = known ? alu(group,byte8(&s,dst),byte8(&s,src),0xffu) : 0u;
                if (dst == src && (group == 5 || group == 6)) {
                    known = s.number_known[parent8(dst)]; val = 0;
                }
                set8(&s,dst,val,known);
            }
            s.at += 2;
            continue;
        }
        /* XCHG reg,reg and short XCHG AX,reg: preserve verified values/tags. */
        if (op == 0x87 || op == 0x86 || (op >= 0x91 && op <= 0x97)) {
            unsigned reg, rm;
            size_t length = op >= 0x91 ? 1u : 2u;
            if (remaining < length || !mark(v, at, length)) return 0;
            if (op >= 0x91) {reg = REG_AX; rm = op - 0x90;}
            else {
                if ((p[1] & 0xc0u) != 0xc0u) return 0;
                reg = (p[1] >> 3) & 7u; rm = p[1] & 7u;
            }
            if (op == 0x86) {
                int ka = s.number_known[parent8(reg)], kb = s.number_known[parent8(rm)];
                unsigned a = byte8(&s,reg), b = byte8(&s,rm);
                if (reg != rm) {
                    set8(&s,reg,b,kb);
                    set8(&s,rm,a,ka);
                }
            } else {
                if (!ordinary_reg(reg) || !ordinary_reg(rm)) return 0;
                uint8_t tag = s.regs[reg], known = s.number_known[reg];
                uint16_t val = s.number[reg];
                s.regs[reg] = s.regs[rm]; s.number_known[reg] = s.number_known[rm];
                s.number[reg] = s.number[rm];
                s.regs[rm] = tag; s.number_known[rm] = known; s.number[rm] = val;
            }
            s.at += length;
            continue;
        }

        if (op == 0x8c || op == 0x89 || op == 0x8b ||
            op == 0x31 || op == 0x33) {
            if (remaining < 2 || (p[1] & 0xc0) != 0xc0 ||
                !mark(v, at, 2)) return 0;
            unsigned reg = (p[1] >> 3) & 7u;
            unsigned rm = p[1] & 7u;
            if (op == 0x8c) { /* MOV r16,DS */
                if (reg != 3 || !ordinary_reg(rm)) return 0;
                s.regs[rm] = ARG_DS;
                s.number_known[rm] = 0;
            } else {
                unsigned dst = op == 0x8b || op == 0x33 ? reg : rm;
                unsigned src = op == 0x8b || op == 0x33 ? rm : reg;
                if (!ordinary_reg(dst) || !ordinary_reg(src)) return 0;
                if (op == 0x89 || op == 0x8b) {
                    s.regs[dst] = s.regs[src];
                    s.number_known[dst] = s.number_known[src];
                    s.number[dst] = s.number[src];
                } else {
                    s.regs[dst] = ARG_WORD;
                    s.number_known[dst] = dst == src; /* XOR r16,r16 */
                    if (dst == src) s.number[dst] = 0;
                }
            }
            s.at += 2;
            continue;
        }
        if (op >= 0x40 && op <= 0x4f) { /* INC/DEC r16 */
            unsigned reg = op & 7u;
            if (!ordinary_reg(reg) || !mark(v, at, 1)) return 0;
            s.regs[reg] = ARG_WORD;
            if (s.number_known[reg])
                s.number[reg] = (uint16_t)(s.number[reg] +
                                           (op < 0x48 ? 1 : -1));
            s.at++;
            continue;
        }
        if (op == 0x05 || op == 0x2d) { /* ADD/SUB AX,imm16 */
            if (remaining < 3 || !mark(v, at, 3)) return 0;
            s.regs[REG_AX] = ARG_WORD;
            if (s.number_known[REG_AX]) {
                uint16_t delta = read16(p + 1);
                if (op == 0x2d) delta = (uint16_t)-delta;
                s.number[REG_AX] = (uint16_t)(s.number[REG_AX] + delta);
            }
            s.at += 3;
            continue;
        }

        if (op != 0x9a || remaining < 5 || s.calls >= 32u ||
            !mark(v, at, 5))
            return 0;
        uint16_t offset = read16(p + 1);
        uint16_t selector = read16(p + 3);
        if (selector != v->thunk_selector ||
            (offset % WIN_NE_THUNK_BYTES) ||
            offset / WIN_NE_THUNK_BYTES >= v->thunk_count)
            return 0;
        unsigned words = args_for(v->service_ids[offset / WIN_NE_THUNK_BYTES]);
        if (words != s.depth) return 0;
        if ((words == 1 && s.stack[0] != ARG_WORD) ||
            (words == 2 && (s.stack[0] != ARG_DS || s.stack[1] != ARG_WORD)) ||
            (words == 4 && (s.stack[0] != ARG_DS || s.stack[1] != ARG_WORD ||
                            s.stack[2] != ARG_DS || s.stack[3] != ARG_WORD)) ||
            (words == 6 && (s.stack[0] != ARG_WORD || s.stack[1] != ARG_DS ||
                            s.stack[2] != ARG_WORD || s.stack[3] != ARG_DS ||
                            s.stack[4] != ARG_WORD || s.stack[5] != ARG_WORD)))
            return 0;
        if (s.stack_bytes < 2u * words) return 0;
        s.stack_bytes -= 2u * words;
        s.depth = 0; /* The thunk's RETF imm16 cleans PASCAL arguments. */
        /* Native services return through AX (and DX for DWORD/FAR results).
         * These registers cannot retain a previously proven DS selector. */
        s.regs[REG_AX] = ARG_WORD;
        s.regs[REG_DX] = ARG_WORD;
        s.number_known[REG_AX] = 0;
        s.number_known[REG_DX] = 0;
        s.calls++;
        s.at += 5;
    }
    return 0;
}

int win_ne_entry_calls_data(const uint8_t *p, size_t bytes, size_t ip,
                            uint16_t thunk_selector, const uint16_t *service_ids,
                            unsigned thunk_count, size_t data_limit,
                            unsigned *calls)
{
    if (!p || !calls || !service_ids || ip >= bytes || !thunk_selector ||
        !thunk_count || thunk_count > WIN_NE_GUEST_ACTIVE_THUNKS ||
        bytes > 65536u || data_limit > 65536u)
        return 0;
    verifier v = { 0 };
    v.code = p;
    v.entry = ip;
    v.end = bytes - ip > ENTRY_WINDOW ? ip + ENTRY_WINDOW : bytes;
    v.data_limit = data_limit;
    v.thunk_selector = thunk_selector;
    v.service_ids = service_ids;
    v.thunk_count = thunk_count;
    path_state initial = { 0 };
    initial.at = ip;
    for (unsigned i = 0; i < 8; i++) initial.regs[i] = ARG_WORD;
    if (!check_path(&v, initial) || !v.returns)
        return 0;
    *calls = v.max_calls; /* Worst-case bursts needed for any valid path. */
    return 1;
}

int win_ne_entry_calls(const uint8_t *p, size_t bytes, size_t ip,
                       uint16_t thunk_selector, const uint16_t *service_ids,
                       unsigned thunk_count, unsigned *calls)
{
    return win_ne_entry_calls_data(p, bytes, ip, thunk_selector, service_ids,
                                   thunk_count, 0, calls);
}
