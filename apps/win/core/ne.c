/*
 * ne.c - bounded NE (New Executable) parser and staged image loader.
 *
 * NE metadata offsets are relative to the NE header; segment data and the
 * nonresident-name table use file offsets. All file ranges are validated
 * before reads, and arithmetic uses 64 bits for offset calculations.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ne.h"

#define NE_HDR_SIZE       64u
#define NE_SEG_SIZE        8u
#define NE_FIXUP_SIZE      8u
#define NE_HAS_RELOCS 0x0100u

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16;
}

static int read_at(FILE *f, uint64_t size, uint64_t offset, void *dst, size_t n)
{
    if (offset > size || n > size - offset || offset > LONG_MAX ||
        fseek(f, (long)offset, SEEK_SET))
        return 0;
    return fread(dst, 1, n, f) == n;
}

static int pascal(FILE *f, uint64_t size, uint64_t offset, char out[256])
{
    uint8_t len;
    if (!read_at(f, size, offset, &len, 1) ||
        !read_at(f, size, offset + 1u, out, len))
        return 0;
    out[len] = '\0';
    return 1;
}

static int import_name(FILE *f, uint64_t size, uint64_t names,
                       uint64_t at, char out[256])
{
    return pascal(f, size, names + at, out);
}

int win_ne_scan(const char *path, win_ne_info *info,
                win_ne_import_cb cb, void *opaque)
{
    if (!info)
        return WIN_NE_BAD_IMAGE;
    memset(info, 0, sizeof(*info));
    if (!path)
        return WIN_NE_NO_FILE;
    FILE *f = fopen(path, "rb");
    if (!f)
        return WIN_NE_NO_FILE;

    int rc = WIN_NE_BAD_IMAGE;
    uint64_t size;
    long end;
    uint8_t mz[64], hdr[NE_HDR_SIZE];
    if (fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0) {
        rc = WIN_NE_IO_ERROR;
        goto done;
    }
    size = (uint64_t)end;
    if (!read_at(f, size, 0, mz, sizeof(mz)) || rd16(mz) != 0x5a4d)
        goto done;
    uint32_t base = rd32(mz + 0x3c);
    if (!read_at(f, size, base, hdr, sizeof(hdr)) || rd16(hdr) != 0x454e) {
        rc = WIN_NE_NOT_NE;
        goto done;
    }

    uint16_t nseg = rd16(hdr + 0x1c), nmod = rd16(hdr + 0x1e);
    uint16_t segtab = rd16(hdr + 0x22), modtab = rd16(hdr + 0x28);
    uint16_t nametab = rd16(hdr + 0x2a);
    uint16_t shift = rd16(hdr + 0x32);
    if (!shift)
        shift = 9;  /* NE default file alignment */
    uint64_t segments = (uint64_t)base + segtab;
    uint64_t modules = (uint64_t)base + modtab;
    uint64_t names = (uint64_t)base + nametab;
    if (shift > 15 || !nseg || nseg > 4096 || nmod > 4096 ||
        segtab < NE_HDR_SIZE || modtab < NE_HDR_SIZE ||
        nametab < NE_HDR_SIZE ||
        segments > size || (uint64_t)nseg * NE_SEG_SIZE > size - segments ||
        modules > size || (uint64_t)nmod * 2u > size - modules ||
        names > size)
        goto done;

    win_ne_info found = { 0 };
    found.ip = rd16(hdr + 0x14);
    found.cs_index = rd16(hdr + 0x16);
    found.sp = rd16(hdr + 0x18);
    found.ss_index = rd16(hdr + 0x1a);
    found.segment_count = nseg;
    found.module_count = nmod;
    found.auto_data = rd16(hdr + 0x0e);
    found.heap_size = rd16(hdr + 0x10);
    found.stack_size = rd16(hdr + 0x12);
    found.target_os = hdr[0x36];
    found.module_flags = rd16(hdr + 0x0c);
    if (found.cs_index > nseg || found.ss_index > nseg || found.auto_data > nseg)
        goto done;

    /* Every import relocation refers to a 1-based module-table entry. A
     * relocation chain is intentionally not expanded in this scan. */
    for (unsigned i = 0; i < nseg; i++) {
        uint8_t seg[NE_SEG_SIZE];
        if (!read_at(f, size, segments + (uint64_t)i * NE_SEG_SIZE,
                     seg, sizeof(seg)))
            goto done;
        if (!WIN_NE_LOADER_SEGMENT(&found, i + 1u))
            continue;                   /* the loader's own format */
        uint16_t sector = rd16(seg), seglen = rd16(seg + 2);
        uint16_t flags = rd16(seg + 4);
        uint32_t length = seglen ? seglen : 65536u;
        uint64_t data = (uint64_t)sector << shift;
        /* An uninitialised segment has no data on disk. */
        if (sector && (data > size || length > size - data))
            goto done;
        if (!(flags & NE_HAS_RELOCS))
            continue;
        if (!sector || data + length > size || size - (data + length) < 2)
            goto done;

        uint8_t count_bytes[2];
        uint64_t fixups = data + length;
        if (!read_at(f, size, fixups, count_bytes, 2))
            goto done;
        uint16_t count = rd16(count_bytes);
        fixups += 2;
        if (fixups > size || (uint64_t)count * NE_FIXUP_SIZE > size - fixups)
            goto done;

        for (unsigned j = 0; j < count; j++) {
            uint8_t rec[NE_FIXUP_SIZE];
            if (!read_at(f, size, fixups + (uint64_t)j * NE_FIXUP_SIZE,
                         rec, sizeof(rec)))
                goto done;
            uint8_t kind = rec[1] & 3;
            uint16_t src = rd16(rec + 2);
            /* An ordinary source offset must point inside its own segment.
             * 0xffff is a documented empty-chain marker for some fixups. */
            if (src != 0xffff && src >= length)
                goto done;
            found.fixup_records++;
            if (kind != 1 && kind != 2)
                continue; /* internal reference or OS fixup */
            win_ne_import imp;
            memset(&imp, 0, sizeof(imp));
            imp.segment_index = (uint16_t)(i + 1);
            imp.source_offset = src;
            imp.source_type = rec[0];
            imp.flags = rec[1];
            imp.module_index = rd16(rec + 4);
            if (!imp.module_index || imp.module_index > nmod)
                goto done;
            uint8_t mo[2];
            if (!read_at(f, size, modules + (uint64_t)(imp.module_index - 1) * 2,
                         mo, sizeof(mo)) ||
                !import_name(f, size, names, rd16(mo), imp.module))
                goto done;
            if (kind == 1)
                imp.ordinal = rd16(rec + 6);
            else if (!import_name(f, size, names, rd16(rec + 6), imp.procedure))
                goto done;
            found.import_records++;
            if (cb && cb(opaque, &imp)) {
                rc = WIN_NE_CANCELLED;
                goto done;
            }
        }
    }
    *info = found;
    rc = WIN_NE_OK;
