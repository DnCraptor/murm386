#ifndef TEST_WINN_DOS_API_H
#define TEST_WINN_DOS_API_H
#include "../../../api/cpu.h"
typedef struct PC { CPU *cpu; } PC;
PC *get_PC(void);
#endif
