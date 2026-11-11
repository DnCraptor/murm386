/*
 * ne_guest_int.h - what the Win16 services of ne_guest_user.c and
 * ne_guest_call.c share: the state of a running program, its arguments.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_NE_GUEST_INT_H
#define WINN_NE_GUEST_INT_H

#include <string.h>
#include "wincore.h"
#include "ne_guest_api.h"

#define NE_CLASSES 16
#define NE_PAINTS  4
#define NE_DIALOGS 8
#define NE_ACCELS  4
#define NE_TIMERS  16
#define NE_SUBCLASSED 16
#define NE_LOCALS  64
#define NE_HEAPS   4
#define NE_LENT    8
#define NE_IMAGES  16
#define NE_RESOURCES 16
#define NE_HOOKS   8

typedef struct ne_user {
    struct ne_user *next;
    win_ne_guest_api_context *ctx;
    HTASK task;
    struct {
        char name[32];
        uint32_t proc;
        char menu[32];              /* its menu resource: a name, */
        uint16_t menu_id;           /* or a number */
    } cls[NE_CLASSES];
    struct { HWND hwnd; PAINTSTRUCT ps; } paint[NE_PAINTS];
    /* the dialog procedures of its dialogs (and the lParam of their
     * WM_INITDIALOG), and those of the dialog being made */
    struct { HWND hwnd; uint32_t proc, param; } dlg[NE_DIALOGS];
    uint32_t pending, pending_param;
    HACCEL accel[NE_ACCELS];        /* destroyed at the end */
    /* its timers with procedures of its own */
    struct { HWND hwnd; UINT id; uint32_t proc; } timer[NE_TIMERS];
    /* windows it subclassed: their procedure (its own), the one before */
    struct { HWND hwnd; uint32_t proc; WNDPROC old; } sub[NE_SUBCLASSED];
    /* its local heap in DGROUP: blocks (offset, size) */
    /* the local heaps (heap 0 DGROUP's, the others its DLLs'): the
     * segment's selector, the heap's place in it, where it grows from (the
     * segment's end as it was); blocks (offset, size) of each */
    struct { uint16_t sel, lo, hi, end; } heap[NE_HEAPS];
    struct { uint16_t at, size; uint8_t heap; } local[NE_LOCALS];
    /* native pointers handed to its procedures as guest copies (lParam of
     * WM_CREATE, WM_GETMINMAXINFO ...), for DefWindowProc to take back */
    struct { uint32_t far; void *native; UINT msg; } lent[NE_LENT];
    int lents;
    /* DOS (ne_guest_int21.c): the DTA (0: the PSP's), the search, the last
     * error, the environment's block */
    uint32_t dta;
    struct { char spec[128]; uint16_t attr; int next; } find;
    uint16_t dos_error;
    uint16_t env;
    /* its icons (kind 2) and cursors (1), by number or name */
    struct { HANDLE h; uint8_t kind; uint16_t id, inst; char name[32]; } image[NE_IMAGES];
    /* ne_guest_kernel.c: the resources it found (FindResource), its error
     * mode */
    struct { uint16_t type, id, inst; char name[32]; } res[NE_RESOURCES];
    uint16_t error_mode;
    /* ne_guest_call.c: the clipboard's data it got, copies in its memory
     * (freed as it closes or empties the clipboard) */
    struct { UINT fmt; uint16_t sel; } clip[8];
    /* ne_guest_sys.c: timeSetEvent's procedures, mmio's files (0: none) */
    /* ne_guest_call.c: its hooks (SetWindowsHook(Ex)): the type, its
     * procedure, the task it is for (0 all its own), its number (HHOOK's
     * low word, made one after the other: the later the first called) */
    struct { int16_t type; uint16_t task, id; uint32_t proc; } hook[NE_HOOKS];
} ne_user;


typedef struct {
    win_ne_guest_api_context *ctx;
    win_ne_regs *cpu;
    int bad;
    uint16_t inst;          /* the module its resources come from: 0 the program's */
} args;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

/* the argument word at SP + off (the first is at 4) */
static inline uint16_t w(args *a, uint32_t off)
{
    uint16_t v = 0;
    if (!win_ne_arg(a->ctx, a->cpu, off, &v))
        a->bad = 1;
    return v;
}

static inline uint32_t dw(args *a, uint32_t off) { return w(a, off) | (uint32_t)w(a, off + 2) << 16; }

/* a Win16 coordinate or size; CW_USEDEFAULT is 8000h there */
static inline int coord(uint16_t v) { return v == 0x8000u ? CW_USEDEFAULT : (int)(int16_t)v; }

