/*
 * pe.c - Win32 programs and DLLs (PE) staged in guest memory: the image
 * (headers, sections), its relocations, its imports (WINN's modules'
 * functions as thunks, the program's DLLs from their files, loaded the
 * same way), its exports and resources; the descriptor table, the thunk
 * segment and the thread's information block the 386 executor needs
 * (ne_exec.c).
 *
 * Every offset of the file and of the image is checked before it is used.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
#include "wincore.h"
#define WIN_PE_SIGS_TABLE 1         /* the functions' table is here */
#include "pe_int.h"
#include "ne_boot.h"


const char *win_ne_dll_dir(void);
const char *win_ne_dll_system_dir(void);

static const char *const native_names[PE_MODULES] = { "KERNEL32", "USER32", "GDI32" };

/* ---------------------------------------------------------------- the file */

typedef struct {
    uint32_t entry, image_base, size_image, size_headers;
    uint32_t stack_reserve, stack_commit;
    uint16_t machine, nsections, characteristics, subsystem;
    uint32_t sections;              /* the section table's offset */
    uint32_t ndirs;
    uint32_t dir[16][2];            /* rva, size */
} pe_header;

static int read_at(FILE *f, uint32_t off, void *buf, uint32_t n)
{
    return off <= LONG_MAX && !fseek(f, (long)off, SEEK_SET) && fread(buf, 1, n, f) == n;
}

static int header(FILE *f, pe_header *h)
{
    uint8_t mz[64], pe[24 + 224];
    memset(h, 0, sizeof(*h));
    if (!read_at(f, 0, mz, sizeof(mz)) || mz[0] != 'M' || mz[1] != 'Z')
        return 0;
    uint32_t at = pe_rd32(mz + 0x3c);
    if (at < 64 || at > 0x100000u || !read_at(f, at, pe, 24) ||
        memcmp(pe, "PE\0\0", 4))
        return 0;
    h->machine = pe_rd16(pe + 4);
    h->nsections = pe_rd16(pe + 6);
    uint16_t optsize = pe_rd16(pe + 20);
    h->characteristics = pe_rd16(pe + 22);
    if (h->machine != 0x14c || optsize < 96 || optsize > 224 ||
        !read_at(f, at + 24, pe + 24, optsize) || pe_rd16(pe + 24) != 0x10b)
        return 0;
    const uint8_t *o = pe + 24;
    h->entry = pe_rd32(o + 16);
    h->image_base = pe_rd32(o + 28);
    h->size_image = pe_rd32(o + 56);
    h->size_headers = pe_rd32(o + 60);
    h->subsystem = pe_rd16(o + 68);
    h->stack_reserve = pe_rd32(o + 72);
    h->stack_commit = pe_rd32(o + 76);
    h->ndirs = pe_rd32(o + 92);
    if (h->ndirs > 16)
        h->ndirs = 16;
    if (96u + 8u * h->ndirs > optsize)
        h->ndirs = (optsize - 96u) / 8u;
    for (uint32_t i = 0; i < h->ndirs; i++) {
        h->dir[i][0] = pe_rd32(o + 96 + 8 * i);
        h->dir[i][1] = pe_rd32(o + 100 + 8 * i);
    }
    h->sections = at + 24u + optsize;
    return h->size_image && h->size_image <= 0x4000000u && h->nsections &&
           h->nsections <= 96 && h->size_headers <= h->size_image;
}

int win_pe_is(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return 0;
    pe_header h;
    int ok = header(f, &h) && (h.subsystem == 2 || h.subsystem == 3);
    fclose(f);
    return ok;
}

/* ---------------------------------------------------------------- memory */

static win_pe_mem *mem_slot(win_pe_image *img)
{
    for (int i = 0; i < img->nmem; i++)
        if (img->mem[i].kind == PE_MEM_FREE)
            return &img->mem[i];
    win_pe_mem *m = (win_pe_mem *)realloc(img->mem, sizeof(*m) * (size_t)(img->nmem + 32));
    if (!m)
        return NULL;
    memset(m + img->nmem, 0, sizeof(*m) * 32);
    img->mem = m;
    img->nmem += 32;
    return &img->mem[img->nmem - 32];
}

