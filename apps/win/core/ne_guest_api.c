/*
 * ne_guest_api.c - checked Win16 stack/pointer marshalling into Native WINN.
 * While CR0.PE=1 only nonblocking, DOS-independent native calls are allowed.
 * The x86 OUT callback has already returned to the WINN ARM call stack.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_guest_api.h"
#include <stdint.h>
#include <string.h>
#include "ne_boot.h"

extern uint32_t GetVersion(void);
extern uint16_t GetCurrentTask(void);
extern unsigned int GetNumTasks(void);
extern uint32_t GetTickCount(void);
extern int GetSystemMetrics(int);
extern uint32_t GetSysColor(int);
extern int MessageBox(uint16_t, const char *, const char *, unsigned int);

/* The segment of a selector: one of the program's, its PSP, a global block */
static const win_ne_guest_segment *lookup_segment(
    const win_ne_guest_api_context *ctx, uint16_t selector)
{
    if (!ctx || !ctx->image || !ctx->image->segments)
        return NULL;
    const win_ne_guest_image *g = ctx->image;
    for (uint16_t i = 1; i <= g->info.segment_count; i++)
        if ((g->segments[i].selector & ~3u) == (selector & ~3u))
            return &g->segments[i];
    if (g->psp.selector && (g->psp.selector & ~3u) == (selector & ~3u))
        return &g->psp;
    for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++)     /* GlobalAlloc's */
        if (g->globals[i].linear && (g->globals[i].selector & ~3u) == (selector & ~3u))
            return &g->globals[i];
    return NULL;
}

/* Bounds are those of the segment, not of the complete DOS memory map. */
static int read_segment(win_ne_guest_api_context *ctx, uint16_t selector,
                        uint32_t offset, void *dst, uint32_t len)
{
    const win_ne_guest_segment *s = lookup_segment(ctx, selector);
    if (!s || !ctx->read || !dst || offset > s->size || len > s->size - offset)
        return 0;
    return ctx->read(ctx->opaque, s->linear + offset, dst, len);
}

static int read_stack_word(win_ne_guest_api_context *ctx, win_ne_regs *cpu,
                           uint32_t stack_offset, uint16_t *value)
{
    uint32_t sp = cpu->gprx[regsp].r16;
    uint8_t bytes[2];
    if (!ctx || !ctx->image ||
        !read_segment(ctx, cpu->ss, sp + stack_offset, bytes, 2))
        return 0;
    *value = (uint16_t)(bytes[0] | (uint16_t)bytes[1] << 8);
    return 1;
}

/* A far pointer is a *selector:offset*, never an ARM address or RM segment.
 * Strings must be entirely contained and NUL-terminated within that segment.
 * Scan in fixed-size chunks to bound native stack and allocation overhead. */
static int guest_strlen(win_ne_guest_api_context *ctx,
                        uint16_t selector, uint16_t offset, uint16_t *length)
{
    const win_ne_guest_segment *s = lookup_segment(ctx, selector);
    if (!s || offset >= s->size)
        return 0;
    uint32_t pos = offset;
    uint8_t buf[32];
    while (pos < s->size) {
        uint32_t n = s->size - pos;
        if (n > sizeof(buf)) n = sizeof(buf);
        if (!read_segment(ctx, selector, pos, buf, n)) return 0;
        const uint8_t *end = (const uint8_t *)memchr(buf, 0, n);
        if (end) {
            uint32_t count = pos - offset + (uint32_t)(end - buf);
            if (count > UINT16_MAX) return 0;
            *length = (uint16_t)count;
            return 1;
        }
        pos += n;
    }
    return 0;
}

/* Guest string functions write only to loaded, writable data segments.
 * The original DOS guest allocator owns the memory; its write callback
 * works while the bridge has restored the CPU's Real Mode DOS context. */
static int write_segment(win_ne_guest_api_context *ctx, uint16_t selector,
                         uint32_t offset, const void *src, uint32_t len)
{
    const win_ne_guest_segment *s = lookup_segment(ctx, selector);
    if (!s || !(s->flags & 1u) || !ctx->image->io.write ||
        !src || offset > s->size || len > s->size - offset)
        return 0;
    return ctx->image->io.write(ctx->image->opaque,
                                s->linear + offset, src, len);
}

/* Copy a NUL-terminated source using a bounded temporary buffer.  Check the
 * complete source and destination ranges before writing.  Overlap is
 * rejected rather than relying on undefined strcpy/strcat semantics. */
