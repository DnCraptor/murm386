/*
 * murm386 xlat: resident translation cache and burst-boundary dispatcher.
 *
 * Everything lives in one block allocated on first use: tables, the
 * translator's working context and the code arena. On the device the block is
 * a native PSRAM span (the pool ELF applications run from), not heap: the
 * SRAM heap is small and pico malloc() panics instead of returning NULL. If no
 * span is available the translator stays off.
 *
 * The arena after the trampoline is a ring: each region takes one chunk (its
 * code, then the copies of its x86 bytes), placed at the ring head. A
 * translation that does not fit in the free space at the head reports the
 * size it needs; just the chunks in that many bytes ahead of the head are
 * evicted (from the ring start when the arena ends sooner), the oldest
 * translations first. Evicting a region drops its entries and rebuilds the entry and code maps
 * from the regions left. Only xlat_flush() drops everything.
 *
 * Hot spots are found by sampling: xlat_run() is called at the start of every
 * XRIP burst, and an entry point seen there XLAT_HOT_THRESHOLD times in a row
 * (same hash slot) is translated. A region that ran to its budget stops at a
 * back edge, i.e. at a region entry, so the next burst re-enters it at once.
 */
#include <string.h>

#include "xlat_internal.h"
#include <limits.h>

xlat_stats_t xlat_stats;

typedef struct {
    uint32_t cs_base;
    uint32_t chunk_lo, chunk_hi;   /* arena bytes held: code, then data_off.. */
    uint32_t data_off;
    uint32_t lo, span;
    uint16_t cs;
    uint16_t n_runs;
    uint16_t ip;           /* the entry point it was built from */
    uint8_t held;          /* the slot holds a chunk (live, cold or invalidated) */
    uint8_t live;
    uint8_t cold;          /* dropped: too little runs per entry (see xlat_run) */
    uint8_t fn;            /* a region without a loop (trace): the fewest native
                              instructions an entry runs (charged on entry) */
    uint32_t entries;
    uint32_t insns;        /* instructions charged at its back edges */
    uint32_t us;           /* time inside it (rt->clock), entries included */
    uint32_t sides;        /* instructions sent to XRIP for memory outside fast memory */
    uint32_t exits[XLAT_EXIT_COUNT];
    uint32_t epoch;        /* st->epoch when its bytes were last compared */
    uint32_t mem_size;     /* rt->mem_size it was built for (reads compare with it) */
    uint8_t trace_min;     /* xlat_translate's trace_min it was built with */
    uint8_t n_extra;       /* entry points added (add_entry) */
    uint16_t extra[XLAT_EXTRA_MAX];
} region_t;

#define XLAT_PAGES 4096u   /* XLAT_LOG: xlat_prof_page() pages (256 bytes) of the first MiB */

typedef struct {
    uint32_t key;          /* cs << 16 | ip */
    uint32_t code_off;     /* arena offset of the entry label */
    uint16_t region;
    uint8_t used;          /* E_* */
} entry_t;

/* entry_t.used: E_FREE ends a probe chain, E_DEAD (an evicted region's
 * entry) does not; E_MARK: set in xlat_entry_map. */
enum { E_FREE = 0, E_DEAD = 1, E_LIVE = 2, E_MARK = 4 };

typedef struct {
    uint32_t key;
    uint8_t count;         /* samples in a row; after a reject: samples since */
    uint8_t reject;        /* HOT_* */
    uint8_t why;           /* XLAT_RJ_* of the reject */
} hot_t;

/* HOT_RETRY allows another request after XLAT_REJECT_RETRY more samples.
 * HOT_FINAL leaves the entry to XRIP. */
enum { HOT_NONE = 0, HOT_RETRY = 1, HOT_FINAL = 2 };

/* In order of increasing priority, FIFO within each priority. */
enum { COMPILE_BURST, COMPILE_RETRY, COMPILE_EXIT, COMPILE_CALL };

typedef struct {
    uint32_t key;
    uint8_t kind, why;
} compile_request_t;



/* XLAT_LOG: hot entry points that got no region, the most often seen kept. */
#define XLAT_REJ_SLOTS 16
#define XLAT_REJ_BYTES 32
typedef struct {
    uint32_t key;
    uint32_t hits;          /* bursts that started there afterwards */
    uint8_t why, n, n_interp, used;
    uint8_t b[XLAT_REJ_BYTES];
} rej_t;

/* PSRAM directory: one bit per held region for each guest 4-KiB page.
 * The span is conservative; the source runs decide exact overlap. */
#define REGION_PAGE_SHIFT 12u
#define REGION_PAGE_COUNT (0x110000u >> REGION_PAGE_SHIFT)
#define REGION_WORDS ((XLAT_MAX_REGIONS + 31u) / 32u)

typedef struct {
    xlat_arena_t arena;
    region_t region[XLAT_MAX_REGIONS];
    uint32_t page_regions[REGION_PAGE_COUNT][REGION_WORDS];
    uint32_t last[XLAT_MAX_REGIONS];  /* st->seq at each region's last entry (take_slot);
                                         written by the dispatcher too (rt->last) */
    xlat_link_t link[XLAT_LINK_ENTRIES];  /* entries the dispatcher may make (rt->link) */
    uint32_t seq;           /* xlat_run() region entries so far */
    uint32_t ring_lo;       /* first byte after the trampoline */
    uint32_t head;          /* where the next chunk goes */
    uint32_t free_end;      /* cached window_end(), zero means recompute */
    uint32_t held_bytes;    /* sum of all held chunks, including invalidated */
    uint32_t free_slots[REGION_WORDS];
    uint32_t epoch;         /* changes whenever x86 code may have been written */
    uint8_t *mem;
    uint32_t mem_size;      /* guest memory of the last xlat_run() */
    entry_t entry[XLAT_ENTRY_SLOTS];
    hot_t hot[XLAT_HOT_SLOTS];       /* burst-start samples */
    hot_t exit_hot[XLAT_HOT_SLOTS];  /* untranslated region exit targets */
    xlat_region_out_t out;  /* translator output, kept here to spare the stack */
    void *ctx;              /* translator working memory */
    compile_request_t queue[XLAT_COMPILE_QUEUE];
    unsigned queued;
    uint8_t compile_blocked;       /* priorities denied since the last dequeue */
    int32_t compile_credit;        /* microseconds of guest work, debt if negative */
    uint32_t compile_burst;        /* at most one compiler job per CPU burst */
    /* One unpublished region owns the free arena window. Lives in PSRAM. */
    struct {
        xlat_rt_t rt;
        compile_request_t request;
        uint32_t need, old_lo;
        uint16_t ip, extra[XLAT_EXTRA_MAX];
        int trace_min, n_extra, attempt, old_slot;
        bool pending, started;
#if XLAT_STACKFUL
        bool result, full;  /* written by the PSP worker, never caller MSP */
#endif
#if XLAT_LOG
        uint32_t quanta, us;
#endif
    } job;
#if XLAT_LOG
    /* Per-length counts for ALL successful publications, not just T dumps.
     * state_t is allocated in PSRAM; no new resident SRAM tables. */
    struct {
        uint32_t built, first, quanta, us, arm, final_us;
    } compile_len[XLAT_MAX_INSNS + 1];
    char *log;              /* text kept until the next safe point (xlat_log_write) */
    uint32_t log_len;
    uint32_t log_lost;      /* bytes that did not fit */
    uint32_t log_skipped;   /* region dumps left out (XLAT_LOG_RESERVE) */
    rej_t rej[XLAT_REJ_SLOTS];
    uint32_t rej_why[XLAT_RJ_COUNT];
    uint32_t page[XLAT_PAGES + 1];  /* xlat_prof_page: 256-byte pages of 1 MiB, then above */
#endif
} state_t;

#ifndef XLAT_LOG
#define XLAT_LOG 0
#endif
#define XLAT_LOG_SIZE (XLAT_LOG ? 32768u : 0u)
/* The region dumps (T lines, C lines of evicted regions) stop this far
 * from the end: the summary at the program's exit always fits. */
#define XLAT_LOG_RESERVE (8192u + XLAT_MAX_INSNS * 112u)

#define ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

#if XLAT_ARENA_SIZE > 0x800000
#error "XLAT_ARENA_SIZE: the B.W / BL from the arena end to the trampoline reach 16 MiB"
#endif

static state_t *st;
static bool disabled;
/* Non-NULL only while xlat_run owns this runtime state. */
static xlat_rt_t *gap_rt;
static bool flush_pending;
static void gap_reset(void);
/* Secondary negative cache. The existing ARM fast path still uses its
 * single hot gap; a miss here need not walk any PSRAM region metadata. */
#define GAP_RECENT 8u
static struct { uint32_t lo, hi; } gap_recent[GAP_RECENT];
static unsigned gap_next;

uint8_t xlat_entry_map[XLAT_MAP_BITS / 8];
uint8_t xlat_code_map[XLAT_CODE_MAP_BYTES];
bool xlat_dirty = true;
uint32_t xlat_write_lo, xlat_write_hi;
bool xlat_clean_resume;
uint32_t xlat_call_key[XLAT_CALL_SLOTS];
uint8_t xlat_call_count[XLAT_CALL_SLOTS];
bool xlat_call_queue_blocked;

#if defined(XLAT_HOST_TEST)
void *xlat_test_calloc(size_t size);   /* executable, zeroed */
#define XLAT_ALLOC(size) xlat_test_calloc(size)
static uint32_t stack_free(void)
{
    return 0xffffffffu;
}
#else
#include "core0_stack.h"
#include "board_config.h"
#include "../psram_alloc.h"
#include "../psram_layout.h"

/* Span owner: not a segment any DOS process can have. */
#define XLAT_SPAN_OWNER 0xffffu

static void *XLAT_ALLOC(size_t size)
{
    fdos_psram_span *span =
        fdos_native_span_reserve(XLAT_SPAN_OWNER, (uint32_t)((size + 1023u) & ~(size_t)1023u));
    if (!span)
        return NULL;
    return (void *)((uintptr_t)PSRAM_BASE_ADDR + FDOS_XMS_EMB_BASE_PHYS + span->base);
}

/* Native stack left below SP, as native_stack_free() in task.c: the core-0
 * stack bounds, or MSPLIM where native DOS/WINN switched stacks. */
static uint32_t stack_free(void)
{
    uint32_t sp, floor = (uint32_t)core0_stack_floor_runtime;
    __asm volatile("mov %0, sp" : "=r"(sp));
    if (sp < floor || sp > core0_stack_top_runtime) {
        __asm volatile("mrs %0, msplim" : "=r"(floor));
        if (!floor)
            return 0;
    }
    return sp > floor ? sp - floor : 0;
}
#endif

/* Translating needs about 0.5 KiB of stack beyond xlat_run(); keep the same
 * margin the DOS EXEC guard keeps for whatever runs after us
 * (XLAT_STACK_NEEDED, xlat.h). */
uint32_t xlat_stack_free(void)
{
    return stack_free();
}

/* rt->cur when no region runs */
#define NO_REGION UINT32_MAX

void xlat_sync_code(void *start, uint32_t len)
{
#if defined(XLAT_HOST_TEST)
    __builtin___clear_cache((char *)start, (char *)start + len);
#else
    /* Cortex-M33: no instruction cache in the core; make the stores visible
     * and drop prefetched instructions. */
    (void)start;
    (void)len;
    __asm volatile("dsb 0xf\n\tisb 0xf" ::: "memory");
#endif
}

/* lazy_cf is CF when the flags are resolved (xlat.h) */
static inline void cf_mirror(xlat_rt_t *rt)
{
    if (!rt->lazy_op)
        rt->lazy_cf = (uint8_t)(rt->eflags & 1u);
}

static uint32_t hash32(uint32_t key)
{
    return key * 0x9e3779b1u;
}

#if XLAT_LOG
#include <stdarg.h>
#include <stdio.h>

static void log_printf(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 1)
        n = (int)sizeof(line) - 1;
    if (st->log_len + (uint32_t)n > XLAT_LOG_SIZE) {
        st->log_lost += (uint32_t)n;
        return;
    }
    memcpy(st->log + st->log_len, line, (size_t)n);
    st->log_len += (uint32_t)n;
}

/* x86 bytes of a region, as the translator saw them: replayable offline. */
/* Room for one more region dump, keeping XLAT_LOG_RESERVE. */
static bool log_dump_room(void)
{
    if (st->log_len + XLAT_LOG_RESERVE <= XLAT_LOG_SIZE)
        return true;
    st->log_skipped++;
    return false;
}

static void log_region_code(const region_t *r)
{
    if (!log_dump_room())
        return;
    log_printf("T %04X:%04X lo=%05lX span=%lu runs=%u n=%u quanta=%lu us=%lu arm=%lu cut=%u next=%u final=%lu cont=%u\r\n",
               r->cs, r->ip, (unsigned long)r->lo, (unsigned long)r->span, r->n_runs,
               (unsigned)st->out.n_insns, (unsigned long)st->job.quanta,
               (unsigned long)st->job.us, (unsigned long)st->out.code_len,
               0u, 0u, (unsigned long)st->out.final_us, 0u);
    const uint8_t *d = st->arena.base + r->data_off;
    for (unsigned k = 0; k < r->n_runs; ++k) {
        uint32_t linear;
        memcpy(&linear, d, 4);
        const uint32_t len = d[4] | (uint32_t)d[5] << 8;
        const uint8_t *b = d + 8;
        for (uint32_t i = 0; i < len; i += 16) {
            /* Fixed-width replay line; avoid vsnprintf on every 16 bytes. */
            static const char digits[] = "0123456789ABCDEF";
            char line[15 + 16 * 3 + 2];
            const uint32_t n = len - i < 16 ? len - i : 16;
            const uint32_t ip = linear - r->cs_base + i;
            line[0] = line[1] = ' ';
            line[6] = ':';
            for (unsigned j = 0; j < 4; ++j)
                line[2 + j] = digits[(r->cs >> (12u - 4u * j)) & 15u];
            unsigned pos = 7;
            /* %04X is a minimum width, not truncation at 16 bits. */
            unsigned shift = 28;
            while (shift > 12u && !(ip >> shift))
                shift -= 4u;
            for (;;) {
                line[pos++] = digits[(ip >> shift) & 15u];
                if (!shift)
                    break;
                shift -= 4u;
            }
            for (uint32_t j = 0; j < n; ++j) {
                const uint8_t v = b[i + j];
                line[pos++] = ' ';
                line[pos++] = digits[v >> 4];
                line[pos++] = digits[v & 15u];
            }
            line[pos++] = '\r';
            line[pos++] = '\n';
            const uint32_t bytes = pos;
            if (st->log_len + bytes > XLAT_LOG_SIZE) {
                st->log_lost += bytes;
            } else {
                memcpy(st->log + st->log_len, line, bytes);
                st->log_len += bytes;
            }
        }
        d += 8 + ((len + 3u) & ~3u);
    }
}

static void log_region_count(const region_t *r)
{
    {
        log_printf("C %04X:%04X entries=%lu exit branch/budget/side/stop=%lu/%lu/%lu/%lu ins=%lu us=%lu sides=%lu%s\r\n",
                   r->cs, r->ip, (unsigned long)r->entries, (unsigned long)r->exits[0],
                   (unsigned long)r->exits[1], (unsigned long)r->exits[2],
                   (unsigned long)r->exits[3], (unsigned long)r->insns, (unsigned long)r->us,
                   (unsigned long)r->sides,
                   r->live ? (r->fn ? " fn" : "") : r->cold ? " (cold)" : " (invalidated)");
    }
}

