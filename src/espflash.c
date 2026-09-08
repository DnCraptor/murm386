#include "espflash.h"

#include "board_config.h"
#include "config_save.h"
#include "ff.h"
#include "usbserial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP_ROM_BAUD            115200u
#define ESP_FLASH_BLOCK         0x400u
#define ESP_SLIP_END            0xC0u
#define ESP_SLIP_ESC            0xDBu
#define ESP_SLIP_ESC_END        0xDCu
#define ESP_SLIP_ESC_ESC        0xDDu
#define ESP_CHECKSUM_MAGIC      0xEFu

#define ESP_CMD_FLASH_BEGIN     0x02u
#define ESP_CMD_FLASH_DATA      0x03u
#define ESP_CMD_FLASH_END       0x04u
#define ESP_CMD_SYNC            0x08u

#define ESP_RESPONSE_MAX        128u
#define ESP_PACKET_RAW_MAX      (8u + 16u + ESP_FLASH_BLOCK)
#define ESP_PACKET_SLIP_MAX     (2u + 2u * ESP_PACKET_RAW_MAX)

static void set_detail(char *detail, size_t detail_size, const char *text)
{
    if (!detail || detail_size == 0)
        return;
    snprintf(detail, detail_size, "%s", text ? text : "");
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint8_t flash_checksum(const uint8_t *data, size_t len)
{
    uint8_t checksum = ESP_CHECKSUM_MAGIC;
    while (len--)
        checksum ^= *data++;
    return checksum;
}

static size_t slip_encode(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t dst_size)
{
    size_t out = 0;
    if (dst_size < 2)
        return 0;

    dst[out++] = ESP_SLIP_END;
    for (size_t i = 0; i < src_len; ++i) {
        uint8_t b = src[i];
        if (b == ESP_SLIP_END || b == ESP_SLIP_ESC) {
            if (out + 2 >= dst_size)
                return 0;
            dst[out++] = ESP_SLIP_ESC;
            dst[out++] = (b == ESP_SLIP_END) ? ESP_SLIP_ESC_END : ESP_SLIP_ESC_ESC;
        } else {
            if (out + 1 >= dst_size)
                return 0;
            dst[out++] = b;
        }
    }
    dst[out++] = ESP_SLIP_END;
    return out;
}

static bool read_slip_packet(uint8_t *dst, size_t dst_size,
                             size_t *out_len, uint32_t timeout_ms)
{
    bool in_packet = false;
    bool escaped = false;
    size_t len = 0;

    while (true) {
        uint8_t b;
        if (!usbserial_program_read_byte(&b, timeout_ms))
            return false;

        if (b == ESP_SLIP_END) {
            if (!in_packet) {
                in_packet = true;
                escaped = false;
                len = 0;
                continue;
            }
            if (len == 0)
                continue;
            *out_len = len;
            return true;
        }

        if (!in_packet)
            continue;

        if (escaped) {
            if (b == ESP_SLIP_ESC_END)
                b = ESP_SLIP_END;
            else if (b == ESP_SLIP_ESC_ESC)
                b = ESP_SLIP_ESC;
            else {
                in_packet = false;
                escaped = false;
                len = 0;
                continue;
            }
            escaped = false;
        } else if (b == ESP_SLIP_ESC) {
            escaped = true;
            continue;
        }

        if (len >= dst_size) {
            in_packet = false;
            escaped = false;
            len = 0;
            continue;
        }
        dst[len++] = b;
    }
}

static bool esp_command(uint8_t command,
                        const uint8_t *data, uint16_t data_len,
                        uint32_t checksum, uint32_t timeout_ms,
                        uint8_t *raw_packet, uint8_t *slip_packet,
                        uint8_t *response, char *detail, size_t detail_size)
{
    const size_t raw_len = 8u + data_len;
    if (raw_len > ESP_PACKET_RAW_MAX) {
        set_detail(detail, detail_size, "ESP command too large");
        return false;
    }

    raw_packet[0] = 0x00;
    raw_packet[1] = command;
    put_le16(raw_packet + 2, data_len);
    put_le32(raw_packet + 4, checksum);
    if (data_len)
        memcpy(raw_packet + 8, data, data_len);

    size_t slip_len = slip_encode(raw_packet, raw_len,
                                  slip_packet, ESP_PACKET_SLIP_MAX);
    if (!slip_len || !usbserial_program_write(slip_packet, slip_len, timeout_ms)) {
        set_detail(detail, detail_size, "USB serial write failed");
        return false;
    }

    /* SYNC can leave several valid 0x08 replies queued. Ignore replies for
     * older commands until the response matching this command arrives. */
    for (int attempt = 0; attempt < 16; ++attempt) {
        size_t response_len = 0;
        if (!read_slip_packet(response, ESP_RESPONSE_MAX, &response_len, timeout_ms)) {
            set_detail(detail, detail_size, "ESP ROM response timeout");
            return false;
        }
        if (response_len < 10 || response[0] != 0x01)
            continue;
        if (response[1] != command)
            continue;

        uint16_t payload_len = get_le16(response + 2);
        if ((size_t)payload_len + 8u > response_len || payload_len < 2u) {
            set_detail(detail, detail_size, "Malformed ESP ROM response");
            return false;
        }

        const uint8_t *payload = response + 8;
        if (payload[0] != 0) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "ESP ROM error %02X on command %02X",
                         payload[1], command);
            return false;
        }
        return true;
    }

    set_detail(detail, detail_size, "No matching ESP ROM response");
    return false;
}

