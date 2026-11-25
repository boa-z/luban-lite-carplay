/* SPDX-License-Identifier: Apache-2.0 */
#include "usbd_core.h"
#include "usb_luban_baremetal.h"
#include <assert.h>
#include <stdio.h>
int usb_init(void);
int usb_deinit(void);
static const struct usb_descriptor *registered;
static unsigned int registrations, interfaces, class_inits, starts, stops;
static int start_error;
static bool configured;
static uint64_t now;
uint64_t aic_get_time_us(void) { now += 100; return now; }
void usbd_desc_register(uint8_t bus, const struct usb_descriptor *desc)
{ assert(bus == 0 && desc); registered = desc; ++registrations; }
void usbd_add_interface(uint8_t bus, struct usbd_interface *intf)
{ assert(bus == 0 && intf); ++interfaces; }
struct usbd_interface *usbd_aicupg_init_intf(struct usbd_interface *intf, uint8_t out, uint8_t in)
{ assert(out == 2 && in == 0x81); ++class_inits; return intf; }
int usbd_initialize(uint8_t bus, uintptr_t base, usbd_event_handler_t event)
{ assert(bus == 0 && base == 0x10000000U && event); ++starts; event(bus, USBD_EVENT_INIT); return start_error; }
int usbd_deinitialize(uint8_t bus) { assert(bus == 0); ++stops; return 0; }
bool usb_device_is_configured(uint8_t bus) { assert(bus == 0); return configured; }
int main(void)
{
    start_error = -1; assert(usb_init() == -1);
    assert(registered && registrations == 1 && interfaces == 1 && class_inits == 1);
    const uint8_t *device = registered->device_descriptor_callback(USB_SPEED_HIGH);
    assert(device[0] == 18 && device[1] == USB_DESCRIPTOR_TYPE_DEVICE);
    assert(device[8] == 0xc3 && device[9] == 0x33 && device[10] == 0x77 && device[11] == 0x66);
    const uint8_t *config = registered->config_descriptor_callback(USB_SPEED_HIGH);
    assert(config[0] == 9 && config[1] == USB_DESCRIPTOR_TYPE_CONFIGURATION);
    assert(config[2] == 32 && config[4] == 1 && config[9 + 4] == 2);
    assert(registered->device_quality_descriptor_callback(USB_SPEED_HIGH)[1] == USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER);
    const char *lang = registered->string_descriptor_callback(USB_SPEED_HIGH, 0);
    assert(lang[0] == 9 && lang[1] == 4);
    assert(!strcmp(registered->string_descriptor_callback(USB_SPEED_HIGH, 1), "Artinchip"));
    assert(!registered->string_descriptor_callback(USB_SPEED_HIGH, 4));
    start_error = 0;
    assert(!usb_init() && !usb_init() && starts == 2);
    configured = true; assert(usbd_connect_check(0));
    configured = false; uint64_t start = now;
    assert(!usbd_connect_check(3) && now - start >= 3000);
    assert(!usb_deinit() && !usb_deinit() && stops == 1);
    assert(!usb_init() && starts == 3 && registrations == 1 && class_inits == 1);
    assert(!usb_deinit() && stops == 2);
    puts("Baremetal upgrade device: typed descriptors, non-null events, failure retry and idempotent restart passed");
    return 0;
}
