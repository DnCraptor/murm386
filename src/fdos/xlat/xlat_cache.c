/*
 * murm386 xlat: resident translation cache and burst-boundary dispatcher.
 *
 * Everything lives in one block allocated on first use: tables, the
 * translator's working context and the code arena. On the device the block is
 * a native PSRAM span (the pool ELF applications run from), not heap: the
 * SRAM heap is small and pico malloc() panics instead of returning NULL. If no
 * span is available the translator stays off. When the arena, the region
 * table or the entry table fills up, the whole cache is dropped.
 *
 * Hot spots are found by sampling: xlat_run() is called at the start of every
 * XRIP burst, and an entry point seen there XLAT_HOT_THRESHOLD times in a row
 * (same hash slot) is translated. A region that ran to its budget stops at a
 * back edge, i.e. at a region entry, so the next burst re-enters it at once.
 */
#include <string.h>

#include "xlat_internal.h"

xlat_stats_t xlat_stats;

typedef struct {
    uint32_t cs_base;
    uint32_t data_off;
    uint32_t lo, span;
    uint16_t cs;
    uint16_t n_runs;
    uint16_t ip;           /* the entry point it was built from */
    uint8_t live;
    uint32_t entries;
    uint32_t exits[XLAT_EXIT_COUNT];
} region_t;

typedef struct {
    uint32_t key;          /* cs << 16 | ip */
    uint16_t code_off;     /* arena offset of the entry label */
    uint8_t region;
    uint8_t used;
} entry_t;

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
    int n_regions;
    entry_t entry[XLAT_ENTRY_SLOTS];
    hot_t hot[XLAT_HOT_SLOTS];
    xlat_region_out_t out;  /* translator output, kept here to spare the stack */
    void *ctx;              /* translator working memory */
#if XLAT_LOG
    char *log;              /* text kept until the next safe point (xlat_log_write) */
    uint32_t log_len;
    uint32_t log_lost;      /* bytes that did not fit */
    rej_t rej[XLAT_REJ_SLOTS];
    uint32_t rej_why[XLAT_RJ_COUNT];
#endif
} state_t;

#ifndef XLAT_LOG
#define XLAT_LOG 0
#endif
#define XLAT_LOG_SIZE (XLAT_LOG ? 16384u : 0u)

#define ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

#if XLAT_ARENA_SIZE > 0x10000
#error "entry_t.code_off is 16-bit"
#endif

static state_t *st;
static bool disabled;

uint8_t xlat_entry_map[XLAT_MAP_BITS / 8];

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

