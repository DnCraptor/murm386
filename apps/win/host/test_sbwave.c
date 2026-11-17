/*
 * test_sbwave.c - SBWAVE.DLL (apps/windll/sbwave) on a model of the
 * emulator's SB16 and 8237 as far as it uses them (src/sb16.c,
 * src/i8257.c): the DMA reads ahead of what plays into a 4096-byte buffer,
 * a DSP transfer's end sets the interrupt's status, auto-init goes round
 * again, DA/D9 ends it after the block, a single-cycle command drops what
 * is buffered; a masked channel reads nothing. Streams of blocks of
 * several sizes, 8 and 16 bits, the player asking at several paces, must
 * come out whole, without a pause between the blocks; with the DMA read
 * between the DSP's command and its DA (not masked), whole and in order.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "windows.h"
#include "windll.h"
#include "wave_driver.h"

const win_dll_ne *__win_dll_ne;

/* ---------------------------------------------------------------- the model */
static uint8_t mem[0x10000];            /* low memory at linear 0x30000 */
#define LIN 0x30000u
void *dos_alloc_low(size_t n) { (void)n; return mem + 0x123 * 16; }
void dos_free_low(void *p) { (void)p; }
uint32_t dos_ptr_linear(const void *p) { return LIN + (uint32_t)((const uint8_t *)p - mem); }
uint32_t sound_hw_mask(void) { return 1u << 2; }

static int race, nomask;                /* a DMA run between the command and DA; no mask */
static unsigned base8, cnt8, base16, cnt16, ff8, ff16, mixidx, masked;
static unsigned pos;                    /* bytes into the transfer region */
static int dsp_on, autoinit, bits16, irqbits;
static unsigned block, till;
static uint8_t cmdbuf[8];
static int need, ncmd;
static uint8_t audio[4096];
static unsigned aq, ap;                 /* produced, consumed */
static uint8_t out[1 << 20];
static unsigned nout;

static uint32_t region(void) { return bits16 ? (base16 << 1) - LIN : base8 - LIN; }
static unsigned len(void) { return bits16 ? (cnt16 + 1) * 2 : cnt8 + 1; }

static void dma_run(void)
{
    while (dsp_on && (nomask || !(masked & (bits16 ? 2 : 1))) && aq - ap < 4096) {
        unsigned k = 4096 - (aq - ap);
        if (k > till) k = till;
        if (k > len() - pos) k = len() - pos;
        for (unsigned i = 0; i < k; i++)
            audio[aq++ % 4096] = mem[(region() + pos + i) & 0xffff];
        pos = (pos + k) % len();
        till -= k;
        if (!till) {
            irqbits |= bits16 ? 2 : 1;
            if (autoinit) till = block;
            else dsp_on = 0;
        }
    }
}

static void play(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        out[nout++] = ap != aq ? audio[ap++ % 4096] : 0xee;    /* 0xee: nothing to play */
    dma_run();
}

static void dsp_cmd(void)
{
    uint8_t c = cmdbuf[0];
    if (c == 0xc6 || c == 0xb6 || c == 0xc0 || c == 0xb0) {
        bits16 = c < 0xc0;
        autoinit = (c >> 2) & 1;
        block = (cmdbuf[2] | cmdbuf[3] << 8) + 1;
        block <<= bits16;
        if (!autoinit) ap = aq;          /* the flush */
        till = block;
        dsp_on = 1;
        if (race) dma_run();
    } else if (c == 0xd9 || c == 0xda) autoinit = 0;
    else if (c == 0xd0 || c == 0xd5) dsp_on = 0;
    else if (c == 0xd4 || c == 0xd6) dsp_on = 1;
}

uint8_t inp(uint16_t p)
{
    switch (p) {
    case 0x22c: return 0;
    case 0x22e: { uint8_t r = irqbits & 1 ? 0x80 : 0x80; irqbits &= ~1; return r; }
    case 0x22f: irqbits &= ~2; return 0x80;
    case 0x22a: return 0xaa;
    case 0x225: return mixidx == 0x82 ? (uint8_t)irqbits : 0;
    case 0x02: { unsigned a = base8 + (bits16 ? 0 : pos); unsigned v = ff8 ? a >> 8 : a; ff8 ^= 1; return (uint8_t)v; }
    case 0xc4: { unsigned a = base16 + (bits16 ? pos / 2 : 0); unsigned v = ff16 ? a >> 8 : a; ff16 ^= 1; return (uint8_t)v; }
    }
    return 0;
}

