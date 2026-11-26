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
 * code, then the copies of its x86 bytes), placed at the ring head. Before a
 * translation the regions in the XLAT_RING_WANT bytes ahead of the head are
 * evicted (as jit_make_room() in frank-386's jit16); a translation that does
 * not fit evicts up to the end of the arena, then wraps to the ring start.
 * Evicting a region drops its entries and rebuilds the entry and code maps
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
    uint32_t exits[XLAT_EXIT_COUNT];
    uint32_t epoch;        /* st->epoch when its bytes were last compared */
    uint32_t mem_size;     /* rt->mem_size it was built for (reads compare with it) */
    uint8_t trace_min;     /* xlat_translate's trace_min it was built with */
    uint8_t n_extra;       /* entry points added (add_entry) */
    uint16_t extra[XLAT_EXTRA_MAX];
} region_t;

typedef struct {
    uint32_t key;          /* cs << 16 | ip */
    uint16_t code_off;     /* arena offset of the entry label */
    uint8_t region;
    uint8_t used;          /* E_* */
} entry_t;

/* entry_t.used: E_FREE ends a probe chain, E_DEAD (an evicted region's
 * entry) does not; E_MARK: set in xlat_entry_map. */
enum { E_FREE = 0, E_DEAD = 1, E_LIVE = 2, E_MARK = 4 };

typedef struct {
    uint32_t key;
    uint8_t count;
    uint8_t reject;
} hot_t;

/* XLAT_LOG: hot entry points that got no region, the most often seen kept. */
#define XLAT_REJ_SLOTS 16
#define XLAT_REJ_BYTES 32
typedef struct {
    uint32_t key;
    uint32_t hits;          /* bursts that started there afterwards */
    uint8_t why, n, n_interp, used;
    uint8_t b[XLAT_REJ_BYTES];
} rej_t;

typedef struct {
    xlat_arena_t arena;
    region_t region[XLAT_MAX_REGIONS];
    uint32_t last[XLAT_MAX_REGIONS];  /* st->seq at each region's last entry (take_slot);
                                         written by the dispatcher too (rt->last) */
    xlat_link_t link[XLAT_LINK_SLOTS];  /* entries the dispatcher may make (rt->link) */
    uint32_t seq;           /* xlat_run() region entries so far */
    uint32_t ring_lo;       /* first byte after the trampoline */
    uint32_t head;          /* where the next chunk goes */
    uint32_t epoch;         /* changes whenever x86 code may have been written */
    uint32_t mem_size;      /* rt->mem_size of the last xlat_run() */
    entry_t entry[XLAT_ENTRY_SLOTS];
    hot_t hot[XLAT_HOT_SLOTS];       /* burst-start samples */
    hot_t exit_hot[XLAT_HOT_SLOTS];  /* untranslated region exit targets */
    xlat_region_out_t out;  /* translator output, kept here to spare the stack */
    void *ctx;              /* translator working memory */
#if XLAT_LOG
    char *log;              /* text kept until the next safe point (xlat_log_write) */
    uint32_t log_len;
    uint32_t log_lost;      /* bytes that did not fit */
    uint32_t log_skipped;   /* region dumps left out (XLAT_LOG_RESERVE) */
    rej_t rej[XLAT_REJ_SLOTS];
    uint32_t rej_why[XLAT_RJ_COUNT];
#endif
} state_t;

#ifndef XLAT_LOG
#define XLAT_LOG 0
#endif
#define XLAT_LOG_SIZE (XLAT_LOG ? 32768u : 0u)
/* The region dumps (T lines, C lines of evicted regions) stop this far
 * from the end: the summary at the program's exit always fits. */
#define XLAT_LOG_RESERVE 8192u

#define ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

#if XLAT_ARENA_SIZE > 0x10000
#error "entry_t.code_off is 16-bit"
#endif

static state_t *st;
static bool disabled;

