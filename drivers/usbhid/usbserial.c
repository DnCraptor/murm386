#include "usbserial.h"

#include "config_save.h"
#include "pico/stdlib.h"
#include "tusb.h"

#if CFG_TUH_ENABLED && CFG_TUH_CDC

static int cdc_idx = -1;
static uint32_t requested_baud = 1200;
static uint32_t applied_baud = 1200;
static bool baud_pending;

/* One-shot physical reset for the CH340C-connected ZiModem.  This is kept
 * completely separate from the guest 8250 MCR. */
enum {
    MODEM_RESET_IDLE = 0,
    MODEM_RESET_ASSERT,
    MODEM_RESET_HOLD,
    MODEM_RESET_RELEASE
};
static uint8_t modem_reset_state = MODEM_RESET_IDLE;
static uint32_t modem_reset_deadline;

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
        modem_reset_state = MODEM_RESET_ASSERT;
    }
}

void tuh_cdc_umount_cb(uint8_t idx)
{
    if (cdc_idx == idx) {
        cdc_idx = -1;
        modem_reset_state = MODEM_RESET_IDLE;
    }
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

    /*
     * Reset ZiModem once when COM1's USB serial interface is mounted.
     * TinyUSB line-state bit 1 is RTS.  On the CH340C/ESP32 auto-reset
     * circuit, asserted RTS holds ESP32 EN low; DTR stays inactive so GPIO0
     * remains in normal-boot state.  Keep the USB pump non-blocking.
     */
    if (modem_reset_state == MODEM_RESET_ASSERT) {
        if (tuh_cdc_set_control_line_state((uint8_t)cdc_idx, 0x02, NULL, 0)) {
            modem_reset_deadline = time_us_32() + 100000u;
            modem_reset_state = MODEM_RESET_HOLD;
        }
        return;
    }

    if (modem_reset_state == MODEM_RESET_HOLD) {
        if ((int32_t)(time_us_32() - modem_reset_deadline) >= 0)
            modem_reset_state = MODEM_RESET_RELEASE;
        else
            return;
    }

    if (modem_reset_state == MODEM_RESET_RELEASE) {
        if (tuh_cdc_set_control_line_state((uint8_t)cdc_idx, 0x00, NULL, 0))
            modem_reset_state = MODEM_RESET_IDLE;
        return;
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
void usbserial_task(void) {}

#endif
