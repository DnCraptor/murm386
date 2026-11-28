/*
 * murm386 xlat: x86 region -> Thumb-2 translation.
 *
 * A region is the set of supported instructions reachable from a hot entry
 * point by direct control flow, limited to XLAT_MAX_INSNS. It contains a
 * loop, or every path from the entry runs enough native instructions before
 * it can leave (a trace, xlat_translate's trace_min).
 *
 * Register convention inside a region:
 *   r4 AX, r5 CX, r6 DX, r7 BX, r8 BP, r9 SI, r10 DI (full 32-bit values;
 *   16-bit writes use BFI), SP stays in rt->sp (read and written there;
 *   the trampoline copies it from and back to rt->gpr[4]),
 *   r11 = xlat_rt_t *, r0-r3, r12, lr scratch.
 *
 * Flags. XRIP keeps the arithmetic flags lazily (flags_op/result/dst/src/cf);
 * rt holds a copy of that state. An instruction setting flags computes them
 * in ARM flags with its operands shifted to bit 31 (16-bit: << 16, 8-bit:
 * << 24), so a Jcc right after it branches on N/Z/C/V directly. Wherever the
 * x86 flags can be observed elsewhere (a region exit, a Jcc that cannot use
 * ARM flags) the lazy state must be in rt. That is done either by
 * reconstructing it in the exit stub from registers (the producer is the
 * instruction right before and its operands are still in registers), or by
 * storing it at the producer ("materializing"). A reaching-definitions pass
 * over the region decides which. A materializing producer stores only its
 * operands and kind, marked pending (XLAT_LAZY_PENDING): the result and CF
 * are worked out by xlat_rt_pending() where C reads the flags (helpers, the
 * interpreter, the end of xlat_run). Only the code that reads CF itself
 * (INC / DEC, ADC / SBB) needs it stored by the producers before it.
 *
 * Code shared by all regions (xlat_emit_trampoline): the write checks and
 * PUSH / POP (BL, with the interpreter's arguments after the BL), the exit
 * tails, the back-edge slow path, the dispatcher.
 *
 * Exits are always at an x86 instruction boundary with all registers written
 * back, so XRIP continues exactly there.
 */
#include <limits.h>
#include <string.h>

#include "xlat_internal.h"

#define NONE_BIT   (1ull << 63)
#define MAX_STUBS  128
#define MAX_FIXUPS 128

/* x86 16-bit register index -> ARM register; SP (4) is not cached. */
static const int8_t arm_reg16[8] = {A_R4, A_R5, A_R6, A_R7, -1, A_R8, A_R9, A_R10};

enum { ST_EXIT = 0, ST_BACK = 1, ST_SIDE = 2 };
enum { D_INSN = 0, D_STUB = 1 };

typedef struct {
    uint32_t site[2];
    uint8_t cond[2];
    uint8_t nsites;
    uint8_t type;        /* ST_* */
    uint8_t exit_kind;   /* XLAT_EXIT_* */
    int8_t recon;        /* producer to reconstruct, or < 0 */
    uint16_t ip;         /* exit IP */
    int16_t target;      /* ST_BACK: instruction to continue at */
    int16_t exit_stub;   /* ST_BACK: stub taken when the budget is used up */
    uint8_t budget_n;    /* ST_BACK: guest instructions charged (loop body length) */
    uint8_t dyn;         /* ST_EXIT: rt->ip already stored by the code */
    uint8_t mw;          /* ST_SIDE: the access may hit the region's code */
    int16_t insn;        /* ST_SIDE: the instruction to interpret */
    uint32_t pos;
} stub_t;

typedef struct {
    uint32_t site;
    int16_t insn;
    uint8_t cond;
} fixup_t;

typedef struct {
    uint8_t type;
    int16_t idx;
} dest_t;

typedef struct {
    const xlat_rt_t *rt;
    uint32_t cs_base;
    uint32_t lo, span;
    int n;
    x86_insn_t in[XLAT_MAX_INSNS];
    uint64_t in_set[XLAT_MAX_INSNS], out_set[XLAT_MAX_INSNS], cf_in[XLAT_MAX_INSNS];
    uint64_t cf_out[XLAT_MAX_INSNS];
    int8_t fall_idx[XLAT_MAX_INSNS], tgt_idx[XLAT_MAX_INSNS];
    uint8_t pred_head[XLAT_MAX_INSNS], pred_next[2 * XLAT_MAX_INSNS];
    int8_t pfp[XLAT_MAX_INSNS];
    uint8_t leader[XLAT_MAX_INSNS], mat[XLAT_MAX_INSNS], need_cf[XLAT_MAX_INSNS];
    uint8_t full[XLAT_MAX_INSNS];   /* mat: store the whole lazy state, not pending */
    uint8_t local_jcc[XLAT_MAX_INSNS];
    int16_t side_stub[XLAT_MAX_INSNS][2];
    uint32_t label[XLAT_MAX_INSNS];
    uint32_t queue[2 * XLAT_MAX_INSNS + 2];   /* root << 16 | ip */
    uint16_t deferred[XLAT_MAX_INSNS];        /* CALL targets, searched last */
    uint16_t seen[4 * XLAT_MAX_INSNS];
    int n_seen;
    stub_t stub[MAX_STUBS];
    int n_stubs;
    fixup_t fix[MAX_FIXUPS];
    int n_fix;
    struct { uint32_t pos; int16_t insn; } dfix[MAX_FIXUPS];   /* emit_checks: halfword offsets */
    int n_dfix;
    bool overflow;
    arm_emit_t e;
    uint32_t epilogue, dispatch, interp_call, exit_ip[2], exit_dyn, wchk[4], wside[4], rchk[2], back_slow, pend;
    uint32_t spush, spop, scalar_call[SC_PATH_COUNT];
    uint8_t trace_min;             /* a region without a loop may do (xlat_translate) */
    uint8_t fn;                    /* built so: min_native_path (>= trace_min), else 0 */
    uint8_t no_calls;              /* discover: leave called functions out */
    int n_interp;
    /* Persistent compiler cursors in the PSRAM context. */
    uint8_t variant;
#if !XLAT_STACKFUL
    uint8_t phase;
#endif
    bool discovering, flow_changed;
    uint32_t seal_at, final_us;   /* time in finalization, not time between slices */
    uint64_t plan_mat, plan_full, head_marks;
    uint8_t plan_pass;
    int plan_i, fix_i, dfix_i, entry_i;
    int head, tail, dhead, dtail, flow_i, emit_i, stub_pass, stub_i;
    uint32_t data_bytes;
    int n_source;
    struct { uint16_t ip; uint8_t len, b[16]; } source[XLAT_MAX_INSNS];
} tctx_t;

#if XLAT_STACKFUL
/* The PSP context preserves locals and PC at an arbitrary ARM instruction.
 * The firmware compiler has no software checkpoints or phase dispatch. */
#define compiler_checkpoint(rt) false
#define COMPILER_PHASE(c, p) ((void)0)
#define COMPILER_LABEL(name)
#else
enum { PH_DISCOVER, PH_FLOW, PH_PLAN, PH_RUNS, PH_EMIT_INIT, PH_EMIT, PH_STUBS, PH_FINISH };
#define compiler_checkpoint(rt) xlat_compile_irq(rt)
#define COMPILER_PHASE(c, p) ((c)->phase = (p))
#define COMPILER_LABEL(name) name:
#endif

static inline uint32_t compiler_clock(const xlat_rt_t *rt)
{
#if XLAT_STACKFUL
    return xlat_preempt_time(rt->clock);
#else
    return *rt->clock;
#endif
}

#if defined(XLAT_HOST_TEST)
uint32_t xlat_test_compile_ticks = UINT32_MAX;
#endif

/* ---------------------------------------------------------------------- */
/* Region discovery                                                       */
/* ---------------------------------------------------------------------- */

static int find_insn(const tctx_t *c, uint16_t ip)
{
    int lo = 0, hi = c->n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) >> 1;
        if (c->in[mid].ip == ip)
            return mid;
        if (c->in[mid].ip < ip)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static bool is_branch(const x86_insn_t *in)
{
    return in->op == XO_JCC || in->op == XO_JMP || in->op == XO_LOOP || in->op == XO_JCXZ ||
           in->op == XO_CALL || in->op == XO_LOOPCC || (in->op == XO_FAR && in->local);
}

/* Control reaches the next instruction directly. A CALL does not: its return
 * site is reached through RET and the dispatcher, so it is an entry point. */
static bool falls_through(const x86_insn_t *in)
{
    if (in->op == XO_INTERP)
        return !(in->sub & XI_NO_FALL);
    return in->op != XO_JMP && in->op != XO_CALL && in->op != XO_RET && in->op != XO_FAR &&
           in->op != XO_IND;
}

/* Leaves for an IP known only at run time (stored to rt->ip): RET, RETF,
 * CALL / JMP r/m, far transfers to another CS. */
static bool dyn_exit(const x86_insn_t *in)
{
    return in->op == XO_RET || in->op == XO_IND || (in->op == XO_FAR && !in->local);
}

/* After it the flags are in rt (a helper or the interpreter left them there,
 * or the instruction works on rt), as at an entry. */
static bool flags_in_rt(const x86_insn_t *in)
{
    return in->op == XO_INTERP ||
           (in->op == XO_IO && (in->sub == IO_STI || in->sub == IO_OUT)) ||
           (in->op == XO_SHIFT && !in->local) ||
           (in->op == XO_MULDIV && !in->local && !x86_is_div(in)) ||
           in->op == XO_FLAG || x86_str_flags(in);
}

static bool is_producer(const x86_insn_t *in)
{
    return x86_flag_kind(in) != FK_NONE;
}

static bool native_string(const x86_insn_t *in)
{
    return in->op == XO_STR && (in->sub == STR_MOVS || in->sub == STR_STOS);
}

/* Side operations that can complete with explicit operands, without
 * the general interpreter thunk. Multi-write and other RMW instructions
 * keep their old paths until all effects can be committed together. */
static bool native_scalar(const x86_insn_t *in)
{
    switch (in->op) {
    case XO_MOV: return x86_has_mem(in);
    case XO_STR: return in->sub == STR_LODS || x86_str_flags(in);
    case XO_ALU:
        return in->sub == ALU_ADD && in->a.kind == OPD_MEM &&
               (in->b.kind == OPD_REG || in->b.kind == OPD_IMM);
    case XO_MISC:
        return in->sub == MISC_XLAT ||
               ((in->sub == MISC_MOVZX || in->sub == MISC_MOVSX) && in->b.kind == OPD_MEM);
    case XO_PUSH: return in->b.kind == OPD_REG || in->b.kind == OPD_IMM;
    case XO_POP: return in->a.kind == OPD_REG;
    case XO_CALL: case XO_RET: return true;
    case XO_FAR: return in->sub == FAR_RET;
    case XO_SEG:
        return in->sub == SEG_PUSH || in->sub == SEG_POP ||
               (in->sub == SEG_LOAD && in->b.kind == OPD_MEM) ||
               (in->sub == SEG_STORE && in->a.kind == OPD_MEM);
    default: return false;
    }
}

static uint32_t string_ctl(const x86_insn_t *in)
{
    const unsigned size = in->size == 32 ? 2u : in->size == 16 ? 1u : 0u;
    return XLAT_XI_STRING | XI_SIDE |
           (in->sub == STR_STOS ? XLAT_XS_STOS : 0u) |
           (in->rep ? XLAT_XS_REP : 0u) |
           (size << XLAT_XS_SIZE_SHIFT) |
           ((uint32_t)in->mem_seg << XLAT_XS_SEG_SHIFT);
}

/* STI followed by an audited one-byte instruction. The successor keeps
 * its ordinary entry for branches which do not execute STI. */
static int sti_successor(const tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    if (in->op != XO_IO || in->sub != IO_STI || in->len != 1)
        return -1;
    const int j = find_insn(c, (uint16_t)(in->ip + 1u));
    if (j <= i)
        return -1;
    const x86_insn_t *p = &c->in[j];
    if (p->len != 1)
        return -1;
    if (p->op == XO_SEG && p->sub == SEG_POP && (p->sreg == 0 || p->sreg == 3))
        return j;
    if (p->op == XO_STR && !p->rep && (p->size == 8 || p->size == 16) &&
        (p->sub == STR_STOS || p->sub == STR_MOVS || p->sub == STR_LODS))
        return j;
    return -1;
}

static bool seen_before(tctx_t *c, uint16_t ip)
{
    for (int k = 0; k < c->n_seen; ++k)
        if (c->seen[k] == ip)
            return true;
    if (c->n_seen < (int)(sizeof(c->seen) / sizeof(c->seen[0])))
        c->seen[c->n_seen++] = ip;
    return false;
}

/* The region has a loop that runs without leaving it: a cycle through fall
 * through and branch edges reachable from the entry. A near CALL into the
 * region continues at its return site (RET there is dispatched inside the
 * region); a far CALL, a CALL / JMP r/m, RET / RETF or a CALL out of the
 * region leave it, and their return sites are entries, not loop paths. (A
 * branch backwards alone is not enough: a loop body that calls into another
 * CS exits on every pass.) */
static bool has_cycle(const tctx_t *c, int first);

/* The fewest native instructions any path from the entry runs before it can
 * leave the region (an exit edge, a RET / far / indirect transfer, an
 * instruction XRIP does: INT, a far transfer it decodes, ... may change CS
 * and leave). A REP string instruction counts XLAT_TRACE_MIN. */
static int min_native_path(const tctx_t *c, int first)
{
    int dist[XLAT_MAX_INSNS];
    for (int i = 0; i < c->n; ++i)
        dist[i] = INT_MAX;
    dist[first] = 0;
    int best = INT_MAX;
    for (int pass = 0; pass < c->n; ++pass) {   /* Bellman-Ford, n <= 63 */
        bool changed = false;
        for (int i = 0; i < c->n; ++i) {
            if (dist[i] == INT_MAX)
                continue;
            const x86_insn_t *in = &c->in[i];
            if (in->op == XO_INTERP) {
                /* Decoded helpers may leave the region. A REP OUTS can
                 * execute many elements without dispatching each one. */
                const int w = (in->sub & XLAT_XI_OUTS) && in->len == 2 ? XLAT_TRACE_MIN :
                              (in->sub & XLAT_XI_DIRECT_MASK) != 0;
                const int d = dist[i] + w;
                if (d < best)
                    best = d;
                continue;
            }
            const int w = in->op == XO_STR && in->rep ? XLAT_TRACE_MIN : 1;
            const int d = dist[i] + w;
            int succ[3], ns = 0;
            bool leaves = dyn_exit(in);
            if (falls_through(in)) {
                if (c->fall_idx[i] < 0) leaves = true; else succ[ns++] = c->fall_idx[i];
            }
            if (is_branch(in)) {
                if (c->tgt_idx[i] < 0) leaves = true; else succ[ns++] = c->tgt_idx[i];
            }
            if ((in->op == XO_CALL || in->op == XO_FAR) && c->tgt_idx[i] >= 0) {
                const int r = find_insn(c, (uint16_t)(in->ip + in->len));
                if (r >= 0) succ[ns++] = r;
            }
            if ((leaves || !ns) && d < best)
                best = d;
            for (int k = 0; k < ns; ++k)
                if (d < dist[succ[k]]) {
                    dist[succ[k]] = d;
                    changed = true;
                }
        }
        if (!changed)
            break;
    }
    return best;
}

static bool has_cycle(const tctx_t *c, int first)
{
    uint8_t color[XLAT_MAX_INSNS];         /* 0 new, 1 on the path, 2 done */
    int8_t stack[XLAT_MAX_INSNS];
    uint8_t next[XLAT_MAX_INSNS];          /* successor tried next */
    xlat_memset(color, 0, sizeof(color));
    int sp = 0;
    stack[sp++] = (int8_t)first;
    color[first] = 1;
    next[first] = 0;
    while (sp) {
        const int i = stack[sp - 1];
        const x86_insn_t *in = &c->in[i];
        int succ = -1;
        while (succ < 0 && next[i] < 3) {
            const int k = next[i]++;
            if (k == 0)
                succ = c->fall_idx[i];
            else if (k == 1)
                succ = c->tgt_idx[i];
            else if ((in->op == XO_CALL || in->op == XO_FAR) && c->tgt_idx[i] >= 0)
                succ = find_insn(c, (uint16_t)(in->ip + in->len));   /* CALL, local far CALL */
        }
        if (succ < 0) {
            color[i] = 2;
            --sp;
        } else if (color[succ] == 1) {
            return true;
        } else if (!color[succ]) {
            color[succ] = 1;
            next[succ] = 0;
            stack[sp++] = (int8_t)succ;
        }
    }
    return false;
}

/* XLAT_RJ_NONE when a region with a loop was found, else the reason.
 * Breadth first from the entry, within XLAT_SPAN_WINDOW of it; the code of
 * called functions (CALL, PUSH CS + CALL, a far CALL into this CS) comes
 * after that, each within the window of its own start, as long as room is
 * left: the loop itself is found first, and a loop whose calls do not fit
 * leaves the region on every pass (has_cycle rejects it). */
static int discover(tctx_t *c, uint16_t start, const xlat_image_t *image)
{
#if !XLAT_PRECOMPILE
    (void)image;
#endif
    const int qcap = (int)(sizeof(c->queue) / sizeof(c->queue[0]));
    const int dcap = (int)(sizeof(c->deferred) / sizeof(c->deferred[0]));
    if (!c->discovering) {
        c->head = c->tail = c->dhead = c->dtail = 0;
        c->queue[c->tail++] = (uint32_t)start << 16 | start;
        c->n = c->n_seen = c->n_source = 0;
        c->discovering = true;
    }
    for (;;) {
        const bool more = c->head < c->tail || (!c->no_calls && c->dhead < c->dtail);
        if (compiler_checkpoint(c->rt))
            return XLAT_RJ_DEFER;
        if (!more || c->n >= XLAT_MAX_INSNS)
            break;
        if (c->head == c->tail) {
            c->head = c->tail = 0;
            const uint16_t f = c->deferred[c->dhead++];
            c->queue[c->tail++] = (uint32_t)f << 16 | f;
        }
        const uint32_t item = c->queue[c->head++];
        const uint16_t ip = (uint16_t)item, root = (uint16_t)(item >> 16);
        if ((uint16_t)(ip - root + XLAT_SPAN_WINDOW / 2) >= XLAT_SPAN_WINDOW ||
            seen_before(c, ip))
            continue;
        const uint32_t linear = c->cs_base + ip;
        uint32_t bytes = 16u;
#if XLAT_PRECOMPILE
        if (image) {
            if (linear < image->lo || linear >= image->end)
                continue;
            if (image->end - linear < bytes)
                bytes = image->end - linear;
            if (0x10000u - ip < bytes)
                bytes = 0x10000u - ip;
        }
#endif
        if (!xlat_code_direct(linear, bytes))
            continue;
        x86_insn_t in;
        const int s = c->n_source;
        uint8_t *const snapshot = c->source[s].b;
        const uint8_t *const code = c->rt->mem + linear;
        if (bytes < 16u) {
            xlat_memcpy(snapshot, code, bytes);
            xlat_memset(snapshot + bytes, 0, 16u - bytes);
        } else {
            /* Four fixed-size loads/stores, including an unaligned guest IP.
             * A single 16-byte builtin may become __aeabi_memcpy under -Os. */
            for (unsigned j = 0; j < 16; j += 4) {
                uint32_t word;
                __builtin_memcpy(&word, code + j, 4);
                __builtin_memcpy(snapshot + j, &word, 4);
            }
        }
        if (!x86_decode(c->source[s].b, ip, &in) &&
            !x86_decode_interp(c->source[s].b, ip, &in))
            continue;
        if (!in.len || in.len > 15u || in.len > bytes)
            continue;
        c->source[s].ip = ip;
        c->source[s].len = (uint8_t)(in.len -
            (in.op == XO_INTERP && (in.sub & XLAT_XI_INT_IMM) != 0));
        c->n_source++;
        /* a far CALL / JMP into this CS is a near one that pushes CS too */
        if (in.op == XO_FAR && (in.sub == FAR_CALL || in.sub == FAR_JMP) && in.a.imm == c->rt->cs)
            in.local = 1;
        c->in[c->n++] = in;
        if ((falls_through(&in) || x86_is_call(&in)) && c->tail < qcap)
            c->queue[c->tail++] = (uint32_t)root << 16 | (uint16_t)(ip + in.len);
        if (is_branch(&in)) {
            if (x86_is_call(&in)) {
                if (c->dtail < dcap)
                    c->deferred[c->dtail++] = in.target;
            } else if (c->tail < qcap) {
                c->queue[c->tail++] = (uint32_t)root << 16 | in.target;
            }
        }
    }
    c->seal_at = c->rt->wall_deadline ? compiler_clock(c->rt) : 0;
    c->discovering = false;
    if (!c->n || c->in[0].ip != start)
        return XLAT_RJ_START;

    /* Sort by IP (insertion sort, n <= 63). */
    for (int i = 1; i < c->n; ++i) {
        x86_insn_t t = c->in[i];
        int j = i - 1;
        while (j >= 0 && c->in[j].ip > t.ip) {
            c->in[j + 1] = c->in[j];
            --j;
        }
        c->in[j + 1] = t;
    }
    for (int i = 0; i + 1 < c->n; ++i) {
        x86_insn_t *in = &c->in[i];
        const x86_insn_t *nx = &c->in[i + 1];
        if ((uint32_t)in->ip + in->len <= nx->ip)
            continue;
        /* An entry after the NOP / FWAIT bytes taken into an instruction
         * (90 D9 .., 90 9B: XRIP runs them one by one and a burst may end
         * between them): those bytes become an instruction of their own,
         * falling through to the entry. */
        if (in->pad && nx->ip > in->ip && nx->ip <= in->ip + in->pad &&
            (uint32_t)nx->ip + nx->len == (uint32_t)in->ip + in->len) {
            for (int s = 0; s < c->n_source; ++s)
                if (c->source[s].ip == in->ip) {
                    x86_decode_pad(c->source[s].b, in->ip,
                                   (unsigned)(nx->ip - in->ip), in);
                    break;
                }
            continue;
        }
        /* Other overlapping instructions (a jump into the middle of one,
         * data decoded as code): the one not at the entry is left out, and
         * the edges to it leave the region. */
        const int drop = nx->ip == start ? i : i + 1;
        memmove(&c->in[drop], &c->in[drop + 1], (size_t)(c->n - drop - 1) * sizeof(c->in[0]));
        c->n--;
        --i;    /* the instruction now after in */
    }

    /* Edges, leaders, loop check. */
    xlat_memset(c->leader, 0, sizeof(c->leader));
    const int first = find_insn(c, start);
    if (first < 0)
        return XLAT_RJ_START;
    c->leader[first] = 1;
    c->head_marks = 1ull << first;
    c->lo = UINT32_MAX;
    uint32_t hi = 0;
    for (int i = 0; i < c->n; ++i) {
        const x86_insn_t *in = &c->in[i];
        c->fall_idx[i] = falls_through(in) ? (int8_t)find_insn(c, (uint16_t)(in->ip + in->len)) : -1;
        c->tgt_idx[i] = is_branch(in) ? (int8_t)find_insn(c, in->target) : -1;
        if (c->tgt_idx[i] >= 0) {
            c->leader[c->tgt_idx[i]] = 1;
            if (c->tgt_idx[i] <= i)
                c->head_marks |= 1ull << c->tgt_idx[i];
        }
        if ((in->op == XO_STR && in->rep) ||
            (in->op == XO_INTERP && (in->sub & XLAT_XI_OUTS) && in->len == 2)) {
            c->leader[i] = 1;   /* restart after IRQ or partial REP */
            c->head_marks |= 1ull << i;
        }
        if (sti_successor(c, i) >= 0) {
            const int ret = find_insn(c, (uint16_t)(in->ip + 2u));
            if (ret >= 0) {
                c->leader[ret] = 1;
                c->head_marks |= 1ull << ret;
            }
        }
        if (x86_is_call(in) || (in->op == XO_INTERP && (in->sub & XLAT_XI_INT_MASK))) {
            const int ret = find_insn(c, (uint16_t)(in->ip + in->len));
            if (ret >= 0) {
                c->leader[ret] = 1;
                c->head_marks |= 1ull << ret;
            }
        }
        const uint32_t l = c->cs_base + in->ip;
        if (l < c->lo)
            c->lo = l;
        if (l + in->len > hi)
            hi = l + in->len;
    }
    c->span = hi - c->lo;
    /* Each instruction run by the interpreter costs a register spill and a
     * call: keep regions mostly native. */
    int interp = 0;
    for (int i = 0; i < c->n; ++i)
        interp += c->in[i].op == XO_INTERP && !(c->in[i].sub & XLAT_XI_DIRECT_MASK);
    c->n_interp = interp;
    if (interp * XLAT_INTERP_RATIO > c->n)
        return XLAT_RJ_INTERP;
    if (has_cycle(c, first))
        return XLAT_RJ_NONE;
    const int min = c->trace_min;
    if (min) {
        const int len = min_native_path(c, first);
        if (len >= min) {
            c->fn = (uint8_t)(len < 255 ? len : 255);
            return XLAT_RJ_NONE;
        }
    }
    return XLAT_RJ_NOLOOP;
}

#if XLAT_PRECOMPILE
static void image_target(xlat_image_t *image, uint16_t cs, uint16_t ip)
{
    const uint32_t linear = ((uint32_t)cs << 4) + ip;
    if (linear < image->lo || linear >= image->end || image->n == XLAT_IMAGE_ROOTS)
        return;
    const uint32_t key = (uint32_t)cs << 16 | ip;
    for (unsigned i = 0; i < image->n; ++i)
        if (image->keys[i] == key)
            return;
    image->keys[image->n++] = key;
}

/* Continue at direct edges left outside the region. If its entry was
 * rejected, also try the known loop heads and direct callees inside it. */
