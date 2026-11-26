/*
 * murm386 x86 -> ARM Thumb-2 region translator (FDOS xlat).
 * See docs/FDOS-JIT-architecture.md.
 *
 * Public interface used by the XRIP adapter. The translator is entered only
 * at the start of an XRIP burst (one lookup per <= 4096 guest instructions);
 * the interpreter loop itself carries no translator checks.
 */
#ifndef FDOS_XLAT_H
#define FDOS_XLAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Guest state shared with the generated code. The generated code addresses
 * the fields by the XLAT_RT_* offsets below, so the layout is fixed.
 *
 * Flags use the XRIP lazy representation unchanged: lazy_op == 0 means
 * eflags holds all arithmetic flags; otherwise lazy_result/dst/src/cf
 * describe the last ALU operation (src inverted for SUB, values shifted so
 * the operand sign is bit 31; lazy_op = kind << 5 | shift).
 */
typedef struct xlat_rt {
    uint8_t *mem;                  /* fast (conventional) guest memory */
    uint32_t mem_size;             /* bytes reachable through mem, 0: none */
    uint32_t seg_base[4];          /* ES, CS, SS, DS (XRIP reges..regds) */
    int32_t budget;                /* guest instructions until the next clock check */
    const volatile uint8_t *intr;  /* pending IRQ (or a constant 0 when IF=0) */
    uint32_t *gpr;                 /* EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI */
    uint32_t lazy_result;
    uint32_t lazy_dst;
    uint32_t lazy_src;
    uint32_t eflags;               /* x86 FLAGS image; read only for the translator */
    uint8_t lazy_op;
    uint8_t lazy_cf;
    uint16_t cs;                   /* CS selector */
    uint32_t ip;                   /* in: entry IP; out: guest IP at exit */
    uint32_t exit_kind;            /* XLAT_EXIT_* of the last region exit */
    const volatile uint32_t *clock;  /* microseconds, the clock of get_uticks() */
    uint32_t deadline;             /* the burst ends once *clock reaches it */
    uint32_t extra;                /* budget added at clock checks (instructions) */
    const uint8_t *code_map;       /* xlat_code_map (set by xlat_run) */
    uint32_t fn_key;               /* CS:IP of a hot CALL target, or UINT32_MAX */
    /* Set by xlat_run() (the caller need not): the link table the region
     * exits look the next region up in (xlat_emit_trampoline), the epoch
     * its entries must carry, the stamp written to last[region] on a linked
     * entry, the running region (index) and the linked entries made. */
    const void *link;
    uint32_t epoch;
    uint32_t seq;
    uint32_t *last;
    uint32_t cur;
    uint32_t links;
    uint32_t sp;                   /* ESP while generated code runs (the trampoline
                                      copies it from and back to gpr[4]) */
    /* Upper memory the generated code reads and writes through mem as
     * well: [XLAT_UMB_BASE, XLAT_UMB_BASE + XLAT_UMB_MARGIN + umb_room)
     * is at mem + linear (0: none). An access of up to XLAT_UMB_MARGIN + 1
     * bytes is checked by its last byte: the first XLAT_UMB_MARGIN bytes
     * of the window are left to XRIP. Not the VGA aperture A0000-BFFFF. */
    uint32_t umb_room;
    /* Exact checks behind xlat_code_map (xlat_rt_gap): a write into a
     * marked block within [gap_lo, gap_hi) touches no translated x86 code;
     * one meeting [hit_lo, hit_hi) does (a run of some region). Both are
     * reset when a region is installed; empty when lo >= hi. */
    uint32_t gap_lo, gap_hi;
    uint32_t hit_lo, hit_hi;
    uint32_t wall_deadline;       /* nonzero: independent real-time deadline */
    uint32_t budget_limit;        /* total instruction credit, 0 unlimited */
    uint32_t link_alt;            /* secondary link matches, before budget check */
    uint32_t gap2_lo, gap2_hi;    /* second exact negative range, checked in ARM */
} xlat_rt_t;

#define XLAT_UMB_BASE   0xC0000u
#define XLAT_UMB_MARGIN 127u

