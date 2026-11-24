/*
 * murm386 xlat: internal interfaces of the translator subsystem.
 */
#ifndef FDOS_XLAT_INTERNAL_H
#define FDOS_XLAT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xlat.h"

/* Functions run once per burst or from generated code live in RAM; the
 * translator itself runs from flash. */
#if defined(XLAT_HOST_TEST)
#define XLAT_HOT_FN(name) name
#else
#include "pico.h"
#define XLAT_HOT_FN(name) __time_critical_func(name)
#endif

/* ---------------------------------------------------------------------- */
/* Limits                                                                 */
/* ---------------------------------------------------------------------- */

#ifndef XLAT_ARENA_SIZE
#define XLAT_ARENA_SIZE      (16u * 1024u)   /* code + source copies, lazily allocated */
#endif
#define XLAT_MAX_INSNS       63              /* per region; bit 63 of a set is "entry state" */
#define XLAT_MAX_REGIONS     32
#define XLAT_ENTRY_SLOTS     256             /* power of two */
#define XLAT_HOT_SLOTS       64              /* power of two */
#define XLAT_HOT_THRESHOLD   2               /* burst-start samples before translating */
#define XLAT_ENTRY_COST      8               /* budget charged per region entry */

/* ---------------------------------------------------------------------- */
/* Thumb-2 emitter (arm_emit.c)                                           */
/* ---------------------------------------------------------------------- */

enum { A_R0 = 0, A_R1, A_R2, A_R3, A_R4, A_R5, A_R6, A_R7, A_R8, A_R9, A_R10, A_R11,
       A_R12, A_SP, A_LR, A_PC };

enum { C_EQ = 0, C_NE, C_CS, C_CC, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT,
       C_GT, C_LE, C_AL };

enum { DP_AND = 0, DP_BIC = 1, DP_ORR = 2, DP_ORN = 3, DP_EOR = 4, DP_ADD = 8,
       DP_ADC = 10, DP_SBC = 11, DP_SUB = 13, DP_RSB = 14 };

enum { SH_LSL = 0, SH_LSR = 1, SH_ASR = 2, SH_ROR = 3 };

enum { LS_STR = 0, LS_STRB, LS_STRH, LS_LDR, LS_LDRB, LS_LDRH };

typedef struct arm_emit {
    uint8_t *buf;
    uint32_t pos;      /* byte offset of the next instruction */
    uint32_t cap;      /* bytes available */
    bool full;
} arm_emit_t;

void arm_e16(arm_emit_t *e, uint16_t hw);
void arm_e32(arm_emit_t *e, uint16_t hw1, uint16_t hw2);
int  arm_thumb_imm(uint32_t value);  /* modified immediate imm12, or -1 */

/* Data processing, register second operand with an immediate shift. */
void arm_dp_reg(arm_emit_t *e, int op, int s, int rd, int rn, int rm, int sh, int amount);
/* Data processing, modified immediate. Returns false when not encodable. */
bool arm_dp_imm(arm_emit_t *e, int op, int s, int rd, int rn, uint32_t imm);
void arm_addw(arm_emit_t *e, int rd, int rn, uint32_t imm12);
void arm_subw(arm_emit_t *e, int rd, int rn, uint32_t imm12);
void arm_movw(arm_emit_t *e, int rd, uint32_t imm16);
void arm_movt(arm_emit_t *e, int rd, uint32_t imm16);
void arm_mov32(arm_emit_t *e, int rd, uint32_t value);  /* never touches flags */
void arm_mov(arm_emit_t *e, int rd, int rm);            /* MOV.W, flags unchanged */
void arm_lsl(arm_emit_t *e, int rd, int rm, int amount);/* flags unchanged */
void arm_lsr(arm_emit_t *e, int rd, int rm, int amount);/* flags unchanged */
void arm_bfi(arm_emit_t *e, int rd, int rn, int lsb, int width);
void arm_ubfx(arm_emit_t *e, int rd, int rn, int lsb, int width);
void arm_uxth(arm_emit_t *e, int rd, int rm);
void arm_ls_imm(arm_emit_t *e, int kind, int rt, int rn, uint32_t imm12);
void arm_ls_reg(arm_emit_t *e, int kind, int rt, int rn, int rm);
void arm_push(arm_emit_t *e, uint16_t list);
void arm_pop(arm_emit_t *e, uint16_t list);
void arm_bx(arm_emit_t *e, int rm);
void arm_blx(arm_emit_t *e, int rm);
/* Placeholder for B / B<cond>.W, patched by arm_patch_branch(). */
uint32_t arm_branch_slot(arm_emit_t *e);
bool arm_patch_branch(arm_emit_t *e, uint32_t at, uint32_t target, int cond);

