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
    /* Register-based PASCAL args with a conventional BP frame. */
    const uint16_t api[] = { 0x1001, 88, 89 };
    const uint8_t frame[] = {
        0x55, 0x8b, 0xec,             /* PUSH BP; MOV BP,SP */
        0x31, 0xc0, 0x50,             /* XOR AX,AX; PUSH AX: owner */
        0xbb, 0x20, 0x00, 0x1e, 0x53,/* MOV BX,20h; PUSH DS; PUSH BX */
        0xb9, 0x80, 0x00, 0x1e, 0x51,/* MOV CX,80h; PUSH DS; PUSH CX */
        0x50,                         /* PUSH AX: MB_OK */
        0x9a, 0x00, 0x00, 0x08, 0x00,/* CALL FAR thunk 0: MessageBox */
        0x89, 0xec, 0x5d, 0xcb       /* MOV SP,BP; POP BP; RETF */
    };
    assert(win_ne_entry_calls(frame, sizeof(frame), 0, 8, api, 3, &n) && n == 1);
    memcpy(bad, good, sizeof good);
    bad[0] = 0x8b; bad[1] = 0x07; /* MOV AX,[BX] cannot access arbitrary RAM */
    assert(!win_ne_entry_calls(bad, sizeof bad, 0, 8, ids, 4, &n));
    /* Bad BP frame and wrong selector provenance must be rejected. */
    uint8_t broken_frame[sizeof frame];
    memcpy(broken_frame, frame, sizeof frame);
    broken_frame[1] = 0x90;
    assert(!win_ne_entry_calls(broken_frame, sizeof broken_frame, 0, 8, api, 3, &n));
    memcpy(broken_frame, frame, sizeof frame);
    broken_frame[22] = 0x90; /* no MOV SP,BP prior to POP BP */
    assert(!win_ne_entry_calls(broken_frame, sizeof broken_frame, 0, 8, api, 3, &n));
    memcpy(broken_frame, frame, sizeof frame);
    broken_frame[11] = 0x90; /* damaged MOV CX,imm16 */
    assert(!win_ne_entry_calls(broken_frame, sizeof broken_frame, 0, 8, api, 3, &n));
    const uint16_t str_ids[] = { 90 };
    const uint8_t ds_reg[] = {
        0x8c, 0xd8,                   /* MOV AX,DS preserves selector provenance */
        0x50, 0x68, 0x10, 0x00,       /* PUSH AX; PUSH 10h */
        0x9a, 0x00, 0x00, 0x08, 0x00, 0xcb
    };
    assert(win_ne_entry_calls(ds_reg,sizeof ds_reg,0,8,str_ids,1,&n) && n == 1);
    const uint8_t fake_ds[] = {
        0xb8, 0x18, 0x00,             /* MOV AX,18h is not MOV AX,DS */
        0x50, 0x68, 0x10, 0x00,
        0x9a, 0x00, 0x00, 0x08, 0x00, 0xcb
    };
    assert(!win_ne_entry_calls(fake_ds,sizeof fake_ds,0,8,str_ids,1,&n));
    /* Two FAR pointers, PASCAL order: dest first, then source. */
    const uint8_t strings[] = {
        0x1e, 0x68, 0x20, 0x00,
        0x1e, 0x68, 0x80, 0x00,
        0x9a, 0x10, 0x00, 0x08, 0x00, /* KERNEL.lstrcpy */
        0x1e, 0x68, 0x20, 0x00,
        0x1e, 0x68, 0x90, 0x00,
        0x9a, 0x20, 0x00, 0x08, 0x00, /* KERNEL.lstrcat */
        0xcb
    };
    assert(win_ne_entry_calls(strings,sizeof strings,0,8,api,3,&n) && n == 2);
    puts("ne entry stack validator: PASS");
    return 0;
}
