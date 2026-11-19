#ifndef NATIVE_EZ_RELOC_H
#define NATIVE_EZ_RELOC_H

#include "ez.h"

static inline uint16_t ez_read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t ez_read32(const uint8_t *p)
{
    return ez_read16(p) | ((uint32_t)ez_read16(p + 2) << 16);
}

static inline void ez_write16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void ez_write32(uint8_t *p, uint32_t v)
{
    ez_write16(p, (uint16_t)v);
    ez_write16(p + 2, (uint16_t)(v >> 16));
}

static inline uint16_t ez_thumb_mov_get(const uint8_t *p)
{
    const uint16_t a = ez_read16(p), b = ez_read16(p + 2);
    return (uint16_t)(((a & 15u) << 12) | ((a & 0x0400u) << 1) |
                      ((b & 0x7000u) >> 4) | (b & 255u));
}

static inline void ez_thumb_mov_set(uint8_t *p, uint16_t v)
{
    ez_write16(p, (ez_read16(p) & ~0x040fu) | (v >> 12) | ((v & 0x0800u) >> 1));
    ez_write16(p + 2, (ez_read16(p + 2) & ~0x70ffu) | ((v & 0x0700u) << 4) | (v & 255u));
}

static inline int ez_apply_reloc(uint8_t *base, uint32_t end,
                                 const struct ez_reloc *r)
{
    if (r->rva < EZ_IMAGE_RVA || r->rva > end || end - r->rva < 4 ||
        r->reserved[2] ||
        (r->type != EZ_RELOC_THM_MOVT_ABS && (r->reserved[0] || r->reserved[1])))
        return -1;

    uint8_t *p = base + r->rva;
    const uint32_t address = (uint32_t)(uintptr_t)base;
    if (r->type == EZ_RELOC_ABS32) {
        ez_write32(p, ez_read32(p) + address);
        return 0;
    }
    if (r->rva & 1u)
        return -1;

    const uint16_t opcode = ez_read16(p) & 0xfbf0u;
    switch (r->type) {
    case EZ_RELOC_THM_ALU_ABS_G0_NC: /* Legacy EZ v1 record. */
    case EZ_RELOC_THM_MOVW_ABS_NC:
        if (r->type == EZ_RELOC_THM_MOVW_ABS_NC && opcode != 0xf240u)
            return -1;
        ez_thumb_mov_set(p, (uint16_t)(ez_thumb_mov_get(p) + address));
        return 0;
    case EZ_RELOC_THM_MOVT_ABS: {
        if (opcode != 0xf2c0u)
            return -1;
        const uint32_t rva = ((uint32_t)ez_thumb_mov_get(p) << 16) |
                             ((uint32_t)r->reserved[1] << 8) | r->reserved[0];
        ez_thumb_mov_set(p, (uint16_t)((rva + address) >> 16));
        return 0;
    }
    default:
        return -1;
    }
}

#endif
