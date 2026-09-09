#include "espflash.h"

#include "board_config.h"
#include "config_save.h"
#include "ff.h"
#include "usbserial.h"
#include "sdcard.h"
#include "pico/time.h"

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
#define ESP_CMD_SPI_ATTACH      0x0Du
#define ESP_CMD_SPI_FLASH_MD5   0x13u

#define ESP_RESPONSE_MAX        128u
#define ESP_PACKET_RAW_MAX      (8u + 16u + ESP_FLASH_BLOCK)
#define ESP_PACKET_SLIP_MAX     (2u + 2u * ESP_PACKET_RAW_MAX)


static void set_detail(char *detail, size_t detail_size, const char *text);

typedef struct {
    uint32_t h[4];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} md5_ctx_t;

static uint32_t rol32(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

static void md5_transform(md5_ctx_t *ctx, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const uint8_t r[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    uint32_t w[16];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);

    uint32_t a=ctx->h[0], b=ctx->h[1], c=ctx->h[2], d=ctx->h[3];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t f, g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3*i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7*i) & 15; }
        uint32_t t = d; d = c; c = b;
        b = b + rol32(a + f + k[i] + w[g], r[i]);
        a = t;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
}

static void md5_init(md5_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->h[0]=0x67452301; ctx->h[1]=0xefcdab89; ctx->h[2]=0x98badcfe; ctx->h[3]=0x10325476;
}

static void md5_update(md5_ctx_t *ctx, const uint8_t *data, size_t len)
{
    ctx->bytes += len;
    while (len) {
        size_t n = 64u - ctx->used;
        if (n > len) n = len;
        memcpy(ctx->block + ctx->used, data, n);
        ctx->used += n; data += n; len -= n;
        if (ctx->used == 64) { md5_transform(ctx, ctx->block); ctx->used = 0; }
    }
}

static void md5_final(md5_ctx_t *ctx, uint8_t out[16])
{
    uint64_t bits = ctx->bytes * 8u;
    uint8_t pad[72] = {0x80};
    size_t pad_len = (ctx->used < 56) ? (56u - ctx->used) : (120u - ctx->used);
    md5_update(ctx, pad, pad_len);
    uint8_t lenbuf[8];
    for (unsigned i = 0; i < 8; ++i) lenbuf[i] = (uint8_t)(bits >> (8*i));
    md5_update(ctx, lenbuf, 8);
    for (unsigned i = 0; i < 4; ++i) {
        out[i*4] = (uint8_t)ctx->h[i]; out[i*4+1] = (uint8_t)(ctx->h[i] >> 8);
        out[i*4+2] = (uint8_t)(ctx->h[i] >> 16); out[i*4+3] = (uint8_t)(ctx->h[i] >> 24);
    }
}

static bool file_md5(FIL *file, uint32_t size, uint8_t out[16], char *detail, size_t detail_size)
{
    if (f_lseek(file, 0) != FR_OK) { set_detail(detail, detail_size, "Cannot rewind firmware image"); return false; }
    md5_ctx_t ctx; md5_init(&ctx);
    uint8_t buf[512];
    uint32_t left = size;
    while (left) {
        UINT want = left > sizeof(buf) ? sizeof(buf) : left, got = 0;
        if (f_read(file, buf, want, &got) != FR_OK || got != want) {
            set_detail(detail, detail_size, "SD read failed while hashing firmware"); return false;
        }
        md5_update(&ctx, buf, got); left -= got;
    }
    md5_final(&ctx, out);
    return f_lseek(file, 0) == FR_OK;
}

static int hexval(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

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
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (!time_reached(deadline)) {
        uint8_t b;
        int64_t left_us = absolute_time_diff_us(get_absolute_time(), deadline);
        if (left_us <= 0)
            break;
        uint32_t left_ms = (uint32_t)((left_us + 999) / 1000);
        if (!usbserial_program_read_byte(&b, left_ms))
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
    return false;
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
        /* ESP32 ROM responses end in four status bytes: status, error, 0, 0. */
        if (payload_len < 4u) {
            set_detail(detail, detail_size, "Short ESP32 ROM status");
            return false;
        }
        const uint8_t *status = payload + payload_len - 4u;
        if (status[0] != 0) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "ESP ROM error %02X on command %02X",
                         status[1], command);
            return false;
        }
        return true;
    }

    set_detail(detail, detail_size, "No matching ESP ROM response");
    return false;
}

