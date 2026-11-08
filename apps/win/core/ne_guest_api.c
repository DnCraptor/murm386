/*
 * ne_guest_api.c - checked Win16 stack/pointer marshalling into Native WINN.
 * While CR0.PE=1 only nonblocking, DOS-independent native calls are allowed.
 * The x86 OUT callback has already returned to the WINN ARM call stack.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_guest_api.h"
#include <stdint.h>
#include <string.h>

extern uint32_t GetVersion(void);
extern uint16_t GetCurrentTask(void);
extern unsigned int GetNumTasks(void);
extern uint32_t GetTickCount(void);
extern int GetSystemMetrics(int);
extern uint32_t GetSysColor(int);

/* Bounds are those of the NE segment, not of the complete DOS memory map. */
static int read_segment(win_ne_guest_api_context *ctx, uint16_t selector,
                        uint32_t offset, void *dst, uint32_t len)
{
    if (!ctx || !ctx->image || !ctx->read || !dst)
        return 0;
    const win_ne_guest_image *g = ctx->image;
    for (uint16_t i = 1; i <= g->info.segment_count; i++) {
        const win_ne_guest_segment *s = &g->segments[i];
        if (s->selector != selector) continue;
        if (offset > s->size || len > s->size - offset)
            return 0;
        return ctx->read(ctx->opaque, s->linear + offset, dst, len);
    }
    return 0;
}

static int read_stack_word(win_ne_guest_api_context *ctx, CPU *cpu,
                           uint32_t stack_offset, uint16_t *value)
{
    uint32_t sp = cpu->gprx[regsp].r16;
    uint8_t bytes[2];
    if (!ctx || !ctx->image ||
        !read_segment(ctx, ctx->image->ss, sp + stack_offset, bytes, 2))
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
    if (!ctx || !ctx->image) return 0;
    for (uint16_t i = 1; i <= ctx->image->info.segment_count; i++) {
        const win_ne_guest_segment *s = &ctx->image->segments[i];
        if (s->selector != selector) continue;
        if (offset >= s->size) return 0;
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
    return 0;
}

int win_ne_guest_api(void *opaque, CPU *cpu, uint16_t service)
{
    if (!cpu) return 0;
    win_ne_guest_api_context *ctx = (win_ne_guest_api_context *)opaque;
    uint32_t value = 0;
    int dword = 0;
    uint16_t arg, ptr_seg;
    switch (service) {
    case 3:       value = GetVersion(); dword = 1; break;
    case 36:      value = GetCurrentTask(); break;
    case 152:     value = GetNumTasks(); break;
    case 0x100d:  value = GetTickCount(); dword = 1; break;
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
    default: return 0;
    }
    cpu->gprx[regax].r16 = (uint16_t)value;
    cpu->gprx[regdx].r16 = dword ? (uint16_t)(value >> 16) : 0;
    return 1;
}