static __attribute__((noinline)) int guest_string_edit(win_ne_guest_api_context *ctx,
                             uint16_t dst_sel, uint16_t dst_off,
                             uint16_t src_sel, uint16_t src_off, int append)
{
    const win_ne_guest_segment *dst = lookup_segment(ctx, dst_sel);
    const win_ne_guest_segment *src = lookup_segment(ctx, src_sel);
    uint16_t src_len, dst_len = 0;
    if (!dst || !src || !(dst->flags & 1u) || !ctx->image->io.write ||
        !guest_strlen(ctx, src_sel, src_off, &src_len))
        return 0;
    if (append && !guest_strlen(ctx, dst_sel, dst_off, &dst_len))
        return 0;
    uint32_t start = (uint32_t)dst_off + dst_len;
    uint32_t count = (uint32_t)src_len + 1u;
    if (start > dst->size || count > dst->size - start)
        return 0;
    if (dst_sel == src_sel &&
        start < (uint32_t)src_off + count &&
        (uint32_t)src_off < start + count)
        return 0;
    uint8_t buf[64];
    for (uint32_t copied = 0; copied < count;) {
        uint32_t n = count - copied;
        if (n > sizeof(buf)) n = sizeof(buf);
        if (!read_segment(ctx, src_sel, (uint32_t)src_off + copied, buf, n) ||
            !write_segment(ctx, dst_sel, start + copied, buf, n))
            return 0;
        copied += n;
    }
    return 1;
}

/* Bounded Win16 FAR ANSI string copy. Never pass a guest pointer into a
 * native USER routine: the DOS-backed NE memory belongs to the emulator. */
static int guest_copy_string(win_ne_guest_api_context *ctx,
                             uint16_t selector, uint16_t offset,
                             char *dst, uint32_t capacity)
{
    if (!dst || !capacity) return 0;
    if (!selector && !offset) { dst[0] = 0; return 1; }
    for (uint32_t i = 0; i < capacity; i++) {
        if (!read_segment(ctx, selector, (uint32_t)offset + i, dst + i, 1))
            return 0;
        if (!dst[i]) return 1;
    }
    return 0; /* unterminated, even when the guest segment is larger */
}

/* The guest's other registers, as the gateway saved them below its return
 * address (4 bytes below the frame): PUSHA, then DS, ES, FLAGS. A service
 * that returns more than AX and DX writes them there. An interrupt's frame
 * is above: the guest's AX, IP, CS, FLAGS. The ring 0 stack, where the
 * CPU and the gateway put all of it, is no segment of the program's. */
enum { FR_CX = -8, FR_DX = -10, FR_BX = -12, FR_BP = -16, FR_SI = -18,
       FR_DI = -20, FR_DS = -22, FR_ES = -24, FR_AX = 0, FR_FLAGS = 6 };

static int frame_get(win_ne_guest_api_context *ctx, win_ne_regs *cpu, int at, uint16_t *v)
{
    uint8_t b[2];
    if (!ctx->read || !cpu->frame || !ctx->read(ctx->opaque, (uint32_t)(cpu->frame + at), b, 2))
        return 0;
    *v = (uint16_t)(b[0] | b[1] << 8);
    return 1;
}

static int frame_set(win_ne_guest_api_context *ctx, win_ne_regs *cpu, int at, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    return cpu->frame && ctx->image->io.write &&
           ctx->image->io.write(ctx->image->opaque, (uint32_t)(cpu->frame + at), b, 2);
}

static uint16_t dgroup(const win_ne_guest_image *g)
{
    uint16_t i = g->info.auto_data ? g->info.auto_data : g->info.ss_index;
    return i ? g->segments[i].selector : 0;
}

/* KERNEL.91 InitTask: AX 1, BX the command line (ES:BX, in the PSP), CX
 * the stack size, DX nCmdShow, SI hPrevInstance, DI hInstance (DGROUP's
 * selector, as in Windows), ES the PSP; the stack bounds in DGROUP's
 * header (pStackTop, pStackMin, pStackBottom at 0Ah). */
static int init_task(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint32_t *value)
{
    const win_ne_guest_image *g = ctx->image;
    uint16_t ds = dgroup(g);
    if (!frame_set(ctx, cpu, FR_BX, 0x81) ||
        !frame_set(ctx, cpu, FR_CX, g->info.stack_size) ||
        !frame_set(ctx, cpu, FR_SI, 0) ||
        !frame_set(ctx, cpu, FR_DI, ds) ||
        !frame_set(ctx, cpu, FR_ES, g->psp.selector))
        return 0;
    uint16_t bottom = g->sp, top = (uint16_t)(g->sp - g->info.stack_size);
    uint8_t hdr[6] = { (uint8_t)top, (uint8_t)(top >> 8), (uint8_t)top, (uint8_t)(top >> 8),
                       (uint8_t)bottom, (uint8_t)(bottom >> 8) };
    if (g->info.stack_size && g->info.stack_size < g->sp)
        write_segment(ctx, ds, 0x0a, hdr, sizeof(hdr));
    *value = ((uint32_t)(uint16_t)ctx->show << 16) | 1u;
    return 1;
}

