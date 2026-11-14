/* SPDX-License-Identifier: Apache-2.0 */
#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_luban_baremetal.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

struct usbh_bus g_usbhost_bus[CONFIG_USBHOST_MAX_BUS];
static uint64_t now;
static unsigned int allocations, locked, enum_count, releases, init_count, deinit_count;
static unsigned int event_init, event_deinit;
static int allocation_budget = -1, hcd_error;
static bool connected, changed;
void *aicos_malloc(unsigned int type, size_t size)
{
    (void)type;
    if (allocation_budget == 0) return NULL;
    if (allocation_budget > 0) --allocation_budget;
    void *p = malloc(size); if (p) ++allocations; return p;
}
void aicos_free(unsigned int type, void *p)
{ (void)type; if (p) { assert(allocations); --allocations; free(p); } }
size_t aicos_enter_critical_section(void) { return locked++; }
void aicos_leave_critical_section(size_t flags) { assert(locked == flags + 1); locked = flags; }
uint64_t aic_get_time_us(void) { assert(!locked); now += 100; return now; }
void aicos_msleep(uint32_t ms) { now += (uint64_t)ms * 1000; }
int usb_hc_init(struct usbh_bus *bus) { (void)bus; ++init_count; return hcd_error; }
int usb_hc_deinit(struct usbh_bus *bus) { (void)bus; ++deinit_count; return 0; }
int usbh_roothub_control(struct usbh_bus *bus, struct usb_setup_packet *setup, uint8_t *buffer)
{
    (void)bus;
    if (setup->bRequest == USB_REQUEST_GET_STATUS) {
        struct hub_port_status status = {
            .wPortStatus = HUB_PORT_STATUS_POWER | (connected ?
                HUB_PORT_STATUS_CONNECTION | HUB_PORT_STATUS_ENABLE | HUB_PORT_STATUS_HIGH_SPEED : 0),
            .wPortChange = changed ? HUB_PORT_STATUS_C_CONNECTION : 0,
        };
        memcpy(buffer, &status, sizeof(status));
    } else if (setup->bRequest == USB_REQUEST_CLEAR_FEATURE &&
               setup->wValue == HUB_PORT_FEATURE_C_CONNECTION) changed = false;
    return 0;
}
int usbh_enumerate(struct usbh_hubport *port)
{ assert(port->connected && port->speed == USB_SPEED_HIGH && port->mutex); ++enum_count; return 0; }
void usbh_hubport_release(struct usbh_hubport *port)
{
    if (port->mutex) { usb_osal_mutex_delete(port->mutex); port->mutex = NULL; }
    if (port->connected) ++releases;
    port->connected = false;
}
int usbh_control_transfer(struct usbh_hubport *p, struct usb_setup_packet *s, uint8_t *b)
{ (void)p; (void)s; (void)b; assert(!"unexpected external-hub control"); return -1; }
int usbh_submit_urb(struct usbh_urb *urb) { (void)urb; return 0; }
int usbh_kill_urb(struct usbh_urb *urb) { (void)urb; return 0; }
static void event(uint8_t b, uint8_t h, uint8_t p, uint8_t i, uint8_t e)
{
    (void)b; (void)h; (void)p; (void)i;
    if (e == USBH_EVENT_INIT) ++event_init;
    if (e == USBH_EVENT_DEINIT) ++event_deinit;
}
static void reset_bus(void)
{ memset(g_usbhost_bus, 0, sizeof(g_usbhost_bus)); g_usbhost_bus[0].event_handler = event; }
int main(void)
{
    struct usbh_bus *bus = &g_usbhost_bus[0];
    reset_bus();
    allocation_budget = 0;
    assert(usbh_hub_initialize(bus) < 0 && !bus->hub_mq && !allocations);
    allocation_budget = 1;
    assert(usbh_hub_initialize(bus) < 0 && !bus->hub_mq && !bus->hub_sem && !allocations);
    allocation_budget = -1; hcd_error = -1;
    assert(usbh_hub_initialize(bus) < 0 && !bus->hub_mq && !bus->hub_sem && !allocations);
    assert(init_count == 1 && deinit_count == 1);
    hcd_error = 0;
    assert(!usbh_hub_initialize(bus) && bus->hub_mq && bus->hub_sem && !bus->hub_thread);
    struct usbh_hub *hub = &bus->hcd.roothub;
    hub->int_buffer[0] = 2;
    connected = changed = true;
    usbh_hub_thread_wakeup(hub);
    assert(usbh_hub_connect_check() == 1 && enum_count == 1);
    usbh_hub_poll(); assert(enum_count == 1);
    connected = false; changed = true;
    usbh_hub_thread_wakeup(hub); usbh_hub_poll();
    assert(!hub->child[0].connected && releases == 1);
    connected = changed = true;
    usbh_hub_thread_wakeup(hub); usbh_hub_poll();
    assert(enum_count == 2);
    assert(!usbh_hub_deinitialize(bus));
    assert(!bus->hub_mq && !bus->hub_sem && !allocations && !locked);
    assert(releases == 2 && event_init == 2 && event_deinit == 1);
    usbh_hub_poll();
    assert(usbh_hub_deinitialize(bus) < 0);
    puts("Baremetal Hub: allocation/HCD rollback, real connect/disconnect/reconnect polling and orderly teardown passed");
    return 0;
}
