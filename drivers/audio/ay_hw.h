#pragma once

#include <stdint.h>

void ay_hw_init(void);
void ay_hw_write_pcm(uint8_t sample);
void ay_hw_psg_select_register(uint8_t reg);
void ay_hw_psg_write_data(uint8_t value);
void ay_hw_psg_write_registers(const uint8_t regs[16]);
