/* SPDX-License-Identifier: GPL-3.0-only
 * CherryUSB AOA adapter. Persistent aligned URBs/DMA buffers and DATA toggles
 * follow the product's existing USB adapter; no Apple protocol is linked.
 */
#include "androidauto_platform.h"
#include <rtthread.h>
#include <rthw.h>
#include <stdatomic.h>
#include "usbh_core.h"

#define BULK_BYTES 4096u
typedef struct
{
    struct usbh_urb urb;
    struct usb_endpoint_descriptor endpoint;
    usb_osal_sem_t done;
    uint8_t toggle;
    bool quarantined;
    USB_MEM_ALIGNX struct usb_setup_packet setup;
    USB_MEM_ALIGNX uint8_t bytes[BULK_BYTES];
} transfer;
static struct
{
    rt_mutex_t lock;
    transfer *in, *out, *control;
    struct usbh_hubport *hport;
    rtaa_aoa aoa;
    rtaa_rtthread_clock clock;
    atomic_bool present, claimed, poisoned;
    atomic_uint generation;
    uint8_t bus, hub, port;
} usb;
static uint64_t clock_ms(void *unused)
{
    (void)unused;
    rt_base_t flags = rt_hw_interrupt_disable();
    uint64_t now = rtaa_rtthread_now_ms(&usb.clock);
    rt_hw_interrupt_enable(flags);
    return now;
}
static void complete(void *arg, int bytes)
{
    (void)bytes;
    usb_osal_sem_give(arg);
}
static int map_error(int rc)
{
    if (rc == -USB_ERR_TIMEOUT)
        return RTAA_ETIMEOUT;
    if (rc == -USB_ERR_NODEV || rc == -USB_ERR_NOTCONN || rc == -USB_ERR_SHUTDOWN)
        return RTAA_ECANCEL;
    return RTAA_EIO;
}
/* The caller holds the device lifetime lock. A failed kill quarantines the
 * persistent URB, semaphore and DMA memory until reboot: never reuse memory
 * possibly still owned by the controller. No caller buffer reaches DMA. */
static int transact(transfer *t, struct usbh_hubport *hport, bool control, bool input,
                    uint8_t *data, size_t size, uint32_t timeout)
{
    if (!t || t->quarantined || !hport || !hport->connected || !timeout)
        return RTAA_EIO;
    if (size > BULK_BYTES)
        size = BULK_BYTES;
    while (usb_osal_sem_take(t->done, 0) == 0)
    {
    }
    memset(&t->urb, 0, sizeof(t->urb));
    if (size && !input)
        memcpy(t->bytes, data, size);
    if (control)
    {
        usbh_control_urb_fill(&t->urb, hport, &t->setup, size ? t->bytes : NULL, size, 0, complete,
                              t->done);
        t->endpoint = *t->urb.ep;
        t->urb.ep = &t->endpoint;
    }
    else
    {
        usbh_bulk_urb_fill(&t->urb, hport, &t->endpoint, t->bytes, size, 0, complete, t->done);
        t->urb.data_toggle = t->toggle;
    }
    int rc = usbh_submit_urb(&t->urb);
    if (rc < 0)
        return map_error(rc);
    bool finished = usb_osal_sem_take(t->done, timeout) == 0;
    if (!finished)
    {
        finished = usb_osal_sem_take(t->done, 1) == 0;
        if (!finished)
        {
            int killed = usbh_kill_urb(&t->urb);
            if (killed && usb_osal_sem_take(t->done, 1) != 0)
            {
                t->quarantined = true;
                usb.poisoned = true;
                usb.claimed = false;
                return RTAA_EIO;
            }
            /* The kill callback is cancellation, not successful input. A partial
             * cancelled transfer cannot safely be resumed as a fresh AAP frame. */
            return t->urb.actual_length ? RTAA_EIO : RTAA_ETIMEOUT;
        }
    }
    if (t->urb.errorcode < 0)
        return map_error(t->urb.errorcode);
    size_t prefix = control ? sizeof(t->setup) : 0;
    size_t actual = t->urb.actual_length;
    if (actual < prefix || actual - prefix > size)
        return RTAA_EIO;
    actual -= prefix;
    if (!control)
        t->toggle = t->urb.data_toggle;
    if (input && actual)
        memcpy(data, t->bytes, actual);
    return (int)actual;
}
static int aoa_control(void *ctx, uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *data, uint16_t length, uint32_t timeout)
{
    transfer *t = usb.control;
    t->setup = (struct usb_setup_packet){type, request, value, index, length};
    return transact(t, ctx, true, (type & 0x80) != 0, data, length, timeout);
}
static int init_usb_adapter(void)
{
    usb.lock = rt_mutex_create("aa-usb", RT_IPC_FLAG_PRIO);
    transfer **pipes[] = {&usb.in, &usb.out, &usb.control};
    for (unsigned i = 0; i < 3; ++i)
    {
        *pipes[i] = rt_malloc_align(sizeof(transfer), CONFIG_USB_ALIGN_SIZE);
        if (!*pipes[i])
            goto failed;
        memset(*pipes[i], 0, sizeof(transfer));
        (*pipes[i])->done = usb_osal_sem_create(0);
        if (!(*pipes[i])->done)
            goto failed;
    }
    if (usb.lock)
        return 0;
failed:
    usb.poisoned = true;
    return -RT_ENOMEM; /* Initial resources are retained; no later submit. */
}
INIT_PREV_EXPORT(init_usb_adapter);

