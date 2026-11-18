/*
 * pe_int.h - Win32 programs (PE) on the 386 core: what the loader (pe.c),
 * the executor (ne_exec.c), KERNEL32 (pe_api.c) and USER32 and GDI32
 * (pe_user.c) share.
 *
 * A Win32 program runs in ring 3 in a flat 4G code and data segment, FS
 * its thread's information block. The segments' base is where its image
 * is less the address it was linked for (ImageBase), so a program without
 * relocations (most Win32 programs' EXEs) runs where WINN has the memory:
 * its addresses are the guest's linear ones less that base (modulo 4G);
 * win_pe_ptr and win_pe_addr convert them. Its calls
 * of WINN's functions are thunks of WINN's (mov eax,service; call far the
 * 386 call gate to the gateway; ret n), as a Win16 program's are, only
 * 32-bit. Its DLLs are loaded from their files (PE as well), WINN's own
 * modules (KERNEL32, USER32, GDI32) are native.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_PE_INT_H
#define WINN_PE_INT_H

#include <stdint.h>
#include <string.h>
#include "wincore.h"
#include "dos_guest_ptr.h"
#include "ne_guest.h"
#include "ne_exec.h"
#include "ne_boot.h"

/* WINN's modules: KERNEL32, USER32, GDI32 its own; WINMM the native
 * MMSYSTEM.DLL's (core/pe_api.c's table); WING32 pe_user.c's; Win32s'
 * others pe_dlls.c's */
enum { PE_KERNEL32, PE_USER32, PE_GDI32, PE_WINMM, PE_WING32, PE_ADVAPI32, PE_COMDLG32,
       PE_SHELL32, PE_LZ32, PE_VERSION, PE_MODULES };
extern const char *const win_pe_module_names[PE_MODULES];

/* a function of WINN's: sig NULL for those with code of their own */
typedef struct {
    uint8_t module;
    const char *name;
    const char *sig;
    const void *fn;
    int16_t args;           /* bytes; -1 a C function's (the caller pops) */
} win_pe_fn;

#include "pe_sigs.h"
extern const win_pe_fn win_pe_fns[];
extern const unsigned win_pe_nfns;

/* services beyond the functions (win_pe_fns' indexes): the yield vector
 * (as a Win16 program's), the start (the DLLs' entries, then the
 * program's), a function WINN has not (its call ends the program: 4000h
 * + its place in the image's list) */
#define WIN_PE_SERVICE_YIELD     WIN_NE_SERVICE_YIELD
#define WIN_PE_SERVICE_START     0x2201u
/* an exception (the fault stubs' service, ne_boot.h), the dispatcher's
 * (its next handler, pe_api.c) */
#define WIN_PE_SERVICE_EXCEPTION WIN_PE_SERVICE_EXCEPTION_
#define WIN_PE_SERVICE_SEH       0x2203u
#define WIN_PE_SERVICE_UNDEFINED 0x4000u

#define WIN_PE_MODULE_SLOTS 16
#define WIN_PE_THUNK_BLOCKS 32      /* of 256 thunks of 16 bytes */
#define WIN_PE_THUNKS_PER_BLOCK 256u
#define WIN_PE_UNDEFINED 32

/* the descriptor table: null, the thunk segment as ring 0 and ring 3 code,
 * the API, exit and callback-return gates (386 ones), the TSS, the ring 0
 * area, the flat code and data, the thread's information block */
enum { PE_GDT_THUNK = 1, PE_GDT_THUNK3, PE_GDT_API, PE_GDT_EXIT, PE_GDT_CBRET, PE_GDT_TSS,
       PE_GDT_RING0, PE_GDT_CODE, PE_GDT_DATA, PE_GDT_FS, PE_GDT_ENTRIES };

/* the thread's information block (NT's TEB, what programs read of it) */
#define WIN_PE_TIB_BYTES 0x1000u
#define WIN_PE_TIB_TLS   0xe10u     /* 64 slots */
#define WIN_PE_TLS_SLOTS 64u
#define WIN_PE_TIB_PEB   0x800u     /* the process's block, after it */

typedef struct {
    char name[16];              /* "OWDLL32" */
    char path[160];
    uint32_t base, size;        /* the program's addresses (as all here) */
    uint32_t entry;             /* 0: none */
    uint32_t exports, exports_size, resources, resources_size;
    uint8_t dll, attached;      /* DllMain(DLL_PROCESS_ATTACH) called */
    uint8_t order;              /* loaded completely: the order of the entries */
    uint16_t refs;              /* LoadLibrary's */
} win_pe_module;