static void image_targets(const tctx_t *c, xlat_image_t *image, bool rejected)
{
    const uint16_t cs = c->rt->cs;
    for (int i = 0; i < c->n && image->n < XLAT_IMAGE_ROOTS; ++i) {
        const x86_insn_t *in = &c->in[i];
        if (in->op == XO_FAR && !in->local && (in->sub == FAR_CALL || in->sub == FAR_JMP))
            image_target(image, (uint16_t)in->a.imm, in->target);
        else if (is_branch(in) && (c->tgt_idx[i] < 0 ||
                 (rejected && (in->target <= in->ip || x86_is_call(in)))))
            image_target(image, cs, in->target);

        /* Also consider the return site of a call. */
        const uint16_t next = (uint16_t)(in->ip + in->len);
        if ((falls_through(in) || x86_is_call(in)) && find_insn(c, next) < 0)
            image_target(image, cs, next);
    }
}
#endif

/* ---------------------------------------------------------------------- */
/* Flag analysis                                                          */
/* ---------------------------------------------------------------------- */

/* The producer right before i on the only path into i, or -1. */
static int pred_fall_producer(const tctx_t *c, int i)
{
    if (c->leader[i] || i == 0)
        return -1;
    const x86_insn_t *p = &c->in[i - 1];
    if ((uint16_t)(p->ip + p->len) != c->in[i].ip || !falls_through(p) || !is_producer(p))
        return -1;
    return i - 1;
}

static bool is_sp(const x86_opd_t *o, int size)
{
    return o->kind == OPD_REG && size == 16 && o->reg == REG16_SP;
}

static bool recon_ok(const x86_insn_t *in)
{
    /* SP changes with every PUSH / POP / CALL / RET in between: no rebuild
     * from it (the producer stores its flags itself) */
    if (is_sp(&in->a, in->size) || is_sp(&in->b, in->size))
        return false;
    if (in->op == XO_INCDEC)
        return in->a.kind == OPD_REG;
    if (in->op != XO_ALU || x86_is_carry_op(in) || in->sub == ALU_NEG || in->a.kind != OPD_REG ||
        (in->b.kind != OPD_REG && in->b.kind != OPD_IMM))
        return false;
    if ((in->sub == ALU_ADD || in->sub == ALU_SUB) && in->b.kind == OPD_REG &&
        in->b.reg == in->a.reg)
        return false;   /* ADD/SUB x,x: the old value is gone */
    return true;
}

/* A shift or rotate that may go without flags: a register operand, not
 * RCL / RCR (they read CF). */
static bool dead_shift_ok(const x86_insn_t *x)
{
    return x->op == XO_SHIFT && x->a.kind == OPD_REG && x->sub != 2 && x->sub != 3;
}

/* MUL / IMUL (one operand or r16, r/m16, imm): its flags may be dropped. */
static bool dead_mul_ok(const x86_insn_t *x)
{
    return x->op == XO_MULDIV && !x86_is_div(x);
}

/* Shifts and multiplications whose flags nothing sees: the instructions
 * after them on their one path touch neither the flags nor memory (nor
 * leave) until an ALU instruction without a memory operand that writes all
 * of them and reads none.
 * Such an instruction is done here without flags (emit_shift_value,
 * emit_mul_value) and is no producer: the flags pass it unchanged
 * (dataflow). Nothing in between can observe them (a side exit, an exit
 * edge, a helper); the instruction itself may side-exit before it changes
 * anything (a memory operand of MUL: XRIP does it, flags included). */
static bool flags_dead_after(const tctx_t *c, int i)
{
    int k = i;
    for (int steps = 0; steps < 16; ++steps) {
        k = c->fall_idx[k];
        if (k < 0)
            return false;
        const x86_insn_t *x = &c->in[k];
        switch (x->op) {
        case XO_ALU:
            /* writes every arithmetic flag; one with a memory operand may
             * leave by its side exit, where XRIP would write them too, but
             * an exception it raises there (#GP at offset FFFF) would see
             * flags never computed */
            return !x86_is_carry_op(x) && !x86_has_mem(x);
        case XO_MOV:
        case XO_XCHG:
        case XO_LEA:
        case XO_NOP:
            if (x86_has_mem(x) && x->op != XO_LEA)
                return false;
            break;
        case XO_MISC:
            if (x86_has_mem(x) || x->sub == MISC_XLAT)
                return false;
            break;
        case XO_SHIFT:                        /* dead as well: the same ALU follows */
            if (!dead_shift_ok(x))
                return false;
            break;
        case XO_MULDIV:                       /* the same; DIV may leave (#DE) */
            if (!dead_mul_ok(x) || x->b.kind == OPD_MEM)
                return false;
            break;
        default:
            return false;
        }
    }
    return false;
}

static void mark_dead_flags(tctx_t *c)
{
    for (int i = 0; i < c->n; ++i) {
        x86_insn_t *x = &c->in[i];
        if (x->op == XO_SHIFT || x->op == XO_MULDIV)
            x->local = (dead_shift_ok(x) || dead_mul_ok(x)) && flags_dead_after(c, i);
    }
}

/* Each vertex has at most two outgoing edges. Build incoming lists once,
 * after overlap removal and edge construction, then reuse across flow slices. */
static void build_preds(tctx_t *c)
{
    xlat_memset(c->pred_head, 0xff, sizeof(c->pred_head));
    for (int p = c->n - 1; p >= 0; --p) {
        const int f = c->fall_idx[p], t = c->tgt_idx[p];
        if (f >= 0) {
            c->pred_next[2 * p] = c->pred_head[f];
            c->pred_head[f] = (uint8_t)(2 * p);
        }
        if (t >= 0 && t != f) {
            c->pred_next[2 * p + 1] = c->pred_head[t];
            c->pred_head[t] = (uint8_t)(2 * p + 1);
        }
    }
}

static bool dataflow(tctx_t *c)
{
    uint64_t *const cf_out = c->cf_out;
    for (;;) {
        for (; c->flow_i < c->n; ++c->flow_i) {
            if (compiler_checkpoint(c->rt))
                return false;
            const int i = c->flow_i;
            uint64_t in = c->leader[i] ? NONE_BIT : 0, cfin = in;
            for (unsigned edge = c->pred_head[i]; edge != 0xffu;
                 edge = c->pred_next[edge]) {
                const unsigned p = edge >> 1;
                in |= c->out_set[p];
                cfin |= cf_out[p];
            }
            const x86_insn_t *x = &c->in[i];
            /* after the interpreter or a helper the flags are in rt, as at
             * an entry */
            const bool in_rt = flags_in_rt(x);
            const uint64_t out = in_rt ? NONE_BIT : is_producer(x) ? 1ull << i : in;
            const uint64_t cfout = in_rt ? NONE_BIT
                                   : is_producer(x) && x->op != XO_INCDEC ? 1ull << i : cfin;
            if (in != c->in_set[i] || out != c->out_set[i] || cfin != c->cf_in[i] ||
                cfout != cf_out[i]) {
                c->in_set[i] = in;
                c->out_set[i] = out;
                c->cf_in[i] = cfin;
                cf_out[i] = cfout;
                c->flow_changed = true;
            }
        }
        if (!c->flow_changed)
            return true;
        c->flow_i = 0;
        c->flow_changed = false;
    }
}

/* How the lazy flags reach rt at a point where they may be observed:
 * -2 nothing to do (entry state still in rt), >= 0 reconstruct from that
 * producer, -1 the reaching producers store them themselves. */
static int sync_recon(const tctx_t *c, uint64_t reach, int p)
{
    if (!(reach & ~NONE_BIT))
        return -2;
    if (p >= 0 && reach == 1ull << p && recon_ok(&c->in[p]))
        return p;
    return -1;
}

static void plan_sync(tctx_t *c, uint64_t reach, int p)
{
    const int r = sync_recon(c, reach, p);
    if (r >= 0) {
        if (c->in[r].op == XO_INCDEC)
            c->need_cf[r] = 1;
    } else if (r == -1) {
        c->plan_mat |= reach & ~NONE_BIT;
    }
}

/* Local Jcc on ARM flags: up to two conditional steps, each to the target
 * (to_target) or over the rest (skip). */
typedef struct { uint8_t cond, to_target; } jstep_t;

/* Returns step count, -1 when the condition needs the lazy flags. */
static int jcc_map(int kind, bool incdec, int cc, jstep_t s[2])
{
    if (cc == 0xa || cc == 0xb)
        return -1;      /* parity */
    if (kind == FK_LOGIC) {
        switch (cc) {
        case 0x0: case 0x2: return 0;                                   /* OF=0, CF=0 */
        case 0x1: case 0x3: s[0] = (jstep_t){C_AL, 1}; return 1;
        case 0x4: case 0x6: s[0] = (jstep_t){C_EQ, 1}; return 1;
        case 0x5: case 0x7: s[0] = (jstep_t){C_NE, 1}; return 1;
        case 0x8: case 0xc: s[0] = (jstep_t){C_MI, 1}; return 1;
        case 0x9: case 0xd: s[0] = (jstep_t){C_PL, 1}; return 1;
        case 0xe: s[0] = (jstep_t){C_EQ, 1}; s[1] = (jstep_t){C_MI, 1}; return 2;
        default:  s[0] = (jstep_t){C_EQ, 0}; s[1] = (jstep_t){C_PL, 1}; return 2;
        }
    }
    const bool cf_cond = cc == 2 || cc == 3 || cc == 6 || cc == 7;
    if (incdec && cf_cond)
        return -1;      /* INC/DEC keep CF; ARM C is not it */
    switch (cc) {
    case 0x0: s[0] = (jstep_t){C_VS, 1}; return 1;
    case 0x1: s[0] = (jstep_t){C_VC, 1}; return 1;
    case 0x4: s[0] = (jstep_t){C_EQ, 1}; return 1;
    case 0x5: s[0] = (jstep_t){C_NE, 1}; return 1;
    case 0x8: s[0] = (jstep_t){C_MI, 1}; return 1;
    case 0x9: s[0] = (jstep_t){C_PL, 1}; return 1;
    case 0xc: s[0] = (jstep_t){C_LT, 1}; return 1;
    case 0xd: s[0] = (jstep_t){C_GE, 1}; return 1;
    case 0xe: s[0] = (jstep_t){C_LE, 1}; return 1;
    case 0xf: s[0] = (jstep_t){C_GT, 1}; return 1;
    default: break;
    }
    if (kind == FK_SUB) {   /* x86 CF = !C */
        static const uint8_t sub_cond[4] = {C_CC, C_CS, C_LS, C_HI};
        s[0] = (jstep_t){sub_cond[cc == 2 ? 0 : cc == 3 ? 1 : cc == 6 ? 2 : 3], 1};
        return 1;
    }
    switch (cc) {           /* ADD: x86 CF = C */
    case 0x2: s[0] = (jstep_t){C_CS, 1}; return 1;
    case 0x3: s[0] = (jstep_t){C_CC, 1}; return 1;
    case 0x6: s[0] = (jstep_t){C_CS, 1}; s[1] = (jstep_t){C_EQ, 1}; return 2;
    default:  s[0] = (jstep_t){C_CS, 0}; s[1] = (jstep_t){C_NE, 1}; return 2;
    }
}

static int edge_producer(const tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    if (is_producer(in))
        return i;
    /* these leave AX..DI alone, so a reconstruction after them is still valid */
    if (in->op == XO_JCC || in->op == XO_JMP || in->op == XO_JCXZ || in->op == XO_PUSH ||
        in->op == XO_CALL || in->op == XO_RET || in->op == XO_FAR || in->op == XO_IND)
        return c->pfp[i];
    return -1;
}

static bool edge_needs_sync(int i, int dest)
{
    return dest < 0 || dest <= i;   /* region exit, or back edge (budget exit) */
}

/* Producer a side exit of instruction i may reconstruct the flags from: none
 * for REP string moves, whose later elements leave after SI, DI and CX
 * changed (the reaching producers then store the flags themselves). */
static int side_producer(const tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    return in->op == XO_STR && in->rep ? -1 : c->pfp[i];
}

static bool plan(tctx_t *c)
{
    if (c->plan_pass == 0) {
        for (; c->plan_i < c->n; ++c->plan_i) {
            if (compiler_checkpoint(c->rt))
                return false;
            const int i = c->plan_i;
            const x86_insn_t *in = &c->in[i];
            c->pfp[i] = (int8_t)pred_fall_producer(c, i);
            c->local_jcc[i] = 0;
            if ((in->op == XO_JCC || in->op == XO_LOOPCC) && c->pfp[i] >= 0) {
                const x86_insn_t *p = &c->in[c->pfp[i]];
                jstep_t s[2];
                const int cc = in->op == XO_JCC ? in->sub : 4;
                c->local_jcc[i] = !x86_is_carry_op(p) &&
                                  jcc_map(x86_flag_kind(p), p->op == XO_INCDEC, cc, s) >= 0;
            }
            if (x86_accesses_mem(in) || in->op == XO_FPU || in->op == XO_IO || x86_is_div(in))
                plan_sync(c, c->in_set[i], side_producer(c, i));
            if (dyn_exit(in))
                plan_sync(c, c->out_set[i], edge_producer(c, i));
            if (flags_in_rt(in))
                plan_sync(c, c->in_set[i], c->pfp[i]);
            if ((in->op == XO_JCC || in->op == XO_LOOPCC) && !c->local_jcc[i])
                plan_sync(c, c->in_set[i], c->pfp[i]);
            const int ep = edge_producer(c, i);
            if (falls_through(in) && edge_needs_sync(i, c->fall_idx[i]))
                plan_sync(c, c->out_set[i], ep);
            if (is_branch(in) && edge_needs_sync(i, c->tgt_idx[i]))
                plan_sync(c, c->out_set[i], ep);
        }
        c->plan_i = 0;
        c->plan_pass = 1;
    }
    if (c->plan_pass == 1) {
        /* Only INC/DEC materialized by the first pass, reconstructed INC/DEC
         * and ADC/SBB require CF. Full CF materialization must not feed back
         * into that set (same order of dependencies as the original plan). */
        for (; c->plan_i < c->n; ++c->plan_i) {
            if (compiler_checkpoint(c->rt))
                return false;
            const int i = c->plan_i;
            const uint64_t bit = 1ull << i;
            const x86_insn_t *in = &c->in[i];
            if (x86_is_carry_op(in)) {
                c->plan_full |= bit;
                c->need_cf[i] = 1;
            } else if (in->op == XO_INCDEC && (c->plan_mat & bit)) {
                c->need_cf[i] = 1;
            }
            if (c->need_cf[i])
                c->plan_full |= c->cf_in[i] & ~NONE_BIT;
        }
        c->plan_mat |= c->plan_full;
        c->plan_i = 0;
        c->plan_pass = 2;
    }
    for (; c->plan_i < c->n; ++c->plan_i) {
        if (compiler_checkpoint(c->rt))
            return false;
        const int i = c->plan_i;
        c->mat[i] = (uint8_t)((c->plan_mat >> i) & 1u);
        c->full[i] = (uint8_t)((c->plan_full >> i) & 1u);
    }
    return true;
}

static void local_patch(tctx_t *c, uint32_t site, int cond);

/* The lazy state in rt may be pending (an entry state from the region the
 * dispatcher came from, or a producer of this one): complete it. Keeps
 * r0-r3; clobbers r12, lr and the ARM flags. */
static void emit_pending_check(tctx_t *c)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_LDRB, A_R12, A_R11, XLAT_RT_OP);
    arm_dp_imm(e, DP_AND, 1, A_PC, A_R12, XLAT_LAZY_PENDING);      /* TST */
    const uint32_t done = arm_branch_slot(e);
    if (!arm_bl(e, c->pend))
        c->overflow = true;
    local_patch(c, done, C_EQ);
}

/* ---------------------------------------------------------------------- */
/* Code generation helpers                                                */
/* ---------------------------------------------------------------------- */

static int new_stub(tctx_t *c, uint8_t type)
{
    if (c->n_stubs >= MAX_STUBS) {
        c->overflow = true;
        return 0;
    }
    stub_t *s = &c->stub[c->n_stubs];
    xlat_memset(s, 0, sizeof(*s));
    s->type = type;
    s->recon = -1;
    s->exit_stub = -1;
    s->target = -1;
    return c->n_stubs++;
}

static int exit_stub(tctx_t *c, uint16_t ip, uint8_t kind, int recon)
{
    const int k = new_stub(c, ST_EXIT);
    c->stub[k].ip = ip;
    c->stub[k].exit_kind = kind;
    c->stub[k].recon = (int8_t)(recon >= 0 ? recon : -1);
    return k;
}

/* Destination of the edge from instruction i to target_ip. */
static dest_t make_dest(tctx_t *c, int i, uint16_t target_ip)
{
    const int j = find_insn(c, target_ip);
    const int recon = sync_recon(c, c->out_set[i], edge_producer(c, i));
    if (j > i)
        return (dest_t){D_INSN, (int16_t)j};
    if (j >= 0) {
        const int ex = exit_stub(c, target_ip, XLAT_EXIT_BUDGET, recon);
        const int b = new_stub(c, ST_BACK);
        c->stub[b].target = (int16_t)j;
        c->stub[b].exit_stub = (int16_t)ex;
        c->stub[b].budget_n = (uint8_t)(i - j + 1);
        return (dest_t){D_STUB, (int16_t)b};
    }
    return (dest_t){D_STUB, (int16_t)exit_stub(c, target_ip, XLAT_EXIT_BRANCH, recon)};
}

static void add_site(tctx_t *c, int stub, uint32_t site, int cond)
{
    stub_t *s = &c->stub[stub];
    if (s->nsites >= 2) {
        c->overflow = true;
        return;
    }
    s->site[s->nsites] = site;
    s->cond[s->nsites] = (uint8_t)cond;
    s->nsites++;
}

static void emit_branch(tctx_t *c, dest_t d, int cond)
{
    const uint32_t site = arm_branch_slot(&c->e);
    if (d.type == D_STUB) {
        add_site(c, d.idx, site, cond);
        return;
    }
    if (c->n_fix >= MAX_FIXUPS) {
        c->overflow = true;
        return;
    }
    c->fix[c->n_fix++] = (fixup_t){site, d.idx, (uint8_t)cond};
}

static void local_patch(tctx_t *c, uint32_t site, int cond)
{
    if (!arm_patch_branch(&c->e, site, c->e.pos, cond))
        c->overflow = true;
}

/* Operand value shifted to bit 31 into rd. Memory uses the linear address in r0. */
/* Load / store of an operand of size bits. */
static int ls_load(int size)
{
    return size == 32 ? LS_LDR : size == 16 ? LS_LDRH : LS_LDRB;
}

static int ls_store(int size)
{
    return size == 32 ? LS_STR : size == 16 ? LS_STRH : LS_STRB;
}

static void get_shifted(tctx_t *c, const x86_opd_t *o, int size, int rd)
{
    arm_emit_t *e = &c->e;
    const int s = 32 - size;
    switch (o->kind) {
    case OPD_REG:
        if (size == 16 && o->reg == REG16_SP) {
            arm_ls_imm(e, LS_LDRH, rd, A_R11, XLAT_RT_SP);
            arm_lsl(e, rd, rd, 16);
        } else if (size >= 16) {
            arm_lsl(e, rd, arm_reg16[o->reg], s);
        } else if (o->reg < 4) {
            arm_lsl(e, rd, arm_reg16[o->reg], 24);
        } else {
            arm_ubfx(e, rd, arm_reg16[o->reg - 4], 8, 8);
            arm_lsl(e, rd, rd, 24);
        }
        break;
    case OPD_MEM:
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, ls_load(size), rd, A_LR, A_R0);
        if (s)
            arm_lsl(e, rd, rd, s);
        break;
    default:
        arm_mov32(e, rd, (uint32_t)o->imm << s);
        break;
    }
}

/* Register holding the operand in its low bits (may be the cached x86 register). */
static int get_raw(tctx_t *c, const x86_opd_t *o, int size, int scratch)
{
    arm_emit_t *e = &c->e;
    switch (o->kind) {
    case OPD_REG:
        if (size == 16 && o->reg == REG16_SP) {
            arm_ls_imm(e, LS_LDRH, scratch, A_R11, XLAT_RT_SP);
            return scratch;
        }
        if (size >= 16 || o->reg < 4)
            return arm_reg16[o->reg];
        arm_ubfx(e, scratch, arm_reg16[o->reg - 4], 8, 8);
        return scratch;
    case OPD_MEM:
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, ls_load(size), scratch, A_LR, A_R0);
        return scratch;
    default:
        arm_mov32(e, scratch, o->imm);
        return scratch;
    }
}

static void put_raw(tctx_t *c, const x86_opd_t *o, int size, int src)
{
    arm_emit_t *e = &c->e;
    if (o->kind == OPD_MEM) {
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, ls_store(size), src, A_LR, A_R0);
    } else if (size == 32) {
        if (src != arm_reg16[o->reg])
            arm_mov(e, arm_reg16[o->reg], src);
    } else if (size == 16 && o->reg == REG16_SP) {
        arm_ls_imm(e, LS_STRH, src, A_R11, XLAT_RT_SP);
    } else if (size == 16) {
        arm_bfi(e, arm_reg16[o->reg], src, 0, 16);
    } else if (o->reg < 4) {
        arm_bfi(e, arm_reg16[o->reg], src, 0, 8);
    } else {
        arm_bfi(e, arm_reg16[o->reg - 4], src, 8, 8);
    }
}

/* r0 = 16-bit effective address (offset only). */
static void emit_offset(tctx_t *c, const x86_insn_t *in)
{
    arm_emit_t *e = &c->e;
    static const int8_t base[8] = {A_R7, A_R7, A_R8, A_R8, A_R9, A_R10, A_R8, A_R7};
    static const int8_t index[8] = {A_R9, A_R10, A_R9, A_R10, -1, -1, -1, -1};
    const uint32_t disp = in->disp;
    if (in->mem_direct) {
        arm_mov32(e, A_R0, disp);
        return;
    }
    /* the base register, then the index and the displacement */
    int from = base[in->mem_rm];
    if (index[in->mem_rm] >= 0) {
        arm_dp_reg(e, DP_ADD, 0, A_R0, from, index[in->mem_rm], SH_LSL, 0);
        from = A_R0;
    }
    if (!disp) {
        if (from != A_R0) {
            arm_uxth(e, A_R0, from);
            return;
        }
    } else if (disp <= 0xfffu) {
        arm_addw(e, A_R0, from, disp);
    } else if (0x10000u - disp <= 0xfffu) {
        arm_subw(e, A_R0, from, 0x10000u - disp);
    } else {
        arm_movw(e, A_R12, disp);
        arm_dp_reg(e, DP_ADD, 0, A_R0, from, A_R12, SH_LSL, 0);
    }
    arm_uxth(e, A_R0, A_R0);
}

/* r0 = linear address; branches to the side exit when the access is not in
 * fast memory or (for writes) may touch translated x86 code (xlat_code_map;
 * bytes <= 16). */
static void emit_checks(tctx_t *c, int i, int bytes, bool write);

static void emit_mem_access(tctx_t *c, int i, int bytes, bool write)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    emit_offset(c, in);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * in->mem_seg);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    emit_checks(c, i, bytes, write);
}

/* The stub that runs instruction i in the interpreter when its access cannot
 * be done here: outside fast memory (mw 0), or onto the region's code (mw 1). */
static dest_t side_dest(tctx_t *c, int i, int mw)
{
    int k = c->side_stub[i][mw];
    if (k < 0 || c->stub[k].nsites >= 2) {
        k = new_stub(c, ST_SIDE);
        c->stub[k].ip = c->in[i].ip;
        c->stub[k].insn = (int16_t)i;
        c->stub[k].mw = (uint8_t)mw;
        c->stub[k].exit_kind = XLAT_EXIT_SIDE;
        const int r = sync_recon(c, c->in_set[i], side_producer(c, i));
        c->stub[k].recon = (int8_t)(r >= 0 ? r : -1);
        c->side_stub[i][mw] = (int16_t)k;
    }
    return (dest_t){D_STUB, (int16_t)k};
}

/* r0 = linear address of an access by instruction i. Uses r12, lr only. */
static bool emit_side_args(tctx_t *c, int i, uint32_t target, uint32_t xi);
static bool side_writes_more(const x86_insn_t *in);

static void emit_checks(tctx_t *c, int i, int bytes, bool write)
{
    arm_emit_t *e = &c->e;
    if (write) {
        /* The map covers starting addresses of writes up to 16 bytes.
         * Wider individual stores (x87) must not bypass a later marked
         * block just because their first block was unmarked. */
        if (bytes > 16) {
            emit_branch(c, side_dest(c, i, 1), C_AL);
            return;
        }
        /* the shared check (xlat_emit_trampoline): fast or upper memory,
         * and no translated x86 code of any region in the bytes
         * (xlat_code_map, then the exact gapchk) */
        const int k = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
        if (k == 3)
            arm_addw(e, A_R12, A_R0, (uint32_t)bytes - 1u);
        /* XRIP rechecks the region after it only if it wrote translated
         * code (xlat_region_still_valid) */
        if (emit_side_args(c, i, c->wside[k], XI_MAY_WRITE))
            return;
        if (!arm_bl(e, c->wchk[k]))
            c->overflow = true;
        emit_branch(c, side_dest(c, i, 1), C_CS);
        return;
    }
    /* the region is built for this fast memory size (region_valid); past
     * it, rchk: upper memory (rt->umb_room). r12 = the last byte. */
    const uint32_t size = c->rt->mem_size;
    const int last = bytes > 1 ? A_R12 : A_R0;
    if (bytes > 1)
        arm_addw(e, A_R12, A_R0, (uint32_t)bytes - 1u);
    if (arm_thumb_imm(size) >= 0) {
        arm_dp_imm(e, DP_SUB, 1, A_PC, last, size);
    } else {
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM_SIZE);
        arm_dp_reg(e, DP_SUB, 1, A_PC, last, A_LR, SH_LSL, 0);
    }
    if (bytes <= (int)XLAT_UMB_MARGIN + 1) {
        arm_e16(e, 0xbf28u);                                  /* IT CS */
        if (!arm_bl(e, c->rchk[bytes > 1]))                  /* BLCS: CC when it is there */
            c->overflow = true;
    }
    emit_branch(c, side_dest(c, i, 0), C_CS);
}

static void emit_lazy_values(tctx_t *c, int kind, int size, bool store_op);