uint8_t xlat_entry_map[XLAT_MAP_BITS / 8];
uint8_t xlat_code_map[XLAT_CODE_MAP_BYTES];
bool xlat_dirty = true;
uint32_t xlat_call_key[XLAT_CALL_SLOTS];
uint8_t xlat_call_count[XLAT_CALL_SLOTS];

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
    log_printf("T %04X:%04X lo=%05lX span=%lu runs=%u\r\n", r->cs, r->ip,
               (unsigned long)r->lo, (unsigned long)r->span, r->n_runs);
    const uint8_t *d = st->arena.base + r->data_off;
    for (unsigned k = 0; k < r->n_runs; ++k) {
        uint32_t linear;
        memcpy(&linear, d, 4);
        const uint32_t len = d[4] | (uint32_t)d[5] << 8;
        const uint8_t *b = d + 8;
        for (uint32_t i = 0; i < len; i += 16) {
            char hex[16 * 3 + 1];
            uint32_t n = len - i < 16 ? len - i : 16;
            for (uint32_t j = 0; j < n; ++j) {
                static const char digits[] = "0123456789ABCDEF";
                hex[j * 3] = ' ';
                hex[j * 3 + 1] = digits[b[i + j] >> 4];
                hex[j * 3 + 2] = digits[b[i + j] & 15];
            }
            hex[n * 3] = 0;
            log_printf("  %04X:%04X%s\r\n", r->cs, (unsigned)(linear - r->cs_base + i), hex);
        }
        d += 8 + ((len + 3u) & ~3u);
    }
}

static void log_region_count(const region_t *r)
{
    {
        log_printf("C %04X:%04X entries=%lu exit branch/budget/side/stop=%lu/%lu/%lu/%lu ins=%lu us=%lu%s\r\n",
                   r->cs, r->ip, (unsigned long)r->entries, (unsigned long)r->exits[0],
                   (unsigned long)r->exits[1], (unsigned long)r->exits[2],
                   (unsigned long)r->exits[3], (unsigned long)r->insns, (unsigned long)r->us,
                   r->live ? (r->fn ? " fn" : "") : r->cold ? " (cold)" : " (invalidated)");
    }
}

static void log_region_counts(void)
{
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k)
        if (st->region[k].held)
            log_region_count(&st->region[k]);
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
        slot->b[i] = linear + i < rt->mem_size ? rt->mem[linear + i] : 0;
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
#else
#define log_region_code(r) ((void)0)
#define log_region_counts() ((void)0)
#define log_region_count(r) ((void)0)
#define log_dump_room() false
#define note_reject(rt, key, why) ((void)0)
#define note_reject_hit(key) ((void)0)
#endif

static void reset_tables(void)
{
    log_region_counts();    /* the regions are about to go */
    memset(st->region, 0, sizeof(st->region));
    memset(st->last, 0, sizeof(st->last));
    for (unsigned k = 0; k < XLAT_LINK_SLOTS; ++k)
        st->link[k].key = UINT32_MAX;
    memset(st->entry, 0, sizeof(st->entry));
    memset(st->exit_hot, 0, sizeof(st->exit_hot));
    for (int k = 0; k < XLAT_HOT_SLOTS; ++k)
        st->hot[k].reject = 0;
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    memset(xlat_code_map, 0, sizeof(xlat_code_map));
    xlat_emit_trampoline(&st->arena);
    xlat_sync_code(st->arena.base, st->arena.code_top);
    st->ring_lo = st->head = (st->arena.code_top + 7u) & ~7u;
    xlat_stats.code_bytes = st->arena.code_top;
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
#else
    const size_t arena_off = ALIGN8(ctx_off + xlat_translate_ctx_size());
    const size_t log_off = arena_off + XLAT_ARENA_SIZE;
#endif
    uint8_t *block = (uint8_t *)XLAT_ALLOC(log_off + XLAT_LOG_SIZE);
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
    st->arena.size = XLAT_ARENA_SIZE;
#if XLAT_LOG
    st->log = (char *)block + log_off;
#endif
    reset_tables();
    return true;
}

