/*
 * ne_guest.h - Native WINN's guest-memory NE staging and 16-bit descriptors.
 *
 * A staging operation runs entirely in DOS real mode. It allocates DOS-owned
 * memory, writes a GDT and relocated NE segments, and emits FAR-callable x86
 * thunks. It does NOT load GDTR, enable PE or start a Win16 task.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_NE_GUEST_H
#define WINN_NE_GUEST_H

#include <stddef.h>
#include <stdint.h>
#include "ne.h"

/* (Word for Windows 1.0 has 196) */
#define WIN_NE_GUEST_MAX_SEGMENTS 1024u
#define WIN_NE_THUNK_BYTES 16u /* includes FAR return + Pascal stack adjustment */
/* the thunk segment (ne_boot.h) up to its boot code: the API thunks */
#define WIN_NE_GUEST_ACTIVE_THUNKS 384u
/* The program's PSP (its own selector, ES after InitTask): the command line
 * at 81h, as DOS has it, but ending with a NUL (Windows). */
#define WIN_NE_GUEST_PSP_BYTES 256u
/* GDT entries for the program's global memory blocks (GlobalAlloc) */
#define WIN_NE_GUEST_GLOBALS 64u
/* Service numbers beyond KERNEL's (its ordinals), USER's (0x1000 + n) and
 * GDI's (0x3000 + n): KEYBOARD's, and KERNEL.102 DOS3Call, whose thunk is
 * an INT 21h of its own (the registers are its arguments) */
#define WIN_NE_SERVICE_KEYBOARD 0x4000u
#define WIN_NE_SERVICE_DOS3CALL 102u
/* USER's keybd_event and mouse_event: their arguments are the registers
 * (AX pushed by the thunk, the first word of the service's stack below the
 * caller's return address; the others as the gateway saved them) */
#define WIN_NE_SERVICE_KEYBD_EVENT 0x1121u
#define WIN_NE_SERVICE_MOUSE_EVENT 0x112bu
#define WIN_NE_SERVICE_SHELL    0x6000u
/* WIN87EM's: __FPMATH (1) is x86 code of WINN's (the coprocessor is the
 * emulator's own); the rest native */
#define WIN_NE_SERVICE_WIN87EM  0x7000u
#define WIN_NE_SERVICE_FPMATH   (WIN_NE_SERVICE_WIN87EM + 1u)
/* Windows' own DLLs WINN has (ne_guest_sys.c): TOOLHELP */
#define WIN_NE_SERVICE_TOOLHELP 0x8800u
/* the native DLLs' (core/windll.c): 9000h + the DLL's slot << 10 + the
 * export's place in its table (below 1024: its ordinal may be more); 12
 * slots */
#define WIN_NE_SERVICE_DLL      0x9000u
#define WIN_NE_DLL_SERVICE(slot, n) ((uint16_t)(WIN_NE_SERVICE_DLL + ((unsigned)(slot) << 10) + (n)))
/* A function of Windows' WINN has not (of its modules, or of Windows' own
 * ones it has nothing of): the program loads, its call of it ends it, as
 * Windows' "undefined dynalink" would. C000h + the module (its index, 4
 * bits) << 10 + the ordinal (1016 + n: the nth imported by name) */
#define WIN_NE_SERVICE_UNDEFINED 0xC000u
_Static_assert(WIN_NE_SERVICE_DLL + (12u << 10) == WIN_NE_SERVICE_UNDEFINED, "the DLLs' services");
/* GetWinFlags: protected mode, a 386, 386 enhanced mode (WINN runs 32-bit
 * programs, Win32's), a coprocessor */
#define WIN_NE_WINFLAGS         0x0425u

/* Guest linear allocations must be addressable via an x86 segment base and
 * remain allocated until the entire staged image is released. The callbacks
 * must not switch the shared CPU to protected mode. */
