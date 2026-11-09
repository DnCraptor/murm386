/* Native x86 bridge regression test using a scripted CPU backend.
 * It checks the dispatch boundary, CPU state and callback restoration;
 * the actual x86 instructions are exercised by WINN /x86test on RP2350.
 */
#include <assert.h>
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

    cpu.gen = 2;
    assert(winx86_run(&cpu, fake_service, &state, 4) == WINX86_BAD_CONTEXT);
    puts("x86bridge: PASS");
    return 0;
}
