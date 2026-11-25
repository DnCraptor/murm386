#ifndef FPU_GLUE_H
#define FPU_GLUE_H
#include <stdint.h>
extern uint8_t *tfpu_mem;
extern int tfpu_exceptions;
void tfpu_exec(int op, int group, int rm, int mem, uint32_t addr, uint16_t *ax);
void tfpu_state(uint8_t *buf, int save);   /* 94 bytes */
void tfpu_note_write(uint32_t addr, uint32_t n);   /* the test's (undo log) */
#endif