#define XLAT_RT_MEM          0
#define XLAT_RT_MEM_SIZE     4
#define XLAT_RT_SEG_BASE     8
#define XLAT_RT_BUDGET      24
#define XLAT_RT_INTR        28
#define XLAT_RT_GPR         32
#define XLAT_RT_RESULT      36
#define XLAT_RT_DST         40
#define XLAT_RT_SRC         44
#define XLAT_RT_EFLAGS      48
#define XLAT_RT_OP          52
#define XLAT_RT_CF          53
#define XLAT_RT_CS          54
#define XLAT_RT_IP          56
#define XLAT_RT_EXIT_KIND   60
#define XLAT_RT_CLOCK       64
#define XLAT_RT_DEADLINE    68
#define XLAT_RT_EXTRA       72
#define XLAT_RT_CODE_MAP    76
#define XLAT_RT_LINK        84
#define XLAT_RT_EPOCH       88
#define XLAT_RT_SEQ         92
#define XLAT_RT_LAST        96
#define XLAT_RT_CUR        100
#define XLAT_RT_LINKS      104
#define XLAT_RT_SP         108
#define XLAT_RT_UMB        112
#define XLAT_RT_GAP_LO     116
#define XLAT_RT_GAP_HI     120
#define XLAT_RT_HIT_LO     124
#define XLAT_RT_HIT_HI     128
#define XLAT_RT_LINK_ALT   140
#define XLAT_RT_GAP2_LO    144
#define XLAT_RT_GAP2_HI    148

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(__builtin_offsetof(xlat_rt_t, mem_size) == XLAT_RT_MEM_SIZE &&
               __builtin_offsetof(xlat_rt_t, seg_base) == XLAT_RT_SEG_BASE &&
               __builtin_offsetof(xlat_rt_t, budget) == XLAT_RT_BUDGET &&
               __builtin_offsetof(xlat_rt_t, intr) == XLAT_RT_INTR &&
               __builtin_offsetof(xlat_rt_t, gpr) == XLAT_RT_GPR &&
               __builtin_offsetof(xlat_rt_t, lazy_result) == XLAT_RT_RESULT &&
               __builtin_offsetof(xlat_rt_t, lazy_dst) == XLAT_RT_DST &&
               __builtin_offsetof(xlat_rt_t, lazy_src) == XLAT_RT_SRC &&
               __builtin_offsetof(xlat_rt_t, eflags) == XLAT_RT_EFLAGS &&
               __builtin_offsetof(xlat_rt_t, lazy_op) == XLAT_RT_OP &&
               __builtin_offsetof(xlat_rt_t, lazy_cf) == XLAT_RT_CF &&
               __builtin_offsetof(xlat_rt_t, cs) == XLAT_RT_CS &&
               __builtin_offsetof(xlat_rt_t, ip) == XLAT_RT_IP &&
               __builtin_offsetof(xlat_rt_t, exit_kind) == XLAT_RT_EXIT_KIND &&
               __builtin_offsetof(xlat_rt_t, clock) == XLAT_RT_CLOCK &&
               __builtin_offsetof(xlat_rt_t, deadline) == XLAT_RT_DEADLINE &&
               __builtin_offsetof(xlat_rt_t, extra) == XLAT_RT_EXTRA &&
               __builtin_offsetof(xlat_rt_t, code_map) == XLAT_RT_CODE_MAP &&
               __builtin_offsetof(xlat_rt_t, link) == XLAT_RT_LINK &&
               __builtin_offsetof(xlat_rt_t, epoch) == XLAT_RT_EPOCH &&
               __builtin_offsetof(xlat_rt_t, seq) == XLAT_RT_SEQ &&
               __builtin_offsetof(xlat_rt_t, last) == XLAT_RT_LAST &&
               __builtin_offsetof(xlat_rt_t, cur) == XLAT_RT_CUR &&
               __builtin_offsetof(xlat_rt_t, links) == XLAT_RT_LINKS &&
               __builtin_offsetof(xlat_rt_t, sp) == XLAT_RT_SP &&
               __builtin_offsetof(xlat_rt_t, umb_room) == XLAT_RT_UMB &&
               __builtin_offsetof(xlat_rt_t, gap_lo) == XLAT_RT_GAP_LO &&
               __builtin_offsetof(xlat_rt_t, gap_hi) == XLAT_RT_GAP_HI &&
               __builtin_offsetof(xlat_rt_t, hit_lo) == XLAT_RT_HIT_LO &&
               __builtin_offsetof(xlat_rt_t, hit_hi) == XLAT_RT_HIT_HI &&
               __builtin_offsetof(xlat_rt_t, link_alt) == XLAT_RT_LINK_ALT &&
               __builtin_offsetof(xlat_rt_t, gap2_lo) == XLAT_RT_GAP2_LO &&
               __builtin_offsetof(xlat_rt_t, gap2_hi) == XLAT_RT_GAP2_HI,
               "xlat_rt_t layout is used by generated code");
