/*
 * murm386 xlat: x86 region -> Thumb-2 translation.
 *
 * A region is the set of supported instructions reachable from a hot entry
 * point by direct control flow, limited to XLAT_MAX_INSNS. It must contain a
 * loop (a branch to an instruction at or before it), otherwise translation
 * gains nothing over XRIP.
 *
 * Register convention inside a region:
 *   r4 AX, r5 CX, r6 DX, r7 BX, r8 BP, r9 SI, r10 DI (full 32-bit values;
 *   16-bit writes use BFI), SP stays in rt->gpr (instructions using it are
 *   not translated yet), r11 = xlat_rt_t *, r0-r3, r12, lr scratch.
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
 * over the region decides which.
 *
 * Exits are always at an x86 instruction boundary with all registers written
 * back, so XRIP continues exactly there.
 */
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
    uint8_t budget_n;    /* ST_BACK: guest instructions charged */
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
    int8_t pfp[XLAT_MAX_INSNS];
    uint8_t leader[XLAT_MAX_INSNS], mat[XLAT_MAX_INSNS], need_cf[XLAT_MAX_INSNS];
    uint8_t local_jcc[XLAT_MAX_INSNS];
    int16_t side_stub[XLAT_MAX_INSNS][2];
    uint32_t label[XLAT_MAX_INSNS];
    uint16_t queue[2 * XLAT_MAX_INSNS + 2];
    uint16_t seen[4 * XLAT_MAX_INSNS];
    int n_seen;
    stub_t stub[MAX_STUBS];
    int n_stubs;
    fixup_t fix[MAX_FIXUPS];
    int n_fix;
    bool overflow;
    arm_emit_t e;
    uint32_t epilogue;
} tctx_t;

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
           in->op == XO_CALL;
}

/* Control reaches the next instruction directly. A CALL does not: its return
 * site is reached through RET and the dispatcher, so it is an entry point. */
static bool falls_through(const x86_insn_t *in)
{
    if (in->op == XO_INTERP)
        return !(in->sub & XI_NO_FALL);
    return in->op != XO_JMP && in->op != XO_CALL && in->op != XO_RET;
}

