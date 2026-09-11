#include "usbmsc_host.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "pico/time.h"
#include "tusb.h"
#include "usbhid.h"
#include "debug.h"

#if CFG_TUH_MSC

static volatile uint8_t msc_dev_addr;
static uint8_t msc_lun;
static uint32_t msc_block_count;
static uint32_t msc_block_size;
static volatile bool io_done;
static volatile bool io_ok;
static volatile bool in_tuh_task;

/* MSC READ/WRITE is made synchronous by pumping TinyUSB itself.  Do not call
 * the higher-level usbhid_task() here: it also services CDC and the HID
 * re-arm watchdog and may enqueue unrelated USB transfers while a SCSI
 * command is in flight.  This mirrors pico-speccy's working MSC pump. */
static void msc_service(void)
{
    if (in_tuh_task)
        return;
    in_tuh_task = true;
    tuh_task();
    in_tuh_task = false;
}

static bool io_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
    (void)dev_addr;
    io_ok = cb_data && cb_data->csw && cb_data->csw->status == 0;
    io_done = true;
    return true;
}

static bool io_wait(uint32_t timeout_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!io_done) {
        if (time_reached(deadline))
            return false;
        msc_service();
    }
    return io_ok;
}

bool usbmsc_host_ready(void)
{
    return msc_dev_addr != 0 && msc_block_size == 512u && msc_block_count != 0;
}

bool usbmsc_host_wait_ready(uint32_t timeout_ms)
{
    if (usbmsc_host_ready())
        return true;

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!usbmsc_host_ready()) {
        if (time_reached(deadline))
            return false;
        msc_service();
        sleep_ms(1);
    }
    return true;
}

uint32_t usbmsc_host_sector_count(void)
{
    return usbmsc_host_ready() ? msc_block_count : 0u;
}

bool usbmsc_host_read(uint32_t lba, void *buf, uint16_t count)
{
    if (!usbmsc_host_ready() || !buf || !count)
        return false;
    if (lba >= msc_block_count || count > msc_block_count - lba)
        return false;

    io_done = false;
    io_ok = false;
    if (!tuh_msc_read10(msc_dev_addr, msc_lun, buf, lba, count,
                        io_complete_cb, 0))
        return false;
    return io_wait(3000u + 100u * count);
}

bool usbmsc_host_write(uint32_t lba, const void *buf, uint16_t count)
{
    if (!usbmsc_host_ready() || !buf || !count)
        return false;
    if (lba >= msc_block_count || count > msc_block_count - lba)
        return false;

    io_done = false;
    io_ok = false;
    if (!tuh_msc_write10(msc_dev_addr, msc_lun, buf, lba, count,
                         io_complete_cb, 0))
        return false;
    return io_wait(5000u + 250u * count);
}

bool usbmsc_host_sync(void)
{
    /* WRITE(10) completion is sufficient here: there is no host-side write
       cache. A device-side cache, if any, is owned by the MSC device. */
    return usbmsc_host_ready();
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    /* One BIOS USB disk for now. Keep the first usable 512-byte-LBA device. */
    if (msc_dev_addr != 0)
        return;

    const uint8_t lun = 0;
    uint32_t blocks = tuh_msc_get_block_count(dev_addr, lun);
    uint32_t block_size = tuh_msc_get_block_size(dev_addr, lun);
    if (!blocks || block_size != 512u) {
        DBG_PRINT("USB MSC ignored: addr=%u blocks=%lu block=%lu\n",
                  dev_addr, (unsigned long)blocks, (unsigned long)block_size);
        return;
    }

    msc_lun = lun;
    msc_block_count = blocks;
    msc_block_size = block_size;
    msc_dev_addr = dev_addr;
    DBG_PRINT("USB MSC mounted: addr=%u sectors=%lu (%lu MiB)\n",
              dev_addr, (unsigned long)blocks,
              (unsigned long)(((uint64_t)blocks * 512u) >> 20));
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    if (dev_addr != msc_dev_addr)
        return;
    msc_dev_addr = 0;
    DBG_PRINT("USB MSC removed\n");
}

#else

bool usbmsc_host_ready(void) { return false; }
bool usbmsc_host_wait_ready(uint32_t timeout_ms) { (void)timeout_ms; return false; }
uint32_t usbmsc_host_sector_count(void) { return 0; }
bool usbmsc_host_read(uint32_t lba, void *buf, uint16_t count) { (void)lba; (void)buf; (void)count; return false; }
bool usbmsc_host_write(uint32_t lba, const void *buf, uint16_t count) { (void)lba; (void)buf; (void)count; return false; }
bool usbmsc_host_sync(void) { return false; }

#endif
