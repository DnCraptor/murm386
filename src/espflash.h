#ifndef ESPFLASH_H
#define ESPFLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESPFLASH_OK = 0,
    ESPFLASH_NO_DEVICE,
    ESPFLASH_FILE_ERROR,
    ESPFLASH_BAD_IMAGE,
    ESPFLASH_SYNC_ERROR,
    ESPFLASH_PROTOCOL_ERROR,
    ESPFLASH_SAVE_ERROR
} espflash_result_t;

typedef void (*espflash_progress_cb_t)(uint32_t done, uint32_t total, void *user);
typedef void (*espflash_status_cb_t)(const char *status, void *user);

/* Program the selected merged image after an explicit request from Disk
 * Manager. Verify the written image with SPI_FLASH_MD5 before resetting the
 * modem; no attempt is made to identify firmware already installed in ESP32.
 */
espflash_result_t espflash_update_file(const char *filename,
                                        espflash_progress_cb_t progress,
                                        espflash_status_cb_t status,
                                        void *user,
                                        char *detail,
                                        size_t detail_size);

const char *espflash_result_string(espflash_result_t result);

#endif /* ESPFLASH_H */