static bool is_producer(const x86_insn_t *in)
{
    return x86_flag_kind(in) != FK_NONE;
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

static bool discover(tctx_t *c, uint16_t start)
{
    int head = 0, tail = 0;
    const int qcap = (int)(sizeof(c->queue) / sizeof(c->queue[0]));
    c->queue[tail++] = start;
    c->n = 0;
    c->n_seen = 0;
    while (head < tail) {
        const uint16_t ip = c->queue[head++];
        if (seen_before(c, ip) || c->n >= XLAT_MAX_INSNS)
            continue;
        /* Stay near the entry point: far CALL targets leave the region, so
         * the range checked for writes into the code stays small. */
        if ((uint16_t)(ip - start + XLAT_SPAN_WINDOW / 2) >= XLAT_SPAN_WINDOW)
            continue;
        const uint32_t linear = c->cs_base + ip;
        if (linear + 16u > c->rt->mem_size)
            continue;
        x86_insn_t in;
        if (!x86_decode(c->rt->mem + linear, ip, &in) &&
            !x86_decode_interp(c->rt->mem + linear, ip, &in))
            continue;
        c->in[c->n++] = in;
        if ((falls_through(&in) || in.op == XO_CALL) && tail < qcap)
            c->queue[tail++] = (uint16_t)(ip + in.len);
        if (is_branch(&in) && tail < qcap)
            c->queue[tail++] = in.target;
    }
    if (!c->n || c->in[0].ip != start)
        return false;

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
    for (int i = 0; i + 1 < c->n; ++i)
        if ((uint32_t)c->in[i].ip + c->in[i].len > c->in[i + 1].ip)
            return false;   /* overlapping instructions: leave it to XRIP */

    /* Edges, leaders, loop check. */
    bool loop = false;
    memset(c->leader, 0, sizeof(c->leader));
    const int first = find_insn(c, start);
    if (first < 0)
        return false;
    c->leader[first] = 1;
    c->lo = UINT32_MAX;
    uint32_t hi = 0;
    for (int i = 0; i < c->n; ++i) {
        const x86_insn_t *in = &c->in[i];
        c->fall_idx[i] = falls_through(in) ? (int8_t)find_insn(c, (uint16_t)(in->ip + in->len)) : -1;
        c->tgt_idx[i] = is_branch(in) ? (int8_t)find_insn(c, in->target) : -1;
        if (c->tgt_idx[i] >= 0) {
            c->leader[c->tgt_idx[i]] = 1;
            if (c->tgt_idx[i] <= i)
                loop = true;
        }
        if (in->op == XO_CALL) {
            const int ret = find_insn(c, (uint16_t)(in->ip + in->len));
            if (ret >= 0)
                c->leader[ret] = 1;
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
        interp += c->in[i].op == XO_INTERP;
    if (interp * XLAT_INTERP_RATIO > c->n)
        return false;
    return loop;
}

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

static bool recon_ok(const x86_insn_t *in)
{
    if (in->op == XO_INCDEC)
        return in->a.kind == OPD_REG;
    if (in->op != XO_ALU || x86_is_carry_op(in) || in->a.kind != OPD_REG ||
        (in->b.kind != OPD_REG && in->b.kind != OPD_IMM))
        return false;
    if ((in->sub == ALU_ADD || in->sub == ALU_SUB) && in->b.kind == OPD_REG &&
        in->b.reg == in->a.reg)
        return false;   /* ADD/SUB x,x: the old value is gone */
    return true;
}

static void dataflow(tctx_t *c)
{
    memset(c->in_set, 0, sizeof(c->in_set));
    memset(c->out_set, 0, sizeof(c->out_set));
    memset(c->cf_in, 0, sizeof(c->cf_in));
    memset(c->cf_out, 0, sizeof(c->cf_out));
    uint64_t *const cf_out = c->cf_out;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < c->n; ++i) {
            uint64_t in = c->leader[i] ? NONE_BIT : 0, cfin = in;
            for (int p = 0; p < c->n; ++p) {
                if (c->fall_idx[p] == i || c->tgt_idx[p] == i) {
                    in |= c->out_set[p];
                    cfin |= cf_out[p];
                }
            }
            const x86_insn_t *x = &c->in[i];
            /* after the interpreter the flags are in rt, as at an entry */
            const uint64_t out = x->op == XO_INTERP ? NONE_BIT : is_producer(x) ? 1ull << i : in;
            const uint64_t cfout = x->op == XO_INTERP ? NONE_BIT
                                   : is_producer(x) && x->op != XO_INCDEC ? 1ull << i : cfin;
            if (in != c->in_set[i] || out != c->out_set[i] || cfin != c->cf_in[i] ||
                cfout != cf_out[i]) {
                c->in_set[i] = in;
                c->out_set[i] = out;
                c->cf_in[i] = cfin;
                cf_out[i] = cfout;
                changed = true;
            }
        }
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
        for (int k = 0; k < c->n; ++k) {
            if (reach & (1ull << k)) {
                c->mat[k] = 1;
                if (c->in[k].op == XO_INCDEC)
                    c->need_cf[k] = 1;
            }
        }
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
        in->op == XO_CALL || in->op == XO_RET)
        return c->pfp[i];
    return -1;
}

static bool edge_needs_sync(int i, int dest)
{
    return dest < 0 || dest <= i;   /* region exit, or back edge (budget exit) */
}

static void plan(tctx_t *c)
{
    memset(c->mat, 0, sizeof(c->mat));
    memset(c->need_cf, 0, sizeof(c->need_cf));
    for (int i = 0; i < c->n; ++i) {
        const x86_insn_t *in = &c->in[i];
        c->pfp[i] = (int8_t)pred_fall_producer(c, i);
        c->local_jcc[i] = 0;
        if (in->op == XO_JCC && c->pfp[i] >= 0) {
            const x86_insn_t *p = &c->in[c->pfp[i]];
            jstep_t s[2];
            c->local_jcc[i] = !x86_is_carry_op(p) &&
                              jcc_map(x86_flag_kind(p), p->op == XO_INCDEC, in->sub, s) >= 0;
        }
    }
    for (int i = 0; i < c->n; ++i) {
        const x86_insn_t *in = &c->in[i];
        if (x86_accesses_mem(in))
            plan_sync(c, c->in_set[i], c->pfp[i]);            /* side exit */
        if (in->op == XO_RET)
            plan_sync(c, c->out_set[i], edge_producer(c, i)); /* exit to the popped IP */
        if (in->op == XO_INTERP)
            plan_sync(c, c->in_set[i], c->pfp[i]);            /* the interpreter reads rt */
        if (in->op == XO_JCC && !c->local_jcc[i])
            plan_sync(c, c->in_set[i], c->pfp[i]);            /* helper Jcc */
        const int ep = edge_producer(c, i);
        if (falls_through(in) && edge_needs_sync(i, c->fall_idx[i]))
            plan_sync(c, c->out_set[i], ep);
        if (is_branch(in) && edge_needs_sync(i, c->tgt_idx[i]))
            plan_sync(c, c->out_set[i], ep);
    }
    /* ADC/SBB always store their flags and read CF from rt. INC/DEC whose
     * flags reach rt read CF from rt too. The CF producers that reach them
     * must have stored it. */
    for (int i = 0; i < c->n; ++i) {
        if (x86_is_carry_op(&c->in[i])) {
            c->mat[i] = 1;
            c->need_cf[i] = 1;
        }
    }
    for (int i = 0; i < c->n; ++i)
        if (c->need_cf[i])
            for (int k = 0; k < c->n; ++k)
                if (c->cf_in[i] & (1ull << k))
                    c->mat[k] = 1;
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
    memset(s, 0, sizeof(*s));
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
        const int n = (i - j + 1) >> XLAT_BUDGET_SHIFT;
        c->stub[b].budget_n = (uint8_t)(n > 0 ? n : 1);
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
static void get_shifted(tctx_t *c, const x86_opd_t *o, int size, int rd)
{
    arm_emit_t *e = &c->e;
    const int s = 32 - size;
    switch (o->kind) {
    case OPD_REG:
        if (size == 16) {
            arm_lsl(e, rd, arm_reg16[o->reg], 16);
        } else if (o->reg < 4) {
            arm_lsl(e, rd, arm_reg16[o->reg], 24);
        } else {
            arm_ubfx(e, rd, arm_reg16[o->reg - 4], 8, 8);
            arm_lsl(e, rd, rd, 24);
        }
        break;
    case OPD_MEM:
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, size == 16 ? LS_LDRH : LS_LDRB, rd, A_LR, A_R0);
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
        if (size == 16 || o->reg < 4)
            return arm_reg16[o->reg];
        arm_ubfx(e, scratch, arm_reg16[o->reg - 4], 8, 8);
        return scratch;
    case OPD_MEM:
        arm_ls_imm(e, LS_LDR, A_LR, A_R11, XLAT_RT_MEM);
        arm_ls_reg(e, size == 16 ? LS_LDRH : LS_LDRB, scratch, A_LR, A_R0);
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
        arm_ls_reg(e, size == 16 ? LS_STRH : LS_STRB, src, A_LR, A_R0);
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
    if (index[in->mem_rm] >= 0)
        arm_dp_reg(e, DP_ADD, 0, A_R0, base[in->mem_rm], index[in->mem_rm], SH_LSL, 0);
    else
        arm_mov(e, A_R0, base[in->mem_rm]);
    if (disp) {
        if (disp <= 0xfffu) {
            arm_addw(e, A_R0, A_R0, disp);
        } else if (0x10000u - disp <= 0xfffu) {
            arm_subw(e, A_R0, A_R0, 0x10000u - disp);
        } else {
            arm_movw(e, A_R12, disp);
            arm_dp_reg(e, DP_ADD, 0, A_R0, A_R0, A_R12, SH_LSL, 0);
        }
    }
    arm_uxth(e, A_R0, A_R0);
}

/* r0 = linear address; branches to the side exit when the access is not in
 * fast memory or (for writes) may touch this region's x86 code. */
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
        const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
        c->stub[k].recon = (int8_t)(r >= 0 ? r : -1);
        c->side_stub[i][mw] = (int16_t)k;
    }
    return (dest_t){D_STUB, (int16_t)k};
}