done:
    fclose(f);
    return rc;
}

/* The NE format is independent of the emulated CPU generation. Keep
 * recognition separate from the restrictions of the current 386 executor. */
win_ne_launch_status win_ne_classify_launch(const char *path, unsigned cpu_gen)
{
    win_ne_info info;
    if (win_ne_scan(path, &info, NULL, NULL) != WIN_NE_OK || info.target_os != 2)
        return WIN_NE_LAUNCH_OTHER;
    if (cpu_gen < 3)
        return WIN_NE_LAUNCH_REQUIRES_386;
    if (info.segment_count < 2 || !info.cs_index || !info.ss_index ||
        info.cs_index == info.ss_index)
        return WIN_NE_LAUNCH_UNSUPPORTED;
    return WIN_NE_LAUNCH_READY;
}

/* ---------------------------------------------------------------- staged image */

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void win_ne_free_image(win_ne_image *image)
{
    if (!image)
        return;
    if (image->segments) {
        for (unsigned i = 1; i <= image->info.segment_count; i++)
            free(image->segments[i].data);
        free(image->segments);
    }
    memset(image, 0, sizeof(*image));
}

/* 16-bit Windows relocation source types: low byte, segment, 16:16 pointer,
 * and 16-bit offset. 32-bit variants belong to a different execution ABI. */
