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
    /* Both sides of a forward JNZ must be valid; exactly one service is
     * executed on either path. A direct DS write/read is allowed below the
     * caller stack, but not if the window reaches its reserved bytes. */
    const uint16_t branch_ids[] = { 3, 0x1001 };
    const uint8_t forward[] = {
        0xc7,0x06,0x20,0x00,0x34,0x12, /* MOV word DS:[20h],1234h */
        0xa1,0x20,0x00,                /* MOV AX,DS:[20h] */
        0x3d,0x34,0x12,                /* CMP AX,1234h */
        0x75,0x07,                     /* JNZ alternative */
        0x9a,0x00,0x00,0x08,0x00,      /* KERNEL.GetVersion */
        0xeb,0x05,                     /* JMP return */
        0x9a,0x10,0x00,0x08,0x00,      /* USER.MessageBox -- wrong argc */
        0xcb
    };
    /* The alternative must also match the API's Pascal stack layout. */
    assert(!win_ne_entry_calls_data(forward, sizeof forward, 0, 8,
                                    branch_ids, 2, 0xc0, &n));
    const uint16_t zero_args[] = { 3, 36 };
    assert(win_ne_entry_calls_data(forward, sizeof forward, 0, 8,
                                   zero_args, 2, 0xc0, &n) && n == 1);
    assert(!win_ne_entry_calls(forward, sizeof forward, 0, 8,
                               zero_args, 2, &n));
    assert(!win_ne_entry_calls_data(forward, sizeof forward, 0, 8,
                                    zero_args, 2, 0x21, &n));
    uint8_t flow_bad[sizeof forward];
    memcpy(flow_bad, forward, sizeof forward);
    flow_bad[13] = 0xff; /* backward JNZ */
    assert(!win_ne_entry_calls_data(flow_bad, sizeof flow_bad, 0, 8,
                                    zero_args, 2, 0xc0, &n));
    memcpy(flow_bad, forward, sizeof forward);
    flow_bad[20] = 0x00; /* JMP +0 is disallowed */
    assert(!win_ne_entry_calls_data(flow_bad, sizeof flow_bad, 0, 8,
                                    zero_args, 2, 0xc0, &n));
    memcpy(flow_bad, forward, sizeof forward);
    flow_bad[13] = 0x05; /* target inside JMP's instruction operands */
    assert(!win_ne_entry_calls_data(flow_bad, sizeof flow_bad, 0, 8,
                                    zero_args, 2, 0xc0, &n));
    memcpy(flow_bad, forward, sizeof forward);
    flow_bad[21] = 0xcb; /* alternative returns without calling an API */
    assert(!win_ne_entry_calls_data(flow_bad, sizeof flow_bad, 0, 8,
                                    zero_args, 2, 0xc0, &n));
    memcpy(flow_bad, forward, sizeof forward);
    flow_bad[2] = 0xc0; /* direct DS write into the protected stack region */
    assert(!win_ne_entry_calls_data(flow_bad, sizeof flow_bad, 0, 8,
                                    zero_args, 2, 0xc0, &n));
    memcpy(flow_bad, forward, sizeof forward);
    flow_bad[0] = 0x2e; /* CS override cannot write executable segment */
    assert(!win_ne_entry_calls_data(flow_bad, sizeof flow_bad, 0, 8,
                                    zero_args, 2, 0xc0, &n));
    /* A LOOP is allowed only when CX is exactly known and small. The
     * verifier follows each iteration, including all API calls in it. */
    const uint16_t loop_service[] = { 3 };
    const uint8_t bounded_loop[] = {
        0xb9,0x05,0x00,        /* MOV CX,5 */
        0x31,0xc0,             /* XOR AX,AX */
        0x40,                  /* INC AX */
        0xe2,0xfd,             /* LOOP -3 (five iterations) */
        0x9a,0x00,0x00,0x08,0x00, /* KERNEL.GetVersion */
        0xcb
    };
    assert(win_ne_entry_calls(bounded_loop, sizeof bounded_loop, 0,
                              8, loop_service, 1, &n) && n == 1);
    uint8_t broken_loop[sizeof bounded_loop];
    memcpy(broken_loop, bounded_loop, sizeof broken_loop);
    broken_loop[1] = 0; /* MOV CX,0: LOOP would wrap to 65535 */
    assert(!win_ne_entry_calls(broken_loop, sizeof broken_loop, 0,
                               8, loop_service, 1, &n));
    memcpy(broken_loop, bounded_loop, sizeof broken_loop);
    broken_loop[1] = 65; /* over static bound */
    assert(!win_ne_entry_calls(broken_loop, sizeof broken_loop, 0,
                               8, loop_service, 1, &n));
    memcpy(broken_loop, bounded_loop, sizeof broken_loop);
    broken_loop[7] = 2; /* LOOP forward */
    assert(!win_ne_entry_calls(broken_loop, sizeof broken_loop, 0,
                               8, loop_service, 1, &n));
    memcpy(broken_loop, bounded_loop, sizeof broken_loop);
    broken_loop[7] = 0xfc; /* backward into MOV CX immediate */
    assert(!win_ne_entry_calls(broken_loop, sizeof broken_loop, 0,
                               8, loop_service, 1, &n));
    const uint8_t unknown_cx[] = {
        0xb9,0x02,0x00, 0x31,0xc1, 0x90, 0xe2,0xfd,
        0x9a,0x00,0x00,0x08,0x00,0xcb
    }; /* XOR CX,AX loses exact counter value */
    assert(!win_ne_entry_calls(unknown_cx, sizeof unknown_cx, 0,
                               8, loop_service, 1, &n));
    const uint8_t endless_reset[] = {
        0xb9,0x02,0x00,0x90,0xe2,0xfa,
        0x9a,0x00,0x00,0x08,0x00,0xcb
    }; /* iteration jumps to MOV CX,2, and resets counter forever */
    assert(!win_ne_entry_calls(endless_reset, sizeof endless_reset, 0,
                               8, loop_service, 1, &n));
    const uint8_t uninitialized[] = {
        0x90,0xe2,0xfd,0x9a,0x00,0x00,0x08,0x00,0xcb
    };
    assert(!win_ne_entry_calls(uninitialized, sizeof uninitialized, 0,
                               8, loop_service, 1, &n));
    /* Clobbering CX via MOV must invalidate the exact loop count. */
    const uint8_t clobbered[] = {
        0xb9,0x02,0x00, 0x89,0xd1, 0x90, 0xe2,0xfd,
        0x9a,0x00,0x00,0x08,0x00,0xcb
    }; /* MOV CX,DX */
    assert(!win_ne_entry_calls(clobbered, sizeof clobbered, 0,
                               8, loop_service, 1, &n));
    /* Deliberate terminal #UD after a known far API call is permitted:
     * the private IDT will terminate this NE task without returning to
     * guest code. A fault before the first import remains disallowed. */
    const uint8_t deliberate_ud[] = {
        0x9a,0x00,0x00,0x08,0x00, /* KERNEL.GetVersion */
        0x0f,0x0b                /* UD2, terminal #UD */
    };
    assert(win_ne_entry_calls(deliberate_ud, sizeof deliberate_ud, 0,
                              8, loop_service, 1, &n) && n == 1);
    assert(!win_ne_entry_calls(deliberate_ud+5, 2, 0,
                               8, loop_service, 1, &n));
    /* Register-indexed DATA with statically known BX/SI values, plus LEA.
     * Old tests above still reject unknown-index MOV AX,[BX]. */
    const uint8_t index_ok[] = {
        0xbb, 0x1e, 0x00,       /* MOV BX,001Eh */
        0x83, 0xc3, 0x02,       /* ADD BX,+2 -> 0020h */
        0xbe, 0x04, 0x00,       /* MOV SI,0004h */
        0xb8, 0x34, 0x12,       /* MOV AX,1234h */
        0x89, 0x40, 0x01,       /* MOV [BX+SI+1],AX -> 0025h */
        0x8b, 0x40, 0x01,       /* MOV AX,[BX+SI+1] */
        0x8d, 0x78, 0x01,       /* LEA DI,[BX+SI+1] -> 0025h */
        0x8b, 0x05,             /* MOV AX,[DI] */
        0x9a, 0x00, 0x00, 0x08, 0x00, /* KERNEL.GetVersion */
        0xcb
    };
    assert(win_ne_entry_calls_data(index_ok, sizeof index_ok, 0,
                                   8, loop_service, 1, 0x40, &n) && n == 1);
    assert(!win_ne_entry_calls(index_ok, sizeof index_ok, 0,
                               8, loop_service, 1, &n));
    uint8_t index_bad[sizeof index_ok];
    memcpy(index_bad, index_ok, sizeof index_ok);
    index_bad[1] = 0x3e; /* address becomes 0045h: beyond 0040h */
    assert(!win_ne_entry_calls_data(index_bad, sizeof index_bad, 0,
                                    8, loop_service, 1, 0x40, &n));
    memcpy(index_bad, index_ok, sizeof index_ok);
    index_bad[4] = 0xeb; /* SUB BX,2: still in range and known */
    assert(win_ne_entry_calls_data(index_bad, sizeof index_bad, 0,
                                   8, loop_service, 1, 0x40, &n));
    memcpy(index_bad, index_ok, sizeof index_ok);
    index_bad[4] = 0xf3; /* SUB BX,unknown grp6 -> forbidden */
    assert(!win_ne_entry_calls_data(index_bad, sizeof index_bad, 0,
                                    8, loop_service, 1, 0x40, &n));
    memcpy(index_bad, index_ok, sizeof index_ok);
    index_bad[13] = 0x46; /* indexed write via [BP+SI+disp] (SS) */
    assert(!win_ne_entry_calls_data(index_bad, sizeof index_bad, 0,
                                    8, loop_service, 1, 0x40, &n));
    memcpy(index_bad, index_ok, sizeof index_ok);
    index_bad[12] = 0x26; /* ES override, not a safe 16-bit DS write */
    assert(!win_ne_entry_calls_data(index_bad, sizeof index_bad, 0,
                                    8, loop_service, 1, 0x40, &n));
    memcpy(index_bad, index_ok, sizeof index_ok);
    index_bad[10] = 0x3f; index_bad[11] = 0x00; /* AX value irrelevant */
    assert(win_ne_entry_calls_data(index_bad, sizeof index_bad, 0,
                                   8, loop_service, 1, 0x40, &n));
    /* The source index must not be a value read from guest memory. */
    const uint8_t unknown_index[] = {
        0xbe,0x10,0x00,      /* MOV SI,0010 */
        0x8b,0x1c,           /* MOV BX,[SI] -> unknown */
        0x8b,0x07,           /* MOV AX,[BX] rejected */
        0x9a,0x00,0x00,0x08,0x00,0xcb
    };
    assert(!win_ne_entry_calls_data(unknown_index, sizeof unknown_index, 0,
                                    8, loop_service, 1, 0x40, &n));
    const uint8_t wrap_index[] = {
        0xbe,0xff,0xff, /* MOV SI,FFFFh */
        0x8b,0x44,0x01, /* MOV AX,[SI+1] - cannot wrap to DS:[0] */
        0x9a,0x00,0x00,0x08,0x00,0xcb
    };
    assert(!win_ne_entry_calls_data(wrap_index, sizeof wrap_index, 0,
                                    8, loop_service, 1, 0x40, &n));
    /* Both paths of JNZ must have proven in-range indexed addresses. */
    const uint8_t branch_index[] = {
        0xbb,0x20,0x00, 0x3d,0x00,0x00, /* BX=0020; CMP AX,0 */
        0x75,0x02,                      /* JNZ +2 => MOV BX,003F */
        0xeb,0x03,                      /* JMP +3 => MOV AX,[BX] */
        0xbb,0x3f,0x00,                /* MOV BX,003F */
        0x8b,0x07,                     /* word read at 003F crosses 0040 */
        0x9a,0x00,0x00,0x08,0x00,0xcb
    };
    assert(!win_ne_entry_calls_data(branch_index, sizeof branch_index, 0,
                                    8, loop_service, 1, 0x40, &n));
    /* LOOP advances a checked index and must recheck each iteration. */
    const uint8_t indexed_loop[] = {
        0xb9,0x03,0x00,       /* MOV CX,3 */
        0xbe,0x20,0x00,       /* MOV SI,0020h */
        0xb8,0x34,0x12,       /* MOV AX,1234h */
        0x89,0x04,            /* MOV [SI],AX */
        0x46,                 /* INC SI */
        0xe2,0xfb,            /* LOOP -5 */
        0x9a,0x00,0x00,0x08,0x00,0xcb
    };
    assert(win_ne_entry_calls_data(indexed_loop, sizeof indexed_loop, 0,
                                   8, loop_service, 1, 0x40, &n) && n == 1);
    uint8_t index_loop_bad[sizeof indexed_loop];
    memcpy(index_loop_bad, indexed_loop, sizeof indexed_loop);
    index_loop_bad[4] = 0x3e; /* 003Eh fits once, 003F crosses DATA limit */
    assert(!win_ne_entry_calls_data(index_loop_bad, sizeof index_loop_bad, 0,
                                    8, loop_service, 1, 0x40, &n));
    memcpy(index_loop_bad, indexed_loop, sizeof indexed_loop);
    index_loop_bad[1] = 0; /* LOOP with CX=0 wraps to 0xffff */
    assert(!win_ne_entry_calls_data(index_loop_bad, sizeof index_loop_bad, 0,
                                    8, loop_service, 1, 0x40, &n));
    /* Clobbering an exact pointer with a native API return invalidates it. */
    const uint8_t api_pointer[] = {
        0x9a,0x00,0x00,0x08,0x00, /* GetVersion returns unknown AX */
        0x89,0xc3,                /* MOV BX,AX */
        0x8b,0x07,                /* MOV AX,[BX] rejected */
        0x9a,0x00,0x00,0x08,0x00,0xcb
    };
    assert(!win_ne_entry_calls_data(api_pointer, sizeof api_pointer, 0,
                                    8, loop_service, 1, 0x40, &n));
    puts("ne entry stack validator: PASS");
    return 0;
}