static bool esp_flash_md5(uint32_t address, uint32_t size, uint8_t md5[16],
                          uint8_t *raw_packet, uint8_t *slip_packet, uint8_t *response,
                          char *detail, size_t detail_size)
{
    uint8_t data[16] = {0};
    put_le32(data + 0, address);
    put_le32(data + 4, size);

    raw_packet[0] = 0x00; raw_packet[1] = ESP_CMD_SPI_FLASH_MD5;
    put_le16(raw_packet + 2, sizeof(data)); put_le32(raw_packet + 4, 0);
    memcpy(raw_packet + 8, data, sizeof(data));
    size_t slip_len = slip_encode(raw_packet, 8u + sizeof(data), slip_packet, ESP_PACKET_SLIP_MAX);
    if (!slip_len || !usbserial_program_write(slip_packet, slip_len, 1000u)) {
        set_detail(detail, detail_size, "USB serial write failed during MD5"); return false;
    }

    for (int attempt = 0; attempt < 16; ++attempt) {
        size_t response_len = 0;
        if (!read_slip_packet(response, ESP_RESPONSE_MAX, &response_len, 5000u)) {
            set_detail(detail, detail_size, "ESP ROM MD5 timeout"); return false;
        }
        if (response_len < 10 || response[0] != 0x01 || response[1] != ESP_CMD_SPI_FLASH_MD5)
            continue;
        uint16_t payload_len = get_le16(response + 2);
        if ((size_t)payload_len + 8u > response_len) {
            set_detail(detail, detail_size, "Malformed ESP ROM MD5 response"); return false;
        }
        const uint8_t *payload = response + 8;
        if (payload_len >= 34u) {
            bool ascii = true;
            for (unsigned i = 0; i < 32; ++i) if (hexval(payload[i]) < 0) { ascii = false; break; }
            if (ascii) {
                for (unsigned i = 0; i < 16; ++i)
                    md5[i] = (uint8_t)((hexval(payload[i*2]) << 4) | hexval(payload[i*2+1]));
                if (payload[32] != 0) {
                    if (detail && detail_size) snprintf(detail, detail_size, "ESP ROM MD5 error %02X", payload[33]);
                    return false;
                }
                return true;
            }
        }
        if (payload_len >= 18u) {
            memcpy(md5, payload, 16);
            if (payload[16] != 0) {
                if (detail && detail_size) snprintf(detail, detail_size, "ESP ROM MD5 error %02X", payload[17]);
                return false;
            }
            return true;
        }
        set_detail(detail, detail_size, "Short ESP ROM MD5 response"); return false;
    }
    set_detail(detail, detail_size, "No matching ESP ROM MD5 response");
    return false;
}

static bool reset_normal(char *detail, size_t detail_size)
{
    if (!usbserial_program_set_control_lines(0x02)) {
        set_detail(detail, detail_size, "Cannot reset ESP32"); return false;
    }
    usbserial_program_delay_ms(100);
    if (!usbserial_program_set_control_lines(0x00)) {
        set_detail(detail, detail_size, "Cannot release ESP32 reset"); return false;
    }
    usbserial_program_delay_ms(250);
    return true;
}

static bool enter_rom_loader(espflash_status_cb_t status, void *user,
                             char *detail, size_t detail_size)
{
    /* Espressif classic auto-reset sequence. DTR/RTS are active-low at the
     * ESP board's transistor network. TinyUSB line-state bits are DTR=bit0,
     * RTS=bit1, hence 0x02 -> 0x01 -> 0x00 enters UART download mode. */
    if (status) status("Setting ROM baud rate...", user);
    if (!usbserial_program_set_baudrate(ESP_ROM_BAUD)) {
        set_detail(detail, detail_size, "Cannot set CH340 baud rate");
        return false;
    }
    if (status) status("Draining modem input...", user);
    usbserial_program_flush_input();

    if (status) status("Resetting ESP32 into ROM loader...", user);
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
    if (status) status("Draining ROM startup data...", user);
    usbserial_program_flush_input();
    return true;
}

