#include "usbserial.h"

#include "config_save.h"
#include "tusb.h"

#if CFG_TUH_ENABLED && CFG_TUH_CDC

static int cdc_idx = -1;
static uint32_t requested_baud = 1200;
static uint32_t applied_baud = 1200;
static bool baud_pending;
static uint8_t requested_line_state;
static uint8_t applied_line_state;
static bool line_state_pending;

static bool usbserial_host_active(void)
{
    return config_get_usb_mode() == USB_MODE_HOST;
}

static bool usbserial_iface_ready(void)
{
    return usbserial_host_active() &&
           cdc_idx >= 0 &&
           tuh_cdc_mounted((uint8_t)cdc_idx);
}

bool usbserial_connected(void)
{
    return usbserial_iface_ready();
}

bool usbserial_read_byte(uint8_t *value)
{
    if (!value || !usbserial_iface_ready())
        return false;

    return tuh_cdc_read((uint8_t)cdc_idx, value, 1) == 1;
}

bool usbserial_write_byte(uint8_t value)
{
    if (!usbserial_iface_ready())
        return false;

    if (tuh_cdc_write((uint8_t)cdc_idx, &value, 1) != 1)
        return false;

    tuh_cdc_write_flush((uint8_t)cdc_idx);
    return true;
}

void usbserial_set_baudrate(uint32_t baudrate)
{
    if (baudrate == 0)
        return;

    requested_baud = baudrate;
    baud_pending = (requested_baud != applied_baud);
}

void usbserial_set_control_lines(uint8_t line_state)
{
    requested_line_state = line_state & 0x03;
    line_state_pending = (requested_line_state != applied_line_state);
}

void tuh_cdc_mount_cb(uint8_t idx)
{
    if (!usbserial_host_active())
        return;

    /* COM1 owns the first USB serial interface. */
    if (cdc_idx < 0)
        cdc_idx = idx;

    if (cdc_idx == idx) {
        applied_baud = 1200; /* CFG_TUH_CDC_LINE_CODING_ON_ENUM */
        baud_pending = (requested_baud != applied_baud);
        applied_line_state = 0; /* no forced DTR/RTS on enumeration */
        line_state_pending = (requested_line_state != applied_line_state);
    }
}

void tuh_cdc_umount_cb(uint8_t idx)
{
    if (cdc_idx == idx)
        cdc_idx = -1;
}

void tuh_cdc_rx_cb(uint8_t idx)
{
    (void)idx;
    /* RX stays in TinyUSB's FIFO. u8250_update() consumes a byte only when
     * the emulated 8250 receive register is free.
     */
}

void usbserial_task(void)
{
    if (!usbserial_iface_ready())
        return;

    /* Apply guest modem-control outputs from the normal host-pump context.
     * Only MCR.DTR and MCR.RTS are physical RS-232 lines; OUT1/OUT2/LOOP
     * remain entirely inside the emulated 8250.
     */
    if (line_state_pending) {
        if (tuh_cdc_set_control_line_state((uint8_t)cdc_idx, requested_line_state, NULL, 0)) {
            applied_line_state = requested_line_state;
            line_state_pending = false;
        }
    }

    /* Baud changes are deferred for the same reason: never initiate a USB
     * control transfer from guest OUT handling or a CDC callback.
     */
    if (baud_pending) {
        if (tuh_cdc_set_baudrate((uint8_t)cdc_idx, requested_baud, NULL, 0)) {
            applied_baud = requested_baud;
            baud_pending = false;
        }
    }
}

#else

bool usbserial_connected(void) { return false; }
bool usbserial_read_byte(uint8_t *value) { (void)value; return false; }
bool usbserial_write_byte(uint8_t value) { (void)value; return false; }
void usbserial_set_baudrate(uint32_t baudrate) { (void)baudrate; }
void usbserial_set_control_lines(uint8_t line_state) { (void)line_state; }
void usbserial_task(void) {}

#endif
