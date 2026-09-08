#ifndef ESPFLASH_H
#define ESPFLASH_H

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

/* Program the selected merged ESP32 image if it differs from esp_flashed.
 * The image is written at flash address 0 and esp_flashed is updated only
 * after every ROM-loader command has completed successfully.
 */
espflash_result_t espflash_update_if_needed(espflash_progress_cb_t progress,
                                             void *user,
                                             char *detail,
                                             size_t detail_size);

const char *espflash_result_string(espflash_result_t result);

#endif /* ESPFLASH_H */
