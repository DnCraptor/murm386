/* Bounded, straight-line Win16 entry validator.
 *
 * No guest instruction may address arbitrary memory, change a segment or
 * branch outside a relocated FAR import.  This is deliberately a *subset*
 * of 16-bit x86, not a general disassembler or a fault-safe NE sandbox.
 * A small abstract stack tracks the provenance of PASCAL arguments, so a
 * FAR pointer's selector must originate from DS rather than an immediate.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_entry.h"
#include "ne_guest.h"

/* Argument count in 16-bit stack words for supported native services. */
static unsigned args_for(uint16_t id)
{
    switch (id) {
    case 3: case 36: case 152: case 0x100d: return 0;
    case 90: return 2;                      /* lstrlen: offset + selector */
    case 88: case 89: return 4;             /* lstrcpy/lstrcat: two FAR pointers */
    case 0x1001: return 6;                  /* MessageBox: 12 bytes */
    case 0x10b3: case 0x10b4: return 1;     /* int */
    default: return 0xffu;
    }
}

enum { ARG_WORD = 1, ARG_DS = 2 };
enum {
    FRAME_NONE, FRAME_NEED_BP, FRAME_ACTIVE,
    FRAME_NEED_POP, FRAME_DONE
};
enum { REG_AX, REG_CX, REG_DX, REG_BX, REG_SP, REG_BP, REG_SI, REG_DI };

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

/* The register forms of MOV are safe only when neither operand is BP/SP.
 * BP/SP have their own tightly matched function prologue and epilogue. */
static int ordinary_reg(unsigned reg)
{
    return reg != REG_SP && reg != REG_BP;
}