/* INT 21h: the guest's AX is the frame's first word, then the interrupt's IP, CS,
 * FLAGS (its carry: the error); DX stays the guest's unless the function
 * sets it. */
static int int21(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint32_t *value)
{
    uint16_t ax, dx, flags;
    if (!frame_get(ctx, cpu, FR_AX, &ax) || !frame_get(ctx, cpu, FR_DX, &dx) ||
        !frame_get(ctx, cpu, FR_FLAGS, &flags))
        return 0;
    switch (ax >> 8) {
    case 0x30:                  /* DOS version: 7.0, as GetVersion says */
        ax = 0x0007;
        if (!frame_set(ctx, cpu, FR_BX, 0) || !frame_set(ctx, cpu, FR_CX, 0))
            return 0;
        break;
    case 0x4c:                  /* exit */
        ctx->exited = 1;
        ctx->exit_code = (uint8_t)ax;
        return WINX86_SERVICE_EXIT;
    default:
        ctx->unsupported = (uint16_t)(WIN_NE_SERVICE_INT21 + (ax >> 8));
        return 0;
    }
    flags &= (uint16_t)~1u;     /* CF: no error */
    if (!frame_set(ctx, cpu, FR_FLAGS, flags))
        return 0;
    *value = ((uint32_t)dx << 16) | ax;
    return 1;
}

/* no window services where ne_guest_user.c is not linked in */
__attribute__((weak)) int win_ne_guest_user(win_ne_guest_api_context *ctx, win_ne_regs *cpu,
                                            uint16_t service, uint32_t *value, int *dword)
{
    (void)ctx; (void)cpu; (void)service; (void)value; (void)dword;
    return 0;
}
__attribute__((weak)) void win_ne_user_start(win_ne_guest_api_context *ctx) { (void)ctx; }
__attribute__((weak)) void win_ne_user_end(win_ne_guest_api_context *ctx) { (void)ctx; }

int win_ne_read(win_ne_guest_api_context *ctx, uint16_t sel, uint32_t off, void *dst, uint32_t n)
{
    return read_segment(ctx, sel, off, dst, n);
}

int win_ne_write(win_ne_guest_api_context *ctx, uint16_t sel, uint32_t off, const void *src,
                 uint32_t n)
{
    return write_segment(ctx, sel, off, src, n);
}

int win_ne_arg(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint32_t off, uint16_t *v)
{
    return read_stack_word(ctx, cpu, off, v);
}

int win_ne_string(win_ne_guest_api_context *ctx, uint16_t sel, uint16_t off, char *dst,
                  uint32_t cap)
{
    return guest_copy_string(ctx, sel, off, dst, cap);
}

/* MessageBox's strings: in a frame of its own, not in the dispatcher's
 * (which a program's nested window procedures stack up on its task) */
static __attribute__((noinline)) uint32_t message_box(win_ne_guest_api_context *ctx,
                                                      win_ne_regs *cpu)
{
    uint16_t type, title_off, title_sel, text_off, text_sel, owner;
    char title[128], message[512];
    if (!read_stack_word(ctx, cpu, 4, &type) ||
        !read_stack_word(ctx, cpu, 6, &title_off) ||
        !read_stack_word(ctx, cpu, 8, &title_sel) ||
        !read_stack_word(ctx, cpu, 10, &text_off) ||
        !read_stack_word(ctx, cpu, 12, &text_sel) ||
        !read_stack_word(ctx, cpu, 14, &owner) ||
        !guest_copy_string(ctx, title_sel, title_off, title, sizeof(title)) ||
        !guest_copy_string(ctx, text_sel, text_off, message, sizeof(message))) {
        if (ctx) ctx->invalid_args = 1;
        return 0;
    }
    return (uint16_t)MessageBox(owner, message, title, type);
}

