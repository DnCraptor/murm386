#include "usbserial.h"

#include "config_save.h"
#include "pico/stdlib.h"
#include "tusb.h"

#if CFG_TUH_ENABLED && CFG_TUH_CDC

static int cdc_idx = -1;
static uint32_t requested_baud = 1200;
static uint32_t applied_baud = 1200;
static bool baud_pending;
static bool programmer_active;

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
    return usbserial_iface_ready() && !programmer_active;
}

bool usbserial_read_byte(uint8_t *value)
{
    if (!value || programmer_active || !usbserial_iface_ready())
        return false;

    return tuh_cdc_read((uint8_t)cdc_idx, value, 1) == 1;
}

bool usbserial_write_byte(uint8_t value)
{
    if (programmer_active || !usbserial_iface_ready())
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
    if (!usbserial_iface_ready() || programmer_active)
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

static void usbserial_program_pump_once(void)
{
    tuh_task();
}

void usbserial_program_delay_ms(uint32_t delay_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(delay_ms);
    while (!time_reached(deadline)) {
        usbserial_program_pump_once();
        sleep_us(100);
    }
}

bool usbserial_program_begin(uint32_t wait_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(wait_ms);
    while (!usbserial_iface_ready()) {
        if (time_reached(deadline))
            return false;
        usbserial_program_pump_once();
        sleep_ms(1);
    }

    programmer_active = true;
    modem_reset_state = MODEM_RESET_IDLE;
    return true;
}

void usbserial_program_end(void)
{
    programmer_active = false;
    baud_pending = (requested_baud != applied_baud);
}

bool usbserial_program_set_baudrate(uint32_t baudrate)
{
    if (!programmer_active || !usbserial_iface_ready() || baudrate == 0)
        return false;

    if (!tuh_cdc_set_baudrate((uint8_t)cdc_idx, baudrate, NULL, 0))
        return false;
    usbserial_program_delay_ms(20);
    applied_baud = baudrate;
    return true;
}

bool usbserial_program_set_control_lines(uint8_t line_state)
{
    if (!programmer_active || !usbserial_iface_ready())
        return false;

    if (!tuh_cdc_set_control_line_state((uint8_t)cdc_idx, line_state & 0x03, NULL, 0))
        return false;
    usbserial_program_delay_ms(20);
    return true;
}

void usbserial_program_flush_input(void)
{
    if (!programmer_active || !usbserial_iface_ready())
        return;

    uint8_t scratch[64];
    absolute_time_t quiet = make_timeout_time_ms(20);
    absolute_time_t hard_deadline = make_timeout_time_ms(100);
    while (!time_reached(quiet) && !time_reached(hard_deadline)) {
        usbserial_program_pump_once();
        uint32_t got = tuh_cdc_read((uint8_t)cdc_idx, scratch, sizeof(scratch));
        if (got)
            quiet = make_timeout_time_ms(20);
        else
            sleep_us(100);
    }
}

bool usbserial_program_write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!programmer_active || !usbserial_iface_ready() || (!data && len))
        return false;

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    size_t done = 0;
    while (done < len) {
        usbserial_program_pump_once();
        uint32_t wrote = tuh_cdc_write((uint8_t)cdc_idx, data + done, (uint32_t)(len - done));
        if (wrote) {
            done += wrote;
            tuh_cdc_write_flush((uint8_t)cdc_idx);
            continue;
        }
        if (time_reached(deadline))
            return false;
        sleep_us(100);
    }

    tuh_cdc_write_flush((uint8_t)cdc_idx);
    usbserial_program_pump_once();
    return true;
}

bool usbserial_program_read_byte(uint8_t *value, uint32_t timeout_ms)
{
    if (!programmer_active || !usbserial_iface_ready() || !value)
        return false;

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        usbserial_program_pump_once();
        if (tuh_cdc_read((uint8_t)cdc_idx, value, 1) == 1)
            return true;
        sleep_us(100);
    } while (!time_reached(deadline));
    return false;
}


#else

bool usbserial_connected(void) { return false; }
bool usbserial_read_byte(uint8_t *value) { (void)value; return false; }
bool usbserial_write_byte(uint8_t value) { (void)value; return false; }
void usbserial_set_baudrate(uint32_t baudrate) { (void)baudrate; }
void usbserial_task(void) {}
bool usbserial_program_begin(uint32_t wait_ms) { (void)wait_ms; return false; }
void usbserial_program_end(void) {}
bool usbserial_program_set_baudrate(uint32_t baudrate) { (void)baudrate; return false; }
bool usbserial_program_set_control_lines(uint8_t line_state) { (void)line_state; return false; }
void usbserial_program_flush_input(void) {}
bool usbserial_program_write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{ (void)data; (void)len; (void)timeout_ms; return false; }
bool usbserial_program_read_byte(uint8_t *value, uint32_t timeout_ms)
{ (void)value; (void)timeout_ms; return false; }
void usbserial_program_delay_ms(uint32_t delay_ms) { (void)delay_ms; }

#endif
