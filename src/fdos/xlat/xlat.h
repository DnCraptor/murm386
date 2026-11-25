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
} xlat_rt_t;

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
               __builtin_offsetof(xlat_rt_t, extra) == XLAT_RT_EXTRA,
               "xlat_rt_t layout is used by generated code");
#endif

enum {
    XLAT_EXIT_BRANCH = 0,   /* control left the region (target not translated) */
    XLAT_EXIT_BUDGET = 1,   /* burst time used up or IRQ pending, at a back edge */
    XLAT_EXIT_SIDE = 2,     /* memory operand outside fast memory, or a write to the region's code */
    XLAT_EXIT_STOP = 3,     /* after an interpreted instruction the state needs XRIP (mode, CS, IRQ...) */
    XLAT_EXIT_COUNT = 4
};

typedef struct xlat_stats {
    uint32_t entries;           /* region executions */
    uint32_t translations;      /* regions built */
    uint32_t rejects;           /* hot entry points that could not be translated */
    uint32_t invalidations;     /* regions dropped because their x86 bytes changed */
    uint32_t flushes;           /* whole-cache resets */
    uint32_t exits[XLAT_EXIT_COUNT];
    uint32_t guest_insns;       /* approximate guest instructions run natively */
    uint32_t code_bytes;        /* current code-cache use */
    uint32_t alloc_failed;      /* no PSRAM span for the cache: translator off */
    uint32_t stack_skips;       /* translations postponed: core-0 stack too low */
    uint32_t interp;            /* instructions run by XRIP from inside a region */
    uint32_t side_interp;       /* of those: accesses outside fast memory or onto the code */
    /* XLAT_LOG: CPU bursts timed by the adapter (time in us, instructions),
     * without bursts that ran nested bursts (native INT handlers); and the
     * device service after each burst (pc_service_impl). Histograms by
     * duration: < 50, 100, 200, 400, 800, 1600, 3200 us, longer. */
    uint32_t xrip_bursts, xlat_bursts;   /* XRIP only / with translated code */
    uint64_t xrip_us, xrip_insns, xlat_us, xlat_insns;
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
#define XLAT_SLICE_CHECK 1024

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

/* Drop every translation (e.g. after a program terminates). */
void xlat_flush(void);

/* The memory block was taken away (machine reset clears the PSRAM span
 * table): forget it; the next hot spot allocates a new one. */
void xlat_release(void);

/* XLAT_LOG builds: append the collected regions and counters to
 * SD_DATA_DIR/xlat.log. Only at a safe point (a DOS program has ended). */
void xlat_log_write(uint16_t psp, const char *name);

/* Provided by the CPU adapter (and by tools/xlat_test): execute the one
 * instruction at rt->cs:rt->ip with the interpreter, rt->gpr and the lazy
 * flags in rt being the current state, and update rt (IP, flags, FLAGS image,
 * segment bases, IRQ pointer). flags bit 0: the instruction may write
 * memory, recheck the region's code; bit 2: statistics only (a side access).
 * Returns 1 if the region may go on; 2 if CS changed (rt->cs and
 * rt->seg_base[1] updated) and the dispatcher may continue there; 0 if XRIP
 * must take over (mode, TF, IRQ, HLT, stack low, or the region's code changed). */
uint32_t xlat_interp_one(xlat_rt_t *rt, uint32_t flags);

/* For xlat_interp_one(): the x86 bytes of the running region are unchanged
 * (if not, the region is dropped). */
bool xlat_region_still_valid(const xlat_rt_t *rt);

/* Native stack left (bytes) and the margin needed before translating or
 * interpreting from translated code. */
uint32_t xlat_stack_free(void);
#define XLAT_STACK_NEEDED 1024u

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
uint32_t xlat_rt_cond(const xlat_rt_t *rt, uint32_t cc);

#ifdef __cplusplus
}
#endif

#endif
