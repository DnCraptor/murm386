/*
 * ne_pool.h - the host's extended memory for the programs' segments (as
 * WINN's heap above the HMA has it on the target): one block, first fit,
 * in paragraphs, a block made larger where it is when what follows is
 * free. ne_host.c's.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_NE_POOL_H
#define WINN_NE_POOL_H

#include <stdint.h>
#include <string.h>

#define NE_POOL_BLOCKS 1024

typedef struct {
    uint32_t base, size;            /* the block: linear, bytes (0: none) */
    uint32_t used;                  /* the bytes its blocks have */
    int n;                          /* its blocks, in address order */
    struct { uint32_t at, size; } b[NE_POOL_BLOCKS];
} ne_pool;

static inline uint32_t ne_pool_round(uint32_t bytes) { return (bytes + 15u) & ~15u; }

static inline int ne_pool_has(const ne_pool *p, uint32_t linear, uint32_t bytes)
{
    return p->size && linear >= p->base && linear - p->base <= p->size &&
           bytes <= p->size - (linear - p->base);
}

static inline int ne_pool_find(const ne_pool *p, uint32_t linear)
{
    for (int i = 0; i < p->n; i++)
        if (p->b[i].at == linear)
            return i;
    return -1;
}

/* first fit; 0 if there is no room */
static inline int ne_pool_alloc(ne_pool *p, uint32_t bytes, uint32_t *linear)
{
    if (!p->size || !bytes || p->n >= NE_POOL_BLOCKS)
        return 0;
    bytes = ne_pool_round(bytes);
    uint32_t at = p->base, end = p->base + p->size;
    int i = 0;
    for (; i < p->n; i++) {
        if (p->b[i].at - at >= bytes)
            break;
        at = p->b[i].at + p->b[i].size;
    }
    if (end - at < bytes)
        return 0;
    memmove(&p->b[i + 1], &p->b[i], sizeof(p->b[0]) * (size_t)(p->n - i));
    p->b[i].at = at;
    p->b[i].size = bytes;
    p->n++;
    p->used += bytes;
    *linear = at;
    return 1;
}

/* 1 if it was one of the pool's */
static inline int ne_pool_free(ne_pool *p, uint32_t linear)
{
    int i = ne_pool_find(p, linear);
    if (i < 0)
        return 0;
    p->used -= p->b[i].size;
    memmove(&p->b[i], &p->b[i + 1], sizeof(p->b[0]) * (size_t)(p->n - i - 1));
    p->n--;
    return 1;
}

/* where it is: 1 done, 0 not (what follows is in use), -1 not the pool's */
static inline int ne_pool_resize(ne_pool *p, uint32_t linear, uint32_t bytes)
{
    int i = ne_pool_find(p, linear);
    if (i < 0)
        return -1;
    bytes = ne_pool_round(bytes);
    uint32_t limit = i + 1 < p->n ? p->b[i + 1].at : p->base + p->size;
    if (!bytes || limit - linear < bytes)
        return 0;
    p->used = p->used - p->b[i].size + bytes;
    p->b[i].size = bytes;
    return 1;
}

#endif /* WINN_NE_POOL_H */