void xlat_release(void)
{
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

static bool insert(uint32_t key, uint8_t region, uint32_t code_off, bool mark)
{
    const uint32_t h = hash32(key) >> (32 - XLAT_ENTRY_BITS);
    for (uint32_t k = 0; k < 8; ++k) {
        entry_t *e = &st->entry[(h + k) & (XLAT_ENTRY_SLOTS - 1)];
        if (e->used <= E_DEAD || e->key == key || !st->region[e->region].live) {
            /* an entry of the same key further on is shadowed (lookup takes
             * the first); it comes back if this region is evicted */
            e->key = key;
            e->code_off = (uint16_t)code_off;
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
static bool XLAT_HOT_FN(region_valid)(const region_t *r, const xlat_rt_t *rt)
{
    if (rt->seg_base[1] != r->cs_base || rt->mem_size != r->mem_size)
        return false;
    const uint8_t *d = st->arena.base + r->data_off;
    for (unsigned k = 0; k < r->n_runs; ++k) {
        const uint32_t linear = load32(d);
        const uint32_t len = d[4] | (uint32_t)d[5] << 8;
        const uint8_t *copy = d + 8;
        if (linear + len > rt->mem_size)
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

/* The dispatcher no longer enters region k (dropped, cold or evicted). */
static void unlink_region(int k)
{
    for (unsigned j = 0; j < XLAT_LINK_SLOTS; ++j)
        if (st->link[j].region == k)
            st->link[j].key = UINT32_MAX;   /* no IP is 0xffff with CS 0xffff here */
}

/* Drop region k: its entries go (the chunk is reused when the head comes
 * to it). The maps are rebuilt by the caller (rebuild_maps). */
static void evict(int k)
{
    region_t *r = &st->region[k];
    if (log_dump_room())
        log_region_count(r);
    unlink_region(k);
    for (int j = 0; j < XLAT_ENTRY_SLOTS; ++j) {
        entry_t *e = &st->entry[j];
        if (e->used >= E_LIVE && e->region == k)
            e->used = E_DEAD;
    }
    memset(r, 0, sizeof(*r));
    xlat_stats.evictions++;
}

/* xlat_entry_map and xlat_code_map from the regions left. */
static void rebuild_maps(void)
{
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    memset(xlat_code_map, 0, sizeof(xlat_code_map));
    for (int j = 0; j < XLAT_ENTRY_SLOTS; ++j) {
        const entry_t *e = &st->entry[j];
        if ((e->used & (E_LIVE | E_MARK)) == (E_LIVE | E_MARK)) {
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

/* Evict the regions whose chunks meet [lo, hi). */
static bool evict_range(uint32_t lo, uint32_t hi)
{
    bool any = false;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (r->held && r->chunk_lo < hi && r->chunk_hi > lo) {
            evict(k);
            any = true;
        }
    }
    return any;
}

/* End of the free space from the head: the next chunk, or the arena end. */
static uint32_t window_end(void)
{
    uint32_t end = st->arena.size;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (r->held && r->chunk_lo >= st->head && r->chunk_lo < end)
            end = r->chunk_lo;
    }
    return end;
}

/* A free region slot: an empty one, else an invalidated one, else the
 * region entered longest ago (a hot loop translated early stays). */
static int take_slot(bool *evicted)
{
    int best = -1;
    uint32_t best_d = UINT32_MAX;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k) {
        const region_t *r = &st->region[k];
        if (!r->held)
            return k;
        const uint32_t age = st->seq - st->last[k];
        const uint32_t d = !r->live && !r->cold ? 0u : UINT32_MAX - age;
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    evict(best);
    *evicted = true;
    return best;
}

/* Translate at rt->cs:ip (with extra entry points); the new region's slot,
 * or -1. */
static int translate_here(const xlat_rt_t *rt, uint16_t ip, int trace_min, const uint16_t *extra,
                          int n_extra)
{
    xlat_region_out_t *out = &st->out;
    xlat_arena_t *const a = &st->arena;
    bool full, evicted = false;
    /* First the free space at the head as it is (a region rejected for
     * another reason evicts nothing). When it does not fit: evict
     * XLAT_RING_WANT bytes ahead of the head (from the ring start when the
     * arena ends sooner), then up to the arena end, then the whole ring. */
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t hi = a->size;
        if (attempt == 1) {
            if (st->head + XLAT_RING_WANT > a->size)
                st->head = st->ring_lo;
            hi = st->head + XLAT_RING_WANT < a->size ? st->head + XLAT_RING_WANT : a->size;
        } else if (attempt == 3) {
            if (st->head == st->ring_lo)
                break;          /* attempt 2 had the whole ring */
            st->head = st->ring_lo;
        }
        if (attempt)
            evicted |= evict_range(st->head, hi);
        a->code_top = st->head;
        a->data_bottom = window_end();
        if (xlat_translate(a, rt, ip, trace_min, extra, n_extra, out, &full, st->ctx))
            goto built;
        if (!full)
            break;
    }
    if (evicted)
        rebuild_maps();
    return -1;

built:;
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
    uint8_t *d = st->arena.base + r->data_off;
    for (unsigned k = 0; k < out->n_runs; ++k) {
        const uint32_t linear = r->cs_base + out->runs[k].ip;
        const uint32_t len = out->runs[k].len;
        __builtin_memcpy(d, &linear, 4);
        d[4] = (uint8_t)len;
        d[5] = (uint8_t)(len >> 8);
        d[6] = d[7] = 0;
        for (uint32_t i = 0; i < len; ++i)
            d[8 + i] = rt->mem[linear + i];
        /* the blocks a write may reach the run from (up to 16 bytes long) */
        const uint32_t from = linear >= 15u ? linear - 15u : 0u;
        for (uint32_t b = from >> XLAT_CODE_BLOCK_SHIFT;
             b <= (linear + len - 1u) >> XLAT_CODE_BLOCK_SHIFT && b < XLAT_CODE_MAP_BYTES * 8u; ++b)
            xlat_code_map[b >> 3] |= (uint8_t)(1u << (b & 7u));
        d += 8 + ((len + 3u) & ~3u);
    }
    r->live = 1;
    r->epoch = st->epoch;
    if (evicted)
        rebuild_maps();    /* before the new marks: they are set below */
    for (unsigned k = 0; k < out->n_entries; ++k) {
        insert((uint32_t)rt->cs << 16 | out->entries[k].ip, (uint8_t)idx,
               out->entries[k].code_off, out->entries[k].mark);
        if (out->entries[k].mark) {
            const uint32_t m = xlat_map_index(r->cs_base + out->entries[k].ip);
            xlat_entry_map[m >> 3] |= (uint8_t)(1u << (m & 7u));
        }
    }
    xlat_sync_code(st->arena.base + out->code_off, out->code_len);
    log_region_code(r);
    xlat_stats.translations++;
    uint32_t used = st->ring_lo;
    for (int k = 0; k < XLAT_MAX_REGIONS; ++k)
        if (st->region[k].held)
            used += st->region[k].chunk_hi - st->region[k].chunk_lo;
    xlat_stats.code_bytes = used;
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

/* rt->ip lies in the code of live region k but is not one of its entries:
 * translate the region again with it as one more entry (instead of another
 * region with a copy of the same code), then drop the old one. */
static bool add_entry(const xlat_rt_t *rt, int k)
{
    region_t *r = &st->region[k];
    if (r->n_extra >= XLAT_EXTRA_MAX)
        return false;
    uint16_t extra[XLAT_EXTRA_MAX];
    const int n = r->n_extra;
    for (int j = 0; j < n; ++j)
        extra[j] = r->extra[j];
    extra[n] = (uint16_t)rt->ip;
    const uint32_t lo = r->chunk_lo;
    const uint16_t root = r->ip;
    const int idx = translate_here(rt, root, r->trace_min, extra, n + 1);
    if (idx < 0)
        return false;
    /* the old region, unless the ring took it already (its entries now
     * lead to the new one: insert() replaced them) */
    if (idx != k && r->held && r->chunk_lo == lo && r->ip == root) {
        evict(k);
        rebuild_maps();
    }
    xlat_stats.entries_added++;
    return true;
}

/* Called when no translation exists at rt->cs:ip. True when one was built. */
static __attribute__((noinline)) bool sample(const xlat_rt_t *rt)
{
    const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
    hot_t *h = &st->hot[hash32(key) >> (32 - 6)];
    /* a hot CALL target (xlat_call_hot) is translated at once */
    const bool fn = rt->fn_entry == rt->seg_base[1] + (rt->ip & 0xffffu);
    if (h->key != key) {
        h->key = key;
        h->count = 1;
        h->reject = 0;
        if (!fn)
            return false;
    } else if (h->reject) {
        note_reject_hit(key);
        return false;
    } else if (++h->count < XLAT_HOT_THRESHOLD && !fn) {
        return false;
    }
    h->count = 0;
    if (stack_free() < XLAT_STACK_NEEDED) {
        xlat_stats.stack_skips++;   /* try again when it is hot at a shallower depth */
        return false;
    }
    /* Inside a live region but not at one of its entries: one more entry
     * there. Else (inside a cold region) XRIP keeps it. */
    const int in_live = covering(rt);
    if (in_live >= 0 && add_entry(rt, in_live))
        return true;
    const bool inside = in_live >= 0 || covered(rt);
    if (inside || translate_here(rt, (uint16_t)rt->ip, fn ? XLAT_TRACE_MIN : 0, NULL, 0) < 0) {
        h->reject = 1;
        xlat_stats.rejects++;
        note_reject(rt, key, inside ? XLAT_RJ_COVERED : st->out.reject);
        return false;
    }
    return true;
}

/* Called when a region left (BRANCH) for rt->cs:ip and no translation is
 * there. A target reached so XLAT_EXIT_HOT times in a row (same slot) is
 * translated, without a loop too (XLAT_LINK_MIN): the exits to it then go on
 * there through the link table instead of returning to XRIP. Unlike a burst
 * start it may lie inside another region's code (a return site, a branch
 * target in the middle): the code is translated again from there. */
static __attribute__((noinline)) bool sample_exit(const xlat_rt_t *rt)
{
    if (!XLAT_TRACES)
        return false;
    const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
    hot_t *h = &st->exit_hot[hash32(key) >> (32 - 6)];
    if (h->key != key) {
        h->key = key;
        h->count = 1;
        h->reject = 0;
        return false;
    }
    if (h->reject || ++h->count < XLAT_EXIT_HOT)
        return false;
    h->count = 0;
    if (stack_free() < XLAT_STACK_NEEDED) {
        xlat_stats.stack_skips++;
        return false;
    }
    /* inside a live region: one more entry there (no second copy) */
    const int in_live = covering(rt);
    if (in_live >= 0 && add_entry(rt, in_live))
        return true;
    if (translate_here(rt, (uint16_t)rt->ip, XLAT_LINK_MIN, NULL, 0) < 0) {
        h->reject = 1;
        xlat_stats.rejects++;
        note_reject(rt, key, st->out.reject);
        return false;
    }
    xlat_stats.exit_built++;
    return true;
}

bool XLAT_HOT_FN(xlat_run)(xlat_rt_t *rt)
{
    if (!st) {
        /* Nothing is allocated until some entry point is seen twice in a
         * row at a burst start (no allocation during BIOS POST etc.). */
        static uint32_t first_key = UINT32_MAX;
        const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
        if (disabled || key != first_key) {
            first_key = key;
            return false;
        }
        if (!ready())
            return false;
    }
    const int32_t start = rt->budget;
    rt->code_map = xlat_code_map;
    /* Something but XRIP's ordinary writes ran since the last call (or XRIP
     * wrote into translated code): compare each region's bytes once */
    if (xlat_dirty || rt->mem_size != st->mem_size) {
        xlat_dirty = false;
        st->mem_size = rt->mem_size;   /* regions built for another size go */
        st->epoch++;
    }
    const xlat_tramp_fn tramp = (xlat_tramp_fn)(uintptr_t)(st->arena.base + 1);
    rt->link = st->link;
    rt->last = st->last;
    cf_mirror(rt);
    rt->cur = NO_REGION;
    rt->links = 0;
    bool ran = false, built = false;
    for (;;) {
        const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
        entry_t *e = lookup(key);
        if (!e || !st->region[e->region].live) {
            if (e && st->region[e->region].cold)
                break;      /* XRIP runs it: entering costs more than it saves */
            /* one translation per call: at the burst start, or where a
             * region left for code without a region */
            if (built || !(ran ? rt->exit_kind == XLAT_EXIT_BRANCH && sample_exit(rt) : sample(rt)))
                break;
            built = true;
            continue;
        }
        region_t *r = &st->region[e->region];
        if (r->epoch != st->epoch) {
            /* Since then XRIP may have written anywhere; translated code
             * cannot write into translated x86 code (xlat_code_map). */
            if (!region_valid(r, rt)) {
                r->live = 0;
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
        xlat_link_t *l = &st->link[xlat_link_hash(key)];
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
        xlat_stats.entries++;
        r->entries++;
        r->insns += (uint32_t)((b0 - rt->budget) + ((int32_t)rt->extra - x0)) + r->fn;
        /* A region left before its loops run (a loop body that calls or
         * jumps out every time) costs an entry and an exit per pass: after
         * XLAT_COLD_ENTRIES entries with fewer than XLAT_COLD_INSNS loop
         * instructions per entry, XRIP keeps it. (The time per instruction
         * is logged only: a REP string instruction counts as one.) */
        if (r->entries == XLAT_COLD_ENTRIES && r->insns < XLAT_COLD_ENTRIES * XLAT_COLD_INSNS) {
            r->live = 0;
            r->cold = 1;
            unlink_region(e->region);
            xlat_stats.cold++;
        }
        if (rt->exit_kind < XLAT_EXIT_COUNT) {
            xlat_stats.exits[rt->exit_kind]++;
            x->exits[rt->exit_kind]++;
        }
        /* STOP: XRIP must run the next instruction. */
        if (*rt->intr || rt->exit_kind == XLAT_EXIT_STOP)
            break;
        /* Budget used up (entry costs, short regions): the burst lasts
         * until its deadline, as in the back-edge stubs. */
        if (rt->budget <= 0) {
            if ((int32_t)(*rt->clock - rt->deadline) >= 0)
                break;
            rt->budget += XLAT_SLICE_CHECK;
            rt->extra += XLAT_SLICE_CHECK;
        }
    }
    xlat_rt_pending(rt);    /* XRIP takes the lazy flags as they are */
    if (ran)
        xlat_stats.guest_insns += (uint32_t)(start + (int32_t)rt->extra - rt->budget);
    xlat_stats.links += rt->links;
    xlat_stats.entries += rt->links;
    return ran;
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
    log_region_counts();
    log_rejects();
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
    log_printf("interp=%lu side=%lu cuts=%lu/%lu cold=%lu fn=%lu xb=%lu xe=%lu links=%lu lost=%lu skipped=%lu\r\n",
               (unsigned long)xlat_stats.interp,
               (unsigned long)xlat_stats.side_interp, (unsigned long)xlat_stats.cut_entries,
               (unsigned long)xlat_stats.cuts, (unsigned long)xlat_stats.cold,
               (unsigned long)xlat_stats.fn_regions, (unsigned long)xlat_stats.exit_built,
               (unsigned long)xlat_stats.entries_added,
               (unsigned long)xlat_stats.links, (unsigned long)st->log_lost,
               (unsigned long)st->log_skipped);
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
    xlat_stats.xrip_bursts = xlat_stats.xlat_bursts = 0;
    xlat_stats.xrip_us = xlat_stats.xrip_insns = xlat_stats.xlat_us = xlat_stats.xlat_insns = 0;
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
    /* Nothing wrote translated x86 code since xlat_run() started or the last
     * check (XRIP's writes into xlat_code_map blocks, native handlers, OUT,
     * x87 stores set xlat_dirty): nothing to compare. */
    if (!xlat_dirty)
        return true;
    xlat_dirty = false;
    region_t *r = rt->cur < XLAT_MAX_REGIONS ? &st->region[rt->cur] : NULL;
    /* the other regions are compared at their next entry (through
     * xlat_run(): the link table entries are for the old epoch) */
    rt->epoch = ++st->epoch;
    if (!r || region_valid(r, rt)) {
        if (r)
            r->epoch = st->epoch;
        return true;
    }
    r->live = 0;
    unlink_region((int)rt->cur);
    xlat_stats.invalidations++;
    return false;
}