#endif

enum {
    XLAT_EXIT_BRANCH = 0,   /* control left the region (target not translated) */
    XLAT_EXIT_BUDGET = 1,   /* burst time used up or IRQ pending, at a back edge */
    XLAT_EXIT_SIDE = 2,     /* memory operand outside fast and upper memory, or a write to the region's code */
    XLAT_EXIT_STOP = 3,     /* after an interpreted instruction the state needs XRIP (mode, CS, IRQ...) */
    XLAT_EXIT_COUNT = 4,
    XLAT_EXIT_FAR_CALL = XLAT_EXIT_COUNT  /* far/indirect call miss, counted as a branch */
};

/* xlat_enter() outcomes, at a burst start (S_*) and at a cut (C_*) */
enum {
    XLAT_ENTER_S_RAN, XLAT_ENTER_S_NONE, XLAT_ENTER_S_MOMENT, XLAT_ENTER_S_MODE, XLAT_ENTER_S_BUSY,
    XLAT_ENTER_C_RAN, XLAT_ENTER_C_NONE, XLAT_ENTER_C_MOMENT, XLAT_ENTER_C_MODE,
    XLAT_ENTER_COUNT
};

typedef struct xlat_stats {
    uint32_t entries;           /* region executions */
    uint32_t translations;      /* regions built */
    uint32_t rejects;           /* hot entry points that could not be translated */
    uint32_t invalidations;     /* regions dropped because their x86 bytes changed */
    uint32_t flushes;           /* whole-cache resets */
    uint32_t evictions;         /* regions dropped to make room (ring, slots) */
    uint32_t slot_evictions;    /* of those: for a region slot (all 64 held) */
    uint32_t ring_retries;      /* translations that did not fit at the ring head */
    uint32_t gap_calls;         /* xlat_rt_gap(): exact write checks the cached gap/hit missed */
    uint32_t retries;           /* rejected burst starts tried again (still hot) */
    uint32_t retries_built;     /* of those: built */
    uint32_t translate_us;      /* time in the translator (xlat_translate, eviction) */
    uint32_t translate_max_us;
    uint32_t compile_defers;    /* compiler slices paused (not rejected) */
    uint32_t compile_resumes;   /* work continued from a previous slice */
    uint32_t compile_cancel;    /* unpublished job discarded on memory change */
    uint32_t compile_queue_drops; /* requests denied or displaced by a higher priority */
    uint32_t disk_irq_yields;   /* ISR service points in native FDOS reads */
    uint32_t fn_regions;        /* regions without a loop (traces, function regions) */
    uint32_t exit_built;        /* of those: built at a hot region exit target */
    uint32_t entries_added;     /* regions translated again with one more entry point */
    uint32_t links;             /* region entries through the link table (no C) */
    uint32_t exits[XLAT_EXIT_COUNT];
    uint32_t guest_insns;       /* approximate guest instructions run natively */
    uint32_t code_bytes;        /* current code-cache use */
    uint32_t alloc_failed;      /* no PSRAM span for the cache: translator off */
    uint32_t stack_skips;       /* translations postponed: core-0 stack too low */
    uint32_t interp;            /* instructions run by XRIP from inside a region */
    uint32_t side_interp;       /* of those: accesses outside fast memory or onto the code */
    uint32_t int_direct, int_leaf; /* software INT / native BIOS escape without decoding */
    uint32_t local_checks, local_invalid;
    uint32_t local_candidates, gap_candidates, rom_dispatch;
    uint32_t string_calls, string_elems, shadow_pop;
    uint32_t fast_string_calls, fast_string_elems, shadow_fast, gap_reuse;
    uint32_t rep_yield, shadow_umb, shadow_mem_miss, shadow_irq_miss;
    uint32_t thunk_int, thunk_retf, link_alt, bios_flags_umb;
    uint32_t bus_read, bus_write, bus_smc, bus_write_miss, bus_pop, bus_rmw;
    uint32_t string_preempt, bios_write_leaf, bios_cursor_local;
    uint32_t scalar_resume, scalar_dispatch, scalar_refused, scalar_stop;
    uint32_t compiled_code_bytes, compiled_source_bytes;
    uint32_t epoch_start, epoch_helper, revalidations, revalidate_bytes;
    uint32_t service_clean, service_foreign, host_local, host_unknown;
    uint32_t cuts;              /* XRIP bursts ended at a marked branch target */
    uint32_t cut_entries;       /* of those: translated code was entered */
    uint32_t cold;              /* regions given back to XRIP (too short per entry) */
    /* XLAT_LOG: CPU bursts timed by the adapter (time in us, instructions),
     * without bursts that ran nested bursts (native INT handlers); and the
     * device service after each burst (pc_service_impl). Histograms by
     * duration: < 50, 100, 200, 400, 800, 1600, 3200 us, longer. */
    uint32_t xrip_bursts, xlat_bursts;   /* XRIP only / with translated code */
    uint64_t xrip_tail;         /* XRIP instructions in bursts with translated code */
    uint32_t enter_why[XLAT_ENTER_COUNT];  /* xlat_enter() outcomes (XLAT_LOG) */
    uint64_t xrip_us, xrip_insns, xlat_us, xlat_insns;
    uint64_t mix_xrip_us, mix_xrip_insns;  /* of xlat_us / xlat_insns: inside run_instructions() */
    uint32_t xrip_hist[8], xlat_hist[8];
    uint32_t svc_n, svc_max, svc_hist[8];
    uint64_t svc_us;
} xlat_stats_t;

