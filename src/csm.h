#ifndef MURM386_CSM_H
#define MURM386_CSM_H

#include <stdint.h>
#include "i8257.h"
#include "i8259.h"

/* Covox Sound Master instance used by murm386.  Prince of Persia 1.0
 * expects DMA1, so Sound Master owns DMA1 while selected; the settings
 * layer makes it mutually exclusive with AdLib/Sound Blaster. */
#define CSM_IO_BASE_DEFAULT 0x0240
#define CSM_DMA_CHAN        1
#define CSM_IRQ             7

void csm_init(I8257State *dma, PicState2 *pic);
void csm_set_io_base(uint16_t base);
uint16_t csm_get_io_base(void);
void csm_bind_dma(void);
void csm_deactivate(void);
uint8_t csm_read(uint16_t port);
void csm_write(uint16_t port, uint8_t value);
void csm_service(void);
int csm_channel_c_output_enabled(void);
int16_t csm_getsample(void);

#endif