/* Dedicated phone connector: query AOA support without a vendor whitelist.
 * Exclude Apple and hubs. AOA requests stay on the same bus/hub/physical port
 * across re-enumeration; another connector cannot finish this transaction. */
uint8_t androidauto_usb_select_config(struct usbh_hubport *hport)
{
    if (!hport || hport->bus->busid != RTANDROIDAUTO_USB_BUS || !usb.lock || usb.poisoned ||
        hport->device_desc.idVendor == 0x05ac ||
        hport->device_desc.bDeviceClass == USB_DEVICE_CLASS_HUB)
        return 0;
    if (rt_mutex_take(usb.lock, rt_tick_from_millisecond(20)) != RT_EOK)
        return 0;
    if (!usb.present &&
        !rtaa_aoa_accessory(hport->device_desc.idVendor, hport->device_desc.idProduct))
    {
        if (usb.aoa.state == RTAA_AOA_REENUMERATING)
        {
            /* Keep the original route until its bounded re-enumeration expires. */
            if (rtaa_aoa_poll(&usb.aoa, clock_ms(NULL)) == RTAA_AGAIN)
            {
                rt_mutex_release(usb.lock);
                return 0;
            }
        }
        memset(&usb.aoa, 0, sizeof(usb.aoa));
        const char *strings[] = {"boa-z",
                                 "rt-androidauto",
                                 "Android Auto receiver",
                                 "0.1",
                                 "https://github.com/boa-z/rt-androidauto",
                                 "D213-AA"};
        rtaa_usb_control control = {hport, aoa_control, NULL};
        usb.bus = hport->bus->busid;
        usb.hub = hport->parent->index;
        usb.port = hport->port;
        int rc = rtaa_aoa_begin(&usb.aoa, &control, strings, clock_ms(NULL));
        if (!rc)
            rt_kprintf("androidauto: AOA requested on bus=%u hub=%u port=%u\n", usb.bus, usb.hub,
                       usb.port);
    }
    rt_mutex_release(usb.lock);
    return 0;
}
uint8_t usbh_get_hport_active_config_index(struct usbh_hubport *hport)
{
    return androidauto_usb_select_config(hport);
}
static int accessory_connect(struct usbh_hubport *hport, uint8_t intf)
{
    if (!hport || hport->bus->busid != RTANDROIDAUTO_USB_BUS || !usb.lock || usb.poisoned)
        return -USB_ERR_IO;
    rt_mutex_take(usb.lock, RT_WAITING_FOREVER);
    int rc = RTAA_ESTATE;
    if (usb.present || (usb.aoa.state == RTAA_AOA_REENUMERATING &&
                        (usb.bus != hport->bus->busid || usb.hub != hport->parent->index ||
                         usb.port != hport->port)))
        goto out;
    rc = rtaa_aoa_bind(&usb.aoa, hport->device_desc.idVendor, hport->device_desc.idProduct,
                       hport->raw_config_desc, hport->config.config_desc.wTotalLength,
                       clock_ms(NULL));
    if (rc)
        goto out;
    rc = RTAA_ESTATE;
    if (usb.aoa.interface_number !=
        hport->config.intf[intf].altsetting[0].intf_desc.bInterfaceNumber)
        goto out;
    struct usbh_interface_altsetting *alt = &hport->config.intf[intf].altsetting[0];
    if (alt->intf_desc.bNumEndpoints > CONFIG_USBHOST_MAX_ENDPOINTS)
        goto out;
    for (unsigned i = 0; i < alt->intf_desc.bNumEndpoints; ++i)
    {
        struct usb_endpoint_descriptor *ep = &alt->ep[i].ep_desc;
        if (ep->bEndpointAddress == usb.aoa.endpoint_in)
            usb.in->endpoint = *ep;
        if (ep->bEndpointAddress == usb.aoa.endpoint_out)
            usb.out->endpoint = *ep;
    }
    usb.in->toggle = usb.out->toggle = 0;
    usb.hport = hport;
    usb.claimed = false;
    ++usb.generation;
    usb.present = true;
    rc = RTAA_OK;
    rt_kprintf("androidauto: accessory bulk endpoints %02x/%02x attached\n", usb.aoa.endpoint_in,
               usb.aoa.endpoint_out);
out:
    rt_mutex_release(usb.lock);
    return rc ? -USB_ERR_NOTSUPP : 0;
}
static int accessory_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    (void)intf;
    rt_mutex_take(usb.lock, RT_WAITING_FOREVER);
    if (usb.hport == hport)
    {
        usb.present = usb.claimed = false;
        usb.hport = NULL;
        ++usb.generation;
        rtaa_aoa_detach(&usb.aoa);
    }
    rt_mutex_release(usb.lock);
    return 0;
}
static const uint16_t ids[][2] = {
    {0x18d1, 0x2d00}, {0x18d1, 0x2d01}, {0x18d1, 0x2d04}, {0x18d1, 0x2d05}, {0, 0}};