int win_ne_guest_api(void *opaque, win_ne_regs *cpu, uint16_t service)
{
    if (!cpu) return 0;
    win_ne_guest_api_context *ctx = (win_ne_guest_api_context *)opaque;
    uint32_t value = 0;
    int dword = 0;
    uint16_t arg, ptr_seg;
    switch (service) {
    case 91:      /* KERNEL.InitTask: registers */
        if (!init_task(ctx, cpu, &value)) {
            ctx->invalid_args = 1;
            value = 0;
        }
        dword = 1;
        break;
    case 30:      /* KERNEL.WaitEvent(HTASK): nothing waits */
        value = 0;
        break;
    case 0x1005:  /* USER.InitApp(HINSTANCE) */
        value = 1;
        break;
    case 49: {    /* KERNEL.GetModuleFileName(HINSTANCE, LPSTR, int) */
        uint16_t size, off, sel;
        value = 0;
        if (!read_stack_word(ctx, cpu, 4, &size) || !read_stack_word(ctx, cpu, 6, &off) ||
            !read_stack_word(ctx, cpu, 8, &sel) || !size) {
            ctx->invalid_args = 1;
            break;
        }
        const char *p = ctx->path ? ctx->path : "";
        uint16_t n = (uint16_t)strlen(p);
        if (n > size - 1u)
            n = (uint16_t)(size - 1u);
        static const char nul = 0;
        if (!write_segment(ctx, sel, off, p, n) || !write_segment(ctx, sel, (uint32_t)off + n, &nul, 1))
            ctx->invalid_args = 1;
        else
            value = n;
        break;
    }
    case WIN_NE_SERVICE_YIELD: {
        /* the yield vector: AX, DX and the flags as they were, but IF (set
         * to take it) */
        uint16_t ax, dx, flags;
        if (!frame_get(ctx, cpu, FR_AX, &ax) || !frame_get(ctx, cpu, FR_DX, &dx) ||
            !frame_get(ctx, cpu, FR_FLAGS, &flags))
            return 0;
        flags &= (uint16_t)~0x200u;
        if (!frame_set(ctx, cpu, FR_FLAGS, flags))
            return 0;
        if (ctx->yield && ctx->yield()) {
            ctx->stopped = 1;
            return WINX86_SERVICE_EXIT;
        }
        value = ((uint32_t)dx << 16) | ax;
        dword = 1;
        break;
    }
    case WIN_NE_SERVICE_INT21: {
        int r = int21(ctx, cpu, &value);
        if (r != 1)
            return r;
        dword = 1;
        break;
    }
    case 3:       value = GetVersion(); dword = 1; break;
    case 36:      value = GetCurrentTask(); break;
    case 152:     value = GetNumTasks(); break;
    case 0x100d:  value = GetTickCount(); dword = 1; break;
    case 88: case 89: /* KERNEL.lstrcpy/lstrcat: (FAR dst, FAR src) */
        {
            uint16_t src_off, src_sel, dst_off, dst_sel;
            dword = 1;          /* return destination FAR pointer in DX:AX */
            if (!read_stack_word(ctx, cpu, 4, &src_off) ||
                !read_stack_word(ctx, cpu, 6, &src_sel) ||
                !read_stack_word(ctx, cpu, 8, &dst_off) ||
                !read_stack_word(ctx, cpu, 10, &dst_sel) ||
                !guest_string_edit(ctx, dst_sel, dst_off,
                                   src_sel, src_off, service == 89)) {
                if (ctx) ctx->invalid_args = 1;
                value = 0;
            } else {
                value = ((uint32_t)dst_sel << 16) | dst_off;
            }
        }
        break;
    case 90:      /* KERNEL.90 lstrlen(LPCSTR): 16:16 FAR pointer */
        {
            uint16_t len = 0;
            if (!read_stack_word(ctx, cpu, 4, &arg) ||
                !read_stack_word(ctx, cpu, 6, &ptr_seg) ||
                !guest_strlen(ctx, ptr_seg, arg, &len)) {
                if (ctx) ctx->invalid_args = 1;
            }
            value = len;
        }
        break;
    case 0x1001: /* USER.1 MessageBox(HWND, FAR text, FAR caption, UINT) */
        value = message_box(ctx, cpu);
        break;
    case 0x10b3:  /* USER.179 GetSystemMetrics(int): Win16 WORD argument */
        if (!read_stack_word(ctx, cpu, 4, &arg)) {
            if (ctx) ctx->invalid_args = 1;
            value = 0;
        } else {
            value = (uint16_t)GetSystemMetrics((int)(int16_t)arg);
        }
        break;
    case 0x10b4:  /* USER.180 GetSysColor(int): DWORD result */
        dword = 1;
        if (!read_stack_word(ctx, cpu, 4, &arg)) {
            if (ctx) ctx->invalid_args = 1;
            value = 0;
        } else {
            value = GetSysColor((int)(int16_t)arg);
        }
        break;
    default:
        if (!win_ne_guest_user(ctx, cpu, service, &value, &dword)) {
            ctx->unsupported = service;
            return 0;
        }
        break;
    }
    cpu->gprx[regax].r16 = (uint16_t)value;
    cpu->gprx[regdx].r16 = dword ? (uint16_t)(value >> 16) : 0;
    return 1;
}
