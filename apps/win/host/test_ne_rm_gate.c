/* Test that a native Win16 service runs under the DOS CPU context, with
 * its arguments on the ring 3 stack the call gate's frame names, then
 * resumes the same Real Mode gateway context with its AX:DX result. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gate_mock/dos-api.h"
#include "gate_mock/dos_guest_ptr.h"
#include "../core/ne_guest_api.h"

/* Compile the ARM-only executor path on the host with a mocked DOS ABI.
 * Include it so the static native callback itself is under test. */
#define __arm__ 1
#include "../core/ne_exec.c"

uint8_t test_guest_ram[0x100000];
static CPU cpu;
static PC pc = { &cpu };
static CPU_ext_accessors_t ext;
static uint16_t selectors[6];
static uint16_t dos_cs, rm_cs;
static int modal_calls;
PC *get_PC(void) { return &pc; }

static uint16_t get_segment(const CPU *c, uint8_t i)
{
    assert(c == &cpu && i < 6);
    return selectors[i];
}
static void set_segment(CPU *c, uint8_t i, uint16_t value)
{
    assert(c == &cpu && i < 6);
    selectors[i] = value;
}
static uint32_t get_flags(CPU *c, uint32_t mask)
{
    uint32_t flags;
    assert(c == &cpu);
    /* Public CPU ABI uses a 32-bit flags representation; memcpy works
     * with both the real x86_flags_t union and minimal host test headers. */
    memcpy(&flags, &c->flags, sizeof(flags));
    return flags & mask;
}
static void set_flags(CPU *c, uint32_t set, uint32_t clear)
{
    uint32_t flags;
    assert(c == &cpu);
    memcpy(&flags, &c->flags, sizeof(flags));
    flags = (flags | set) & ~clear;
    memcpy(&c->flags, &flags, sizeof(flags));
}

uint32_t GetVersion(void) { return 0x07000a03; }
uint16_t GetCurrentTask(void) { return 0x3333; }
unsigned GetNumTasks(void) { return 1; }
uint32_t GetTickCount(void) { return 0; }
int GetSystemMetrics(int i) { return i; }
uint32_t GetSysColor(int i) { return (uint32_t)i; }
int MessageBox(uint16_t owner, const char *text, const char *caption, unsigned type)
{
    /* Only this callback must observe the saved original DOS state. */
    assert(selectors[WIN_CS] == dos_cs);
    assert(cpu.gprx[regbx].r32 == 0x11223344u);
    assert(owner == 0 && type == 0);
    assert(!strcmp(text, "Hello from Win16 NE"));
    assert(!strcmp(caption, "NE dialog"));
    modal_calls++;
    /* A DOS BIOS call may change the emulated registers transiently. */
    cpu.gprx[regbx].r32 = 0xDEADu;
    selectors[WIN_CS] = 0xf000;
    return 1;
}

static void store16(uint32_t linear, uint16_t v)
{
    test_guest_ram[linear] = (uint8_t)v;
    test_guest_ram[linear+1] = (uint8_t)(v >> 8);
}
int main(void)
{
    memset(&cpu, 0, sizeof(cpu));
    memset(&ext, 0, sizeof(ext));
    ext.get_seg16 = get_segment;
    ext.set_seg16 = set_segment;
    ext.get_flags = get_flags;
    ext.set_flags = set_flags;
    cpu.ext_accessors = &ext;

    dos_cs = 0x1000; rm_cs = 0x4321;
    for (unsigned i=0; i<6; ++i) selectors[i] = dos_cs;
    cpu.gprx[regbx].r32 = 0x11223344u;
    cpu.gprx[regsp].r16 = 0x7ff0;
    set_flags(&cpu, 0x202u, 0);
    win_cpu_snapshot original;
    save_cpu(&cpu, &original);

    for (unsigned i=0; i<6; ++i) selectors[i] = rm_cs;
    cpu.gprx[regbx].r32 = 0x55667788u;
    cpu.gprx[regsp].r16 = 0xee;
    set_flags(&cpu, 2u, ~2u);
    cpu.ip = 0xba;
    cpu.next_ip = 0xbb;
    uint32_t scratch = 0x40400u;
    store16(scratch+WIN_NE_BOOT_CALL_SP_OFFSET, 0x7c); /* caller SP = 0x80 */

    win_ne_guest_segment segments[2] = { {0}, {0x30000u, 256u, 0x18u, 1u} };
    win_ne_guest_image img = { 0 };
    img.info.segment_count = 1;
    img.segments = segments;
    img.ss = 0x18u;
    memcpy(test_guest_ram + 0x30020u, "Hello from Win16 NE", 20);
    memcpy(test_guest_ram + 0x30080u, "NE dialog", 10);
    /* SP=0xc0 points to far return CS:IP, then 12 bytes of arguments. */
    store16(0x300c4u, 0);                 /* type */
    store16(0x300c6u, 0x80);
    store16(0x300c8u, 0x18);              /* caption DS:0x80 */
    store16(0x300cau, 0x20);
    store16(0x300ccu, 0x18);              /* text DS:0x20 */
    store16(0x300ceu, 0);                 /* hwnd */
    /* the ring 0 stack at the gateway's entry, through the API call gate:
     * its return address, then the ring 3 caller's SP and SS (0x1b: the
     * segment, RPL 3) */
    const uint32_t ring0 = 0x40800u;
    store16(scratch+WIN_NE_BOOT_CALL_SP_OFFSET, 0x2f8u);
    store16(ring0 + 0x2f8u, 0x000a);      /* the thunk's IP after the CALL */
    store16(ring0 + 0x2fau, 0x0053);      /* its CS, ring 3 */
    store16(ring0 + 0x2fcu, 0x00c0);      /* SP3 */
    store16(ring0 + 0x2feu, 0x001b);      /* SS3 */

    win_ne_guest_api_context api = { .image = &img, .read = read_guest, .path = "TEST.EXE", .show = 1 };
    win_ne_native_gate gate = { .dos_context = &original, .api = &api, .scratch_linear = scratch,
                                .ring0_linear = ring0, .rm_segment = rm_cs, .ring0_selector = 0x68 };
    assert(invoke_native(&gate, &cpu, 0x1001));
    assert(modal_calls == 1 && !api.invalid_args);
    assert(cpu.gprx[regax].r16 == 1 && cpu.gprx[regdx].r16 == 0);
    assert(cpu.gprx[regbx].r32 == 0x55667788u);
    assert(cpu.gprx[regsp].r16 == 0xee);
    assert(selectors[WIN_CS] == rm_cs);
    assert(cpu.ip == 0xba && cpu.next_ip == 0xbb);
    assert(get_flags(&cpu, 0xffffffffu) == 2u);
    /* a call through the gate with no ring 3 caller's frame (the ring 0
     * stack's top), or a CALL_SP outside the ring 0 stack: refused */
    store16(scratch+WIN_NE_BOOT_CALL_SP_OFFSET, 0x2fcu);
    assert(!invoke_native(&gate, &cpu, 0x1001));
    store16(scratch+WIN_NE_BOOT_CALL_SP_OFFSET, 0x10u);
    assert(!invoke_native(&gate, &cpu, 0x1001) && modal_calls == 1);
    puts("ne RM native gate context: PASS");
    return 0;
}