extern xlat_stats_t xlat_stats;

/*
 * Length of a CPU burst that runs translated code, in microseconds of the
 * clock rt->clock. XRIP bursts are 4096 instructions; translated code runs
 * them several times faster, and counting its instructions alone would make
 * the device service after each burst (pc_service) run several times as
 * often per unit of real time. So translated code runs until the burst's
 * deadline: the instruction budget only sets how often the clock is read
 * (every XLAT_SLICE_CHECK instructions once the first rt->budget is used).
 * The default is about the time XRIP needs for 4096 instructions (the XLAT_LOG
 * line "B" shows the measured value).
 */
#ifndef XLAT_SLICE_US
#define XLAT_SLICE_US 400u
#endif
#define XLAT_SLICE_CHECK 256
#define XLAT_XRIP_CHECK 128u

/*
 * Run translated code for the guest state in rt, starting at rt->cs:rt->ip.
 * Returns false and leaves rt untouched when no translation applies (the
 * caller interprets). Returns true after running at least one region; rt
 * then holds the new IP, flags and budget, and the registers have been
 * written back to rt->gpr. Instructions run: initial budget + rt->extra -
 * rt->budget (the caller sets extra to 0).
 *
 * Preconditions (checked by the caller): real mode, 16-bit code, TF clear,
 * no pending IRQ with IF set, not halted, rt->mem_size != 0.
 */
bool xlat_run(xlat_rt_t *rt);
/* Credit from completed guest CPU bursts, excluding compiler time. */
void xlat_compile_credit(uint32_t guest_us);

/* Drop every translation; defer recycling while a trampoline is active. */
void xlat_flush(void);

/* Invalidate code in guest DOS memory returned to the MCB allocator.
 * Safe inside a native handler called by a translated region. */
void xlat_forget_range(uint32_t linear, uint32_t len);

/* The memory block was taken away (machine reset clears the PSRAM span
 * table): forget it; the next hot spot allocates a new one. */
void xlat_release(void);

/* XLAT_LOG builds: append the collected regions and counters to
 * SD_DATA_DIR/xlat.log. Only at a safe point (a DOS program has ended). */
void xlat_log_write(uint16_t psp, const char *name);

