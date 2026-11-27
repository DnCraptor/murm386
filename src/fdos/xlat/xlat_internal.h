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
#if defined(NO_PAGING) && defined(I386_MODE)
/* main() copies RAM_4_EXT code before init_hardware(); the runtime stack
 * floor is raised to __ram_4_ext_end__ after hardware initialization. */
#define XLAT_HOT_FN(name) __attribute__((section(".ram_4_ext_code.xlat." #name))) name
#else
#define XLAT_HOT_FN(name) __time_critical_func(name)
#endif
#endif

/* ---------------------------------------------------------------------- */
/* Limits                                                                 */
/* ---------------------------------------------------------------------- */

#ifndef XLAT_ARENA_SIZE
#define XLAT_ARENA_SIZE      (16u * 1024u)   /* code + source copies, allocated on first use */
#endif
#define XLAT_MAX_INSNS       63              /* storage capacity; bit 63 is "entry state" */
#ifndef XLAT_MAX_REGIONS
#define XLAT_MAX_REGIONS     384             /* region slots (entry_t.region, xlat_link_t.region: 16 bits) */
#endif
/* Regions without a loop (traces, XLAT_TRACES): a hot CALL or exit target
 * can use XLAT_LINK_MIN to connect native regions. XLAT_TRACE_MIN remains
 * the threshold for retries of burst-start requests. A REP string
 * instruction counts as XLAT_TRACE_MIN. */
#ifndef XLAT_TRACE_MIN
#define XLAT_TRACE_MIN       8
#endif
#ifndef XLAT_LINK_MIN
#define XLAT_LINK_MIN        1               /* even a short epilogue can connect hot regions */
#endif
#ifndef XLAT_EXIT_HOT
#define XLAT_EXIT_HOT        16              /* amortize compilation of short exit traces */
#endif
/* Link table: a region exit (BRANCH) looks its target up here, in the
 * trampoline's dispatcher, and goes on without returning to xlat_run().
 * xlat_run() fills an entry at each region entry it makes. */
#define XLAT_LINK_BITS       8
#define XLAT_LINK_SLOTS      (1u << XLAT_LINK_BITS)
#define XLAT_LINK_ENTRIES    (2u * XLAT_LINK_SLOTS) /* primary + secondary banks */
typedef struct xlat_link {
    uint32_t key;          /* cs << 16 | ip */
    uint32_t code;         /* entry label address | 1 */
    uint32_t epoch;        /* rt->epoch it is good for */
    uint16_t charge;       /* budget charged on entry: XLAT_ENTRY_COST + region fn */
    uint16_t region;       /* the dispatcher reads it at offset 14 (LDRH) */
} xlat_link_t;
_Static_assert(__builtin_offsetof(xlat_link_t, charge) == 12 &&
               __builtin_offsetof(xlat_link_t, region) == 14,
               "xlat_link_t layout is used by the dispatcher");