static espflash_result_t program_file(const char *filename,
                                      espflash_progress_cb_t progress,
                                      espflash_status_cb_t status,
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

    if (status) status("Opening firmware image...", user);
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

    uint8_t expected_md5[16];
    if (status) status("Hashing firmware image...", user);
    if (!file_md5(&file, image_size, expected_md5, detail, detail_size)) {
        f_close(&file);
        return ESPFLASH_FILE_ERROR;
    }

    if (status) status("Acquiring USB modem...", user);
    if (!usbserial_program_begin(3000u)) {
        f_close(&file);
        set_detail(detail, detail_size, "USB modem/CH340 is not connected");
        return ESPFLASH_NO_DEVICE;
    }

    if (status) status("Borrowing SD cache memory...", user);
    const size_t scratch_need = ESP_PACKET_RAW_MAX + ESP_PACKET_SLIP_MAX +
                                ESP_RESPONSE_MAX + ESP_FLASH_BLOCK;
    size_t scratch_bytes = 0;
    uint8_t *scratch = (uint8_t *)sdcard_borrow_ff_cache_arena(scratch_need,
                                                               &scratch_bytes);
    if (!scratch || scratch_bytes < scratch_need) {
        usbserial_program_end();
        f_close(&file);
        set_detail(detail, detail_size, "No reclaimable SD cache arena for ESP flasher");
        return ESPFLASH_PROTOCOL_ERROR;
    }
    uint8_t *raw_packet = scratch;
    uint8_t *slip_packet = raw_packet + ESP_PACKET_RAW_MAX;
    uint8_t *response = slip_packet + ESP_PACKET_SLIP_MAX;
    uint8_t *block = response + ESP_RESPONSE_MAX;

    espflash_result_t result = ESPFLASH_PROTOCOL_ERROR;
    if (status) status("Entering bootloader...", user);
    if (!enter_rom_loader(status, user, detail, detail_size))
        goto out;

    uint8_t sync_data[36];
    sync_data[0] = 0x07;
    sync_data[1] = 0x07;
    sync_data[2] = 0x12;
    sync_data[3] = 0x20;
    memset(sync_data + 4, 0x55, 32);

    if (status) status("Synchronizing with ESP32 ROM...", user);
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

    if (status) status("Attaching SPI flash...", user);
    uint8_t attach_data[8] = {0};
    if (!esp_command(ESP_CMD_SPI_ATTACH, attach_data, sizeof(attach_data), 0,
                     1000u, raw_packet, slip_packet, response,
                     detail, detail_size))
        goto out;

    uint8_t installed_md5[16];
    if (status) status("Checking installed firmware...", user);
    if (!esp_flash_md5(0u, image_size, installed_md5, raw_packet, slip_packet, response,
                       detail, detail_size))
        goto out;
    if (memcmp(installed_md5, expected_md5, 16) == 0) {
        if (status) status("Firmware already matches.", user);
        if (!reset_normal(detail, detail_size))
            goto out;
        result = ESPFLASH_NOT_NEEDED;
        set_detail(detail, detail_size, "Firmware already matches physical ESP32 flash");
        goto out;
    }

    const uint32_t block_count = (image_size + ESP_FLASH_BLOCK - 1u) / ESP_FLASH_BLOCK;
    uint8_t begin_data[16];
    put_le32(begin_data + 0, image_size);
    put_le32(begin_data + 4, block_count);
    put_le32(begin_data + 8, ESP_FLASH_BLOCK);
    put_le32(begin_data + 12, 0u);

    /* FLASH_BEGIN may spend substantial time erasing the requested range. */
    if (status) status("Erasing flash...", user);
    if (!esp_command(ESP_CMD_FLASH_BEGIN, begin_data, sizeof(begin_data), 0,
                     15000u, raw_packet, slip_packet, response,
                     detail, detail_size))
        goto out;

    if (status) status("Writing flash...", user);
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

    if (status) status("Verifying flash...", user);
    uint8_t verify_md5[16];
    if (!esp_flash_md5(0u, image_size, verify_md5, raw_packet, slip_packet, response,
                       detail, detail_size))
        goto out;
    if (memcmp(verify_md5, expected_md5, 16) != 0) {
        set_detail(detail, detail_size, "ESP32 flash verify MD5 mismatch");
        goto out;
    }

    /* Do not send FLASH_END to the ESP32 ROM loader. Espressif's current
     * serial-flasher deliberately skips it for ROM: the ROM may stop
     * responding after FLASH_END and its internal reboot does not re-sample
     * the strapping pins. Reset the target through the board's auto-reset
     * circuit instead, with GPIO0 released so it boots the new image. */
    if (status) status("Resetting modem...", user);
    if (!reset_normal(detail, detail_size))
        goto out;

    result = ESPFLASH_OK;
    set_detail(detail, detail_size, "Modem firmware programmed successfully");

out:
    /* Leave the auto-reset circuit inactive regardless of protocol outcome. */
    usbserial_program_set_control_lines(0x00);
    usbserial_program_end();
    f_close(&file);
    sdcard_release_ff_cache_arena(scratch);
    return result;
}

