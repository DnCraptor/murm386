/*
 * EZ image reader shared by WIN.COM and WINN.EXE.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ez_image.h"
#include "../../api/ez_reloc.h"

#define EZ_BASE_ALIGN 64u

#if defined(__arm__)
static int win_ez_has_thumb2(void)
{
    /* WINN itself may be built for Thumb-1 and run on either chip. */
    const uint32_t part = (*(const volatile uint32_t *)0xe000ed00u >> 4) & 0xfffu;
    return part == 0xd21u; /* RP2350 Cortex-M33 */
}
#endif

/* Read, place and relocate an EZ image. 0 ok, -1 no file, < -1 bad. */
int win_ez_load(const char *path, win_ez_image *img)
{
    memset(img, 0, sizeof(*img));
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    struct ez_file_header h;
    int rc = -2;
    uint8_t *raw = NULL;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) || h.magic != EZ_MAGIC || h.version != EZ_FORMAT_VERSION ||
        (h.flags & ~EZ_FLAG_KNOWN_MASK) || !(h.flags & EZ_FLAG_THUMB) ||
        !(h.flags & EZ_FLAG_SOFT_FLOAT) ||
        ((h.flags & EZ_FLAG_ARMV6M) && (h.flags & EZ_FLAG_THUMB2)) ||
#if defined(__arm__)
        ((h.flags & EZ_FLAG_THUMB2) && !win_ez_has_thumb2()) ||
#endif
        h.header_size < sizeof(h) || h.image_mem_size < h.image_file_size ||
        h.image_mem_size > UINT32_MAX - EZ_IMAGE_RVA - EZ_BASE_ALIGN ||
        !(h.entry_rva & 1) || h.entry_rva < EZ_IMAGE_RVA ||
        h.entry_rva - EZ_IMAGE_RVA >= h.image_mem_size ||
        (h.reloc_count && h.reloc_entry_size < sizeof(struct ez_reloc)))
        goto out;

    /* Validate file ranges before allocating memory and applying relocations. */
    long file_end;
    if (fseek(f, 0, SEEK_END) || (file_end = ftell(f)) < 0 ||
        (uint64_t)h.header_size + h.image_file_size > (uint64_t)file_end ||
        (h.reloc_count &&
         (uint64_t)h.reloc_offset + (uint64_t)h.reloc_count * h.reloc_entry_size >
             (uint64_t)file_end))
        goto out;

    uint32_t total = EZ_IMAGE_RVA + h.image_mem_size;
    raw = calloc(1, total + EZ_BASE_ALIGN);
    rc = -3;
    if (!raw)
        goto out;
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + EZ_BASE_ALIGN - 1) & ~(uintptr_t)(EZ_BASE_ALIGN - 1));
    rc = -4;
    if (fseek(f, (long)h.header_size, SEEK_SET) ||
        fread(base + EZ_IMAGE_RVA, 1, h.image_file_size, f) != h.image_file_size)
        goto out;
    if (h.reloc_count) {
        if (fseek(f, (long)h.reloc_offset, SEEK_SET))
            goto out;
        for (uint32_t i = 0; i < h.reloc_count; i++) {
            struct ez_reloc r;
            if (fread(&r, 1, sizeof(r), f) != sizeof(r))
                goto out;
            if (h.reloc_entry_size > sizeof(r) &&
                fseek(f, (long)(h.reloc_entry_size - sizeof(r)), SEEK_CUR))
                goto out;
            if (ez_apply_reloc(base, total, &r)) {
                rc = -5;
                goto out;
            }
        }
    }
    img->raw = raw;
    img->base = base;
    img->entry = base + h.entry_rva;
    img->image_size = h.image_mem_size;
    img->stack_size = h.native_stack_size;
    img->dos_api_version = h.required_dos_api_version;
    raw = NULL;
    rc = 0;
out:
    fclose(f);
    free(raw);
    return rc;
}
