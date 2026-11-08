/* Restricted 386 protected-mode NE guest execution. */
#ifndef WINN_NE_EXEC_H
#define WINN_NE_EXEC_H
#include <stdint.h>
enum {
    WIN_NE_EXEC_BAD_CPU = -20,
    WIN_NE_EXEC_BAD_ENTRY = -21,
    WIN_NE_EXEC_BAD_BOOT = -22,
    WIN_NE_EXEC_BAD_ARGS = -23,
    WIN_NE_EXEC_BUSY = -24
};
#if defined(__arm__) || defined(__thumb__)
int win_ne_execute_basic(const char *path, uint32_t *value);
#endif
#endif