/* a block of guest memory WINN gave the program (its image, VirtualAlloc,
 * its heaps' chunks): where it is (the program's address), its size, the
 * allocation (linear) */
typedef struct {
    uint32_t base, size, raw;
    uint8_t kind;               /* PE_MEM_ */
} win_pe_mem;
enum { PE_MEM_FREE, PE_MEM_IMAGE, PE_MEM_VIRTUAL, PE_MEM_HEAP, PE_MEM_OTHER };

typedef struct win_pe_image {
    win_ne_guest_io io;
    void *opaque;
    uint8_t gdt[PE_GDT_ENTRIES * 8];
    uint32_t gdt_linear;
    uint16_t gdt_limit;
    uint32_t flat;              /* the flat segments' base: program address +
                                 * flat = linear */
    uint32_t thunk_linear;      /* the thunk segment (ne_boot.h's layout) */
    uint16_t thunk_selector, thunk3_selector, api_gate, exit_gate, cbret_gate;
    uint16_t tss_selector, ring0_selector, code_selector, data_selector, fs_selector;
    uint32_t tib;               /* the program's address */
    uint32_t stack_lo, stack_top;
    win_pe_module mod[WIN_PE_MODULE_SLOTS];
    int nmod;                   /* 0 the program's */
    uint8_t order;
    /* the thunks: blocks of them, their services */
    uint32_t thunk_block[WIN_PE_THUNK_BLOCKS];
    unsigned thunks;
    uint16_t thunk_service[WIN_PE_THUNK_BLOCKS * WIN_PE_THUNKS_PER_BLOCK];
    char undefined[WIN_PE_UNDEFINED][48];
    int nundefined;
    char unresolved[48];        /* the first DLL it needs that is not there */
    /* the guest memory it has */
    win_pe_mem *mem;
    int nmem;
} win_pe_image;

/* ---------------------------------------------------------------- pe.c */

/* a Win32 program or DLL for the 386 (MZ, PE, i386): 1 */
int win_pe_is(const char *path);
/* the program staged (its DLLs too), the descriptor table and the thunk
 * segment made; WIN_NE_ codes */
int win_pe_load(const char *path, win_pe_image *img, const win_ne_guest_io *io, void *opaque);
void win_pe_release(win_pe_image *img);
/* LoadLibrary: the module's index (loaded now, the DLLs it needs too), -1
 * if it cannot be; its entry is the caller's to call (not yet attached) */
int win_pe_load_library(win_pe_image *img, const char *name);
/* GetProcAddress of a module of the program's (by ordinal: name NULL):
 * its address, 0 none */
uint32_t win_pe_export(win_pe_image *img, int m, const char *name, uint16_t ordinal);
/* the module whose image an address (an HMODULE, an HINSTANCE) is in, -1
 * none */
int win_pe_module_of(const win_pe_image *img, uint32_t linear);
/* one of WINN's modules ("KERNEL32", "kernel32.dll"): PE_*, -1 none; the
 * thunk of its function, made now if there is none (0: WINN has not it) */
int win_pe_native_module(const char *name);
uint32_t win_pe_native_proc(win_pe_image *img, int module, const char *name, uint16_t ordinal);
uint32_t win_pe_thunk(win_pe_image *img, uint16_t service, int args);
/* a resource of module m (FindResource): its entry's address (an HRSRC),
 * its data's address and size; type and name numbers (below 10000h) or
 * native strings ("#12" a number); 0 none. An entry's data and size. */
uint32_t win_pe_resource(win_pe_image *img, int m, const char *type, const char *name,
                         uint32_t *data, uint32_t *size);
uint32_t win_pe_resource_data(win_pe_image *img, int m, uint32_t entry, uint32_t *size);
/* guest memory: a block of bytes (aligned: a power of two, 16 at least),
 * zeroed; freed; its record */
uint32_t win_pe_alloc(win_pe_image *img, uint32_t bytes, uint32_t align, int kind);
int win_pe_free(win_pe_image *img, uint32_t base);
win_pe_mem *win_pe_mem_of(win_pe_image *img, uint32_t linear);

/* the program's memory: a pointer to its address (NULL for 0 or memory it
 * has not: bytes of it), the address of a pointer to it */
