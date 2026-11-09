/* Symbolic DS state: overlapping stores, unknown writes, eviction and isolation. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../core/ne_ds_shadow.h"

static void check8(const win_ne_ds_shadow *s, unsigned off,
                   int known, unsigned expected)
{
    unsigned n = 0xdecafbad;
    int k = win_ne_ds_get8(s, (uint16_t)off, &n);
    assert(k == known);
    if (known) assert(n == expected);
    else assert(n == 0xdecafbad);
}
static void check16(const win_ne_ds_shadow *s, unsigned off,
                    int known, unsigned expected)
{
    unsigned n = 0xdecafbad;
    int k = win_ne_ds_get16(s, (uint16_t)off, &n);
    assert(k == known);
    if (known) assert(n == expected);
    else assert(n == 0xdecafbad);
}
int main(void)
{
    win_ne_ds_shadow s = {0};
    check8(&s, 0x20, 0, 0);
    check16(&s, 0x20, 0, 0);
    win_ne_ds_put16(&s, 0x20, 1, 0x1234);
    check16(&s, 0x20, 1, 0x1234);
    check8(&s, 0x20, 1, 0x34);
    check8(&s, 0x21, 1, 0x12);
    win_ne_ds_put8(&s, 0x20, 1, 0x56);
    check16(&s, 0x20, 1, 0x1256);
    win_ne_ds_put8(&s, 0x21, 0, 0x99);
    check8(&s, 0x21, 0, 0);
    check16(&s, 0x20, 0, 0);
    win_ne_ds_put16(&s, 0x20, 1, 0xabcd);
    win_ne_ds_put16(&s, 0x21, 1, 0xef12);
    check16(&s, 0x20, 1, 0x12cd);
    check16(&s, 0x21, 1, 0xef12);
    win_ne_ds_shadow fork = s;
    win_ne_ds_put8(&fork, 0x20, 1, 0x88);
    check8(&s, 0x20, 1, 0xcd);
    check8(&fork, 0x20, 1, 0x88);
    win_ne_ds_clear(&fork);
    check16(&fork, 0x20, 0, 0);
    check16(&s, 0x20, 1, 0x12cd);
    /* At capacity, discard *all* earlier proofs rather than alias entries. */
    for (unsigned i=0; i<WIN_NE_DS_SHADOW_BYTES+4u; i++)
        win_ne_ds_put8(&fork, (uint16_t)(0x100u+i), 1, i);
    assert(fork.used <= WIN_NE_DS_SHADOW_BYTES);
    check8(&fork, 0x100, 0, 0);
    check8(&fork, 0x100+WIN_NE_DS_SHADOW_BYTES+3u, 1,
           WIN_NE_DS_SHADOW_BYTES+3u);
    win_ne_ds_put8(&fork, 0xffff, 1, 1);
    check16(&fork, 0xffff, 0, 0);
    puts("WINN DS shadow: aliasing, taint, overflow, branch-local state PASS");
    return 0;
}
