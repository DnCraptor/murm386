/* Bounded Win16 entry verifier for the shared DOS/386 CPU.
 *
 * This is deliberately NOT a general NE sandbox. Only a small 16-bit
 * instruction subset is accepted. All reachable paths must terminate in
 * RETF, and control flow may only jump forward, preventing unbounded loops
 * while no protected-mode fault-recovery mechanism exists.
 *
 * In particular, direct DS memory accesses must stay below the reserved
 * Win16 stack area; no instruction may change DS/SS or make an indirect
 * control transfer. A symbolic stack records the origin of FAR selectors.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_entry.h"
#include "ne_guest.h"

#define ENTRY_WINDOW 256u
#define ENTRY_VISIT_LIMIT 1024u

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
    size_t at;
    unsigned calls, depth, frame;
    uint8_t stack[8];
    uint8_t regs[8];
} path_state;

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static int ordinary_reg(unsigned reg)
{
    return reg != REG_SP && reg != REG_BP;
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
            s.at += 2;
            continue;
        }
        if (s.frame == FRAME_NEED_POP) {
            if (op != 0x5d || !mark(v, at, 1)) return 0;
            s.frame = FRAME_DONE;
            s.at++;
            continue;
        }
        if (s.frame == FRAME_DONE && op != 0xcb)
            return 0;
        if (op == 0xcb) { /* RETF */
            if (!s.calls || s.depth ||
                (s.frame != FRAME_NONE && s.frame != FRAME_DONE) ||
                !mark(v, at, 1))
                return 0;
            if (s.calls > v->max_calls)
                v->max_calls = s.calls;
            v->returns++;
            return 1;
        }
        if (op == 0x55 && s.frame == FRAME_NONE &&
            !s.calls && !s.depth) { /* PUSH BP */
            if (!mark(v, at, 1)) return 0;
            s.frame = FRAME_NEED_BP;
            s.at++;
            continue;
        }
        if (s.frame == FRAME_ACTIVE && remaining >= 2 &&
            ((op == 0x89 && p[1] == 0xec) ||
             (op == 0x8b && p[1] == 0xe5))) { /* MOV SP,BP */
            if (s.depth || !mark(v, at, 2)) return 0;
            s.frame = FRAME_NEED_POP;
            s.at += 2;
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

        /* Absolute 16-bit DS accesses only. DS cannot be changed by any
         * verified instruction. Stack bytes above data_limit are excluded. */
        if (op == 0xa1 || op == 0xa3) { /* MOV AX,[disp16] / MOV [disp16],AX */
            if (remaining < 3 || !direct_data(v, read16(p + 1), 2) ||
                !mark(v, at, 3)) return 0;
            if (op == 0xa1) s.regs[REG_AX] = ARG_WORD;
            s.at += 3;
            continue;
        }
        if (op == 0xc6 || op == 0xc7) { /* MOV byte/word [disp16],imm */
            size_t length = op == 0xc6 ? 5u : 6u;
            if (remaining < length || p[1] != 0x06 ||
                !direct_data(v, read16(p + 2), op == 0xc6 ? 1 : 2) ||
                !mark(v, at, length)) return 0;
            s.at += length;
            continue;
        }

        if (op == 0x1e) { /* PUSH DS */
            if (s.depth == sizeof(s.stack) || !mark(v, at, 1)) return 0;
            s.stack[s.depth++] = ARG_DS;
            s.at++;
            continue;
        }
        if (op == 0x68 || op == 0x6a) { /* PUSH immediate */
            size_t length = op == 0x68 ? 3u : 2u;
            if (remaining < length || s.depth == sizeof(s.stack) ||
                !mark(v, at, length)) return 0;
            s.stack[s.depth++] = ARG_WORD;
            s.at += length;
            continue;
        }
        if (op >= 0xb8 && op <= 0xbf) { /* MOV r16,imm16 */
            unsigned reg = op - 0xb8;
            if (remaining < 3 || !ordinary_reg(reg) || !mark(v, at, 3)) return 0;
            s.regs[reg] = ARG_WORD;
            s.at += 3;
            continue;
        }
        if (op >= 0x50 && op <= 0x57) { /* PUSH register */
            unsigned reg = op - 0x50;
            if (s.depth == sizeof(s.stack) || !ordinary_reg(reg) ||
                !mark(v, at, 1)) return 0;
            s.stack[s.depth++] = s.regs[reg];
            s.at++;
            continue;
        }
        if (op >= 0x58 && op <= 0x5f) { /* POP register, not BP/SP */
            unsigned reg = op - 0x58;
            if (!s.depth || !ordinary_reg(reg) || !mark(v, at, 1)) return 0;
            s.regs[reg] = s.stack[--s.depth];
            s.at++;
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
            } else {
                unsigned dst = op == 0x8b || op == 0x33 ? reg : rm;
                unsigned src = op == 0x8b || op == 0x33 ? rm : reg;
                if (!ordinary_reg(dst) || !ordinary_reg(src)) return 0;
                s.regs[dst] = (op == 0x89 || op == 0x8b) ?
                              s.regs[src] : ARG_WORD;
            }
            s.at += 2;
            continue;
        }
        if (op >= 0x40 && op <= 0x4f) { /* INC/DEC r16 */
            unsigned reg = op & 7u;
            if (!ordinary_reg(reg) || !mark(v, at, 1)) return 0;
            s.regs[reg] = ARG_WORD;
            s.at++;
            continue;
        }
        if (op == 0x05 || op == 0x2d) { /* ADD/SUB AX,imm16 */
            if (remaining < 3 || !mark(v, at, 3)) return 0;
            s.regs[REG_AX] = ARG_WORD;
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
        s.depth = 0; /* The thunk's RETF imm16 cleans PASCAL arguments. */
        /* Native services return through AX (and DX for DWORD/FAR results).
         * These registers cannot retain a previously proven DS selector. */
        s.regs[REG_AX] = ARG_WORD;
        s.regs[REG_DX] = ARG_WORD;
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