/* Provided by the CPU side: the guest bytes [linear, linear + len) are at
 * rt->mem + linear, readable directly (conventional memory, and upper
 * memory / ROM / HMA where they are direct PSRAM, not VGA or an EMS
 * window). Only code is fetched this way: translated memory accesses past
 * rt->mem_size still go to XRIP. */
bool xlat_code_direct(uint32_t linear, uint32_t len);

/* An instruction of the running region (rt->cur) went to XRIP for a memory
 * access outside fast memory (side): counted per region (xlat_run drops
 * regions that mostly do that, as cold ones). */
void xlat_note_side(const xlat_rt_t *rt);

/* XLAT_LOG: time outside translated code, by CS:IP (key cs << 16 | ip):
 * XRIP instructions run at the end of a burst (weight: how many), the
 * instructions translated code hands to XRIP (xlat_interp_one): those it has
 * no translation for (interp) and the memory accesses outside fast memory or
 * into translated code (side). Logged
 * as P lines at a program's exit. */
enum { XLAT_PROF_XRIP, XLAT_PROF_INTERP, XLAT_PROF_SIDE, XLAT_PROF_KINDS };
/* Exact XRIP instruction counts by 256-byte linear page of the burst end
 * (XLAT_LOG; the space-saving top list above only bounds them). */
void xlat_prof_page(uint32_t linear, uint32_t weight);
void xlat_prof_note(unsigned kind, uint32_t key, uint32_t linear, const uint8_t *mem,
                    uint32_t mem_size, uint32_t weight);

/* Provided by the CPU adapter (and by tools/xlat_test): execute the one
 * instruction at rt->cs:rt->ip with the interpreter, rt->gpr and the lazy
 * flags in rt being the current state, and update rt (IP, flags, FLAGS image,
 * segment bases, IRQ pointer). flags bit 0: the instruction may write
 * memory, recheck the region's code; bit 2: statistics only (a side access).
 * Returns 1 if the region may go on; 2 if CS changed (rt->cs and
 * rt->seg_base[1] updated) and the dispatcher may continue there; 0 if XRIP
 * must take over (mode, TF, IRQ, HLT, stack low, or the region's code changed). */
#define XLAT_XI_INT3     8u
#define XLAT_XI_INT_IMM 16u
#define XLAT_XI_INT_MASK (XLAT_XI_INT3 | XLAT_XI_INT_IMM)
/* Bits 8..11 carry the decoded length; bit 15 is the continuation.
 * The low bits of a native string call encode operands, not INT flags. */
#define XLAT_XI_STRING     0x1000u
#define XLAT_XI_STI_POP    0x2000u
#define XLAT_XS_STOS       1u
#define XLAT_XS_REP        2u
#define XLAT_XS_SIZE_SHIFT 3u
#define XLAT_XS_SEG_SHIFT  5u
uint32_t xlat_interp_one(xlat_rt_t *rt, uint32_t flags);

/* Try a checked nonblocking native ROM entry at rt->cs:ip after a
 * translated far transfer. False: no guest instruction executed. */
bool xlat_try_native_escape(xlat_rt_t *rt);

/* Dynamic three-byte INT/RETF thunks in conventional RAM. No saved
 * source or assumed vector; check the live bytes at each dispatch. */
static inline unsigned xlat_thunk_kind(const xlat_rt_t *rt, uint32_t key)
{
#if defined(XLAT_HOST_TEST)
    (void)rt; (void)key;
#else
    const uint16_t ip = (uint16_t)key;
    const uint32_t a = ((key >> 16) << 4) + ip;
    if (a >= rt->mem_size)
        return 0;
    if (rt->mem[a] == 0xcdu && ip <= 0xfffdu &&
        rt->mem_size - a >= 3u && rt->mem[a + 2u] == 0xcbu)
        return 1;
    if (rt->mem[a] == 0xcbu && ip >= 2u && rt->mem[a - 2u] == 0xcdu)
        return 2;
#endif
    return 0;
}
bool xlat_try_native_thunk(xlat_rt_t *rt, unsigned kind);

/* For xlat_interp_one(): the x86 bytes of the running region are unchanged
 * (if not, the region is dropped). Compares only when xlat_dirty is set:
 * the instruction (or something before it) may have written translated
 * code. */
bool xlat_region_still_valid(xlat_rt_t *rt);