static unsigned ne_source_width(uint8_t source)
{
    switch (source & 0x0fu) {
    case 0: return 1;
    case 2: return 2;
    case 3: return 4;
    case 5: return 2;
    default: return 0;
    }
}

static void ne_write_target(uint8_t *p, unsigned type, uint16_t sel,
                            uint16_t off, int additive)
{
    switch (type) {
    case 0:
        p[0] = (uint8_t)(off + (additive ? p[0] : 0));
        break;
    case 2:
        wr16(p, (uint16_t)(sel + (additive ? rd16(p) : 0)));
        break;
    case 3: {
        uint16_t old_off = additive ? rd16(p) : 0;
        wr16(p, (uint16_t)(off + old_off));
        wr16(p + 2, sel);
        break;
    }
    case 5:
        wr16(p, (uint16_t)(off + (additive ? rd16(p) : 0)));
        break;
    }
}

/* NE exports are numbered through bundles of fixed or movable entries.
 * An internal fixup with segment number 0xff references an entry ordinal.
 * Each movable bundle item has flags, an INT 3f stub word, segment and offset.
 * We do not implement NE constant (0xfe) entries here. */
static int ne_entry_target(FILE *f, uint64_t size, uint64_t base,
                           const uint8_t hdr[NE_HDR_SIZE], uint16_t ordinal,
                           const win_ne_image *img,
                           uint16_t *sel, uint16_t *off)
{
    uint16_t start = rd16(hdr + 4), length = rd16(hdr + 6);
    if (!ordinal || !start || !length)
        return WIN_NE_UNRESOLVED;
    uint64_t at = base + start;
    uint64_t end = at + length;
    if (at > size || end > size)
        return WIN_NE_BAD_IMAGE;
    uint32_t current = 1;
    while (at < end) {
        uint8_t count;
        if (!read_at(f, size, at++, &count, 1))
            return WIN_NE_BAD_IMAGE;
        if (!count)
            break;
        uint8_t type;
        if (at >= end || !read_at(f, size, at++, &type, 1))
            return WIN_NE_BAD_IMAGE;
        unsigned width = type == 0 ? 0 : type == 0xffu ? 6 : 3;
        uint64_t bytes = (uint64_t)count * width;
        if (bytes > end - at)
            return WIN_NE_BAD_IMAGE;
        if ((uint32_t)ordinal >= current &&
            (uint32_t)ordinal < current + count) {
            if (type == 0)
                return WIN_NE_UNRESOLVED;
            if (type == 0xfeu)
                return WIN_NE_UNSUPPORTED; /* constant-value export */
            uint64_t entry_at = at + ((uint32_t)ordinal - current) * width;
            uint8_t entry[6];
            if (!read_at(f, size, entry_at, entry, width))
                return WIN_NE_BAD_IMAGE;
            unsigned target_seg = type == 0xffu ? entry[3] : type;
            unsigned target_off = type == 0xffu ? rd16(entry + 4) : rd16(entry + 1);
            if (!target_seg || target_seg > img->info.segment_count ||
                (target_off >= img->segments[target_seg].size &&
                 WIN_NE_LOADER_SEGMENT(&img->info, target_seg)))
                return WIN_NE_BAD_IMAGE;
            *sel = img->segments[target_seg].selector;
            *off = (uint16_t)target_off;
            return WIN_NE_OK;
        }
        current += count;
        at += bytes;
    }
    return WIN_NE_UNRESOLVED;
}

/* An OS fixup: a coprocessor instruction (its type: 1 to 4 with DS, SS,
 * CS or ES, 5 any other, 6 a WAIT). The image has the coprocessor's own
 * (WAIT, ESC ...), Windows turns them into the emulator's interrupts (INT
 * 34h to 3Dh) only without a coprocessor, and the emulator has one: nothing
 * to do. Should a linker have left the interrupts in: the instructions
 * again. A second record for an override's ESC byte finds no INT there. */