/* Store the lazy flags of an operation: r2 = dst << s, r3 = src << s,
 * r1 = result << s. Clobbers r12, lr and the ARM flags. */
static void emit_lazy(tctx_t *c, int kind, int size, bool incdec, int p)
{
    arm_emit_t *e = &c->e;
    const uint32_t op = (uint32_t)(kind << 5 | (32 - size));
    if (incdec) {
        /* CF stays: lazy_cf is CF (xlat.h), unless the entry state is pending */
        if (c->cf_in[p] & NONE_BIT)
            emit_pending_check(c);
        emit_lazy_values(c, kind, size, true);
        return;
    }
    /* lazy_op and lazy_cf (adjacent bytes) in one STRH: op | CF << 8 */
    if (kind == FK_LOGIC) {
        arm_mov32(e, A_R12, op);
    } else {
        if (kind == FK_ADD)
            arm_dp_reg(e, DP_ADD, 1, A_PC, A_R2, A_R3, SH_LSL, 0);   /* CMN: C = carry */
        else
            arm_dp_reg(e, DP_SUB, 1, A_PC, A_R2, A_R3, SH_LSL, 0);   /* CMP: C = !borrow */
        arm_dp_reg(e, DP_SBC, 0, A_R12, A_R12, A_R12, SH_LSL, 0);     /* C ? 0 : ~0 */
        arm_dp_imm(e, DP_AND, 0, A_R12, A_R12, 0x100u);
        /* ADD: CF = C, the bit inverted; SUB: CF = borrow */
        if (!arm_dp_imm(e, kind == FK_ADD ? DP_EOR : DP_ORR, 0, A_R12, A_R12,
                        kind == FK_ADD ? 0x100u | op : op)) {
            arm_mov32(e, A_LR, kind == FK_ADD ? 0x100u | op : op);
            arm_dp_reg(e, kind == FK_ADD ? DP_EOR : DP_ORR, 0, A_R12, A_R12, A_LR, SH_LSL, 0);
        }
    }
    arm_ls_imm(e, LS_STRH, A_R12, A_R11, XLAT_RT_OP);
    emit_lazy_values(c, kind, size, false);
}

/* The lazy fields other than CF: r1 result, r2 dst, r3 src (all << s);
 * lazy_op too when store_op. */
static void emit_lazy_values(tctx_t *c, int kind, int size, bool store_op)
{
    arm_emit_t *e = &c->e;
    const int s = 32 - size;
    _Static_assert(XLAT_RT_DST == XLAT_RT_RESULT + 4 && XLAT_RT_SRC == XLAT_RT_DST + 4 &&
                   XLAT_RT_CF == XLAT_RT_OP + 1, "lazy flag fields stored together");
    if (kind == FK_LOGIC) {
        arm_strd(e, A_R1, A_R1, A_R11, XLAT_RT_RESULT);
        arm_ls_imm(e, LS_STR, A_R1, A_R11, XLAT_RT_SRC);
    } else {
        arm_strd(e, A_R1, A_R2, A_R11, XLAT_RT_RESULT);
        if (kind == FK_SUB) {
            /* XRIP keeps (~src) << shift */
            arm_dp_reg(e, DP_ORN, 0, A_R12, A_PC, A_R3, s ? SH_LSR : SH_LSL, s);   /* MVN r12, r3, LSR #s */
            if (s)
                arm_lsl(e, A_R12, A_R12, s);
            arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_SRC);
        } else {
            arm_ls_imm(e, LS_STR, A_R3, A_R11, XLAT_RT_SRC);
        }
    }
    if (store_op) {
        arm_mov32(e, A_R12, (uint32_t)(kind << 5 | s));
        arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_OP);
    }
}

#if defined(XLAT_HOST_TEST)
uint32_t xlat_size_bytes[XLAT_SIZE_N], xlat_size_count[XLAT_SIZE_N];
#define SIZE_NOTE(k, from) (xlat_size_bytes[k] += c->e.pos - (from), xlat_size_count[k]++)
#else
#define SIZE_NOTE(k, from) ((void)0)
#endif

static void emit_recon_body(tctx_t *c, int p);
/* Rebuild producer p's lazy flags from registers right after it ran. */
static void emit_recon(tctx_t *c, int p)
{
    const uint32_t from = c->e.pos;
    emit_recon_body(c, p);
    SIZE_NOTE(XLAT_SIZE_RECON, from);
    (void)from;
}

static void emit_recon_body(tctx_t *c, int p)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[p];
    const int size = in->size, s = 32 - size;
    const int kind = x86_flag_kind(in);
    if (in->op == XO_INCDEC) {
        get_shifted(c, &in->a, size, A_R1);
        arm_mov32(e, A_R3, 1u << s);
        arm_dp_reg(e, in->sub ? DP_ADD : DP_SUB, 0, A_R2, A_R1, A_R3, SH_LSL, 0);
        emit_lazy(c, kind, size, true, p);
        return;
    }
    switch (in->sub) {
    case ALU_ADD:
    case ALU_SUB:
        get_shifted(c, &in->a, size, A_R1);
        get_shifted(c, &in->b, size, A_R3);
        arm_dp_reg(e, in->sub == ALU_ADD ? DP_SUB : DP_ADD, 0, A_R2, A_R1, A_R3, SH_LSL, 0);
        break;
    case ALU_CMP:
    case ALU_TEST:
        get_shifted(c, &in->a, size, A_R2);
        get_shifted(c, &in->b, size, A_R3);
        arm_dp_reg(e, in->sub == ALU_CMP ? DP_SUB : DP_AND, 0, A_R1, A_R2, A_R3, SH_LSL, 0);
        break;
    default:
        get_shifted(c, &in->a, size, A_R1);
        break;
    }
    emit_lazy(c, kind, size, false, p);
}

/* r12 = CF from the lazy state in rt (0 or 1) at instruction i. Clobbers
 * the ARM flags. */
static void emit_carry_in(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    if (c->cf_in[i] & NONE_BIT)
        emit_pending_check(c);
    arm_ls_imm(e, LS_LDRB, A_R12, A_R11, XLAT_RT_CF);   /* CF, resolved or not (xlat.h) */
}

/* ADC/SBB on r2 = dst << s, r3 = src << s; r1 = result << s, flags stored.
 * The carry is brought into bit s through the low bits: ADC adds
 * (2^s - 1) and 1 below the operands, SBB subtracts 1 below the source. */
static void emit_carry_op(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int size = in->size, s = 32 - size;
    emit_carry_in(c, i);
    if (in->sub == ALU_ADC) {
        arm_lsl(e, A_LR, A_R12, s);
        arm_dp_reg(e, DP_SUB, 0, A_LR, A_LR, A_R12, SH_LSL, 0);
        arm_dp_reg(e, DP_ORR, 0, A_LR, A_LR, A_R2, SH_LSL, 0);
        arm_dp_reg(e, DP_ORR, 0, A_R12, A_R12, A_R3, SH_LSL, 0);
        arm_dp_reg(e, DP_ADD, 1, A_R1, A_LR, A_R12, SH_LSL, 0);
        arm_mov32(e, A_R12, 0);
        arm_dp_imm(e, DP_ADC, 0, A_R12, A_R12, 0);           /* CF = carry */
    } else {
        arm_dp_reg(e, DP_ORR, 0, A_R12, A_R12, A_R3, SH_LSL, 0);
        arm_dp_reg(e, DP_SUB, 1, A_R1, A_R2, A_R12, SH_LSL, 0);
        arm_mov32(e, A_R12, 0);
        arm_dp_imm(e, DP_ADC, 0, A_R12, A_R12, 0);
        arm_dp_imm(e, DP_EOR, 0, A_R12, A_R12, 1);           /* CF = borrow */
        arm_lsr(e, A_R1, A_R1, s);
        arm_lsl(e, A_R1, A_R1, s);
    }
    arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_CF);
    emit_lazy_values(c, x86_flag_kind(in), size, true);
}

/* In a guarded frame body r0 is a host pointer to the lowest local.
 * These operands use the proven range; all other forms retain their
 * original emitter. The frame matcher permits only 16-bit accesses. */
static void get_shifted_at(tctx_t *c, const x86_opd_t *o, int size, int rd, int off)
{
    if (off >= 0 && o->kind == OPD_MEM) {
        arm_ls_imm(&c->e, LS_LDRH, rd, A_R0, (uint32_t)off);
        arm_lsl(&c->e, rd, rd, 16);
    } else
        get_shifted(c, o, size, rd);
}

static void put_raw_at(tctx_t *c, const x86_opd_t *o, int size, int src, int off)
{
    if (off >= 0 && o->kind == OPD_MEM)
        arm_ls_imm(&c->e, LS_STRH, src, A_R0, (uint32_t)off);
    else
        put_raw(c, o, size, src);
}

static void emit_alu_at(tctx_t *c, int i, int off, bool discard_flags)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int size = in->size;
    const int kind = x86_flag_kind(in);
    const bool incdec = in->op == XO_INCDEC;
    const bool writes = incdec || (in->sub != ALU_CMP && in->sub != ALU_TEST);
    int dp;

    if (off < 0 && x86_has_mem(in))
        emit_mem_access(c, i, size / 8, in->a.kind == OPD_MEM && writes);

    if (in->op == XO_ALU && in->sub == ALU_NEG) {
        get_shifted_at(c, &in->a, size, A_R3, off);
        arm_mov32(e, A_R2, 0);
    } else {
        get_shifted_at(c, &in->a, size, A_R2, off);
    }
    if (x86_is_carry_op(in)) {
        get_shifted_at(c, &in->b, size, A_R3, off);
        emit_carry_op(c, i);
        arm_lsr(e, A_R12, A_R1, 32 - size);
        put_raw_at(c, &in->a, size, A_R12, off);
        return;
    }
    if (incdec) {
        arm_mov32(e, A_R3, 1u << (32 - size));
        dp = in->sub ? DP_SUB : DP_ADD;
    } else if (in->sub == ALU_NEG) {
        dp = DP_SUB;                        /* 0 - operand */
    } else {
        get_shifted_at(c, &in->b, size, A_R3, off);
        switch (in->sub) {
        case ALU_ADD: dp = DP_ADD; break;
        case ALU_OR:  dp = DP_ORR; break;
        case ALU_AND: case ALU_TEST: dp = DP_AND; break;
        case ALU_XOR: dp = DP_EOR; break;
        default:      dp = DP_SUB; break;   /* SUB, CMP */
        }
    }
    if (!discard_flags && c->mat[i]) {
        const uint32_t from = e->pos;
        (void)from;
        if (c->full[i]) {
            arm_dp_reg(e, dp, 0, A_R1, A_R2, A_R3, SH_LSL, 0);
            emit_lazy(c, kind, size, incdec, i);
        } else {
            /* pending (xlat_rt_pending): the operands and the kind only;
             * INC / DEC leave lazy_cf, which is CF unless the entry state
             * is pending */
            if (incdec && (c->cf_in[i] & NONE_BIT))
                emit_pending_check(c);
            if (kind == FK_LOGIC) {
                arm_dp_reg(e, dp, 0, A_R1, A_R2, A_R3, SH_LSL, 0);
                arm_strd(e, A_R1, A_R1, A_R11, XLAT_RT_DST);
            } else {
                arm_strd(e, A_R2, A_R3, A_R11, XLAT_RT_DST);
            }
            arm_mov32(e, A_R12, XLAT_LAZY_PENDING | (incdec ? XLAT_LAZY_KEEP_CF : 0u) |
                                    (uint32_t)(kind << 5 | (32 - size)));
            arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_OP);
        }
        SIZE_NOTE(XLAT_SIZE_MAT, from);
    }
    /* The flag-setting operation is last: a Jcc after it reads ARM flags. */
    arm_dp_reg(e, dp, 1, writes ? A_R1 : A_PC, A_R2, A_R3, SH_LSL, 0);
    if (writes) {
        arm_lsr(e, A_R12, A_R1, 32 - size);
        put_raw_at(c, &in->a, size, A_R12, off);
    }
}

static void emit_mov(tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    /* Stack thunks repeatedly write CD/CB even if the bytes are unchanged.
     * A same-value store into direct RAM cannot change a translation.
     * Keep the regular fast store and the checked fallback for actual
     * changes or non-direct memory. */
    if (in->a.kind == OPD_MEM && in->b.kind == OPD_IMM &&
        in->size == 8 && in->mem_seg == 2 /* SS */) {
        arm_emit_t *e = &c->e;
        emit_offset(c, in);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * in->mem_seg);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
        if (!arm_bl(e, c->wchk[0]))
            c->overflow = true;
        const uint32_t safe = arm_branch_slot(e);
        emit_checks(c, i, 1, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRB, A_R1, A_LR, A_R0);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R1, in->b.imm & 0xffu);
        emit_branch(c, side_dest(c, i, 1), C_NE);
        const uint32_t unchanged = arm_branch_slot(e);
        local_patch(c, safe, C_CC);
        const int r = get_raw(c, &in->b, 8, A_R1);
        put_raw(c, &in->a, 8, r);
        local_patch(c, unchanged, C_AL);
        return;
    }
    if (x86_has_mem(in))
        emit_mem_access(c, i, in->size / 8, in->a.kind == OPD_MEM);
    const int r = get_raw(c, &in->b, in->size, A_R1);
    put_raw(c, &in->a, in->size, r);
}

static void emit_xchg(tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    /* the memory form: r0 = address, checked as a write (it covers the read) */
    if (x86_has_mem(in))
        emit_mem_access(c, i, in->size / 8, true);
    const int ra = get_raw(c, &in->a, in->size, A_R1);
    if (ra != A_R1)
        arm_mov(&c->e, A_R1, ra);
    const int rb = get_raw(c, &in->b, in->size, A_R2);
    put_raw(c, &in->a, in->size, rb);
    put_raw(c, &in->b, in->size, A_R1);
}

/* r1 = (SP + delta) & 0xffff, r0 = SS base + r1. Uses r12. */
static void emit_stack_addr(tctx_t *c, int delta)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_LDRH, A_R1, A_R11, XLAT_RT_SP);
    if (delta < 0)
        arm_subw(e, A_R1, A_R1, (uint32_t)-delta);
    else if (delta > 0)
        arm_addw(e, A_R1, A_R1, (uint32_t)delta);
    if (delta)
        arm_uxth(e, A_R1, A_R1);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
}

static void emit_store_sp(tctx_t *c)
{
    arm_ls_imm(&c->e, LS_STRH, A_R1, A_R11, XLAT_RT_SP);
}

/* A transfer to the IP in register ipreg (16 bits, CS unchanged) whose
 * flags are in rt already: when it is the return site of a CALL in this
 * region (a leader), go on there (a back edge charges the budget). RET,
 * RETF into this CS, JMP r/m16 (a stack probe's JMP CX). Uses r12. */
static void emit_site_dispatch(tctx_t *c, int i, int ipreg)
{
    arm_emit_t *e = &c->e;
    for (int j = 0; j < c->n; ++j) {
        if (!x86_is_call(&c->in[j]))
            continue;
        const int site = find_insn(c, (uint16_t)(c->in[j].ip + c->in[j].len));
        if (site < 0)
            continue;
        arm_mov32(e, A_R12, c->in[site].ip);
        arm_dp_reg(e, DP_SUB, 1, A_PC, ipreg, A_R12, SH_LSL, 0);
        if (site > i) {
            emit_branch(c, (dest_t){D_INSN, (int16_t)site}, C_EQ);
        } else {
            const int ex = exit_stub(c, c->in[site].ip, XLAT_EXIT_BUDGET, -1);
            const int b = new_stub(c, ST_BACK);
            c->stub[b].target = (int16_t)site;
            c->stub[b].exit_stub = (int16_t)ex;
            c->stub[b].budget_n = (uint8_t)(i - site + 1);
            emit_branch(c, (dest_t){D_STUB, (int16_t)b}, C_EQ);
        }
    }
}

/* PUSH / CALL: the 16-bit value in register v is pushed; checks first, so a
 * side exit leaves SP alone. */
/* The interpreter's arguments for instruction i after a BL to a check that
 * calls XRIP itself when the access cannot be done here (wside, spush,
 * spop): the IP, XI_* flags, the offset of where the region goes on after
 * it. False (nothing emitted) when the side exit needs a flag
 * reconstruction or the next instruction lies before this one. */
static bool emit_side_args(tctx_t *c, int i, uint32_t target, uint32_t xi)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const bool cont = falls_through(in) && !is_producer(in) && c->fall_idx[i] >= 0;
    const bool scalar = native_scalar(in);
    const bool redirect = scalar && (target == c->spush || target == c->spop);
    if (native_string(in) || (scalar && !redirect) ||
        sync_recon(c, c->in_set[i], side_producer(c, i)) >= 0 || c->n_dfix >= MAX_FIXUPS ||
        (cont && c->fall_idx[i] <= i))
        return false;
    if (!arm_bl(e, target))
        c->overflow = true;
    arm_e16(e, in->ip);
    arm_e16(e, (uint16_t)(xi | XI_SIDE | (uint32_t)(in->len & 0xfu) << 8 |
                        (cont ? 0x8000u : 0u) | (redirect ? SC_CHECK_REDIRECT : 0u)));
    if (redirect || cont) {
        c->dfix[c->n_dfix].pos = e->pos;
        /* Negative index encodes a forward SIDE stub, not an insn label. */
        c->dfix[c->n_dfix++].insn = redirect ?
            (int16_t)(-1 - side_dest(c, i, target == c->spush).idx) : c->fall_idx[i];
    }
    arm_e16(e, 0);
    return true;
}

static void emit_push_value(tctx_t *c, int i, int v)
{
    arm_emit_t *e = &c->e;
    /* the shared push (xlat_emit_trampoline): value in r2, r3 kept */
    if (v != A_R2)
        arm_mov(e, A_R2, v);
    if (emit_side_args(c, i, c->spush, XI_MAY_WRITE))
        return;
    emit_stack_addr(c, -2);
    emit_checks(c, i, 2, true);
    arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
    arm_ls_reg(e, LS_STRH, v, A_LR, A_R0);
    emit_store_sp(c);
}

/* POP / RET: r2 = popped word, SP += 2 + release. */
static void emit_pop_value(tctx_t *c, int i, uint32_t release)
{
    arm_emit_t *e = &c->e;
    /* the shared pop (xlat_emit_trampoline): r2 = the word, r1 = SP */
    if (!release && emit_side_args(c, i, c->spop, side_writes_more(&c->in[i]) ? XI_MAY_WRITE : 0u))
        return;
    emit_stack_addr(c, 0);
    emit_checks(c, i, 2, false);
    arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
    arm_ls_reg(e, LS_LDRH, A_R2, A_LR, A_R0);
    if (release + 2u <= 0xfffu) {
        arm_addw(e, A_R1, A_R1, release + 2u);
    } else {
        arm_mov32(e, A_R12, release + 2u);
        arm_dp_reg(e, DP_ADD, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
    }
    arm_uxth(e, A_R1, A_R1);
    emit_store_sp(c);
}

/* Inline a forward, straight-line, flag-neutral near leaf. The simulated SP
 * is relative to the caller's SP; only balanced register/immediate pushes,
 * register pops and MOVs are admitted. BP-based loads require a known frame.
 * The real return-address and saved-register stores are retained, even though
 * the return target is known and the final SP equals its original value. */
static bool emit_leaf_call(tctx_t *c, int i)
{
    const x86_insn_t *call = &c->in[i];
    const int start = c->tgt_idx[i];
    const uint16_t next = (uint16_t)(call->ip + call->len);
    const int site = find_insn(c, next);
    if (call->size != 16 || start <= i || site != i + 1 || start <= site ||
        (c->in_set[i] & ~NONE_BIT & ~c->plan_mat))
        return false;

    int sp = -2, lo = -2, hi = 0, bp = 0, ret = -1;
    bool bp_known = false;
    int load_off[8];
    for (int j = start; j < c->n && j < start + 8; ++j) {
        const x86_insn_t *x = &c->in[j];
        if (x->size != 16)
            return false;
        if (x->op == XO_RET) {
            if (sp != -2 || x->b.imm || j == start)
                return false;
            ret = j;
            break;
        }
        if (c->fall_idx[j] != j + 1)
            return false;
        if (x->op == XO_PUSH) {
            if ((x->b.kind != OPD_REG && x->b.kind != OPD_IMM) ||
                is_sp(&x->b, 16))
                return false;
            sp -= 2;
            if (sp < lo) lo = sp;
        } else if (x->op == XO_POP) {
            if (x->a.kind != OPD_REG || is_sp(&x->a, 16) || sp >= -2)
                return false;   /* never pop the return address */
            sp += 2;
            if (x->a.reg == 5 /* BP */) bp_known = false;
        } else if (x->op == XO_MOV) {
            if (x->a.kind != OPD_REG || is_sp(&x->a, 16))
                return false;
            if (x->b.kind == OPD_MEM) {
                if (!bp_known || x->mem_direct || !x->mem_mod ||
                    x->mem_seg != 2 || x->mem_rm != 6)
                    return false;
                const int off = bp + (int16_t)x->disp;
                load_off[j - start] = off;
                if (off < lo) lo = off;
                if (off + 2 > hi) hi = off + 2;
            } else if (x->b.kind != OPD_REG && x->b.kind != OPD_IMM) {
                return false;
            }
            if (x->a.reg == 5 /* BP */) {
                bp_known = is_sp(&x->b, 16);
                bp = sp;
            }
        } else {
            return false;
        }
        if (hi - lo > 64)
            return false;
    }
    if (ret < 0 || sync_recon(c, c->out_set[ret], edge_producer(c, ret)) >= 0)
        return false;

    /* Guard before any guest mutation. Require an unwrapped offset range,
     * direct RAM and one zero map byte. The ordinary path handles all
     * other mappings, possible code writes, and independent entry points. */
    arm_emit_t *e = &c->e;
    uint32_t slow[4];
    arm_ls_imm(e, LS_LDRH, A_R3, A_R11, XLAT_RT_SP); /* original SP */
    arm_subw(e, A_R0, A_R3, (uint32_t)-lo);
    arm_addw(e, A_R1, A_R0, (uint32_t)(hi - lo - 1));
    arm_dp_reg(e, DP_ORR, 0, A_R12, A_R0, A_R1, SH_LSL, 0);
    arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R12, SH_LSR, 16);
    slow[0] = arm_branch_slot(e);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 8u);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    arm_dp_reg(e, DP_ADD, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
    const uint32_t limit = c->rt->mem_size < XLAT_CODE_LIMIT ? c->rt->mem_size : XLAT_CODE_LIMIT;
    if (!arm_dp_imm(e, DP_SUB, 1, A_PC, A_R1, limit)) {
        arm_mov32(e, A_R12, limit);
        arm_dp_reg(e, DP_SUB, 1, A_PC, A_R1, A_R12, SH_LSL, 0);
    }
    slow[1] = arm_branch_slot(e);
    arm_dp_reg(e, DP_EOR, 0, A_R2, A_R0, A_R1, SH_LSL, 0);
    arm_dp_reg(e, DP_ORR, 1, A_R2, A_PC, A_R2, SH_LSR, XLAT_CODE_BLOCK_SHIFT + 3);
    slow[2] = arm_branch_slot(e);
    arm_lsr(e, A_R2, A_R0, XLAT_CODE_BLOCK_SHIFT + 3);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_CODE_MAP);
    arm_ls_reg(e, LS_LDRB, A_R2, A_R12, A_R2);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R2, 0);
    slow[3] = arm_branch_slot(e);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_MEM);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);

    /* r0 = checked host pointer at SP + lo; r3 = original SP.
     * All guest register accesses are 16-bit, preserving high halves. */
    sp = -2;
    arm_mov32(e, A_R1, next);
    arm_ls_imm(e, LS_STRH, A_R1, A_R0, (uint32_t)(sp - lo));
    for (int j = start; j < ret; ++j) {
        const x86_insn_t *x = &c->in[j];
        if (x->op == XO_PUSH) {
            const int v = get_raw(c, &x->b, 16, A_R1);
            sp -= 2;
            arm_ls_imm(e, LS_STRH, v, A_R0, (uint32_t)(sp - lo));
        } else if (x->op == XO_POP) {
            arm_ls_imm(e, LS_LDRH, A_R1, A_R0, (uint32_t)(sp - lo));
            put_raw(c, &x->a, 16, A_R1);
            sp += 2;
        } else {
            int v;
            if (x->b.kind == OPD_MEM) {
                arm_ls_imm(e, LS_LDRH, A_R1, A_R0, (uint32_t)(load_off[j - start] - lo));
                v = A_R1;
            } else if (is_sp(&x->b, 16)) {
                arm_subw(e, A_R1, A_R3, (uint32_t)-sp);
                v = A_R1;
            } else {
                v = get_raw(c, &x->b, 16, A_R1);
            }
            put_raw(c, &x->a, 16, v);
        }
    }
    /* No writes can overlap the return word. No SP store is needed. Keep
     * exactly the original RET's backward-edge charge and exit IP. */
    emit_branch(c, make_dest(c, ret, next), C_AL);
    local_patch(c, slow[0], C_NE);
    local_patch(c, slow[1], C_CS);
    local_patch(c, slow[2], C_NE);
    local_patch(c, slow[3], C_NE);
    return true;
}

static void emit_stack_op(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    switch (in->op) {
    case XO_PUSH: {
        if (in->b.kind == OPD_MEM)
            emit_mem_access(c, i, 2, false);     /* reading first changes nothing */
        const int v = get_raw(c, &in->b, 16, A_R2);
        emit_push_value(c, i, v);
        break;
    }
    case XO_POP:
        if (in->a.kind == OPD_MEM) {
            /* both addresses are checked before SP or memory change */
            emit_mem_access(c, i, 2, true);
            arm_mov(e, A_R3, A_R0);
            emit_pop_value(c, i, 0);
            arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
            arm_ls_reg(e, LS_STRH, A_R2, A_LR, A_R3);
        } else {
            emit_pop_value(c, i, 0);
            put_raw(c, &in->a, 16, A_R2);
        }
        break;
    case XO_CALL:
        (void)emit_leaf_call(c, i); /* guarded prefix, followed by the original path */
        arm_mov32(e, A_R2, (uint16_t)(in->ip + in->len));
        emit_push_value(c, i, A_R2);
        emit_branch(c, make_dest(c, i, in->target), C_AL);
        break;
    default: {  /* RET: leave for the popped IP; the dispatcher continues there */
        /* The flags reach rt first, as at an exit: a return site is a leader
         * and expects them there. Return sites of CALLs in this region are
         * reached directly (a back edge charges the budget). The
         * reconstruction (r1-r3) comes before the pop: RET changes no
         * register it reads. */
        const int r = sync_recon(c, c->out_set[i], edge_producer(c, i));
        if (r >= 0)
            emit_recon(c, r);
        emit_pop_value(c, i, in->b.imm);
        emit_site_dispatch(c, i, A_R2);
        arm_ls_imm(e, LS_STR, A_R2, A_R11, XLAT_RT_IP);
        const int k = exit_stub(c, 0, XLAT_EXIT_BRANCH, -1);
        c->stub[k].dyn = 1;
        emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_AL);
        break;
    }
    }
}

