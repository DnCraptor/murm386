#pragma once

#include <stdint.h>

void csm_psg_reset(void);
void csm_psg_select_register(uint8_t reg);
void csm_psg_write_data(uint8_t value);
uint8_t csm_psg_read_data(void);
uint8_t csm_psg_sample(void);