/* linear memory of the guest's, aligned, zeroed; 0 none */
static uint32_t alloc_linear(win_pe_image *img, uint32_t bytes, uint32_t align, uint32_t *raw)
{
    if (align < 16)
        align = 16;
    if (!bytes || bytes > 0x4000000u || !img->io.alloc_ext ||
        !img->io.alloc_ext(img->opaque, bytes + align - 16u, raw))
        return 0;
    uint32_t linear = (*raw + align - 1u) & ~(align - 1u);
    memset(dos_guest_linear_ptr(linear), 0, bytes);
    return linear;
}

uint32_t win_pe_alloc(win_pe_image *img, uint32_t bytes, uint32_t align, int kind)
{
    win_pe_mem *m = mem_slot(img);
    uint32_t raw;
    uint32_t linear = m ? alloc_linear(img, bytes, align, &raw) : 0;
    if (!linear)
        return 0;
    m->base = linear - img->flat;
    m->size = bytes;
    m->raw = raw;
    m->kind = (uint8_t)kind;
    return m->base;
}

win_pe_mem *win_pe_mem_of(win_pe_image *img, uint32_t addr)
{
    for (int i = 0; i < img->nmem; i++)
        if (img->mem[i].kind && addr - img->mem[i].base < img->mem[i].size)
            return &img->mem[i];
    return NULL;
}

int win_pe_free(win_pe_image *img, uint32_t base)
{
    for (int i = 0; i < img->nmem; i++)
        if (img->mem[i].kind && img->mem[i].base == base) {
            img->io.free(img->opaque, img->mem[i].raw);
            img->mem[i].kind = PE_MEM_FREE;
            return 1;
        }
    return 0;
}

/* ---------------------------------------------------------------- names */

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* "C:\\DIR\\OWDLL32.DLL" -> "OWDLL32" */
static void stem(const char *path, char *out, size_t cap)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    size_t n = 0;
    while (b[n] && b[n] != '.' && n + 1 < cap) {
        out[n] = (char)upper((unsigned char)b[n]);
        n++;
    }
    out[n] = 0;
}

