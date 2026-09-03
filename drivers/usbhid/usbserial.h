#ifndef USBSERIAL_H
#define USBSERIAL_H

#include <stdbool.h>
#include <stdint.h>

/* USB host serial backend for the emulated PC COM port.
 * Active only while the firmware runs in USB_MODE_HOST.
 */
bool usbserial_connected(void);
bool usbserial_read_byte(uint8_t *value);
bool usbserial_write_byte(uint8_t value);
void usbserial_set_baudrate(uint32_t baudrate);
void usbserial_set_control_lines(uint8_t line_state);
void usbserial_task(void);

#endif /* USBSERIAL_H */