/* Native stack left (bytes) and the margin needed before translating or
 * interpreting from translated code. */
uint32_t xlat_stack_free(void);
#define XLAT_STACK_NEEDED 1024u

/*
 * Entry points XRIP's taken branches (jump_relative: Jcc, JMP, CALL, LOOP)
 * look up: a hashed bit per linear address of a loop head or region entry.
 * A set bit ends the interpreter burst early so the adapter can enter the
 * translated code (xrip_step_cpu); a false hit only costs a lookup.
 */
/* Translated x86 code by 64-byte block. The filter also marks possible
 * starting addresses of writes up to 16 bytes. Wider individual stores
 * use XRIP; bulk REP checks its whole destination separately. */
#define XLAT_CODE_BLOCK_SHIFT 6u
/* Up to the end of the HMA: code outside fast memory (upper memory, ROM,
 * HMA) can be translated too (xlat_code_direct) */
#define XLAT_CODE_MAP_BYTES (0x110000u >> (XLAT_CODE_BLOCK_SHIFT + 3u))
extern uint8_t xlat_code_map[XLAT_CODE_MAP_BYTES];

/* Translated x86 code may have changed since the last xlat_run(). Set on
 * untrusted CPU entry, untrusted callbacks, native BIOS/DOS calls and
 * mapping changes. Known RAM writes use the local range below.
 * Consecutive guest-only bursts may retain their epoch. xlat_run() and
 * xlat_region_still_valid() revalidate the affected regions when dirty. */
extern bool xlat_dirty;  /* unknown writes / mapping changes */
extern uint32_t xlat_write_lo, xlat_write_hi; /* known writes: half-open range, hi=0 when clean */
/* One-shot permission from an audited consecutive guest loop. Other CPU
 * entry points retain the conservative dirty-on-entry behavior. */
extern bool xlat_clean_resume;
/* n is 1..16: the map also covers starts of writes before source bytes. */
static inline void xlat_note_write_n(uint32_t linear, uint32_t n)
{
    if (xlat_code_map[linear >> (XLAT_CODE_BLOCK_SHIFT + 3)] & (1u << ((linear >> XLAT_CODE_BLOCK_SHIFT) & 7u))) {
        if (!xlat_write_hi || linear < xlat_write_lo)
            xlat_write_lo = linear;
        if (linear + n > xlat_write_hi)
            xlat_write_hi = linear + n;
    }
}

static inline void xlat_note_write(uint32_t linear)
{
    xlat_note_write_n(linear, 16u);
}

/* Synchronous host/device write, before the actual store(s). Only audited
 * direct conventional RAM is admitted to the exact local-write path. */
void xlat_note_host_write(uint32_t linear, uint32_t len);

/* Hot CALL targets (near, far, r/m) seen by XRIP: a target reaching
 * XLAT_CALL_HOT calls in its slot requests a cut (the caller sets
 * rt->fn_key), so a function without a loop can get a region. Further
 * cuts are spaced out until the cache acknowledges the request; later
 * calls reach it through xlat_entry_map, its return sites get regions as hot
 * region exit targets (xlat_run). Cleared with the cache. XLAT_TRACES: CMake
 * XLAT_TRACES (regions without a loop, xlat_internal.h). */
#ifndef XLAT_TRACES
#define XLAT_TRACES 1
#endif
#define XLAT_CALL_SLOTS 128u
#define XLAT_CALL_HOT   32u
#define XLAT_CALL_RESCAN 8u
#define XLAT_CALL_ACK   UINT8_MAX
extern uint32_t xlat_call_key[XLAT_CALL_SLOTS];
extern uint8_t xlat_call_count[XLAT_CALL_SLOTS];
extern bool xlat_call_queue_blocked;
static inline bool xlat_call_hot(uint32_t linear)
{
    if (!XLAT_TRACES || xlat_call_queue_blocked)
        return false;
    const uint32_t h = (linear * 0x9e3779b1u) >> 25;
    if (xlat_call_key[h] != linear) {
        xlat_call_key[h] = linear;
        xlat_call_count[h] = 1;
        return false;
    }
    const uint32_t n = xlat_call_count[h];
    if (n == XLAT_CALL_ACK)
        return false;
    const uint8_t next = n + 1 == XLAT_CALL_HOT + XLAT_CALL_RESCAN ? XLAT_CALL_HOT : n + 1;
    xlat_call_count[h] = next;
    return next == XLAT_CALL_HOT;
}