int win_pe_native_module(const char *name)
{
    char s[16];
    stem(name, s, sizeof(s));
    for (int i = 0; i < PE_MODULES; i++)
        if (!strcmp(s, native_names[i]))
            return i;
    return -1;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* a DLL's file: beside the program, in Windows' directory, its system
 * directory */
static int locate(const char *program, const char *module, char *out, size_t cap)
{
    char base[16], name[24];
    stem(module, base, sizeof(base));
    if (!base[0])
        return 0;
    const char *dot = NULL;
    for (const char *p = module; *p; p++)
        if (*p == '.')
            dot = p;
        else if (*p == '\\' || *p == '/')
            dot = NULL;
    const char *ext = dot ? dot : ".DLL";
    if (strlen(base) + strlen(ext) >= sizeof(name))
        return 0;
    strcpy(name, base);
    strcat(name, ext);
    const char *dirs[3] = { program, win_ne_dll_dir(), win_ne_dll_system_dir() };
    for (int i = 0; i < 3; i++) {
        if (!dirs[i])
            continue;
        size_t n = strlen(dirs[i]);
        if (i == 0)
            while (n && dirs[i][n - 1] != '\\' && dirs[i][n - 1] != '/' && dirs[i][n - 1] != ':')
                n--;
        if (n + strlen(name) + 2 > cap)
            continue;
        memcpy(out, dirs[i], n);
        if (i && n && out[n - 1] != '\\' && out[n - 1] != '/')
            out[n++] = dirs[i][0] && strchr(dirs[i], '\\') ? '\\' : '/';
        strcpy(out + n, name);
        if (file_exists(out))
            return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- thunks */

uint32_t win_pe_thunk(win_pe_image *img, uint16_t service, int args)
{
    for (unsigned i = 0; i < img->thunks; i++)
        if (img->thunk_service[i] == service)
            return img->thunk_block[i / WIN_PE_THUNKS_PER_BLOCK] +
                   16u * (i % WIN_PE_THUNKS_PER_BLOCK);
    unsigned i = img->thunks;
    unsigned b = i / WIN_PE_THUNKS_PER_BLOCK;
    if (b >= WIN_PE_THUNK_BLOCKS)
        return 0;
    if (!img->thunk_block[b]) {
        img->thunk_block[b] = win_pe_alloc(img, 16u * WIN_PE_THUNKS_PER_BLOCK, 16, PE_MEM_OTHER);
        if (!img->thunk_block[b])
            return 0;
        memset(win_pe_ptr(img, img->thunk_block[b], 16u * WIN_PE_THUNKS_PER_BLOCK), 0xcc,
               16u * WIN_PE_THUNKS_PER_BLOCK);
    }
    uint32_t at = img->thunk_block[b] + 16u * (i % WIN_PE_THUNKS_PER_BLOCK);
    uint8_t *p = (uint8_t *)win_pe_ptr(img, at, 16);
    p[0] = 0xb8;                            /* mov eax,service */
    pe_wr32(p + 1, service);
    p[5] = 0x9a;                            /* call far gate:0 */
    pe_wr32(p + 6, 0);
    pe_wr16(p + 10, img->api_gate);
    if (args > 0) {                         /* ret n (stdcall) */
        p[12] = 0xc2;
        pe_wr16(p + 13, (uint16_t)args);
    } else {
        p[12] = 0xc3;                       /* ret (C, none) */
    }
    img->thunk_service[i] = service;
    img->thunks++;
    return at;
}

uint32_t win_pe_native_proc(win_pe_image *img, int module, const char *name, uint16_t ordinal)
{
    if (!name || module < 0)
        return 0;                           /* WINN's have no ordinals */
    (void)ordinal;
    for (unsigned i = 0; i < win_pe_nfns; i++)
        if (win_pe_fns[i].module == module && !strcmp(win_pe_fns[i].name, name))
            return win_pe_thunk(img, (uint16_t)i, win_pe_fns[i].args);
    return 0;
}

/* a function none has: a thunk whose call ends the program */
static uint32_t undefined(win_pe_image *img, const char *module, const char *name,
                          uint16_t ordinal)
{
    char text[48], base[16];
    stem(module, base, sizeof(base));
    if (name)
        pe_fmt(text, sizeof(text), "%s.%s", base, name);
    else
        pe_fmt(text, sizeof(text), "%s.%u", base, ordinal);
    int k = 0;
    while (k < img->nundefined && strcmp(img->undefined[k], text))
        k++;
    if (k == img->nundefined) {
        if (k == WIN_PE_UNDEFINED)
            k--;                            /* the last one stands for the rest */
        else
            img->nundefined++;
        memcpy(img->undefined[k], text, sizeof(text));
    }
    return win_pe_thunk(img, (uint16_t)(WIN_PE_SERVICE_UNDEFINED + k), 0);
}

const char *win_pe_service_name(uint16_t service, char *buf, size_t cap)
{
    if (service < win_pe_nfns) {
        pe_fmt(buf, cap, "%s.%s", native_names[win_pe_fns[service].module],
                 win_pe_fns[service].name);
        return buf;
    }
    return NULL;
}

/* ---------------------------------------------------------------- exports */

int win_pe_module_of(const win_pe_image *img, uint32_t addr)
{
    for (int i = 0; i < img->nmod; i++)
        if (img->mod[i].base && addr - img->mod[i].base < img->mod[i].size)
            return i;
    return -1;
}

static int load(win_pe_image *img, const char *path, int dll);

uint32_t win_pe_export(win_pe_image *img, int m, const char *name, uint16_t ordinal)
{
    if (m < 0 || m >= img->nmod || !img->mod[m].exports)
        return 0;
    const win_pe_module *mod = &img->mod[m];
    const uint8_t *d = (const uint8_t *)win_pe_ptr(img, mod->exports, 40);
    if (!d || mod->exports_size < 40)
        return 0;
    uint32_t base = pe_rd32(d + 16), nfun = pe_rd32(d + 20), nnames = pe_rd32(d + 24);
    uint32_t funs = pe_rd32(d + 28), names = pe_rd32(d + 32), ords = pe_rd32(d + 36);
    uint32_t index = (uint32_t)ordinal - base;
    if (name) {
        index = UINT32_MAX;
        for (uint32_t i = 0; i < nnames && i < 65536u; i++) {
            const uint8_t *np = (const uint8_t *)win_pe_ptr(img, mod->base + names + 4u * i, 4);
            const uint8_t *op = (const uint8_t *)win_pe_ptr(img, mod->base + ords + 2u * i, 2);
            const char *s = np ? (const char *)win_pe_ptr(img, mod->base + pe_rd32(np), 1) : NULL;
            if (!np || !op || !s)
                return 0;
            if (!strcmp(s, name)) {
                index = pe_rd16(op);
                break;
            }
        }
    }
    if (index >= nfun)
        return 0;
    const uint8_t *fp = (const uint8_t *)win_pe_ptr(img, mod->base + funs + 4u * index, 4);
    uint32_t rva = fp ? pe_rd32(fp) : 0;
    if (!rva || rva >= mod->size)
        return 0;
    if (rva - (mod->exports - mod->base) < mod->exports_size) {
        /* a forwarder: "MODULE.name" */
        const char *s = (const char *)win_pe_ptr(img, mod->base + rva, 1);
        char target[24];
        const char *dot = s ? strchr(s, '.') : NULL;
        if (!dot || (size_t)(dot - s) >= sizeof(target))
            return 0;
        memcpy(target, s, (size_t)(dot - s));
        target[dot - s] = 0;
        int nm = win_pe_native_module(target);
        if (nm >= 0)
            return win_pe_native_proc(img, nm, dot + 1, 0);
        int dm = win_pe_load_library(img, target);
        return dm >= 0 && dm != m ? win_pe_export(img, dm, dot + 1, 0) : 0;
    }
    return mod->base + rva;
}

/* ---------------------------------------------------------------- loading */

static int find_module(win_pe_image *img, const char *name)
{
    char s[16];
    stem(name, s, sizeof(s));
    for (int i = 0; i < img->nmod; i++)
        if (img->mod[i].base && !strcmp(img->mod[i].name, s))
            return i;
    return -1;
}

int win_pe_load_library(win_pe_image *img, const char *name)
{
    int m = find_module(img, name);
    if (m >= 0)
        return m;
    char path[160];
    if (!locate(img->mod[0].path, name, path, sizeof(path)))
        return -1;
    return load(img, path, 1);
}

static int relocate(win_pe_image *img, const pe_header *h, uint32_t base, uint32_t delta)
{
    if (!delta)
        return 1;
    if (h->ndirs <= 5 || !h->dir[5][0] || (h->characteristics & 1u))
        return 0;                           /* where it was linked for only */
    uint32_t at = h->dir[5][0], end = at + h->dir[5][1];
    if (end < at || end > h->size_image)
        return 0;
    while (at + 8 <= end) {
        const uint8_t *b = (const uint8_t *)win_pe_ptr(img, base + at, 8);
        uint32_t page = pe_rd32(b), size = pe_rd32(b + 4);
        if (size < 8 || size > end - at)
            break;
        for (uint32_t k = 8; k + 2 <= size; k += 2) {
            uint16_t e = pe_rd16(b + k);
            uint32_t off = page + (e & 0xfffu);
            switch (e >> 12) {
            case 0:
                break;
            case 3: {                       /* HIGHLOW */
                if (off > h->size_image - 4u)
                    return 0;
                uint8_t *p = (uint8_t *)win_pe_ptr(img, base + off, 4);
                pe_wr32(p, pe_rd32(p) + delta);
                break;
            }
            case 1: case 2: {               /* HIGH, LOW */
                if (off > h->size_image - 2u)
                    return 0;
                uint8_t *p = (uint8_t *)win_pe_ptr(img, base + off, 2);
                pe_wr16(p, (uint16_t)(pe_rd16(p) + ((e >> 12) == 1 ? delta >> 16 : delta)));
                break;
            }
            default:
                return 0;
            }
        }
        at += size;
    }
    return 1;
}

static int imports(win_pe_image *img, int m, const pe_header *h)
{
    if (h->ndirs <= 1 || !h->dir[1][0])
        return 1;
    uint32_t base = img->mod[m].base;
    for (uint32_t at = h->dir[1][0]; at + 20 <= h->size_image; at += 20) {
        const uint8_t *d = (const uint8_t *)win_pe_ptr(img, base + at, 20);
        uint32_t lookup = pe_rd32(d), name = pe_rd32(d + 12), iat = pe_rd32(d + 16);
        if (!name && !iat)
            break;
        const char *mod = name < h->size_image ? (const char *)win_pe_ptr(img, base + name, 1) : NULL;
        if (!mod || !iat || iat >= h->size_image)
            return 0;
        char modname[40];
        pe_fmt(modname, sizeof(modname), "%.39s", mod);
        int nm = win_pe_native_module(modname), dm = -1;
        if (nm < 0) {
            dm = win_pe_load_library(img, modname);
            base = img->mod[m].base;
            if (dm < 0) {
                if (!img->unresolved[0])
                    pe_fmt(img->unresolved, sizeof(img->unresolved), "%s", modname);
                return 0;
            }
        }
        if (!lookup)
            lookup = iat;
        for (uint32_t k = 0; ; k++) {
            if (lookup + 4u * k + 4u > h->size_image || iat + 4u * k + 4u > h->size_image)
                return 0;
            uint32_t v = pe_rd32((const uint8_t *)win_pe_ptr(img, base + lookup + 4u * k, 4));
            if (!v)
                break;
            const char *fn = NULL;
            uint16_t ordinal = 0;
            if (v & 0x80000000u)
                ordinal = (uint16_t)v;
            else if (v + 2u < h->size_image)
                fn = (const char *)win_pe_ptr(img, base + v + 2u, 1);
            else
                return 0;
            uint32_t addr = nm >= 0 ? win_pe_native_proc(img, nm, fn, ordinal)
                                    : win_pe_export(img, dm, fn, ordinal);
            if (!addr)
                addr = undefined(img, modname, fn, ordinal);
            if (!addr)
                return 0;
            pe_wr32((uint8_t *)win_pe_ptr(img, base + iat + 4u * k, 4), addr);
        }
    }
    return 1;
}

/* a module from its file: its index, -1 if it cannot be */
static int load(win_pe_image *img, const char *path, int dll)
{
    if (img->nmod >= WIN_PE_MODULE_SLOTS)
        return -1;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    pe_header h;
    int m = -1;
    if (!header(f, &h) || (dll != !!(h.characteristics & 0x2000u)))
        goto done;
    /* the program at the address it was linked for (the segments' base
     * made so), a DLL where there is room, relocated */
    uint32_t raw, linear = alloc_linear(img, h.size_image, 4096, &raw);
    if (!linear)
        goto done;
    if (!dll)
        img->flat = linear - h.image_base;
    win_pe_mem *mem = mem_slot(img);
    if (!mem) {
        img->io.free(img->opaque, raw);
        goto done;
    }
    mem->base = linear - img->flat;
    mem->size = h.size_image;
    mem->raw = raw;
    mem->kind = PE_MEM_IMAGE;
    m = img->nmod++;
    win_pe_module *mod = &img->mod[m];
    memset(mod, 0, sizeof(*mod));
    mod->base = mem->base;
    mod->size = h.size_image;
    mod->dll = (uint8_t)dll;
    stem(path, mod->name, sizeof(mod->name));
    pe_fmt(mod->path, sizeof(mod->path), "%s", path);
    uint8_t *image = (uint8_t *)dos_guest_linear_ptr(linear);
    int ok = read_at(f, 0, image, h.size_headers);
    for (uint16_t i = 0; ok && i < h.nsections; i++) {
        uint8_t s[40];
        if (!read_at(f, h.sections + 40u * i, s, sizeof(s))) {
            ok = 0;
            break;
        }
        uint32_t vsize = pe_rd32(s + 8), va = pe_rd32(s + 12);
        uint32_t rsize = pe_rd32(s + 16), roff = pe_rd32(s + 20);
        uint32_t n = vsize && vsize < rsize ? vsize : rsize;
        if (va > h.size_image || n > h.size_image - va || (vsize > h.size_image - va))
            ok = 0;
        else if (n && roff && !read_at(f, roff, image + va, n))
            ok = 0;
    }
    if (ok)
        ok = relocate(img, &h, mod->base, mod->base - h.image_base);
    if (ok && h.ndirs > 0 && h.dir[0][0] && h.dir[0][0] < h.size_image &&
        h.dir[0][1] <= h.size_image - h.dir[0][0]) {
        mod->exports = mod->base + h.dir[0][0];
        mod->exports_size = h.dir[0][1];
    }
    if (ok && h.ndirs > 2 && h.dir[2][0] && h.dir[2][0] < h.size_image &&
        h.dir[2][1] <= h.size_image - h.dir[2][0]) {
        mod->resources = mod->base + h.dir[2][0];
        mod->resources_size = h.dir[2][1];
    }
    if (ok && h.entry && h.entry < h.size_image)
        mod->entry = mod->base + h.entry;
    if (!dll) {
        img->stack_lo = h.stack_commit > h.stack_reserve ? h.stack_commit : h.stack_reserve;
        if (img->stack_lo < 0x10000u)
            img->stack_lo = 0x10000u;
        if (img->stack_lo > 0x40000u)
            img->stack_lo = 0x40000u;
    }
    fclose(f);
    f = NULL;
    if (ok)                                 /* the DLLs it needs load here */
        ok = imports(img, m, &h);
    if (!ok) {
        win_pe_free(img, img->mod[m].base);
        memset(&img->mod[m], 0, sizeof(img->mod[m]));
        if (m == img->nmod - 1)
            img->nmod--;
        m = -1;
    } else {
        img->mod[m].order = ++img->order;
    }
done:
    if (f)
        fclose(f);
    return m;
}

/* ---------------------------------------------------------------- resources */

/* an entry of a resource directory by a number or a native string (the
 * directory's names are counted Unicode); the first if want is NULL */
static uint32_t res_entry(win_pe_image *img, const win_pe_module *mod, uint32_t dir,
                          const char *want)
{
    const uint8_t *d = (const uint8_t *)win_pe_ptr(img, mod->resources + dir, 16);
    if (!d || dir > mod->resources_size - 16u)
        return 0;
    unsigned named = pe_rd16(d + 12), ids = pe_rd16(d + 14);
    for (unsigned i = 0; i < named + ids; i++) {
        const uint8_t *e = (const uint8_t *)win_pe_ptr(img, mod->resources + dir + 16u + 8u * i, 8);
        if (!e)
            return 0;
        uint32_t id = pe_rd32(e), off = pe_rd32(e + 4);
        int match = !want;
        if (want && (id & 0x80000000u) && (uintptr_t)want >= 0x10000u) {
            const uint8_t *s = (const uint8_t *)win_pe_ptr(img, mod->resources + (id & 0x7fffffffu), 2);
            unsigned n = s ? pe_rd16(s) : 0;
            const uint8_t *u = s ? (const uint8_t *)win_pe_ptr(img, mod->resources +
                                   (id & 0x7fffffffu) + 2u, 2u * n + 1u) : NULL;
            match = u && strlen(want) == n;
            for (unsigned k = 0; match && k < n; k++)
                match = upper(pe_rd16(u + 2 * k)) == upper((unsigned char)want[k]);
        } else if (want && !(id & 0x80000000u) && (uintptr_t)want < 0x10000u) {
            match = id == (uintptr_t)want;
        }
        if (match)
            return off ? off : 0xffffffffu;
    }
    return 0;
}

uint32_t win_pe_resource_data(win_pe_image *img, int m, uint32_t entry, uint32_t *size)
{
    if (m < 0 || m >= img->nmod || !img->mod[m].resources)
        return 0;
    const win_pe_module *mod = &img->mod[m];
    const uint8_t *data = (const uint8_t *)win_pe_ptr(img, entry, 16);
    if (!data || entry - mod->resources > mod->resources_size - 16u)
        return 0;
    uint32_t rva = pe_rd32(data), n = pe_rd32(data + 4);
    if (rva >= mod->size || n > mod->size - rva)
        return 0;
    if (size)
        *size = n;
    return mod->base + rva;
}

uint32_t win_pe_resource(win_pe_image *img, int m, const char *type, const char *name,
                         uint32_t *data, uint32_t *size)
{
    if (m < 0 || m >= img->nmod || !img->mod[m].resources)
        return 0;
    const win_pe_module *mod = &img->mod[m];
    /* "#123" is the number 123 */
    const char *keys[2] = { type, name };
    for (int i = 0; i < 2; i++)
        if ((uintptr_t)keys[i] >= 0x10000u && keys[i][0] == '#') {
            unsigned v = (unsigned)atoi(keys[i] + 1);
            keys[i] = (const char *)(uintptr_t)v;
        }
    uint32_t e = res_entry(img, mod, 0, keys[0]);
    if (!(e & 0x80000000u) || e == 0xffffffffu)
        return 0;
    e = res_entry(img, mod, e & 0x7fffffffu, keys[1]);
    if (!(e & 0x80000000u) || e == 0xffffffffu)
        return 0;
    e = res_entry(img, mod, e & 0x7fffffffu, NULL);        /* its first language */
    if (!e || (e & 0x80000000u) || e > mod->resources_size - 16u)
        return 0;
    uint32_t at = win_pe_resource_data(img, m, mod->resources + e, size);
    if (data)
        *data = at;
    return at ? mod->resources + e : 0;
}

/* ---------------------------------------------------------------- the image */

static void seg_descriptor(uint8_t *p, uint32_t base, uint32_t size, uint8_t access,
                           uint8_t flags)
{
    uint32_t limit = flags & 0x80u ? 0xfffffu : size - 1u;
    pe_wr16(p, (uint16_t)limit);
    pe_wr16(p + 2, (uint16_t)base);
    p[4] = (uint8_t)(base >> 16);
    p[5] = access;
    p[6] = (uint8_t)(flags | ((limit >> 16) & 0x0fu));
    p[7] = (uint8_t)(base >> 24);
}

/* a 386 call gate, DPL 3, to ring 0 code */
static void call_gate(uint8_t *p, uint16_t selector, uint16_t offset)
{
    pe_wr16(p, offset);
    pe_wr16(p + 2, selector);
    p[4] = 0;
    p[5] = 0xec;
    pe_wr16(p + 6, 0);
}

int win_pe_load(const char *path, win_pe_image *img, const win_ne_guest_io *io, void *opaque)
{
    memset(img, 0, sizeof(*img));
    if (!io || !io->alloc_ext || !io->free || !io->alloc_low)
        return WIN_NE_BAD_IMAGE;
    img->io = *io;
    img->opaque = opaque;
    if (!win_pe_is(path))
        return WIN_NE_BAD_IMAGE;
    int rc = WIN_NE_NO_MEMORY;
    img->gdt_limit = sizeof(img->gdt) - 1u;
    img->thunk_selector = PE_GDT_THUNK * 8u;
    img->thunk3_selector = PE_GDT_THUNK3 * 8u | 3u;
    img->api_gate = PE_GDT_API * 8u | 3u;
    img->exit_gate = PE_GDT_EXIT * 8u | 3u;
    img->cbret_gate = PE_GDT_CBRET * 8u | 3u;
    img->tss_selector = PE_GDT_TSS * 8u;
    img->ring0_selector = PE_GDT_RING0 * 8u;
    img->code_selector = PE_GDT_CODE * 8u | 3u;
    img->data_selector = PE_GDT_DATA * 8u | 3u;
    img->fs_selector = PE_GDT_FS * 8u | 3u;
    /* below 1M: the trampoline runs in Real Mode, the tables are loaded
     * there */
    if (!io->alloc_low(opaque, sizeof(img->gdt), &img->gdt_linear) ||
        !io->alloc_low(opaque, WIN_NE_THUNK_SEGMENT_BYTES, &img->thunk_linear))
        goto fail;
    rc = WIN_NE_UNSUPPORTED;
    if (load(img, path, 0) != 0)
        goto fail;
    rc = WIN_NE_NO_MEMORY;
    uint32_t stack = img->stack_lo;
    img->stack_lo = win_pe_alloc(img, stack, 16, PE_MEM_OTHER);
    img->tib = win_pe_alloc(img, WIN_PE_TIB_BYTES, 16, PE_MEM_OTHER);
    if (!img->stack_lo || !img->tib)
        goto fail;
    img->stack_top = img->stack_lo + stack;
    uint8_t *tib = (uint8_t *)win_pe_ptr(img, img->tib, WIN_PE_TIB_BYTES);
    pe_wr32(tib, 0xffffffffu);              /* the end of the exception handlers */
    pe_wr32(tib + 4, img->stack_top);
    pe_wr32(tib + 8, img->stack_lo);
    pe_wr32(tib + 0x18, img->tib);          /* itself */
    pe_wr32(tib + 0x20, 1);                 /* the process's id, the thread's */
    pe_wr32(tib + 0x24, 1);
    pe_wr32(tib + 0x2c, img->tib + WIN_PE_TIB_TLS);
    pe_wr32(tib + 0x30, img->tib + WIN_PE_TIB_PEB);
    pe_wr32(tib + WIN_PE_TIB_PEB + 8, img->mod[0].base);    /* ImageBaseAddress */

    uint8_t *g = img->gdt;
    seg_descriptor(g + img->thunk_selector, img->thunk_linear, WIN_NE_THUNK_SEGMENT_BYTES, 0x9a, 0);
    seg_descriptor(g + (img->thunk3_selector & ~7u), img->thunk_linear,
                   WIN_NE_THUNK_SEGMENT_BYTES, 0xfa, 0);
    call_gate(g + (img->api_gate & ~7u), img->thunk_selector, WIN_NE_BOOT_GATEWAY_OFFSET);
    call_gate(g + (img->exit_gate & ~7u), img->thunk_selector, WIN_NE_BOOT_EXIT_OFFSET);
    call_gate(g + (img->cbret_gate & ~7u), img->thunk_selector, WIN_NE_CBRET_OFFSET);
    seg_descriptor(g + img->tss_selector, img->thunk_linear + WIN_NE_RING0_OFFSET,
                   WIN_NE_TSS_BYTES, 0x81, 0);
    seg_descriptor(g + img->ring0_selector, img->thunk_linear + WIN_NE_RING0_OFFSET,
                   WIN_NE_RING0_BYTES, 0x92, 0);
    seg_descriptor(g + (img->code_selector & ~7u), img->flat, 0, 0xfa, 0xc0);
    seg_descriptor(g + (img->data_selector & ~7u), img->flat, 0, 0xf2, 0xc0);
    seg_descriptor(g + (img->fs_selector & ~7u), img->tib + img->flat, WIN_PE_TIB_BYTES, 0xf2, 0x40);
    memset(dos_guest_linear_ptr(img->thunk_linear), 0xcc, WIN_NE_THUNK_SEGMENT_BYTES);
    memcpy(dos_guest_linear_ptr(img->gdt_linear), img->gdt, sizeof(img->gdt));
    return WIN_NE_OK;
fail:
    if (!img->unresolved[0] && rc == WIN_NE_UNSUPPORTED) {
        /* a DLL missing is the usual reason */
    }
    win_pe_release(img);
    return rc;
}

void win_pe_release(win_pe_image *img)
{
    for (int i = 0; i < img->nmem; i++)
        if (img->mem[i].kind)
            img->io.free(img->opaque, img->mem[i].raw);
    free(img->mem);
    img->mem = NULL;
    img->nmem = 0;
    if (img->thunk_linear)
        img->io.free(img->opaque, img->thunk_linear);
    if (img->gdt_linear)
        img->io.free(img->opaque, img->gdt_linear);
    img->thunk_linear = img->gdt_linear = 0;
}
#endif
