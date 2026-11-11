/* Win16 x86 service dispatch, called in the original native WINN ARM thread. */
#ifndef WINN_NE_GUEST_API_H
#define WINN_NE_GUEST_API_H

#include "x86bridge.h"
#include "ne_guest.h"
#include "ne_exec.h"

/* The guest's general registers at a call, as the gateway left them; SP
 * and ss: the caller's stack (ring 3), at SS:SP the caller's far return
 * address, then the arguments. frame: the linear address in the ring 0
 * stack just above the gateway's return address, with the registers the
 * gateway saved below it and, for an interrupt (INT 21h, the yield
 * vector), the guest's AX, then the interrupt's IP, CS, FLAGS from it. */
typedef struct {
    gprx_t gprx[8];
    uint16_t ss;
    uint32_t frame;
    uint16_t ds;        /* the caller's DS: its local heap's (a DLL's or the program's) */
} win_ne_regs;

typedef struct {
    const win_ne_guest_image *image;
    int (*read)(void *opaque, uint32_t linear, void *dest, uint32_t bytes);
    void *opaque;
    int invalid_args;  /* latched; reported only after the CPU leaves PE */
    uint16_t invalid_call;  /* the first call refused for it (its service) */
    const char *path;  /* the program file (GetModuleFileName) */
    int show;          /* nCmdShow (InitTask) */
    int exited;        /* it ended itself (INT 21h 4Ch), with exit_code */
    uint8_t exit_code;
    uint16_t unsupported;   /* the call it made that WINN does not have:
                             * WIN_NE_SERVICE_INT21 + AH for INT 21h */
    char undefined[48];     /* a WIN_NE_SERVICE_UNDEFINED one: MODULE.function */
    int (*yield)(void);     /* its time slice is over: other tasks run;
                             * nonzero: it is to end (stopped) */
    int stopped;
    int dead;               /* faulted in a callback (fault) */
    int dead_call;          /* ... or called there what WINN has not, or refused */
    win_ne_fault fault;
    void *exec;             /* ne_exec's, for callbacks */
    void *user;             /* ne_guest_user's: classes, paints */
} win_ne_guest_api_context;

/* Marshals the declared Win16 PASCAL signatures, INT 21h from the guest's
 * registers. The caller MUST have switched the shared emulator to the
 * original DOS real-mode context before invoking this function: MessageBox
 * may run the native message loop. AX and DX of cpu are the results.
 * Returns 1, 0 when the call is not supported, WINX86_SERVICE_EXIT when
 * the program ends. */
int win_ne_guest_api(void *opaque, win_ne_regs *cpu, uint16_t service);

/* An INT 21h of the program (or its DOS3Call): its registers, as DOS
 * takes and gives them, and the carry flag (an error: its code in AX) */
typedef struct {
    uint16_t ax, bx, cx, dx, si, di, ds, es;
    int carry;
    int zero;               /* the BIOS's: ZF (INT 16h's "no key") */
} win_ne_dos_regs;
/* The DOS functions (ne_guest_int21.c; without it, none): 1 when done,
 * 0 when WINN does not have it. 30h and 4Ch are the gateway's own. */
int win_ne_dos(win_ne_guest_api_context *ctx, win_ne_dos_regs *r);
/* The software interrupts but INT 21h (the BIOS's 10h, 11h, 12h, 16h,
 * 1Ah; 22h to 33h: the multiplex 2Fh, DPMI's 31h, the mouse's 33h) as
 * Windows' DOS extender answers them for a program: the video and
 * equipment questions, the keyboard's state, the clock, Windows' version,
 * the descriptors and DOS memory; those it has not leave the registers as
 * they were (DPMI's: its error) (ne_guest_int21.c; without it, all of
 * them). */
void win_ne_bios(win_ne_guest_api_context *ctx, unsigned vector, win_ne_dos_regs *r);

/* The window and GDI services (ne_guest_user.c; without it, as in the
 * tests of the rest, there are none): 1 with the result in value (dword:
 * DX too), 0 when not one of them. */
int win_ne_guest_user(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint16_t service,
                      uint32_t *value, int *dword);
/* a program starts, ends (its classes and its window procedures) */
void win_ne_user_start(win_ne_guest_api_context *ctx);
void win_ne_user_end(win_ne_guest_api_context *ctx);

/* For those services: the guest's memory, by selector:offset, checked
 * against its segments (and PSP); an argument word at SP + offset; a NUL
 * terminated string (0:0 is ""). */
int win_ne_read(win_ne_guest_api_context *ctx, uint16_t sel, uint32_t off, void *dst, uint32_t n);
int win_ne_write(win_ne_guest_api_context *ctx, uint16_t sel, uint32_t off, const void *src,
                 uint32_t n);
int win_ne_arg(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint32_t off, uint16_t *v);
/* a selector freed (GlobalFree, FreeSelector ...): DS and ES of the caller
 * no longer it (0, as Windows does), lest they fault as they are loaded
 * again on the way back */
void win_ne_forget_selector(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint16_t sel);
int win_ne_string(win_ne_guest_api_context *ctx, uint16_t sel, uint16_t off, char *dst,
                  uint32_t cap);

/* ne_exec.c: a FAR PASCAL procedure of the program (selector:offset),
 * called with a window procedure's arguments; DX:AX */
uint32_t win_ne_callback(win_ne_guest_api_context *ctx, uint32_t proc, uint16_t hwnd,
                         uint16_t msg, uint16_t wp, uint32_t lp);
/* ... with any arguments: words, in the order a PASCAL caller pushes them,
 * and a blob (len bytes, or none) put on the program's stack above them,
 * at the far address win_ne_blob_far gives just before, copied back after
 * the call */
uint32_t win_ne_call(win_ne_guest_api_context *ctx, uint32_t proc, const uint16_t *words,
                     int n, void *blob, uint16_t len);
uint32_t win_ne_blob_far(win_ne_guest_api_context *ctx, uint16_t len);
/* the DLLs the program loaded and has not started: their entries called
 * (ne_exec.c; without it, nothing) */
void win_ne_dll_init(win_ne_guest_api_context *ctx);

#endif /* WINN_NE_GUEST_API_H */
