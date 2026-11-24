/*
 * murm386 xlat: resident translation cache and burst-boundary dispatcher.
 *
 * Everything is allocated on first use (one block: tables + code arena), so a
 * firmware that never runs a hot loop pays nothing in SRAM. When the arena,
 * the region table or the entry table fills up, the whole cache is dropped.
 *
 * Hot spots are found by sampling: xlat_run() is called at the start of every
 * XRIP burst, and an entry point seen there XLAT_HOT_THRESHOLD times in a row
 * (same hash slot) is translated. A region that ran to its budget stops at a
 * back edge, i.e. at a region entry, so the next burst re-enters it at once.
 */
#include <stdlib.h>
#include <string.h>

#include "xlat_internal.h"

xlat_stats_t xlat_stats;

typedef struct {
    uint32_t cs_base;
    uint32_t data_off;
    uint32_t lo, span;
    uint16_t cs;
    uint16_t n_runs;
    uint8_t live;
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

typedef struct {
    xlat_arena_t arena;
    region_t region[XLAT_MAX_REGIONS];
    int n_regions;
    entry_t entry[XLAT_ENTRY_SLOTS];
    hot_t hot[XLAT_HOT_SLOTS];
    xlat_region_out_t out;  /* translator output, kept here to spare the stack */
    uint32_t code[];        /* arena (word aligned) */
} state_t;

#if XLAT_ARENA_SIZE > 0x10000
#error "entry_t.code_off is 16-bit"
#endif

static state_t *st;
static bool disabled;

#if defined(XLAT_HOST_TEST)
void *xlat_test_calloc(size_t size);   /* executable, zeroed */
#define XLAT_ALLOC(size) xlat_test_calloc(size)
#else
#define XLAT_ALLOC(size) calloc(1, size)
#endif

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

static void reset_tables(void)
{
    memset(st->region, 0, sizeof(st->region));
    memset(st->entry, 0, sizeof(st->entry));
    for (int k = 0; k < XLAT_HOT_SLOTS; ++k)
        st->hot[k].reject = 0;
    st->n_regions = 0;
    xlat_emit_trampoline(&st->arena);
    xlat_sync_code(st->arena.base, st->arena.code_top);
    xlat_stats.code_bytes = st->arena.code_top;
}

static bool ready(void)
{
    if (st)
        return true;
    if (disabled)
        return false;
    st = (state_t *)XLAT_ALLOC(sizeof(state_t) + XLAT_ARENA_SIZE);
    if (!st) {
        disabled = true;
        xlat_stats.alloc_failed = 1;
        return false;
    }
    st->arena.base = (uint8_t *)st->code;
    st->arena.size = XLAT_ARENA_SIZE;
    reset_tables();
    return true;
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
        if (xlat_translate(&st->arena, rt, (uint16_t)rt->ip, out, &full))
            break;
        if (!full || attempt)
            return false;
        xlat_flush();
    }

    const int idx = st->n_regions++;
    region_t *r = &st->region[idx];
    r->cs = rt->cs;
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
    for (unsigned k = 0; k < out->n_entries; ++k)
        insert((uint32_t)rt->cs << 16 | out->entries[k].ip, (uint8_t)idx,
               out->entries[k].code_off);
    xlat_sync_code(st->arena.base + out->code_off, out->code_len);
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
    if (h->reject || ++h->count < XLAT_HOT_THRESHOLD)
        return false;
    h->count = 0;
    /* Inside an existing region but not at one of its entries: the region is
     * entered at its loop head once a burst ends there. */
    if (covered(rt) || !translate_here(rt)) {
        h->reject = 1;
        xlat_stats.rejects++;
        return false;
    }
    return true;
}

bool XLAT_HOT_FN(xlat_run)(xlat_rt_t *rt)
{
    if (!ready())
        return false;
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
        tramp(rt, (uint32_t)(uintptr_t)(st->arena.base + e->code_off) | 1u);
        ran = true;
        xlat_stats.entries++;
        if (rt->exit_kind < XLAT_EXIT_COUNT)
            xlat_stats.exits[rt->exit_kind]++;
        if (rt->budget <= 0 || *rt->intr)
            break;
    }
    if (ran && start > rt->budget)
        xlat_stats.guest_insns += (uint32_t)(start - rt->budget);
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
