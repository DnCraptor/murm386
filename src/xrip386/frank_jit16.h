#pragma once

#include <stdbool.h>
#include <stddef.h>

bool xrip_fastloop_active(void);
void xrip_fastloop_enable(void);
bool xrip_jit_active(void);
size_t xrip_jit_cache_size(void);
void xrip_jit_use_cache(void *memory);
void xrip_jit_use_sram(void *memory, size_t capacity);
