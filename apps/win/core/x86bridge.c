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
    struct winx86_frame *parent;
    uint16_t request;
    int pending;
} winx86_frame;

/* Only core 0 executes x86 code.  Stacking frames permits native APIs to
 * make another guest call, provided the caller saves the outer CPU state. */
static winx86_frame *active_frame;
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
        (active_frame && active_frame->cpu != cpu))
        return WINX86_BAD_CONTEXT;

    winx86_frame frame = { 0 };
    frame.cpu = cpu;
    frame.parent = active_frame;
    bool old_native_done = cpu->native_done;
    if (!active_frame) {
        original_out16 = cpu->cb.io_write16;
        cpu->cb.io_write16 = bridge_out16;
    }
    active_frame = &frame;

    int rc = WINX86_TIMEOUT;
    for (unsigned i = 0; i < max_bursts; i++) {
        frame.pending = 0;
        cpu->native_done = false;
        cpu->ext_accessors->step(cpu, 4096);
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
        if (!service || !service(opaque, cpu, frame.request)) {
            rc = WINX86_BAD_SERVICE;
            break;
        }
    }

    active_frame = frame.parent;
    if (!active_frame) {
        cpu->cb.io_write16 = original_out16;
        original_out16 = NULL;
    }
    cpu->native_done = old_native_done;
    return rc;
}
