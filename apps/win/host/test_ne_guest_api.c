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
static int write_guest(void *opaque, uint32_t addr, const void *src, uint32_t n)
{
    (void)opaque;
    if (addr < BASE || addr - BASE > sizeof(mem) ||
        n > sizeof(mem) - (addr - BASE)) return 0;
    memcpy(mem + (addr - BASE), src, n);
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
static unsigned messagebox_calls;
int MessageBox(uint16_t owner, const char *text, const char *title, unsigned type)
{
    assert(owner == 0 && type == 0);
    assert(!strcmp(text, "Hello from Win16 NE"));
    assert(!strcmp(title, "NE dialog"));
    messagebox_calls++;
    return 1; /* IDOK */
}

int main(void)
{
    win_ne_regs cpu;
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
    img.io.write = write_guest;
    win_ne_guest_api_context ctx = { .image = &img, .read = read_guest, .path = "TEST.EXE", .show = 1 };
    cpu.gprx[regsp].r16 = 0x80; /* SS:SP points at caller's far return IP */
    cpu.ss = 0x18;
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
    /* MessageBox args are pushed left-to-right (Win16 PASCAL). */
    memcpy(mem + 0x10, "Hello from Win16 NE", 20);
    memcpy(mem + 0x40, "NE dialog", 10);
    put16(0x84, 0);               /* type, last argument */
    put16(0x86, 0x40); put16(0x88, 0x18); /* caption */
    put16(0x8a, 0x10); put16(0x8c, 0x18); /* text */
    put16(0x8e, 0);               /* HWND */
    assert(win_ne_guest_api(&ctx, &cpu, 0x1001));
    assert(messagebox_calls == 1 && cpu.gprx[regax].r16 == 1);
    put16(0x8c, 0xffff);           /* invalid text selector */
    assert(win_ne_guest_api(&ctx, &cpu, 0x1001));
    assert(ctx.invalid_args && messagebox_calls == 1);
    ctx.invalid_args = 0;
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

    /* KERNEL.lstrcpy: two FAR parameters; destination must be writable. */
    ctx.invalid_args = 0;
    cpu.gprx[regsp].r16 = 0x80;
    memcpy(mem + 0xb0, "Copy", 5);
    memset(mem + 0x90, 0x55, 32);
    put16(0x84, 0xb0); put16(0x86, 0x18); /* source FAR */
    put16(0x88, 0x90); put16(0x8a, 0x18); /* destination FAR */
    assert(win_ne_guest_api(&ctx, &cpu, 88));
    assert(!ctx.invalid_args && !strcmp((char *)mem+0x90, "Copy"));
    assert(cpu.gprx[regax].r16 == 0x90 && cpu.gprx[regdx].r16 == 0x18);

    /* KERNEL.lstrcat: append while retaining the destination FAR result. */
    memcpy(mem + 0xb0, " Win16", 7);
    assert(win_ne_guest_api(&ctx, &cpu, 89));
    assert(!strcmp((char *)mem+0x90, "Copy Win16"));
    assert(cpu.gprx[regax].r16 == 0x90 && cpu.gprx[regdx].r16 == 0x18);

    /* Source and destination overlap: reject, leaving memory unchanged. */
    put16(0x84, 0x91);
    assert(win_ne_guest_api(&ctx, &cpu, 88) && ctx.invalid_args);
    assert(!strcmp((char *)mem+0x90, "Copy Win16"));
    ctx.invalid_args = 0;
    put16(0x84, 0xb0);
    /* Destination too small: reject before the first write. */
    put16(0x88, 0xfd);
    mem[0xfd] = 0x5a;
    assert(win_ne_guest_api(&ctx, &cpu, 88) && ctx.invalid_args);
    assert(mem[0xfd] == 0x5a);
    ctx.invalid_args = 0;
    put16(0x88, 0x90);
    /* Reject writes to code segments, including a valid readable string. */
    segments[1].flags = 0;
    assert(win_ne_guest_api(&ctx, &cpu, 88) && ctx.invalid_args);
    segments[1].flags = 1;
    ctx.invalid_args = 0;
    /* No guest write callback means no modification. */
    img.io.write = NULL;
    assert(win_ne_guest_api(&ctx, &cpu, 88) && ctx.invalid_args);
    assert(!strcmp((char *)mem+0x90, "Copy Win16"));
    img.io.write = write_guest;
    ctx.invalid_args = 0;

    /* Long strings cross the 64-byte staging buffer without truncation. */
    img.io.write = write_guest;
    ctx.invalid_args = 0;
    for (unsigned i = 0; i < 80; ++i)
        mem[0xa0+i] = (uint8_t)('a' + i % 26);
    mem[0xf0] = 0;
    put16(0x84, 0xa0); put16(0x86, 0x18); /* source */
    put16(0x88, 0x10); put16(0x8a, 0x18); /* destination */
    assert(win_ne_guest_api(&ctx, &cpu, 88) && !ctx.invalid_args);
    assert(!memcmp(mem + 0x10, mem + 0xa0, 81));
    assert(cpu.gprx[regax].r16 == 0x10 && cpu.gprx[regdx].r16 == 0x18);

    assert(!win_ne_guest_api(&ctx, &cpu, 1));
    assert(!win_ne_guest_api(NULL, NULL, 3));
    puts("ne guest stack/api: PASS");
    return 0;
}