typedef struct {
    int (*alloc)(void *opaque, uint32_t bytes, uint32_t *linear);
    void (*free)(void *opaque, uint32_t linear);
    int (*write)(void *opaque, uint32_t linear, const void *bytes, uint32_t size);
    /* the allocation at linear made bytes long where it is: 0 if it cannot
     * be (what follows is in use); NULL: never */
    int (*resize)(void *opaque, uint32_t linear, uint32_t bytes);
    /* below 1M, where Real Mode reaches it (the trampoline, the PSP, the
     * descriptor table, GlobalDOSAlloc's); NULL: alloc's is */
    int (*alloc_low)(void *opaque, uint32_t bytes, uint32_t *linear);
    /* extended memory of any size (a Win32 program's), 16-aligned; NULL:
     * none */
    int (*alloc_ext)(void *opaque, uint32_t bytes, uint32_t *linear);
} win_ne_guest_io;

/* the guest memory WINN may read and write directly: below 1M, and the
 * programs' blocks of extended memory (ne_guest_dos.c, the host's) */
int win_ne_guest_mapped(uint32_t linear, uint32_t bytes);

/* the programs' memory (WINN's trace): the bytes their blocks have in DOS
 * memory and in extended memory, what they may have of that; the largest
 * block DOS has free in conventional memory, and with the UMBs */
typedef struct {
    uint32_t dos_used, ext_used, ext_size, dos_free, dos_free_umb;
} win_ne_mem_info;
void win_ne_guest_mem_info(win_ne_mem_info *m);
/* the extended memory given back (WINN ends) */
void win_ne_guest_mem_end(void);

typedef struct {
    uint32_t linear;
    uint32_t size;
    uint16_t selector;
    uint16_t flags;
} win_ne_guest_segment;

/* A DLL the program imports from (or loads): its module name, its file,
 * its segments (dll_segments[first ..], count of them), its DGROUP (a
 * segment number of its own, 0 none), its entry (LibEntry, selector and
 * offset; cs 0 none) and local heap's size; when it was loaded completely
 * (its dependencies first: the order its entries are called in), whether
 * that has been done. */
#define WIN_NE_GUEST_DLLS 8u
#define WIN_NE_GUEST_DLL_SEGMENTS 48u
typedef struct {
    char name[9];
    char path[160];
    uint16_t first, count, auto_data;
    uint16_t cs, ip, heap_size;
    uint8_t order, initialized;
} win_ne_guest_dll;

typedef struct {
    win_ne_info info;
    win_ne_guest_io io;
    void *opaque;
    win_ne_guest_segment *segments; /* 1-based; 0 unused */
    uint32_t gdt_linear, thunk_linear;
    uint16_t gdt_limit;
    /* the thunk segment as ring 0 code (WINN's) and as ring 3 code (the
     * thunks and stubs the program calls), the call gates to ring 0 (its
     * API calls, its entry's and its procedures' return), the TSS, the
     * ring 0 area's data */
    uint16_t thunk_selector, thunk3_selector;
    uint16_t api_gate, exit_gate, cbret_gate, tss_selector, ring0_selector;
    uint16_t bios_selector;        /* the BIOS data area (__0040H), read only */
    win_ne_guest_segment psp;       /* a data segment of its own */
    /* its global memory blocks, a selector each (0: a free entry) */
    win_ne_guest_segment globals[WIN_NE_GUEST_GLOBALS];
    uint16_t cs, ip, ss, sp;
    uint16_t thunk_count;
    /* Each active slot holds a Win16 service with a known Pascal ABI. */
    uint16_t thunk_services[WIN_NE_GUEST_ACTIVE_THUNKS];
    /* Owned host-side descriptor image; copied into guest RAM on success. */
    uint8_t *gdt;
    char path[160];                 /* the program's file (DLLs are looked for by it) */
    /* its DLLs, their segments (selectors after the GDT's extras) */
    win_ne_guest_dll dlls[WIN_NE_GUEST_DLLS];
    uint16_t dll_count, dll_segment_count;
    uint8_t dll_order;
    win_ne_guest_segment dll_segments[WIN_NE_GUEST_DLL_SEGMENTS];
    /* the functions it imports by name that WINN has not (its calls of
     * them end it: WIN_NE_SERVICE_UNDEFINED) */
    char undefined[8][32];
    /* a self-loading one's (WIN_NE_SELF_LOADING): its module database (a
     * global block: the selector its loader is given as hModule), the
     * segment its loader is loading (0: none), the loader's stack (a
     * global block) */
    uint16_t module_db, loading, loader_stack;
} win_ne_guest_image;