void outp(uint16_t p, uint8_t v)
{
    switch (p) {
    case 0x22c:
        if (!need) {
            ncmd = 0;
            need = v == 0x41 ? 3 : (v == 0xc6 || v == 0xb6 || v == 0xc0 || v == 0xb0) ? 4 : 1;
        }
        cmdbuf[ncmd++] = v;
        if (ncmd == need) { if (cmdbuf[0] != 0x41) dsp_cmd(); need = 0; }
        break;
    case 0x224: mixidx = v; break;
    case 0x0a: if ((v & 3) == 1) { masked = v & 4 ? masked | 1 : masked & ~1u; if (!(v & 4)) dma_run(); } break;
    case 0xd4: if ((v & 3) == 1) { masked = v & 4 ? masked | 2 : masked & ~2u; if (!(v & 4)) dma_run(); } break;
    case 0x0c: ff8 = 0; break;
    case 0xd8: ff16 = 0; break;
    case 0x02: base8 = ff8 ? (base8 & 0xff) | v << 8 : (base8 & ~0xffu) | v; ff8 ^= 1; if (!ff8) pos = 0; break;
    case 0x83: base8 = (base8 & 0xffff) | (unsigned)v << 16; break;
    case 0x03: cnt8 = ff8 ? (cnt8 & 0xff) | v << 8 : (cnt8 & ~0xffu) | v; ff8 ^= 1; break;
    case 0xc4: base16 = ff16 ? (base16 & 0xff) | v << 8 : (base16 & ~0xffu) | v; ff16 ^= 1; if (!ff16) pos = 0; break;
    case 0x8b: base16 = (base16 & 0xffff) | (unsigned)v << 15; break;
    case 0xc6: cnt16 = ff16 ? (cnt16 & 0xff) | v << 8 : (cnt16 & ~0xffu) | v; ff16 ^= 1; break;
    }
}

/* ---------------------------------------------------------------- the test */

const win_dll_info *sbwave_WinDllInit(void);

static uint8_t src[200000];

/* blocks of sizes from sz[], the player polling every poll bytes of play */
static int run(const unsigned *sz, int nsz, unsigned total, unsigned poll, int r, int bits)
{
    const win_wave_driver *(*entry)(void) = NULL;
    const win_dll_info *info = sbwave_WinDllInit();
    for (unsigned i = 0; i < info->export_count; i++)
        if (!strcmp(info->exports[i].name, "WinWaveDriver"))
            entry = (const win_wave_driver *(*)(void))info->exports[i].native;
    const win_wave_driver *d = entry();
    race = r;
    nout = 0;
    for (unsigned i = 0; i < total; i++)
        src[i] = (uint8_t)(bits == 16 ? (i & 1 ? 0x12 : 1 + (i * 7) % 250) : 1 + (i * 13) % 100);
    d->open();
    unsigned sent = 0, k = 0, done = 0, nblocks = 0, steps = 0;
    while ((done < nblocks || sent < total) && steps++ < 100000) {
        done += (unsigned)d->finished();
        while (sent < total && d->room() > 0) {
            unsigned n = sz[k++ % nsz];
            if (n > total - sent) n = total - sent;
            if (!d->submit(src + sent, n, 22050, bits, 1)) { printf("submit refused\n"); break; }
            sent += n;
            nblocks++;
        }
        play(poll);
    }
    play(8192);
    d->close();
    /* the output: silence/nothing before, then the samples whole, then silence */
    unsigned i = 0, q = bits == 16 ? 0 : 0x80;
    while (i < nout && (out[i] == 0xee || out[i] == q)) i++;
    int ok = nout - i >= total && !memcmp(out + i, src, total);
    unsigned bad = 0;
    for (unsigned j = 0; j < total && i + j < nout; j++) if (out[i + j] != src[j]) { bad = j; break; }
    printf("blocks %u done %u, out %u from %u: %s (apart at %u)\n", nblocks, done, nout, i,
           ok ? "whole, no pause" : "not in one piece", bad);
    return ok;
}

int main(void)
{
    static const unsigned a[] = { 4096 }, b[] = { 1500, 700, 4096, 333, 2000 }, c[] = { 1024 }, e[] = { 1500, 700, 4096, 334, 2000 };
    int ok = 1;
    ok &= run(a, 1, 50000, 512, 0, 8);
    ok &= run(b, 5, 50000, 300, 0, 8);
    ok &= run(c, 1, 30000, 200, 0, 8);
    ok &= run(b, 5, 50000, 300, 1, 8);     /* the race: DA late */
    ok &= run(a, 1, 50000, 512, 1, 8);
    ok &= run(a, 1, 60000, 512, 0, 16);
    ok &= run(e, 5, 50000, 300, 1, 16);
    ok &= run(c, 1, 40000, 100, 1, 16);
    /* the realignment: the DMA not masked, the race; the samples all, in
     * order, a pause between */
    nomask = 1;
    for (int r = 0; r < 2; r++) {
        run(r ? c : b, r ? 1 : 5, 50000, 300, 1, 8);
        unsigned j = 0, m = 0;
        for (unsigned i = 0; i < nout && j < 50000; i++)
            if (out[i] != 0x80 && out[i] != 0xee) {
                if (out[i] != src[j]) { m = 1; break; }
                j++;
            }
        printf("  realigned: %u samples, in order: %s\n", j, !m && j == 50000 ? "yes" : "NO");
        ok &= !m && j == 50000;
    }
    printf(ok ? "sbwave: PASS\n" : "sbwave: FAIL\n");
    return !ok;
}