static int ne_os_fixup(win_ne_segment_image *seg, const uint8_t rec[NE_FIXUP_SIZE])
{
    static const uint8_t override[4] = { 0x3e, 0x36, 0x2e, 0x26 };   /* DS SS CS ES */
    uint16_t where = rd16(rec + 2), type = rd16(rec + 4);
    if (!type || type > 6)
        return WIN_NE_UNSUPPORTED;
    if ((uint32_t)where + 2u > seg->size)
        return WIN_NE_BAD_IMAGE;
    uint8_t *p = seg->data + where;
    if (p[0] != 0xcd)
        return WIN_NE_OK;
    if (p[1] >= 0x34 && p[1] <= 0x3b) {          /* WAIT, ESC 0 to 7 */
        p[1] = (uint8_t)(p[1] - 0x34 + 0xd8);
        p[0] = 0x9b;
    } else if (p[1] == 0x3d) {                   /* WAIT */
        p[0] = 0x90;
        p[1] = 0x9b;
    } else if (p[1] == 0x3c) {                   /* WAIT, segment, ESC */
        if ((uint32_t)where + 3u > seg->size)
            return WIN_NE_BAD_IMAGE;
        p[0] = 0x9b;
        p[1] = override[p[2] >> 6];
        p[2] |= 0xc0;
    }
    return WIN_NE_OK;
}

/* Resolve one fixup, then apply its target through all chain links. Read the
 * next link BEFORE overwriting its source. Chained records have a 16-bit
 * next-link even when their relocation source type is a single byte. */
static int ne_apply_record(FILE *f, uint64_t file_size, uint64_t base,
                           const uint8_t hdr[NE_HDR_SIZE], uint64_t modtab,
                           uint64_t nametab, uint16_t nmod,
                           win_ne_image *img, unsigned segno,
                           const uint8_t rec[NE_FIXUP_SIZE],
                           win_ne_resolve_cb resolver, void *opaque)
{
    win_ne_segment_image *srcseg = &img->segments[segno];
    uint8_t kind = rec[1] & 3;
    unsigned type = rec[0] & 0x0fu;
    unsigned width = ne_source_width(rec[0]);
    int additive = !!(rec[1] & 4);
    uint16_t where = rd16(rec + 2);
    uint16_t target_sel = 0, target_off = 0;

    if ((rec[0] & 0xf0u) || (rec[1] & ~7u) || !width)
        return WIN_NE_UNSUPPORTED;
    if (kind == 3)
        return ne_os_fixup(srcseg, rec);
    if (kind == 0) {
        uint16_t target_idx = rec[4];
        if (target_idx == 0xffu) {
            int rc = ne_entry_target(f, file_size, base, hdr, rd16(rec + 6),
                                     img, &target_sel, &target_off);
            if (rc)
                return rc;
        } else {
            if (!target_idx || target_idx > img->info.segment_count || rec[5])
                return WIN_NE_BAD_IMAGE;
            target_sel = img->segments[target_idx].selector;
            target_off = rd16(rec + 6);
            /* a segment its loader loads: where it will be, unchecked */
            if (target_off >= img->segments[target_idx].size &&
                WIN_NE_LOADER_SEGMENT(&img->info, target_idx))
                return WIN_NE_BAD_IMAGE;
        }
    } else {
        win_ne_import imp;
        memset(&imp, 0, sizeof(imp));
        imp.segment_index = (uint16_t)segno;
        imp.source_offset = where;
        imp.source_type = rec[0];
        imp.flags = rec[1];
        imp.module_index = rd16(rec + 4);
        if (!imp.module_index || imp.module_index > nmod)
            return WIN_NE_BAD_IMAGE;
        uint8_t mo[2];
        if (!read_at(f, file_size, modtab + (uint64_t)(imp.module_index - 1) * 2u,
                     mo, sizeof(mo)) ||
            !import_name(f, file_size, nametab, rd16(mo), imp.module))
            return WIN_NE_BAD_IMAGE;
        if (kind == 1)
            imp.ordinal = rd16(rec + 6);
        else if (!import_name(f, file_size, nametab, rd16(rec + 6),
                              imp.procedure))
            return WIN_NE_BAD_IMAGE;
        if (!resolver || !resolver(opaque, &imp, &target_sel, &target_off) ||
            !target_sel)
            return WIN_NE_UNRESOLVED;
    }
    if (where == 0xffffu)
        return additive ? WIN_NE_BAD_IMAGE : WIN_NE_OK;
    if ((uint32_t)where + width > srcseg->size)
        return WIN_NE_BAD_IMAGE;
    if (additive) {
        ne_write_target(srcseg->data + where, type, target_sel, target_off, 1);
        return WIN_NE_OK;
    }

    /* Validate the entire chain BEFORE writing. Floyd's cycle detector
     * requires no auxiliary allocation and rejects a two-node cycle as well
     * as a self-link without corrupting previously patched bytes. */
    uint16_t slow = where, fast = where;
    for (;;) {
        if ((uint32_t)fast + width > srcseg->size ||
            (uint32_t)fast + 2u > srcseg->size)
            return WIN_NE_BAD_IMAGE;
        fast = rd16(srcseg->data + fast);
        if (fast == 0xffffu)
            break;
        if ((uint32_t)fast + width > srcseg->size ||
            (uint32_t)fast + 2u > srcseg->size)
            return WIN_NE_BAD_IMAGE;
        fast = rd16(srcseg->data + fast);
        if (fast == 0xffffu)
            break;
        if ((uint32_t)slow + width > srcseg->size ||
            (uint32_t)slow + 2u > srcseg->size)
            return WIN_NE_BAD_IMAGE;
        slow = rd16(srcseg->data + slow);
        if (fast == slow)
            return WIN_NE_BAD_IMAGE;
    }
    /* The linked list is in the segment's ON-DISK image. Read the next
     * pointer before writing the FAR address at the current location. */
    for (uint32_t links = 0; links <= srcseg->size; links++) {
        if ((uint32_t)where + width > srcseg->size ||
            (uint32_t)where + 2u > srcseg->size)
            return WIN_NE_BAD_IMAGE;
        uint16_t next = rd16(srcseg->data + where);
        ne_write_target(srcseg->data + where, type, target_sel, target_off, 0);
        if (next == 0xffffu)
            return WIN_NE_OK;
        where = next;
    }
    return WIN_NE_BAD_IMAGE;
}

