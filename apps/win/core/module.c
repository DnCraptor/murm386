/*
 * module.c - loader for driver modules (EZ images, see ddi/winmod.h).
 *
 * The EZ layout is the one the firmware loader uses: RVAs count from a base
 * whose first 0x100 bytes would be a PSP; the image starts at base + 0x100,
 * the zero tail follows it, and relocation records add the base to words
 * (ABS32) or to a MOVW-style immediate, exactly as the firmware loader. The module's memory is never freed:
 * drivers live as long as the program.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"
#include "winmod.h"

#define EZ_MAGIC        0x5a45u
#define EZ_IMAGE_RVA    0x100u
#define EZ_BASE_ALIGN   64u

#pragma pack(push, 1)
typedef struct {
    uint16_t magic, version, header_size, flags;
    uint32_t required_dos_api_version;
    uint32_t native_stack_size, dos_stack_size;
    uint32_t image_file_size, image_mem_size;
    uint32_t entry_rva;
    uint32_t reloc_offset, reloc_count;
    uint16_t reloc_entry_size, reserved;
} ezhdr;

typedef struct {
    uint32_t rva;
    uint8_t  type, reserved[3];
} ezrel;
#pragma pack(pop)

/* the kinds the firmware's EZ loader applies; others are rejected */
enum { EZ_ABS32 = 1, EZ_THM_G0_NC = 2 };

static void key_cb(int vk, int sc, int down) { win_ddi_key_event(vk, sc, down); }
static void mouse_cb(int x, int y, unsigned b) { win_ddi_mouse_event(x, y, b); }

static const win_module_host host_table = {
    WINMOD_VERSION, sizeof(win_module_host), key_cb, mouse_cb,
};

/* 16-bit immediate split as imm4:i:imm3:imm8 (MOVW encoding). */
static uint32_t movw_get(const uint16_t *p)
{
    return ((uint32_t)(p[0] & 0xF) << 12) | ((uint32_t)((p[0] >> 10) & 1) << 11) |
           ((uint32_t)((p[1] >> 12) & 7) << 8) | (p[1] & 0xFF);
}

static void movw_set(uint16_t *p, uint32_t v)
{
    v &= 0xFFFF;
    p[0] = (uint16_t)((p[0] & ~0x040Fu) | ((v >> 12) & 0xF) | (((v >> 11) & 1) << 10));
    p[1] = (uint16_t)((p[1] & ~0x70FFu) | (((v >> 8) & 7) << 12) | (v & 0xFF));
}

static int apply(uint8_t *base, uint32_t mem_end, const ezrel *r)
{
    if (r->rva < EZ_IMAGE_RVA || r->rva + 4 > mem_end)
        return -1;
    uint8_t *p = base + r->rva;
    uint32_t b = (uint32_t)(uintptr_t)base;
    switch (r->type) {
    case EZ_ABS32: {
        uint32_t v;
        memcpy(&v, p, 4);
        v += b;
        memcpy(p, &v, 4);
        return 0;
    }
    case EZ_THM_G0_NC:          /* handled like the firmware loader does */
        movw_set((uint16_t *)p, movw_get((uint16_t *)p) + b);
        return 0;
    default:
        return -1;
    }
}

/* Read, place and relocate an EZ image. 0 ok, -1 no file, < -1 bad. */
int win_ez_load(const char *path, win_ez_image *img)
{
    memset(img, 0, sizeof(*img));
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    ezhdr h;
    int rc = -2;
    uint8_t *raw = NULL;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) || h.magic != EZ_MAGIC || h.version != 1 ||
        h.header_size < sizeof(h) || h.image_mem_size < h.image_file_size ||
        h.entry_rva < EZ_IMAGE_RVA || h.entry_rva >= EZ_IMAGE_RVA + h.image_mem_size ||
        (h.reloc_count && h.reloc_entry_size < sizeof(ezrel)))
        goto out;

    uint32_t total = EZ_IMAGE_RVA + h.image_mem_size;
    raw = (uint8_t *)win_alloc(total + EZ_BASE_ALIGN);          /* zeroed */
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
        uint8_t rec[16];
        for (uint32_t i = 0; i < h.reloc_count; i++) {
            if (fread(rec, 1, h.reloc_entry_size > sizeof(rec) ? sizeof(rec) : h.reloc_entry_size, f) == 0)
                goto out;
            if (h.reloc_entry_size > sizeof(rec))
                fseek(f, (long)(h.reloc_entry_size - sizeof(rec)), SEEK_CUR);
            ezrel r;
            memcpy(&r, rec, sizeof(r));
            if (apply(base, total, &r)) {
                rc = -5;
                goto out;
            }
        }
    }
    img->raw = raw;
    img->base = base;
    img->entry = base + h.entry_rva;
    img->stack_size = h.native_stack_size;
    raw = NULL;
    rc = 0;
out:
    fclose(f);
    win_free(raw);
    return rc;
}

/* __ez_start of sdk/winapp_entry.c: "b 1f; .hword 0; .word WINAPP_MAGIC" */
int win_ez_entry_is_app(const void *entry)
{
    uintptr_t e = (uintptr_t)entry & ~(uintptr_t)1;
    if (e & 3)
        return 0;
    uint32_t sig;
    memcpy(&sig, (const uint8_t *)e + 4, 4);
    return sig == WINAPP_MAGIC;
}

int win_ez_is_app(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    ezhdr h;
    uint32_t sig = 0;
    uint32_t e = 0;
    if (fread(&h, 1, sizeof(h), f) == sizeof(h) && h.magic == EZ_MAGIC && h.version == 1 &&
        h.header_size >= sizeof(h)) {
        e = h.entry_rva & ~1u;
        if (!(e & 3) && e >= EZ_IMAGE_RVA && e + 8 <= EZ_IMAGE_RVA + h.image_file_size &&
            !fseek(f, (long)(h.header_size + e - EZ_IMAGE_RVA + 4), SEEK_SET) &&
            fread(&sig, 1, 4, f) != 4)
            sig = 0;
    }
    fclose(f);
    return sig == WINAPP_MAGIC;
}

int win_load_module(const char *path, const win_module_info **info)
{
    *info = NULL;
    win_ez_image img;
    int rc = win_ez_load(path, &img);
    if (rc)
        return rc;
#if defined(__arm__) || defined(__thumb__)
    win_module_entry entry = (win_module_entry)(uintptr_t)img.entry;
    const win_module_info *mi = entry(&host_table);
    if (!mi || mi->version != WINMOD_VERSION || mi->size < sizeof(win_module_info)) {
        win_free(img.raw);
        return -6;
    }
    *info = mi;              /* stays loaded */
    return 0;
#else
    (void)host_table;
    win_free(img.raw);
    return -7;               /* ARM code cannot run on the host */
#endif
}
