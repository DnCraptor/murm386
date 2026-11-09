/* 386 protected-mode NE guest execution. */
#ifndef WINN_NE_EXEC_H
#define WINN_NE_EXEC_H
#include <stdint.h>
enum {
    WIN_NE_EXEC_BAD_CPU = -20,
    WIN_NE_EXEC_BAD_ENTRY = -21,
    WIN_NE_EXEC_BAD_BOOT = -22,
    WIN_NE_EXEC_BAD_ARGS = -23,
    WIN_NE_EXEC_FAULT = -25        /* see win_ne_fault */
};

/* Where a guest stopped: the exception vector (WIN_NE_TIMEOUT_VECTOR when
 * WINN stopped it) and CS:IP; segment is CS's NE segment number, 0 when
 * CS is none of the program's (a thunk, or a frame that cannot be read). */
typedef struct {
    int vector;
    uint16_t cs, ip, segment;
} win_ne_fault;

#if defined(__arm__) || defined(__thumb__)
int win_ne_execute_basic(const char *path, uint32_t *value, win_ne_fault *fault);
#endif
#endif
