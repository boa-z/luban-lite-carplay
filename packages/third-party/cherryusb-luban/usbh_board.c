/* SPDX-License-Identifier: Apache-2.0 */
/* SDK startup policy for the unmodified upstream CherryUSB host API. */
#include <aic_core.h>
#include "usbh_core.h"
#ifdef KERNEL_BAREMETAL
#include <aic_time.h>
#else
#include <rtthread.h>
#endif
#ifdef KERNEL_BAREMETAL
#if defined(LPKG_CHERRYUSB_OTG) && !defined(AIC_BOOTLOADER)
#include "usb_otg.h"
extern struct usb_otg g_usb_otg;
#endif
static int start_bus(uint8_t id, uintptr_t base)
{
    struct usbh_bus *bus = &g_usbhost_bus[id];
    if (bus->hub_mq) return 0;
#if defined(LPKG_CHERRYUSB_OTG) && !defined(AIC_BOOTLOADER)
    if (id == 0) {
        bus->busid = id;
        bus->hcd.reg_base = base;
        if (usb_otg_register_host(id, bus)) return 0;
    }
#endif
    int ret = usbh_initialize(id, base, NULL);
#if defined(LPKG_CHERRYUSB_OTG) && !defined(AIC_BOOTLOADER)
    if (id == 0 && !ret) g_usb_otg.host_on[id] = 1;
#endif
    return ret;
}
int usbh_init(void)
#else
static int usbh_init(void)
#endif
{
    uint8_t busid = 0;
    int rc;
#if defined(AIC_USING_USB0_HOST) || defined(AIC_USING_USB0_OTG)
#ifdef KERNEL_BAREMETAL
    rc = start_bus(busid++, USB_HOST0_BASE);
#else
    rc = usbh_initialize(busid++, USB_HOST0_BASE, NULL);
#endif
    if (rc) return rc;
#endif
#if defined(AIC_USING_USB1_HOST)
#ifdef KERNEL_BAREMETAL
    rc = start_bus(busid++, USB_HOST1_BASE);
#else
    rc = usbh_initialize(busid++, USB_HOST1_BASE, NULL);
#endif
    if (rc) return rc;
#endif
    (void)busid;
    return 0;
}
#ifndef KERNEL_BAREMETAL
INIT_ENV_EXPORT(usbh_init);
#endif
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT_ALIAS(lsusb, lsusb, list USB devices);
#endif