/* The XLAT_LOG_TOP regions with the most time in them, while the log has
 * room (the summary is written before them). */
#define XLAT_LOG_TOP 40
static void log_region_counts(void)
{
    static uint32_t done[(XLAT_MAX_REGIONS + 31) / 32];   /* bits: SRAM is short */
    memset(done, 0, sizeof(done));
    int held = 0, shown = 0;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k)
        held += st->region[k].held;
    for (; shown < XLAT_LOG_TOP && st->log_len + 256u <= XLAT_LOG_SIZE; ++shown) {
        int best = -1;
        for (int k = 0; k < XLAT_MAX_REGIONS; ++k)
            if (st->region[k].held && !(done[k >> 5] >> (k & 31) & 1u) &&
                (best < 0 || st->region[k].us > st->region[best].us))
                best = k;
        if (best < 0)
            break;
        done[best >> 5] |= 1u << (best & 31);
        log_region_count(&st->region[best]);
    }
    if (held > shown)
        log_printf("C ... %d more regions\r\n", held - shown);
}

static void note_reject(const xlat_rt_t *rt, uint32_t key, int why)
{
    st->rej_why[why]++;
    rej_t *slot = &st->rej[0];
    for (int k = 0; k < XLAT_REJ_SLOTS; ++k) {
        rej_t *q = &st->rej[k];
        if (q->used && q->key == key) {
            slot = q;
            break;
        }
        if (!q->used || q->hits < slot->hits)
            slot = q;
        if (!q->used)
            break;
    }
    if (slot->used && slot->key == key) {
        slot->why = (uint8_t)why;
        return;
    }
    slot->key = key;
    slot->hits = 0;
    slot->used = 1;
    slot->why = (uint8_t)why;
    slot->n = st->out.n_insns;
    slot->n_interp = st->out.n_interp;
    if (why == XLAT_RJ_COVERED)
        slot->n = slot->n_interp = 0;
    const uint32_t linear = rt->seg_base[1] + (rt->ip & 0xffffu);
    for (uint32_t i = 0; i < XLAT_REJ_BYTES; ++i)
        slot->b[i] = xlat_code_direct(linear + i, 1) ? rt->mem[linear + i] : 0;
}

/* A burst started at a rejected entry point again. */
static void note_reject_hit(uint32_t key)
{
    for (int k = 0; k < XLAT_REJ_SLOTS; ++k)
        if (st->rej[k].used && st->rej[k].key == key) {
            st->rej[k].hits++;
            return;
        }
}

static void log_rejects(void)
{
    static const char *const names[XLAT_RJ_COUNT] = {
        "none", "covered", "start", "overlap", "interp", "noloop", "emit", "full"};
    log_printf("J covered=%lu start=%lu overlap=%lu interp=%lu noloop=%lu emit=%lu full=%lu\r\n",
               (unsigned long)st->rej_why[1], (unsigned long)st->rej_why[2],
               (unsigned long)st->rej_why[3], (unsigned long)st->rej_why[4],
               (unsigned long)st->rej_why[5], (unsigned long)st->rej_why[6],
               (unsigned long)st->rej_why[7]);
    for (int k = 0; k < XLAT_REJ_SLOTS; ++k) {
        const rej_t *q = &st->rej[k];
        if (!q->used)
            continue;
        char hex[XLAT_REJ_BYTES * 3 + 1];
        for (int j = 0; j < XLAT_REJ_BYTES; ++j) {
            static const char digits[] = "0123456789ABCDEF";
            hex[j * 3] = ' ';
            hex[j * 3 + 1] = digits[q->b[j] >> 4];
            hex[j * 3 + 2] = digits[q->b[j] & 15];
        }
        hex[XLAT_REJ_BYTES * 3] = 0;
        log_printf("R %04X:%04X why=%s n=%u interp=%u hits=%lu%s\r\n", (unsigned)(q->key >> 16),
                   (unsigned)(q->key & 0xffffu), names[q->why < XLAT_RJ_COUNT ? q->why : 0], q->n,
                   q->n_interp, (unsigned long)q->hits, hex);
    }
    memset(st->rej, 0, sizeof(st->rej));
    memset(st->rej_why, 0, sizeof(st->rej_why));
}

/* Where the time goes outside translated code (xlat_prof_note, xlat.h): the
 * heaviest CS:IP of each kind, counted as in the space-saving algorithm (a
 * new key takes the smallest slot and its count: counts are upper bounds). */
#define XLAT_PROF_SLOTS 24
typedef struct {
    uint32_t key, linear, count;
    uint32_t err;          /* the count it took over: the true count is at least count - err */
    uint8_t b[8];
} prof_t;
static prof_t prof[XLAT_PROF_KINDS][XLAT_PROF_SLOTS];
static uint32_t prof_total[XLAT_PROF_KINDS];

void XLAT_HOT_FN(xlat_prof_note)(unsigned kind, uint32_t key, uint32_t linear, const uint8_t *mem,
                                 uint32_t mem_size, uint32_t weight)
{
    prof_t *const t = prof[kind];
    prof_t *min = &t[0];
    prof_total[kind] += weight;
    for (int k = 0; k < XLAT_PROF_SLOTS; ++k) {
        if (t[k].key == key && t[k].count) {
            t[k].count += weight;
            return;
        }
        if (t[k].count < min->count)
            min = &t[k];
    }
    min->key = key;
    min->linear = linear;
    min->err = min->count;
    min->count += weight;
    for (uint32_t i = 0; i < 8; ++i)
        min->b[i] = xlat_code_direct(linear + i, 1) ? mem[linear + i] : 0;
    (void)mem_size;
}

void XLAT_HOT_FN(xlat_prof_page)(uint32_t linear, uint32_t weight)
{
    if (st)
        st->page[linear < 0x100000u ? linear >> 8 : XLAT_PAGES] += weight;
}

/* The XLAT_LOG_TOP_PAGES pages XRIP ran most in. */
#define XLAT_LOG_TOP_PAGES 20
static void log_pages(void)
{
    uint64_t total = 0;
    for (uint32_t k = 0; k <= XLAT_PAGES; ++k)
        total += st->page[k];
    log_printf("X xrip pages total=%lu\r\n", (unsigned long)total);
    for (int n = 0; n < XLAT_LOG_TOP_PAGES; ++n) {
        uint32_t best = 0, best_n = 0;
        for (uint32_t k = 0; k <= XLAT_PAGES; ++k)
            if (st->page[k] > best_n) {
                best_n = st->page[k];
                best = k;
            }
        if (!best_n)
            break;
        if (best == XLAT_PAGES)
            log_printf("X >=100000 n=%lu\r\n", (unsigned long)best_n);
        else
            log_printf("X %05lX-%05lX n=%lu %lu%%\r\n", (unsigned long)(best << 8),
                       (unsigned long)((best << 8) | 0xffu), (unsigned long)best_n,
                       (unsigned long)(total ? (uint64_t)best_n * 100u / total : 0));
        st->page[best] = 0;
    }
    memset(st->page, 0, sizeof(st->page));
}

static void log_prof(void)
{
    static const char *const kinds[XLAT_PROF_KINDS] = {"xrip", "interp", "side"};
    for (unsigned kind = 0; kind < XLAT_PROF_KINDS; ++kind) {
        prof_t *const t = prof[kind];
        log_printf("P %s total=%lu\r\n", kinds[kind], (unsigned long)prof_total[kind]);
        for (int n = 0; n < XLAT_PROF_SLOTS; ++n) {
            prof_t *best = NULL;
            for (int k = 0; k < XLAT_PROF_SLOTS; ++k)
                if (t[k].count && (!best || t[k].count > best->count))
                    best = &t[k];
            if (!best)
                break;
            const uint32_t a = best->linear;
            const char *const area = a < 0xa0000u ? "" : a < 0xc0000u ? " vram" : a < 0x100000u
                                     ? " rom/umb" : " hma/xms";
            log_printf("P %s %04X:%04X %05lX n=%lu err=%lu%s %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                       kinds[kind], (unsigned)(best->key >> 16), (unsigned)(best->key & 0xffffu),
                       (unsigned long)a, (unsigned long)best->count, (unsigned long)best->err, area,
                       best->b[0], best->b[1],
                       best->b[2], best->b[3], best->b[4], best->b[5], best->b[6], best->b[7]);
            best->count = 0;
        }
    }
    memset(prof, 0, sizeof(prof));
    memset(prof_total, 0, sizeof(prof_total));
}
#else
#define log_region_code(r) ((void)0)
#define log_region_counts() ((void)0)
#define log_region_count(r) ((void)0)
#define log_dump_room() false
#define note_reject(rt, key, why) ((void)0)
#define note_reject_hit(key) ((void)0)
#define log_prof() ((void)0)
#define log_pages() ((void)0)
#endif

static void clear_compile_blocked(void)
{
    st->compile_blocked = 0;
    xlat_call_queue_blocked = false;
}

static void reset_tables(void)
{
    xlat_preempt_discard();
    gap_reset();
    st->job.pending = false;
    st->queued = 0;
    clear_compile_blocked();
    st->compile_credit = 0;
    st->compile_burst = 0;
    log_region_counts();    /* the regions are about to go */
    memset(st->region, 0, sizeof(st->region));
    memset(st->page_regions, 0, sizeof(st->page_regions));
    memset(st->last, 0, sizeof(st->last));
    for (unsigned k = 0; k < XLAT_LINK_ENTRIES; ++k)
        st->link[k].key = UINT32_MAX;
    memset(st->entry, 0, sizeof(st->entry));
    memset(st->exit_hot, 0, sizeof(st->exit_hot));
    memset(st->hot, 0, sizeof(st->hot));
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    memset(xlat_code_map, 0, sizeof(xlat_code_map));
    xlat_emit_trampoline(&st->arena);
    xlat_sync_code(st->arena.base, st->arena.code_top);
    st->ring_lo = st->head = (st->arena.code_top + 7u) & ~7u;
    st->free_end = st->arena.size;
    st->held_bytes = 0;
    memset(st->free_slots, 0xff, sizeof(st->free_slots));
    st->free_slots[REGION_WORDS - 1] = UINT32_MAX >> ((32u - (XLAT_MAX_REGIONS & 31u)) & 31u);
    xlat_stats.code_bytes = st->ring_lo;
}

static __attribute__((noinline)) bool ready(void)
{
    if (st)
        return true;
    if (disabled)
        return false;
    const size_t ctx_off = ALIGN8(sizeof(state_t));
#if XLAT_ARENA_SRAM
    /* Experiment: code in SRAM, out of the XIP cache shared with guest RAM
     * and core 1's flash code. Tables and context stay in the PSRAM span. */
    static uint32_t sram_arena[XLAT_ARENA_SIZE / 4];
    const size_t arena_off = ALIGN8(ctx_off + xlat_translate_ctx_size());
    const size_t log_off = arena_off;
    uint8_t *block = (uint8_t *)XLAT_ALLOC(log_off + XLAT_LOG_SIZE);
    const uint32_t arena_size = XLAT_ARENA_SIZE;
#else
    /* The arena as configured, else as large as the pool still gives
     * (down to 32 KiB) */
    const size_t arena_off = ALIGN8(ctx_off + xlat_translate_ctx_size());
    uint32_t arena_size = XLAT_ARENA_SIZE;
    uint8_t *block;
    size_t log_off;
    for (;;) {
        log_off = arena_off + arena_size;
        block = (uint8_t *)XLAT_ALLOC(log_off + XLAT_LOG_SIZE);
        if (block || arena_size <= 32768u)
            break;
        arena_size /= 2u;
    }
#endif
    if (!block) {
        disabled = true;
        xlat_stats.alloc_failed = 1;
        return false;
    }
    st = (state_t *)block;
    memset(st, 0, sizeof(*st));
    st->ctx = block + ctx_off;
#if XLAT_ARENA_SRAM
    st->arena.base = (uint8_t *)sram_arena;
#else
    st->arena.base = block + arena_off;
#endif
    st->arena.size = arena_size;
#if XLAT_LOG
    st->log = (char *)block + log_off;
#endif
    reset_tables();
    return true;
}

void xlat_release(void)
{
    xlat_preempt_discard();
    gap_reset();
    flush_pending = false;
    xlat_write_hi = 0;
    xlat_call_queue_blocked = false;
    memset(xlat_call_key, 0, sizeof(xlat_call_key));
    memset(xlat_call_count, 0, sizeof(xlat_call_count));
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    memset(xlat_code_map, 0, sizeof(xlat_code_map));
    st = NULL;
    disabled = false;
}

void xlat_flush(void)
{
    if (!st)
        return;
    if (gap_rt) {
        /* FDOS can switch processes inside a helper called by the
         * trampoline. Invalidate now, but keep code and descriptors
         * until the outstanding native return addresses are gone. */
        flush_pending = true;
        for (int k = 0; k < XLAT_MAX_REGIONS; ++k)
            st->region[k].live = st->region[k].cold = 0;
        for (unsigned k = 0; k < XLAT_LINK_ENTRIES; ++k)
            st->link[k].key = UINT32_MAX;
        memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
        gap_reset();
        return;
    }
    flush_pending = false;
    xlat_write_hi = 0;
    reset_tables();
    memset(xlat_call_key, 0, sizeof(xlat_call_key));
    memset(xlat_call_count, 0, sizeof(xlat_call_count));
    xlat_stats.flushes++;
}

static entry_t *XLAT_HOT_FN(lookup)(uint32_t key)
{
    const uint32_t h = hash32(key) >> (32 - XLAT_ENTRY_BITS);
    for (uint32_t k = 0; k < 8; ++k) {
        entry_t *e = &st->entry[(h + k) & (XLAT_ENTRY_SLOTS - 1)];
        if (e->used == E_FREE)
            return NULL;
        if (e->key == key && e->used != E_DEAD)
            return e;
    }
    return NULL;
}

static bool insert(uint32_t key, uint16_t region, uint32_t code_off, bool mark)
{
    const uint32_t h = hash32(key) >> (32 - XLAT_ENTRY_BITS);
    for (uint32_t k = 0; k < 8; ++k) {
        entry_t *e = &st->entry[(h + k) & (XLAT_ENTRY_SLOTS - 1)];
        if (e->used <= E_DEAD || e->key == key || !st->region[e->region].live) {
            /* an entry of the same key further on is shadowed (lookup takes
             * the first); it comes back if this region is evicted */
            e->key = key;
            e->code_off = code_off;
            e->region = region;
            e->used = (uint8_t)(E_LIVE | (mark ? E_MARK : 0));
            return true;
        }
    }
    return false;
}

static uint32_t XLAT_HOT_FN(load32)(const uint8_t *p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, 4);  /* one unaligned LDR */
    return v;
}