#define GPR_LIST(x) x(A_R4, 0) x(A_R5, 4) x(A_R6, 8) x(A_R7, 12) x(A_R8, 20) x(A_R9, 24) x(A_R10, 28)

/* Run instruction i in XRIP (xlat_interp_one) through the common thunk
 * (xlat_emit_trampoline): registers written back and reloaded there. The
 * thunk leaves the region with rt as it stands if XRIP must take over (stop)
 * or, when cont is false, CS changed or the instruction did not fall
 * through, at its new IP (XLAT_EXIT_SIDE for XI_SIDE, else BRANCH).
 * Otherwise it returns and execution goes on after the code emitted here.
 * flags: XI_* bits passed on to the helper. */
static void emit_helper_call(tctx_t *c, int i, uint32_t flags, unsigned len, bool cont)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (!arm_bl(e, c->interp_call))
        c->overflow = true;
    arm_e16(e, in->ip);
    arm_e16(e, (uint16_t)((flags & 0x70ffu) | (uint32_t)(len & 0xfu) << 8 | (cont ? 0x8000u : 0u)));
}

static void emit_interp_call(tctx_t *c, int i, uint32_t flags, bool cont)
{
    emit_helper_call(c, i, flags, c->in[i].len, cont);
}

/* An instruction the translator does not handle: XRIP executes it, and the
 * region goes on when it fell through to the next instruction. */
static void emit_interp(tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
    if (r >= 0)
        emit_recon(c, r);
    emit_interp_call(c, i, in->sub & (XI_MAY_WRITE | XLAT_XI_DIRECT_MASK | XLAT_XI_OUTS_WORD),
                     !(in->sub & XI_NO_FALL));
}

static void emit_jcc(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (c->local_jcc[i]) {
        const x86_insn_t *p = &c->in[c->pfp[i]];
        jstep_t st[2];
        const int n = jcc_map(x86_flag_kind(p), p->op == XO_INCDEC, in->sub, st);
        if (n <= 0)
            return;     /* never taken */
        const dest_t d = make_dest(c, i, in->target);
        uint32_t skip = UINT32_MAX;
        for (int k = 0; k < n; ++k) {
            if (st[k].to_target)
                emit_branch(c, d, st[k].cond);
            else
                skip = arm_branch_slot(e);
        }
        if (skip != UINT32_MAX)
            local_patch(c, skip, st[0].cond);
        return;
    }
    const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
    if (r >= 0)
        emit_recon(c, r);
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R1, in->sub);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_cond);
    arm_blx(e, A_R12);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
    emit_branch(c, make_dest(c, i, in->target), C_NE);
}

/* Shift / rotate through xlat_rt_shift(): the lazy flags go to rt first,
 * the helper leaves its flags there. Memory operands are checked (side exit)
 * before anything changes. */
/* A register shift or rotate whose flags are dead (mark_dead_shifts): the
 * value only, the count as XRIP's (& 31; rotates modulo the size). */
static void emit_shift_value(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int size = in->size;
    const int v = get_raw(c, &in->a, size, A_R1);
    const bool rot = in->sub < 2;
    /* r1 = the operand as the shift needs it */
    if (rot) {
        arm_ubfx(e, A_R1, v, 0, size);
        arm_dp_reg(e, DP_ORR, 0, A_R1, A_R1, A_R1, SH_LSL, size);        /* x:x */
        if (size == 8)
            arm_dp_reg(e, DP_ORR, 0, A_R1, A_R1, A_R1, SH_LSL, 16);    /* x:x:x:x */
    } else if (in->sub == 5) {
        arm_ubfx(e, A_R1, v, 0, size);
    } else if (in->sub == 7) {
        arm_sbfx(e, A_R1, v, 0, size);
    } else if (v != A_R1) {
        arm_mov(e, A_R1, v);
    }
    const int sh = rot ? SH_ROR : in->sub == 5 ? SH_LSR : in->sub == 7 ? SH_ASR : SH_LSL;
    if (in->b.kind == OPD_IMM) {
        int n = (int)(in->b.imm & 31u);
        if (rot) {
            n &= size - 1;
            if (n && in->sub == 0)
                n = size - n;                 /* ROL n = ROR size - n */
        }
        if (!n)
            return;                           /* the value stays */
        arm_shift_imm(e, sh, A_R1, A_R1, n);
    } else {
        arm_dp_imm(e, DP_AND, 0, A_R2, A_R5, rot ? (uint32_t)size - 1u : 31u);   /* CL */
        if (rot && in->sub == 0)
            arm_dp_imm(e, DP_RSB, 0, A_R2, A_R2, (uint32_t)size);  /* ROR by size - n (or size) */
        arm_shift_reg(e, sh, A_R1, A_R1, A_R2);
    }
    put_raw(c, &in->a, size, A_R1);
}

static void emit_shift(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (in->local) {
        emit_shift_value(c, i);
        return;
    }
    const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
    if (r >= 0)
        emit_recon(c, r);
    const bool mem = in->a.kind == OPD_MEM;
    if (mem) {
        emit_mem_access(c, i, in->size / 8, true);
        arm_push(e, 0x0003u);                    /* r0 (address), r1: 8-byte aligned */
    }
    const int v = get_raw(c, &in->a, in->size, A_R1);
    if (v != A_R1)
        arm_mov(e, A_R1, v);
    const uint32_t ctl = in->sub | (in->size == 16 ? 8u : 0u);
    if (in->b.kind == OPD_IMM) {
        arm_mov32(e, A_R2, ctl | (uint32_t)(in->b.imm & 0xffu) << 8);
    } else {
        arm_ubfx(e, A_R2, A_R5, 0, 8);           /* CL */
        arm_lsl(e, A_R2, A_R2, 8);
        if (ctl)
            arm_dp_imm(e, DP_ORR, 0, A_R2, A_R2, ctl);
    }
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_shift);
    arm_blx(e, A_R12);
    if (mem) {
        arm_mov(e, A_R1, A_R0);
        arm_pop(e, 0x0005u);                     /* r0 = address (r2: the pushed r1) */
        put_raw(c, &in->a, in->size, A_R1);
    } else {
        put_raw(c, &in->a, in->size, A_R0);
    }
}

/* Segment register moves (real mode): the selector is base >> 4, a load
 * stores selector << 4 into rt->seg_base, which the adapter gives back to
 * XRIP (and before each interpreted instruction). Only ES and DS change. */
static void emit_seg(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const uint32_t seg_off = XLAT_RT_SEG_BASE + 4u * in->sreg;
    switch (in->sub) {
    case SEG_PUSH:
        arm_ls_imm(e, LS_LDR, A_R2, A_R11, seg_off);
        arm_lsr(e, A_R2, A_R2, 4);
        emit_push_value(c, i, A_R2);
        break;
    case SEG_POP:
        emit_pop_value(c, i, 0);
        arm_lsl(e, A_R2, A_R2, 4);
        arm_ls_imm(e, LS_STR, A_R2, A_R11, seg_off);
        break;
    case SEG_LOAD: {
        if (in->b.kind == OPD_MEM)
            emit_mem_access(c, i, 2, false);
        const int v = get_raw(c, &in->b, 16, A_R1);
        arm_uxth(e, A_R12, v);
        arm_lsl(e, A_R12, A_R12, 4);
        arm_ls_imm(e, LS_STR, A_R12, A_R11, seg_off);
        break;
    }
    case SEG_STORE:
        if (in->a.kind == OPD_MEM)
            emit_mem_access(c, i, 2, true);
        arm_ls_imm(e, LS_LDR, A_R1, A_R11, seg_off);
        arm_lsr(e, A_R1, A_R1, 4);
        put_raw(c, &in->a, 16, A_R1);
        break;
    default: {  /* LES / LDS: offset to the register, selector to ES / DS */
        emit_mem_access(c, i, 4, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRH, A_R1, A_LR, A_R0);
        arm_addw(e, A_R12, A_R0, 2);
        arm_ls_reg(e, LS_LDRH, A_R2, A_LR, A_R12);
        arm_lsl(e, A_R2, A_R2, 4);
        put_raw(c, &in->a, 16, A_R1);
        arm_ls_imm(e, LS_STR, A_R2, A_R11, seg_off);
        break;
    }
    }
}

/* CBW, CWD, CLD, STD, NOT, XLAT, MOVZX, MOVSX: no arithmetic flags. DF lives in rt->eflags (the
 * image goes back to XRIP at the exit). */
static void emit_misc(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    switch (in->sub) {
    case MISC_CBW:
        if (in->size == 32) {                                       /* CWDE */
            arm_lsl(e, A_R12, A_R4, 16);
            arm_dp_reg(e, DP_ORR, 0, A_R4, A_PC, A_R12, SH_ASR, 16);
            break;
        }
        arm_lsl(e, A_R12, A_R4, 24);
        arm_dp_reg(e, DP_ORR, 0, A_R12, A_PC, A_R12, SH_ASR, 24);   /* ASR #24 */
        arm_bfi(e, A_R4, A_R12, 0, 16);
        break;
    case MISC_CWD:
        if (in->size == 32) {                                       /* CDQ */
            arm_dp_reg(e, DP_ORR, 0, A_R6, A_PC, A_R4, SH_ASR, 31);
            break;
        }
        arm_lsl(e, A_R12, A_R4, 16);
        arm_dp_reg(e, DP_ORR, 0, A_R12, A_PC, A_R12, SH_ASR, 31);   /* ASR #31 */
        arm_bfi(e, A_R6, A_R12, 0, 16);
        break;
    case MISC_CLD:
    case MISC_STD:
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_EFLAGS);
        arm_dp_imm(e, in->sub == MISC_STD ? DP_ORR : DP_BIC, 0, A_R12, A_R12, 0x400u);
        arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_EFLAGS);
        break;
    case MISC_XLAT:     /* AL = [seg:(BX + AL) & 0xffff] */
        arm_ubfx(e, A_R12, A_R4, 0, 8);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R7, A_R12, SH_LSL, 0);
        arm_uxth(e, A_R0, A_R0);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * in->mem_seg);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
        emit_checks(c, i, 1, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRB, A_R1, A_LR, A_R0);
        arm_bfi(e, A_R4, A_R1, 0, 8);
        break;
    case MISC_MOVZX:
    case MISC_MOVSX: {  /* r16 / r32 (local) <- r/m8 / r/m16, zero / sign extended */
        const int from = in->size;
        if (in->b.kind == OPD_MEM)
            emit_mem_access(c, i, from / 8, false);
        const int v = get_raw(c, &in->b, from, A_R1);
        if (in->sub == MISC_MOVZX) {
            arm_ubfx(e, A_R1, v, 0, from);
        } else {
            arm_lsl(e, A_R1, v, 32 - from);
            arm_dp_reg(e, DP_ORR, 0, A_R1, A_PC, A_R1, SH_ASR, 32 - from);
        }
        put_raw(c, &in->a, in->local ? 32 : 16, A_R1);
        break;
    }
    default: {  /* NOT r/m */
        if (in->a.kind == OPD_MEM)
            emit_mem_access(c, i, in->size / 8, true);
        const int v = get_raw(c, &in->a, in->size, A_R1);
        arm_dp_reg(e, DP_ORN, 0, A_R1, A_PC, v, SH_LSL, 0);         /* MVN */
        put_raw(c, &in->a, in->size, A_R1);
        break;
    }
    }
}

/* MUL / IMUL / DIV / IDIV through xlat_rt_muldiv(): flags to rt first, as
 * for the shift helper. #DE (division by zero or overflow) leaves by the side
 * exit before anything changed: XRIP runs the instruction and the fault. */
/* DIV / IDIV here (no flags, as XRIP's); #DE (a zero divisor, a quotient
 * that does not fit) leaves by the side exit: XRIP raises it. r2 = the
 * divisor (from get_raw). */
static void emit_div_value(tctx_t *c, int i, int v)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const bool sign = in->sub == 7, w = in->size == 16;
    if (sign)
        arm_sbfx(e, A_R2, v, 0, in->size);
    else
        arm_ubfx(e, A_R2, v, 0, in->size);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R2, 0);
    emit_branch(c, side_dest(c, i, 0), C_EQ);
    /* r1 = the dividend: DX:AX or AX */
    if (w) {
        arm_uxth(e, A_R1, A_R4);
        arm_dp_reg(e, DP_ORR, 0, A_R1, A_R1, A_R6, SH_LSL, 16);
    } else if (sign) {
        arm_sbfx(e, A_R1, A_R4, 0, 16);
    } else {
        arm_uxth(e, A_R1, A_R4);
    }
    if (sign)
        arm_sdiv(e, A_R3, A_R1, A_R2);
    else
        arm_udiv(e, A_R3, A_R1, A_R2);
    /* the quotient fits: 0..max, or -half..half-1 (INT32_MIN / -1 too) */
    const uint32_t lim = w ? 0x10000u : 0x100u;
    if (sign) {
        arm_addw(e, A_R12, A_R3, lim / 2u);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, lim);
    } else {
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R3, lim);
    }
    emit_branch(c, side_dest(c, i, 0), C_CS);
    arm_mls(e, A_R1, A_R3, A_R2, A_R1);                              /* remainder */
    if (w) {
        arm_bfi(e, A_R4, A_R3, 0, 16);                               /* AX */
        arm_bfi(e, A_R6, A_R1, 0, 16);                               /* DX */
    } else {
        arm_bfi(e, A_R4, A_R3, 0, 8);                                /* AL */
        arm_bfi(e, A_R4, A_R1, 8, 8);                                /* AH */
    }
}

/* MUL / IMUL whose flags are dead (mark_dead_flags): the product only. */
static void emit_mul_value(tctx_t *c, int i, int v)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (in->sub == MULDIV_IMUL3) {
        arm_sbfx(e, A_R2, v, 0, 16);
        arm_mov32(e, A_R3, (uint32_t)(int32_t)(int16_t)in->a.imm);
        arm_mul(e, A_R1, A_R2, A_R3);
        put_raw(c, &in->a, 16, A_R1);
        return;
    }
    const bool sign = in->sub == 5, w = in->size == 16;
    if (sign) {
        arm_sbfx(e, A_R2, v, 0, in->size);
        arm_sbfx(e, A_R3, A_R4, 0, in->size);
    } else {
        arm_ubfx(e, A_R2, v, 0, in->size);
        arm_ubfx(e, A_R3, A_R4, 0, in->size);
    }
    arm_mul(e, A_R1, A_R2, A_R3);
    if (w) {
        arm_bfi(e, A_R4, A_R1, 0, 16);                               /* AX */
        arm_lsr(e, A_R1, A_R1, 16);
        arm_bfi(e, A_R6, A_R1, 0, 16);                               /* DX */
    } else {
        arm_bfi(e, A_R4, A_R1, 0, 16);                               /* AX */
    }
}

/* Guarded straight-line unsigned MUL/DIV pair. DIV's flags are preserved
 * on its independent entry and on #DE; only the successful combined path
 * may omit the MUL flags, because the next register ALU overwrites them.
 * No guest registers or flags are committed before the fault guard. */
static void emit_muldiv_pair(tctx_t *c, int i)
{
    const x86_insn_t *m = &c->in[i];
    if (m->local || m->sub != 4 || m->size != 16 || m->b.kind != OPD_REG ||
        i + 2 >= c->n || c->fall_idx[i] != i + 1 || c->fall_idx[i + 1] != i + 2)
        return;
    const x86_insn_t *d = &c->in[i + 1], *a = &c->in[i + 2];
    if (d->op != XO_MULDIV || d->sub != 6 || d->size != 16 || d->b.kind != OPD_REG ||
        d->b.reg == 0 /* AX */ || d->b.reg == 2 /* DX */ ||
        a->op != XO_ALU || x86_is_carry_op(a) || x86_has_mem(a))
        return;

    arm_emit_t *e = &c->e;
    const int v = get_raw(c, &m->b, 16, A_R2);
    arm_uxth(e, A_R2, v);
    arm_uxth(e, A_R1, A_R4);
    arm_mul(e, A_R1, A_R1, A_R2);  /* full unsigned product */
    if (m->b.reg == d->b.reg) {
        /* For a nonzero unchanged divisor, (AX * d) / d always fits. */
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R2, 0);
    } else {
        const int w = get_raw(c, &d->b, 16, A_R2);
        arm_uxth(e, A_R2, w);
        arm_lsr(e, A_R3, A_R1, 16);
        /* Unsigned quotient fits iff high word < divisor, including d=0. */
        arm_dp_reg(e, DP_SUB, 1, A_PC, A_R3, A_R2, SH_LSL, 0);
    }
    const uint32_t fault = arm_branch_slot(e);
    arm_udiv(e, A_R3, A_R1, A_R2);
    arm_mls(e, A_R1, A_R3, A_R2, A_R1);
    arm_bfi(e, A_R4, A_R3, 0, 16);
    arm_bfi(e, A_R6, A_R1, 0, 16);
    emit_branch(c, make_dest(c, i, a->ip), C_AL);
    /* Fault: original MUL (including flags), then original DIV and #DE.
     * The independent DIV label is unchanged for all other predecessors. */
    local_patch(c, fault, m->b.reg == d->b.reg ? C_EQ : C_CS);
}

static void emit_muldiv(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (x86_is_div(in) || in->local) {
        const int size = in->sub == MULDIV_IMUL3 ? 16 : in->size;
        if (in->b.kind == OPD_MEM)
            emit_mem_access(c, i, size / 8, false);
        const int v = get_raw(c, &in->b, size, A_R2);
        if (x86_is_div(in))
            emit_div_value(c, i, v);
        else
            emit_mul_value(c, i, v);
        return;
    }
    emit_muldiv_pair(c, i);
    const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
    if (r >= 0)
        emit_recon(c, r);
    const bool imul3 = in->sub == MULDIV_IMUL3;
    const int size = imul3 ? 16 : in->size;
    if (in->b.kind == OPD_MEM)
        emit_mem_access(c, i, size / 8, false);
    const int v = get_raw(c, &in->b, size, A_R2);
    if (v != A_R2)
        arm_mov(e, A_R2, v);
    arm_mov32(e, A_R1, in->sub | (size == 16 ? 16u : 0u));
    if (imul3) {
        arm_mov32(e, A_R3, in->a.imm);
    } else {
        arm_uxth(e, A_R3, A_R4);                                   /* AX */
        arm_dp_reg(e, DP_ORR, 0, A_R3, A_R3, A_R6, SH_LSL, 16);    /* | DX << 16 */
    }
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_muldiv);
    arm_blx(e, A_R12);
    if (in->sub >= 6 && !imul3) {
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R1, 0);
        emit_branch(c, side_dest(c, i, 0), C_NE);                  /* #DE */
    }
    if (imul3) {
        put_raw(c, &in->a, 16, A_R0);
    } else {
        arm_bfi(e, A_R4, A_R0, 0, 16);                             /* AX */
        if (size == 16) {
            arm_lsr(e, A_R12, A_R0, 16);
            arm_bfi(e, A_R6, A_R12, 0, 16);                        /* DX */
        }
    }
}

/* r0 = linear address of [rm-base] with segment seg (string operands). */
static void emit_str_addr(tctx_t *c, int i, int rm, int seg, int bytes, bool write)
{
    arm_emit_t *e = &c->e;
    x86_insn_t t = c->in[i];
    t.mem_mod = 0;
    t.mem_direct = 0;
    t.disp = 0;
    t.mem_rm = (uint8_t)rm;
    emit_offset(c, &t);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * (uint32_t)seg);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    emit_checks(c, i, bytes, write);
}

/* r0 = string step (bytes, or -bytes with DF). */
static void emit_str_step(tctx_t *c, int bytes)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_EFLAGS);
    arm_ubfx(e, A_R12, A_R12, 10, 1);
    arm_mov32(e, A_R0, (uint32_t)bytes);
    arm_dp_reg(e, DP_SUB, 0, A_R0, A_R0, A_R12, SH_LSL, bytes == 4 ? 3 : bytes);
}

/* CMPS / SCAS, with REPE / REPNE: the lazy SUB flags of each element go to
 * rt (as XRIP's sub_flags), then SI / DI step, CX counts down, and REPE
 * stops on ZF clear, REPNE on ZF set. Checks come before the changes. */
static void emit_string_cmp(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int bytes = in->size / 8, s = 32 - in->size;
    if (in->rep) {
        arm_uxth(e, A_R12, A_R5);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, XLAT_REP_CHUNK);
        /* A large initial CX need not imply a long search. Use the
         * decoded read helper, not a STOP before the first comparison. */
        emit_branch(c, side_dest(c, i, 0), C_HI);
    }
    uint32_t top = 0, done = 0;
    if (in->rep) {
        top = e->pos;
        arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R5, SH_LSL, 16);   /* Z = (CX == 0) */
        done = arm_branch_slot(e);
    }
    if (in->sub == STR_CMPS) {
        emit_str_addr(c, i, 4, in->mem_seg, bytes, false);          /* DS:[SI] */
        arm_mov(e, A_R3, A_R0);
        emit_str_addr(c, i, 5, 0, bytes, false);                    /* ES:[DI] */
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, ls_load(in->size), A_R2, A_LR, A_R3);
        arm_ls_reg(e, ls_load(in->size), A_R3, A_LR, A_R0);
        arm_lsl(e, A_R2, A_R2, s);
    } else {
        emit_str_addr(c, i, 5, 0, bytes, false);                    /* ES:[DI] */
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, ls_load(in->size), A_R3, A_LR, A_R0);
        arm_lsl(e, A_R2, A_R4, s);                                  /* AL / AX / EAX */
    }
    arm_lsl(e, A_R3, A_R3, s);
    arm_dp_reg(e, DP_SUB, 0, A_R1, A_R2, A_R3, SH_LSL, 0);
    emit_lazy(c, FK_SUB, in->size, false, i);                        /* r1 stays */
    emit_str_step(c, bytes);
    if (in->sub == STR_CMPS) {
        arm_dp_reg(e, DP_ADD, 0, A_R12, A_R9, A_R0, SH_LSL, 0);
        arm_bfi(e, A_R9, A_R12, 0, 16);
    }
    arm_dp_reg(e, DP_ADD, 0, A_R12, A_R10, A_R0, SH_LSL, 0);
    arm_bfi(e, A_R10, A_R12, 0, 16);
    if (in->rep) {
        arm_subw(e, A_R12, A_R5, 1);
        arm_bfi(e, A_R5, A_R12, 0, 16);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R1, 0);                    /* Z = ZF */
        const uint32_t again = arm_branch_slot(e);
        if (!arm_patch_branch(e, again, top, in->rep == 1 ? C_EQ : C_NE))
            c->overflow = true;
        local_patch(c, done, C_EQ);
    }
}

/* MOVS / STOS / LODS, with REP: the element loop runs here while CX != 0.
 * All checks of an element come before its changes, so a side exit runs the
 * (restartable) instruction in XRIP with the current SI, DI and CX. */
/* Explicit string operands; ARM GPRs stay live across the call. */
static void emit_string_args(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int bytes = in->size / 8;
    x86_insn_t t = *in;
    t.mem_mod = 0;
    t.mem_direct = 0;
    t.disp = 0;
    if (in->sub == STR_MOVS) {
        t.mem_rm = 4;                                   /* [SI] */
        emit_offset(c, &t);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * in->mem_seg);
        arm_dp_reg(e, DP_ADD, 0, A_R3, A_R0, A_R12, SH_LSL, 0);
    } else if (bytes == 4) {
        arm_mov(e, A_R3, A_R4);
    } else {
        arm_ubfx(e, A_R3, A_R4, 0, bytes * 8);         /* AL / AX */
    }
    t.mem_rm = 5;                                       /* ES:[DI] */
    emit_offset(c, &t);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 0u);
    arm_dp_reg(e, DP_ADD, 0, A_R2, A_R0, A_R12, SH_LSL, 0);
    arm_mov(e, A_R1, A_R3);
    if (in->rep)
        arm_uxth(e, A_R3, A_R5);                        /* CX */
    else
        arm_mov32(e, A_R3, 1u);
    uint32_t ctl = (uint32_t)bytes << 16 | (in->sub == STR_STOS ? XLAT_REP_STOS : 0u) |
                   (uint32_t)in->mem_seg << XLAT_REP_SEG_SHIFT;
    if (!arm_dp_imm(e, DP_ORR, 0, A_R3, A_R3, ctl)) {
        arm_mov32(e, A_R12, ctl);
        arm_dp_reg(e, DP_ORR, 0, A_R3, A_R3, A_R12, SH_LSL, 0);
    }
    arm_mov(e, A_R0, A_R11);
}

/* r0 = transferred elements, including partial REP and backward runs. */
static void emit_string_update(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int bytes = in->size / 8;
    if (in->rep) {
        arm_dp_reg(e, DP_SUB, 0, A_R1, A_R5, A_R0, SH_LSL, 0);
        arm_bfi(e, A_R5, A_R1, 0, 16);
    }
    arm_mov(e, A_R12, A_R0);
    if (bytes > 1)
        arm_lsl(e, A_R12, A_R12, bytes == 4 ? 2 : 1);
    arm_ls_imm(e, LS_LDR, A_R1, A_R11, XLAT_RT_EFLAGS);
    arm_dp_imm(e, DP_AND, 1, A_PC, A_R1, 0x400u);
    const uint32_t forward = arm_branch_slot(e);
    arm_dp_imm(e, DP_RSB, 0, A_R12, A_R12, 0u);
    local_patch(c, forward, C_EQ);
    if (in->sub == STR_MOVS) {
        arm_dp_reg(e, DP_ADD, 0, A_R1, A_R9, A_R12, SH_LSL, 0);
        arm_bfi(e, A_R9, A_R1, 0, 16);
    }
    arm_dp_reg(e, DP_ADD, 0, A_R1, A_R10, A_R12, SH_LSL, 0);
    arm_bfi(e, A_R10, A_R1, 0, 16);
}

