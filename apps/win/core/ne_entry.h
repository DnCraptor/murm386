/* Preflight of bounded Win16 code before switching the shared CPU to PE. */
#ifndef WINN_NE_ENTRY_H
#define WINN_NE_ENTRY_H
#include <stdint.h>
#include <stddef.h>

/* All reachable paths must end at a RETF after at least one supported API
 * call. Conditional and unconditional branches are allowed only forward;
 * DATA reads and writes (direct or register-indexed) must stay strictly
 * below data_limit. Indexed addresses require exact known 16-bit register
 * values; BP/SS accesses, segment overrides, and indirect branches remain
 * disabled. The private IDT provides recovery for supported guest faults.
 * LOOP rel8 is allowed only with an exactly tracked CX <= 64; all taken
 * iterations are validated within the instruction visit limit.
 * calls receives the maximum number of API calls on any validated path. */
int win_ne_entry_calls_data(const uint8_t *p, size_t bytes, size_t ip,
                            uint16_t thunk_selector, const uint16_t *service_ids,
                            unsigned thunk_count, size_t data_limit,
                            unsigned *calls);

/* Legacy straight-line interface for host tests and callers not providing
 * a verified DATA region: identical rules, but no direct memory accesses. */
int win_ne_entry_calls(const uint8_t *p, size_t bytes, size_t ip,
                       uint16_t thunk_selector, const uint16_t *service_ids,
                       unsigned thunk_count, unsigned *calls);
#endif