/* r0 = linear address of an access by instruction i. Uses r12, lr only. */
static void emit_checks(tctx_t *c, int i, int bytes, bool write)
{
    arm_emit_t *e = &c->e;

    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_MEM_SIZE);
    if (bytes > 1) {
        arm_addw(e, A_LR, A_R0, (uint32_t)bytes - 1u);
        arm_dp_reg(e, DP_SUB, 1, A_PC, A_LR, A_R12, SH_LSL, 0);
    } else {
        arm_dp_reg(e, DP_SUB, 1, A_PC, A_R0, A_R12, SH_LSL, 0);
    }
    emit_branch(c, side_dest(c, i, 0), C_CS);

    if (write) {
        /* (addr - (lo - (bytes-1))) < span + bytes - 1: overlaps the code. */
        arm_mov32(e, A_R12, c->lo - (uint32_t)(bytes - 1));
        arm_dp_reg(e, DP_SUB, 0, A_LR, A_R0, A_R12, SH_LSL, 0);
        arm_mov32(e, A_R12, c->span + (uint32_t)(bytes - 1));
        arm_dp_reg(e, DP_SUB, 1, A_PC, A_LR, A_R12, SH_LSL, 0);
        emit_branch(c, side_dest(c, i, 1), C_CC);
    }
}