_Static_assert(sizeof(xlat_link_t) == 16, "xlat_link_t is indexed << 4 by generated code");
static inline uint32_t xlat_link_hash(uint32_t key)
{
    return (key ^ (key >> 7) ^ (key >> 16)) & (XLAT_LINK_SLOTS - 1u);
}
#ifndef XLAT_ENTRY_BITS
#define XLAT_ENTRY_BITS      11              /* entry points of all regions (their leaders) */
#endif
#define XLAT_ENTRY_SLOTS     (1 << XLAT_ENTRY_BITS)  /* power of two */
#define XLAT_HOT_SLOTS       64              /* power of two */
#define XLAT_EXTRA_MAX       12              /* entry points added to a cached region */
#define XLAT_HOT_THRESHOLD   2               /* burst-start samples before translating */
#ifndef XLAT_REJECT_RETRY
#define XLAT_REJECT_RETRY    32              /* samples of a rejected burst start before one more try */
#endif
#define XLAT_ENTRY_COST      8               /* budget charged per region entry */
#define XLAT_INTERP_RATIO    8               /* at most 1/8 of a region interpreted */
#define XLAT_SPAN_WINDOW     2048u           /* code within +-1 KiB of the entry or of a called function */
#define XLAT_COLD_ENTRIES    512             /* entries before a region is judged */
#define XLAT_COLD_SIDE_RATIO 4               /* cold too: more than 1 in this many instructions side */
#ifndef XLAT_ARENA_SRAM
#define XLAT_ARENA_SRAM      0
#endif

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
    uint32_t over;     /* bytes that did not fit (pos stays): the size still needed */
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
void arm_lsr_reg(arm_emit_t *e, int rd, int rn, int rm);   /* LSR.W rd, rn, rm; flags unchanged */
void arm_shift_reg(arm_emit_t *e, int sh, int rd, int rn, int rm);  /* SH_* rd, rn, rm (by rm & 0xff); flags unchanged */
void arm_shift_imm(arm_emit_t *e, int sh, int rd, int rm, int amount);  /* SH_* by 1..31; flags unchanged */
void arm_sbfx(arm_emit_t *e, int rd, int rn, int lsb, int width);
void arm_mul(arm_emit_t *e, int rd, int rn, int rm);               /* MUL.W: flags unchanged */
void arm_udiv(arm_emit_t *e, int rd, int rn, int rm);
void arm_sdiv(arm_emit_t *e, int rd, int rn, int rm);
void arm_mls(arm_emit_t *e, int rd, int rn, int rm, int ra);      /* rd = ra - rn * rm */
void arm_bfi(arm_emit_t *e, int rd, int rn, int lsb, int width);
void arm_ubfx(arm_emit_t *e, int rd, int rn, int lsb, int width);
void arm_uxth(arm_emit_t *e, int rd, int rm);
void arm_ls_imm(arm_emit_t *e, int kind, int rt, int rn, uint32_t imm12);
void arm_ls_reg(arm_emit_t *e, int kind, int rt, int rn, int rm);
void arm_strd(arm_emit_t *e, int rt, int rt2, int rn, uint32_t imm);  /* STRD rt, rt2, [rn, #imm] (imm: 4 * 0..255) */
void arm_push(arm_emit_t *e, uint16_t list);
void arm_pop(arm_emit_t *e, uint16_t list);
void arm_bx(arm_emit_t *e, int rm);
void arm_blx(arm_emit_t *e, int rm);
bool arm_bl(arm_emit_t *e, uint32_t target);
/* Placeholder for B / B<cond>.W, patched by arm_patch_branch(). */
uint32_t arm_branch_slot(arm_emit_t *e);
bool arm_patch_branch(arm_emit_t *e, uint32_t at, uint32_t target, int cond);

/* ---------------------------------------------------------------------- */
/* x86 decoder (x86_decode.c): 16-bit real-mode subset                    */
/* ---------------------------------------------------------------------- */

enum { XO_ALU = 1, XO_INCDEC, XO_MOV, XO_LEA, XO_XCHG, XO_NOP, XO_JCC, XO_JMP, XO_LOOP,
       XO_JCXZ, XO_PUSH, XO_POP, XO_CALL, XO_RET, XO_INTERP, XO_STR, XO_SHIFT, XO_MULDIV, XO_MISC, XO_SEG, XO_FAR, XO_IND, XO_STK, XO_FPU,
       XO_FLAG, XO_LOOPCC, XO_IO };

/* XO_FAR (real mode): CALL / JMP ptr16:16 (target = offset, a.imm = selector,
 * local: the selector is the region's CS, so it is a near transfer that
 * pushes CS too), RETF [b.imm], CALL / JMP m16:16 (b = the memory operand). */
enum { FAR_CALL = 0, FAR_JMP = 1, FAR_RET = 2, FAR_CALLM = 3, FAR_JMPM = 4 };

/* XO_IND: near CALL / JMP r/m16 (b). */
enum { IND_CALL = 0, IND_JMP = 1 };

/* XO_STK: PUSHA, POPA, ENTER b.imm, 0 / 1 (the nesting level), LEAVE. */
enum { STK_PUSHA = 0, STK_POPA = 1, STK_ENTER0 = 2, STK_ENTER1 = 3, STK_LEAVE = 4 };

/* XO_FLAG: they read or write the flags in rt (synchronized before). */
enum { FL_CLC = 0, FL_STC = 1, FL_CMC = 2, FL_LAHF = 3, FL_SAHF = 4, FL_PUSHF = 5, FL_POPF = 6 };

/* XO_LOOPCC: sub 0 LOOPNZ (E0), 1 LOOPZ (E1). */

/* XO_IO (xlat_rt_io): IN / OUT AL / AX (size 8 / 16) with the port in a.imm
 * or in DX (local), CLI, STI. */
enum { IO_IN = 0, IO_OUT = 1, IO_CLI = 2, IO_STI = 3 };