/* Check the IRQ after a completed fast helper before the next guest IP. */
static void emit_string_irq(tctx_t *c, uint16_t ip)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_INTR);
    arm_ls_imm(e, LS_LDRB, A_R12, A_R12, 0);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, 0);
    const int stop = exit_stub(c, ip, XLAT_EXIT_STOP, -1);
    emit_branch(c, (dest_t){D_STUB, (int16_t)stop}, C_NE);
}

static void emit_rep_bulk(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    emit_string_args(c, i);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_rep);
    arm_blx(e, A_R12);
    arm_mov(e, A_R3, A_R0);          /* count + yield reason */
    arm_uxth(e, A_R0, A_R0);
    emit_string_update(c, i);
    arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R5, SH_LSL, 16);
    const uint32_t complete = arm_branch_slot(e);
    emit_string_irq(c, in->ip);
    arm_dp_imm(e, DP_AND, 1, A_PC, A_R3, XLAT_REP_YIELD);
    const int yield = exit_stub(c, in->ip, XLAT_EXIT_BUDGET, -1);
    emit_branch(c, (dest_t){D_STUB, (int16_t)yield}, C_NE);
    /* Only a mapping/code refusal reaches the synchronized helper. */
    emit_branch(c, side_dest(c, i, 1), C_AL);
    local_patch(c, complete, C_EQ);
    emit_string_irq(c, (uint16_t)(in->ip + in->len));
}

static void emit_string(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int bytes = in->size / 8;
    if (in->rep && native_string(in)) {
        emit_rep_bulk(c, i);
        return;
    }
    /* Long REP LODS uses the same decoded read path as a memory miss. */
    if (in->rep) {
        arm_uxth(e, A_R12, A_R5);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, XLAT_REP_CHUNK);
        emit_branch(c, side_dest(c, i, 0), C_HI);
    }
    /* r2 = step: bytes, or -bytes when DF is set */
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_EFLAGS);
    arm_ubfx(e, A_R12, A_R12, 10, 1);
    arm_mov32(e, A_R2, (uint32_t)bytes);
    arm_dp_reg(e, DP_SUB, 0, A_R2, A_R2, A_R12, SH_LSL, bytes == 4 ? 3 : bytes);
    uint32_t top = 0, done = 0;
    if (in->rep) {
        top = e->pos;
        arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R5, SH_LSL, 16);   /* Z = (CX == 0) */
        done = arm_branch_slot(e);
    }
    x86_insn_t t = *in;
    t.mem_mod = 0;
    t.mem_direct = 0;
    t.disp = 0;
    if (in->sub != STR_STOS) {
        t.mem_rm = 4;                                   /* [SI] */
        emit_offset(c, &t);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * in->mem_seg);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
        emit_checks(c, i, bytes, false);
        if (in->sub == STR_MOVS)
            arm_mov(e, A_R3, A_R0);
    }
    if (in->sub != STR_LODS) {
        t.mem_rm = 5;                                   /* ES:[DI] */
        emit_offset(c, &t);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 0u);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
        emit_checks(c, i, bytes, true);
    }
    arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
    if (in->sub == STR_LODS) {
        if (bytes == 4) {
            arm_ls_reg(e, LS_LDR, A_R4, A_LR, A_R0);
        } else {
            arm_ls_reg(e, ls_load(in->size), A_R1, A_LR, A_R0);
            arm_bfi(e, A_R4, A_R1, 0, bytes * 8);
        }
    } else if (in->sub == STR_STOS) {
        arm_ls_reg(e, ls_store(in->size), A_R4, A_LR, A_R0);
    } else {
        arm_ls_reg(e, ls_load(in->size), A_R1, A_LR, A_R3);
        arm_ls_reg(e, ls_store(in->size), A_R1, A_LR, A_R0);
    }
    if (in->sub != STR_STOS) {
        arm_dp_reg(e, DP_ADD, 0, A_R1, A_R9, A_R2, SH_LSL, 0);
        arm_bfi(e, A_R9, A_R1, 0, 16);
    }
    if (in->sub != STR_LODS) {
        arm_dp_reg(e, DP_ADD, 0, A_R1, A_R10, A_R2, SH_LSL, 0);
        arm_bfi(e, A_R10, A_R1, 0, 16);
    }
    if (in->rep) {
        arm_subw(e, A_R1, A_R5, 1);
        arm_bfi(e, A_R5, A_R1, 0, 16);
        const uint32_t back = arm_branch_slot(e);
        if (!arm_patch_branch(e, back, top, C_AL))
            c->overflow = true;
        local_patch(c, done, C_EQ);
    }
}

/* ---------------------------------------------------------------------- */
/* Far and indirect transfers, stack frames, flag instructions (iter. 7)   */
/* ---------------------------------------------------------------------- */

/* The lazy flags reach rt before a transfer to a run-time IP (as for RET). */
static void emit_dyn_recon(tctx_t *c, int i)
{
    const int r = sync_recon(c, c->out_set[i], edge_producer(c, i));
    if (r >= 0)
        emit_recon(c, r);
}

/* Dispatch to the run-time target already stored in rt. */
static void emit_dyn_exit(tctx_t *c, uint8_t kind)
{
    const int k = exit_stub(c, 0, kind, -1);
    c->stub[k].dyn = 1;
    emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_AL);
}

/* Far transfer: r2 = IP, r3 = selector (16 bits each). rt->cs and the CS
 * base change here; the adapter loads CS into XRIP (xlat_segs_to_xrip). */
static void emit_far_exit(tctx_t *c, bool call)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_STRH, A_R3, A_R11, XLAT_RT_CS);
    arm_lsl(e, A_R12, A_R3, 4);
    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u);
    arm_ls_imm(e, LS_STR, A_R2, A_R11, XLAT_RT_IP);
    emit_dyn_exit(c, call ? XLAT_EXIT_FAR_CALL : XLAT_EXIT_BRANCH);
}

/* Two pushes: v1 to SP-2, v2 to SP-4 (each address wraps at 64 KiB). Both
 * are checked before anything changes. A register number < 0 means the
 * constant (put in r2 right before its store). v1, v2 not r0, r1, r12, lr. */
static void emit_push2(tctx_t *c, int i, int v1, uint32_t c1, int v2, uint32_t c2)
{
    arm_emit_t *e = &c->e;
    emit_stack_addr(c, -2);
    emit_checks(c, i, 2, true);
    arm_subw(e, A_R1, A_R1, 2);
    arm_uxth(e, A_R1, A_R1);                                     /* r1 = new SP */
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
    emit_checks(c, i, 2, true);
    arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
    if (v2 < 0) {
        arm_mov32(e, A_R2, c2);
        v2 = A_R2;
    }
    arm_ls_reg(e, LS_STRH, v2, A_LR, A_R0);
    arm_addw(e, A_R0, A_R1, 2);
    arm_uxth(e, A_R0, A_R0);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    if (v1 < 0) {
        arm_mov32(e, A_R2, c1);
        v1 = A_R2;
    }
    arm_ls_reg(e, LS_STRH, v1, A_LR, A_R0);
    emit_store_sp(c);
}

/* CALL / JMP ptr16:16, RETF, CALL / JMP m16:16 (real mode: base = selector
 * << 4, as XRIP's far_call / far_jump / far_return). Into the region's own
 * CS (local) they are near transfers; otherwise the region leaves with the
 * new CS and the dispatcher goes on there. */
static void emit_far(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const uint16_t ret_ip = (uint16_t)(in->ip + in->len);
    if (!in->local)
        emit_dyn_recon(c, i);
    switch (in->sub) {
    case FAR_CALL:
        emit_push2(c, i, -1, c->rt->cs, -1, ret_ip);
        if (in->local) {
            emit_branch(c, make_dest(c, i, in->target), C_AL);
        } else {
            arm_mov32(e, A_R2, in->target);
            arm_mov32(e, A_R3, in->a.imm);
            emit_far_exit(c, true);
        }
        break;
    case FAR_JMP:
        if (in->local) {
            emit_branch(c, make_dest(c, i, in->target), C_AL);
        } else {
            arm_mov32(e, A_R2, in->target);
            arm_mov32(e, A_R3, in->a.imm);
            emit_far_exit(c, false);
        }
        break;
    case FAR_RET:
        /* IP at SP, CS at SP+2: both checked, then SP += 4 + release */
        emit_stack_addr(c, 0);
        emit_checks(c, i, 2, false);
        arm_addw(e, A_R3, A_R1, 2);
        arm_uxth(e, A_R3, A_R3);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R3, A_R12, SH_LSL, 0);
        emit_checks(c, i, 2, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRH, A_R3, A_LR, A_R0);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
        arm_ls_reg(e, LS_LDRH, A_R2, A_LR, A_R0);
        if (in->b.imm + 4u <= 0xfffu) {
            arm_addw(e, A_R1, A_R1, in->b.imm + 4u);
        } else {
            arm_mov32(e, A_R12, in->b.imm + 4u);
            arm_dp_reg(e, DP_ADD, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
        }
        emit_store_sp(c);                    /* the low 16 bits */
        {   /* back into this CS: a return site here goes on directly */
            arm_mov32(e, A_R12, c->rt->cs);
            arm_dp_reg(e, DP_SUB, 1, A_PC, A_R3, A_R12, SH_LSL, 0);
            const uint32_t other = arm_branch_slot(e);
            emit_site_dispatch(c, i, A_R2);
            local_patch(c, other, C_NE);
        }
        emit_far_exit(c, false);
        break;
    default:    /* CALL / JMP m16:16: offset at [m], selector at [m+2] */
        emit_mem_access(c, i, 4, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRH, A_R2, A_LR, A_R0);
        arm_addw(e, A_R12, A_R0, 2);
        arm_ls_reg(e, LS_LDRH, A_R3, A_LR, A_R12);
        if (in->sub == FAR_CALLM) {
            /* the offset waits in rt->ip: a side exit stores rt->ip itself */
            arm_ls_imm(e, LS_STR, A_R2, A_R11, XLAT_RT_IP);
            emit_push2(c, i, -1, c->rt->cs, -1, ret_ip);
            arm_ls_imm(e, LS_LDR, A_R2, A_R11, XLAT_RT_IP);
        }
        emit_far_exit(c, in->sub == FAR_CALLM);
        break;
    }
}

/* CALL / JMP r/m16: the operand is read first (CALL SP, CALL [SP-relative]
 * use the old SP), then the return address is pushed. */
static void emit_ind(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    emit_dyn_recon(c, i);
    if (in->b.kind == OPD_MEM)
        emit_mem_access(c, i, 2, false);
    const int v = get_raw(c, &in->b, 16, A_R3);
    arm_uxth(e, A_R3, v);
    if (in->sub == IND_CALL) {
        arm_mov32(e, A_R2, (uint16_t)(in->ip + in->len));
        emit_push_value(c, i, A_R2);
    } else {
        emit_site_dispatch(c, i, A_R3);
    }
    arm_ls_imm(e, LS_STR, A_R3, A_R11, XLAT_RT_IP);
    emit_dyn_exit(c, in->sub == IND_CALL ? XLAT_EXIT_FAR_CALL : XLAT_EXIT_BRANCH);
}

/* PUSHA / POPA (one 16-byte block, SP must not wrap inside it: otherwise
 * XRIP runs it), ENTER n, 0 / 1, LEAVE. */
static void emit_stk(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    static const int8_t pusha[8] = {A_R10, A_R9, A_R8, -1, A_R7, A_R6, A_R5, A_R4};
    switch (in->sub) {
    case STK_PUSHA:
        emit_stack_addr(c, 0);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R1, 16);
        emit_branch(c, side_dest(c, i, 0), C_CC);                    /* SP < 16 */
        arm_subw(e, A_R0, A_R0, 16);
        emit_checks(c, i, 16, true);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_dp_reg(e, DP_ADD, 0, A_LR, A_LR, A_R0, SH_LSL, 0);
        for (int k = 7; k >= 0; --k)          /* AX at SP-2 ... DI at SP-16 */
            arm_ls_imm(e, LS_STRH, pusha[k] >= 0 ? pusha[k] : A_R1, A_LR, 2u * (uint32_t)k);
        arm_subw(e, A_R1, A_R1, 16);
        emit_store_sp(c);
        break;
    case STK_POPA:
        emit_stack_addr(c, 0);
        arm_addw(e, A_R12, A_R1, 16);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, 0x10000u);
        emit_branch(c, side_dest(c, i, 0), C_HI);                    /* SP > 0xfff0 */
        emit_checks(c, i, 16, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_dp_reg(e, DP_ADD, 0, A_LR, A_LR, A_R0, SH_LSL, 0);
        for (int k = 0; k < 8; ++k) {         /* DI, SI, BP, (SP), BX, DX, CX, AX */
            if (pusha[k] < 0)
                continue;
            arm_ls_imm(e, LS_LDRH, A_R12, A_LR, 2u * (uint32_t)k);
            arm_bfi(e, pusha[k], A_R12, 0, 16);
        }
        arm_addw(e, A_R1, A_R1, 16);
        emit_store_sp(c);
        break;
    case STK_LEAVE:
        /* SP = BP, then BP = pop */
        arm_uxth(e, A_R1, A_R8);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
        arm_dp_reg(e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
        emit_checks(c, i, 2, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRH, A_R2, A_LR, A_R0);
        arm_addw(e, A_R1, A_R1, 2);
        emit_store_sp(c);
        arm_bfi(e, A_R8, A_R2, 0, 16);
        break;
    default: {  /* ENTER size, 0 / 1 */
        emit_stack_addr(c, -2);                                      /* r1 = frame */
        emit_checks(c, i, 2, true);
        if (in->sub == STK_ENTER1) {
            /* level 1 also pushes the new frame pointer at SP-4 */
            arm_subw(e, A_R2, A_R1, 2);
            arm_uxth(e, A_R2, A_R2);
            arm_mov(e, A_R3, A_R0);
            arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
            arm_dp_reg(e, DP_ADD, 0, A_R0, A_R2, A_R12, SH_LSL, 0);
            emit_checks(c, i, 2, true);
            arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
            arm_ls_reg(e, LS_STRH, A_R1, A_LR, A_R0);
            arm_ls_reg(e, LS_STRH, A_R8, A_LR, A_R3);
        } else {
            arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
            arm_ls_reg(e, LS_STRH, A_R8, A_LR, A_R0);
            arm_mov(e, A_R2, A_R1);
        }
        arm_bfi(e, A_R8, A_R1, 0, 16);                               /* BP = frame */
        arm_mov(e, A_R1, A_R2);
        if (in->b.imm <= 0xfffu) {
            arm_subw(e, A_R1, A_R1, in->b.imm);
        } else {
            arm_mov32(e, A_R12, in->b.imm);
            arm_dp_reg(e, DP_SUB, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
        }
        emit_store_sp(c);                    /* the low 16 bits */
        break;
    }
    }
}

/* r0 = rt->eflags with the lazy flags resolved (xlat_rt_resolve). */
static void emit_resolve(tctx_t *c)
{
    arm_emit_t *e = &c->e;
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_resolve);
    arm_blx(e, A_R12);
}

/* CLC / STC / CMC, LAHF, SAHF, PUSHF, POPF on the flags in rt. XRIP resolves the
 * lazy flags for these; CLC / STC / CMC here change CF where it is (the
 * lazy CF or the image), the same flags. */
static void emit_flag(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
    if (r >= 0)
        emit_recon(c, r);
    switch (in->sub) {
    case FL_CLC:
    case FL_STC:
    case FL_CMC: {
        emit_pending_check(c);
        /* CF in both places: the lazy CF and the image (xlat.h) */
        if (in->sub == FL_CMC) {
            arm_ls_imm(e, LS_LDRB, A_R12, A_R11, XLAT_RT_CF);
            arm_dp_imm(e, DP_EOR, 0, A_R12, A_R12, 1);
        } else {
            arm_mov32(e, A_R12, in->sub == FL_STC);
        }
        arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_CF);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_EFLAGS);
        arm_bfi(e, A_LR, A_R12, 0, 1);
        arm_ls_imm(e, LS_STR, A_LR, A_R11, XLAT_RT_EFLAGS);
        break;
    }
    case FL_LAHF:   /* AH = (2 | FLAGS & 0xd5) */
        emit_resolve(c);
        arm_dp_imm(e, DP_AND, 0, A_R12, A_R0, 0xd5u);
        arm_dp_imm(e, DP_ORR, 0, A_R12, A_R12, 2u);
        arm_bfi(e, A_R4, A_R12, 8, 8);
        break;
    case FL_SAHF:   /* SF ZF AF PF CF from AH */
        emit_resolve(c);
        arm_ubfx(e, A_R12, A_R4, 8, 8);
        arm_dp_imm(e, DP_AND, 0, A_R12, A_R12, 0xd5u);
        arm_dp_imm(e, DP_BIC, 0, A_R0, A_R0, 0xd5u);
        arm_dp_reg(e, DP_ORR, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
        arm_ls_imm(e, LS_STR, A_R0, A_R11, XLAT_RT_EFLAGS);
        arm_dp_imm(e, DP_AND, 0, A_R12, A_R0, 1);                /* resolved: lazy_cf = CF */
        arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_CF);
        break;
    case FL_POPF:   /* xlat_rt_popf(rt, word at SS:SP), then SP += 2 */
        emit_stack_addr(c, 0);
        emit_checks(c, i, 2, false);
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, LS_LDRH, A_R1, A_LR, A_R0);
        arm_mov(e, A_R0, A_R11);
        arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_popf);
        arm_blx(e, A_R12);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
        emit_branch(c, side_dest(c, i, 0), C_EQ);
        arm_ls_imm(e, LS_LDRH, A_R1, A_R11, XLAT_RT_SP);
        arm_addw(e, A_R1, A_R1, 2u);
        arm_uxth(e, A_R1, A_R1);
        emit_store_sp(c);
        /* POPF completed (including SP): no STI shadow. */
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_INTR);
        arm_ls_imm(e, LS_LDRB, A_R12, A_R12, 0);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, 0);
        emit_branch(c, (dest_t){D_STUB, (int16_t)exit_stub(c, (uint16_t)(in->ip + in->len),
                                                        XLAT_EXIT_STOP, -1)}, C_NE);
        break;
    default:        /* PUSHF: 2 | (FLAGS & 0x7fd5), as XRIP's makeflagsword() */
        emit_resolve(c);
        arm_mov32(e, A_R12, 0x7fd5u);
        arm_dp_reg(e, DP_AND, 0, A_R2, A_R0, A_R12, SH_LSL, 0);
        arm_dp_imm(e, DP_ORR, 0, A_R2, A_R2, 2u);
        emit_push_value(c, i, A_R2);
        break;
    }
}

/* LOOPNZ / LOOPZ: CX - 1, then taken when CX != 0 and ZF is clear / set.
 * ZF comes from the ARM flags of the producer right before (local) or
 * from rt (xlat_rt_cond). */
static void emit_loopcc(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    int zcond;          /* ARM condition that holds when ZF = 1 */
    if (c->local_jcc[i]) {
        zcond = C_EQ;
    } else {
        const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
        if (r >= 0)
            emit_recon(c, r);
        arm_mov(e, A_R0, A_R11);
        arm_mov32(e, A_R1, 4);                                       /* JZ */
        arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_cond);
        arm_blx(e, A_R12);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
        zcond = C_NE;
    }
    arm_subw(e, A_R12, A_R5, 1);                                     /* no flags */
    arm_bfi(e, A_R5, A_R12, 0, 16);
    /* LOOPNZ skips when ZF = 1, LOOPZ when ZF = 0 */
    const uint32_t skip = arm_branch_slot(e);
    arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R5, SH_LSL, 16);         /* Z = (CX == 0) */
    emit_branch(c, make_dest(c, i, in->target), C_NE);
    local_patch(c, skip, in->sub ? zcond ^ 1 : zcond);
}

/* Safe OUT and IF updates need no decoder; unsafe OUT falls back before
 * any port side effect, through the synchronized interpreter thunk. */
static void emit_io(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (in->sub == IO_STI || in->sub == IO_OUT) {
        const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
        if (r >= 0)
            emit_recon(c, r);
    }
    const int shadow = sti_successor(c, i);
    if (shadow >= 0 && c->in[shadow].op == XO_STR) {
        const x86_insn_t *p = &c->in[shadow];
        const uint16_t next = (uint16_t)(in->ip + 2u);
        const int ret = find_insn(c, next);
        const bool resume = ret > i;
        /* The shared scalar trampoline saves only its local ARM frame.
         * On refusal its ordinary payload executes STI ALONE, never the
         * protected instruction: XRIP then consumes the real shadow. */
        if (!arm_bl(e, c->scalar_call[SC_PATH_STI_STR]))
            c->overflow = true;
        arm_e16(e, (uint16_t)(SC_STI_STR | (p->size == 16 ? 1u << 4 : 0u) |
                              (resume ? SC_CONT : 0u)));
        arm_e16(e, (uint16_t)(p->sub | (unsigned)p->mem_seg << SC_STR_SEG_SHIFT));
        arm_e16(e, 0);
        arm_e16(e, 0);
        arm_e16(e, 0);
        arm_e16(e, next);
        arm_e16(e, in->ip);
        arm_e16(e, 1u << 8);   /* fallback length=1, no continuation */
        if (resume)
            emit_branch(c, (dest_t){D_INSN, (int16_t)ret}, C_AL);
        return;
    }
    if (shadow >= 0) {
        /* The helper completes STI and exactly its shadow instruction.
         * POP still has its own label for branches targeting it. */
        arm_mov(e, A_R0, A_R11);
        arm_mov32(e, A_R1, c->in[shadow].sreg);
        arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_sti_pop);
        arm_blx(e, A_R12);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
        const uint32_t fast = arm_branch_slot(e);
        emit_helper_call(c, i, XLAT_XI_STI_POP | (c->in[shadow].sreg == 3),
                         2u, true);
        const uint32_t done = arm_branch_slot(e);
        local_patch(c, fast, C_NE);
        arm_dp_imm(e, DP_AND, 1, A_PC, A_R0, XLAT_IO_BOUNDARY);
        const int stop = exit_stub(c, (uint16_t)(in->ip + 2u), XLAT_EXIT_STOP, -1);
        emit_branch(c, (dest_t){D_STUB, (int16_t)stop}, C_NE);
        local_patch(c, done, C_AL);
        emit_branch(c, make_dest(c, i, (uint16_t)(in->ip + 2u)), C_AL);
        return;
    }
    uint32_t ctl;
    switch (in->sub) {
    case IO_CLI: ctl = XLAT_IO_CLI; break;
    case IO_STI: ctl = XLAT_IO_STI; break;
    default:
        ctl = (uint32_t)(in->size / 8) << 16 | (in->sub == IO_OUT ? XLAT_IO_OUT : 0u);
        if (!in->local)
            ctl |= in->a.imm & 0xffu;
        break;
    }
    arm_mov32(e, A_R1, ctl);
    if ((in->sub == IO_IN || in->sub == IO_OUT) && in->local) {
        arm_ubfx(e, A_R12, A_R6, 0, 16);                            /* DX */
        arm_dp_reg(e, DP_ORR, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
    }
    if (in->sub == IO_OUT)
        arm_mov(e, A_R2, A_R4);
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_io);
    arm_blx(e, A_R12);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
    if (in->sub == IO_STI || in->sub == IO_OUT) {
        const uint32_t native = arm_branch_slot(e);
        emit_interp_call(c, i, in->sub == IO_OUT ? XI_MAY_WRITE : 0u, true);
        const uint32_t done = arm_branch_slot(e);
        local_patch(c, native, C_NE);
        arm_dp_imm(e, DP_AND, 1, A_PC, A_R0, XLAT_IO_BOUNDARY);
        const int k = exit_stub(c, (uint16_t)(in->ip + in->len), XLAT_EXIT_STOP, -1);
        emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_NE);
        local_patch(c, done, C_AL);
    } else {
        emit_branch(c, side_dest(c, i, 0), C_EQ);
        if (in->sub == IO_IN)
            arm_bfi(e, A_R4, A_R0, 0, in->size);
    }
}

/* x87: fpu.c through xlat_rt_fpu (no flags, no x86 registers but FNSTSW
 * AX); a 0 from it (CR0.EM/TS) leaves the instruction to XRIP. */
static void emit_fpu(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    uint32_t ctl;
    if (in->rep && in->sub != FPU_WAIT) {
        /* FWAIT folded in front (x86_decode) */
        arm_mov32(e, A_R1, XLAT_FPU_WAIT);
        arm_mov(e, A_R0, A_R11);
        arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_fpu);
        arm_blx(e, A_R12);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
        emit_branch(c, side_dest(c, i, 0), C_EQ);
    }
    if (in->sub == FPU_WAIT) {
        ctl = XLAT_FPU_WAIT;
    } else {
        ctl = in->sub | (uint32_t)in->sreg << 3;
        if (in->b.kind == OPD_MEM) {
            bool write;
            x86_fpu_mem_bytes(in->sub, in->sreg, &write);
            emit_mem_access(c, i, in->size, write);
            arm_mov(e, A_R2, A_R0);
            ctl |= XLAT_FPU_MEM;
        } else {
            ctl |= (uint32_t)in->a.reg << 6;
        }
    }
    arm_mov32(e, A_R1, ctl);
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_fpu);
    arm_blx(e, A_R12);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
    emit_branch(c, side_dest(c, i, 0), C_EQ);
    if (in->sub == 7 && in->sreg == 4 && in->b.kind != OPD_MEM) {   /* FNSTSW AX */
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
        arm_ls_imm(e, LS_LDRH, A_R12, A_R12, 0);
        arm_bfi(e, A_R4, A_R12, 0, 16);
    }
}