/* The region running now (for xlat_region_still_valid). */
static region_t *cur_region;

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
static void log_region_code(const region_t *r)
{
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

static void log_region_counts(void)
{
    for (int k = 0; k < st->n_regions; ++k) {
        const region_t *r = &st->region[k];
        log_printf("C %04X:%04X entries=%lu exit branch/budget/side/stop=%lu/%lu/%lu/%lu%s\r\n",
                   r->cs, r->ip, (unsigned long)r->entries, (unsigned long)r->exits[0],
                   (unsigned long)r->exits[1], (unsigned long)r->exits[2],
                   (unsigned long)r->exits[3],
                   r->live ? "" : " (invalidated)");
    }
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
#define note_reject(rt, key, why) ((void)0)
#define note_reject_hit(key) ((void)0)
#endif

static void reset_tables(void)
{
    log_region_counts();    /* the regions are about to go */
    memset(st->region, 0, sizeof(st->region));
    memset(st->entry, 0, sizeof(st->entry));
    for (int k = 0; k < XLAT_HOT_SLOTS; ++k)
        st->hot[k].reject = 0;
    st->n_regions = 0;
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    xlat_emit_trampoline(&st->arena);
    xlat_sync_code(st->arena.base, st->arena.code_top);
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
    memset(xlat_entry_map, 0, sizeof(xlat_entry_map));
    st = NULL;
    disabled = false;
}

void xlat_flush(void)
{
    if (!st)
        return;
    reset_tables();
    xlat_stats.flushes++;
}

static entry_t *XLAT_HOT_FN(lookup)(uint32_t key)
{
    const uint32_t h = hash32(key) >> (32 - 8);
    for (uint32_t k = 0; k < 8; ++k) {
        entry_t *e = &st->entry[(h + k) & (XLAT_ENTRY_SLOTS - 1)];
        if (!e->used)
            return NULL;
        if (e->key == key)
            return e;
    }
    return NULL;
}

static bool insert(uint32_t key, uint8_t region, uint32_t code_off)
{
    const uint32_t h = hash32(key) >> (32 - 8);
    for (uint32_t k = 0; k < 8; ++k) {
        entry_t *e = &st->entry[(h + k) & (XLAT_ENTRY_SLOTS - 1)];
        if (!e->used || e->key == key || !st->region[e->region].live) {
            e->key = key;
            e->code_off = (uint16_t)code_off;
            e->region = region;
            e->used = 1;
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
    if (rt->seg_base[1] != r->cs_base)
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

static bool covered(const xlat_rt_t *rt)
{
    const uint32_t linear = rt->seg_base[1] + (rt->ip & 0xffffu);
    for (int k = 0; k < st->n_regions; ++k) {
        const region_t *r = &st->region[k];
        if (r->live && linear - r->lo < r->span)
            return true;
    }
    return false;
}

static bool translate_here(const xlat_rt_t *rt)
{
    xlat_region_out_t *out = &st->out;
    bool full;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (st->n_regions >= XLAT_MAX_REGIONS) {
            xlat_flush();
            continue;
        }
        if (xlat_translate(&st->arena, rt, (uint16_t)rt->ip, out, &full, st->ctx))
            break;
        if (!full || attempt)
            return false;
        xlat_flush();
    }

    const int idx = st->n_regions++;
    region_t *r = &st->region[idx];
    r->cs = rt->cs;
    r->ip = (uint16_t)rt->ip;
    r->cs_base = rt->seg_base[1];
    r->lo = out->lo;
    r->span = out->span;
    r->n_runs = out->n_runs;
    st->arena.data_bottom -= out->data_size;
    r->data_off = st->arena.data_bottom;
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
        d += 8 + ((len + 3u) & ~3u);
    }
    r->live = 1;
    for (unsigned k = 0; k < out->n_entries; ++k) {
        insert((uint32_t)rt->cs << 16 | out->entries[k].ip, (uint8_t)idx,
               out->entries[k].code_off);
        if (out->entries[k].mark) {
            const uint32_t m = xlat_map_index(r->cs_base + out->entries[k].ip);
            xlat_entry_map[m >> 3] |= (uint8_t)(1u << (m & 7u));
        }
    }
    xlat_sync_code(st->arena.base + out->code_off, out->code_len);
    log_region_code(r);
    xlat_stats.translations++;
    xlat_stats.code_bytes = st->arena.code_top + (st->arena.size - st->arena.data_bottom);
    return true;
}

/* Called when no translation exists at rt->cs:ip. True when one was built. */
static __attribute__((noinline)) bool sample(const xlat_rt_t *rt)
{
    const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
    hot_t *h = &st->hot[hash32(key) >> (32 - 6)];
    if (h->key != key) {
        h->key = key;
        h->count = 1;
        h->reject = 0;
        return false;
    }
    if (h->reject) {
        note_reject_hit(key);
        return false;
    }
    if (++h->count < XLAT_HOT_THRESHOLD)
        return false;
    h->count = 0;
    if (stack_free() < XLAT_STACK_NEEDED) {
        xlat_stats.stack_skips++;   /* try again when it is hot at a shallower depth */
        return false;
    }
    /* Inside an existing region but not at one of its entries: the region is
     * entered at its loop head once a burst ends there. */
    const bool inside = covered(rt);
    if (inside || !translate_here(rt)) {
        h->reject = 1;
        xlat_stats.rejects++;
        note_reject(rt, key, inside ? XLAT_RJ_COVERED : st->out.reject);
        return false;
    }
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
    const xlat_tramp_fn tramp = (xlat_tramp_fn)(uintptr_t)(st->arena.base + 1);
    bool ran = false, built = false;
    for (;;) {
        const uint32_t key = (uint32_t)rt->cs << 16 | (rt->ip & 0xffffu);
        entry_t *e = lookup(key);
        if (!e || !st->region[e->region].live) {
            if (ran || built || !sample(rt))
                break;
            built = true;
            continue;
        }
        region_t *r = &st->region[e->region];
        if (!region_valid(r, rt)) {
            r->live = 0;
            xlat_stats.invalidations++;
            break;
        }
        rt->budget -= XLAT_ENTRY_COST;
        cur_region = r;
        tramp(rt, (uint32_t)(uintptr_t)(st->arena.base + e->code_off) | 1u);
        cur_region = NULL;
        ran = true;
        xlat_stats.entries++;
        r->entries++;
        if (rt->exit_kind < XLAT_EXIT_COUNT) {
            xlat_stats.exits[rt->exit_kind]++;
            r->exits[rt->exit_kind]++;
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
    if (ran)
        xlat_stats.guest_insns += (uint32_t)(start + (int32_t)rt->extra - rt->budget);
    return ran;
}

/* x86 condition cc (Jcc low nibble) on the lazy flags in rt, as XRIP's
 * condition(). Called from generated code. */
uint32_t XLAT_HOT_FN(xlat_rt_cond)(const xlat_rt_t *rt, uint32_t cc)
{
    bool cf, zf, sf, of, pf;
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
    log_printf("S translations=%lu rejects=%lu entries=%lu invalid=%lu flushes=%lu "
               "exit b/bu/s/st=%lu/%lu/%lu/%lu native=%lu code=%lu stack_skips=%lu interp=%lu side=%lu cuts=%lu/%lu lost=%lu\r\n",
               (unsigned long)xlat_stats.translations, (unsigned long)xlat_stats.rejects,
               (unsigned long)xlat_stats.entries, (unsigned long)xlat_stats.invalidations,
               (unsigned long)xlat_stats.flushes, (unsigned long)xlat_stats.exits[0],
               (unsigned long)xlat_stats.exits[1], (unsigned long)xlat_stats.exits[2],
               (unsigned long)xlat_stats.exits[3], (unsigned long)xlat_stats.guest_insns, (unsigned long)xlat_stats.code_bytes,
               (unsigned long)xlat_stats.stack_skips, (unsigned long)xlat_stats.interp,
               (unsigned long)xlat_stats.side_interp, (unsigned long)xlat_stats.cut_entries,
               (unsigned long)xlat_stats.cuts, (unsigned long)st->log_lost);
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
bool XLAT_HOT_FN(xlat_region_still_valid)(const xlat_rt_t *rt)
{
    region_t *r = cur_region;
    if (!r || region_valid(r, rt))
        return true;
    r->live = 0;
    xlat_stats.invalidations++;
    return false;
}