/* The x86 bytes the region was built from are still there. */
static bool XLAT_HOT_FN(region_bytes_valid)(const region_t *r, const xlat_rt_t *rt)
{
#if XLAT_LOG
    ++xlat_stats.revalidations;
#endif
    if (rt->mem_size != r->mem_size)
        return false;
    const uint8_t *d = st->arena.base + r->data_off;
    for (unsigned k = 0; k < r->n_runs; ++k) {
        const uint32_t linear = load32(d);
        const uint32_t len = d[4] | (uint32_t)d[5] << 8;
        const uint8_t *copy = d + 8;
#if XLAT_LOG
        xlat_stats.revalidate_bytes += len;
#endif
        if (!xlat_code_direct(linear, len))
            return false;
        const uint8_t *g = rt->mem + linear;
        uint32_t i = 0;
        for (; i + 4 <= len; i += 4)
            if (load32(g + i) != load32(copy + i))
                return false;
        for (; i < len; ++i)
            if (g[i] != copy[i])
                return false;
        d += 8 + ((len + 3u) & ~3u);
    }
    return true;
}

static bool XLAT_HOT_FN(region_valid)(const region_t *r, const xlat_rt_t *rt)
{
    return rt->seg_base[1] == r->cs_base && region_bytes_valid(r, rt);
}

/* rt->cs:ip is in the x86 code of a live or dropped region (its runs: a
 * region may take code from far apart, e.g. called functions). */
static bool covered(const xlat_rt_t *rt)
{
    const uint32_t linear = rt->seg_base[1] + (rt->ip & 0xffffu);
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (!(r->live || r->cold) || linear - r->lo >= r->span)
            continue;
        const uint8_t *d = st->arena.base + r->data_off;
        for (unsigned j = 0; j < r->n_runs; ++j) {
            uint32_t at;
            memcpy(&at, d, 4);
            const uint32_t len = d[4] | (uint32_t)d[5] << 8;
            if (linear - at < len)
                return true;
            d += 8 + ((len + 3u) & ~3u);
        }
    }
    return false;
}

/* Membership follows the held chunk, including cold regions. Clear a
 * slot before reuse; invalidated chunks are filtered by live/cold. */
static void index_region(unsigned k, bool add)
{
    const region_t *r = &st->region[k];
    const uint8_t *d = st->arena.base + r->data_off;
    const uint32_t bit = 1u << (k & 31u);
    /* Only pages containing source bytes, not holes in the region span. */
    for (unsigned j = 0; j < r->n_runs; ++j) {
        const uint32_t at = load32(d);
        const uint32_t len = d[4] | (uint32_t)d[5] << 8;
        d += 8 + ((len + 3u) & ~3u);
        if (!len || at >= 0x110000u)
            continue;
        uint32_t last = (at + len - 1u) >> REGION_PAGE_SHIFT;
        if (last >= REGION_PAGE_COUNT)
            last = REGION_PAGE_COUNT - 1u;
        for (uint32_t p = at >> REGION_PAGE_SHIFT; p <= last; ++p) {
            uint32_t *w = &st->page_regions[p][k >> 5];
            if (add)
                *w |= bit;
            else
                *w &= ~bit;
        }
    }
}

typedef struct {
    uint32_t first, last, word, bits;
} region_iter_t;

static inline region_iter_t regions_in(uint32_t lo, uint32_t hi)
{
    region_iter_t it = {0, 0, REGION_WORDS, 0};
    if (hi > lo && lo < 0x110000u) {
        it.first = lo >> REGION_PAGE_SHIFT;
        it.last = (hi - 1u) >> REGION_PAGE_SHIFT;
        if (it.last >= REGION_PAGE_COUNT)
            it.last = REGION_PAGE_COUNT - 1u;
        it.word = 0;
    }
    return it;
}

/* Union the page masks a word at a time: visit each candidate once.
 * Empty slots never cause PSRAM descriptor reads. */
static inline int region_next(region_iter_t *it)
{
    while (!it->bits) {
        if (it->word == REGION_WORDS)
            return -1;
        for (uint32_t p = it->first; p <= it->last; ++p)
            it->bits |= st->page_regions[p][it->word];
        ++it->word;
    }
    const unsigned bit = (unsigned)__builtin_ctz(it->bits);
    it->bits &= it->bits - 1u;
    return (int)((it->word - 1u) * 32u + bit);
}

/* The dispatcher no longer enters region k (dropped, cold or evicted). */
static void unlink_region(int k)
{
    for (unsigned j = 0; j < XLAT_LINK_ENTRIES; ++j)
        if (st->link[j].region == k)
            st->link[j].key = UINT32_MAX;   /* no IP is 0xffff with CS 0xffff here */
}

/* Drop region k: its entries go (the chunk is reused when the head comes
 * to it). The maps are rebuilt by the caller (rebuild_maps). */
static void evict_storage(int k)
{
    region_t *r = &st->region[k];
    if (log_dump_room())
        log_region_count(r);
    index_region((unsigned)k, false);
    if (r->held) {
        st->held_bytes -= r->chunk_hi - r->chunk_lo;
        xlat_stats.code_bytes = st->ring_lo + st->held_bytes;
        if (r->chunk_lo == st->free_end)
            st->free_end = 0;
        st->free_slots[(unsigned)k >> 5] |= 1u << ((unsigned)k & 31u);
    }
    memset(r, 0, sizeof(*r));
    xlat_stats.evictions++;
}

static void evict(int k)
{
    evict_storage(k);
    unlink_region(k);
    for (int j = 0; j < XLAT_ENTRY_SLOTS; ++j) {
        entry_t *e = &st->entry[j];
        if (e->used >= E_LIVE && e->region == k)
            e->used = E_DEAD;
    }
    gap_reset();
}

/* xlat_entry_map and xlat_code_map from the regions left. */
static void rebuild_maps(void)
{
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    memset(xlat_code_map, 0, sizeof(xlat_code_map));
    for (int j = 0; j < XLAT_ENTRY_SLOTS; ++j) {
        const entry_t *e = &st->entry[j];
        if ((e->used & (E_LIVE | E_MARK)) == (E_LIVE | E_MARK) &&
            st->region[e->region].live) {
            const uint32_t m =
                xlat_map_index(st->region[e->region].cs_base + (e->key & 0xffffu));
            xlat_entry_map[m >> 3] |= (uint8_t)(1u << (m & 7u));
        }
    }
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (!r->live)
            continue;
        const uint8_t *d = st->arena.base + r->data_off;
        for (unsigned j = 0; j < r->n_runs; ++j) {
            uint32_t linear;
            memcpy(&linear, d, 4);
            const uint32_t len = d[4] | (uint32_t)d[5] << 8;
            /* the blocks a write may reach the run from (up to 16 bytes long) */
            const uint32_t from = linear >= 15u ? linear - 15u : 0u;
            for (uint32_t b = from >> XLAT_CODE_BLOCK_SHIFT;
                 b <= (linear + len - 1u) >> XLAT_CODE_BLOCK_SHIFT && b < XLAT_CODE_MAP_BYTES * 8u;
                 ++b)
                xlat_code_map[b >> 3] |= (uint8_t)(1u << (b & 7u));
            d += 8 + ((len + 3u) & ~3u);
        }
    }
}

/* Compare only the intersection with saved source for a known write.
 * Unrelated bytes and regions retain their existing epoch. */
static bool region_range_changed(const region_t *r, const xlat_rt_t *rt,
                                 uint32_t lo, uint32_t hi)
{
    const uint8_t *d = st->arena.base + r->data_off;
    for (unsigned j = 0; j < r->n_runs; ++j) {
        const uint32_t at = load32(d);
        const uint32_t len = d[4] | (uint32_t)d[5] << 8;
        const uint32_t first = lo > at ? lo : at;
        const uint32_t end = hi < at + len ? hi : at + len;
        if (first < end) {
            if (rt->mem_size != r->mem_size || !xlat_code_direct(first, end - first))
                return true;
            const uint8_t *saved = d + 8 + first - at;
            for (uint32_t a = first; a < end; ++a)
                if (rt->mem[a] != *saved++)
                    return true;
        }
        d += 8 + ((len + 3u) & ~3u);
    }
    return false;
}

/* Only known guest writes reach here. Retain the epoch and every link
 * whose region still matches its source bytes, even across different CS. */
static void validate_local_writes(const xlat_rt_t *rt)
{
    const uint32_t hi = xlat_write_hi;
    if (!hi)
        return;
    const uint32_t lo = xlat_write_lo;
    xlat_write_hi = 0;
    xlat_stats.local_checks++;
    bool changed = false;
    region_iter_t it = regions_in(lo, hi);
    for (int k; (k = region_next(&it)) >= 0;) {
        region_t *r = &st->region[k];
        if (!(r->live || r->cold) || r->lo >= hi || r->lo + r->span <= lo)
            continue;
        xlat_stats.local_candidates++;
        if (!region_range_changed(r, rt, lo, hi))
            continue;
        xlat_stats.invalidations += r->live != 0;
        xlat_stats.local_invalid++;
        r->live = r->cold = 0;
        unlink_region(k);
        changed = true;
    }
    if (changed) {
        gap_reset();
        rebuild_maps();
    }
}

static uint32_t request_linear(uint32_t key)
{
    return ((key >> 16) << 4) + (uint16_t)key;
}

static compile_request_t dequeue_compile(unsigned i)
{
    const compile_request_t request = st->queue[i];
    --st->queued;
    memmove(&st->queue[i], &st->queue[i + 1],
            (st->queued - i) * sizeof(st->queue[0]));
    clear_compile_blocked();
    return request;
}

/* A hot CALL consumes its signal only once a request is accepted. Restore
 * the signal if an unfinished request is cancelled. */
static void rearm_request(compile_request_t request)
{
    if (request.kind == COMPILE_CALL)
        xlat_call_rearm(request_linear(request.key));
}

/* A DOS MCB has been freed or shrunk. Invalidate just the regions whose
 * source runs touch the returned bytes. Do not evict their chunks here:
 * INT 21h may have reached FDOS from inside a running trampoline. Its
 * region descriptors and native return addresses remain valid until exit. */
void xlat_forget_range(uint32_t linear, uint32_t len)
{
    if (!len)
        return;
    const uint32_t end = linear + len;
    for (unsigned k = 0; k < XLAT_CALL_SLOTS; ++k)
        if (xlat_call_key[k] >= linear && xlat_call_key[k] < end)
            xlat_call_count[k] = 0;
    if (!st)
        return;
    if (st->job.pending) {
        xlat_preempt_discard();
        rearm_request(st->job.request);
        st->job.pending = false;
        xlat_stats.compile_cancel++;
    }
    for (unsigned i = st->queued; i-- > 0;) {
        const uint32_t at = request_linear(st->queue[i].key);
        if (at >= linear && at < end)
            dequeue_compile(i);
    }
    for (unsigned k = 0; k < XLAT_HOT_SLOTS; ++k) {
        hot_t *h[2] = {&st->hot[k], &st->exit_hot[k]};
        for (unsigned j = 0; j < 2; ++j) {
            const uint32_t at = request_linear(h[j]->key);
            if (at >= linear && at < end)
                memset(h[j], 0, sizeof(*h[j]));
        }
    }
    bool changed = false;
    region_iter_t it = regions_in(linear, end);
    for (int k; (k = region_next(&it)) >= 0;) {
        region_t *r = &st->region[k];
        if (!(r->live || r->cold) || r->lo >= end || r->lo + r->span <= linear)
            continue;
        const uint8_t *d = st->arena.base + r->data_off;
        for (unsigned j = 0; j < r->n_runs; ++j) {
            const uint32_t at = load32(d);
            const uint32_t n = d[4] | (uint32_t)d[5] << 8;
            if (n && at < end && at + n > linear) {
                xlat_stats.invalidations += r->live != 0;
                r->live = r->cold = 0;
                unlink_region(k);
                changed = true;
                break;
            }
            d += 8 + ((n + 3u) & ~3u);
        }
    }
    if (changed) {
        gap_reset();
        rebuild_maps();
    }
}

/* Evict the regions whose chunks meet [lo, hi). */
static bool evict_range(uint32_t lo, uint32_t hi)
{
    int first = -1;
    bool batch = false;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (r->held && r->chunk_lo < hi && r->chunk_hi > lo) {
            if (first < 0) {
                first = k;
                continue;
            }
            if (!batch) {
                evict_storage(first);
                batch = true;
            }
            evict_storage(k);
        }
    }
    if (first < 0)
        return false;
    if (!batch) {
        evict(first);
    } else {
        /* No guest execution between storage release and table cleanup.
         * Free-slot bits are dense; avoid touching PSRAM region descriptors
         * for every entry, and avoid O(victims * table size) cleanup. */
        for (unsigned j = 0; j < XLAT_LINK_ENTRIES; ++j) {
            xlat_link_t *l = &st->link[j];
            if (l->key != UINT32_MAX &&
                (st->free_slots[l->region >> 5] & (1u << (l->region & 31u))))
                l->key = UINT32_MAX;
        }
        for (int j = 0; j < XLAT_ENTRY_SLOTS; ++j) {
            entry_t *e = &st->entry[j];
            if (e->used >= E_LIVE &&
                (st->free_slots[e->region >> 5] & (1u << (e->region & 31u))))
                e->used = E_DEAD;
        }
        gap_reset();
    }
    return true;
}

/* End of the free space from the head: the next chunk, or the arena end. */
static uint32_t window_end(void)
{
    if (st->free_end)
        return st->free_end;
    uint32_t end = st->arena.size;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (r->held && r->chunk_lo >= st->head && r->chunk_lo < end)
            end = r->chunk_lo;
    }
    return st->free_end = end;
}

/* A free region slot: an empty one, else an invalidated one, else the
 * region entered longest ago (a hot loop translated early stays). */
static int take_slot(bool *evicted)
{
    for (unsigned w = 0; w < REGION_WORDS; ++w) {
        const uint32_t bits = st->free_slots[w];
        if (bits)
            return (int)(w * 32u + (unsigned)__builtin_ctz(bits));
    }
    int best = -1;
    uint32_t best_d = UINT32_MAX;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (!r->held)
            return k;
        const uint32_t age = st->seq - st->last[k];
        const uint32_t d = !r->live && !r->cold ? 0u : UINT32_MAX - age;
        if (best < 0 || d < best_d) {
            best_d = d;
            best = k;
        }
    }
    evict(best);
    xlat_stats.slot_evictions++;
    *evicted = true;
    return best;
}

void xlat_compile_credit(uint32_t guest_us)
{
    if (st) {
        const uint32_t need = (uint32_t)(XLAT_COMPILE_CREDIT_US - st->compile_credit);
        st->compile_credit += (int32_t)(guest_us < need ? guest_us : need);
    }
}

#if XLAT_STACKFUL
/* Only private compilation executes on PSP. The window is reserved and
 * all arguments/results live in the PSRAM job across hardware switches. */
static void translate_worker(void *arg)
{
    state_t *s = arg;
    s->job.result = xlat_translate_step(&s->arena, &s->job.rt, s->job.ip,
                                        s->job.trace_min, s->job.extra, s->job.n_extra,
                                        &s->out, &s->job.full, s->ctx, s->job.attempt != 0);
}
#endif

