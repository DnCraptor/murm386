/* Verify checked 16-bit PASCAL stack, FAR pointer and DX:AX marshalling. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_guest_api.h"

#define BASE 0x30000u
static uint8_t mem[256];

static int read_guest(void *opaque, uint32_t addr, void *dst, uint32_t n)
{
    (void)opaque;
    if (addr < BASE || addr - BASE > sizeof(mem) ||
        n > sizeof(mem) - (addr - BASE)) return 0;
    memcpy(dst, mem + (addr - BASE), n);
    return 1;
}
static void put16(size_t at, uint16_t n)
{
    assert(at + 1 < sizeof(mem));
    mem[at] = (uint8_t)n;
    mem[at+1] = (uint8_t)(n >> 8);
}

uint32_t GetVersion(void) { return 0x07000A03UL; }
uint16_t GetCurrentTask(void) { return 0x1234; }
unsigned int GetNumTasks(void) { return 2; }
uint32_t GetTickCount(void) { return 0x34567890UL; }
int GetSystemMetrics(int id) { return id == 0 ? 640 : -1; }
uint32_t GetSysColor(int id) { return id == 5 ? 0x00123456u : 0; }

int main(void)
{
    CPU cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.gprx[regax].r32 = 0x1234FFFF;
    cpu.gprx[regdx].r32 = 0x5678FFFF;
    assert(win_ne_guest_api(NULL, &cpu, 3));
    assert(cpu.gprx[regax].r16 == 0x0A03);
    assert(cpu.gprx[regdx].r16 == 0x0700);
    assert(cpu.gprx[regax].r32 == 0x12340A03);
    assert(cpu.gprx[regdx].r32 == 0x56780700);
    assert(win_ne_guest_api(NULL, &cpu, 36) && cpu.gprx[regax].r16 == 0x1234);
    assert(cpu.gprx[regdx].r16 == 0);
    assert(win_ne_guest_api(NULL, &cpu, 152) && cpu.gprx[regax].r16 == 2);
    assert(win_ne_guest_api(NULL, &cpu, 0x100d));
    assert(cpu.gprx[regax].r16 == 0x7890 && cpu.gprx[regdx].r16 == 0x3456);

    win_ne_guest_segment segments[2] = { {0}, {BASE, sizeof(mem), 0x18, 1} };
    win_ne_guest_image img = { 0 };
    img.info.segment_count = 1;
    img.segments = segments;
    img.ss = 0x18;
    win_ne_guest_api_context ctx = { &img, read_guest, NULL, 0 };
    cpu.gprx[regsp].r16 = 0x80; /* SS:SP points at caller's far return IP */
    put16(0x80, 0x2222);
    put16(0x82, 0x0010);

    /* USER.GetSystemMetrics(0): one word at SP+4 */
    put16(0x84, 0);
    assert(win_ne_guest_api(&ctx, &cpu, 0x10b3));
    assert(cpu.gprx[regax].r16 == 640 && cpu.gprx[regdx].r16 == 0);
    /* USER.GetSysColor(5): DWORD result in DX:AX */
    put16(0x84, 5);
    assert(win_ne_guest_api(&ctx, &cpu, 0x10b4));
    assert(cpu.gprx[regax].r16 == 0x3456 && cpu.gprx[regdx].r16 == 0x0012);

    /* KERNEL.lstrlen(SS:0x10): valid FAR pointer */
    memcpy(mem + 0x10, "NE arguments", 13);
    put16(0x84, 0x10); put16(0x86, 0x18);
    assert(win_ne_guest_api(&ctx, &cpu, 90));
    assert(cpu.gprx[regax].r16 == 12 && cpu.gprx[regdx].r16 == 0);
    assert(!ctx.invalid_args);
    /* Invalid selector: no out-of-bounds read, fault latched. */
    put16(0x86, 0x7777);
    assert(win_ne_guest_api(&ctx, &cpu, 90));
    assert(ctx.invalid_args && cpu.gprx[regax].r16 == 0);
    ctx.invalid_args = 0;
    /* Unterminated string at the end of the segment is rejected. */
    put16(0x84, 254); put16(0x86, 0x18);
    mem[254] = 'X'; mem[255] = 'Y';
    assert(win_ne_guest_api(&ctx, &cpu, 90) && ctx.invalid_args);
    ctx.invalid_args = 0;
    /* Read spanning the segment boundary must fail before the callback. */
    cpu.gprx[regsp].r16 = 252;
    assert(win_ne_guest_api(&ctx, &cpu, 0x10b3) && ctx.invalid_args);

    assert(!win_ne_guest_api(&ctx, &cpu, 1));
    assert(!win_ne_guest_api(NULL, NULL, 3));
    puts("ne guest stack/api: PASS");
    return 0;
}