static void emit_lazy_values(tctx_t *c, int kind, int size);

/* Store the lazy flags of an operation: r2 = dst << s, r3 = src << s,
 * r1 = result << s. Clobbers r12, lr and the ARM flags. */
static void emit_lazy(tctx_t *c, int kind, int size, bool incdec)
{
    arm_emit_t *e = &c->e;
    if (incdec) {
        /* CF stays: if the lazy state is resolved, take CF from FLAGS. */
        arm_ls_imm(e, LS_LDRB, A_R12, A_R11, XLAT_RT_OP);
        arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, 0);
        const uint32_t skip = arm_branch_slot(e);
        arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_EFLAGS);
        arm_dp_imm(e, DP_AND, 0, A_R12, A_R12, 1);
        arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_CF);
        local_patch(c, skip, C_NE);
    } else if (kind == FK_LOGIC) {
        arm_mov32(e, A_R12, 0);
        arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_CF);
    } else {
        if (kind == FK_ADD)
            arm_dp_reg(e, DP_ADD, 1, A_PC, A_R2, A_R3, SH_LSL, 0);   /* CMN: C = carry */
        else
            arm_dp_reg(e, DP_SUB, 1, A_PC, A_R2, A_R3, SH_LSL, 0);   /* CMP: C = !borrow */
        arm_mov32(e, A_R12, 0);
        arm_dp_imm(e, DP_ADC, 0, A_R12, A_R12, 0);
        if (kind == FK_SUB)
            arm_dp_imm(e, DP_EOR, 0, A_R12, A_R12, 1);
        arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_CF);
    }
    emit_lazy_values(c, kind, size);
}

/* The lazy fields other than CF: r1 result, r2 dst, r3 src (all << s). */
static void emit_lazy_values(tctx_t *c, int kind, int size)
{
    arm_emit_t *e = &c->e;
    const int s = 32 - size;
    arm_ls_imm(e, LS_STR, A_R1, A_R11, XLAT_RT_RESULT);
    if (kind == FK_LOGIC) {
        arm_ls_imm(e, LS_STR, A_R1, A_R11, XLAT_RT_DST);
        arm_ls_imm(e, LS_STR, A_R1, A_R11, XLAT_RT_SRC);
    } else {
        arm_ls_imm(e, LS_STR, A_R2, A_R11, XLAT_RT_DST);
        if (kind == FK_SUB) {
            /* XRIP keeps (~src) << shift */
            arm_dp_reg(e, DP_ORN, 0, A_R12, A_PC, A_R3, SH_LSL, 0);
            arm_lsr(e, A_R12, A_R12, s);
            arm_lsl(e, A_R12, A_R12, s);
            arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_SRC);
        } else {
            arm_ls_imm(e, LS_STR, A_R3, A_R11, XLAT_RT_SRC);
        }
    }
    arm_mov32(e, A_R12, (uint32_t)(kind << 5 | s));
    arm_ls_imm(e, LS_STRB, A_R12, A_R11, XLAT_RT_OP);
}

