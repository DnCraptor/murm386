#ifndef USBMSC_HOST_H
#define USBMSC_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TinyUSB MSC host backend. Only 512-byte logical sectors are exposed. */
bool usbmsc_host_ready(void);
bool usbmsc_host_wait_ready(uint32_t timeout_ms);
uint32_t usbmsc_host_sector_count(void);
bool usbmsc_host_read(uint32_t lba, void *buf, uint16_t count);
bool usbmsc_host_write(uint32_t lba, const void *buf, uint16_t count);
bool usbmsc_host_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* USBMSC_HOST_H */
