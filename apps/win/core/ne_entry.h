/* Preflight of bounded Win16 code before switching the shared CPU to PE. */
#ifndef WINN_NE_ENTRY_H
#define WINN_NE_ENTRY_H
#include <stdint.h>
#include <stddef.h>

/* Every reachable path must end at a RETF (or a deliberate fault), after
 * at least one supported Win16 API call. Forward branches, counted LOOP,
 * and forward direct near CALL/RET with at most two nested calls are checked.
 * A near callee may read caller-pushed word parameters at BP+4, BP+6,...
 * and may release exactly those words through RET imm16 (PASCAL ABI), or
 * return with plain RET followed immediately by caller ADD SP,exact bytes
 * (CDECL ABI; 83 C4 ib and 81 C4 iw are accepted).
 * BP-relative stack locals require a standard frame and a bounded SUB SP.
 * DS reads/writes must stay below data_limit. Indirect calls/returns,
 * unknown SS addresses, segment overrides, delayed/partial caller cleanup
 * and recursion remain prohibited.
 * calls receives the maximum number of API calls on a validated path. */
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