int win_ne_entry_calls(const uint8_t *p, size_t bytes, size_t ip,
                       uint16_t thunk_selector, const uint16_t *service_ids,
                       unsigned thunk_count, unsigned *calls)
{
    if (!p || !calls || !service_ids || ip >= bytes || !thunk_selector ||
        !thunk_count || thunk_count > WIN_NE_GUEST_ACTIVE_THUNKS)
        return 0;

    unsigned count = 0, depth = 0, frame = FRAME_NONE;
    uint8_t stack[8] = { 0 };
    uint8_t regs[8] = { ARG_WORD, ARG_WORD, ARG_WORD, ARG_WORD,
                        ARG_WORD, ARG_WORD, ARG_WORD, ARG_WORD };
    size_t at = ip;
    while (at < bytes && at - ip < 256u) {
        uint8_t op = p[at];
        size_t remaining = bytes - at;
        if (op == 0x90) { at++; continue; } /* NOP */

        /* Conventional 16-bit far-function frame without local variables:
         * PUSH BP / MOV BP,SP ... MOV SP,BP / POP BP / RETF.
         * No other instruction may write BP or SP directly. */
        if (frame == FRAME_NEED_BP) {
            if (remaining < 2 ||
                !((op == 0x8b && p[at+1] == 0xec) ||
                  (op == 0x89 && p[at+1] == 0xe5)))
                return 0;
            frame = FRAME_ACTIVE;
            at += 2;
            continue;
        }
        if (frame == FRAME_NEED_POP) {
            if (op != 0x5d) return 0;
            frame = FRAME_DONE;
            at++;
            continue;
        }
        if (frame == FRAME_DONE && op != 0xcb)
            return 0;
        if (op == 0xcb) {                /* RETF */
            if (!count || depth || (frame != FRAME_NONE && frame != FRAME_DONE))
                return 0;
            *calls = count;
            return 1;
        }
        if (op == 0x55 && frame == FRAME_NONE && !count && !depth) {
            frame = FRAME_NEED_BP;    /* PUSH BP */
            at++;
            continue;
        }
        if (frame == FRAME_ACTIVE && remaining >= 2 &&
            ((op == 0x89 && p[at+1] == 0xec) ||
             (op == 0x8b && p[at+1] == 0xe5))) { /* MOV SP,BP */
            if (depth) return 0;
            frame = FRAME_NEED_POP;
            at += 2;
            continue;
        }
        if (op == 0x1e) {             /* PUSH DS */
            if (depth == sizeof(stack)) return 0;
            stack[depth++] = ARG_DS;
            at++;
            continue;
        }
        if (op == 0x68 || op == 0x6a) { /* PUSH imm16 / signed imm8 */
            size_t length = op == 0x68 ? 3u : 2u;
            if (remaining < length || depth == sizeof(stack)) return 0;
            stack[depth++] = ARG_WORD;
            at += length;
            continue;
        }
        if (op >= 0xb8 && op <= 0xbf) { /* MOV r16,imm16 */
            unsigned r = op - 0xb8;
            if (remaining < 3 || !ordinary_reg(r)) return 0;
            regs[r] = ARG_WORD;
            at += 3;
            continue;
        }
        if (op >= 0x50 && op <= 0x57) { /* PUSH r16 */
            unsigned r = op - 0x50;
            if (depth == sizeof(stack) || !ordinary_reg(r)) return 0;
            stack[depth++] = regs[r];
            at++;
            continue;
        }
        if (op >= 0x58 && op <= 0x5f) { /* POP r16, never BP/SP */
            unsigned r = op - 0x58;
            if (!depth || !ordinary_reg(r)) return 0;
            regs[r] = stack[--depth];
            at++;
            continue;
        }
        if (op == 0x8c || op == 0x89 || op == 0x8b ||
            op == 0x31 || op == 0x33) {
            if (remaining < 2 || (p[at+1] & 0xc0) != 0xc0)
                return 0;           /* register/register only, no memory */
            unsigned reg = (p[at+1] >> 3) & 7u;
            unsigned rm = p[at+1] & 7u;
            if (op == 0x8c) {       /* MOV r16,DS */
                if (reg != 3 || !ordinary_reg(rm)) return 0;
                regs[rm] = ARG_DS;
            } else {
                unsigned dst = op == 0x8b || op == 0x33 ? reg : rm;
                unsigned src = op == 0x8b || op == 0x33 ? rm : reg;
                if (!ordinary_reg(dst) || !ordinary_reg(src)) return 0;
                if (op == 0x89 || op == 0x8b)
                    regs[dst] = regs[src];
                else
                    regs[dst] = ARG_WORD; /* XOR r16,r16 */
            }
            at += 2;
            continue;
        }
        if (op >= 0x40 && op <= 0x4f) { /* INC/DEC r16 */
            unsigned r = op & 7u;
            if (!ordinary_reg(r)) return 0;
            regs[r] = ARG_WORD;
            at++;
            continue;
        }
        if (op == 0x05 || op == 0x2d) { /* ADD/SUB AX,imm16 */
            if (remaining < 3) return 0;
            regs[REG_AX] = ARG_WORD;
            at += 3;
            continue;
        }
        if (op != 0x9a || remaining < 5 || count >= 32u)
            return 0;
        uint16_t offset = read16(p + at + 1);
        uint16_t selector = read16(p + at + 3);
        if (selector != thunk_selector || (offset % WIN_NE_THUNK_BYTES) ||
            offset / WIN_NE_THUNK_BYTES >= thunk_count)
            return 0;
        unsigned words = args_for(service_ids[offset / WIN_NE_THUNK_BYTES]);
        if (words != depth) return 0;
        if ((words == 1 && stack[0] != ARG_WORD) ||
            (words == 2 && (stack[0] != ARG_DS || stack[1] != ARG_WORD)) ||
            (words == 4 && (stack[0] != ARG_DS || stack[1] != ARG_WORD ||
                            stack[2] != ARG_DS || stack[3] != ARG_WORD)) ||
            (words == 6 && (stack[0] != ARG_WORD || stack[1] != ARG_DS ||
                            stack[2] != ARG_WORD || stack[3] != ARG_DS ||
                            stack[4] != ARG_WORD || stack[5] != ARG_WORD)))
            return 0;
        depth = 0; /* The thunk's RETF imm16 drops its PASCAL arguments. */
        at += 5;
        count++;
    }
    return 0;
}