/* XO_FPU: an x87 instruction (D8..DF, sub = opcode - D8, sreg = the ModR/M
 * reg field, a.reg = rm for a register form, b = the memory operand, size =
 * its bytes) or FWAIT (sub = FPU_WAIT). Only forms fpu.c does without an
 * exception and without the CPU flags (no FCOMI, FCMOV, FLDENV, FSAVE...). */
#define FPU_WAIT 8

/* XO_SEG (real mode: selector = base >> 4; ES and DS only are written):
 * PUSH sreg (b.reg = sreg), POP ES/DS, MOV ES/DS <- r/m16 (b), MOV r/m16 <- sreg
 * (a), LES / LDS r16, m16:16 (a = register, b = memory). sreg: 0 ES 1 CS 2 SS 3 DS */
enum { SEG_PUSH = 0, SEG_POP = 1, SEG_LOAD = 2, SEG_STORE = 3, SEG_LES = 4, SEG_LDS = 5 };

/* XO_MISC operations (no arithmetic flags): */
enum { MISC_CBW = 0, MISC_CWD = 1, MISC_CLD = 2, MISC_STD = 3, MISC_NOT = 4,
       MISC_XLAT = 5,     /* b: [BX + AL] in DS or the override segment */
       MISC_MOVZX = 6, MISC_MOVSX = 7 };  /* r16 (a) <- r/m8 (b), size 8; with
                                             local: r32 <- r/m8 or r/m16 (size) */

/* XO_MULDIV: sub 4-7 as F6/F7 /reg (b = the r/m operand); 8 IMUL r, r/m, imm
 * (a = destination register, a.imm = the immediate, b = r/m). */
#define MULDIV_IMUL3 8

/* XO_STR operations (sub); rep: REP prefix (CX times, MOVS/STOS/LODS). */
enum { STR_MOVS = 0, STR_STOS = 1, STR_LODS = 2, STR_CMPS = 3, STR_SCAS = 4 };

/* XO_INTERP flags (in sub): the interpreter runs the instruction. */
#define XI_MAY_WRITE 1u   /* may write memory: recheck the region's code after it */
#define XI_NO_FALL   2u   /* never continues at the next instruction */
#define XI_SIDE      4u   /* helper flag: an access the region could not do (statistics) */

/* ALU operations, numbered as the x86 /reg field of 80-83; TEST is extra. */
enum { ALU_ADD = 0, ALU_OR = 1, ALU_ADC = 2, ALU_SBB = 3, ALU_AND = 4, ALU_SUB = 5,
       ALU_XOR = 6, ALU_CMP = 7, ALU_TEST = 8,
       ALU_NEG = 9 };    /* a = 0 - a: SUB flags with destination 0 */

/* Flag kinds as XRIP's lazy flags_op kind. */
enum { FK_NONE = 0, FK_ADD = 1, FK_SUB = 2, FK_LOGIC = 3 };

enum { OPD_NONE = 0, OPD_REG, OPD_MEM, OPD_IMM };

#define REG16_SP 4   /* SP lives in rt->gpr only: read and written there */

typedef struct x86_opd {
    uint8_t kind;      /* OPD_* */
    uint8_t reg;       /* register index (8-bit: AL..BH, 16/32-bit: AX..DI) */
    uint32_t imm;
} x86_opd_t;

typedef struct x86_insn {
    uint16_t ip;       /* offset in CS */
    uint16_t target;   /* branch target */
    uint8_t len;
    uint8_t op;        /* XO_* */
    uint8_t sub;       /* ALU_* / 0 INC 1 DEC / condition code */
    uint8_t size;      /* 8, 16 or 32 (66h: see x86_decode) */
    x86_opd_t a, b;    /* a = destination / first operand */
    /* memory operand (at most one per instruction) */
    uint8_t mem_mod, mem_rm, mem_seg;  /* mod 0..2; rm; 0 ES 1 CS 2 SS 3 DS */
    uint8_t mem_direct;                /* moffs / mod 0 rm 6: offset is disp only */
    uint8_t rep;                       /* XO_STR: 0, 1 REP / REPE (F3), 2 REPNE (F2) */
    uint8_t sreg;                      /* XO_SEG: segment register (0 ES 1 CS 2 SS 3 DS) */
    uint8_t local;                     /* XO_FAR CALL / JMP to the region's own CS;
                                          XO_SHIFT, MUL / IMUL: its flags are never seen
                                          (x86_translate: mark_dead_flags) */
    uint8_t pad;                       /* NOP / FWAIT bytes taken in front (x86_decode) */
    uint16_t disp;
} x86_insn_t;

