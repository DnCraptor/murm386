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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "wincore.h"
#include "winmod.h"
#include "windll.h"
#include "../../api/ez.h"

static void key_cb(int vk, int sc, int down) { win_ddi_key_event(vk, sc, down); }
static void mouse_cb(int x, int y, unsigned b) { win_ddi_mouse_event(x, y, b); }
static void track_cb(int x, int y) { win_ddi_mouse_track(x, y); }

static const win_module_host host_table = {
    WINMOD_VERSION, sizeof(win_module_host), key_cb, mouse_cb, track_cb,
};

/* __ez_start of sdk/winapp_entry.c: "b 1f; .hword 0; .word WINAPP_MAGIC" */
int win_ez_entry_is_app(const win_ez_image *img)
{
    if (!img || !img->base || !img->entry || img->image_size < 8)
        return 0;
    uintptr_t e = (uintptr_t)img->entry & ~(uintptr_t)1;
    uintptr_t base = (uintptr_t)img->base;
    if ((e & 3) || e < base + EZ_IMAGE_RVA ||
        e - base - EZ_IMAGE_RVA > img->image_size - 8)
        return 0;
    uint32_t sig;
    memcpy(&sig, (const uint8_t *)e + 4, 4);
    return sig == WINAPP_MAGIC;
}

/* Determine which loader owns the EZ without allocating the image. */
static int ez_kind(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    struct ez_file_header h;
    int kind = 0;
    uint32_t sig;
    uint32_t e = 0;
    if (fread(&h, 1, sizeof(h), f) == sizeof(h) && h.magic == EZ_MAGIC && h.version == EZ_FORMAT_VERSION &&
        h.header_size >= sizeof(h)) {
        e = h.entry_rva & ~1u;
        uint64_t end = (uint64_t)EZ_IMAGE_RVA + h.image_file_size;
        uint64_t at = (uint64_t)h.header_size + e - EZ_IMAGE_RVA + 4;
        if (e >= EZ_IMAGE_RVA && (uint64_t)e < end) {
            kind = 2;                   /* FDOS executable */
            if (!(e & 3) && (uint64_t)e + 8 <= end && at <= INT32_MAX &&
                !fseek(f, (long)at, SEEK_SET) && fread(&sig, 1, 4, f) == 4) {
                if (sig == WINAPP_MAGIC)
                    kind = 1;
                else if (sig == WINDLL_MAGIC)
                    kind = 0;
            }
        }
    }
    fclose(f);
    return kind;
}

int win_ez_is_app(const char *path)
{
    return ez_kind(path) == 1;
}

int win_dos_is_ez(const char *path)
{
    /* Drivers use the EZ container too. */
    const char *ext = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '.')
            ext = p;
        else if (*p == '\\' || *p == '/' || *p == ':')
            ext = NULL;
    return ext && (!lstrcmpi(ext, ".EXE") || !lstrcmpi(ext, ".EZ") ||
                   !lstrcmpi(ext, ".COM")) && ez_kind(path) == 2;
}

/* The winapp_info of an application file (its address follows WINAPP_MAGIC
 * at entry+8), read from the file without loading it: win_alloc'd, NULL if
 * there is none. */
winapp_info *win_ez_read_info(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    struct ez_file_header h;
    uint32_t w[2] = { 0, 0 };
    winapp_info head, *out = NULL;
    const uint32_t hsize = (uint32_t)offsetof(winapp_info, icon);
    if (fread(&h, 1, sizeof(h), f) == sizeof(h) && h.magic == EZ_MAGIC && h.version == EZ_FORMAT_VERSION &&
        h.header_size >= sizeof(h)) {
        uint32_t e = h.entry_rva & ~1u;
        uint64_t end = (uint64_t)EZ_IMAGE_RVA + h.image_file_size;
        uint64_t entry_at = (uint64_t)h.header_size + e - EZ_IMAGE_RVA + 4;
        if (!(e & 3) && e >= EZ_IMAGE_RVA && (uint64_t)e + 12 <= end &&
            entry_at <= INT32_MAX && !fseek(f, (long)entry_at, SEEK_SET) &&
            fread(w, 1, 8, f) == 8 && w[0] == WINAPP_MAGIC) {
            uint32_t rva = w[1];
            uint64_t at = (uint64_t)h.header_size + rva - EZ_IMAGE_RVA;
            if (rva >= EZ_IMAGE_RVA && (uint64_t)rva + hsize <= end && !(rva & 3) &&
                at <= INT32_MAX && !fseek(f, (long)at, SEEK_SET) &&
                fread(&head, 1, hsize, f) == hsize && head.magic == WINAPP_INFO_MAGIC &&
                head.icon_count <= WINAPP_MAX_ICONS && head.size == WINAPP_INFO_SIZE(head.icon_count) &&
                (uint64_t)rva + head.size <= end &&
                (out = (winapp_info *)win_alloc(head.size)) != NULL) {
                if (fseek(f, (long)at, SEEK_SET) || fread(out, 1, head.size, f) != head.size) {
                    win_free(out);
                    out = NULL;
                } else {
                    out->description[sizeof(out->description) - 1] = 0;
                }
            }
        }
    }
    fclose(f);
    return out;
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
