/* Native x86 bridge regression test using a scripted CPU backend.
 * It checks the dispatch boundary, CPU state and callback restoration;
 * the actual x86 instructions are exercised by WINN /x86test on RP2350.
 */
#include <assert.h>
#include <ucontext.h>
#include <stdio.h>
#include <string.h>
#include "../core/x86bridge.h"

typedef struct {
    int phase;
    int calls;
    int forwarded;
} test_state;

static test_state state;
static int forward_port;

static void original_out(void *io, int port, uint16_t value)
{
    assert(io == &state);
    assert(port == 0x1234 && value == 0x4321);
    forward_port++;
}

static void fake_step(CPU *cpu, int count)
{
    assert(count == 4096);
    if (state.phase == 0) {
        cpu->cb.io_write16(cpu->cb.io, 0x1234, 0x4321);
        cpu->cb.io_write16(cpu->cb.io, WINX86_BRIDGE_PORT, 3);
    } else if (state.phase == 1) {
        assert(cpu->gprx[regax].r16 == 0x1234);
        cpu->gprx[regbx].r16 = cpu->gprx[regax].r16;
        cpu->cb.io_write16(cpu->cb.io, WINX86_BRIDGE_PORT, WINX86_BRIDGE_EXIT);
    }
    state.phase++;
}

static int fake_service(void *opaque, CPU *cpu, uint16_t id)
{
    test_state *s = (test_state *)opaque;
    assert(id == 3);
    assert(cpu->native_done);
    s->calls++;
    cpu->gprx[regax].r16 = 0x1234;
    return 1;
}

static int exit_service(void *opaque, CPU *cpu, uint16_t id)
{
    (void)opaque; (void)cpu;
    assert(id == 3);
    return WINX86_SERVICE_EXIT;
}

static int reject_service(void *opaque, CPU *cpu, uint16_t id)
{
    (void)opaque; (void)cpu; (void)id;
    return 0;
}

static void fault_step(CPU *cpu, int count)
{
    assert(count == 4096);
    cpu->gprx[regbx].r16 = 6;
    cpu->cb.io_write16(cpu->cb.io, WINX86_BRIDGE_PORT, WINX86_BRIDGE_FAULT);
}

static void hanging_step(CPU *cpu, int count)
{
    (void)cpu; (void)count;
}

/* Two runs that do not end in the order they began, as two WINN tasks
 * whose guests call a native API that switches tasks: A calls, B starts and
 * calls, A finishes, B finishes. */
static ucontext_t ctx_main, ctx_a, ctx_b;
static char stack_a[65536], stack_b[65536];
static CPU *two_cpu;
static int cur, phase_a, phase_b, done_a, done_b;
static void (*hooked)(void *, int, uint16_t);

static void two_step(CPU *cpu, int count)
{
    (void)count;
    int *phase = cur == 'A' ? &phase_a : &phase_b;
    uint16_t svc = cur == 'A' ? 1 : 2;
    cpu->cb.io_write16(cpu->cb.io, WINX86_BRIDGE_PORT, (*phase)++ ? WINX86_BRIDGE_EXIT : svc);
}

static int two_service(void *opaque, CPU *cpu, uint16_t id)
{
    (void)opaque; (void)cpu;
    if (id == 1) {                  /* A's call: B runs meanwhile */
        cur = 'B';
        swapcontext(&ctx_a, &ctx_b);
        cur = 'A';
        return 1;
    }
    cur = 'A';                      /* B's call: back to A */
    swapcontext(&ctx_b, &ctx_a);
    cur = 'B';
    return 1;
}

static void task_a(void)
{
    assert(winx86_run(two_cpu, two_service, NULL, 4) == WINX86_OK);
    assert(two_cpu->cb.io_write16 == hooked);   /* B still runs */
    done_a = 1;
    cur = 'B';
    swapcontext(&ctx_a, &ctx_b);
}

static void task_b(void)
{
    assert(winx86_run(two_cpu, two_service, NULL, 4) == WINX86_OK);
    done_b = 1;
    swapcontext(&ctx_b, &ctx_main);
}

static void two_runs(CPU *cpu)
{
    two_cpu = cpu;
    cpu->ext_accessors->step = two_step;
    void (*original)(void *, int, uint16_t) = cpu->cb.io_write16;
    getcontext(&ctx_a);
    ctx_a.uc_stack.ss_sp = stack_a;
    ctx_a.uc_stack.ss_size = sizeof(stack_a);
    ctx_a.uc_link = &ctx_main;
    makecontext(&ctx_a, task_a, 0);
    getcontext(&ctx_b);
    ctx_b.uc_stack.ss_sp = stack_b;
    ctx_b.uc_stack.ss_size = sizeof(stack_b);
    ctx_b.uc_link = &ctx_main;
    makecontext(&ctx_b, task_b, 0);
    cur = 'A';
    swapcontext(&ctx_main, &ctx_a);
    assert(done_a && done_b && phase_a == 2 && phase_b == 2);
    assert(cpu->cb.io_write16 == original);
}

static void note_hook(CPU *cpu, int count)
{
    (void)count;
    hooked = cpu->cb.io_write16;
    cpu->cb.io_write16(cpu->cb.io, WINX86_BRIDGE_PORT, WINX86_BRIDGE_EXIT);
}

int main(void)
{
    CPU cpu;
    CPU_ext_accessors_t ext;
    memset(&cpu, 0, sizeof(cpu));
    memset(&ext, 0, sizeof(ext));
    cpu.gen = 3;
    cpu.ext_accessors = &ext;
    cpu.cb.io = &state;
    cpu.cb.io_write16 = original_out;
    ext.step = fake_step;
    cpu.native_done = true;

    assert(winx86_run(&cpu, fake_service, &state, 4) == WINX86_OK);
    assert(state.calls == 1 && state.phase == 2 && forward_port == 1);
    assert(cpu.gprx[regbx].r16 == 0x1234);
    assert(cpu.native_done);
    assert(cpu.cb.io_write16 == original_out);

    state.phase = 0;
    assert(winx86_run(&cpu, reject_service, &state, 4) == WINX86_BAD_SERVICE);
    assert(cpu.cb.io_write16 == original_out && cpu.native_done);

    ext.step = fault_step;
    int calls_before = state.calls;
    assert(winx86_run(&cpu, fake_service, &state, 2) == WINX86_FAULT);
    assert(state.calls == calls_before && cpu.gprx[regbx].r16 == 6);
    assert(cpu.cb.io_write16 == original_out && cpu.native_done);

    ext.step = hanging_step;
    assert(winx86_run(&cpu, fake_service, &state, 2) == WINX86_TIMEOUT);
    assert(cpu.cb.io_write16 == original_out && cpu.native_done);

    /* a service that ends the run (a DOS exit) */
    ext.step = fake_step;
    state.phase = 0;
    assert(winx86_run(&cpu, exit_service, &state, 4) == WINX86_OK);
    assert(state.phase == 1 && cpu.cb.io_write16 == original_out && cpu.native_done);

    ext.step = note_hook;
    assert(winx86_run(&cpu, NULL, NULL, 1) == WINX86_OK);
    two_runs(&cpu);
    assert(cpu.native_done);

    cpu.gen = 2;
    assert(winx86_run(&cpu, fake_service, &state, 4) == WINX86_BAD_CONTEXT);
    puts("x86bridge: PASS");
    return 0;
}