/* Consecutive 16-bit MOV/ALU/INCDEC on SS:[BP+disp]. No instruction
 * changes BP or SS, reads CF explicitly, or calls out. The last operation
 * must be a full ALU producer, so the successful path need not materialize
 * earlier flags. The ordinary path and all independent entry labels stay. */
static bool frame_member(const x86_insn_t *in)
{
    return in->size == 16 && !in->mem_direct && in->mem_rm == 6 &&
           in->mem_mod != 0 && in->mem_seg == 2 && x86_has_mem(in) &&
           (in->op == XO_MOV || in->op == XO_INCDEC ||
            (in->op == XO_ALU && !x86_is_carry_op(in))) &&
           !(in->a.kind == OPD_REG && (in->a.reg == 5 /* BP */ || in->a.reg == REG16_SP));
}

/* Emit a bounded prefix only at the start of a run (or an independent
 * entry). The guard proves every access before changing any guest state.
 * A single zero code-map byte covers 512 bytes, including every possible
 * write start in this span. A marked byte is conservatively slow. */
static void emit_frame_run(tctx_t *c, int i)
{
    if (!frame_member(&c->in[i]) ||
        (i && !c->leader[i] && c->fall_idx[i - 1] == i && frame_member(&c->in[i - 1])))
        return;
    int lo = (int16_t)c->in[i].disp, hi = lo + 2;
    int end = -1, best_lo = 0, best_hi = 0;
    bool write = false;
    for (int j = i; j < c->n - 1 && j < i + 8; ++j) {
        const x86_insn_t *in = &c->in[j];
        if (!frame_member(in) || c->fall_idx[j] != j + 1)
            break;
        const int d = (int16_t)in->disp;
        if (d < lo) lo = d;
        if (d + 2 > hi) hi = d + 2;
        if (hi - lo > 64)
            break;
        write |= in->a.kind == OPD_MEM &&
                 (in->op != XO_ALU || (in->sub != ALU_CMP && in->sub != ALU_TEST));
        if (write && j >= i + 2 && in->op == XO_ALU) {
            end = j + 1;
            best_lo = lo;
            best_hi = hi;
        }
    }
    if (end < 0)
        return;

    arm_emit_t *e = &c->e;
    x86_insn_t first = c->in[i];
    first.disp = (uint16_t)best_lo;
    const uint32_t last = (uint32_t)(best_hi - best_lo - 1);
    uint32_t slow[4];
    emit_offset(c, &first);                                  /* wrapped offset */
    arm_addw(e, A_R1, A_R0, last);
    arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R1, SH_LSR, 16);
    slow[0] = arm_branch_slot(e);                             /* offset wrap / #GP */
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 8u);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    arm_dp_reg(e, DP_ADD, 0, A_R1, A_R1, A_R12, SH_LSL, 0);    /* linear last */
    const uint32_t limit = c->rt->mem_size < XLAT_CODE_LIMIT ? c->rt->mem_size : XLAT_CODE_LIMIT;
    if (!arm_dp_imm(e, DP_SUB, 1, A_PC, A_R1, limit)) {
        arm_mov32(e, A_R12, limit);
        arm_dp_reg(e, DP_SUB, 1, A_PC, A_R1, A_R12, SH_LSL, 0);
    }
    slow[1] = arm_branch_slot(e);                             /* not direct RAM */
    arm_dp_reg(e, DP_EOR, 0, A_R2, A_R0, A_R1, SH_LSL, 0);
    arm_dp_reg(e, DP_ORR, 1, A_R2, A_PC, A_R2, SH_LSR, XLAT_CODE_BLOCK_SHIFT + 3);
    slow[2] = arm_branch_slot(e);                             /* crosses map byte */
    arm_lsr(e, A_R2, A_R0, XLAT_CODE_BLOCK_SHIFT + 3);
    arm_ls_imm(e, LS_LDR, A_R3, A_R11, XLAT_RT_CODE_MAP);
    arm_ls_reg(e, LS_LDRB, A_R2, A_R3, A_R2);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R2, 0);
    slow[3] = arm_branch_slot(e);                             /* possible SMC */
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_MEM);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);    /* host pointer */

    for (int j = i; j < end; ++j) {
        const x86_insn_t *in = &c->in[j];
        const int off = (int16_t)in->disp - best_lo;
        if (in->op == XO_MOV) {
            int v;
            if (in->b.kind == OPD_MEM) {
                arm_ls_imm(e, LS_LDRH, A_R1, A_R0, (uint32_t)off);
                v = A_R1;
            } else {
                v = get_raw(c, &in->b, 16, A_R1);
            }
            put_raw_at(c, &in->a, 16, v, off);
        } else {
            emit_alu_at(c, j, off, j + 1 != end);
        }
    }
    /* Forward edge only: normal back-edge budget, original instruction
     * accounting and all independently reachable instructions are retained. */
    emit_branch(c, (dest_t){D_INSN, (int16_t)end}, C_AL);
    local_patch(c, slow[0], C_NE);
    local_patch(c, slow[1], C_CS);
    local_patch(c, slow[2], C_NE);
    local_patch(c, slow[3], C_NE);
}

/* A bounded block of LODS; accumulator ALU imm; STOS; LOOP. The source
 * and destination are checked in full before committing any guest state.
 * Each element is loaded and stored in order, including overlapping ranges.
 * The last ALU uses its ordinary flag emitter; only the earlier results
 * are dead. The ordinary LOOP charges the final edge and observes IRQs. */
static void emit_lods_alu_stos(tctx_t *c, int i)
{
    enum { count = 16, charge = (count - 1) * 4 };
    if (i + 3 >= c->n || c->fall_idx[i] != i + 1 ||
        c->fall_idx[i + 1] != i + 2 || c->fall_idx[i + 2] != i + 3)
        return;
    const x86_insn_t *l = &c->in[i], *a = &c->in[i + 1];
    const x86_insn_t *s = &c->in[i + 2], *b = &c->in[i + 3];
    const int size = l->size;
    if (l->op != XO_STR || l->sub != STR_LODS || l->rep || l->mem_seg > 3 ||
        (size != 8 && size != 16) || a->op != XO_ALU || a->size != size ||
        a->a.kind != OPD_REG || a->a.reg != 0 || a->b.kind != OPD_IMM ||
        s->op != XO_STR || s->sub != STR_STOS || s->rep || s->size != size ||
        b->op != XO_LOOP || b->size != 16 || c->tgt_idx[i + 3] != i ||
        l->pad || a->pad || s->pad || b->pad || !c->mat[i + 1])
        return;
    int dp;
    switch (a->sub) {
    case ALU_ADD: dp = DP_ADD; break;
    case ALU_SUB: dp = DP_SUB; break;
    case ALU_AND: dp = DP_AND; break;
    case ALU_OR:  dp = DP_ORR; break;
    case ALU_XOR: dp = DP_EOR; break;
    default: return;
    }
    arm_emit_t *e = &c->e;
    const uint32_t bytes = (uint32_t)size / 8u;
    const uint32_t span = count * bytes, last = span - 1u;
    const uint32_t imm = a->b.imm & (size == 8 ? 0xffu : 0xffffu);
    const uint32_t limit = c->rt->mem_size < XLAT_CODE_LIMIT ? c->rt->mem_size : XLAT_CODE_LIMIT;
    uint32_t slow[8];
    /* CX=0 denotes 65536 iterations for LOOP; use the normal path there.
     * A positive budget after every skipped edge excludes back_slow. */
    arm_uxth(e, A_R12, A_R5);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, count);
    slow[0] = arm_branch_slot(e);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, charge);
    slow[1] = arm_branch_slot(e);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_EFLAGS);
    arm_dp_imm(e, DP_AND, 1, A_PC, A_R12, 0x400u);
    slow[2] = arm_branch_slot(e);                         /* DF != 0 */

    arm_uxth(e, A_R0, A_R10);                            /* destination */
    arm_uxth(e, A_R1, A_R9);                             /* source */
    arm_addw(e, A_R2, A_R0, last);
    arm_addw(e, A_R3, A_R1, last);
    arm_dp_reg(e, DP_ORR, 0, A_R12, A_R2, A_R3, SH_LSL, 0);
    arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R12, SH_LSR, 16);
    slow[3] = arm_branch_slot(e);                         /* offset wrap */
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    arm_dp_reg(e, DP_ADD, 0, A_R2, A_R2, A_R12, SH_LSL, 0);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * l->mem_seg);
    arm_dp_reg(e, DP_ADD, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
    arm_dp_reg(e, DP_ADD, 0, A_R3, A_R3, A_R12, SH_LSL, 0);
    arm_mov32(e, A_R12, limit);
    arm_dp_reg(e, DP_SUB, 1, A_PC, A_R2, A_R12, SH_LSL, 0);
    slow[4] = arm_branch_slot(e);
    arm_dp_reg(e, DP_SUB, 1, A_PC, A_R3, A_R12, SH_LSL, 0);
    slow[5] = arm_branch_slot(e);
    /* One zero code-map byte proves the complete destination, including
     * every possible write start. Cross-byte or marked spans stay slow. */
    arm_dp_reg(e, DP_EOR, 0, A_R3, A_R0, A_R2, SH_LSL, 0);
    arm_dp_reg(e, DP_ORR, 1, A_R3, A_PC, A_R3, SH_LSR, XLAT_CODE_BLOCK_SHIFT + 3);
    slow[6] = arm_branch_slot(e);
    arm_lsr(e, A_R3, A_R0, XLAT_CODE_BLOCK_SHIFT + 3);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_CODE_MAP);
    arm_ls_reg(e, LS_LDRB, A_R3, A_R12, A_R3);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R3, 0);
    slow[7] = arm_branch_slot(e);

    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_MEM);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
    arm_dp_reg(e, DP_ADD, 0, A_R1, A_R1, A_R12, SH_LSL, 0);
    const bool imm_ok = arm_thumb_imm(imm) >= 0;
    if (!imm_ok)
        arm_mov32(e, A_R3, imm);
    for (unsigned j = 0; j < count - 1u; ++j) {
        arm_ls_imm(e, ls_load(size), A_R2, A_R1, j * bytes);
        if (imm_ok)
            arm_dp_imm(e, dp, 0, A_R2, A_R2, imm);
        else
            arm_dp_reg(e, dp, 0, A_R2, A_R2, A_R3, SH_LSL, 0);
        arm_ls_imm(e, ls_store(size), A_R2, A_R0, j * bytes);
    }
    arm_ls_imm(e, ls_load(size), A_R2, A_R1, span - bytes);
    put_raw(c, &a->a, size, A_R2);
    arm_addw(e, A_R12, A_R9, span);
    arm_bfi(e, A_R9, A_R12, 0, 16);
    arm_addw(e, A_R12, A_R10, span);
    arm_bfi(e, A_R10, A_R12, 0, 16);
    arm_subw(e, A_R12, A_R5, count - 1u);
    arm_bfi(e, A_R5, A_R12, 0, 16);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
    arm_subw(e, A_R12, A_R12, charge);
    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_BUDGET);
    /* r0 remains the host destination across the register/immediate ALU.
     * Keep the ordinary lazy-flags representation, including CF and AF. */
    emit_alu_at(c, i + 1, -1, false);
    arm_ls_imm(e, ls_store(size), A_R4, A_R0, span - bytes);
    emit_branch(c, (dest_t){D_INSN, (int16_t)(i + 3)}, C_AL);
    static const uint8_t cond[8] = {C_CC, C_LE, C_NE, C_NE, C_CS, C_CS, C_NE, C_NE};
    for (unsigned j = 0; j < 8; ++j)
        local_patch(c, slow[j], cond[j]);
}

/* A closed register-only LOOP body. For the first fifteen iterations the
 * low 16 bits of each result are independent of all high bits and flags.
 * Save the affected high halves once; the original final iteration makes
 * the lazy flags and the last budget/IRQ edge exactly as before. */
static void emit_reg_loop(tctx_t *c, int i)
{
    enum { count = 16, unroll = 3, repeats = (count - 1) / unroll };
    static const int scratch[] = {A_R0, A_R1, A_R2, A_R3, A_LR};
    int save[8], dp[8], end = i, nsave = 0;
    if (c->in[i].op != XO_ALU)
        return;                         /* first op must replace all arithmetic flags */
    for (int k = 0; k < 8; ++k)
        save[k] = -1;
    for (; end < c->n && end < i + 9; ++end) {
        const x86_insn_t *in = &c->in[end];
        if (in->op == XO_LOOP)
            break;
        if (end == i + 8 || in->size != 16 || in->pad ||
            c->fall_idx[end] != end + 1 || in->a.kind != OPD_REG ||
            in->a.reg >= 8 || in->a.reg == 1 || in->a.reg == REG16_SP)
            return;
        int op;
        if (in->op == XO_INCDEC) {
            op = in->sub ? DP_SUB : DP_ADD;
        } else if (in->op == XO_ALU) {
            switch (in->sub) {
            case ALU_ADD: op = DP_ADD; break;
            case ALU_SUB: op = DP_SUB; break;
            case ALU_AND: op = DP_AND; break;
            case ALU_OR:  op = DP_ORR; break;
            case ALU_XOR: op = DP_EOR; break;
            default: return;
            }
            if (in->b.kind == OPD_REG) {
                if (in->b.reg >= 8 || in->b.reg == 1 || in->b.reg == REG16_SP)
                    return;
            } else if (in->b.kind != OPD_IMM || arm_thumb_imm(in->b.imm & 0xffffu) < 0) {
                return;
            }
        } else {
            return;
        }
        dp[end - i] = op;
        if (save[in->a.reg] < 0) {
            if (nsave == (int)(sizeof(scratch) / sizeof(scratch[0])))
                return;
            save[in->a.reg] = scratch[nsave++];
        }
    }
    if (end >= c->n || end - i < 3 || c->in[end].op != XO_LOOP ||
        c->in[end].size != 16 || c->in[end].pad || c->tgt_idx[end] != i)
        return;
    arm_emit_t *e = &c->e;
    const uint32_t charge = (count - 1u) * (uint32_t)(end - i + 1);
    arm_uxth(e, A_R12, A_R5);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, count);
    const uint32_t short_cx = arm_branch_slot(e);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, charge);
    const uint32_t short_budget = arm_branch_slot(e);

    for (int k = 0; k < 8; ++k)
        if (save[k] >= 0)
            arm_lsr(e, save[k], arm_reg16[k], 16);
    arm_mov32(e, A_R12, repeats);
    const uint32_t top = e->pos;
    for (int u = 0; u < unroll; ++u) {
        for (int j = i; j < end; ++j) {
            const x86_insn_t *in = &c->in[j];
            const int rd = arm_reg16[in->a.reg], op = dp[j - i];
            if (in->op == XO_INCDEC)
                arm_dp_imm(e, op, 0, rd, rd, 1u);
            else if (in->b.kind == OPD_IMM)
                arm_dp_imm(e, op, 0, rd, rd, in->b.imm & 0xffffu);
            else
                arm_dp_reg(e, op, 0, rd, rd, arm_reg16[in->b.reg], SH_LSL, 0);
        }
    }
    arm_dp_imm(e, DP_SUB, 1, A_R12, A_R12, 1u);
    const uint32_t back = arm_branch_slot(e);
    if (!arm_patch_branch(e, back, top, C_NE))
        c->overflow = true;
    for (int k = 0; k < 8; ++k)
        if (save[k] >= 0)
            arm_bfi(e, arm_reg16[k], save[k], 16, 16);
    arm_subw(e, A_R12, A_R5, count - 1u);
    arm_bfi(e, A_R5, A_R12, 0, 16);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
    arm_subw(e, A_R12, A_R12, charge);
    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_BUDGET);
    /* No skipped edge would have reached back_slow. The original labels,
     * final iteration, LOOP and independent entries are left untouched. */
    local_patch(c, short_cx, C_CC);
    local_patch(c, short_budget, C_LE);
}

static void emit_insn(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    c->label[i] = e->pos;
    emit_frame_run(c, i);
    if (in->op == XO_ALU && c->leader[i])
        emit_reg_loop(c, i);
    switch (in->op) {
    case XO_ALU:
    case XO_INCDEC:
        emit_alu_at(c, i, -1, false);
        break;
    case XO_MOV:
        emit_mov(c, i);
        break;
    case XO_LEA:
        emit_offset(c, in);
        put_raw(c, &in->a, 16, A_R0);
        break;
    case XO_XCHG:
        emit_xchg(c, i);
        break;
    case XO_NOP:
        break;
    case XO_JCC:
        emit_jcc(c, i);
        break;
    case XO_JMP:
        emit_branch(c, make_dest(c, i, in->target), C_AL);
        break;
    case XO_LOOP:
        arm_subw(e, A_R12, A_R5, 1);
        arm_bfi(e, A_R5, A_R12, 0, 16);
        arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R5, SH_LSL, 16);   /* LSLS: Z = (CX == 0) */
        emit_branch(c, make_dest(c, i, in->target), C_NE);
        break;
    case XO_JCXZ:
        arm_dp_reg(e, DP_ORR, 1, A_R12, A_PC, A_R5, SH_LSL, 16);
        emit_branch(c, make_dest(c, i, in->target), C_EQ);
        break;
    case XO_PUSH:
    case XO_POP:
    case XO_CALL:
    case XO_RET:
        emit_stack_op(c, i);
        break;
    case XO_INTERP:
        emit_interp(c, i);
        break;
    case XO_STR:
        if (x86_str_flags(in))
            emit_string_cmp(c, i);
        else {
            if (in->sub == STR_LODS && !in->rep)
                emit_lods_alu_stos(c, i);
            emit_string(c, i);
        }
        break;
    case XO_SHIFT:
        emit_shift(c, i);
        break;
    case XO_MULDIV:
        emit_muldiv(c, i);
        break;
    case XO_MISC:
        emit_misc(c, i);
        break;
    case XO_SEG:
        emit_seg(c, i);
        break;
    case XO_FAR:
        emit_far(c, i);
        break;
    case XO_IND:
        emit_ind(c, i);
        break;
    case XO_STK:
        emit_stk(c, i);
        break;
    case XO_FLAG:
        emit_flag(c, i);
        break;
    case XO_LOOPCC:
        emit_loopcc(c, i);
        break;
    case XO_FPU:
        emit_fpu(c, i);
        break;
    case XO_IO:
        emit_io(c, i);
        break;
    }
    if (falls_through(in) && c->fall_idx[i] != i + 1)
        emit_branch(c, make_dest(c, i, (uint16_t)(in->ip + in->len)), C_AL);
}

/* An instruction with more than one access: when one of them cannot be done
 * here, XRIP does all, and a write among the others may hit this region's
 * code (MOVS reading outside fast memory and writing into it, PUSH [m],
 * PUSHA with SP wrapping, ...). The region is checked after it then. */
static bool side_writes_more(const x86_insn_t *in)
{
    switch (in->op) {
    case XO_STR:  return in->sub == STR_MOVS;
    case XO_PUSH: return in->b.kind == OPD_MEM;
    case XO_POP:  return in->a.kind == OPD_MEM;
    case XO_IND:  return in->sub == IND_CALL && in->b.kind == OPD_MEM;
    case XO_FAR:  return in->sub == FAR_CALL || in->sub == FAR_CALLM;
    case XO_STK:  return in->sub == STK_PUSHA || in->sub == STK_ENTER1;
    default:      return false;
    }
}

/* Unlike new ST_EXIT stubs, these local tails do not consume another
 * descriptor during the second emission pass. Flags already reach rt
 * through the side reconstruction. */
static void emit_side_dispatch(tctx_t *c, uint32_t kind)
{
    arm_emit_t *e = &c->e;
    arm_mov32(e, A_R12, kind);
    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
    const uint32_t site = arm_branch_slot(e);
    if (!arm_patch_branch(e, site, kind == XLAT_EXIT_BRANCH ? c->dispatch : c->epilogue, C_AL))
        c->overflow = true;
}

static void emit_side_tail(tctx_t *c, uint16_t ip, uint32_t kind)
{
    arm_movw(&c->e, A_R12, ip);
    arm_ls_imm(&c->e, LS_STR, A_R12, A_R11, XLAT_RT_IP);
    emit_side_dispatch(c, kind);
}

/* Emit only a BL, eight halfwords and (when needed) a continuation
 * branch. All memory semantics and the cold-path register snapshot are
 * shared across regions. This replaces the per-instruction ARM helpers. */
static bool emit_scalar_side(tctx_t *c, int i, bool cont, uint32_t xi)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    if (!native_scalar(in))
        return false;
    unsigned op, reg = 0, bytes = in->size / 8u, flags = 0;
    unsigned ea = in->mem_rm | (unsigned)in->mem_seg << 3 |
                  (in->mem_direct ? SC_EA_DIRECT : 0u);
    uint16_t next = (uint16_t)(in->ip + in->len);
    uint32_t imm = 0;
    const x86_opd_t *src = NULL;
    switch (in->op) {
    case XO_STR:
        op = SC_STR_READ;
        ea = (unsigned)in->sub | (unsigned)in->rep << SC_STR_REP_SHIFT |
             (unsigned)in->mem_seg << SC_STR_SEG_SHIFT;
        break;
    case XO_MOV:
        if (in->a.kind == OPD_MEM) {
            op = SC_STORE;
            src = &in->b;
        } else {
            op = SC_LOAD;
            reg = in->a.reg;
        }
        break;
    case XO_ALU:
        op = SC_ADD;
        src = &in->b;
        break;
    case XO_MISC:
        if (in->sub == MISC_XLAT) {
            op = SC_LOAD;
            bytes = 1;
            ea |= SC_EA_XLAT;
        } else {
            op = in->sub == MISC_MOVSX ? SC_MOVSX : SC_MOVZX;
            reg = in->a.reg;
            if (in->local)
                flags |= SC_WIDE;
        }
        break;
    case XO_PUSH: op = SC_PUSH; bytes = 2; src = &in->b; break;
    case XO_POP: op = SC_POP; bytes = 2; reg = in->a.reg; break;
    case XO_CALL:
        op = SC_CALL;
        bytes = 2;
        imm = next;
        flags |= SC_IMM;
        next = in->target;
        break;
    case XO_RET: case XO_FAR:
        op = in->op == XO_RET ? SC_RET : SC_RETF;
        bytes = 2;
        imm = in->b.imm;
        break;
    case XO_SEG:
        bytes = 2;
        reg = in->sreg;
        switch (in->sub) {
        case SEG_LOAD: op = SC_SEG_LOAD; break;
        case SEG_STORE: op = SC_SEG_STORE; break;
        case SEG_PUSH: op = SC_SEG_PUSH; break;
        default: op = SC_SEG_POP; break;
        }
        break;
    default: return false;
    }
    if (src) {
        if (src->kind == OPD_IMM) {
            flags |= SC_IMM;
            imm = src->imm;
        } else
            reg = src->reg;
    }
    const bool resume = (cont || op == SC_ADD) && c->fall_idx[i] > i;
    if (resume)
        flags |= SC_CONT;
    unsigned path;
    switch (op) {
    case SC_LOAD: case SC_MOVZX: case SC_MOVSX: case SC_SEG_LOAD:
        path = SC_PATH_LOAD; break;
    case SC_STORE: case SC_SEG_STORE: path = SC_PATH_STORE; break;
    case SC_ADD: path = SC_PATH_ADD; break;
    case SC_CALL: case SC_PUSH: case SC_SEG_PUSH: path = SC_PATH_PUSH; break;
    case SC_POP: case SC_SEG_POP: case SC_RET: case SC_RETF:
        path = SC_PATH_POP; break;
    case SC_STR_READ: path = SC_PATH_STR_READ; break;
    default: c->overflow = true; return true;
    }
    /* The failed memory check and lazy reconstruction preserve r0
     * (linear address). Writes need only an explicit value in r1; the
     * guest GPRs remain in the AAPCS callee-saved registers. */
    if (path == SC_PATH_STORE || path == SC_PATH_ADD) {
        if (op == SC_SEG_STORE) {
            arm_ls_imm(e, LS_LDR, A_R1, A_R11, XLAT_RT_SEG_BASE + 4u * reg);
            arm_lsr(e, A_R1, A_R1, 4);
        } else if (flags & SC_IMM) {
            arm_mov32(e, A_R1, imm);
        } else if (bytes == 1u) {
            arm_ubfx(e, A_R1, arm_reg16[reg & 3u], reg >= 4u ? 8 : 0, 8);
        } else if (reg == REG16_SP) {
            arm_ls_imm(e, bytes == 2u ? LS_LDRH : LS_LDR, A_R1, A_R11, XLAT_RT_SP);
        } else if (bytes == 2u) {
            arm_uxth(e, A_R1, arm_reg16[reg]);
        } else {
            arm_mov(e, A_R1, arm_reg16[reg]);
        }
    }
    if (!arm_bl(e, c->scalar_call[path]))
        c->overflow = true;
    arm_e16(e, (uint16_t)(op | (bytes == 4 ? 2u : bytes == 2 ? 1u : 0u) << 4 | reg << 6 | flags));
    arm_e16(e, (uint16_t)ea);
    arm_e16(e, in->disp);
    arm_e16(e, (uint16_t)imm);
    arm_e16(e, (uint16_t)(imm >> 16));
    arm_e16(e, next);
    /* Last four bytes are also an ordinary interpreter payload. */
    arm_e16(e, in->ip);
    arm_e16(e, (uint16_t)(xi | (uint32_t)(in->len & 15u) << 8 | (cont ? 0x8000u : 0u)));
    if (resume || cont) {
        const uint32_t site = arm_branch_slot(e);
        if (!arm_patch_branch(e, site, c->label[c->fall_idx[i]], C_AL))
            c->overflow = true;
    }
    return true;
}

