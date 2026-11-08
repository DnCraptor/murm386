/* Entry validator: bounded instruction and Pascal stack-shape checking. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../core/ne_entry.h"
#include "../core/ne_guest.h"

int main(void)
{
    const uint16_t ids[] = { 3, 0x10b3, 90, 0x10b4 };
    const uint8_t good[] = {
        0x90,                                     /* NOP */
        0x9a,0x00,0x00,0x08,0x00,                /* GetVersion (0 words) */
        0x68,0x00,0x00,                          /* PUSH 0 */
        0x9a,0x10,0x00,0x08,0x00,                /* GetSystemMetrics (1 word) */
        0x1e,0x68,0x10,0x00,                     /* PUSH DS; PUSH 0x10 */
        0x9a,0x20,0x00,0x08,0x00,                /* lstrlen (2 words) */
        0xcb                                      /* RETF */
    };
    unsigned n = 0;
    assert(win_ne_entry_calls(good,sizeof good,0,8,ids,4,&n) && n==3);
    assert(!win_ne_entry_calls(good,sizeof good,0,8,ids,2,&n));
    assert(!win_ne_entry_calls(good,sizeof good,0,0x10,ids,4,&n));
    assert(!win_ne_entry_calls(good,sizeof good,2,8,ids,4,&n));
    assert(!win_ne_entry_calls(good,sizeof good,0,8,ids,0,&n));
    assert(!win_ne_entry_calls(good,sizeof good,0,8,NULL,4,&n));
    uint8_t bad[sizeof good];
    memcpy(bad,good,sizeof bad);
    bad[0] = 0xf4; /* HLT not allowed */
    assert(!win_ne_entry_calls(bad,sizeof bad,0,8,ids,4,&n));
    memcpy(bad,good,sizeof bad);
    bad[sizeof good - 1] = 0xeb; /* branch not allowed */
    assert(!win_ne_entry_calls(bad,sizeof bad,0,8,ids,4,&n));
    memcpy(bad,good,sizeof bad);
    bad[15] = 0x90; /* the far-pointer's PUSH imm16 is missing */
    assert(!win_ne_entry_calls(bad,sizeof bad,0,8,ids,4,&n));
    memcpy(bad,good,sizeof bad);
    bad[14] = 0x68; /* DS marker replaced by a numeric immediate */
    assert(!win_ne_entry_calls(bad,sizeof bad,0,8,ids,4,&n));
    assert(!win_ne_entry_calls(good,sizeof good-1,0,8,ids,4,&n));
    assert(!win_ne_entry_calls(good,sizeof good,sizeof good,8,ids,4,&n));
    puts("ne entry stack validator: PASS");
    return 0;
}