int win_ne_load_image(const char *path, win_ne_image *image,
                      win_ne_selector_cb assign, win_ne_resolve_cb resolve,
                      void *opaque)
{
    if (!image || !assign)
        return WIN_NE_BAD_IMAGE;
    memset(image, 0, sizeof(*image));
    win_ne_info info;
    int rc = win_ne_scan(path, &info, NULL, NULL);
    if (rc)
        return rc;
    FILE *f = fopen(path, "rb");
    if (!f)
        return WIN_NE_NO_FILE;
    rc = WIN_NE_BAD_IMAGE;
    long file_end;
    uint8_t mz[64], h[NE_HDR_SIZE];
    if (fseek(f, 0, SEEK_END) || (file_end = ftell(f)) < 0) {
        rc = WIN_NE_IO_ERROR;
        goto out;
    }
    uint64_t size = (uint64_t)file_end;
    if (!read_at(f, size, 0, mz, sizeof(mz)) || rd16(mz) != 0x5a4d)
        goto out;
    uint64_t base = rd32(mz + 0x3c);
    if (!read_at(f, size, base, h, sizeof(h)) || rd16(h) != 0x454e)
        goto out;
    uint16_t nseg = rd16(h + 0x1c);
    if (nseg != info.segment_count || rd16(h + 0x1e) != info.module_count)
        goto out;
    uint64_t segtab = base + rd16(h + 0x22);
    uint64_t modtab = base + rd16(h + 0x28);
    uint64_t nametab = base + rd16(h + 0x2a);
    uint16_t shift = rd16(h + 0x32);
    if (!shift)
        shift = 9;
    if (shift > 15 || segtab > size ||
        (uint64_t)nseg * NE_SEG_SIZE > size - segtab ||
        modtab > size ||
        (uint64_t)info.module_count * 2u > size - modtab ||
        nametab > size)
        goto out;

    image->info = info;
    image->segments = (win_ne_segment_image *)calloc((size_t)nseg + 1u,
                                                     sizeof(*image->segments));
    if (!image->segments) {
        rc = WIN_NE_NO_MEMORY;
        goto out;
    }
    /* All segment selectors must exist before any internal relocation is
     * applied. This also handles fixups pointing into later segments. */
    for (unsigned i = 1; i <= nseg; i++) {
        uint8_t ent[NE_SEG_SIZE];
        if (!read_at(f, size, segtab + (uint64_t)(i - 1) * NE_SEG_SIZE,
                     ent, sizeof(ent)))
            goto out;
        uint16_t sector = rd16(ent);
        uint32_t disk_len = rd16(ent + 2);
        if (!disk_len)
            disk_len = 65536u;
        uint32_t min_size = rd16(ent + 6);
        if (!min_size)
            min_size = 65536u;
        uint32_t mem_size = sector && disk_len > min_size ? disk_len : min_size;
        win_ne_segment_image *seg = &image->segments[i];
        seg->flags = rd16(ent + 4);
        if (!WIN_NE_LOADER_SEGMENT(&info, i)) {    /* its loader's: a selector, no memory */
            if (!assign(opaque, (uint16_t)i, seg->flags, 0, &seg->selector) || !seg->selector) {
                rc = WIN_NE_UNRESOLVED;
                goto out;
            }
            continue;
        }
        if (i == info.auto_data) {      /* DGROUP: the local heap and the stack follow */
            mem_size += (uint32_t)info.heap_size + info.stack_size;
            if (mem_size > 65536u) {
                rc = WIN_NE_UNSUPPORTED;
                goto out;
            }
        }
        seg->size = mem_size;
        seg->data = (uint8_t *)calloc(mem_size, 1);
        if (!seg->data) {
            rc = WIN_NE_NO_MEMORY;
            goto out;
        }
        if (sector) {
            uint64_t data_off = (uint64_t)sector << shift;
            if (!read_at(f, size, data_off, seg->data, disk_len))
                goto out;
        }
        if (!assign(opaque, (uint16_t)i, seg->flags, mem_size, &seg->selector) ||
            !seg->selector) {
            rc = WIN_NE_UNRESOLVED;
            goto out;
        }
    }
    for (unsigned i = 1; i <= nseg; i++) {
        uint8_t ent[NE_SEG_SIZE];
        if (!read_at(f, size, segtab + (uint64_t)(i - 1) * NE_SEG_SIZE,
                     ent, sizeof(ent)))
            goto out;
        if (!(rd16(ent + 4) & NE_HAS_RELOCS) || !WIN_NE_LOADER_SEGMENT(&info, i))
            continue;
        uint16_t sector = rd16(ent);
        uint32_t disk_len = rd16(ent + 2);
        if (!disk_len)
            disk_len = 65536u;
        if (!sector)
            goto out;
        uint64_t fixups = ((uint64_t)sector << shift) + disk_len;
        uint8_t count_bytes[2];
        if (!read_at(f, size, fixups, count_bytes, 2))
            goto out;
        uint16_t count = rd16(count_bytes);
        fixups += 2;
        if (fixups > size || (uint64_t)count * NE_FIXUP_SIZE > size - fixups)
            goto out;
        for (unsigned j = 0; j < count; j++) {
            uint8_t rec[NE_FIXUP_SIZE];
            if (!read_at(f, size, fixups + (uint64_t)j * NE_FIXUP_SIZE,
                         rec, sizeof(rec)))
                goto out;
            rc = ne_apply_record(f, size, base, h, modtab, nametab,
                                 info.module_count, image, i, rec, resolve, opaque);
            if (rc)
                goto out;
        }
    }
    rc = WIN_NE_OK;
out:
    fclose(f);
    if (rc)
        win_ne_free_image(image);
    return rc;
}