/* Complete staging is atomic with respect to allocations (all allocated
 * guest blocks are freed on error). Only services with implemented Win16
 * argument marshalling are resolved. Unsupported imports return WIN_NE_UNRESOLVED;
 * no ARM code address is exposed to guest execution. */
int win_ne_guest_load(const char *path, win_ne_guest_image *out,
                      const win_ne_guest_io *io, void *opaque);
void win_ne_guest_release(win_ne_guest_image *image);

/* The first import a program needs that WINN does not have (the load
 * fails with WIN_NE_UNRESOLVED): 1 with it in u, 0 if there is none. */
typedef struct {
    char module[16];
    char name[40];
    uint16_t ordinal;               /* 0: by name */
} win_ne_unresolved;
int win_ne_guest_unresolved(const char *path, win_ne_unresolved *u);
/* a WIN_NE_SERVICE_UNDEFINED call's text: "MODULE.ordinal", "MODULE.Name" */
void win_ne_guest_undefined_text(const win_ne_guest_image *g, uint16_t service, char *out,
                                 size_t cap);

/* A global memory block of the program (DOS memory, a data selector of its
 * own, zeroed): its selector, 0 if there is no memory or no entry left.
 * Resized (moved: same selector), freed (all of them go with the image). */
uint16_t win_ne_guest_global_alloc(win_ne_guest_image *g, uint32_t bytes);
/* the same below 1M (GlobalDOSAlloc, DPMI 0100h) */
uint16_t win_ne_guest_global_alloc_low(win_ne_guest_image *g, uint32_t bytes);
int win_ne_guest_global_realloc(win_ne_guest_image *g, uint16_t sel, uint32_t bytes,
                                int (*read)(void *, uint32_t, void *, uint32_t));
int win_ne_guest_global_free(win_ne_guest_image *g, uint16_t sel);
/* Selectors of its own (AllocSelector ...), global entries with no memory
 * of their own (flags WIN_NE_GUEST_ALIAS; freed as a global block is): a
 * copy of another's descriptor (like 0: base 0, limit 0), code (1), data
 * (0) or as that one (-1). A descriptor's base, limit and kind (0 if the
 * selector is none of the program's); an alias's set. */
#define WIN_NE_GUEST_ALIAS 0x8000u
/* a self-loading module's segment its loader allocated (MyAlloc): in its
 * slot, a global block as GlobalAlloc's are (GlobalReAlloc, GlobalFree
 * ...); its other bits the NE segment's flags */
#define WIN_NE_GUEST_MYALLOC 0x4000u
uint16_t win_ne_guest_selector_alloc(win_ne_guest_image *g, uint16_t like, int code);
int win_ne_guest_selector_get(const win_ne_guest_image *g, uint16_t sel, uint32_t *base,
                              uint32_t *limit, int *code);
/* a selector of the program's (one it may load): 1 */
int win_ne_guest_selector_live(const win_ne_guest_image *g, uint16_t sel);
int win_ne_guest_selector_set(win_ne_guest_image *g, uint16_t sel, uint32_t base,
                              uint32_t limit, int code);
/* n empty selectors one after the other (AllocSelectorArray): the first */
uint16_t win_ne_guest_selector_array(win_ne_guest_image *g, unsigned n);
/* GetProcAddress of a module WINN has (0 KERNEL, 1 USER, 2 GDI, 3
 * KEYBOARD, 4 SHELL, 5 WIN87EM, 6 TOOLHELP;
 * win_ne_guest_module gives the number of a name, -1 for none): a thunk's
 * far address, made now if the program had none for it, 0 if WINN has not
 * the function. */
uint32_t win_ne_guest_proc(win_ne_guest_image *g, unsigned module, uint16_t ordinal,
                           const char *name);