/* Decode one instruction from code[0..15]. False: not in the supported subset. */
bool x86_decode(const uint8_t *code, uint16_t ip, x86_insn_t *out);
/* Any other instruction whose length is known: an XO_INTERP placeholder. */
bool x86_decode_interp(const uint8_t *code, uint16_t ip, x86_insn_t *out);
/* The first n NOP / FWAIT bytes of a padded instruction as one instruction
 * (FWAIT if one of them is), for an entry after them (discover). */
void x86_decode_pad(const uint8_t *code, uint16_t ip, unsigned n, x86_insn_t *out);

static inline bool x86_has_mem(const x86_insn_t *in)
{
    return in->a.kind == OPD_MEM || in->b.kind == OPD_MEM;
}

static inline bool x86_is_stack_op(const x86_insn_t *in)
{
    return in->op == XO_PUSH || in->op == XO_POP || in->op == XO_CALL || in->op == XO_RET ||
           (in->op == XO_SEG && (in->sub == SEG_PUSH || in->sub == SEG_POP)) ||
           (in->op == XO_FAR && in->sub != FAR_JMP && in->sub != FAR_JMPM) ||
           (in->op == XO_IND && in->sub == IND_CALL) || in->op == XO_STK ||
           (in->op == XO_FLAG && (in->sub == FL_PUSHF || in->sub == FL_POPF));
}

/* The x87 memory forms done natively: operand bytes, or 0 (XRIP does it).
 * *write: the instruction stores to memory. */
static inline int x86_fpu_mem_bytes(int op, int group, bool *write)
{
    static const int8_t bytes[8][8] = {
        {4, 4, 4, 4, 4, 4, 4, 4},       /* D8: arith m32real */
        {4, 0, 4, 4, 0, 2, 0, 2},       /* D9: FLD FST FSTP m32, FLDCW, FNSTCW */
        {4, 4, 4, 4, 4, 4, 4, 4},       /* DA: arith m32int */
        {4, 0, 4, 4, 0, 10, 0, 10},     /* DB: FILD FIST FISTP m32, FLD FSTP m80 */
        {8, 8, 8, 8, 8, 8, 8, 8},       /* DC: arith m64real */
        {8, 0, 8, 8, 0, 0, 0, 2},       /* DD: FLD FST FSTP m64, FNSTSW m16 */
        {2, 2, 2, 2, 2, 2, 2, 2},       /* DE: arith m16int */
        {2, 0, 2, 2, 10, 8, 10, 8},     /* DF: FILD FIST FISTP m16, FBLD, FILD m64, FBSTP, FISTP m64 */
    };
    *write = (op & 1) && (group == 2 || group == 3 || group == 7 || (op == 7 && group == 6));
    return bytes[op][group];
}

/* The x87 register forms done natively (fpu_exec1 without cpu_setexc and
 * without the CPU flags). */
static inline bool x86_fpu_reg_ok(int op, int group, int rm)
{
    switch (op) {
    case 0: case 4: case 6: return true;            /* arith st, st(i) */
    case 1:
        if (group == 2) return rm == 0;             /* FNOP */
        if (group == 4) return rm == 0 || rm == 1 || rm == 4 || rm == 5;
        if (group == 5) return rm != 7;
        return true;                                /* FLD FXCH FSTP, D9 F0..FF */
    case 2: return group == 5 && rm == 1;           /* FUCOMPP */
    case 3: return group == 4 && (rm == 2 || rm == 3); /* FNCLEX, FNINIT */
    case 5: return group <= 5;                      /* FFREE FXCH FST FSTP FUCOM(P) */
    default: return group <= 3 || (group == 4 && rm == 0); /* DF: ..., FNSTSW AX */
    }
}

/* Pushes a near return address: its return site is reached through RET. */
static inline bool x86_is_near_call(const x86_insn_t *in)
{
    return in->op == XO_CALL || (in->op == XO_IND && in->sub == IND_CALL);
}

/* Pushes a return address (near or far). */
static inline bool x86_is_call(const x86_insn_t *in)
{
    return x86_is_near_call(in) ||
           (in->op == XO_FAR && (in->sub == FAR_CALL || in->sub == FAR_CALLM));
}

/* CMPS / SCAS: string compares leave the SUB flags in rt. */
static inline bool x86_str_flags(const x86_insn_t *in)
{
    return in->op == XO_STR && (in->sub == STR_CMPS || in->sub == STR_SCAS);
}