/* ---------------------------------------------------------------- resources */

/* A resource's id in the table: high bit, the number; else the offset of
 * its length-prefixed name from the table's start. */
static int res_id_match(FILE *f, uint64_t size, uint64_t table, uint16_t v,
                        uint16_t id, const char *name)
{
    if (v & 0x8000u)
        return !name && (v & 0x7fffu) == id;
    if (!name)
        return 0;
    uint8_t len;
    char buf[256];
    if (!read_at(f, size, table + v, &len, 1) || !read_at(f, size, table + v + 1, buf, len))
        return 0;
    size_t n = strlen(name);
    if (n != len)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char a = buf[i], b = name[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b)
            return 0;
    }
    return 1;
}

/* the resource of type by id or name, or (nth >= 0) the nth of the type */
static int find_resource(const char *path, uint16_t type, uint16_t id, const char *name,
                         int nth, uint8_t **data, uint32_t *bytes, uint32_t *place)
{
    *data = NULL;
    *bytes = 0;
    if (name && name[0] == '#') {               /* "#123" is the number */
        id = 0;
        for (const char *p = name + 1; *p >= '0' && *p <= '9'; p++)
            id = (uint16_t)(id * 10 + (*p - '0'));
        name = NULL;
    }
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return WIN_NE_NO_FILE;
    int rc = WIN_NE_BAD_IMAGE;
    long end;
    uint8_t mz[64], h[NE_HDR_SIZE], w[2];
    if (fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0)
        goto out;
    uint64_t size = (uint64_t)end;
    if (!read_at(f, size, 0, mz, sizeof(mz)) || rd16(mz) != 0x5a4d)
        goto out;
    uint64_t base = rd32(mz + 0x3c);
    if (!read_at(f, size, base, h, sizeof(h)) || rd16(h) != 0x454e)
        goto out;
    uint16_t restab = rd16(h + 0x24), resname = rd16(h + 0x26);
    rc = WIN_NE_UNRESOLVED;                     /* no such resource */
    if (restab == resname)
        goto out;
    uint64_t table = base + restab, p = table;
    if (!read_at(f, size, p, w, 2))
        goto out;
    uint16_t shift = rd16(w);
    p += 2;
    for (;;) {
        uint8_t ti[8];
        if (!read_at(f, size, p, ti, 2) || !rd16(ti))
            goto out;
        if (!read_at(f, size, p, ti, 8))
            goto out;
        uint16_t t = rd16(ti), count = rd16(ti + 2);
        p += 8;
        int this_type = (t & 0x8000u) && (t & 0x7fffu) == type;
        for (unsigned i = 0; i < count; i++, p += 12) {
            uint8_t ni[12];
            if (!read_at(f, size, p, ni, sizeof(ni)))
                goto out;
            if (!this_type || (nth >= 0 ? nth-- != 0
                                            : !res_id_match(f, size, table, rd16(ni + 6), id, name)))
                continue;
            uint64_t at = (uint64_t)rd16(ni) << shift;
            uint32_t len = (uint32_t)rd16(ni + 2) << shift;
            if (place) {                        /* where it is, not its bytes */
                *place = (uint32_t)at;
                *bytes = len;
                rc = at + len <= size ? WIN_NE_OK : WIN_NE_BAD_IMAGE;
                goto out;
            }
            uint8_t *d = len ? (uint8_t *)malloc(len) : NULL;
            if (!d) {
                rc = WIN_NE_NO_MEMORY;
                goto out;
            }
            if (!read_at(f, size, at, d, len)) {
                free(d);
                rc = WIN_NE_BAD_IMAGE;
                goto out;
            }
            *data = d;
            *bytes = len;
            rc = WIN_NE_OK;
            goto out;
        }
    }
out:
    fclose(f);
    return rc;
}

