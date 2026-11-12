/* 386 protected-mode NE guest execution. */
#ifndef WINN_NE_EXEC_H
#define WINN_NE_EXEC_H
#include <stdint.h>
enum {
    WIN_NE_EXEC_BAD_CPU = -20,
    WIN_NE_EXEC_BAD_ENTRY = -21,
    WIN_NE_EXEC_BAD_BOOT = -22,
    WIN_NE_EXEC_BAD_ARGS = -23,
    WIN_NE_EXEC_FAULT = -25,       /* see win_ne_fault */
    WIN_NE_EXEC_UNSUPPORTED = -26  /* see win_ne_result.unsupported */
};

/* Where a guest faulted: the exception vector and CS:IP; segment is CS's
 * NE segment number, 0 when CS is none of the program's (a thunk, or a
 * frame that cannot be read). */
typedef struct {
    int vector;
    uint16_t cs, ip, segment;
} win_ne_fault;

/* How a program ended */
typedef struct {
    uint32_t value;         /* DX:AX of an entry that returned (RETF); the
                             * exit code of one that ended through DOS */
    int exited;             /* INT 21h 4Ch (or INT 20h) */
    int stopped;            /* at its turn's end, its task told to quit */
    uint16_t unsupported;   /* WIN_NE_EXEC_UNSUPPORTED: the call it made,
                             * KERNEL n, USER 1000h + n, INT 21h 2100h + AH,
                             * WIN_NE_SERVICE_UNDEFINED + ... (undefined) */
    char undefined[48];     /* that one's MODULE.function */
    win_ne_fault fault;     /* WIN_NE_EXEC_FAULT */
    uint16_t invalid_call;  /* WIN_NE_EXEC_BAD_ARGS: the first call WINN refused,
                             * its arguments pointing outside the program's
                             * memory (as unsupported) */
} win_ne_result;

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
/* Run a Win16 program: its command line (after the program name) in its
 * PSP, nCmdShow for InitTask. yield lets other tasks run when the program
 * has run its time slice without calling the API; nonzero: it ends. */
int win_ne_execute(const char *path, const char *cmdline, int show, int (*yield)(void),
                   win_ne_result *r);
#endif
/* WINN's trace (core/trace.c, SYSTEM.INI [debug] trace=1): each call a
 * program makes of WINN's, with its caller and DX:AX; a running program's
 * place where its turn ended. NULL: no trace. */
extern void (*win_ne_trace_call)(uint16_t service, uint16_t cs, uint16_t ip, uint32_t result);
extern void (*win_ne_trace_turn)(uint16_t cs, uint16_t ip);

#endif