static int translate_at(const xlat_rt_t *rt, uint16_t ip, int trace_min, const uint16_t *extra,
                        int n_extra)
{
    xlat_region_out_t *out = &st->out;
    xlat_arena_t *const a = &st->arena;
    bool full, evicted = false;
    for (; st->job.attempt < 3;) {
        if (xlat_compile_irq(rt)) {
#if !XLAT_STACKFUL
            out->reject = XLAT_RJ_DEFER;
#endif
            return -1;
        }
        if (!st->job.started) {
            if (st->job.attempt) {
                const uint32_t need = (st->job.need + 7u) & ~7u;
                if (need > a->size - st->ring_lo) {
                    out->reject = XLAT_RJ_FULL;
                    break;
                }
                if (st->head + need > a->size) {
                    st->head = st->ring_lo;
                    st->free_end = 0;
                }
                if (evict_range(st->head, st->head + need))
                    rebuild_maps();  /* guest can run between slices */
            }
            a->code_top = st->head;
            a->data_bottom = window_end();
        }
#if XLAT_STACKFUL
        st->job.started = true;
        if (!xlat_preempt_run(translate_worker, st, rt->intr, rt->clock))
            return -1;  /* IRQ: preserve the exact ARM context and its output */
        full = st->job.full;
        if (st->job.result) {
            /* No guest execution between validation and publication. PSP
             * may have been interrupted even after its last source read. */
            const uint32_t t = rt->wall_deadline ? *rt->clock : 0;
            if (xlat_translate_validate(rt, st->ctx)) {
                if (rt->wall_deadline)
                    out->final_us += *rt->clock - t;
                goto built;
            }
            a->code_top = st->head;
            out->reject = XLAT_RJ_CHANGED;
        }
#else
        const bool resume = st->job.started || st->job.attempt != 0;
        st->job.started = true;
        if (xlat_translate_step(a, rt, ip, trace_min, extra, n_extra, out, &full,
                                st->ctx, resume))
            goto built;
        if (out->reject == XLAT_RJ_DEFER)
            return -1;
#endif
        if (!full)
            break;
        const uint32_t had = a->data_bottom - st->head;
        st->job.need = out->need > had ? out->need : had + 64u;
        st->job.attempt++;
        st->job.started = false;
        xlat_stats.ring_retries++;
    }
    st->job.pending = false;
    if (out->reject == XLAT_RJ_CHANGED) {
        xlat_stats.compile_cancel++;
        out->reject = XLAT_RJ_DEFER; /* not an unsupported region */
    }
    return -1;

built:;
    const uint32_t publish_t0 = rt->wall_deadline ? *rt->clock : 0;
    st->job.pending = false;
    const int idx = take_slot(&evicted);   /* not in the new chunk: it was free */
    region_t *r = &st->region[idx];
    r->cs = rt->cs;
    r->ip = ip;
    r->cs_base = rt->seg_base[1];
    r->mem_size = rt->mem_size;
    r->trace_min = (uint8_t)trace_min;
    r->n_extra = (uint8_t)n_extra;
    for (int k = 0; k < n_extra; ++k)
        r->extra[k] = extra[k];
    r->lo = out->lo;
    r->span = out->span;
    r->n_runs = out->n_runs;
    r->fn = out->fn;
    st->last[idx] = st->seq;    /* new: as if entered now */
    xlat_stats.fn_regions += out->fn != 0;
    /* the copies follow the code (xlat_translate keeps code_top + data
     * below data_bottom) */
    r->chunk_lo = out->code_off;
    r->data_off = a->code_top;
    r->chunk_hi = r->data_off + out->data_size;
    st->head = (r->chunk_hi + 7u) & ~7u;
    r->held = 1;
    st->free_slots[(unsigned)idx >> 5] &= ~(1u << ((unsigned)idx & 31u));
    st->held_bytes += r->chunk_hi - r->chunk_lo;
    uint8_t *d = st->arena.base + r->data_off;
    for (unsigned k = 0; k < out->n_runs; ++k) {
        const uint32_t linear = r->cs_base + out->runs[k].ip;
        const uint32_t len = out->runs[k].len;
        __builtin_memcpy(d, &linear, 4);
        d[4] = (uint8_t)len;
        d[5] = (uint8_t)(len >> 8);
        d[6] = d[7] = 0;
        memcpy(d + 8, rt->mem + linear, len);
        /* the blocks a write may reach the run from (up to 16 bytes long) */
        const uint32_t from = linear >= 15u ? linear - 15u : 0u;
        for (uint32_t b = from >> XLAT_CODE_BLOCK_SHIFT;
             b <= (linear + len - 1u) >> XLAT_CODE_BLOCK_SHIFT && b < XLAT_CODE_MAP_BYTES * 8u; ++b)
            xlat_code_map[b >> 3] |= (uint8_t)(1u << (b & 7u));
        d += 8 + ((len + 3u) & ~3u);
    }
    r->live = 1;
    index_region((unsigned)idx, true);
    r->epoch = st->epoch;
    gap_reset();           /* its bytes may be in a cached gap */
    if (evicted)
        rebuild_maps();    /* before the new marks: they are set below */
    for (unsigned k = 0; k < out->n_entries; ++k) {
        insert((uint32_t)rt->cs << 16 | out->entries[k].ip, (uint16_t)idx,
               out->entries[k].code_off, out->entries[k].mark);
        if (out->entries[k].mark) {
            const uint32_t m = xlat_map_index(r->cs_base + out->entries[k].ip);
            xlat_entry_map[m >> 3] |= (uint8_t)(1u << (m & 7u));
        }
    }
    xlat_sync_code(st->arena.base + out->code_off, out->code_len);
    xlat_stats.translations++;
    xlat_stats.compiled_code_bytes += out->code_len;
    xlat_stats.compiled_source_bytes += out->data_size;
    xlat_stats.code_bytes = st->ring_lo + st->held_bytes;
    if (rt->wall_deadline)
        out->final_us += *rt->clock - publish_t0;
    return idx;
}

/* The live region of this CS whose x86 code holds rt->cs:ip, or -1. */
static int covering(const xlat_rt_t *rt)
{
    const uint32_t linear = rt->seg_base[1] + (rt->ip & 0xffffu);
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (!r->live || r->cs != rt->cs || r->cs_base != rt->seg_base[1] ||
            linear - r->lo >= r->span)
            continue;
        const uint8_t *d = st->arena.base + r->data_off;
        for (unsigned j = 0; j < r->n_runs; ++j) {
            uint32_t at;
            memcpy(&at, d, 4);
            const uint32_t len = d[4] | (uint32_t)d[5] << 8;
            if (linear - at < len)
                return k;
            d += 8 + ((len + 3u) & ~3u);
        }
    }
    return -1;
}

static unsigned hot_slot(uint32_t key)
{
    return hash32(key) >> (32 - 6);
}

static bool queue_compile(uint32_t key, uint8_t kind, uint8_t why)
{
    const unsigned priority = 1u << kind;
    if (st->compile_blocked & priority)
        return false;
    if (st->job.pending && st->job.request.key == key && st->job.request.kind >= kind)
        return true;

    unsigned replace = XLAT_COMPILE_QUEUE;
    unsigned upgrade = XLAT_COMPILE_QUEUE;
    for (unsigned i = 0; i < st->queued; ++i) {
        const compile_request_t *q = &st->queue[i];
        if (q->key == key) {
            if (q->kind >= kind)
                return true;
            /* Keep only the strongest queued request for this entry. */
            upgrade = i;
        }
        if (q->kind < kind && (replace == XLAT_COMPILE_QUEUE ||
                               q->kind <= st->queue[replace].kind))
            replace = i;
    }
    if (upgrade != XLAT_COMPILE_QUEUE) {
        dequeue_compile(upgrade);
    } else if (st->queued == XLAT_COMPILE_QUEUE) {
        xlat_stats.compile_queue_drops++;
        if (replace == XLAT_COMPILE_QUEUE) {
            st->compile_blocked |= priority;
            if (kind == COMPILE_CALL)
                xlat_call_queue_blocked = true;
            return false;
        }
        dequeue_compile(replace);
    }
    st->queue[st->queued++] = (compile_request_t){key, kind, why};
    return true;
}

/* Samples can enqueue while the compiler is occupied, out of time, or IF=0.
 * Coverage checks and all work on the arena belong to the compiler. */
static __attribute__((noinline)) void sample(uint32_t key, bool exit)
{
    if (exit && !XLAT_TRACES)
        return;
    hot_t *h = &(exit ? st->exit_hot : st->hot)[hot_slot(key)];
    if (h->key != key) {
        *h = (hot_t){.key = key, .count = 1};
        return;
    }
    if (h->reject && !exit)
        note_reject_hit(key);
    if (h->reject == HOT_FINAL)
        return;
    const unsigned threshold = h->reject == HOT_RETRY ? XLAT_REJECT_RETRY :
                               exit ? XLAT_EXIT_HOT : XLAT_HOT_THRESHOLD;
    if (h->count < threshold)
        h->count++;
    if (h->count >= threshold &&
        queue_compile(key, exit ? COMPILE_EXIT : h->reject ? COMPILE_RETRY : COMPILE_BURST, h->why))
        h->count = 0;
}

static void finish_compile(int idx, int why)
{
    const compile_request_t req = st->job.request;
    const bool exit = req.kind == COMPILE_EXIT;
    hot_t *h = &(exit ? st->exit_hot : st->hot)[hot_slot(req.key)];
    if (idx >= 0) {
        const int old = st->job.old_slot;
        if (old >= 0) {
            const region_t *r = &st->region[old];
            if (idx != old && r->held && r->chunk_lo == st->job.old_lo &&
                r->cs == st->job.rt.cs && r->ip == st->job.ip) {
                evict(old);
                rebuild_maps();
            }
            xlat_stats.entries_added++;
        } else if (exit) {
            xlat_stats.exit_built++;
        }
        xlat_stats.retries_built += req.kind == COMPILE_RETRY;
        if (h->key == req.key)
            h->reject = HOT_NONE;
        return;
    }
    if (why >= XLAT_RJ_COUNT) {
        rearm_request(req);
        return;
    }
    if (h->key == req.key) {
        h->count = 0;
        h->why = (uint8_t)why;
        h->reject = !exit && req.kind != COMPILE_RETRY &&
                    (why == XLAT_RJ_COVERED || why == XLAT_RJ_NOLOOP || why == XLAT_RJ_FULL)
                        ? HOT_RETRY : HOT_FINAL;
    }
    if (req.kind != COMPILE_RETRY) {
        xlat_stats.rejects++;
        note_reject(&st->job.rt, req.key, why);
    }
}

static void set_compile_target(int old, int trace_min)
{
    xlat_preempt_discard();
    st->job.ip = (uint16_t)st->job.request.key;
    st->job.n_extra = 0;
    st->job.trace_min = trace_min;
    st->job.attempt = 0;
    st->job.need = 0;
    st->job.started = false;
    st->job.pending = true;
    st->job.old_slot = old;
#if XLAT_LOG
    st->job.quanta = st->job.us = 0;
#endif
    if (old >= 0) {
        const region_t *r = &st->region[old];
        st->job.old_lo = r->chunk_lo;
        st->job.trace_min = r->trace_min;
        st->job.n_extra = r->n_extra + 1;
        memcpy(st->job.extra, r->extra, r->n_extra * sizeof(r->extra[0]));
        st->job.extra[r->n_extra] = st->job.ip;
        st->job.ip = r->ip;
    }
}

static void start_compile(const xlat_rt_t *rt)
{
    unsigned best = 0;
    for (unsigned i = 1; i < st->queued; ++i)
        if (st->queue[i].kind > st->queue[best].kind)
            best = i;
    const compile_request_t req = dequeue_compile(best);
    entry_t *e = lookup(req.key);
    st->job.rt = *rt;
    st->job.rt.cs = req.key >> 16;
    st->job.rt.seg_base[1] = (uint32_t)st->job.rt.cs << 4;
    st->job.rt.ip = (uint16_t)req.key;
    st->job.request = req;
    if (xlat_thunk_kind(rt, req.key))
        return;  /* dynamic thunk may have appeared since enqueue */
    if (e) {
        region_t *r = &st->region[e->region];
        if ((r->live || r->cold) &&
            (r->epoch == st->epoch || region_valid(r, &st->job.rt)))
            return; /* another request has already covered this entry */
        if (r->live || r->cold) {
            xlat_stats.invalidations += r->live != 0;
            r->live = r->cold = 0;
            gap_reset();
            unlink_region(e->region);
        }
    }
    const bool retry = req.kind == COMPILE_RETRY;
    const bool exit = req.kind == COMPILE_EXIT;
    const bool call = req.kind == COMPILE_CALL;
    const int old = exit || call || (retry && req.why == XLAT_RJ_COVERED)
                        ? covering(&st->job.rt) : -1;
    if (retry)
        xlat_stats.retries++;
    if (old >= 0 && st->region[old].n_extra < XLAT_EXTRA_MAX) {
        set_compile_target(old, 0);
    } else if ((old >= 0 && retry) ||
               (req.kind == COMPILE_BURST && covered(&st->job.rt))) {
        finish_compile(-1, XLAT_RJ_COVERED);
    } else {
        /* A hot callee can connect two regions even when it is very short. */
        const int min = exit || call ? XLAT_LINK_MIN :
                        retry && req.why != XLAT_RJ_FULL ? XLAT_TRACE_MIN : 0;
        set_compile_target(-1, min);
    }
}

static bool compile_ready(const xlat_rt_t *rt)
{
    return (st->job.pending || st->queued) &&
           (!rt->wall_deadline ||
            (st->compile_credit >= XLAT_COMPILE_CREDIT_US && (rt->eflags & 0x200u) &&
             st->compile_burst != rt->wall_deadline))
#if XLAT_STACKFUL
           && xlat_preempt_available()
#endif
           ;
}

/* Run one job until completion or a real guest IRQ. Debit measured cost,
 * including setup and publication; no artificial compiler deadline. */
static __attribute__((noinline)) void compile_slice(const xlat_rt_t *rt)
{
    if (stack_free() < XLAT_STACK_NEEDED) {
        xlat_stats.stack_skips++;
        return;
    }
    const uint32_t t0 = *rt->clock;
    st->compile_burst = rt->wall_deadline;
    if (!st->job.pending)
        start_compile(rt);
    if (st->job.pending) {
        xlat_rt_t *jrt = &st->job.rt;
        jrt->intr = rt->intr;
        jrt->clock = rt->clock;
        jrt->wall_deadline = rt->wall_deadline;
        if (st->job.started || st->job.attempt)
            xlat_stats.compile_resumes++;
        const int idx = translate_at(jrt, st->job.ip, st->job.trace_min,
                                     st->job.extra, st->job.n_extra);
#if XLAT_LOG
        ++st->job.quanta;
        st->job.us += *rt->clock - t0;
        if (idx >= 0) {
            const unsigned n = st->out.n_insns;
            if (n && n <= XLAT_MAX_INSNS) {
                ++st->compile_len[n].built;
                st->compile_len[n].first += st->job.quanta == 1;
                st->compile_len[n].quanta += st->job.quanta;
                st->compile_len[n].us += st->job.us;
                st->compile_len[n].arm += st->out.code_len;
                st->compile_len[n].final_us += st->out.final_us;
            }
            log_region_code(&st->region[idx]);
        }
#endif
        if (st->job.pending) {
            xlat_stats.compile_defers++;
        } else if (idx < 0 && st->out.reject < XLAT_RJ_COUNT &&
                   st->job.old_slot >= 0 &&
                   (st->job.request.kind == COMPILE_EXIT || st->job.request.kind == COMPILE_CALL)) {
            /* When extending a region fails, try a standalone link trace. */
            set_compile_target(-1, XLAT_LINK_MIN);
        } else {
            finish_compile(idx, st->out.reject);
        }
    }
    const uint32_t us = *rt->clock - t0;
    if (rt->wall_deadline)
        st->compile_credit -= (int32_t)us * XLAT_COMPILE_RATIO;
    xlat_stats.translate_us += us;
    if (us > xlat_stats.translate_max_us)
        xlat_stats.translate_max_us = us;
}