int win_ne_resource(const char *path, uint16_t type, uint16_t id, const char *name,
                    uint8_t **data, uint32_t *bytes)
{
    return find_resource(path, type, id, name, -1, data, bytes, NULL);
}

int win_ne_resource_place(const char *path, uint16_t type, uint16_t id, const char *name,
                          uint32_t *place, uint32_t *bytes)
{
    uint8_t *none;
    return find_resource(path, type, id, name, -1, &none, bytes, place);
}

int win_ne_resource_nth(const char *path, uint16_t type, int n, uint8_t **data,
                        uint32_t *bytes)
{
    *data = NULL;
    *bytes = 0;
    return n < 0 ? WIN_NE_UNRESOLVED : find_resource(path, type, 0, NULL, n, data, bytes, NULL);
}

int win_ne_description(const char *path, char *buf, uint32_t cap)
{
    if (!buf || !cap)
        return 0;
    buf[0] = 0;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return 0;
    long end;
    uint8_t mz[64], h[NE_HDR_SIZE], len = 0;
    int ok = 0;
    if (!fseek(f, 0, SEEK_END) && (end = ftell(f)) >= 0) {
        uint64_t size = (uint64_t)end;
        uint64_t base;
        if (read_at(f, size, 0, mz, sizeof(mz)) && rd16(mz) == 0x5a4d &&
            read_at(f, size, base = rd32(mz + 0x3c), h, sizeof(h)) && rd16(h) == 0x454e &&
            rd16(h + 0x20) && read_at(f, size, rd32(h + 0x2c), &len, 1) && len) {
            uint32_t n = len < cap ? len : cap - 1;
            ok = read_at(f, size, (uint64_t)rd32(h + 0x2c) + 1, buf, n);
            buf[ok ? n : 0] = 0;
        }
    }
    fclose(f);
    return ok;
}