static inline void xlat_call_ack(uint32_t linear)
{
    const uint32_t h = (linear * 0x9e3779b1u) >> 25;
    if (xlat_call_key[h] == linear)
        xlat_call_count[h] = XLAT_CALL_ACK;
}

static inline void xlat_call_rearm(uint32_t linear)
{
    const uint32_t h = (linear * 0x9e3779b1u) >> 25;
    if (xlat_call_key[h] == linear && xlat_call_count[h] == XLAT_CALL_ACK)
        xlat_call_count[h] = XLAT_CALL_HOT - 1;
}

#define XLAT_MAP_BITS 8192u
extern uint8_t xlat_entry_map[XLAT_MAP_BITS / 8];
static inline uint32_t xlat_map_index(uint32_t linear)
{
    return (linear ^ (linear >> 13)) & (XLAT_MAP_BITS - 1u);
}
static inline bool xlat_map_test(uint32_t linear)
{
    const uint32_t i = xlat_map_index(linear);
    return (xlat_entry_map[i >> 3] >> (i & 7u)) & 1u;
}

/* XLAT_LOG: histogram bucket of a duration in us (see xlat_stats_t). */
static inline uint32_t xlat_us_bucket(uint32_t us)
{
    uint32_t b = 0;
    for (uint32_t lim = 50; b < 7 && us >= lim; lim <<= 1)
        ++b;
    return b;
}

/* XLAT_LOG: the device service after a CPU burst took us microseconds. */
void xlat_note_service(uint32_t us);

/* Helper called by generated code: x86 condition code cc on the lazy flags. */
uint32_t xlat_rt_cond(xlat_rt_t *rt, uint32_t cc);

/* Flags left pending by generated code: lazy_op has XLAT_LAZY_PENDING set,
 * lazy_dst / lazy_src hold the operands (<< shift; LOGIC: the result in
 * both) of the operation lazy_op & 0x7e, the result and CF are not stored
 * (CF is, with XLAT_LAZY_KEEP_CF). While generated code runs, lazy_cf is
 * also CF when lazy_op is 0 (xlat_run, the helpers keep it so): INC / DEC
 * and ADC / SBB read it there.
 * xlat_rt_pending() completes the lazy state (XRIP's kinds never set the
 * bit). xlat_run() returns, and the helpers read, completed flags only. */
#define XLAT_LAZY_PENDING 0x80u
#define XLAT_LAZY_KEEP_CF 0x01u   /* pending INC / DEC: lazy_cf is its CF already */
void xlat_rt_pending(xlat_rt_t *rt);

/* The thunk's interpreter call: xlat_rt_pending(), then xlat_interp_one(). */
uint32_t xlat_interp_call(xlat_rt_t *rt, uint32_t flags);

/* Helper called by generated code: shift / rotate (group 2) of value with
 * XRIP's flag results in rt. ctl = operation | 16-bit << 3 | count << 8. */
uint32_t xlat_rt_shift(xlat_rt_t *rt, uint32_t value, uint32_t ctl);

/* Helper called by generated code: MUL / IMUL / DIV / IDIV as XRIP; see
 * xlat_cache.c for ctl and axdx. High word of the result: 1 on #DE. */
uint64_t xlat_rt_muldiv(xlat_rt_t *rt, uint32_t ctl, uint32_t operand, uint32_t axdx);

/* Helper called by generated code (PUSHF, LAHF, SAHF): resolves the lazy
 * flags in rt as XRIP's flags_now() and returns rt->eflags. */
uint32_t xlat_rt_resolve(xlat_rt_t *rt);

/* Helper for POPF: TF, IOPL or NT changes fall back to XRIP;
 * IF changes alone do not create an interrupt shadow. */
uint32_t xlat_rt_popf(xlat_rt_t *rt, uint32_t value);