bool XLAT_HOT_FN(xlat_run)(xlat_rt_t *rt)
{
    if (!st) {
        /* Allocate on a repeated burst start or a hot CALL (leave BIOS
         * POST and other cold paths to XRIP). */
        static uint32_t first_key = UINT32_MAX;
        const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
        if (disabled || (key != first_key && rt->fn_key == UINT32_MAX)) {
            first_key = key;
            return false;
        }
        if (!ready())
            return false;
    }
    const int32_t start = rt->budget;
    gap_rt = rt;
    rt->code_map = xlat_code_map;
    /* Something but XRIP's ordinary writes ran since the last call (or XRIP
     * wrote into translated code): compare each region's bytes once */
    if (rt->mem != st->mem || rt->mem_size != st->mem_size) {
        if (st->job.pending) {
            xlat_preempt_discard();
            rearm_request(st->job.request);
            st->job.pending = false;
            xlat_stats.compile_cancel++;
        }
        for (unsigned i = 0; i < st->queued; ++i)
            rearm_request(st->queue[i]);
        st->queued = 0;
        clear_compile_blocked();
        gap_reset();
        st->mem = rt->mem;
        st->mem_size = rt->mem_size;
        xlat_dirty = true;
    }
    if (xlat_dirty) {
        xlat_dirty = false;
        st->epoch++;
#if XLAT_LOG
        ++xlat_stats.epoch_start;
#endif
        rt->hit_lo = rt->hit_hi = 0;
    }
    validate_local_writes(rt);
    const xlat_tramp_fn tramp = (xlat_tramp_fn)(uintptr_t)(st->arena.base + 1);
    rt->link = st->link;
    rt->last = st->last;
    cf_mirror(rt);
    rt->cur = NO_REGION;
    rt->links = 0;
    rt->link_alt = 0;
    rt->epoch = st->epoch;
    if (rt->fn_key != UINT32_MAX) {
        const entry_t *e = lookup(rt->fn_key);
        const region_t *r = e ? &st->region[e->region] : NULL;
        if (xlat_thunk_kind(rt, rt->fn_key) ||
            (r && (r->live || r->cold) && r->epoch == st->epoch) ||
            queue_compile(rt->fn_key, COMPILE_CALL, XLAT_RJ_NONE))
            xlat_call_ack(request_linear(rt->fn_key));
    }
    bool ran = false, serviced = false, resample = true;
    for (;;) {
        const uint32_t key = (uint32_t)rt->cs << 16 | (uint16_t)rt->ip;
        entry_t *e = lookup(key);
        const bool live = e && st->region[e->region].live;
        const bool cold = e && st->region[e->region].cold;
        const int32_t remaining = rt->wall_deadline ? (int32_t)(rt->wall_deadline - *rt->clock)
                                                    : INT32_MAX;
        if (rt->wall_deadline && (remaining <= 0 || *rt->intr))
            break;
        /* On a miss, a live INT/RETF thunk needs neither source copies
         * nor a region. Execute the actual bytes before sampling/queueing. */
        const unsigned thunk = !live && !cold ? xlat_thunk_kind(rt, key) : 0;
        if (thunk && rt->budget > 0 && !*rt->intr) {
            xlat_rt_pending(rt);
            if (xlat_try_native_thunk(rt, thunk)) {
                cf_mirror(rt);
                ran = true;
                resample = true;
                if (flush_pending || rt->exit_kind == XLAT_EXIT_STOP ||
                    rt->budget <= 0 || *rt->intr)
                    break;
                continue;
            }
        }
        if (resample && !live && !cold && !thunk) {
            const bool call = ran && rt->exit_kind == XLAT_EXIT_FAR_CALL;
            const uint32_t linear = request_linear(key);
            if (call && xlat_call_hot(linear) && queue_compile(key, COMPILE_CALL, XLAT_RJ_NONE))
                xlat_call_ack(linear);
            else if (ran ? call || rt->exit_kind == XLAT_EXIT_BRANCH ||
                           rt->exit_kind == XLAT_EXIT_SIDE : key != rt->fn_key)
                sample(key, ran);
        }
        /* A far transfer or INT may have reached a native ROM entry.
         * Try the actual target before compiling or returning to XRIP. */
        const uint32_t linear = request_linear(key);
        /* Do not create a JIT burst for an isolated native callback. */
        if (ran && !live && !cold && rt->budget > 0 && !*rt->intr &&
            linear >= 0xf0000u && linear < 0x100000u) {
            xlat_rt_pending(rt);
            if (xlat_try_native_escape(rt)) {
                cf_mirror(rt);
                ran = true;
                resample = true;
                if (flush_pending || rt->exit_kind == XLAT_EXIT_STOP ||
                    rt->budget <= 0 || *rt->intr)
                    break;
                continue;
            }
        }
        /* Serve misses after sampling; on a ready path drain a full credit
         * at the burst's first entry so long-running regions cannot starve
         * the queue. Recheck the entry after any cache mutation. */
        if (!serviced && (!live || (!ran && rt->wall_deadline)) && compile_ready(rt)) {
            compile_slice(rt);
            serviced = true;
            resample = false;
            continue;
        }
        if (!live)
            break;
        region_t *r = &st->region[e->region];
        if (r->epoch != st->epoch) {
            /* Since then XRIP may have written anywhere; translated code
             * cannot write into translated x86 code (xlat_code_map). */
            if (!region_valid(r, rt)) {
                r->live = 0;
                gap_reset();
                unlink_region(e->region);
                xlat_stats.invalidations++;
                break;
            }
            r->epoch = st->epoch;
        }
        /* A region without a loop has no back edge charging the budget:
         * at least r->fn instructions run. */
        const uint32_t charge = XLAT_ENTRY_COST + r->fn;
        const uint32_t code = (uint32_t)(uintptr_t)(st->arena.base + e->code_off) | 1u;
        /* the exits that reach this entry from now on go on here at once */
        const unsigned slot = xlat_link_hash(key);
        xlat_link_t *l = &st->link[slot];
        xlat_link_t *alt = &st->link[slot + XLAT_LINK_SLOTS];
        /* Keep a colliding valid entry in the secondary bank. An old
         * epoch is never promoted; both banks obey the same invalidation. */
        if (l->key != key) {
            if (l->key != UINT32_MAX && l->epoch == st->epoch)
                *alt = *l;
            else if (alt->key == key)
                alt->key = UINT32_MAX;
        }
        l->key = key;
        l->code = code;
        l->epoch = st->epoch;
        l->charge = (uint16_t)charge;
        l->region = e->region;
        rt->epoch = st->epoch;
        rt->budget -= (int32_t)charge;
        const int32_t b0 = rt->budget, x0 = (int32_t)rt->extra;
        const uint32_t t0 = *rt->clock;
        rt->cur = e->region;
        st->last[e->region] = rt->seq = ++st->seq;
        tramp(rt, code);
        /* the region it left from (entered through the link table, maybe) */
        region_t *const x = &st->region[rt->cur];
        rt->cur = NO_REGION;
        r->us += *rt->clock - t0;
        ran = true;
        resample = true;
        xlat_stats.entries++;
        r->entries++;
        r->insns += (uint32_t)((b0 - rt->budget) + ((int32_t)rt->extra - x0)) + r->fn;
        /* A short region may connect much larger native regions. Its
         * length alone is not a reason to force the chain through XRIP.
         * Retain the existing guard for regions dominated by side exits. */
        if (r->live && r->entries == XLAT_COLD_ENTRIES &&
            (r->sides + r->exits[XLAT_EXIT_SIDE]) * XLAT_COLD_SIDE_RATIO > r->insns) {
            r->live = 0;
            r->cold = 1;
            gap_reset();
            unlink_region(e->region);
            xlat_stats.cold++;
        }
        const uint32_t exit_kind = rt->exit_kind == XLAT_EXIT_FAR_CALL ? XLAT_EXIT_BRANCH : rt->exit_kind;
        if (exit_kind < XLAT_EXIT_COUNT) {
            xlat_stats.exits[exit_kind]++;
            x->exits[exit_kind]++;
        }
        /* STOP: XRIP must run the next instruction. */
        if (flush_pending || *rt->intr || rt->exit_kind == XLAT_EXIT_STOP)
            break;
        /* Budget used up (entry costs, short regions): the burst lasts
         * until its deadline, as in the back-edge stubs. */
        if (rt->budget <= 0) {
            uint32_t more = XLAT_SLICE_CHECK;
            if (rt->budget_limit) {
                const uint32_t granted = (uint32_t)start + rt->extra;
                if (granted >= rt->budget_limit ||
                    (int32_t)(*rt->clock - rt->wall_deadline) >= 0)
                    break;
                const uint32_t remain = rt->budget_limit - granted;
                if (more > remain)
                    more = remain;
            } else if ((int32_t)(*rt->clock - rt->deadline) >= 0) {
                break;
            }
            rt->budget += (int32_t)more;
            rt->extra += more;
        }
    }
    xlat_rt_pending(rt);    /* XRIP takes the lazy flags as they are */
    if (ran)
        xlat_stats.guest_insns += (uint32_t)(start + (int32_t)rt->extra - rt->budget);
    xlat_stats.links += rt->links;
    xlat_stats.link_alt += rt->link_alt;
    xlat_stats.entries += rt->links;
    gap_rt = NULL;
    if (flush_pending)
        xlat_flush();
    return ran;
}

void XLAT_HOT_FN(xlat_note_side)(const xlat_rt_t *rt)
{
    if (st && rt->cur < XLAT_MAX_REGIONS)
        st->region[rt->cur].sides++;
}

/* x86 condition cc (Jcc low nibble) on the lazy flags in rt, as XRIP's
 * condition(). Called from generated code. */
void XLAT_HOT_FN(xlat_rt_pending)(xlat_rt_t *rt)
{
    const uint32_t op = rt->lazy_op;
    if (!(op & XLAT_LAZY_PENDING))
        return;
    const uint32_t kind = (op >> 5) & 3u, shift = op & 30u;
    const uint32_t d = rt->lazy_dst, s = rt->lazy_src;
    const bool keep = (op & XLAT_LAZY_KEEP_CF) != 0;
    if (kind == FK_ADD) {
        rt->lazy_result = d + s;
        if (!keep)
            rt->lazy_cf = rt->lazy_result < d;
    } else if (kind == FK_SUB) {
        rt->lazy_result = d - s;
        if (!keep)
            rt->lazy_cf = d < s;
        rt->lazy_src = ~s & ~((1u << shift) - 1u);   /* XRIP keeps (~src) << shift */
    } else {
        rt->lazy_result = d;
        rt->lazy_cf = 0;
    }
    rt->lazy_op = (uint8_t)(op & 0x7eu);
}

uint32_t XLAT_HOT_FN(xlat_interp_call)(xlat_rt_t *rt, uint32_t flags)
{
    xlat_rt_pending(rt);
    const uint32_t r = xlat_interp_one(rt, flags);
    cf_mirror(rt);
    return r;
}

uint32_t XLAT_HOT_FN(xlat_rt_cond)(xlat_rt_t *rt, uint32_t cc)
{
    bool cf, zf, sf, of, pf;
    xlat_rt_pending(rt);
    if (rt->lazy_op) {
        const uint32_t r = rt->lazy_result, d = rt->lazy_dst, s = rt->lazy_src;
        uint32_t p = (r >> (rt->lazy_op & 31u)) & 0xffu;
        p ^= p >> 4;
        p ^= p >> 2;
        p ^= p >> 1;
        cf = rt->lazy_cf;
        zf = r == 0;
        sf = r >> 31;
        of = ((r ^ d) & (r ^ s)) >> 31;
        pf = !(p & 1u);
    } else {
        const uint32_t f = rt->eflags;
        cf = f & 1u;
        pf = f >> 2 & 1u;
        zf = f >> 6 & 1u;
        sf = f >> 7 & 1u;
        of = f >> 11 & 1u;
    }
    bool result;
    switch ((cc & 0x0fu) >> 1) {
    case 0: result = of; break;
    case 1: result = cf; break;
    case 2: result = zf; break;
    case 3: result = cf || zf; break;
    case 4: result = sf; break;
    case 5: result = pf; break;
    case 6: result = sf != of; break;
    default: result = zf || sf != of; break;
    }
    return result != (cc & 1u);
}

/* x86 flags image from the lazy state, as XRIP's resolve_flags(). */
static void resolve_rt(xlat_rt_t *rt)
{
    if (!rt->lazy_op)
        return;
    const uint32_t shift = rt->lazy_op & 31u, kind = rt->lazy_op >> 5;
    const uint32_t r = rt->lazy_result, d = rt->lazy_dst, s = rt->lazy_src;
    uint32_t p = (r >> shift) & 0xffu;
    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    uint32_t v = (uint32_t)rt->lazy_cf | (~p & 1u) << 2 | (uint32_t)(r == 0) << 6 |
                 (r >> 31) << 7 | (((r ^ d) & (r ^ s)) >> 31) << 11;
    uint32_t carries = d ^ s ^ r;
    if (kind == FK_SUB)
        carries = ~carries;
    if (kind != FK_LOGIC)
        v |= (carries >> shift) & 0x10u;
    rt->eflags = (rt->eflags & ~0x8d5u) | v;
    rt->lazy_op = 0;
}

uint32_t XLAT_HOT_FN(xlat_rt_resolve)(xlat_rt_t *rt)
{
    xlat_rt_pending(rt);
    resolve_rt(rt);
    return rt->eflags;
}

/* In flash as xlat_rt_rep (SRAM heap). */
uint32_t xlat_rt_popf(xlat_rt_t *rt, uint32_t value)
{
    xlat_rt_pending(rt);
    resolve_rt(rt);
    const uint32_t diff = (rt->eflags ^ value) & 0x7fd5u;
    if (diff & 0x7100u)
        return 0;       /* TF, IOPL, NT */
    if ((diff & 0x200u) && !xlat_rt_io(rt, XLAT_IO_SET_IF, value & 0x200u))
        return 0;
    rt->eflags = (rt->eflags & ~0x7fd5u) | (value & 0x7fd5u);
    rt->lazy_cf = (uint8_t)(rt->eflags & 1u);   /* resolved: lazy_cf = CF */
    return 1;
}

/* [linear, + len) is at rt->mem + linear (xlat_rt_t: fast or upper memory). */
static bool rep_direct(const xlat_rt_t *rt, uint32_t linear, uint32_t len)
{
    if (linear + len <= rt->mem_size)
        return true;
    return rt->umb_room && linear >= XLAT_UMB_BASE &&
           linear + len <= XLAT_UMB_BASE + XLAT_UMB_MARGIN + rt->umb_room;
}

/* Clear both cached ranges while the runtime state is active. */
static void gap_reset(void)
{
    if (gap_rt) {
        gap_rt->gap_lo = gap_rt->gap_hi = gap_rt->hit_lo = gap_rt->hit_hi = 0;
        gap_rt->gap2_lo = gap_rt->gap2_hi = 0;
    }
    memset(gap_recent, 0, sizeof(gap_recent));
    gap_next = 0;
}