/* Rebuild producer p's lazy flags from registers right after it ran. */
static void emit_recon(tctx_t *c, int p)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[p];
    const int size = in->size, s = 32 - size;
    const int kind = x86_flag_kind(in);
    if (in->op == XO_INCDEC) {
        get_shifted(c, &in->a, size, A_R1);
        arm_mov32(e, A_R3, 1u << s);
        arm_dp_reg(e, in->sub ? DP_ADD : DP_SUB, 0, A_R2, A_R1, A_R3, SH_LSL, 0);
        emit_lazy(c, kind, size, true);
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
    emit_lazy(c, kind, size, false);
}

/* r12 = CF from the lazy state in rt (0 or 1). Clobbers the ARM flags. */
static void emit_carry_in(tctx_t *c)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_LDRB, A_R12, A_R11, XLAT_RT_OP);
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, 0);
    arm_ls_imm(e, LS_LDRB, A_R12, A_R11, XLAT_RT_CF);   /* flags_op != 0: flags_cf */
    const uint32_t done = arm_branch_slot(e);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_EFLAGS);
    arm_dp_imm(e, DP_AND, 0, A_R12, A_R12, 1);
    local_patch(c, done, C_NE);
}

/* ADC/SBB on r2 = dst << s, r3 = src << s; r1 = result << s, flags stored.
 * The carry is brought into bit s through the low bits: ADC adds
 * (2^s - 1) and 1 below the operands, SBB subtracts 1 below the source. */
static void emit_carry_op(tctx_t *c, const x86_insn_t *in)
{
    arm_emit_t *e = &c->e;
    const int size = in->size, s = 32 - size;
    emit_carry_in(c);
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
    emit_lazy_values(c, x86_flag_kind(in), size);
}

static void emit_alu(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    const int size = in->size;
    const int kind = x86_flag_kind(in);
    const bool incdec = in->op == XO_INCDEC;
    const bool writes = incdec || (in->sub != ALU_CMP && in->sub != ALU_TEST);
    int dp;

    if (x86_has_mem(in))
        emit_mem_access(c, i, size / 8, in->a.kind == OPD_MEM && writes);

    get_shifted(c, &in->a, size, A_R2);
    if (x86_is_carry_op(in)) {
        get_shifted(c, &in->b, size, A_R3);
        emit_carry_op(c, in);
        arm_lsr(e, A_R12, A_R1, 32 - size);
        put_raw(c, &in->a, size, A_R12);
        return;
    }
    if (incdec) {
        arm_mov32(e, A_R3, 1u << (32 - size));
        dp = in->sub ? DP_SUB : DP_ADD;
    } else {
        get_shifted(c, &in->b, size, A_R3);
        switch (in->sub) {
        case ALU_ADD: dp = DP_ADD; break;
        case ALU_OR:  dp = DP_ORR; break;
        case ALU_AND: case ALU_TEST: dp = DP_AND; break;
        case ALU_XOR: dp = DP_EOR; break;
        default:      dp = DP_SUB; break;   /* SUB, CMP */
        }
    }
    if (c->mat[i]) {
        arm_dp_reg(e, dp, 0, A_R1, A_R2, A_R3, SH_LSL, 0);
        emit_lazy(c, kind, size, incdec);
    }
    /* The flag-setting operation is last: a Jcc after it reads ARM flags. */
    arm_dp_reg(e, dp, 1, writes ? A_R1 : A_PC, A_R2, A_R3, SH_LSL, 0);
    if (writes) {
        arm_lsr(e, A_R12, A_R1, 32 - size);
        put_raw(c, &in->a, size, A_R12);
    }
}