espflash_result_t espflash_selected_matches(bool *matches,
                                             espflash_status_cb_t status,
                                             void *user,
                                             char *detail,
                                             size_t detail_size)
{
    if (matches) *matches = false;
    const char *desired = config_get_esp_firmware();
    if (!desired || !desired[0]) {
        set_detail(detail, detail_size, "No managed modem firmware selected");
        return ESPFLASH_FILE_ERROR;
    }
    if (config_get_usb_mode() != USB_MODE_HOST) {
        set_detail(detail, detail_size, "USB HOST mode is required");
        return ESPFLASH_NO_DEVICE;
    }

    char path[FF_LFN_BUF + 1 + sizeof(SD_DATA_DIR_SLASH)];
    int n = snprintf(path, sizeof(path), "/%s/%s", SD_DATA_DIR, desired);
    if (n < 0 || (size_t)n >= sizeof(path)) { set_detail(detail, detail_size, "Firmware path is too long"); return ESPFLASH_FILE_ERROR; }
    FIL file; memset(&file, 0, sizeof(file));
    if (f_open(&file, path, FA_READ) != FR_OK) { set_detail(detail, detail_size, "Cannot open modem firmware"); return ESPFLASH_FILE_ERROR; }
    uint32_t image_size = (uint32_t)f_size(&file);
    if (image_size <= 0x1000u) { f_close(&file); set_detail(detail, detail_size, "Firmware image is too small"); return ESPFLASH_BAD_IMAGE; }
    uint8_t magic=0; UINT br=0;
    if (f_lseek(&file, 0x1000u) != FR_OK || f_read(&file, &magic, 1, &br) != FR_OK || br != 1 || magic != 0xE9u) {
        f_close(&file); set_detail(detail, detail_size, "Not a merged ESP32 image (0x1000 != E9)"); return ESPFLASH_BAD_IMAGE;
    }
    uint8_t expected[16];
    if (status) status("Hashing firmware image...", user);
    if (!file_md5(&file, image_size, expected, detail, detail_size)) { f_close(&file); return ESPFLASH_FILE_ERROR; }
    f_close(&file);

    if (status) status("Acquiring USB modem...", user);
    if (!usbserial_program_begin(3000u)) { set_detail(detail, detail_size, "USB modem/CH340 is not connected"); return ESPFLASH_NO_DEVICE; }
    const size_t scratch_need = ESP_PACKET_RAW_MAX + ESP_PACKET_SLIP_MAX + ESP_RESPONSE_MAX;
    size_t scratch_bytes = 0;
    uint8_t *scratch = (uint8_t *)sdcard_borrow_ff_cache_arena(scratch_need, &scratch_bytes);
    espflash_result_t result=ESPFLASH_PROTOCOL_ERROR;
    if (!scratch || scratch_bytes < scratch_need) {
        set_detail(detail, detail_size, "No reclaimable SD cache arena for ESP flasher");
        goto check_out;
    }
    uint8_t *raw = scratch;
    uint8_t *slip = raw + ESP_PACKET_RAW_MAX;
    uint8_t *resp = slip + ESP_PACKET_SLIP_MAX;
    if (status) status("Entering bootloader...", user);
    if (!enter_rom_loader(status, user, detail, detail_size)) goto check_out;
    uint8_t sync_data[36]={0x07,0x07,0x12,0x20}; memset(sync_data+4,0x55,32);
    if (status) status("Synchronizing with ESP32 ROM...", user);
    bool synced=false;
    for (int attempt=0; attempt<7 && !synced; ++attempt) {
        synced=esp_command(ESP_CMD_SYNC,sync_data,sizeof(sync_data),0,300u,raw,slip,resp,detail,detail_size);
        if (!synced) usbserial_program_delay_ms(50);
    }
    if (!synced) { result=ESPFLASH_SYNC_ERROR; goto check_out; }
    uint8_t attach[8]={0};
    if (status) status("Attaching SPI flash...", user);
    if (!esp_command(ESP_CMD_SPI_ATTACH,attach,sizeof(attach),0,1000u,raw,slip,resp,detail,detail_size)) goto check_out;
    uint8_t installed[16];
    if (status) status("Checking installed firmware...", user);
    if (!esp_flash_md5(0u,image_size,installed,raw,slip,resp,detail,detail_size)) goto check_out;
    if (!reset_normal(detail, detail_size)) goto check_out;
    if (matches) *matches = memcmp(installed, expected, 16) == 0;
    set_detail(detail, detail_size, (matches && *matches) ? "Firmware matches physical ESP32 flash" : "Different firmware is installed");
    result=ESPFLASH_OK;
check_out:
    usbserial_program_set_control_lines(0x00); usbserial_program_end();
    if (scratch)
        sdcard_release_ff_cache_arena(scratch);
    return result;
}

espflash_result_t espflash_update_selected(espflash_progress_cb_t progress,
                                            espflash_status_cb_t status,
                                            void *user,
                                            char *detail,
                                            size_t detail_size)
{
    const char *desired = config_get_esp_firmware();
    if (!desired || !desired[0]) { set_detail(detail, detail_size, "No managed modem firmware selected"); return ESPFLASH_FILE_ERROR; }
    if (config_get_usb_mode() != USB_MODE_HOST) { set_detail(detail, detail_size, "USB HOST mode is required"); return ESPFLASH_NO_DEVICE; }
    return program_file(desired, progress, status, user, detail, detail_size);
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