/* a name, or an atom (selector 0): a native LPCSTR */
static inline LPCSTR name_or_atom(args *a, uint32_t far, char *buf, uint32_t cap)
{
    if (!(far >> 16))
        return MAKEINTRESOURCE((WORD)far);
    if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, buf, cap)) {
        a->bad = 1;
        buf[0] = 0;
    }
    return buf;
}

/* the program's hInstance (its DGROUP selector, from InitTask): its task */
static inline HINSTANCE instance(ne_user *u, uint16_t h) { return h ? (HINSTANCE)u->task : 0; }

/* Win16 MSG: hwnd, message, wParam (words), lParam, time (dwords), pt */
static inline void msg_to_guest(const MSG *m, uint8_t g[18])
{
    wr16(g, (uint16_t)m->hwnd);
    wr16(g + 2, (uint16_t)m->message);
    wr16(g + 4, (uint16_t)m->wParam);
    wr32(g + 6, (uint32_t)m->lParam);
    wr32(g + 10, m->time);
    wr16(g + 14, (uint16_t)m->pt.x);
    wr16(g + 16, (uint16_t)m->pt.y);
}

static inline int msg_from_guest(args *a, uint32_t far, MSG *m)
{
    uint8_t g[18];
    if (!win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, g, sizeof(g)))
        return a->bad = 1, 0;
    memset(m, 0, sizeof(*m));
    m->hwnd = rd16(g);
    m->message = rd16(g + 2);
    m->wParam = rd16(g + 4);
    m->lParam = (LPARAM)rd32(g + 6);
    m->time = rd32(g + 10);
    m->pt.x = (int16_t)rd16(g + 14);
    m->pt.y = (int16_t)rd16(g + 16);
    return 1;
}

/* ne_guest_user.c: the program of a task; whether a window is of one of
 * its classes (its own procedure) */
ne_user *ne_user_of(HTASK task);
int ne_own_window(ne_user *u, HWND h);
/* ne_guest_call.c: the services the generic way, memory, structures,
 * messages with pointers, timers, subclassing; 1 if it is one of them */
int ne_call(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword);
void ne_call_end(ne_user *u);
/* ne_guest_call.c: a DDE message's global memory in lParam (WM_DDE_DATA's
 * ...) as the program's own, a copy of WINN's (remove: retrieved) */
uint32_t ne_dde_in(win_ne_guest_api_context *c, ne_user *u, UINT msg, uint32_t lp, int remove);
/* ne_guest_int21.c: KERNEL's DOS functions (OpenFile, GetDOSEnvironment
 * ...) and KEYBOARD's; 1 if it is one of them */
int ne_dos_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword);
/* user/dialog.c: the menu of the dialog made next (its window's then) */
void user_dialog_menu(HMENU menu);
/* user/msg.c: the hooks USER calls (user.h has when), their types' bits */
#ifndef USER_HOOK_BIT
#define USER_HOOK_BIT(type) (1u << ((type) + 1))
#endif
void user_set_hooks(unsigned types, LRESULT (*call)(int type, int code, WPARAM wp, LPARAM lp));
/* ne_guest_int21.c: the DOS environment block, its selector in the PSP */
void ne_environment(win_ne_guest_api_context *ctx, ne_user *u);
/* ne_guest_int21.c: OpenFile's work, the name given (NULL: the
 * program's, its first argument) */
uint16_t ne_open_file(args *a, const char *name);
/* ne_guest_sys.c: TOOLHELP, the native DLLs; 1 if it is one of theirs */
int ne_sys_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword);
/* ... the service of a native DLL's export */
uint16_t win_ne_dll_service(int slot, const struct win_dll_export *e);
/* ne_guest_kernel.c: KERNEL's modules, resources and the rest, GDI's and
 * USER's few left, SHELL; 1 if it is one of them */
int ne_kernel_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword);
/* a procedure of the program with a message, lParam lent as a guest copy
 * when it points to a structure; a native one (DefWindowProc ...) with
 * lParam from the program, a lent copy given back */
LRESULT ne_message(ne_user *u, uint32_t proc, HWND h, UINT msg, WPARAM wp, LPARAM lp);
LRESULT ne_defproc(args *a, ne_user *u, LRESULT (WINAPI *fn)(HWND, UINT, WPARAM, LPARAM),
                   HWND h, UINT msg, WPARAM wp, uint32_t lp);
/* ne_guest_user.c: wsprintf into dst with fmt; the arguments on the stack
 * (list 0) or at list (wvsprintf) */
int ne_format(args *a, uint32_t dst, uint32_t fmt, uint32_t list);

#endif
