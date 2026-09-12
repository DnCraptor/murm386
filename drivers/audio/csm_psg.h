#pragma once

#include <stdint.h>

void csm_psg_reset(void);
void csm_psg_select_register(uint8_t reg);
void csm_psg_write_data(uint8_t value);
void csm_psg_set_channel_c_output(int enabled);
void csm_psg_force_channel_c_output(void);
uint8_t csm_psg_read_data(void);
int16_t csm_psg_sample(void);
int csm_psg_is_expanded(void);
int csm_psg_is_bank_b(void);
uint8_t csm_psg_get_bank_a_register(uint8_t reg);
