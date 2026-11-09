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
 * DS reads/writes must stay below data_limit. Byte-sized MOV accesses use
 * AL only and require a proven DS offset (direct or indexed).
 * CMP/TEST AL,imm8 and register-only TEST r8,r8 change FLAGS; memory
 * CMP r/m8,imm8 is limited to checked DS, never unknown SS addresses.
 * CLD/STD and bounded MOVS/CMPS/STOS/LODS/SCAS byte/word forms
 * accept REP, REPE, REPNE only with exact CX <= 64 and proven DATA
 * ranges (ES=DS in the WINN bootstrap). A conditional repeat makes
 * the final SI/DI/CX unknown; unsafe later accesses are rejected.
 * The verified ALU supports register 8/16-bit MOV, AND/OR/XOR, shifts,
 * TEST/NOT/NEG, exact-valued CMP and XCHG, and forward 386 Jcc rel16.
 * Register-only arithmetic never grants unproved DS/SS memory access.
 * Bounded ENTER imm16,0 frames and LIFO BX/SI/DI compiler saves are
 * verified through nested direct near CALL/RET, in a 512-byte window.
 * Exact CMP/TEST/INC/DEC condition flags permit bounded backward short/near
 * Jcc; backward direct calls to nonrecursive helpers are allowed. JCXZ and
 * bounded LOOPE/LOOPNE verify all exits when runtime ZF is unknown.
 * Indirect calls/returns,
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
