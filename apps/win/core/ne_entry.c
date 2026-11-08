/* Restrictive Win16 entry validator with a simulated Pascal argument stack.
 * This intentionally does NOT claim to interpret arbitrary Win16 x86 code.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "ne_entry.h"
#include "ne_guest.h"

/* Word arguments, only known safe service signatures. */
static unsigned args_for(uint16_t id)
{
    switch (id) {
    case 3: case 36: case 152: case 0x100d: return 0;
    case 90: return 2;                      /* lstrlen: offset + selector */
    case 0x10b3: case 0x10b4: return 1;     /* int */
    default: return 0xffu;
    }
}

/* A PUSH DS marker cannot be forged by pushing a numeric selector. */
enum { PUSH_IMM = 1, PUSH_DS = 2 };

int win_ne_entry_calls(const uint8_t *p, size_t bytes, size_t ip,
                       uint16_t thunk_selector, const uint16_t *service_ids,
                       unsigned thunk_count, unsigned *calls)
{
    if (!p || !calls || !service_ids || ip >= bytes || !thunk_selector ||
        !thunk_count || thunk_count > WIN_NE_GUEST_ACTIVE_THUNKS)
        return 0;
    unsigned count = 0, depth = 0;
    unsigned char types[4] = { 0 };
    size_t at = ip;
    while (at < bytes && at - ip < 256u) {
        if (p[at] == 0x90) { at++; continue; }
        if (p[at] == 0xcb) {               /* RETF */
            if (!count || depth)
                return 0;
            *calls = count;
            return 1;
        }
        if (p[at] == 0x1e) {               /* PUSH DS */
            if (depth >= 4) return 0;
            types[depth++] = PUSH_DS;
            at++;
            continue;
        }
        if (p[at] == 0x68) {               /* PUSH imm16 */
            if (bytes - at < 3 || depth >= 4) return 0;
            types[depth++] = PUSH_IMM;
            at += 3;
            continue;
        }
        if (p[at] != 0x9a || bytes - at < 5 || count >= 32u)
            return 0;
        uint16_t offset = (uint16_t)(p[at+1] | (uint16_t)p[at+2] << 8);
        uint16_t selector = (uint16_t)(p[at+3] | (uint16_t)p[at+4] << 8);
        if (selector != thunk_selector || (offset % WIN_NE_THUNK_BYTES) ||
            offset / WIN_NE_THUNK_BYTES >= thunk_count)
            return 0;
        unsigned words = args_for(service_ids[offset / WIN_NE_THUNK_BYTES]);
        if (words != depth) return 0;
        if ((words == 1 && types[0] != PUSH_IMM) ||
            (words == 2 && (types[0] != PUSH_DS || types[1] != PUSH_IMM)))
            return 0;
        depth = 0;                        /* PASCAL RETF imm16 cleans the stack */
        at += 5;
        count++;
    }
    return 0;
}