/* Reads or writes guest memory (so it may leave by a side exit). */
static inline bool x86_accesses_mem(const x86_insn_t *in)
{
    return (x86_has_mem(in) && in->op != XO_LEA) || x86_is_stack_op(in);
}

static inline int x86_flag_kind(const x86_insn_t *in)
{
    if (in->op == XO_INCDEC)
        return in->sub ? FK_SUB : FK_ADD;
    if (in->op != XO_ALU)
        return FK_NONE;
    switch (in->sub) {
    case ALU_ADD: case ALU_ADC: return FK_ADD;
    case ALU_SUB: case ALU_SBB: case ALU_CMP: case ALU_NEG: return FK_SUB;
    default: return FK_LOGIC;
    }
}

/* DIV / IDIV: XRIP leaves the flags as they are. */
static inline bool x86_is_div(const x86_insn_t *in)
{
    return in->op == XO_MULDIV && (in->sub == 6 || in->sub == 7);
}

/* ADC/SBB: read CF and always store their flags (XRIP resolves them). */
static inline bool x86_is_carry_op(const x86_insn_t *in)
{
    return in->op == XO_ALU && (in->sub == ALU_ADC || in->sub == ALU_SBB);
}

/* ---------------------------------------------------------------------- */
/* Translator (x86_translate.c) and cache (xlat_cache.c)                  */
/* ---------------------------------------------------------------------- */

typedef struct xlat_entry_point {
    uint16_t ip;
    uint8_t mark;        /* loop head or the region's entry: XRIP branches there stop the burst */
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
    uint32_t need;                     /* XLAT_RJ_FULL: arena bytes the region needs (code + copies) */
    uint8_t reject;                    /* XLAT_RJ_* when no region was built */
    uint8_t n_insns, n_interp;         /* what discovery found (for the log) */
    uint32_t final_us;                 /* actual active time after closing discovery */
    uint8_t fn;                        /* a function region (no loop): the fewest native
                                          instructions an entry runs, else 0 */
} xlat_region_out_t;

/* Why a hot entry point got no region (XLAT_LOG "R" lines). */
enum {
    XLAT_RJ_NONE = 0,
    XLAT_RJ_COVERED,    /* inside a live region, not one of its entries */
    XLAT_RJ_START,      /* the entry instruction is not decoded */
    XLAT_RJ_OVERLAP,    /* overlapping instructions (no longer: the later one is left out) */
    XLAT_RJ_INTERP,     /* more than 1/XLAT_INTERP_RATIO interpreted */
    XLAT_RJ_NOLOOP,     /* no back edge in the reachable code */
    XLAT_RJ_EMIT,       /* stubs, fix-ups or branch range exceeded */
    XLAT_RJ_FULL,       /* does not fit an empty arena */
    XLAT_RJ_COUNT,
    XLAT_RJ_DEFER = XLAT_RJ_COUNT, /* transient: never passed to note_reject */
    XLAT_RJ_CHANGED              /* unpublished source changed */
};

/* A compiler slice is separate from the CPU instruction budget. */
#define XLAT_COMPILE_QUEUE 16u
/* Guest execution credit required to start or resume a job, not a time slice. */
#define XLAT_COMPILE_CREDIT_US 1800
#define XLAT_COMPILE_RATIO 12 /* microseconds of guest time per microsecond of compiler time */
#if defined(XLAT_HOST_TEST)
extern uint32_t xlat_test_compile_ticks;
#endif
static inline bool xlat_compile_irq(const xlat_rt_t *rt)
{
#if defined(XLAT_HOST_TEST)
    if (xlat_test_compile_ticks != UINT32_MAX) {
        if (!xlat_test_compile_ticks)
            return true;
        --xlat_test_compile_ticks;
    }
#endif
    /* Native device IRQs remain enabled. Only an actual deliverable guest
     * IRQ can suspend the compiler, never the execution burst deadline. */
    return rt->intr && *rt->intr;
}

/* Arena layout shared by translator and cache. */
/* Compact scalar SIDE descriptor, eight little-endian halfwords after BL:
 * ctl, ea, disp, imm_lo, imm_hi, next, ip, XI flags/len/continuation.
 * The last two words also form the existing interpreter call arguments.
 * ctl: op[3:0], log2(bytes)[5:4], register[8:6], immediate[9],
 * wide MOVZX/MOVSX destination[10], native continuation[15].
 * ea: r/m[2:0], segment[4:3], direct[5], XLAT[6].
 * frame is a private ARM stack snapshot of r4-r10, not rt->gpr. */