/* Helper called by generated code for REP MOVS / REP STOS (DF clear): the
 * whole run at once when it is in direct memory (fast or upper, xlat_rt_t),
 * neither offset wraps and the destination holds no translated code
 * (xlat_code_map). src: linear address of DS:SI (MOVS) or the value (STOS),
 * dst: of ES:DI, ctl: CX | bytes << 16 | XLAT_REP_STOS | the source
 * segment << XLAT_REP_SEG_SHIFT. Returns elements transferred (possibly
 * partial at an IRQ/deadline), or 0 before any write. Caller updates SI,
 * DI and CX. Each internal chunk has at most XLAT_REP_CHUNK elements. */
#define XLAT_REP_CHUNK 256u
#define XLAT_REP_STOS      (1u << 19)
#define XLAT_REP_SEG_SHIFT 20
/* Low half: completed elements. A partial run with this bit set must
 * yield at the REP IP, not enter the synchronized memory helper. */
#define XLAT_REP_YIELD (1u << 16)
uint32_t xlat_rt_rep(xlat_rt_t *rt, uint32_t src, uint32_t dst, uint32_t ctl);
/* Explicit operands: no GPR spill, decoder or XRIP state synchronization.
 * A zero/partial count leaves the remaining elements for the slow path. */
uint32_t xlat_rt_string_fast(xlat_rt_t *rt, uint32_t src, uint32_t dst, uint32_t ctl);
/* The decoded scalar memory path: same XRIP bus reads, without its CPU
 * context. Writes are admitted only for verified direct RAM or VGA.
 * 0: refused before effects; 1: done; 2: done, redispatch after SMC. */
#define XLAT_MEM_REDISPATCH 2u
uint32_t xlat_bus_read(uint32_t address, unsigned bytes);
uint32_t xlat_rt_bus_write(xlat_rt_t *rt, uint32_t address, uint32_t value, unsigned bytes);
uint32_t xlat_rt_bus_add(xlat_rt_t *rt, uint32_t address, uint32_t value, unsigned bytes);
uint32_t xlat_rt_bus_pop(xlat_rt_t *rt, uint32_t release, uint32_t far_ret);
uint32_t xlat_rt_string_ready(xlat_rt_t *rt);
/* Only called for accesses wholly within A0000..BFFFF. */
uint32_t xlat_vga_read(uint32_t address, unsigned bytes);
void xlat_vga_write(uint32_t address, uint32_t value, unsigned bytes);
/* STI + POP DS/ES, direct stack only. Returns the XLAT_IO_* status,
 * or zero before changing any state (synchronized fallback). */
uint32_t xlat_rt_sti_pop(xlat_rt_t *rt, uint32_t seg);

/* A write of [addr, last] by generated code into a block xlat_code_map
 * marks (the trampoline's checks, when rt->gap / rt->hit do not decide):
 * 1 when it meets the x86 bytes of a live region (rt->hit set to that
 * run), else 0 (rt->gap set to the gap around it, within its 4 KiB). */
uint32_t xlat_rt_gap(uint32_t addr, uint32_t last, xlat_rt_t *rt);

/* Helper called by generated code for an x87 instruction (and FWAIT), the
 * memory operand's linear address already checked (fast memory, no
 * translated code there). Provided by the CPU core (fpu_exec1/2 as XRIP's
 * escape): returns 0 when XRIP must do it instead (CR0.EM/TS: #NM); nothing
 * is done then. FNSTSW AX writes rt->gpr[0]'s low half. */
#define XLAT_FPU_MEM   0x200u     /* ctl: op | group << 3 | rm << 6 | ... */
#define XLAT_FPU_WAIT  0x400u
uint32_t xlat_rt_fpu(xlat_rt_t *rt, uint32_t ctl, uint32_t addr);

/* I/O and IF helper. Unsafe OUT falls back before any side effect.
 * XLAT_IO_BOUNDARY means the instruction completed and XRIP must take over
 * at the next IP (pending IRQ or STI shadow). SET_IF is only for POPF. */
#define XLAT_IO_OUT    0x00100000u
#define XLAT_IO_CLI    0x00200000u
#define XLAT_IO_STI    0x00400000u
#define XLAT_IO_SET_IF 0x00800000u
#define XLAT_IO_DONE   0x00010000u
#define XLAT_IO_BOUNDARY 0x00020000u
uint32_t xlat_rt_io(xlat_rt_t *rt, uint32_t ctl, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