void xlat_note_host_write(uint32_t linear, uint32_t len)
{
    if (!len)
        return;
#if defined(NO_PAGING)
    /* The conventional window cannot be banked or aliased by EMS/VGA.
     * Scan the conservative map across the whole destination, not just
     * the first byte. The local validator checks the actual source bytes. */
    if (linear < 0xa0000u && len <= 0xa0000u - linear) {
        const uint32_t last = linear + len - 1u;
        for (uint32_t b = linear >> XLAT_CODE_BLOCK_SHIFT;
             b <= (last >> XLAT_CODE_BLOCK_SHIFT); ++b) {
            if (xlat_code_map[b >> 3] & (1u << (b & 7u))) {
                if (!xlat_write_hi || linear < xlat_write_lo)
                    xlat_write_lo = linear;
                if (last + 1u > xlat_write_hi)
                    xlat_write_hi = last + 1u;
                break;
            }
        }
#if XLAT_LOG
        ++xlat_stats.host_local;
#endif
        return;
    }
#endif
    /* HMA, UMB/EMS, VGA, remote or paged RAM: keep the original policy. */
    xlat_dirty = true;
#if XLAT_LOG
    ++xlat_stats.host_unknown;
#endif
}

/* [addr, last] meets the x86 bytes of a live region. Cold/invalidated
 * chunks are retained for allocation/statistics, not for code protection.
 * Else *lo, *hi (when given) are narrowed to the gap around the write. */
static bool code_meets(uint32_t addr, uint32_t last, uint32_t *lo, uint32_t *hi,
                       uint32_t *run_lo, uint32_t *run_hi)
{
    const uint32_t wlo = lo ? *lo : addr, whi = hi ? *hi : last + 1u;
    region_iter_t it = regions_in(wlo, whi);
    for (int k; (k = region_next(&it)) >= 0;) {
        const region_t *r = &st->region[k];
        if (!r->live || r->lo >= whi || r->lo + r->span <= wlo)
            continue;
        xlat_stats.gap_candidates++;
        const uint8_t *d = st->arena.base + r->data_off;
        for (unsigned j = 0; j < r->n_runs; ++j) {
            uint32_t at;
            __builtin_memcpy(&at, d, 4);
            const uint32_t len = d[4] | (uint32_t)d[5] << 8;
            d += 8 + ((len + 3u) & ~3u);
            if (!len)
                continue;
            const uint32_t end = at + len;
            if (at <= last && end > addr) {
                *run_lo = at;
                *run_hi = end;
                return true;
            }
            if (end <= addr) {
                if (lo && end > *lo)
                    *lo = end;
            } else if (hi && at < *hi) {
                *hi = at;
            }
        }
    }
    return false;
}

/* In flash: called only for writes into marked blocks the cached gap and
 * hit do not cover. */
static inline void gap_promote(xlat_rt_t *rt, uint32_t lo, uint32_t hi)
{
    if (rt->gap_lo == lo && rt->gap_hi == hi)
        return;
    rt->gap2_lo = rt->gap_lo;
    rt->gap2_hi = rt->gap_hi;
    rt->gap_lo = lo;
    rt->gap_hi = hi;
}

uint32_t xlat_rt_gap(uint32_t addr, uint32_t last, xlat_rt_t *rt)
{
    uint32_t lo = addr & ~0xfffu, hi = (last | 0xfffu) + 1u, run_lo, run_hi;
    xlat_stats.gap_calls++;
    for (unsigned k = 0; k < GAP_RECENT; ++k) {
        if (addr >= gap_recent[k].lo && last < gap_recent[k].hi) {
            gap_promote(rt, gap_recent[k].lo, gap_recent[k].hi);
            ++xlat_stats.gap_reuse;
            return 0;
        }
    }
    if (!st || code_meets(addr, last, &lo, &hi, &run_lo, &run_hi)) {
        if (st) {
            rt->hit_lo = run_lo;
            rt->hit_hi = run_hi;
        }
        return 1;
    }
    gap_promote(rt, lo, hi);
    gap_recent[gap_next].lo = lo;
    gap_recent[gap_next].hi = hi;
    gap_next = (gap_next + 1u) & (GAP_RECENT - 1u);
    return 0;
}

/* Reuse the same exact negative cache for C memory helpers. */
static bool range_has_code(xlat_rt_t *rt, uint32_t first, uint32_t last)
{
    if ((first >= rt->gap_lo && last < rt->gap_hi) ||
        (first >= rt->gap2_lo && last < rt->gap2_hi))
        return false;
    if (rt->hit_lo < rt->hit_hi && last >= rt->hit_lo && first < rt->hit_hi)
        return true;
    return xlat_rt_gap(first, last, rt) != 0;
}

/* A fast string element can use conventional RAM or the audited VGA
 * aperture, but never bypass mapping checks for UMB/EMS/HMA. */
static bool string_ram(const xlat_rt_t *rt, uint32_t a, unsigned bytes)
{
    return rt->mem_size >= bytes && a <= rt->mem_size - bytes;
}

static bool string_vga(uint32_t a, unsigned bytes)
{
    return a >= 0xa0000u && a <= 0xc0000u - bytes;
}

static bool bus_write_ok(const xlat_rt_t *rt, uint32_t address, unsigned bytes)
{
    return (bytes == 1 || bytes == 2 || bytes == 4) &&
           address < 0x110000u && bytes <= 0x110000u - address &&
           (rep_direct(rt, address, bytes) || string_vga(address, bytes));
}

/* A decoded scalar store is already past effective-address calculation.
 * Never bypass EMS/HMA/other non-direct mappings for writes. A store to
 * live source is committed once, validated locally, then redispatched if
 * the active region was invalidated. The caller commits SP afterwards. */
uint32_t xlat_rt_bus_write(xlat_rt_t *rt, uint32_t address, uint32_t value, unsigned bytes)
{
    if (!bus_write_ok(rt, address, bytes)) {
        ++xlat_stats.bus_write_miss;
        return 0;
    }
    if (rep_direct(rt, address, bytes)) {
        if (bytes == 1)
            rt->mem[address] = (uint8_t)value;
        else if (bytes == 2) {
            const uint16_t word = (uint16_t)value;
            __builtin_memcpy(rt->mem + address, &word, 2);
        } else
            __builtin_memcpy(rt->mem + address, &value, 4);
    } else {
        xlat_vga_write(address, value, bytes);
    }
    ++xlat_stats.bus_write;
    xlat_note_write_n(address, bytes);
    if ((xlat_write_hi || xlat_dirty) && !xlat_region_still_valid(rt)) {
        ++xlat_stats.bus_smc;
        return XLAT_MEM_REDISPATCH;
    }
    return 1;
}

/* Decoded ADD r/m,reg/imm: validate the write mapping before the first
 * read (VGA reads modify latches). Keep the exact XRIP lazy ADD flags;
 * the emitter can also reconstruct APSR for an immediately following Jcc. */
uint32_t xlat_rt_bus_add(xlat_rt_t *rt, uint32_t address, uint32_t value, unsigned bytes)
{
    if (!bus_write_ok(rt, address, bytes)) {
        ++xlat_stats.bus_write_miss;
        return 0;
    }
    const unsigned shift = 32u - bytes * 8u;
    const uint32_t dst = xlat_bus_read(address, bytes) << shift;
    const uint32_t src = value << shift;
    const uint32_t result = dst + src;
    const uint32_t status = xlat_rt_bus_write(rt, address, result >> shift, bytes);
    if (status) {
        rt->lazy_dst = dst;
        rt->lazy_src = src;
        rt->lazy_result = result;
        rt->lazy_op = (uint8_t)((FK_ADD << 5) | shift);
        rt->lazy_cf = result < dst;
        ++xlat_stats.bus_rmw;
    }
    return status;
}

/* The stack is 16-bit in JIT mode. The two reads of RETF deliberately
 * use separate addresses, including SP wrap, as XRIP's pop()/far_return. */
uint32_t xlat_rt_bus_pop(xlat_rt_t *rt, uint32_t release, uint32_t far_ret)
{
    uint16_t sp = (uint16_t)rt->sp;
    uint32_t value = xlat_bus_read(rt->seg_base[2] + sp, 2);
    sp = (uint16_t)(sp + 2u);
    if (far_ret) {
        value |= xlat_bus_read(rt->seg_base[2] + sp, 2) << 16;
        sp = (uint16_t)(sp + 2u);
    }
    rt->sp = (rt->sp & 0xffff0000u) | (uint16_t)(sp + release);
    ++xlat_stats.bus_pop;
    return value;
}

/* ARM r4-r10 are EAX, ECX, EDX, EBX, EBP, ESI, EDI in this order.
 * ESP is already in rt. No writes to XRIP's register file or decoder state. */
static uint32_t *scalar_reg(xlat_rt_t *rt, uint32_t *frame, unsigned reg)
{
    return reg == 4u ? &rt->sp : &frame[reg - (reg > 4u)];
}

static uint32_t scalar_get(xlat_rt_t *rt, uint32_t *frame, unsigned reg, unsigned bytes)
{
    if (bytes == 1u)
        return (uint8_t)(frame[reg & 3u] >> (reg >= 4u ? 8u : 0u));
    const uint32_t v = *scalar_reg(rt, frame, reg);
    return bytes == 2u ? (uint16_t)v : v;
}

static void scalar_put(xlat_rt_t *rt, uint32_t *frame, unsigned reg, unsigned bytes, uint32_t v)
{
    if (bytes == 1u) {
        const unsigned shift = reg >= 4u ? 8u : 0u;
        uint32_t *p = &frame[reg & 3u];
        *p = (*p & ~(0xffu << shift)) | ((v & 0xffu) << shift);
    } else {
        uint32_t *p = scalar_reg(rt, frame, reg);
        *p = bytes == 2u ? (*p & 0xffff0000u) | (uint16_t)v : v;
    }
}

static uint32_t scalar_address(const xlat_rt_t *rt, const uint32_t *frame, unsigned ea, uint16_t disp)
{
    /* BX+SI, BX+DI, BP+SI, BP+DI, SI, DI, BP, BX. */
    static const uint8_t base[8] = {3, 3, 4, 4, 5, 6, 4, 3};
    static const uint8_t index[4] = {5, 6, 5, 6};
    const unsigned rm = ea & 7u;
    uint32_t off = disp;
    if (ea & SC_EA_XLAT)
        off = frame[3] + (uint8_t)frame[0];
    else if (!(ea & SC_EA_DIRECT)) {
        off += frame[base[rm]];
        if (rm < 4u)
            off += frame[index[rm]];
    }
    return rt->seg_base[(ea >> 3) & 3u] + (uint16_t)off;
}

/* Run an already decoded scalar operation. The descriptor belongs to
 * generated code, not guest memory. On refusal no guest effects occurred;
 * after a committed store, never execute the old instruction again. */
uint32_t xlat_rt_scalar(xlat_rt_t *rt, uint32_t *frame, const uint16_t *d)
{
    const unsigned ctl = d[0], op = ctl & 15u;
    const unsigned bytes = 1u << ((ctl >> 4) & 3u), reg = (ctl >> 6) & 7u;
    const uint32_t imm = d[3] | (uint32_t)d[4] << 16;
    uint32_t next = d[5], status = 1, v;
    if (*rt->intr) {
        rt->ip = d[6];
        ++xlat_stats.scalar_stop;
        return SC_STOP;
    }
    if (bytes > 4u || op > SC_SEG_POP) {
        ++xlat_stats.scalar_refused;
        return SC_REFUSED;
    }
    switch (op) {
    case SC_LOAD:
    case SC_MOVZX:
    case SC_MOVSX:
    case SC_SEG_LOAD:
        v = xlat_bus_read(scalar_address(rt, frame, d[1], d[2]), bytes);
        if (op == SC_SEG_LOAD)
            rt->seg_base[reg] = (uint32_t)(uint16_t)v << 4;
        else {
            if (op == SC_MOVSX)
                v = bytes == 1u ? (uint32_t)(int32_t)(int8_t)v : (uint32_t)(int32_t)(int16_t)v;
            scalar_put(rt, frame, reg, op == SC_LOAD ? bytes : (ctl & SC_WIDE ? 4u : 2u), v);
        }
        break;
    case SC_STORE:
    case SC_ADD:
    case SC_SEG_STORE: {
        const uint32_t a = scalar_address(rt, frame, d[1], d[2]);
        v = op == SC_SEG_STORE ? rt->seg_base[reg] >> 4 :
            (ctl & SC_IMM) ? imm : scalar_get(rt, frame, reg, bytes);
        status = op == SC_ADD ? xlat_rt_bus_add(rt, a, v, bytes) : xlat_rt_bus_write(rt, a, v, bytes);
        break;
    }
    case SC_CALL:
    case SC_PUSH:
    case SC_SEG_PUSH: {
        const uint16_t sp = (uint16_t)(rt->sp - 2u);
        v = op == SC_SEG_PUSH ? rt->seg_base[reg] >> 4 :
            (ctl & SC_IMM) ? imm : scalar_get(rt, frame, reg, 2u);
        status = xlat_rt_bus_write(rt, rt->seg_base[2] + sp, v, 2u);
        if (status)
            rt->sp = (rt->sp & 0xffff0000u) | sp;
        break;
    }
    case SC_POP:
    case SC_SEG_POP:
    case SC_RET:
    case SC_RETF:
        v = xlat_rt_bus_pop(rt, imm, op == SC_RETF);
        if (op == SC_POP)
            scalar_put(rt, frame, reg, 2u, v);
        else if (op == SC_SEG_POP)
            rt->seg_base[reg] = (uint32_t)(uint16_t)v << 4;
        else {
            next = (uint16_t)v;
            if (op == SC_RETF) {
                rt->cs = (uint16_t)(v >> 16);
                rt->seg_base[1] = (uint32_t)rt->cs << 4;
            }
        }
        break;
    }
    if (!status) {
        ++xlat_stats.scalar_refused;
        return SC_REFUSED;
    }
    rt->ip = next;
    if (*rt->intr) {
        ++xlat_stats.scalar_stop;
        return SC_STOP;
    }
    if (status == XLAT_MEM_REDISPATCH || !(ctl & SC_CONT)) {
        ++xlat_stats.scalar_dispatch;
        return SC_DISPATCH;
    }
    ++xlat_stats.scalar_resume;
    return op == SC_ADD ? SC_RESUME_ADD : SC_RESUME;
}

/* Check at the last possible moment before the full GPR spill. The
 * synchronized string path still rechecks CPU state and the deadline. */
uint32_t xlat_rt_string_ready(xlat_rt_t *rt)
{
    if (*rt->intr || (rt->wall_deadline &&
        (int32_t)(*rt->clock - rt->wall_deadline) >= 0) ||
        (rt->budget_limit && rt->budget <= 0)) {
        ++xlat_stats.string_preempt;
        return 0;
    }
    return 1;
}