static void emit_mov(tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    if (x86_has_mem(in))
        emit_mem_access(c, i, in->size / 8, in->a.kind == OPD_MEM);
    const int r = get_raw(c, &in->b, in->size, A_R1);
    put_raw(c, &in->a, in->size, r);
}

static void emit_xchg(tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    const int ra = get_raw(c, &in->a, in->size, A_R1);
    if (ra != A_R1)
        arm_mov(&c->e, A_R1, ra);
    const int rb = get_raw(c, &in->b, in->size, A_R2);
    put_raw(c, &in->a, in->size, rb);
    put_raw(c, &in->b, in->size, A_R1);
}

/* r1 = (SP + delta) & 0xffff, r0 = SS base + r1. */
static void emit_stack_addr(tctx_t *c, int delta)
{
    arm_emit_t *e = &c->e;
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
    arm_ls_imm(e, LS_LDRH, A_R1, A_R12, 16);
    if (delta < 0)
        arm_subw(e, A_R1, A_R1, (uint32_t)-delta);
    if (delta)
        arm_uxth(e, A_R1, A_R1);
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_SEG_BASE + 4u * 2u);
    arm_dp_reg(e, DP_ADD, 0, A_R0, A_R1, A_R12, SH_LSL, 0);
}

static void emit_store_sp(tctx_t *c)
{
    arm_ls_imm(&c->e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
    arm_ls_imm(&c->e, LS_STRH, A_R1, A_R12, 16);
}

/* PUSH / CALL: the 16-bit value in register v is pushed; checks first, so a
 * side exit leaves SP alone. */
static void emit_push_value(tctx_t *c, int i, int v)
{
    arm_emit_t *e = &c->e;
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
        arm_mov32(e, A_R2, (uint16_t)(in->ip + in->len));
        emit_push_value(c, i, A_R2);
        emit_branch(c, make_dest(c, i, in->target), C_AL);
        break;
    default: {  /* RET: leave for the popped IP; the dispatcher continues there */
        emit_pop_value(c, i, in->b.imm);
        arm_ls_imm(e, LS_STR, A_R2, A_R11, XLAT_RT_IP);
        const int k = exit_stub(c, 0, XLAT_EXIT_BRANCH,
                                sync_recon(c, c->out_set[i], edge_producer(c, i)));
        c->stub[k].dyn = 1;
        emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_AL);
        break;
    }
    }
}

#define GPR_LIST(x) x(A_R4, 0) x(A_R5, 4) x(A_R6, 8) x(A_R7, 12) x(A_R8, 20) x(A_R9, 24) x(A_R10, 28)

static void emit_gpr_transfer(arm_emit_t *e, int kind)
{
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define MOVE(r, off) arm_ls_imm(e, kind, r, A_R12, off);
    GPR_LIST(MOVE)
#undef MOVE
}

/* Run instruction i in XRIP (xlat_interp_one) with the registers written
 * back and reloaded after. Leaves with rt as it stands if XRIP must take
 * over (stop) or, when cont is false or the instruction did not fall
 * through, at its new IP (branch_kind). Otherwise execution goes on after the
 * code emitted here. flags: XI_* bits passed on to the helper. */
static void emit_interp_call(tctx_t *c, int i, uint32_t flags, bool cont, uint8_t branch_kind)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    emit_gpr_transfer(e, LS_STR);
    arm_movw(e, A_R12, in->ip);
    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_IP);
    arm_mov(e, A_R0, A_R11);
    arm_mov32(e, A_R1, flags);
    arm_mov32(e, A_R12, (uint32_t)(uintptr_t)&xlat_interp_one);
    arm_blx(e, A_R12);
    emit_gpr_transfer(e, LS_LDR);
    int k = exit_stub(c, 0, XLAT_EXIT_STOP, -1);
    c->stub[k].dyn = 1;
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 0);
    emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_EQ);      /* state no longer runnable */
    k = exit_stub(c, 0, branch_kind, -1);
    c->stub[k].dyn = 1;
    arm_dp_imm(e, DP_SUB, 1, A_PC, A_R0, 2);
    emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_EQ);      /* other CS: dispatcher */
    if (!cont) {
        emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_AL);
        return;
    }
    arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_IP);
    arm_mov32(e, A_LR, (uint16_t)(in->ip + in->len));
    arm_dp_reg(e, DP_SUB, 1, A_PC, A_R12, A_LR, SH_LSL, 0);
    emit_branch(c, (dest_t){D_STUB, (int16_t)k}, C_NE);      /* it jumped: dispatcher */
}

