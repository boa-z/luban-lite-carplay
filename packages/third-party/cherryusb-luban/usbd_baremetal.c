/* SPDX-License-Identifier: Apache-2.0 */
#include "usbd_core.h"
#include <aic_time.h>

/* Compatibility entry used by the SDK bootloader's USB-upgrade detection. */
bool usbd_connect_check(uint32_t timeout)
{
    uint64_t start = aic_get_time_us();
    do {
        if (usb_device_is_configured(0)) return true;
    } while ((aic_get_time_us() - start) / 1000 < timeout);
    return false;
}