uint32_t xlat_rt_string_fast(xlat_rt_t *rt, uint32_t src, uint32_t dst, uint32_t ctl)
{
    const uint32_t count = ctl & 0xffffu;
    const unsigned bytes = (ctl >> 16) & 7u;
    const bool stos = (ctl & XLAT_REP_STOS) != 0;
    if (bytes != 1 && bytes != 2 && bytes != 4)
        return 0;
    const int step = (rt->eflags & 0x400u) ? -(int)bytes : (int)bytes;
    const uint32_t sb = rt->seg_base[(ctl >> XLAT_REP_SEG_SHIFT) & 3u], db = rt->seg_base[0];
    uint16_t si = (uint16_t)(src - sb), di = (uint16_t)(dst - db);
    uint32_t done = 0;
    ++xlat_stats.fast_string_calls;
    while (done < count) {
        if (*rt->intr || (rt->wall_deadline &&
            (int32_t)(*rt->clock - rt->wall_deadline) >= 0) ||
            (rt->budget_limit && rt->budget <= 0))
            break;
        const uint32_t sa = sb + si, da = db + di;
        const bool dr = string_ram(rt, da, bytes);
        const bool sr = !stos && string_ram(rt, sa, bytes);
        if ((!dr && !string_vga(da, bytes)) ||
            (!stos && !sr && !string_vga(sa, bytes)))
            break;
        if ((rt->code_map[da >> (XLAT_CODE_BLOCK_SHIFT + 3u)] >>
             ((da >> XLAT_CODE_BLOCK_SHIFT) & 7u)) & 1u) {
            if (range_has_code(rt, da, da + bytes - 1u))
                break;  /* before even a VGA read: latches have side effects */
        }
        uint32_t v = src;
        if (!stos) {
            if (!sr)
                v = xlat_vga_read(sa, bytes);
            else if (bytes == 1)
                v = rt->mem[sa];
            else if (bytes == 2) {
                uint16_t w;
                __builtin_memcpy(&w, rt->mem + sa, 2);
                v = w;
            } else
                __builtin_memcpy(&v, rt->mem + sa, 4);
        }
        if (!dr)
            xlat_vga_write(da, v, bytes);
        else if (bytes == 1)
            rt->mem[da] = (uint8_t)v;
        else if (bytes == 2) {
            const uint16_t w = (uint16_t)v;
            __builtin_memcpy(rt->mem + da, &w, 2);
        } else
            __builtin_memcpy(rt->mem + da, &v, 4);
        if (!stos)
            si = (uint16_t)(si + step);
        di = (uint16_t)(di + step);
        ++done;
        --rt->budget;
    }
    xlat_stats.fast_string_elems += done;
    return done;
}

/* The protected POP is a direct stack read: no interpreter context,
 * no pending shadow is left behind after the pair. */
uint32_t xlat_rt_sti_pop(xlat_rt_t *rt, uint32_t seg)
{
    const uint32_t addr = rt->seg_base[2] + (uint16_t)rt->sp;
    if ((seg != 0 && seg != 3) || !rep_direct(rt, addr, 2)) {
        ++xlat_stats.shadow_mem_miss;
        return 0;
    }
    if (*rt->intr) {
        ++xlat_stats.shadow_irq_miss;
        return 0;
    }
    uint16_t selector;
    __builtin_memcpy(&selector, rt->mem + addr, 2);
    if (*rt->intr) {
        ++xlat_stats.shadow_irq_miss;
        return 0;   /* IF was already set: IRQ can precede POP */
    }
    if (!(rt->eflags & 0x200u) && !xlat_rt_io(rt, XLAT_IO_SET_IF, 1))
        return 0;
    rt->sp = (rt->sp & 0xffff0000u) | (uint16_t)(rt->sp + 2u);
    rt->seg_base[seg] = (uint32_t)selector << 4;
    ++xlat_stats.shadow_pop;
    ++xlat_stats.shadow_fast;
    xlat_stats.shadow_umb += addr >= rt->mem_size;
    return XLAT_IO_DONE | (*rt->intr ? XLAT_IO_BOUNDARY : 0u);
}

/* In flash, not SRAM (__time_critical_func): the SRAM heap is small, and
 * the copy itself is bound by guest memory. */
static uint32_t rep_chunk(xlat_rt_t *rt, uint32_t src, uint32_t dst, uint32_t ctl)
{
    const uint32_t n = ctl & 0xffffu, bytes = ctl >> 16 & 7u;
    const bool stos = (ctl & XLAT_REP_STOS) != 0;
    if (!n || (rt->eflags & 0x400u))
        return 0;
    const uint32_t len = n * bytes;
    if ((dst - rt->seg_base[0]) + len > 0x10000u || !rep_direct(rt, dst, len))
        return 0;
    if (!stos && ((src - rt->seg_base[ctl >> XLAT_REP_SEG_SHIFT & 3u]) + len > 0x10000u ||
                  !rep_direct(rt, src, len)))
        return 0;
    /* no translated code in the destination (as the write checks): the
     * blocks, then the bytes when a block is marked */
    for (uint32_t b = dst >> XLAT_CODE_BLOCK_SHIFT; b <= (dst + len - 1u) >> XLAT_CODE_BLOCK_SHIFT; ++b)
        if (rt->code_map[b >> 3] >> (b & 7u) & 1u) {
            if (range_has_code(rt, dst, dst + len - 1u))
                return 0;
            break;
        }
    uint8_t *d = rt->mem + dst;
    if (stos) {
        uint32_t v = bytes == 1 ? (src & 0xffu) * 0x01010101u
                     : bytes == 2 ? (src & 0xffffu) * 0x00010001u : src;
        uint32_t k = 0;
        for (; k + 4u <= len; k += 4u)
            __builtin_memcpy(d + k, &v, 4);
        for (; k < len; ++k)
            d[k] = (uint8_t)(v >> (8u * (k & 3u)));   /* the pattern goes on byte by byte */
        return 1;
    }
    const uint8_t *s = rt->mem + src;
    if (dst > src && dst < src + len) {
        /* forward overlap: element by element, each read whole before it
         * is written, as the instruction does (MOVSW at dst = src + 1
         * repeats the first byte pair, not the first byte) */
        for (uint32_t k = 0; k < len; k += bytes) {
            if (bytes == 4) {
                uint32_t w;
                __builtin_memcpy(&w, s + k, 4);
                __builtin_memcpy(d + k, &w, 4);
            } else if (bytes == 2) {
                uint16_t w;
                __builtin_memcpy(&w, s + k, 2);
                __builtin_memcpy(d + k, &w, 2);
            } else {
                d[k] = s[k];
            }
        }
        return 1;
    }
    uint32_t k = 0;
    for (; k + 4u <= len; k += 4u) {
        uint32_t w;
        __builtin_memcpy(&w, s + k, 4);
        __builtin_memcpy(d + k, &w, 4);
    }
    for (; k < len; ++k)
        d[k] = s[k];
    return 1;
}

uint32_t xlat_rt_rep(xlat_rt_t *rt, uint32_t src, uint32_t dst, uint32_t ctl)
{
    const uint32_t count = ctl & 0xffffu, bytes = ctl >> 16 & 7u;
    const bool stos = (ctl & XLAT_REP_STOS) != 0;
    uint32_t done = 0;
    while (done < count) {
        if (*rt->intr || (rt->wall_deadline &&
            (int32_t)(*rt->clock - rt->wall_deadline) >= 0) ||
            (rt->budget_limit && rt->budget <= 0))
            break;
        const uint32_t left = count - done;
        const uint32_t n = left < XLAT_REP_CHUNK ? left : XLAT_REP_CHUNK;
        if (!rep_chunk(rt, stos ? src : src + done * bytes, dst + done * bytes,
                       (ctl & ~0xffffu) | n)) {
            /* DF=1 rejects bulk at done=0. In all other cases the
             * prefix already copied ran forward. Offsets wrap in fast. */
            done += xlat_rt_string_fast(rt, stos ? src : src + done * bytes,
                                       dst + done * bytes, (ctl & ~0xffffu) | left);
            break;
        }
        /* The next iteration checks IRQ, deadline and budget before the
         * next chunk. On completion the generated REP tail checks IRQ. */
        done += n;
    }
    if (done < count && ((rt->budget_limit && rt->budget <= 0) ||
        (rt->wall_deadline && (int32_t)(*rt->clock - rt->wall_deadline) >= 0))) {
        ++xlat_stats.rep_yield;
        return done | XLAT_REP_YIELD;
    }
    return done;
}

static uint32_t szp(uint32_t value, int bits)
{
    uint32_t p = value & 0xffu;
    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    return (value == 0 ? 0x40u : 0u) | ((value >> (bits - 8)) & 0x80u) | (~p & 1u) << 2;
}

/* Shift / rotate group 2 (C0, C1, D0-D3) as XRIP's op_grp2_8/16. ctl:
 * bits 0-2 the operation (/reg), bit 3 16-bit, bits 8-15 the count. A
 * count of 0 (mod 32) changes nothing, not even the lazy flags; otherwise
 * the flags end up resolved in rt->eflags. Called from generated code. */
uint32_t XLAT_HOT_FN(xlat_rt_shift)(xlat_rt_t *rt, uint32_t value, uint32_t ctl)
{
    xlat_rt_pending(rt);
    const uint32_t op = ctl & 7u, bits = ctl & 8u ? 16u : 8u, mask = (1u << bits) - 1u;
    const uint32_t cnt = (ctl >> 8) & 0x1fu;
    uint32_t s = value & mask;
    if (!cnt)
        return s;
    resolve_rt(rt);
    uint32_t cf = rt->eflags & 1u, of = (rt->eflags >> 11) & 1u, flags_szp = 0;
    bool set_szp = false;
    const uint32_t top = bits - 1u;
    switch (op) {
    case 0: {   /* ROL */
        const uint32_t k = cnt & top;
        s = ((s << k) | (s >> ((bits - k) & top))) & mask;
        cf = s & 1u;
        of = cf ^ ((s >> top) & 1u);
        break;
    }
    case 1: {   /* ROR */
        const uint32_t k = cnt & top;
        s = ((s >> k) | (s << ((bits - k) & top))) & mask;
        cf = s >> top;
        of = (s >> top) ^ ((s >> (top - 1u)) & 1u);
        break;
    }
    case 2: {   /* RCL: rotate [operand:CF] */
        const uint32_t k = bits == 8 ? cnt % 9u : (cnt >= 17 ? cnt - 17 : cnt);
        const uint32_t ring = (s << 1) | cf, wide = (1u << (bits + 1)) - 1u;
        const uint32_t sh = ((ring << k) | (ring >> (bits + 1 - k))) & wide;
        s = sh >> 1;
        cf = sh & 1u;
        of = cf ^ ((s >> top) & 1u);
        break;
    }
    case 3: {   /* RCR: rotate [CF:operand] */
        const uint32_t k = bits == 8 ? cnt % 9u : (cnt >= 17 ? cnt - 17 : cnt);
        const uint32_t ring = (cf << bits) | s, wide = (1u << (bits + 1)) - 1u;
        const uint32_t sh = ((ring >> k) | (ring << (bits + 1 - k))) & wide;
        s = sh & mask;
        cf = sh >> bits;
        of = (s >> top) ^ ((s >> (top - 1u)) & 1u);
        break;
    }
    case 4:
    case 6:     /* SHL */
        cf = cnt <= bits ? (s >> (bits - cnt)) & 1u : 0u;
        s = (s << cnt) & mask;
        of = cf ^ (s >> top);
        set_szp = true;
        break;
    case 5:     /* SHR */
        of = cnt == 1 && (s >> top);
        cf = cnt <= bits ? (s >> (cnt - 1u)) & 1u : 0u;
        s >>= cnt;
        set_szp = true;
        break;
    default: {  /* SAR */
        const int32_t sv = bits == 8 ? (int32_t)(int8_t)s : (int32_t)(int16_t)s;
        cf = (s >> ((cnt <= bits ? cnt : bits) - 1u)) & 1u;
        s = (uint32_t)(sv >> cnt) & mask;
        of = 0;
        set_szp = true;
        break;
    }
    }
    uint32_t f = (rt->eflags & ~0x801u) | cf | of << 11;
    if (set_szp) {
        flags_szp = szp(s, (int)bits);
        f = (f & ~0xc4u) | flags_szp;
    }
    rt->eflags = f;
    cf_mirror(rt);
    return s;
}

/* MUL / IMUL / DIV / IDIV (F6, F7 /4-/7) and IMUL r, r/m, imm (69, 6B) as
 * XRIP. ctl: bits 0-3 the operation (4-7 as /reg, 8 the three-operand IMUL),
 * bit 4 16-bit. axdx: AX | DX << 16, or the immediate for IMUL r, r/m, imm.
 * Returns the new AX | DX << 16 (the destination for the three-operand form)
 * in the low word; the high word is 1 when DIV / IDIV fault (#DE: nothing
 * changed, XRIP runs the instruction). DIV / IDIV leave the flags alone. */
uint64_t XLAT_HOT_FN(xlat_rt_muldiv)(xlat_rt_t *rt, uint32_t ctl, uint32_t operand, uint32_t axdx)
{
    xlat_rt_pending(rt);
    const uint32_t op = ctl & 15u;
    const bool w = (ctl & 16u) != 0;
    uint32_t ax = axdx & 0xffffu, dx = axdx >> 16, out;
    bool cfof;
    switch (op) {
    case 4:     /* MUL */
        if (w) {
            const uint32_t t = (operand & 0xffffu) * ax;
            out = t;
            resolve_rt(rt);
            rt->eflags = (rt->eflags & ~0xc4u) | szp(t & 0xffffu, 16);
            cfof = (t >> 16) != 0;
        } else {
            const uint32_t t = (operand & 0xffu) * (ax & 0xffu);
            out = t | dx << 16;
            resolve_rt(rt);
            rt->eflags = (rt->eflags & ~0xc4u) | szp(t & 0xffu, 8);
            cfof = (t >> 8) != 0;
        }
        break;
    case 5:     /* IMUL */
        if (w) {
            const int32_t t = (int32_t)(int16_t)ax * (int32_t)(int16_t)operand;
            out = (uint32_t)t;
            cfof = t != (int32_t)(int16_t)t;
        } else {
            const int32_t t = (int32_t)(int8_t)ax * (int32_t)(int8_t)operand;
            out = ((uint32_t)t & 0xffffu) | dx << 16;
            cfof = (int16_t)t != (int16_t)(int8_t)t;
        }
        resolve_rt(rt);
        break;
    case 6:     /* DIV */
        if (w) {
            const uint32_t v = ax | dx << 16, d = operand & 0xffffu;
            if (!d || v / d > 0xffffu)
                return 1ull << 32;
            return (uint64_t)((v / d) | (v % d) << 16);
        } else {
            const uint32_t d = operand & 0xffu;
            if (!d || ax / d > 0xffu)
                return 1ull << 32;
            return (uint64_t)((ax / d) | (ax % d) << 8 | dx << 16);
        }
    case 7:     /* IDIV */
        if (w) {
            const int32_t v = (int32_t)(ax | dx << 16), d = (int16_t)operand;
            if (!d || (v == INT32_MIN && d == -1))
                return 1ull << 32;
            const int32_t q = v / d, r = v % d;
            if (q < -32768 || q > 32767)
                return 1ull << 32;
            return (uint64_t)(((uint32_t)q & 0xffffu) | ((uint32_t)r & 0xffffu) << 16);
        } else {
            const int32_t v = (int16_t)ax, d = (int8_t)operand;
            if (!d)
                return 1ull << 32;
            const int32_t q = v / d, r = v % d;
            if (q < -128 || q > 127)
                return 1ull << 32;
            return (uint64_t)(((uint32_t)q & 0xffu) | ((uint32_t)r & 0xffu) << 8 | dx << 16);
        }
    default: {  /* IMUL r16, r/m16, imm */
        const int32_t t = (int32_t)(int16_t)operand * (int32_t)(int16_t)axdx;
        out = (uint32_t)t & 0xffffu;
        cfof = t != (int32_t)(int16_t)t;
        resolve_rt(rt);
        break;
    }
    }
    rt->eflags = cfof ? rt->eflags | 0x801u : rt->eflags & ~0x801u;
    cf_mirror(rt);
    return out;
}

