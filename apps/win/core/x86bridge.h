/*
 * x86bridge.h - synchronous guest execution owned by Native WINN.
 *
 * The guest calls an ARM service by executing OUT DX,AX with DX set to
 * WINX86_BRIDGE_PORT.  The CPU port callback only requests a stop; the
 * actual ARM handler runs after CPU.step() returns to its WINN caller.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WIN_X86BRIDGE_H
#define WIN_X86BRIDGE_H

#include "../../api/cpu.h"

#define WINX86_BRIDGE_PORT 0xE7E7u
#define WINX86_BRIDGE_EXIT 0xFFFFu
#define WINX86_BRIDGE_FAULT 0xFFFEu

enum {
    WINX86_OK = 0,
    WINX86_BAD_CONTEXT = -1,
    WINX86_TIMEOUT = -2,
    WINX86_BAD_SERVICE = -3,
    WINX86_INTERRUPTED = -4,
    WINX86_FAULT = -5
};

/* Handler may update guest registers; 1 = handled, 0 = unsupported.
 * EXIT and FAULT tokens bypass the handler and end the guest run.
 * Native calls using DOS/BIOS are safe only when the caller has explicitly
 * returned the shared CPU to its original real-mode DOS context.
 */
typedef int (*winx86_service_fn)(void *opaque, CPU *cpu, uint16_t service);

/* CPU entry state (CS:IP, SS:SP and segment descriptors) is prepared by the
 * caller.  The caller also owns its restoration and any nested guest frame.
 * Each burst is at most 4096 instructions; max_bursts bounds runaway code.
 */
int winx86_run(CPU *cpu, winx86_service_fn service, void *opaque,
               unsigned max_bursts);

#endif /* WIN_X86BRIDGE_H */
