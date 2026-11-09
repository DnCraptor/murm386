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

#define ENTRY_WINDOW 256u
#define ENTRY_VISIT_LIMIT 1024u
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
    near_call near[ENTRY_NEAR_CALL_DEPTH];
    uint8_t stack[8], stack_known[8];
    uint16_t stack_values[8];
    uint8_t regs[8];
    /* Exact 16-bit register values are tracked independently of the Win16
     * FAR-selector provenance tags. Unknown values cannot address memory. */
    uint16_t number[8];
    uint8_t number_known[8];
} path_state;

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
        if (op == 0xc9) { /* LEAVE = MOV SP,BP; POP BP */
            if (s.frame != FRAME_ACTIVE || s.depth ||
                s.bp_bytes < 2u || !mark(v, at, 1)) return 0;
            s.stack_bytes = s.bp_bytes - 2u;
            s.frame = FRAME_DONE;
            s.at++;
            continue;
        }
        if (op == 0xc3 || op == 0xc2) { /* RET / RET imm16 (near) */
            size_t length = op == 0xc2 ? 3u : 1u;
            if (!s.near_depth || s.depth || s.stack_bytes < 2u ||
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
            s.at = c.return_at;
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
            if (!s.calls || s.depth || s.near_depth || s.stack_bytes ||
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
        if (s.frame == FRAME_ACTIVE && remaining >= 2 &&
            ((op == 0x89 && p[1] == 0xec) ||
             (op == 0x8b && p[1] == 0xe5))) { /* MOV SP,BP */
            if (s.depth || !mark(v, at, 2)) return 0;
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

        /* CMP/TEST only change FLAGS. Restrict ModRM operands to registers
         * so their effective address cannot escape the known DATA region. */
        if (op == 0x3d || op == 0xa9) { /* CMP/TEST AX,imm16 */
            if (remaining < 3 || !mark(v, at, 3)) return 0;
            s.at += 3;
            continue;
        }
        if (op == 0x39 || op == 0x3b || op == 0x85) {
            if (remaining < 2 || (p[1] & 0xc0) != 0xc0 ||
                !mark(v, at, 2)) return 0;
            s.at += 2;
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

        /* ADD/SUB r16, sign-extended immediate. Only direct registers are
         * supported, never SP/BP or an unproven memory operand. */
        if (op == 0x83 || op == 0x81) {
            size_t length = op == 0x83 ? 3u : 4u;
            if (remaining < length || (p[1] & 0xc0u) != 0xc0u ||
                (((p[1] >> 3) & 7u) != 0u &&
                 ((p[1] >> 3) & 7u) != 5u) ||
                !ordinary_reg(p[1] & 7u) || !mark(v, at, length))
                return 0;
            unsigned reg = p[1] & 7u;
            uint16_t delta = op == 0x83 ? (uint16_t)(int16_t)(int8_t)p[2]
                                          : read16(p + 2);
            if (((p[1] >> 3) & 7u) == 5u) delta = (uint16_t)-delta;
            if (s.number_known[reg])
                s.number[reg] = (uint16_t)(s.number[reg] + delta);
            s.regs[reg] = ARG_WORD;
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
        /* Register-register ADD/SUB, commonly emitted for a C helper
         * that combines two parameters read via BP. No memory operands. */
        if (op == 0x01 || op == 0x03 || op == 0x29 || op == 0x2b) {
            if (remaining < 2 || (p[1] & 0xc0u) != 0xc0u ||
                !mark(v, at, 2)) return 0;
            unsigned reg = (p[1] >> 3) & 7u, rm = p[1] & 7u;
            unsigned dst = (op == 0x03 || op == 0x2b) ? reg : rm;
            unsigned src = (op == 0x03 || op == 0x2b) ? rm : reg;
            if (!ordinary_reg(dst) || !ordinary_reg(src)) return 0;
            if (s.number_known[dst] && s.number_known[src]) {
                uint16_t rhs = s.number[src];
                s.number[dst] = (uint16_t)(s.number[dst] +
                               ((op == 0x29 || op == 0x2b) ?
                                (uint16_t)-rhs : rhs));
            } else {
                s.number_known[dst] = 0;
            }
            s.regs[dst] = ARG_WORD;
            s.at += 2;
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