static const struct usbh_class_driver driver = {"androidauto", "Android AOA", accessory_connect,
                                                accessory_disconnect};
CLASS_INFO_DEFINE const struct usbh_class_info androidauto_class = {
    .match_flags = USB_CLASS_MATCH_VID_PID | USB_CLASS_MATCH_INTF_CLASS |
                   USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = 0xff,
    .bInterfaceSubClass = 0xff,
    .bInterfaceProtocol = 0,
    .id_table = ids,
    .class_driver = &driver};
static int bulk(void *ctx, uint8_t *data, size_t size, uint32_t timeout, bool input)
{
    (void)ctx;
    uint64_t begin = clock_ms(NULL);
    if (rt_mutex_take(usb.lock, rt_tick_from_millisecond(timeout)) != RT_EOK)
        return RTAA_ETIMEOUT;
    uint64_t elapsed = clock_ms(NULL) - begin;
    timeout = elapsed < timeout ? timeout - (uint32_t)elapsed : 0;
    int rc = usb.present && usb.claimed && !usb.poisoned
                 ? transact(input ? usb.in : usb.out, usb.hport, false, input, data, size, timeout)
                 : RTAA_ECANCEL;
    rt_mutex_release(usb.lock);
    return input && rc == RTAA_ETIMEOUT ? 0 : rc;
}
static int read_bulk(void *ctx, uint8_t *data, size_t size, uint32_t timeout)
{
    return bulk(ctx, data, size, timeout, true);
}
static int write_bulk(void *ctx, const uint8_t *data, size_t size, uint32_t timeout)
{
    return bulk(ctx, (uint8_t *)data, size, timeout, false);
}
static void close_bulk(void *ctx)
{
    (void)ctx;
    usb.claimed = false; /* worker owns all I/O; none is pending outside a call */
}
int androidauto_usb_open(rtaa_transport *out, uint32_t *generation)
{
    if (!out || !generation || !usb.lock)
        return RTAA_EINVAL;
    rt_mutex_take(usb.lock, RT_WAITING_FOREVER);
    int rc = RTAA_AGAIN;
    if (usb.poisoned)
        rc = RTAA_EIO;
    else if (usb.present && !usb.claimed)
    {
        usb.claimed = true;
        *generation = usb.generation;
        *out = (rtaa_transport){NULL, read_bulk, write_bulk, clock_ms, close_bulk, close_bulk};
        rc = RTAA_OK;
    }
    rt_mutex_release(usb.lock);
    return rc;
}
uint32_t androidauto_usb_generation(void)
{
    return usb.generation;
}
bool androidauto_usb_present(void)
{
    return usb.present;
}