/* ---------------------------------------------------------------------- */
/* x86 decoder (x86_decode.c): 16-bit real-mode subset                    */
/* ---------------------------------------------------------------------- */

enum { XO_ALU = 1, XO_INCDEC, XO_MOV, XO_LEA, XO_XCHG, XO_NOP, XO_JCC, XO_JMP, XO_LOOP,
       XO_JCXZ };

/* ALU operations, numbered as the x86 /reg field of 80-83; TEST is extra. */
enum { ALU_ADD = 0, ALU_OR = 1, ALU_AND = 4, ALU_SUB = 5, ALU_XOR = 6, ALU_CMP = 7,
       ALU_TEST = 8 };

/* Flag kinds as XRIP's lazy flags_op kind. */
enum { FK_NONE = 0, FK_ADD = 1, FK_SUB = 2, FK_LOGIC = 3 };

enum { OPD_NONE = 0, OPD_REG, OPD_MEM, OPD_IMM };

typedef struct x86_opd {
    uint8_t kind;      /* OPD_* */
    uint8_t reg;       /* register index (8-bit: AL..BH, 16-bit: AX..DI) */
    uint16_t imm;
} x86_opd_t;

typedef struct x86_insn {
    uint16_t ip;       /* offset in CS */
    uint16_t target;   /* branch target */
    uint8_t len;
    uint8_t op;        /* XO_* */
    uint8_t sub;       /* ALU_* / 0 INC 1 DEC / condition code */
    uint8_t size;      /* 8 or 16 */
    x86_opd_t a, b;    /* a = destination / first operand */
    /* memory operand (at most one per instruction) */
    uint8_t mem_mod, mem_rm, mem_seg;  /* mod 0..2; rm; 0 ES 1 CS 2 SS 3 DS */
    uint8_t mem_direct;                /* moffs / mod 0 rm 6: offset is disp only */
    uint16_t disp;
} x86_insn_t;

/* Decode one instruction from code[0..15]. False: not in the supported subset. */
bool x86_decode(const uint8_t *code, uint16_t ip, x86_insn_t *out);

static inline bool x86_has_mem(const x86_insn_t *in)
{
    return in->a.kind == OPD_MEM || in->b.kind == OPD_MEM;
}

static inline int x86_flag_kind(const x86_insn_t *in)
{
    if (in->op == XO_INCDEC)
        return in->sub ? FK_SUB : FK_ADD;
    if (in->op != XO_ALU)
        return FK_NONE;
    switch (in->sub) {
    case ALU_ADD: return FK_ADD;
    case ALU_SUB: case ALU_CMP: return FK_SUB;
    default: return FK_LOGIC;
    }
}

/* ---------------------------------------------------------------------- */
/* Translator (x86_translate.c) and cache (xlat_cache.c)                  */
/* ---------------------------------------------------------------------- */

typedef struct xlat_entry_point {
    uint16_t ip;
    uint32_t code_off;
} xlat_entry_point_t;

typedef struct xlat_region_out {
    uint32_t code_off, code_len;
    uint32_t lo, span;                 /* linear bytes covered by the x86 code */
    uint16_t n_entries;
    xlat_entry_point_t entries[XLAT_MAX_INSNS];
    uint16_t n_runs;
    struct { uint16_t ip, len; } runs[XLAT_MAX_INSNS];
    uint32_t data_size;                /* arena bytes the caller stores the runs in */
} xlat_region_out_t;

/* Arena layout shared by translator and cache. */
typedef struct xlat_arena {
    uint8_t *base;
    uint32_t size;
    uint32_t code_top;     /* code grows up from 0 */
    uint32_t data_bottom;  /* source copies grow down from size */
    uint32_t epilogue;     /* offset of the common exit sequence */
} xlat_arena_t;

/* Build the trampoline (entry/exit sequences) at the start of the arena. */
bool xlat_emit_trampoline(xlat_arena_t *arena);

/* Translate the region reachable from cs:ip. Code is appended to the arena.
 * Returns false (arena unchanged) when no worthwhile region exists or the
 * arena is full (*full set). */
bool xlat_translate(xlat_arena_t *arena, const xlat_rt_t *rt, uint16_t ip,
                    xlat_region_out_t *out, bool *full);

typedef void (*xlat_tramp_fn)(xlat_rt_t *rt, uint32_t target);

void xlat_sync_code(void *start, uint32_t len);

#endif
