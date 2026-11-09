/* Win16 x86 service dispatch, called in the original native WINN ARM thread. */
#ifndef WINN_NE_GUEST_API_H
#define WINN_NE_GUEST_API_H

#include "x86bridge.h"
#include "ne_guest.h"

typedef struct {
    const win_ne_guest_image *image;
    int (*read)(void *opaque, uint32_t linear, void *dest, uint32_t bytes);
    void *opaque;
    int invalid_args;  /* latched; reported only after the CPU leaves PE */
    const char *path;  /* the program file (GetModuleFileName) */
    int show;          /* nCmdShow (InitTask) */
    int exited;        /* it ended itself (INT 21h 4Ch), with exit_code */
    uint8_t exit_code;
    uint16_t unsupported;   /* the call it made that WINN does not have:
                             * WIN_NE_SERVICE_INT21 + AH for INT 21h */
    int (*yield)(void);     /* its time slice is over: other tasks run;
                             * nonzero: it is to end (stopped) */
    int stopped;
} win_ne_guest_api_context;

/* Marshals the declared Win16 PASCAL signatures, INT 21h from the guest's
 * registers. The caller MUST have switched the shared emulator to the
 * original DOS real-mode context before invoking this function: MessageBox
 * may run the native message loop. cpu is the guest's at the call: SP the
 * caller's (the gateway's return address just below). Returns 1, 0 when
 * the call is not supported, WINX86_SERVICE_EXIT when the program ends. */
int win_ne_guest_api(void *opaque, CPU *cpu, uint16_t service);

#endif /* WINN_NE_GUEST_API_H */
