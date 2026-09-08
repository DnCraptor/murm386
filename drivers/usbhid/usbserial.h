#ifndef USBSERIAL_H
#define USBSERIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* USB host serial backend for the emulated PC COM port.
 * Active only while the firmware runs in USB_MODE_HOST.
 */
bool usbserial_connected(void);
bool usbserial_read_byte(uint8_t *value);
bool usbserial_write_byte(uint8_t value);
void usbserial_set_baudrate(uint32_t baudrate);
void usbserial_task(void);

/* Exclusive raw access used by the on-device ESP32 ROM flasher. While this
 * session is active the emulated 8250 sees the USB serial transport as
 * disconnected, so guest traffic cannot corrupt the bootloader protocol. */
bool usbserial_program_begin(uint32_t wait_ms);
void usbserial_program_end(void);
bool usbserial_program_set_baudrate(uint32_t baudrate);
bool usbserial_program_set_control_lines(uint8_t line_state);
void usbserial_program_flush_input(void);
bool usbserial_program_write(const uint8_t *data, size_t len, uint32_t timeout_ms);
bool usbserial_program_read_byte(uint8_t *value, uint32_t timeout_ms);
void usbserial_program_delay_ms(uint32_t delay_ms);

#endif /* USBSERIAL_H */