static bool enter_rom_loader(char *detail, size_t detail_size)
{
    /* Espressif classic auto-reset sequence. DTR/RTS are active-low at the
     * ESP board's transistor network. TinyUSB line-state bits are DTR=bit0,
     * RTS=bit1, hence 0x02 -> 0x01 -> 0x00 enters UART download mode. */
    if (!usbserial_program_set_baudrate(ESP_ROM_BAUD)) {
        set_detail(detail, detail_size, "Cannot set CH340 baud rate");
        return false;
    }
    usbserial_program_flush_input();

    if (!usbserial_program_set_control_lines(0x02)) {
        set_detail(detail, detail_size, "Cannot assert ESP reset");
        return false;
    }
    usbserial_program_delay_ms(100);

    if (!usbserial_program_set_control_lines(0x01)) {
        set_detail(detail, detail_size, "Cannot select ESP download mode");
        return false;
    }
    usbserial_program_delay_ms(100);

    if (!usbserial_program_set_control_lines(0x00)) {
        set_detail(detail, detail_size, "Cannot release ESP control lines");
        return false;
    }
    usbserial_program_delay_ms(50);
    usbserial_program_flush_input();
    return true;
}

static espflash_result_t program_file(const char *filename,
                                      espflash_progress_cb_t progress,
                                      void *user,
                                      char *detail, size_t detail_size)
{
    char path[FF_LFN_BUF + 1 + sizeof(SD_DATA_DIR_SLASH)];
    FIL file;
    memset(&file, 0, sizeof(file));

    if (!filename || !filename[0]) {
        set_detail(detail, detail_size, "No modem firmware selected");
        return ESPFLASH_FILE_ERROR;
    }

    int n = snprintf(path, sizeof(path), "/%s/%s", SD_DATA_DIR, filename);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        set_detail(detail, detail_size, "Firmware path is too long");
        return ESPFLASH_FILE_ERROR;
    }

    if (f_open(&file, path, FA_READ) != FR_OK) {
        set_detail(detail, detail_size, "Cannot open modem firmware");
        return ESPFLASH_FILE_ERROR;
    }

    uint32_t image_size = (uint32_t)f_size(&file);
    if (image_size <= 0x1000u) {
        f_close(&file);
        set_detail(detail, detail_size, "Firmware image is too small");
        return ESPFLASH_BAD_IMAGE;
    }

    /* Our managed files are merged esptool images written from address zero.
     * The ESP32 bootloader image begins at 0x1000 and must start with 0xE9.
     * This guard prevents an arbitrary .bin from erasing the modem flash. */
    uint8_t magic = 0;
    UINT br = 0;
    if (f_lseek(&file, 0x1000u) != FR_OK ||
        f_read(&file, &magic, 1, &br) != FR_OK || br != 1 || magic != 0xE9u ||
        f_lseek(&file, 0) != FR_OK) {
        f_close(&file);
        set_detail(detail, detail_size, "Not a merged ESP32 image (0x1000 != E9)");
        return ESPFLASH_BAD_IMAGE;
    }

    if (!usbserial_program_begin(3000u)) {
        f_close(&file);
        set_detail(detail, detail_size, "USB modem/CH340 is not connected");
        return ESPFLASH_NO_DEVICE;
    }

    uint8_t *raw_packet = (uint8_t *)malloc(ESP_PACKET_RAW_MAX);
    uint8_t *slip_packet = (uint8_t *)malloc(ESP_PACKET_SLIP_MAX);
    uint8_t *response = (uint8_t *)malloc(ESP_RESPONSE_MAX);
    uint8_t *block = (uint8_t *)malloc(ESP_FLASH_BLOCK);
    if (!raw_packet || !slip_packet || !response || !block) {
        free(raw_packet); free(slip_packet); free(response); free(block);
        usbserial_program_end();
        f_close(&file);
        set_detail(detail, detail_size, "Not enough RAM for ESP flasher");
        return ESPFLASH_PROTOCOL_ERROR;
    }

    espflash_result_t result = ESPFLASH_PROTOCOL_ERROR;
    if (!enter_rom_loader(detail, detail_size))
        goto out;

    uint8_t sync_data[36];
    sync_data[0] = 0x07;
    sync_data[1] = 0x07;
    sync_data[2] = 0x12;
    sync_data[3] = 0x20;
    memset(sync_data + 4, 0x55, 32);

    bool synced = false;
    for (int attempt = 0; attempt < 7 && !synced; ++attempt) {
        synced = esp_command(ESP_CMD_SYNC, sync_data, sizeof(sync_data), 0,
                             300u, raw_packet, slip_packet, response,
                             detail, detail_size);
        if (!synced)
            usbserial_program_delay_ms(50);
    }
    if (!synced) {
        result = ESPFLASH_SYNC_ERROR;
        goto out;
    }

    const uint32_t block_count = (image_size + ESP_FLASH_BLOCK - 1u) / ESP_FLASH_BLOCK;
    uint8_t begin_data[16];
    put_le32(begin_data + 0, image_size);
    put_le32(begin_data + 4, block_count);
    put_le32(begin_data + 8, ESP_FLASH_BLOCK);
    put_le32(begin_data + 12, 0u);

    /* FLASH_BEGIN may spend substantial time erasing the requested range. */
    if (!esp_command(ESP_CMD_FLASH_BEGIN, begin_data, sizeof(begin_data), 0,
                     15000u, raw_packet, slip_packet, response,
                     detail, detail_size))
        goto out;

    if (progress)
        progress(0, image_size, user);

    uint32_t done = 0;
    for (uint32_t seq = 0; seq < block_count; ++seq) {
        memset(block, 0xFF, ESP_FLASH_BLOCK);
        UINT got = 0;
        uint32_t wanted = image_size - done;
        if (wanted > ESP_FLASH_BLOCK)
            wanted = ESP_FLASH_BLOCK;

        if (f_read(&file, block, wanted, &got) != FR_OK || got != wanted) {
            set_detail(detail, detail_size, "SD read failed while flashing modem");
            result = ESPFLASH_FILE_ERROR;
            goto out;
        }

        uint8_t data[16 + ESP_FLASH_BLOCK];
        put_le32(data + 0, ESP_FLASH_BLOCK);
        put_le32(data + 4, seq);
        put_le32(data + 8, 0u);
        put_le32(data + 12, 0u);
        memcpy(data + 16, block, ESP_FLASH_BLOCK);

        if (!esp_command(ESP_CMD_FLASH_DATA, data, sizeof(data),
                         flash_checksum(block, ESP_FLASH_BLOCK), 3000u,
                         raw_packet, slip_packet, response,
                         detail, detail_size))
            goto out;

        done += wanted;
        if (progress)
            progress(done, image_size, user);
    }

    uint8_t end_data[4];
    put_le32(end_data, 0u); /* reboot and run the newly written image */
    if (!esp_command(ESP_CMD_FLASH_END, end_data, sizeof(end_data), 0,
                     1000u, raw_packet, slip_packet, response,
                     detail, detail_size))
        goto out;

    usbserial_program_delay_ms(100);
    result = ESPFLASH_OK;
    set_detail(detail, detail_size, "Modem firmware programmed successfully");

