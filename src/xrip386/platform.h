#pragma once

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "../i386.h"
#include "../mem.h"
#include "../fpu.h"
#include "cpu.h"

#undef StepIP
#define StepIP(delta) (xrip_ip += (delta))
#define INLINE inline
#define HMA_START 0x100000u
#define dwordregs ((uint32_t *)active_cpu->gprx)
#define wordregs ((uint16_t *)active_cpu->gprx)
#define byteregs ((uint8_t *)active_cpu->gprx)
#define x86_flags active_cpu->flags

void resolve_flags(void);
static inline x86_flags_t *flags_now(void);

typedef uint8_t (*read86_t)(uint32_t);
typedef uint16_t (*read86w_t)(uint32_t);
typedef uint32_t (*read86dw_t)(uint32_t);
typedef void (*write86_t)(uint32_t, uint8_t);
typedef void (*write86w_t)(uint32_t, uint16_t);
typedef void (*write86dw_t)(uint32_t, uint32_t);

static read86_t read86;
static read86w_t readw86;
static read86dw_t readdw86;
static write86_t write86;
static write86w_t writew86;
static write86dw_t writedw86;

static void load_segment_protected(uint8_t index, uint16_t selector);