/* the file and its NE header; NULL if it is none */
static FILE *open_header(const char *path, uint64_t *size, uint64_t *base,
                         uint8_t h[NE_HDR_SIZE])
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    long end;
    uint8_t mz[64];
    if (!f)
        return NULL;
    if (!fseek(f, 0, SEEK_END) && (end = ftell(f)) >= 0) {
        *size = (uint64_t)end;
        if (read_at(f, *size, 0, mz, sizeof(mz)) && rd16(mz) == 0x5a4d &&
            read_at(f, *size, *base = rd32(mz + 0x3c), h, NE_HDR_SIZE) && rd16(h) == 0x454e)
            return f;
    }
    fclose(f);
    return NULL;
}

int win_ne_entry(const char *path, uint16_t ordinal, uint8_t *flags, uint16_t *segment,
                 uint16_t *offset)
{
    uint64_t size, base;
    uint8_t h[NE_HDR_SIZE];
    FILE *f = open_header(path, &size, &base, h);
    if (!f)
        return WIN_NE_NO_FILE;
    int rc = WIN_NE_CANCELLED;          /* past the last */
    uint64_t at = base + rd16(h + 4), end = at + rd16(h + 6);
    uint32_t current = 1;
    while (ordinal && at < end && end <= size) {
        uint8_t count, type;
        if (!read_at(f, size, at++, &count, 1) || !count)
            break;
        if (!read_at(f, size, at++, &type, 1))
            break;
        unsigned width = type == 0 ? 0 : type == 0xffu ? 6 : 3;
        if (ordinal >= current && ordinal < current + count) {
            uint8_t e[6];
            rc = WIN_NE_UNRESOLVED;     /* an unused one */
            if (type && type != 0xfeu &&
                read_at(f, size, at + (uint64_t)(ordinal - current) * width, e, width)) {
                *flags = e[0];
                *segment = type == 0xffu ? e[3] : type;
                *offset = type == 0xffu ? rd16(e + 4) : rd16(e + 1);
                rc = WIN_NE_OK;
            }
            break;
        }
        current += count;
        at += (uint64_t)count * width;
    }
    fclose(f);
    return rc;
}

/* a names table (length-prefixed names, each with its ordinal; a zero
 * length ends it; the first is the module's): the ordinal of name */
static uint16_t names_ordinal(FILE *f, uint64_t size, uint64_t at, const char *name)
{
    size_t n = strlen(name);
    int first = 1;
    for (;;) {
        uint8_t len, w[2];
        char buf[256];
        if (!read_at(f, size, at, &len, 1) || !len || !read_at(f, size, at + 1, buf, len) ||
            !read_at(f, size, at + 1 + len, w, 2))
            return 0;
        if (!first && len == n) {
            size_t i = 0;
            for (; i < n; i++) {
                char a = buf[i], b = name[i];
                if (a >= 'a' && a <= 'z') a = (char)(a - 32);
                if (b >= 'a' && b <= 'z') b = (char)(b - 32);
                if (a != b)
                    break;
            }
            if (i == n)
                return rd16(w);
        }
        first = 0;
        at += 3u + len;
    }
}

uint16_t win_ne_ordinal(const char *path, const char *name)
{
    uint64_t size, base;
    uint8_t h[NE_HDR_SIZE];
    FILE *f = name ? open_header(path, &size, &base, h) : NULL;
    if (!f)
        return 0;
    uint16_t o = names_ordinal(f, size, base + rd16(h + 0x26), name);
    if (!o && rd16(h + 0x20))
        o = names_ordinal(f, size, rd32(h + 0x2c), name);
    fclose(f);
    return o;
}
