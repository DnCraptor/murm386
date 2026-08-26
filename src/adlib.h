#ifndef ADLIB_H
#define ADLIB_H

#include <stdint.h>
#include <pico/time.h>

#define FLOAT float

#define ADLIB_BATCH_SIZE 128

typedef struct AdlibState AdlibState;

void adlib_write(void *opaque, uint32_t nport, uint32_t val);
uint32_t adlib_read(void *opaque, uint32_t nport);
AdlibState *adlib_new();
/* Audio IRQ consumer and private 1 kHz core1 OPL producer, respectively. */
int16_t adlib_getsample(AdlibState *s);
void adlib_service(AdlibState *s);
bool adlib_timer_callback(repeating_timer_t *rt);

#endif /* ADLIB_H */