static bool emit_stubs(tctx_t *c)
{
    arm_emit_t *e = &c->e;
    /* Back-edge stubs first: they branch to exit stubs emitted after them. */
    for (; c->stub_pass < 2; ++c->stub_pass, c->stub_i = 0) {
        for (; c->stub_i < c->n_stubs; ++c->stub_i) {
            if (compiler_checkpoint(c->rt))
                return false;
            stub_t *s = &c->stub[c->stub_i];
            if (c->stub_pass ? s->type == ST_BACK : s->type != ST_BACK)
                continue;
            s->pos = e->pos;
            const uint32_t from = e->pos;
            (void)from;
            for (int j = 0; j < s->nsites; ++j)
                if (!arm_patch_branch(e, s->site[j], s->pos, s->cond[j]))
                    c->overflow = true;
            if (s->type == ST_BACK) {
                /* Budget used up: a pending IRQ, the burst's deadline leave
                 * (back_slow), else the loop goes on for XLAT_SLICE_CHECK
                 * instructions. IRQs are seen there, every XLAT_SLICE_CHECK
                 * instructions at most, as XRIP sees them between bursts. */
                const dest_t ex = {D_STUB, s->exit_stub};
                arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
                arm_dp_imm(e, DP_SUB, 1, A_R12, A_R12, s->budget_n);
                arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_BUDGET);
                const uint32_t slow = arm_branch_slot(e);
                uint32_t site = arm_branch_slot(e);
                if (!arm_patch_branch(e, site, c->label[s->target], C_AL))
                    c->overflow = true;
                local_patch(c, slow, C_LE);
                if (!arm_bl(e, c->back_slow))
                    c->overflow = true;
                emit_branch(c, ex, C_PL);
                site = arm_branch_slot(e);
                if (!arm_patch_branch(e, site, c->label[s->target], C_AL))
                    c->overflow = true;
                SIZE_NOTE(XLAT_SIZE_BACK, from);
            } else if (s->type == ST_SIDE) {
                /* The access cannot be done here: XRIP executes the whole
                 * instruction. A MOV, PUSH or POP sets no flags, so the code
                 * after it may go on; anything else leaves after it. */
                const int i = s->insn;
                const x86_insn_t *in = &c->in[i];
                const bool cont = falls_through(in) && !is_producer(in) && c->fall_idx[i] >= 0;
                if (s->recon >= 0)
                    emit_recon(c, s->recon);
                const uint32_t ctl = native_string(in) ? string_ctl(in) :
                    (s->mw || side_writes_more(in) ? XI_MAY_WRITE : 0u) | XI_SIDE;
                if (emit_scalar_side(c, i, cont, ctl)) {
                    SIZE_NOTE(XLAT_SIZE_SIDE, from);
                    continue;
                }
                if (native_string(in) && !in->rep && cont) {
                    /* A single VGA element need not spill the guest GPRs.
                     * Unknown mappings and code writes use the old thunk. */
                    emit_string_args(c, i);
                    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_string_fast);
                    arm_blx(e, A_R12);
                    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
                    const uint32_t slow = arm_branch_slot(e);
                    emit_string_update(c, i);
                    emit_string_irq(c, (uint16_t)(in->ip + in->len));
                    const uint32_t site = arm_branch_slot(e);
                    if (!arm_patch_branch(e, site, c->label[c->fall_idx[i]], C_AL))
                        c->overflow = true;
                    local_patch(c, slow, C_EQ);
                }
                if (native_string(in)) {
                    arm_mov(e, A_R0, A_R11);
                    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_string_ready);
                    arm_blx(e, A_R12);
                    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
                    const uint32_t ready = arm_branch_slot(e);
                    emit_side_tail(c, in->ip, XLAT_EXIT_BUDGET);
                    local_patch(c, ready, C_NE);
                }
                emit_interp_call(c, i, ctl, cont);
                if (cont) {
                    const uint32_t site = arm_branch_slot(e);
                    if (!arm_patch_branch(e, site, c->label[c->fall_idx[i]], C_AL))
                        c->overflow = true;
                }
                SIZE_NOTE(XLAT_SIZE_SIDE, from);
            } else {
                if (s->recon >= 0)
                    emit_recon(c, s->recon);
                /* shared tails (xlat_emit_trampoline); a BRANCH exit goes on
                 * in the region at its target, if any (the dispatcher leaves
                 * through the epilogue otherwise) */
                if (s->dyn && s->exit_kind == XLAT_EXIT_BRANCH) {
                    const uint32_t site = arm_branch_slot(e);
                    if (!arm_patch_branch(e, site, c->exit_dyn, C_AL))
                        c->overflow = true;
                } else if (!s->dyn && (s->exit_kind == XLAT_EXIT_BRANCH ||
                                       s->exit_kind == XLAT_EXIT_BUDGET)) {
                    if (!arm_bl(e, c->exit_ip[s->exit_kind == XLAT_EXIT_BUDGET]))
                        c->overflow = true;
                    arm_e16(e, s->ip);
                } else {
                    if (!s->dyn) {
                        arm_movw(e, A_R12, s->ip);
                        arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_IP);
                    }
                    arm_mov32(e, A_R12, s->exit_kind);
                    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
                    const uint32_t site = arm_branch_slot(e);
                    const uint32_t tail = s->exit_kind == XLAT_EXIT_FAR_CALL ? c->dispatch : c->epilogue;
                    if (!arm_patch_branch(e, site, tail, C_AL))
                        c->overflow = true;
                }
                SIZE_NOTE(XLAT_SIZE_EXIT, from);
            }
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Trampoline and entry point                                             */
/* ---------------------------------------------------------------------- */

/* B<cond> from slot at to target (inside the trampoline). */
static bool tramp_patch(arm_emit_t *e, uint32_t at, uint32_t target, int cond)
{
    return arm_patch_branch(e, at, target, cond);
}

/* The upper memory test of the trampolines: r12 = the last byte of an
 * access whose first byte is at most XLAT_UMB_MARGIN before it. Sets C when
 * the access is not in [XLAT_UMB_BASE, + XLAT_UMB_MARGIN + rt->umb_room)
 * (xlat.h). Clobbers r12 and tmp. */
static void tramp_umb(arm_emit_t *e, int tmp)
{
    arm_ls_imm(e, LS_LDR, tmp, A_R11, XLAT_RT_UMB);
    arm_dp_imm(e, DP_SUB, 0, A_R12, A_R12, XLAT_UMB_BASE);
    arm_subw(e, A_R12, A_R12, XLAT_UMB_MARGIN);
    arm_dp_reg(e, DP_SUB, 1, A_PC, A_R12, tmp, SH_LSL, 0);   /* CMP: CS = outside */
}

bool xlat_emit_trampoline(xlat_arena_t *a)
{
    arm_emit_t e = {a->base, 0, a->size, false, 0};
    bool ok = true;
    /* entry(rt = r0, target | 1 = r1) */
    arm_push(&e, 0x4ff8u);                     /* r3-r11, lr: 8-byte aligned */
    arm_mov(&e, A_R11, A_R0);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define LOAD(r, off) arm_ls_imm(&e, LS_LDR, r, A_R12, off);
    GPR_LIST(LOAD)
#undef LOAD
    arm_ls_imm(&e, LS_LDR, A_R0, A_R12, 16);   /* ESP -> rt->sp */
    arm_ls_imm(&e, LS_STR, A_R0, A_R11, XLAT_RT_SP);
    arm_bx(&e, A_R1);
    arm_e16(&e, 0xbf00u);                      /* align the epilogue */
    a->epilogue = e.pos;
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define STORE(r, off) arm_ls_imm(&e, LS_STR, r, A_R12, off);
    GPR_LIST(STORE)
#undef STORE
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_SP);
    arm_ls_imm(&e, LS_STR, A_R0, A_R12, 16);
    arm_pop(&e, 0x8ff8u);                      /* r3-r11, pc */

    /* Dispatcher, reached by B from a BRANCH exit stub: rt->ip and
     * exit_kind stored, the lazy flags in rt, the GPRs still in r4-r10 (as
     * at a region entry). The region at rt->cs:ip is entered at once when
     * the link table has a valid entry, no IRQ is pending and
     * the budget covers the entry. A table miss may refill a live target
     * without leaving the trampoline; true misses return to xlat_run().
     * r0-r3, r12 and lr are free here. */
    a->dispatch = e.pos;
#if !XLAT_EVENT_GATE
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_INTR);
    arm_ls_imm(&e, LS_LDRB, A_R0, A_R0, 0);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 0);                 /* CMP r0, #0 */
    const uint32_t miss1 = arm_branch_slot(&e);
#endif
    arm_ls_imm(&e, LS_LDRH, A_R1, A_R11, XLAT_RT_CS);
    arm_ls_imm(&e, LS_LDRH, A_R2, A_R11, XLAT_RT_IP);         /* ip & 0xffff */
    arm_dp_reg(&e, DP_ORR, 0, A_R1, A_R2, A_R1, SH_LSL, 16);  /* key */
    arm_dp_reg(&e, DP_EOR, 0, A_R2, A_R1, A_R1, SH_LSR, 7);   /* xlat_link_hash */
    arm_dp_reg(&e, DP_EOR, 0, A_R2, A_R2, A_R1, SH_LSR, 16);
    arm_dp_imm(&e, DP_AND, 0, A_R2, A_R2, XLAT_LINK_SLOTS - 1u);
    arm_ls_imm(&e, LS_LDR, A_R3, A_R11, XLAT_RT_LINK);
    arm_dp_reg(&e, DP_ADD, 0, A_R3, A_R3, A_R2, SH_LSL, 4);
    /* The dispatcher is a tail transfer: LR is scratch here (the host LR
     * is saved by the outer trampoline). Fetch the complete aligned link
     * in one LDM: r0=key, r2=code, r12=reserved, lr=charge | region<<16.
     * Keep code in r2 until BX; no second metadata load after validation. */
    arm_e32(&e, 0xe893u, 0x5005u);                           /* LDM.W r3,{r0,r2,r12,lr} */
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R0, A_R1, SH_LSL, 0);   /* CMP key */
    const uint32_t miss2 = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_DISPATCH_GATE);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 0);  /* IRQ gate open? */
    const uint32_t miss3 = arm_branch_slot(&e);
    const uint32_t link_charge = e.pos;
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_BUDGET);
    arm_uxth(&e, A_R1, A_LR);
    arm_dp_reg(&e, DP_SUB, 1, A_R0, A_R0, A_R1, SH_LSL, 0);   /* SUBS budget, charge */
    const uint32_t miss4 = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_STR, A_R0, A_R11, XLAT_RT_BUDGET);
    arm_lsr(&e, A_R1, A_LR, 16);                             /* region */
    arm_ls_imm(&e, LS_STR, A_R1, A_R11, XLAT_RT_CUR);
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_LAST);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_SEQ);
    arm_e32(&e, 0xf840u, 0xc021u);                           /* STR.W r12,[r0,r1,LSL #2] */
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_LINKS);
    arm_dp_imm(&e, DP_ADD, 0, A_R0, A_R0, 1);
    arm_ls_imm(&e, LS_STR, A_R0, A_R11, XLAT_RT_LINKS);
    arm_bx(&e, A_R2);
    /* Secondary bank is consulted only on a primary key collision.
     * Both banks remain contiguous in PSRAM. */
    const uint32_t alt_link = e.pos;
    ok &= arm_dp_imm(&e, DP_ADD, 0, A_R3, A_R3, XLAT_LINK_SLOTS * sizeof(xlat_link_t));
    arm_e32(&e, 0xe893u, 0x5005u);                           /* LDM.W r3,{r0,r2,r12,lr} */
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R0, A_R1, SH_LSL, 0);
    const uint32_t alt_miss = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_DISPATCH_GATE);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 0);  /* IRQ gate open? */
    const uint32_t alt_stale = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_LINK_ALT);
    arm_dp_imm(&e, DP_ADD, 0, A_R0, A_R0, 1);
    arm_ls_imm(&e, LS_STR, A_R0, A_R11, XLAT_RT_LINK_ALT);
    const uint32_t alt_charge = arm_branch_slot(&e);
    /* Cold link lookup: the outer trampoline already saves host LR and
     * has an AAPCS-aligned stack. C preserves r4-r11 (guest GPRs + rt).
     * A prepared entry returns through the SAME charge/IRQ gate path. */
    const uint32_t refill = e.pos;
    arm_mov(&e, A_R0, A_R11);
    arm_mov32(&e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_link_refill);
    arm_blx(&e, A_R12);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 0);
    const uint32_t refill_miss = arm_branch_slot(&e);
    arm_mov(&e, A_R3, A_R0);
    arm_e32(&e, 0xe893u, 0x5005u);                           /* LDM.W r3,{r0,r2,r12,lr} */
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_DISPATCH_GATE);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 0);  /* IRQ gate open? */
    const uint32_t refill_stale = arm_branch_slot(&e);
    const uint32_t refill_charge = arm_branch_slot(&e);
#if !XLAT_EVENT_GATE
    ok &= tramp_patch(&e, miss1, a->epilogue, C_NE);
#endif
    ok &= tramp_patch(&e, miss2, alt_link, C_NE) &&
          tramp_patch(&e, miss3, refill, C_NE) && tramp_patch(&e, miss4, a->epilogue, C_LE) &&
          tramp_patch(&e, alt_miss, refill, C_NE) && tramp_patch(&e, alt_stale, refill, C_NE) &&
          tramp_patch(&e, alt_charge, link_charge, C_AL) &&
          tramp_patch(&e, refill_miss, a->epilogue, C_EQ) &&
          tramp_patch(&e, refill_stale, a->epilogue, C_NE) &&
          tramp_patch(&e, refill_charge, link_charge, C_AL);

    /* Exit stubs (emit_stubs): BL here with the exit IP in the halfword
     * after the BL (exit_ip), or B to exit_dyn when the code stored rt->ip.
     * They never return. */
    static const uint8_t exit_kinds[2] = {XLAT_EXIT_BRANCH, XLAT_EXIT_BUDGET};
    for (int k = 0; k < 2; ++k) {
        a->exit_ip[k] = e.pos;
        arm_subw(&e, A_R12, A_LR, 1);                          /* the halfword */
        arm_ls_imm(&e, LS_LDRH, A_R12, A_R12, 0);
        arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_IP);
        if (k == 0)
            a->exit_dyn = e.pos;
        arm_mov32(&e, A_R12, exit_kinds[k]);
        arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
        const uint32_t to = arm_branch_slot(&e);
        ok &= tramp_patch(&e, to, k == 0 ? a->dispatch : a->epilogue, C_AL);
    }

    /* Exact write check behind xlat_code_map, BL here from the trampolines
     * below (their lr pushed) with r0 = the first byte, r12 = the last:
     * r12 = 0 when the bytes hold no translated x86 code (rt->gap), 1 when
     * they do (rt->hit), else what xlat_rt_gap() finds. r0-r3 kept. The
     * callers have pushed 16 bytes: the C call has an 8-byte aligned stack. */
    const uint32_t gapchk = e.pos;
    arm_e16(&e, 0xb406u);                                    /* PUSH {r1, r2} */
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_GAP_LO);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R0, A_R1, SH_LSL, 0);
    const uint32_t g_below = arm_branch_slot(&e);           /* addr < gap_lo */
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_GAP_HI);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R1, SH_LSL, 0);
    const uint32_t g_above = arm_branch_slot(&e);           /* last >= gap_hi */
    const uint32_t g_clear = e.pos;
    arm_mov32(&e, A_R12, 0);
    const uint32_t g_ret = e.pos;
    arm_e16(&e, 0xbc06u);                                    /* POP {r1, r2} */
    arm_bx(&e, A_LR);
    /* Stack and data often alternate between two negative ranges. Do not
     * call C merely to exchange them. r0 and r12 remain the first/last. */
    const uint32_t g_second = e.pos;
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_GAP2_LO);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R0, A_R1, SH_LSL, 0);
    const uint32_t g2_below = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_GAP2_HI);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R1, SH_LSL, 0);
    const uint32_t g2_clear = arm_branch_slot(&e);
    const uint32_t g_hit = e.pos;
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_HIT_LO);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R1, SH_LSL, 0);
    const uint32_t h_below = arm_branch_slot(&e);           /* last < hit_lo */
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_HIT_HI);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R0, A_R1, SH_LSL, 0);
    const uint32_t h_above = arm_branch_slot(&e);           /* addr >= hit_hi */
    arm_mov32(&e, A_R12, 1);
    const uint32_t h_ret = arm_branch_slot(&e);
    const uint32_t g_call = e.pos;
    arm_push(&e, 0x4019u);                     /* r0, r3, r4, lr */
    arm_mov(&e, A_R1, A_R12);
    arm_mov(&e, A_R2, A_R11);
    arm_mov32(&e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_gap);
    arm_blx(&e, A_R12);
    arm_mov(&e, A_R12, A_R0);
    arm_pop(&e, 0x4019u);
    const uint32_t c_ret = arm_branch_slot(&e);
    ok &= tramp_patch(&e, g_below, g_second, C_CC) && tramp_patch(&e, g_above, g_second, C_CS) &&
          tramp_patch(&e, g2_below, g_hit, C_CC) && tramp_patch(&e, g2_clear, g_clear, C_CC) &&
          tramp_patch(&e, h_below, g_call, C_CC) && tramp_patch(&e, h_above, g_call, C_CS) &&
          tramp_patch(&e, h_ret, g_ret, C_AL) && tramp_patch(&e, c_ret, g_ret, C_AL);

    /* Write check (emit_checks), BL here with r0 = linear address of an
     * access of 1, 2, 4 bytes (wchk[0..2]) or of up to 128 ending at r12
     * (wchk[3]). Returns CS when the access is outside fast and upper
     * memory (tramp_umb) or it meets translated x86 code (its 64-byte block
     * marked in xlat_code_map, then gapchk), else CC. r0-r2 are kept, r12
     * clobbered. */
    static const uint8_t wlast[3] = {0, 1, 3};
    uint32_t to_common[3];
    for (int k = 0; k < 3; ++k) {
        a->wchk[k] = e.pos;
        if (wlast[k])
            arm_addw(&e, A_R12, A_R0, wlast[k]);
        else
            arm_mov(&e, A_R12, A_R0);
        to_common[k] = arm_branch_slot(&e);
    }
    a->wchk[3] = e.pos;
    for (int k = 0; k < 3; ++k)
        ok &= tramp_patch(&e, to_common[k], a->wchk[3], C_AL);
    arm_e16(&e, 0xb406u);                                    /* PUSH {r1, r2} */
    arm_mov(&e, A_R2, A_R12);                                /* the last byte */
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_MEM_SIZE);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R1, SH_LSL, 0);   /* CMP last, size */
    const uint32_t wout = arm_branch_slot(&e);
    const uint32_t wmap = e.pos;
    arm_lsr(&e, A_R12, A_R0, XLAT_CODE_BLOCK_SHIFT + 3);
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_CODE_MAP);
    arm_ls_reg(&e, LS_LDRB, A_R12, A_R1, A_R12);
    arm_ubfx(&e, A_R1, A_R0, XLAT_CODE_BLOCK_SHIFT, 3);
    arm_lsr_reg(&e, A_R12, A_R12, A_R1);
    arm_dp_imm(&e, DP_AND, 1, A_PC, A_R12, 1);                 /* TST #1: C stays clear */
    const uint32_t wfree = arm_branch_slot(&e);
    arm_e16(&e, 0xb502u);                                    /* PUSH {r1, lr} */
    arm_mov(&e, A_R12, A_R2);
    ok &= arm_bl(&e, gapchk);
    arm_pop(&e, 0x4002u);                                    /* r1, lr */
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R12, 1);                 /* CMP r12, #1: CS = code */
    const uint32_t wdone = e.pos;
    arm_e16(&e, 0xbc06u);                                    /* POP {r1, r2} */
    arm_bx(&e, A_LR);
    const uint32_t wumb = e.pos;                             /* past fast memory */
    tramp_umb(&e, A_R1);
    const uint32_t wumb_out = arm_branch_slot(&e);
    const uint32_t wumb_in = arm_branch_slot(&e);
    ok &= tramp_patch(&e, wout, wumb, C_CS) && tramp_patch(&e, wfree, wdone, C_EQ) &&
          tramp_patch(&e, wumb_out, wdone, C_CS) && tramp_patch(&e, wumb_in, wmap, C_AL);

    /* Read check past fast memory (emit_checks): BL here (IT CS) with
     * r0 = linear address, r12 = the last byte (rchk[1]) or r0 (rchk[0]).
     * Returns CC when the access is in upper memory, else CS. r0-r3 kept. */
    a->rchk[0] = e.pos;
    arm_mov(&e, A_R12, A_R0);
    a->rchk[1] = e.pos;
    arm_e16(&e, 0xb402u);                                    /* PUSH {r1} */
    tramp_umb(&e, A_R1);
    arm_e16(&e, 0xbc02u);                                    /* POP {r1} */
    arm_bx(&e, A_LR);

    /* The same check, BL here with three halfwords after the BL: the
     * interpreter's arguments (interp_call) and the offset of the code
     * where the region goes on after XRIP did the instruction, from the end
     * of the halfwords. Returns after them when the access may be done
     * here (r0, r1 kept); else runs the instruction in XRIP. */
    uint32_t xto_common[3];
    for (int k = 0; k < 3; ++k) {
        a->wside[k] = e.pos;
        if (wlast[k])
            arm_addw(&e, A_R12, A_R0, wlast[k]);
        else
            arm_mov(&e, A_R12, A_R0);
        xto_common[k] = arm_branch_slot(&e);
    }
    a->wside[3] = e.pos;
    for (int k = 0; k < 3; ++k)
        ok &= tramp_patch(&e, xto_common[k], a->wside[3], C_AL);
    arm_e16(&e, 0xb406u);                                    /* PUSH {r1, r2} */
    arm_mov(&e, A_R2, A_R12);
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_MEM_SIZE);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R1, SH_LSL, 0);
    const uint32_t xout = arm_branch_slot(&e);
    const uint32_t xmap = e.pos;
    arm_lsr(&e, A_R12, A_R0, XLAT_CODE_BLOCK_SHIFT + 3);
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_CODE_MAP);
    arm_ls_reg(&e, LS_LDRB, A_R12, A_R1, A_R12);
    arm_ubfx(&e, A_R1, A_R0, XLAT_CODE_BLOCK_SHIFT, 3);
    arm_lsr_reg(&e, A_R12, A_R12, A_R1);
    arm_dp_imm(&e, DP_AND, 1, A_PC, A_R12, 1);
    const uint32_t xfree = arm_branch_slot(&e);
    arm_e16(&e, 0xb502u);                                    /* PUSH {r1, lr} */
    arm_mov(&e, A_R12, A_R2);
    ok &= arm_bl(&e, gapchk);
    arm_pop(&e, 0x4002u);                                    /* r1, lr */
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R12, 1);
    const uint32_t xcode = arm_branch_slot(&e);
    const uint32_t xok = e.pos;
    arm_e16(&e, 0xbc06u);                                    /* POP {r1, r2} */
    arm_addw(&e, A_LR, A_LR, 6);
    arm_bx(&e, A_LR);
    const uint32_t xfail = e.pos;
    arm_e16(&e, 0xbc06u);                                    /* POP {r1, r2} */
    arm_subw(&e, A_R0, A_LR, 1);                             /* the halfwords */
    arm_ls_imm(&e, LS_LDRH, A_R12, A_R0, 4);
    arm_addw(&e, A_LR, A_LR, 6);
    arm_dp_reg(&e, DP_ADD, 0, A_LR, A_LR, A_R12, SH_LSL, 0);   /* where it goes on */
    arm_ls_imm(&e, LS_LDRH, A_R1, A_R0, 2);
    ok &= arm_dp_imm(&e, DP_AND, 1, A_PC, A_R1, SC_CHECK_REDIRECT);
    const uint32_t to_interp = arm_branch_slot(&e);
    arm_bx(&e, A_LR);                   /* redirected native SIDE stub */
    const uint32_t xumb = e.pos;                             /* past fast memory */
    tramp_umb(&e, A_R1);
    const uint32_t xumb_out = arm_branch_slot(&e);
    const uint32_t xumb_in = arm_branch_slot(&e);
    ok &= tramp_patch(&e, xout, xumb, C_CS) && tramp_patch(&e, xfree, xok, C_EQ) &&
          tramp_patch(&e, xcode, xfail, C_CS) &&
          tramp_patch(&e, xumb_out, xfail, C_CS) && tramp_patch(&e, xumb_in, xmap, C_AL);

    /* PUSH of r2 (emit_push_value) with the arguments as for wside: SS:SP-2
     * checked as a write, the word stored, SP lowered. r1 = the new SP,
     * r0, r12 clobbered, r3 kept. */
    a->spush = e.pos;
    arm_ls_imm(&e, LS_LDRH, A_R1, A_R11, XLAT_RT_SP);
    arm_subw(&e, A_R1, A_R1, 2);
    arm_uxth(&e, A_R1, A_R1);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 8u);
    arm_dp_reg(&e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
    arm_addw(&e, A_R12, A_R0, 1);
    arm_e16(&e, 0xb40au);                                    /* PUSH {r1, r3} */
    arm_ls_imm(&e, LS_LDR, A_R3, A_R11, XLAT_RT_MEM_SIZE);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R3, SH_LSL, 0);
    const uint32_t pout = arm_branch_slot(&e);
    const uint32_t pmap = e.pos;
    arm_lsr(&e, A_R12, A_R0, XLAT_CODE_BLOCK_SHIFT + 3);
    arm_ls_imm(&e, LS_LDR, A_R3, A_R11, XLAT_RT_CODE_MAP);
    arm_ls_reg(&e, LS_LDRB, A_R12, A_R3, A_R12);
    arm_ubfx(&e, A_R3, A_R0, XLAT_CODE_BLOCK_SHIFT, 3);
    arm_lsr_reg(&e, A_R12, A_R12, A_R3);
    arm_dp_imm(&e, DP_AND, 1, A_PC, A_R12, 1);
    const uint32_t pfree = arm_branch_slot(&e);
    arm_e16(&e, 0xb502u);                                    /* PUSH {r1, lr} */
    arm_addw(&e, A_R12, A_R0, 1);
    ok &= arm_bl(&e, gapchk);
    arm_pop(&e, 0x4002u);                                    /* r1, lr */
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R12, 1);
    const uint32_t pcode = arm_branch_slot(&e);
    const uint32_t pok = e.pos;
    arm_e16(&e, 0xbc0au);                                    /* POP {r1, r3} */
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_MEM);
    arm_ls_reg(&e, LS_STRH, A_R2, A_R12, A_R0);
    arm_ls_imm(&e, LS_STRH, A_R1, A_R11, XLAT_RT_SP);
    arm_addw(&e, A_LR, A_LR, 6);
    arm_bx(&e, A_LR);
    const uint32_t pfail = e.pos;
    arm_e16(&e, 0xbc0au);                                    /* POP {r1, r3} */
    const uint32_t pto = arm_branch_slot(&e);
    const uint32_t pumb = e.pos;                             /* past fast memory */
    tramp_umb(&e, A_R3);
    const uint32_t pumb_out = arm_branch_slot(&e);
    const uint32_t pumb_in = arm_branch_slot(&e);
    ok &= tramp_patch(&e, pout, pumb, C_CS) && tramp_patch(&e, pfree, pok, C_EQ) &&
          tramp_patch(&e, pcode, pfail, C_CS) &&
          tramp_patch(&e, pto, xfail + 2u, C_AL) &&          /* wside's failure, after its POP */
          tramp_patch(&e, pumb_out, pfail, C_CS) && tramp_patch(&e, pumb_in, pmap, C_AL);

    /* POP (emit_pop_value, no release): r2 = the word at SS:SP, r1 = SP + 2
     * (stored); r0, r12 clobbered. */
    a->spop = e.pos;
    arm_ls_imm(&e, LS_LDRH, A_R1, A_R11, XLAT_RT_SP);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 8u);
    arm_dp_reg(&e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
    arm_addw(&e, A_R12, A_R0, 1);
    arm_ls_imm(&e, LS_LDR, A_R2, A_R11, XLAT_RT_MEM_SIZE);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R2, SH_LSL, 0);
    const uint32_t oout = arm_branch_slot(&e);
    const uint32_t oload = e.pos;
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_MEM);
    arm_ls_reg(&e, LS_LDRH, A_R2, A_R12, A_R0);
    arm_addw(&e, A_R1, A_R1, 2);
    arm_uxth(&e, A_R1, A_R1);
    arm_ls_imm(&e, LS_STRH, A_R1, A_R11, XLAT_RT_SP);
    arm_addw(&e, A_LR, A_LR, 6);
    arm_bx(&e, A_LR);
    const uint32_t oumb = e.pos;                             /* past fast memory */
    tramp_umb(&e, A_R2);
    const uint32_t oumb_out = arm_branch_slot(&e);
    const uint32_t oumb_in = arm_branch_slot(&e);
    ok &= tramp_patch(&e, oout, oumb, C_CS) && tramp_patch(&e, oumb_out, xfail + 2u, C_CS) &&
          tramp_patch(&e, oumb_in, oload, C_AL);

    /* Pending flags completed (emit_pending_check): xlat_rt_pending(). */
    a->pend = e.pos;
    arm_push(&e, 0x500fu);                     /* r0-r3, r12, lr: 8-byte aligned */
    arm_mov(&e, A_R0, A_R11);
    arm_mov32(&e, A_R12, (uint32_t)(uintptr_t)&xlat_rt_pending);
    arm_blx(&e, A_R12);
    arm_pop(&e, 0x900fu);                      /* r0-r3, r12, pc */

    /* Back edge whose budget ran out (emit_stubs): returns PL when an IRQ
     * is pending or the burst's deadline passed (leave), else MI with
     * budget and extra raised by XLAT_SLICE_CHECK. Uses r0, r12. */
    a->back_slow = e.pos;