static inline void *win_pe_ptr(const win_pe_image *img, uint32_t addr, uint32_t bytes)
{
    uint32_t linear = addr + img->flat;
    if (!addr || !win_ne_guest_mapped(linear, bytes ? bytes : 1u))
        return NULL;
    return dos_guest_linear_ptr(linear);
}
static inline uint32_t win_pe_addr(const win_pe_image *img, const void *p)
{
    return p ? (uint32_t)((const uint8_t *)p - (const uint8_t *)dos_guest_linear_ptr(0)) -
               img->flat : 0;
}

/* ---------------------------------------------------------------- the running program */

#define PE_HANDLES 64
#define PE_CLASSES 32
#define PE_DIALOGS 16
#define PE_TIMERS  16
#define PE_SUBCLASSED 32
#define PE_PAINTS  8
#define PE_HEAPS   8
#define PE_FINDS   8

/* the kernel objects of the program's (its HANDLEs: 100h + the slot) */
enum { PE_H_FREE, PE_H_FILE, PE_H_FIND, PE_H_EVENT, PE_H_MUTEX, PE_H_STD, PE_H_PROCESS,
       PE_H_THREAD, PE_H_HEAP, PE_H_SEMAPHORE };

typedef struct win_pe_ctx {
    win_pe_image *img;
    HTASK task;
    const char *path;
    int show;
    int (*yield)(void);
    void *exec;                 /* ne_exec.c's (callbacks) */
    /* how it ended, as win_ne_result says */
    int exited, stopped, dead, dead_call;
    uint32_t exit_code;
    uint16_t unsupported;
    char undefined[48];
    win_ne_fault fault;
    int invalid_args;
    uint16_t invalid_call;
    uint32_t last_error;
    uint32_t cmdline;           /* GetCommandLineA's (linear) */
    uint32_t cmdline_w;
    uint32_t environment;       /* GetEnvironmentStrings' */
    uint32_t heap;              /* the process's heap (a handle) */
    uint32_t tls_used[2];       /* bits */
    uint32_t exception_filter;  /* SetUnhandledExceptionFilter's */
    uint32_t seh_code;          /* the exceptions' dispatcher (its code, made
                                 * at the first one) */
    int filtering;              /* in UnhandledExceptionFilter */
    struct { uint8_t kind; int32_t fd; uint32_t value; } handle[PE_HANDLES];
    struct pe_user *user;       /* pe_user.c's */
} win_pe_ctx;

/* the arguments of a call: the program's ESP at its call (the return
 * address, then the arguments, a dword each) */
typedef struct {
    win_pe_ctx *ctx;
    uint32_t esp;
    int bad;
} pe_args;

static inline uint32_t pe_arg(pe_args *a, unsigned i)
{
    const uint8_t *p = (const uint8_t *)win_pe_ptr(a->ctx->img, a->esp + 4u + 4u * i, 4);
    if (!p) {
        a->bad = 1;
        return 0;
    }
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint16_t pe_rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t pe_rd32(const uint8_t *p) { return pe_rd16(p) | (uint32_t)pe_rd16(p + 2) << 16; }
static inline void pe_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void pe_wr32(uint8_t *p, uint32_t v) { pe_wr16(p, (uint16_t)v); pe_wr16(p + 2, (uint16_t)(v >> 16)); }

/* snprintf (WINN's target has none): flags - 0 + space #, width,
 * precision (a number or *), the length l h (ignored); d i u x X o c s %;
 * its length */
int pe_fmt(char *out, size_t cap, const char *fmt, ...);

/* the program's string at addr (NULL: none or not its memory) */
const char *pe_str(pe_args *a, uint32_t addr);
/* its buffer of bytes at addr (NULL: 0; bad: not its memory) */
void *pe_buf(pe_args *a, uint32_t addr, uint32_t bytes);

/* ne_exec.c: the program's procedure (its address) called with n dword
 * arguments (stdcall), blob copied below them on its stack first (*at:
 * where, for arguments that point at it; back into blob after); its EAX */
uint32_t win_pe_call(win_pe_ctx *ctx, uint32_t proc, const uint32_t *args, int n, void *blob,
                     uint32_t len, uint32_t *at);
/* where the next call's blob of len bytes goes (0: none can be made now) */
uint32_t win_pe_blob(win_pe_ctx *ctx, uint32_t len);

/* a thread's registers where an exception happened (or RaiseException
 * returns to) */
typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
} win_pe_regs;

/* pe_api.c: an exception (a fault's vector, -1 RaiseException's; its
 * code, flags, arguments): its record and CONTEXT on the program's stack
 * for its dispatcher, where it is to go on (*eip, *esp): 1; else
 * WINX86_SERVICE_EXIT (it ends: nowhere to put them) */