#if XLAT_LOG
#include "ff.h"

/* Called by DOS when a program has ended: no guest code and no FatFs call in
 * progress, so the SD card can be written here. Appends what was collected
 * since the previous call to SD_DATA_DIR/xlat.log. */
void xlat_log_write(uint16_t psp, const char *name)
{
    static FIL lf;
    static int state;      /* 0 unopened, 1 open, 2 failed */
    static uint32_t last_translations, last_entries;
    if (!st || state == 2)
        return;
    if (xlat_stats.translations == last_translations && xlat_stats.entries == last_entries &&
        !st->log_len)
        return;
    last_translations = xlat_stats.translations;
    last_entries = xlat_stats.entries;
    log_printf("== exit PSP %04X %s ==\r\n", psp, name);
    /* in two parts: log_printf() formats at most 255 characters */
    log_printf("S translations=%lu rejects=%lu entries=%lu invalid=%lu flushes=%lu evict=%lu "
               "exit b/bu/s/st=%lu/%lu/%lu/%lu native=%lu code=%lu stack_skips=%lu ",
               (unsigned long)xlat_stats.translations, (unsigned long)xlat_stats.rejects,
               (unsigned long)xlat_stats.entries, (unsigned long)xlat_stats.invalidations,
               (unsigned long)xlat_stats.flushes, (unsigned long)xlat_stats.evictions,
               (unsigned long)xlat_stats.exits[0],
               (unsigned long)xlat_stats.exits[1], (unsigned long)xlat_stats.exits[2],
               (unsigned long)xlat_stats.exits[3], (unsigned long)xlat_stats.guest_insns, (unsigned long)xlat_stats.code_bytes,
               (unsigned long)xlat_stats.stack_skips);
    log_printf("interp=%lu side=%lu cuts=%lu/%lu cold=%lu fn=%lu xb=%lu xe=%lu links=%lu evs=%lu rr=%lu lost=%lu skipped=%lu\r\n",
               (unsigned long)xlat_stats.interp,
               (unsigned long)xlat_stats.side_interp, (unsigned long)xlat_stats.cut_entries,
               (unsigned long)xlat_stats.cuts, (unsigned long)xlat_stats.cold,
               (unsigned long)xlat_stats.fn_regions, (unsigned long)xlat_stats.exit_built,
               (unsigned long)xlat_stats.entries_added,
               (unsigned long)xlat_stats.links, (unsigned long)xlat_stats.slot_evictions,
               (unsigned long)xlat_stats.ring_retries, (unsigned long)st->log_lost,
               (unsigned long)st->log_skipped);
    log_printf("Q ring=%luK tus=%lu tmax=%lu retry=%lu/%lu gap=%lu defer=%lu disk_irq=%lu resume=%lu cancel=%lu qdrop=%lu\r\n",
               (unsigned long)(st->arena.size >> 10), (unsigned long)xlat_stats.translate_us,
               (unsigned long)xlat_stats.translate_max_us, (unsigned long)xlat_stats.retries_built,
               (unsigned long)xlat_stats.retries, (unsigned long)xlat_stats.gap_calls,
               (unsigned long)xlat_stats.compile_defers, (unsigned long)xlat_stats.disk_irq_yields,
               (unsigned long)xlat_stats.compile_resumes, (unsigned long)xlat_stats.compile_cancel,
               (unsigned long)xlat_stats.compile_queue_drops);
#if XLAT_STACKFUL
    log_printf("HW psp_done=%lu psp_irq=%lu psp_other=%lu psp_start_cancel=%lu\r\n",
               (unsigned long)xlat_stats.psp_done, (unsigned long)xlat_stats.psp_irq,
               (unsigned long)xlat_stats.psp_other, (unsigned long)xlat_stats.psp_start_cancel);
#endif
    log_printf("I int_direct=%lu bios_leaf=%lu local_checks=%lu local_invalid=%lu local_candidates=%lu gap_candidates=%lu rom_dispatch=%lu\r\n",
               (unsigned long)xlat_stats.int_direct, (unsigned long)xlat_stats.int_leaf,
               (unsigned long)xlat_stats.local_checks, (unsigned long)xlat_stats.local_invalid,
               (unsigned long)xlat_stats.local_candidates, (unsigned long)xlat_stats.gap_candidates,
               (unsigned long)xlat_stats.rom_dispatch);
    log_printf("N string_calls=%lu string_elems=%lu shadow_pop=%lu fast_string_calls=%lu fast_string_elems=%lu shadow_fast=%lu gap_reuse=%lu\r\n",
               (unsigned long)xlat_stats.string_calls, (unsigned long)xlat_stats.string_elems,
               (unsigned long)xlat_stats.shadow_pop, (unsigned long)xlat_stats.fast_string_calls,
               (unsigned long)xlat_stats.fast_string_elems, (unsigned long)xlat_stats.shadow_fast,
               (unsigned long)xlat_stats.gap_reuse);
    log_printf("L rep_yield=%lu shadow_umb=%lu shadow_mem_miss=%lu shadow_irq_miss=%lu thunk_int=%lu thunk_retf=%lu link_alt=%lu bios_flags_umb=%lu\r\n",
               (unsigned long)xlat_stats.rep_yield, (unsigned long)xlat_stats.shadow_umb,
               (unsigned long)xlat_stats.shadow_mem_miss, (unsigned long)xlat_stats.shadow_irq_miss,
               (unsigned long)xlat_stats.thunk_int, (unsigned long)xlat_stats.thunk_retf,
               (unsigned long)xlat_stats.link_alt, (unsigned long)xlat_stats.bios_flags_umb);
    log_printf("Y bus_read=%lu bus_write=%lu bus_smc=%lu bus_write_miss=%lu bus_pop=%lu bus_rmw=%lu string_preempt=%lu bios_write_leaf=%lu bios_cursor_local=%lu\r\n",
               (unsigned long)xlat_stats.bus_read, (unsigned long)xlat_stats.bus_write,
               (unsigned long)xlat_stats.bus_smc, (unsigned long)xlat_stats.bus_write_miss,
               (unsigned long)xlat_stats.bus_pop, (unsigned long)xlat_stats.bus_rmw,
               (unsigned long)xlat_stats.string_preempt,
               (unsigned long)xlat_stats.bios_write_leaf, (unsigned long)xlat_stats.bios_cursor_local);
    log_printf("Z scalar_resume=%lu scalar_dispatch=%lu scalar_refused=%lu scalar_stop=%lu compiled_code=%lu compiled_source=%lu\r\n",
               (unsigned long)xlat_stats.scalar_resume, (unsigned long)xlat_stats.scalar_dispatch,
               (unsigned long)xlat_stats.scalar_refused, (unsigned long)xlat_stats.scalar_stop,
               (unsigned long)xlat_stats.compiled_code_bytes, (unsigned long)xlat_stats.compiled_source_bytes);
    log_printf("K epoch_start=%lu epoch_helper=%lu revalidations=%lu revalidate_bytes=%lu service_clean=%lu service_foreign=%lu host_local=%lu host_unknown=%lu\r\n",
               (unsigned long)xlat_stats.epoch_start, (unsigned long)xlat_stats.epoch_helper,
               (unsigned long)xlat_stats.revalidations, (unsigned long)xlat_stats.revalidate_bytes,
               (unsigned long)xlat_stats.service_clean, (unsigned long)xlat_stats.service_foreign,
               (unsigned long)xlat_stats.host_local, (unsigned long)xlat_stats.host_unknown);
    /* CPU bursts: averages and histograms (xlat_stats_t), XRIP only / with
     * translated code; device service after the bursts. XLAT_SLICE_US is
     * meant to match the usual XRIP burst. */
    const xlat_stats_t *const b = &xlat_stats;
    log_printf("B xrip n=%lu us=%lu insns=%lu us/4096=%lu  xlat n=%lu us=%lu insns=%lu slice=%u\r\n",
               (unsigned long)b->xrip_bursts,
               (unsigned long)(b->xrip_bursts ? (uint32_t)(b->xrip_us / b->xrip_bursts) : 0),
               (unsigned long)(b->xrip_bursts ? (uint32_t)(b->xrip_insns / b->xrip_bursts) : 0),
               (unsigned long)(b->xrip_insns ? (uint32_t)(b->xrip_us * 4096u / b->xrip_insns) : 0),
               (unsigned long)b->xlat_bursts,
               (unsigned long)(b->xlat_bursts ? (uint32_t)(b->xlat_us / b->xlat_bursts) : 0),
               (unsigned long)(b->xlat_bursts ? (uint32_t)(b->xlat_insns / b->xlat_bursts) : 0),
               (unsigned)XLAT_SLICE_US);
    /* the bursts with translated code split: XRIP (run_instructions) and the
     * rest (xlat_run: translated code, helpers, the translations) */
    log_printf("M ms=%lu insns=%lu xrip ms=%lu insns=%lu rest ms=%lu insns=%lu\r\n",
               (unsigned long)(b->xlat_us / 1000u), (unsigned long)b->xlat_insns,
               (unsigned long)(b->mix_xrip_us / 1000u), (unsigned long)b->mix_xrip_insns,
               (unsigned long)((b->xlat_us - b->mix_xrip_us) / 1000u),
               (unsigned long)(b->xlat_insns - b->mix_xrip_insns));
    static const char *const hist_names[3] = {"xrip", "xlat", "svc"};
    const uint32_t *const hists[3] = {b->xrip_hist, b->xlat_hist, b->svc_hist};
    for (int k = 0; k < 3; ++k) {
        const uint32_t *h = hists[k];
        log_printf("H %s <50:%lu <100:%lu <200:%lu <400:%lu <800:%lu <1600:%lu <3200:%lu more:%lu\r\n",
                   hist_names[k], (unsigned long)h[0], (unsigned long)h[1], (unsigned long)h[2],
                   (unsigned long)h[3], (unsigned long)h[4], (unsigned long)h[5],
                   (unsigned long)h[6], (unsigned long)h[7]);
    }
    log_printf("V svc n=%lu us=%lu max=%lu\r\n", (unsigned long)b->svc_n,
               (unsigned long)(b->svc_n ? (uint32_t)(b->svc_us / b->svc_n) : 0),
               (unsigned long)b->svc_max);
    /* then the details, the region counts last: they fill what room is left */
    {
        const uint32_t *w = xlat_stats.enter_why;
        log_printf("E start ran/none/irq/mode/busy=%lu/%lu/%lu/%lu/%lu cut ran/none/irq/mode=%lu/%lu/%lu/%lu "
                   "xrip_tail=%lu\r\n",
                   (unsigned long)w[XLAT_ENTER_S_RAN], (unsigned long)w[XLAT_ENTER_S_NONE],
                   (unsigned long)w[XLAT_ENTER_S_MOMENT], (unsigned long)w[XLAT_ENTER_S_MODE],
                   (unsigned long)w[XLAT_ENTER_S_BUSY], (unsigned long)w[XLAT_ENTER_C_RAN],
                   (unsigned long)w[XLAT_ENTER_C_NONE], (unsigned long)w[XLAT_ENTER_C_MOMENT],
                   (unsigned long)w[XLAT_ENTER_C_MODE], (unsigned long)xlat_stats.xrip_tail);
        memset(xlat_stats.enter_why, 0, sizeof(xlat_stats.enter_why));
        xlat_stats.xrip_tail = 0;
    }
    /* Interval totals, including regions whose byte dumps did not fit.
     * first = completed without suspension. Retired time-cut fields are 0. */
    log_printf("D limit=%u soft=%u quantum_us=%u n:built/first/fit/quanta/us/arm/cut/cont/final_us\r\n",
               (unsigned)XLAT_MAX_INSNS, (unsigned)XLAT_MAX_INSNS, 0u);
    for (unsigned n = 1; n <= XLAT_MAX_INSNS; ++n) {
        if (!st->compile_len[n].built)
            continue;
        log_printf("D %u:%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu\r\n", n,
                   (unsigned long)st->compile_len[n].built,
                   (unsigned long)st->compile_len[n].first,
                   0ul,
                   (unsigned long)st->compile_len[n].quanta,
                   (unsigned long)st->compile_len[n].us,
                   (unsigned long)st->compile_len[n].arm,
                   0ul, 0ul,
                   (unsigned long)st->compile_len[n].final_us);
    }
    memset(st->compile_len, 0, sizeof(st->compile_len));
    log_pages();
    log_rejects();
    log_prof();
    log_region_counts();
    xlat_stats.xrip_bursts = xlat_stats.xlat_bursts = 0;
    xlat_stats.xrip_us = xlat_stats.xrip_insns = xlat_stats.xlat_us = xlat_stats.xlat_insns = 0;
    xlat_stats.mix_xrip_us = xlat_stats.mix_xrip_insns = 0;
    memset(xlat_stats.xrip_hist, 0, sizeof(xlat_stats.xrip_hist));
    memset(xlat_stats.xlat_hist, 0, sizeof(xlat_stats.xlat_hist));
    memset(xlat_stats.svc_hist, 0, sizeof(xlat_stats.svc_hist));
    xlat_stats.svc_n = xlat_stats.svc_max = 0;
    xlat_stats.svc_us = 0;
    if (state == 0)
        state = f_open(&lf, SD_DATA_DIR_SLASH "xlat.log",
                       FA_WRITE | FA_OPEN_APPEND | FA_OPEN_ALWAYS) == FR_OK ? 1 : 2;
    if (state == 1) {
        UINT bw = 0;
        f_write(&lf, st->log, st->log_len, &bw);
        f_sync(&lf);
    }
    st->log_len = 0;
    st->log_lost = 0;
    st->log_skipped = 0;
}
#endif

#if XLAT_LOG
void xlat_note_service(uint32_t us)
{
    xlat_stats.svc_n++;
    xlat_stats.svc_us += us;
    if (us > xlat_stats.svc_max)
        xlat_stats.svc_max = us;
    xlat_stats.svc_hist[xlat_us_bucket(us)]++;
}
#endif

/* Called through xlat_interp_one() after an instruction that may have
 * written memory: the running region must still match its x86 bytes. */
bool XLAT_HOT_FN(xlat_region_still_valid)(xlat_rt_t *rt)
{
    if (flush_pending)
        return false;
    region_t *r = rt->cur < XLAT_MAX_REGIONS ? &st->region[rt->cur] : NULL;
    if (xlat_write_hi)
        validate_local_writes(rt);
    if (r && !r->live)
        return false;
    if (!xlat_dirty)
        return true;
    xlat_dirty = false;
    /* the other regions are compared at their next entry (through
     * xlat_run(): the link table entries are for the old epoch) */
    rt->epoch = ++st->epoch;
#if XLAT_LOG
    ++xlat_stats.epoch_helper;
#endif
    rt->hit_lo = rt->hit_hi = 0;
    if (!r || region_valid(r, rt)) {
        if (r)
            r->epoch = st->epoch;
        return true;
    }
    r->live = 0;
    gap_reset();
    unlink_region((int)rt->cur);
    xlat_stats.invalidations++;
    return false;
}
