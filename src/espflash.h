#ifndef ESPFLASH_H
#define ESPFLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESPFLASH_NOT_NEEDED = 0,
    ESPFLASH_OK,
    ESPFLASH_NO_DEVICE,
    ESPFLASH_FILE_ERROR,
    ESPFLASH_BAD_IMAGE,
    ESPFLASH_SYNC_ERROR,
    ESPFLASH_PROTOCOL_ERROR,
    ESPFLASH_SAVE_ERROR
} espflash_result_t;

typedef void (*espflash_progress_cb_t)(uint32_t done, uint32_t total, void *user);
typedef void (*espflash_status_cb_t)(const char *status, void *user);

/* Compare the selected merged image with the physical ESP32 flash.
 * The comparison is performed by the ESP32 ROM SPI_FLASH_MD5 command; no
 * remembered/configured "last flashed" state is used.
 */
espflash_result_t espflash_selected_matches(bool *matches,
                                             espflash_status_cb_t status,
                                             void *user,
                                             char *detail,
                                             size_t detail_size);

/* Program the selected merged image. Before erasing, compare the physical
 * flash and return ESPFLASH_NOT_NEEDED when it already matches. Verify the
 * written image with SPI_FLASH_MD5 before resetting the modem.
 */
espflash_result_t espflash_update_selected(espflash_progress_cb_t progress,
                                            espflash_status_cb_t status,
                                            void *user,
                                            char *detail,
                                            size_t detail_size);

const char *espflash_result_string(espflash_result_t result);

#endif /* ESPFLASH_H */