int win_pe_exception(win_pe_ctx *ctx, const win_pe_regs *r, int vector, uint32_t code,
                     uint32_t flags, uint32_t nparams, const uint32_t *params, uint32_t *eip,
                     uint32_t *esp);
/* a fault's exception code (vector, error code, the instruction at eip),
 * its arguments (2) */
uint32_t win_pe_fault_code(win_pe_ctx *ctx, int vector, uint32_t eip, uint32_t *params,
                           uint32_t *nparams);

/* pe_api.c: a call of the program's (1 done, result *eax; 0 WINN has not
 * it; WINX86_SERVICE_EXIT it ends there) */
int win_pe_api(win_pe_ctx *ctx, uint32_t esp, uint16_t service, uint32_t *eax);
/* the program's state made (its command line, environment, heap, USER's),
 * and done with */
void win_pe_start(win_pe_ctx *ctx, const char *cmdline);
void win_pe_end(win_pe_ctx *ctx);
/* pe_user.c's: USER32's and GDI32's (and its start and end); the native
 * window procedures it holds (their thunks: WIN_PE_SERVICE_NATIVE + n) */
int pe_user_api(pe_args *a, uint16_t service, uint32_t *eax);
#define WIN_PE_SERVICE_NATIVE 0x4800u
#define WIN_PE_NATIVE_PROCS 64u
/* WINMM's functions: MMSYSTEM.DLL's (4900h + their place in its table) */
#define WIN_PE_SERVICE_MM 0x4900u
#define WIN_PE_SERVICE_MM16 0x4c00u     /* those MMSYSTEM.DLL has for Win16 callers */
/* a WINMM function's name (NULL: the service is none of them) */
const char *win_pe_mm_name(uint16_t service);
uint32_t win_pe_mm_proc(win_pe_image *img, const char *name);
#define WIN_PE_SERVICE_WING 0x4a00u
uint32_t win_pe_wing_proc(win_pe_image *img, const char *name);
/* pe_dlls.c: ADVAPI32's, COMDLG32's, SHELL32's, LZ32's, VERSION's
 * functions (4B00h + their place in its table); a call of one (1 done);
 * a function's name; a registered message's lParam a native structure of
 * theirs (the Find dialog's): the program's own (0 none); the program's
 * end */
#define WIN_PE_SERVICE_DLLS 0x4b00u
uint32_t win_pe_dll_proc(win_pe_image *img, int module, const char *name);
int win_pe_dll_api(pe_args *a, uint16_t service, uint32_t *eax);
const char *win_pe_dll_name(uint16_t service);
uint32_t win_pe_dll_lparam(win_pe_ctx *ctx, uint32_t msg, uint32_t lp);
void win_pe_dlls_end(win_pe_ctx *ctx);
/* pe_user.c: COMMDLG's dialog of the program's template (inst's resource
 * tpl, or the template at tpl: handle), its procedure native; a window
 * procedure of the program's (a hook) called with a message as USER sends
 * it */
int win_pe_dialog_native(pe_args *a, uint32_t inst, uint32_t tpl, int handle, HWND owner,
                         DLGPROC proc, int modeless);
uint32_t win_pe_message(win_pe_ctx *ctx, uint32_t proc, HWND h, UINT msg, WPARAM wp, LPARAM lp);
void pe_user_start(win_pe_ctx *ctx);
void pe_user_end(win_pe_ctx *ctx);
/* the instance of the program's task for native functions */
static inline HINSTANCE pe_instance(win_pe_ctx *ctx, uint32_t h) { return h ? (HINSTANCE)ctx->task : 0; }
/* the module an HINSTANCE (or HMODULE) is: the program's for its task's,
 * 0 for NULL */
int pe_module_of_instance(win_pe_ctx *ctx, uint32_t h);
/* the guest heap: a block of the process's heap, freed (pe_api.c) */
uint32_t pe_heap_alloc(win_pe_ctx *ctx, uint32_t heap, uint32_t bytes, int zero);
int pe_heap_free(win_pe_ctx *ctx, uint32_t heap, uint32_t p);
/* a block's size (0: none of the heap's) */
uint32_t pe_heap_size(win_pe_ctx *ctx, uint32_t heap, uint32_t p);
/* a copy of a string in the program's heap */
uint32_t pe_strdup(win_pe_ctx *ctx, const char *s);

#endif /* WINN_PE_INT_H */
