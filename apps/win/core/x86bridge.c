/*
 * x86bridge.c - Native WINN -> guest x86 -> Native WINN dispatch loop.
 *
 * Uses the existing 386 CPU callbacks; the CPU decoder, BIOS callback
 * chain and native DOS executor are not modified.  No native API is called
 * from inside the interpreter's OUT instruction: native_done returns to
 * this loop first, keeping the original ARM call stack intact.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "x86bridge.h"

typedef struct winx86_frame {
    CPU *cpu;
    uint16_t request;
    int pending;
} winx86_frame;

/* Only core 0 executes x86 code. Runs need not end in the order they began:
 * a native API called from one guest may switch to another WINN task, which
 * starts or resumes a guest of its own, and the first task may come back
 * and finish while the other is still in a native call. So the OUT hook
 * stays while any run is in progress, and the frame it reports to is the
 * one whose step is executing (a nested run, from a native API, included). */
static winx86_frame *active_frame;
static CPU *bridge_cpu;
static unsigned runs;
static void (*original_out16)(void *, int, uint16_t);

static void bridge_out16(void *io, int port, uint16_t value)
{
    winx86_frame *f = active_frame;
    if (f && f->cpu->cb.io == io && (unsigned)port == WINX86_BRIDGE_PORT) {
        f->request = value;
        f->pending = 1;
        f->cpu->native_done = true;
        return;
    }
    if (original_out16)
        original_out16(io, port, value);
}

int winx86_run(CPU *cpu, winx86_service_fn service, void *opaque,
               unsigned max_bursts)
{
    if (!cpu || cpu->gen < 3 || !cpu->ext_accessors ||
        !cpu->ext_accessors->step || !max_bursts ||
        (runs && bridge_cpu != cpu))
        return WINX86_BAD_CONTEXT;

    winx86_frame frame = { 0 };
    frame.cpu = cpu;
    bool old_native_done = cpu->native_done;
    if (!runs++) {
        bridge_cpu = cpu;
        original_out16 = cpu->cb.io_write16;
        cpu->cb.io_write16 = bridge_out16;
    }

    int rc = WINX86_TIMEOUT;
    for (unsigned left = max_bursts; left; left--) {
        frame.pending = 0;
        cpu->native_done = false;
        winx86_frame *outer = active_frame;
        active_frame = &frame;
        cpu->ext_accessors->step(cpu, 4096);
        active_frame = outer;
        if (!frame.pending) {
            if (cpu->native_done) {
                rc = WINX86_INTERRUPTED;
                break;
            }
            continue;
        }
        if (frame.request == WINX86_BRIDGE_EXIT ||
            frame.request == WINX86_BRIDGE_FAULT) {
            rc = frame.request == WINX86_BRIDGE_EXIT ? WINX86_OK : WINX86_FAULT;
            break;
        }
        int handled = service ? service(opaque, cpu, frame.request) : 0;
        if (!handled) {
            rc = WINX86_BAD_SERVICE;
            break;
        }
        if (handled == WINX86_SERVICE_EXIT) {
            rc = WINX86_OK;
            break;
        }
        left = max_bursts + 1;          /* it came back: a new count */
    }

    if (!--runs) {
        cpu->cb.io_write16 = original_out16;
        original_out16 = NULL;
        bridge_cpu = NULL;
    }
    cpu->native_done = old_native_done;
    return rc;
}
