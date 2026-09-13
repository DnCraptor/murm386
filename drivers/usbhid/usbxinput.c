/* XInput -> murm386 DOS game-port adapter. */
#include "tusb.h"
#include "host/usbh.h"
#include "xinput_host.h"
#include "usbgamepad.h"

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &usbh_xinput_driver;
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                   uint8_t const *report, uint16_t len) {
    (void)len;
    const xinputh_interface_t *xid = (const xinputh_interface_t *)report;
    const xinput_gamepad_t *p = &xid->pad;
    usbgamepad_xinput_report(dev_addr, instance, p->wButtons, p->sThumbLX, p->sThumbLY, xid->connected != 0);
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance,
                         const xinputh_interface_t *xid) {
    usbgamepad_xinput_report(dev_addr, instance, xid->pad.wButtons,
                             xid->pad.sThumbLX, xid->pad.sThumbLY,
                             xid->connected != 0);
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    usbgamepad_xinput_umount(dev_addr, instance);
}
