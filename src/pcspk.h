#ifndef PCSPK_H
#define PCSPK_H

#include <stdint.h>
#include "i8254.h"
typedef struct PCSpkState PCSpkState;
PCSpkState *pcspk_init(PITState *pit);
uint32_t pcspk_ioport_read(void *opaque);
void pcspk_ioport_write(void *opaque, uint32_t val);
/* Square-wave phase: -1 or +1 while sounding, 0 while silent. */
int16_t pcspk_sample(PCSpkState *s);

#endif /* PCSPK_H */