#if !XLAT_EVENT_GATE
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_INTR);
    arm_ls_imm(&e, LS_LDRB, A_R12, A_R12, 0);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R12, 0);                 /* 1..255: PL */
    const uint32_t irq_pending = arm_branch_slot(&e);
#endif
    /* Read the deadline first: if an IRQ closes it between these loads,
     * the sampled clock must not predate the timestamp of that IRQ. */
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_BACK_DEADLINE);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_CLOCK);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R12, 0);
    arm_dp_reg(&e, DP_SUB, 1, A_R12, A_R12, A_R0, SH_LSL, 0);   /* clock - deadline */
    const uint32_t expired = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
    arm_addw(&e, A_R12, A_R12, XLAT_SLICE_CHECK);
    arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_BUDGET);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_EXTRA);
    arm_addw(&e, A_R12, A_R12, XLAT_SLICE_CHECK);
    arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_EXTRA);
    const uint32_t ret = e.pos;
    arm_bx(&e, A_LR);
    ok &= tramp_patch(&e, expired, ret, C_PL);
#if !XLAT_EVENT_GATE
    ok &= tramp_patch(&e, irq_pending, ret, C_NE);
#endif

    /* Interpreter call, reached by BL from region code (emit_interp_call)
     * with two halfwords after the BL: the IP of the instruction, and
     * XI_* flags | length << 8 | 0x8000 when the region goes on at the
     * next instruction. Returns after the halfwords if it does. */
    a->interp_call = e.pos;
    arm_subw(&e, A_R0, A_LR, 1);
    arm_addw(&e, A_LR, A_LR, 4);
    ok &= tramp_patch(&e, to_interp, e.pos, C_EQ);           /* r0 = the halfwords */
    arm_ls_imm(&e, LS_LDRH, A_R12, A_R0, 0);                  /* ip */
    arm_ls_imm(&e, LS_LDRH, A_R1, A_R0, 2);                   /* ctl */
    arm_ubfx(&e, A_R2, A_R1, 8, 4);
    arm_dp_reg(&e, DP_ADD, 0, A_R2, A_R2, A_R12, SH_LSL, 0);
    arm_uxth(&e, A_R2, A_R2);                                 /* ip + length */
    arm_ubfx(&e, A_R0, A_R1, 15, 1);
    arm_subw(&e, A_R0, A_R0, 1);                              /* 0 / ~0 */
    arm_dp_reg(&e, DP_ORR, 0, A_R2, A_R2, A_R0, SH_LSL, 0);
    arm_ubfx(&e, A_R1, A_R1, 0, 15);

    /* Interpreter thunk: r12 = IP of the instruction, r1 = XI_* flags,
     * r2 = IP it falls through to or 0xffffffff (the region does not go on
     * after it); lr = where the region goes on. */
    a->interp_thunk = e.pos;
    arm_push(&e, 0x400eu);                     /* r1-r3, lr: 8-byte aligned */
    arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_IP);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define STORE(r, off) arm_ls_imm(&e, LS_STR, r, A_R12, off);
    GPR_LIST(STORE)
#undef STORE
    arm_ls_imm(&e, LS_LDR, A_R0, A_R11, XLAT_RT_SP);
    arm_ls_imm(&e, LS_STR, A_R0, A_R12, 16);
    arm_mov(&e, A_R0, A_R11);
    arm_mov32(&e, A_R12, (uint32_t)(uintptr_t)&xlat_interp_call);
    arm_blx(&e, A_R12);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define LOAD(r, off) arm_ls_imm(&e, LS_LDR, r, A_R12, off);
    GPR_LIST(LOAD)
#undef LOAD
    arm_ls_imm(&e, LS_LDR, A_R3, A_R12, 16);
    arm_ls_imm(&e, LS_STR, A_R3, A_R11, XLAT_RT_SP);
    arm_pop(&e, 0x400eu);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 1);
    const uint32_t leave1 = arm_branch_slot(&e);      /* 0: stop, 2: other CS */
    arm_dp_imm(&e, DP_ADD, 1, A_PC, A_R2, 1);
    const uint32_t leave2 = arm_branch_slot(&e);      /* region does not go on */
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_IP);
    arm_dp_reg(&e, DP_SUB, 1, A_PC, A_R12, A_R2, SH_LSL, 0);
    const uint32_t leave3 = arm_branch_slot(&e);      /* it jumped */
    arm_bx(&e, A_LR);
    const uint32_t leave = e.pos;
    arm_mov32(&e, A_R12, XLAT_EXIT_STOP);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, 0);
    const uint32_t store = arm_branch_slot(&e);
    arm_mov32(&e, A_R12, XLAT_EXIT_BRANCH);
    arm_dp_imm(&e, DP_AND, 1, A_PC, A_R1, XI_SIDE);
    const uint32_t store2 = arm_branch_slot(&e);
    arm_mov32(&e, A_R12, XLAT_EXIT_SIDE);
    const uint32_t at_store = e.pos;
    arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
    /* The helper accepted the new CPU state. GPRs are already reloaded;
     * try a validated link before spilling them again in the epilogue. */
    const uint32_t to_dispatch = arm_branch_slot(&e);
    const uint32_t at_stop = e.pos;
    arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
    const uint32_t to_epi = arm_branch_slot(&e);
    if (e.full || !ok ||
        !arm_patch_branch(&e, leave1, leave, C_NE) || !arm_patch_branch(&e, leave2, leave, C_EQ) ||
        !arm_patch_branch(&e, leave3, leave, C_NE) || !arm_patch_branch(&e, store, at_stop, C_EQ) ||
        !arm_patch_branch(&e, store2, at_store, C_EQ) ||
        !arm_patch_branch(&e, to_dispatch, a->dispatch, C_AL) ||
        !arm_patch_branch(&e, to_epi, a->epilogue, C_AL))
        return false;
    /* Function addresses are selected only when building the trampoline.
     * Load takes the checked address as its fourth argument. STORE/ADD
     * use the separate no-GPR-snapshot ABI (rt, address, value, descriptor). */
    static const uintptr_t scalar_fn[SC_PATH_COUNT] = {
        (uintptr_t)&xlat_rt_scalar_load, (uintptr_t)&xlat_rt_scalar_store,
        (uintptr_t)&xlat_rt_scalar_add, (uintptr_t)&xlat_rt_scalar_push,
        (uintptr_t)&xlat_rt_scalar_pop, (uintptr_t)&xlat_rt_scalar_str_read,
        (uintptr_t)&xlat_rt_scalar_sti_str
    };
    uint32_t sc_enter[SC_PATH_COUNT];
    for (unsigned p = 0; p < SC_PATH_COUNT; ++p) {
        a->scalar_call[p] = e.pos;
        arm_mov32(&e, A_R12, (uint32_t)scalar_fn[p]);
        if (p == SC_PATH_LOAD)
            arm_mov(&e, A_R3, A_R0);          /* preserve checked address */
        sc_enter[p] = arm_branch_slot(&e);
    }
    const uint32_t sc_write = e.pos;
    arm_push(&e, 0x4008u);                   /* r3,lr: 8-byte alignment */
    arm_mov(&e, A_R2, A_R1);                  /* value */
    arm_mov(&e, A_R1, A_R0);                  /* address */
    arm_mov(&e, A_R0, A_R11);                 /* rt */
    arm_subw(&e, A_R3, A_LR, 1);              /* descriptor */
    arm_blx(&e, A_R12);
    arm_pop(&e, 0x4008u);
    const uint32_t sc_write_done = arm_branch_slot(&e);
    const uint32_t sc_frame = e.pos;
    for (unsigned p = 0; p < SC_PATH_COUNT; ++p)
        if (!tramp_patch(&e, sc_enter[p],
                         p == SC_PATH_STORE || p == SC_PATH_ADD ? sc_write : sc_frame, C_AL))
            return false;
    /* Load, stack and string operations may update guest GPRs. */
    arm_push(&e, 0x47f0u);                     /* r4-r10, lr */
    arm_mov(&e, A_R0, A_R11);
    arm_e16(&e, 0x4669u);                    /* MOV r1, sp */
    arm_subw(&e, A_R2, A_LR, 1);               /* descriptor */
    arm_blx(&e, A_R12);
    arm_pop(&e, 0x47f0u);
    if (!tramp_patch(&e, sc_write_done, e.pos, C_AL))
        return false;
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, SC_REFUSED);
    const uint32_t sc_refused = arm_branch_slot(&e);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, SC_DISPATCH);
    const uint32_t sc_dispatch = arm_branch_slot(&e);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, SC_STOP);
    const uint32_t sc_stop = arm_branch_slot(&e);
    arm_addw(&e, A_LR, A_LR, 2u * SC_WORDS);
    arm_dp_imm(&e, DP_SUB, 1, A_PC, A_R0, SC_RESUME_ADD);
    const uint32_t sc_resume = arm_branch_slot(&e);
    arm_ls_imm(&e, LS_LDR, A_R1, A_R11, XLAT_RT_DST);
    arm_ls_imm(&e, LS_LDR, A_R2, A_R11, XLAT_RT_SRC);
    arm_dp_reg(&e, DP_ADD, 1, A_PC, A_R1, A_R2, SH_LSL, 0);  /* CMN for local Jcc */
    const uint32_t sc_return = e.pos;
    arm_bx(&e, A_LR);
    const uint32_t sc_fallback = e.pos;
    arm_addw(&e, A_LR, A_LR, 2u * SC_WORDS - 4u);
    const uint32_t sc_interp = arm_branch_slot(&e);          /* ordinary IP/XI payload */
    const uint32_t sc_at_stop = e.pos;
    arm_mov32(&e, A_R12, XLAT_EXIT_STOP);
    arm_ls_imm(&e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
    const uint32_t sc_epi = arm_branch_slot(&e);
    if (e.full ||
        !tramp_patch(&e, sc_refused, sc_fallback, C_EQ) ||
        !tramp_patch(&e, sc_dispatch, a->exit_dyn, C_EQ) ||
        !tramp_patch(&e, sc_stop, sc_at_stop, C_EQ) ||
        !tramp_patch(&e, sc_resume, sc_return, C_NE) ||
        !tramp_patch(&e, sc_interp, a->interp_call, C_AL) ||
        !tramp_patch(&e, sc_epi, a->epilogue, C_AL))
        return false;
    a->code_top = (e.pos + 3u) & ~3u;
    a->data_bottom = a->size;
    return true;
}

size_t xlat_translate_ctx_size(void)
{
    return sizeof(tctx_t);
}

/* Must execute after the worker has stopped and before the cache copies
 * the guest source bytes into the published region. */
bool xlat_translate_validate(const xlat_rt_t *rt, const void *ctx_mem)
{
    const tctx_t *c = ctx_mem;
    for (int s = 0; s < c->n_source; ++s) {
        const uint32_t linear = c->cs_base + c->source[s].ip;
        const uint32_t len = c->source[s].len;
        if (!xlat_code_direct(linear, len) ||
            xlat_memcmp(c->source[s].b, rt->mem + linear, len))
            return false;
    }
    return true;
}

bool xlat_translate_step(xlat_arena_t *a, const xlat_rt_t *rt, uint16_t ip, int trace_min,
                    const uint16_t *extra, int n_extra,
                    xlat_region_out_t *out, bool *full, void *ctx_mem, bool resume,
                    xlat_image_t *image)
{
    *full = false;
    tctx_t *const c = (tctx_t *)ctx_mem;
    bool ok = false;
    const bool measure = rt->wall_deadline != 0;
    bool finishing = resume;
#if !XLAT_STACKFUL
    finishing = resume && c->phase != PH_DISCOVER;
#endif
    uint32_t phase_t0 = measure && finishing ? compiler_clock(rt) : 0;
    c->rt = rt;
    out->reject = XLAT_RJ_NONE;
    if (resume) {
#if XLAT_STACKFUL
        /* Only an arena FULL retry calls us again. A preempted worker
         * resumes via exception return, without calling this function. */
        goto emit_init_phase;
#else
        switch (c->phase) {
        case PH_DISCOVER:  goto discover_phase;
        case PH_FLOW:      goto flow_phase;
        case PH_PLAN:      goto plan_phase;
        case PH_RUNS:      goto runs_phase;
        case PH_EMIT_INIT: goto emit_init_phase;
        case PH_EMIT:      goto emit_phase;
        case PH_STUBS:     goto stubs_phase;
        case PH_FINISH:    goto finish_phase;
        default:           out->reject = XLAT_RJ_EMIT; return false;
        }
#endif
    }
    COMPILER_PHASE(c, PH_DISCOVER);
    c->final_us = 0;
    c->variant = 0;
    c->discovering = false;
    c->cs_base = rt->seg_base[1];
    c->n_stubs = c->n_fix = c->n_dfix = 0;
    c->overflow = false;
    c->epilogue = a->epilogue;
    c->dispatch = a->dispatch;
    c->interp_call = a->interp_call;
    c->exit_ip[0] = a->exit_ip[0];
    c->exit_ip[1] = a->exit_ip[1];
    c->exit_dyn = a->exit_dyn;
    __builtin_memcpy(c->wchk, a->wchk, sizeof(c->wchk));
    __builtin_memcpy(c->wside, a->wside, sizeof(c->wside));
    __builtin_memcpy(c->rchk, a->rchk, sizeof(c->rchk));
    c->back_slow = a->back_slow;
    c->pend = a->pend;
    c->spush = a->spush;
    c->spop = a->spop;
    xlat_memcpy(c->scalar_call, a->scalar_call, sizeof(c->scalar_call));
    c->n_interp = 0;
    out->reject = XLAT_RJ_NONE;
    c->trace_min = (uint8_t)(trace_min > 0 ? (trace_min < 255 ? trace_min : 255) : 0);
    c->fn = 0;

    c->no_calls = 0;
    COMPILER_LABEL(discover_phase);
    int why;
    for (;;) {
        /* An existing region extended with extra entries must retain them. */
        why = discover(c, ip, image);
        if (why == XLAT_RJ_DEFER)
            goto deferred;
        if (c->variant == 0 && why == XLAT_RJ_NONE && c->fn) {
            bool calls = false;
            for (int i = 0; i < c->n && !calls; ++i)
                calls = x86_is_call(&c->in[i]) && c->tgt_idx[i] >= 0;
            if (calls) {
                c->variant = 1;
                c->no_calls = 1;
                c->fn = 0;
                continue;
            }
        }
        if (c->variant == 1 && (why != XLAT_RJ_NONE || !c->fn)) {
            c->variant = 2;
            c->no_calls = 0;
            c->fn = 0;
            continue;
        }
        break;
    }
#if XLAT_PRECOMPILE
    if (image && why != XLAT_RJ_START)
        image_targets(c, image, why != XLAT_RJ_NONE);
#endif
    out->fn = c->fn;
    out->n_insns = (uint8_t)c->n;
    out->n_interp = (uint8_t)c->n_interp;
    if (why != XLAT_RJ_NONE) {
        out->reject = (uint8_t)why;
        goto done;
    }
    finishing = true;
    phase_t0 = c->seal_at;
    /* More entry points (xlat_cache: hot region exits that came to these
     * instructions): leaders, entered with the state in rt */
    for (int k = 0; k < n_extra; ++k) {
        const int j = find_insn(c, extra[k]);
        if (j < 0) {
            out->reject = XLAT_RJ_START;
            goto done;
        }
        c->leader[j] = 1;
        c->head_marks |= 1ull << j;
    }
    mark_dead_flags(c);
    build_preds(c);
    xlat_memset(c->in_set, 0, sizeof(c->in_set));
    xlat_memset(c->out_set, 0, sizeof(c->out_set));
    xlat_memset(c->cf_in, 0, sizeof(c->cf_in));
    xlat_memset(c->cf_out, 0, sizeof(c->cf_out));
    c->flow_i = 0;
    c->flow_changed = false;
    COMPILER_PHASE(c, PH_FLOW);
    COMPILER_LABEL(flow_phase)
    if (!dataflow(c))
        goto deferred;
    COMPILER_PHASE(c, PH_PLAN);
    c->plan_pass = 0;
    c->plan_i = 0;
    c->plan_mat = c->plan_full = 0;
    xlat_memset(c->need_cf, 0, sizeof(c->need_cf));
    COMPILER_LABEL(plan_phase)
    if (!plan(c))
        goto deferred;
    /* Source copies: omit only the runtime-read operand of unprefixed INT.
     * The opcode and every other decoded byte remain protected. */
    out->n_runs = 0;
    c->data_bytes = 0;
    c->plan_i = 0;
    COMPILER_PHASE(c, PH_RUNS);
    COMPILER_LABEL(runs_phase)
    for (; c->plan_i < c->n; ++c->plan_i) {
        if (compiler_checkpoint(rt))
            goto deferred;
        const int i = c->plan_i;
        const uint16_t len = (uint16_t)(c->in[i].len -
            (c->in[i].op == XO_INTERP && (c->in[i].sub & XLAT_XI_INT_IMM) != 0));
        if (out->n_runs && out->runs[out->n_runs - 1].ip + out->runs[out->n_runs - 1].len ==
                               c->in[i].ip) {
            const uint32_t old = out->runs[out->n_runs - 1].len;
            const uint32_t total = old + len;
            out->runs[out->n_runs - 1].len = (uint16_t)total;
            c->data_bytes += ((total + 3u) & ~3u) - ((old + 3u) & ~3u);
        } else {
            out->runs[out->n_runs].ip = c->in[i].ip;
            out->runs[out->n_runs].len = len;
            out->n_runs++;
            c->data_bytes += 8u + ((len + 3u) & ~3u);
        }
    }
    COMPILER_PHASE(c, PH_EMIT_INIT);
emit_init_phase:
    if (compiler_checkpoint(rt))
        goto deferred;
    c->n_stubs = c->n_fix = c->n_dfix = 0;
    c->overflow = false;
    /* No room: emitted all the same into no space (arm_e16/e32 count the
     * bytes), so the cache learns the size the region needs. */
    const uint32_t cap = a->data_bottom > a->code_top + c->data_bytes
                       ? a->data_bottom - c->data_bytes : a->code_top;
    c->e = (arm_emit_t){a->base, a->code_top, cap, false, 0};
    for (int i = 0; i < c->n; ++i)
        c->side_stub[i][0] = c->side_stub[i][1] = -1;
    c->emit_i = 0;
    COMPILER_PHASE(c, PH_EMIT);
    COMPILER_LABEL(emit_phase)
    for (; c->emit_i < c->n; ++c->emit_i) {
        const int i = c->emit_i;
        const uint32_t from = c->e.pos;
        (void)from;
        if (compiler_checkpoint(rt))
            goto deferred;
        emit_insn(c, i);
        SIZE_NOTE(c->in[i].op < XLAT_SIZE_RECON ? c->in[i].op : 0, from);
    }
    c->stub_pass = c->stub_i = 0;
    COMPILER_PHASE(c, PH_STUBS);
    COMPILER_LABEL(stubs_phase)
    if (!emit_stubs(c))
        goto deferred;
    COMPILER_PHASE(c, PH_FINISH);
    c->dfix_i = c->fix_i = c->entry_i = 0;
    out->n_entries = 0;
    COMPILER_LABEL(finish_phase)
    if (c->e.full) {
        if (compiler_checkpoint(rt))
            goto deferred;
        /* labels past the space are not real: no patching */
        *full = true;
        out->reject = XLAT_RJ_FULL;
        out->need = (c->e.pos - a->code_top) + c->e.over + 4u + c->data_bytes;
        COMPILER_PHASE(c, PH_EMIT_INIT);
        goto done;
    }
    for (; c->dfix_i < c->n_dfix; ++c->dfix_i) {
        if (compiler_checkpoint(rt))
            goto deferred;
        const int k = c->dfix_i;
        /* Forward continuation or native SIDE stub, from the payload end. */
        const int idx = c->dfix[k].insn;
        const uint32_t at = c->dfix[k].pos;
        const uint32_t to = idx < 0 ? c->stub[-1 - idx].pos : c->label[idx];
        if (to < at + 2u || to - (at + 2u) > 0xffffu) {
            c->overflow = true;
            continue;
        }
        c->e.buf[at] = (uint8_t)(to - (at + 2u));
        c->e.buf[at + 1] = (uint8_t)((to - (at + 2u)) >> 8);
    }
    for (; c->fix_i < c->n_fix; ++c->fix_i) {
        if (compiler_checkpoint(rt))
            goto deferred;
        const int k = c->fix_i;
        if (!arm_patch_branch(&c->e, c->fix[k].site, c->label[c->fix[k].insn], c->fix[k].cond))
            c->overflow = true;
    }
    if (c->overflow) {
        out->reject = XLAT_RJ_EMIT;
        goto done;
    }

    for (; c->entry_i < c->n; ++c->entry_i) {
        if (compiler_checkpoint(rt))
            goto deferred;
        const int i = c->entry_i;
        if (c->leader[i]) {
            out->entries[out->n_entries].ip = c->in[i].ip;
            out->entries[out->n_entries].mark = (c->head_marks >> i) & 1u;
            out->entries[out->n_entries].code_off = c->label[i];
            out->n_entries++;
        }
    }
    if (compiler_checkpoint(rt))
        goto deferred;
#if !XLAT_STACKFUL
    /* Cooperative path: validation and publication share the last slice.
     * On PSP the final check instead belongs to the MSP caller. */
    if (!xlat_translate_validate(rt, c)) {
        out->reject = XLAT_RJ_CHANGED;
        goto done;
    }
#endif
    out->code_off = a->code_top;
    out->code_len = c->e.pos - a->code_top;
    out->lo = c->lo;
    out->span = c->span;
    out->data_size = c->data_bytes;
    a->code_top = (c->e.pos + 3u) & ~3u;
    ok = true;
    goto done;
deferred:
    /* Preserve the context and uncommitted arena window. */
    out->reject = XLAT_RJ_DEFER;
done:
    if (measure && finishing)
        c->final_us += compiler_clock(rt) - phase_t0;
    if (ok)
        out->final_us = c->final_us;
    return ok;
}

bool xlat_translate(xlat_arena_t *a, const xlat_rt_t *rt, uint16_t ip, int trace_min,
                    const uint16_t *extra, int n_extra,
                    xlat_region_out_t *out, bool *full, void *ctx_mem)
{
#if XLAT_STACKFUL
    /* Direct callers, unlike the cache's worker, need validation here. */
    const uint32_t top = a->code_top;
    if (!xlat_translate_step(a, rt, ip, trace_min, extra, n_extra, out, full,
                             ctx_mem, false, NULL))
        return false;
    if (xlat_translate_validate(rt, ctx_mem))
        return true;
    a->code_top = top;
    out->reject = XLAT_RJ_CHANGED;
    return false;
#else
    return xlat_translate_step(a, rt, ip, trace_min, extra, n_extra, out, full,
                               ctx_mem, false, NULL);
#endif
}
