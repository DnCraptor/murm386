#ifndef MURM386_CSM_H
#define MURM386_CSM_H

#include <stdint.h>
#include "i8257.h"
#include "i8259.h"

/* Covox Sound Master instance used by murm386.
 * I/O base is decoded in pc.c at 0240h.  DMA3/IRQ7 are valid original
 * jumper selections and avoid murm386's default SB16 DMA1/IRQ5 and FDC DMA2/IRQ6.
 */
#define CSM_IO_BASE  0x0240
#define CSM_DMA_CHAN 3
#define CSM_IRQ      7

void csm_init(I8257State *dma, PicState2 *pic);
uint8_t csm_read(uint16_t port);
void csm_write(uint16_t port, uint8_t value);
void csm_service(void);
int16_t csm_getsample(void);

#endif