/* ... the thunk of a service, a native DLL's (WIN_NE_DLL_SERVICE) */
uint32_t win_ne_guest_thunk(win_ne_guest_image *g, uint16_t id);
/* Native DLLs (core/windll.c, through ne_guest_sys.c; none in a program
 * without them): the service of a module's export, the DLL loaded and the
 * current task's use of it counted (1), no native DLL of the name (0), one
 * without that function by name (-1); has: the same, the DLL let go after;
 * the bytes of a service's PASCAL arguments; its name */
int win_ne_native_import(const char *module, uint16_t ordinal, const char *name, uint16_t *id);
int win_ne_native_has(const char *module, uint16_t ordinal, const char *name);
uint16_t win_ne_native_args(uint16_t id);
const char *win_ne_native_name(uint16_t id, char *buf, unsigned cap);
int win_ne_guest_module(const char *name);
/* ... and the name of number m (NULL past them) */
const char *win_ne_guest_module_name(int m);
/* DLLs. LoadLibrary: the DLL's index (loaded now if it was not: its
 * segments, its descriptors in the guest's GDT, the DLLs it needs), -1 if
 * it cannot be; its entry is the caller's to call. The far address of one
 * of its exports (by ordinal, or by name), 0 if none. Its instance handle
 * (its DGROUP's selector, else its first segment's). The file a module
 * (an instance handle: the program's, a DLL's) has its resources in. */
int win_ne_guest_load_library(win_ne_guest_image *g, const char *name);
uint32_t win_ne_guest_dll_proc(win_ne_guest_image *g, int dll, uint16_t ordinal,
                               const char *name);
uint16_t win_ne_guest_dll_instance(const win_ne_guest_image *g, int dll);
int win_ne_guest_dll_of(const win_ne_guest_image *g, uint16_t instance);
const char *win_ne_guest_path_of(const win_ne_guest_image *g, uint16_t instance,
                                 const char *program);
/* the segment (the program's, its PSP, a global block, a DLL's) a selector
 * is of, NULL if none; it made bigger (a data segment) */
win_ne_guest_segment *win_ne_guest_segment_of(win_ne_guest_image *g, uint16_t sel);
int win_ne_guest_grow(win_ne_guest_image *g, uint16_t sel, uint32_t bytes,
                      int (*read)(void *, uint32_t, void *, uint32_t));
/* A self-loading module's (WIN_NE_SELF_LOADING): its module database built
 * (a global block, g->module_db: the NE header and tables as Windows keeps
 * them, the segment table's entries with their handles, the module
 * references the handles modref gives for the names); its selector, 0 if
 * it cannot be */
uint16_t win_ne_guest_module_db(win_ne_guest_image *g,
                                uint16_t (*modref)(void *opaque, const char *module),
                                void *opaque);
/* KERNEL's MyAlloc for its loader: a zeroed block for one of its segments
 * (code but for DATA flags), in the slot of the segment being loaded
 * (g->loading), or the first free one, else a global block; its selector
 * (its handle as well), 0 if there is no memory */
uint16_t win_ne_guest_myalloc(win_ne_guest_image *g, uint16_t flags, uint32_t bytes);
/* its loader loaded segment k (LoadAppSeg's answer, handle): the program's
 * segment k from now on, DGROUP grown by its local heap and stack, the
 * database's entry; 0 if handle is no block of the program's */
int win_ne_guest_selfloaded(win_ne_guest_image *g, uint16_t k, uint16_t handle,
                            int (*read)(void *, uint32_t, void *, uint32_t));
/* the name of a service (a Win16 function), NULL if WINN has none */
const char *win_ne_service_name(uint16_t service);
win_ne_guest_segment *win_ne_guest_global(win_ne_guest_image *g, uint16_t sel);

/* The marshalling of a generic function (core/ne_sigs.h) by its service
 * number, NULL if it is not one: "result:arguments" */
const char *win_ne_generic_sig(uint16_t service);

/* Returns the DOS INT 21h-backed guest allocator on the RP2350 target.
 * Allocation/free must run before entering protected mode or after leaving it. */
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
const win_ne_guest_io *win_ne_guest_dos_io(void);
#endif

#endif /* WINN_NE_GUEST_H */