out:
    /* Leave the auto-reset circuit inactive regardless of protocol outcome. */
    usbserial_program_set_control_lines(0x00);
    usbserial_program_end();
    free(raw_packet);
    free(slip_packet);
    free(response);
    free(block);
    f_close(&file);
    return result;
}

espflash_result_t espflash_update_if_needed(espflash_progress_cb_t progress,
                                             void *user,
                                             char *detail,
                                             size_t detail_size)
{
    const char *desired = config_get_esp_firmware();
    const char *flashed = config_get_esp_flashed();

    if (!desired || !desired[0]) {
        set_detail(detail, detail_size, "No managed modem firmware selected");
        return ESPFLASH_NOT_NEEDED;
    }
    if (flashed && strcmp(desired, flashed) == 0) {
        set_detail(detail, detail_size, "Modem firmware is already current");
        return ESPFLASH_NOT_NEEDED;
    }
    if (config_get_usb_mode() != USB_MODE_HOST) {
        set_detail(detail, detail_size, "Modem update pending until USB HOST mode");
        return ESPFLASH_NO_DEVICE;
    }

    espflash_result_t result = program_file(desired, progress, user, detail, detail_size);
    if (result != ESPFLASH_OK)
        return result;

    /* Mark the image as installed only after the ROM loader accepted every
     * block and FLASH_END. If saving this marker fails, leave it mismatched so
     * the next run retries rather than falsely claiming success. */
    config_set_esp_flashed(desired);
    if (!config_save_all()) {
        set_detail(detail, detail_size, "Firmware written, but config save failed");
        return ESPFLASH_SAVE_ERROR;
    }
    return ESPFLASH_OK;
}

const char *espflash_result_string(espflash_result_t result)
{
    switch (result) {
        case ESPFLASH_NOT_NEEDED:   return "not needed";
        case ESPFLASH_OK:           return "ok";
        case ESPFLASH_NO_DEVICE:    return "USB modem not available";
        case ESPFLASH_FILE_ERROR:   return "firmware file error";
        case ESPFLASH_BAD_IMAGE:    return "invalid ESP32 image";
        case ESPFLASH_SYNC_ERROR:   return "ESP32 ROM sync failed";
        case ESPFLASH_PROTOCOL_ERROR:return "ESP32 flash protocol failed";
        case ESPFLASH_SAVE_ERROR:   return "config save failed";
        default:                    return "unknown error";
    }
}