enum {
    SC_LOAD, SC_STORE, SC_ADD, SC_MOVZX, SC_MOVSX, SC_PUSH, SC_POP,
    SC_CALL, SC_RET, SC_RETF, SC_SEG_LOAD, SC_SEG_STORE, SC_SEG_PUSH, SC_SEG_POP
};
#define SC_IMM   0x0200u
#define SC_WIDE  0x0400u
#define SC_CONT  0x8000u
#define SC_EA_DIRECT 0x20u
#define SC_EA_XLAT   0x40u
#define SC_WORDS 8u
/* A compact shared PUSH/POP check redirects its failed path to a SIDE
 * stub, rather than to the interpreter. Only used in the check payload. */
#define SC_CHECK_REDIRECT 0x4000u
enum { SC_REFUSED, SC_RESUME, SC_DISPATCH, SC_STOP, SC_RESUME_ADD };
uint32_t xlat_rt_scalar(xlat_rt_t *rt, uint32_t *frame, const uint16_t *d);

typedef struct xlat_arena {
    uint8_t *base;
    uint32_t size;
    uint32_t code_top;     /* where xlat_translate puts the code */
    uint32_t data_bottom;  /* end of the free space: code + source copies stay below */
    uint32_t epilogue;     /* offset of the common exit sequence */
    uint32_t interp_thunk; /* offset of the common xlat_interp_one() call (after interp_call) */
    uint32_t dispatch;     /* offset of the link-table dispatcher (BRANCH exits) */
    uint32_t interp_call;  /* xlat_interp_one() call with its arguments after the BL */
    uint32_t scalar_call;  /* shared native scalar SIDE dispatcher */
    uint32_t exit_ip[2];   /* exits with the IP after the BL: BRANCH, BUDGET */
    uint32_t exit_dyn;     /* BRANCH exit, rt->ip stored */
    uint32_t wchk[4];      /* write checks: 1, 2, 4 bytes; up to 128 (r12 = last byte) */
    uint32_t rchk[2];      /* read check in upper memory: r12 = r0, r12 = last byte */
    uint32_t wside[4];     /* the same, running the instruction in XRIP when it fails */
    uint32_t spush, spop;  /* 16-bit PUSH / POP the same way */
    uint32_t back_slow;    /* back edge with the budget used up: the clock */
    uint32_t pend;         /* xlat_rt_pending() call keeping r0-r3 */
} xlat_arena_t;

/* Build the trampoline (entry/exit sequences) at the start of the arena. */
bool xlat_emit_trampoline(xlat_arena_t *arena);

/* Translate the region reachable from cs:ip. Code is appended to the arena.
 * extra: n_extra more entry points (marked), instructions of the region;
 * the translation fails (XLAT_RJ_START) if one is not.
 * trace_min: a region without a loop is built when every path from the
 * entry runs at least that many native instructions (0: loops only).
 * Returns false (arena unchanged) when no worthwhile region exists or the
 * arena is full (*full set). ctx_mem: xlat_translate_ctx_size() bytes of
 * working memory, word aligned (kept off the small core-0 stack and heap). */
size_t xlat_translate_ctx_size(void);
/* On DEFER retain the arena window, output and context. On FULL the next
 * step may use a new arena offset (only emission is restarted). */
bool xlat_translate_step(xlat_arena_t *arena, const xlat_rt_t *rt, uint16_t ip,
                         int trace_min, const uint16_t *extra, int n_extra,
                         xlat_region_out_t *out, bool *full, void *ctx_mem, bool resume);
bool xlat_translate(xlat_arena_t *arena, const xlat_rt_t *rt, uint16_t ip, int trace_min,
                    const uint16_t *extra, int n_extra,
                    xlat_region_out_t *out, bool *full, void *ctx_mem);

/* Host test: code bytes and counts by x86 instruction kind (XO_*), stub
 * kind and flag reconstruction (inside the others), producers that store
 * their flags (MAT: bytes of their lazy stores), see --size. */
enum { XLAT_SIZE_RECON = 32, XLAT_SIZE_BACK, XLAT_SIZE_SIDE, XLAT_SIZE_EXIT, XLAT_SIZE_MAT,
       XLAT_SIZE_N };
#if defined(XLAT_HOST_TEST)
extern uint32_t xlat_size_bytes[XLAT_SIZE_N], xlat_size_count[XLAT_SIZE_N];
#endif

typedef void (*xlat_tramp_fn)(xlat_rt_t *rt, uint32_t target);

void xlat_sync_code(void *start, uint32_t len);

#endif