/* An instruction the translator does not handle: XRIP executes it, and the
 * region goes on when it fell through to the next instruction. */
static void emit_interp(tctx_t *c, int i)
{
    const x86_insn_t *in = &c->in[i];
    const int r = sync_recon(c, c->in_set[i], c->pfp[i]);
    if (r >= 0)
        emit_recon(c, r);
    emit_interp_call(c, i, in->sub & XI_MAY_WRITE, !(in->sub & XI_NO_FALL), XLAT_EXIT_BRANCH);
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

static void emit_insn(tctx_t *c, int i)
{
    arm_emit_t *e = &c->e;
    const x86_insn_t *in = &c->in[i];
    c->label[i] = e->pos;
    switch (in->op) {
    case XO_ALU:
    case XO_INCDEC:
        emit_alu(c, i);
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
    }
    if (falls_through(in) && c->fall_idx[i] != i + 1)
        emit_branch(c, make_dest(c, i, (uint16_t)(in->ip + in->len)), C_AL);
}

static void emit_stubs(tctx_t *c)
{
    arm_emit_t *e = &c->e;
    /* Back-edge stubs first: they branch to exit stubs emitted after them. */
    for (int pass = 0; pass < 2; ++pass) {
        for (int k = 0; k < c->n_stubs; ++k) {
            stub_t *s = &c->stub[k];
            if (pass ? s->type == ST_BACK : s->type != ST_BACK)
                continue;
            s->pos = e->pos;
            for (int j = 0; j < s->nsites; ++j)
                if (!arm_patch_branch(e, s->site[j], s->pos, s->cond[j]))
                    c->overflow = true;
            if (s->type == ST_BACK) {
                const dest_t ex = {D_STUB, s->exit_stub};
                arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_BUDGET);
                arm_dp_imm(e, DP_SUB, 1, A_R12, A_R12, s->budget_n);
                arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_BUDGET);
                emit_branch(c, ex, C_LE);
                arm_ls_imm(e, LS_LDR, A_R12, A_R11, XLAT_RT_INTR);
                arm_ls_imm(e, LS_LDRB, A_R12, A_R12, 0);
                arm_dp_imm(e, DP_SUB, 1, A_PC, A_R12, 0);
                emit_branch(c, ex, C_NE);
                const uint32_t site = arm_branch_slot(e);
                if (!arm_patch_branch(e, site, c->label[s->target], C_AL))
                    c->overflow = true;
            } else if (s->type == ST_SIDE) {
                /* The access cannot be done here: XRIP executes the whole
                 * instruction. A MOV, PUSH or POP sets no flags, so the code
                 * after it may go on; anything else leaves after it. */
                const int i = s->insn;
                const x86_insn_t *in = &c->in[i];
                const bool cont = falls_through(in) && !is_producer(in) && c->fall_idx[i] >= 0;
                if (s->recon >= 0)
                    emit_recon(c, s->recon);
                emit_interp_call(c, i, (s->mw ? XI_MAY_WRITE : 0u) | XI_SIDE, cont, XLAT_EXIT_SIDE);
                if (cont) {
                    const uint32_t site = arm_branch_slot(e);
                    if (!arm_patch_branch(e, site, c->label[c->fall_idx[i]], C_AL))
                        c->overflow = true;
                }
            } else {
                if (s->recon >= 0)
                    emit_recon(c, s->recon);
                if (!s->dyn) {
                    arm_movw(e, A_R12, s->ip);
                    arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_IP);
                }
                arm_mov32(e, A_R12, s->exit_kind);
                arm_ls_imm(e, LS_STR, A_R12, A_R11, XLAT_RT_EXIT_KIND);
                const uint32_t site = arm_branch_slot(e);
                if (!arm_patch_branch(e, site, c->epilogue, C_AL))
                    c->overflow = true;
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Trampoline and entry point                                             */
/* ---------------------------------------------------------------------- */

bool xlat_emit_trampoline(xlat_arena_t *a)
{
    arm_emit_t e = {a->base, 0, a->size, false};
    /* entry(rt = r0, target | 1 = r1) */
    arm_push(&e, 0x4ff8u);                     /* r3-r11, lr: 8-byte aligned */
    arm_mov(&e, A_R11, A_R0);
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define LOAD(r, off) arm_ls_imm(&e, LS_LDR, r, A_R12, off);
    GPR_LIST(LOAD)
#undef LOAD
    arm_bx(&e, A_R1);
    arm_e16(&e, 0xbf00u);                      /* align the epilogue */
    a->epilogue = e.pos;
    arm_ls_imm(&e, LS_LDR, A_R12, A_R11, XLAT_RT_GPR);
#define STORE(r, off) arm_ls_imm(&e, LS_STR, r, A_R12, off);
    GPR_LIST(STORE)
#undef STORE
    arm_pop(&e, 0x8ff8u);                      /* r3-r11, pc */
    if (e.full)
        return false;
    a->code_top = (e.pos + 3u) & ~3u;
    a->data_bottom = a->size;
    return true;
}

size_t xlat_translate_ctx_size(void)
{
    return sizeof(tctx_t);
}

bool xlat_translate(xlat_arena_t *a, const xlat_rt_t *rt, uint16_t ip,
                    xlat_region_out_t *out, bool *full, void *ctx_mem)
{
    *full = false;
    tctx_t *const c = (tctx_t *)ctx_mem;
    bool ok = false;
    c->rt = rt;
    c->cs_base = rt->seg_base[1];
    c->n_stubs = c->n_fix = 0;
    c->overflow = false;
    c->epilogue = a->epilogue;

    if (!discover(c, ip))
        goto done;
    dataflow(c);
    plan(c);

    /* Source copies: runs of adjacent instructions. */
    out->n_runs = 0;
    uint32_t data = 0;
    for (int i = 0; i < c->n; ++i) {
        if (out->n_runs && out->runs[out->n_runs - 1].ip + out->runs[out->n_runs - 1].len ==
                               c->in[i].ip) {
            out->runs[out->n_runs - 1].len += c->in[i].len;
        } else {
            out->runs[out->n_runs].ip = c->in[i].ip;
            out->runs[out->n_runs].len = c->in[i].len;
            out->n_runs++;
        }
    }
    for (int r = 0; r < out->n_runs; ++r)
        data += 8u + ((out->runs[r].len + 3u) & ~3u);
    if (a->code_top + data >= a->data_bottom) {
        *full = true;
        goto done;
    }

    c->e = (arm_emit_t){a->base, a->code_top, a->data_bottom - data, false};
    for (int i = 0; i < c->n; ++i)
        c->side_stub[i][0] = c->side_stub[i][1] = -1;
    for (int i = 0; i < c->n; ++i)
        emit_insn(c, i);
    emit_stubs(c);
    for (int k = 0; k < c->n_fix; ++k)
        if (!arm_patch_branch(&c->e, c->fix[k].site, c->label[c->fix[k].insn], c->fix[k].cond))
            c->overflow = true;
    if (c->e.full) {
        *full = true;
        goto done;
    }
    if (c->overflow)
        goto done;

    out->code_off = a->code_top;
    out->code_len = c->e.pos - a->code_top;
    out->lo = c->lo;
    out->span = c->span;
    out->n_entries = 0;
    for (int i = 0; i < c->n; ++i) {
        if (c->leader[i]) {
            out->entries[out->n_entries].ip = c->in[i].ip;
            out->entries[out->n_entries].code_off = c->label[i];
            out->n_entries++;
        }
    }
    out->data_size = data;
    a->code_top = (c->e.pos + 3u) & ~3u;
    ok = true;
done:
    return ok;
}
