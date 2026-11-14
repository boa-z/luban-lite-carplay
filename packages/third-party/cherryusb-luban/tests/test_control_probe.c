/* SPDX-License-Identifier: Apache-2.0 */
/* Compile the actual generated EP0 function. Inject lock/URB elapsed time to
 * verify that retries stay inside hold time and the hook runs after unlock. */
#include "usbh_core.h"
#include <assert.h>
#include <stdio.h>
static uint32_t clock_ms, wait_ms = 200, io_ms = 7, got_wait, got_hold;
static unsigned locked, reports, submissions;
static int failure;
uint32_t usbh_control_probe_now_ms(void) { return clock_ms; }
void usbh_control_probe_record(struct usbh_hubport *h, uint8_t request,
                               uint32_t wait, uint32_t hold)
{
    assert(h && request == 0x45 && !locked);
    got_wait = wait; got_hold = hold; ++reports;
}
int usb_osal_mutex_take(usb_osal_mutex_t m)
{ assert(m && !locked); clock_ms += wait_ms; locked = 1; return 0; }
int usb_osal_mutex_give(usb_osal_mutex_t m)
{ assert(m && locked); locked = 0; return 0; }
int usbh_submit_urb(struct usbh_urb *u)
{
    assert(locked); clock_ms += io_ms; ++submissions;
    u->actual_length = 9; return failure;
}
int main(void)
{
    struct usbh_hubport h = {0}; struct usb_setup_packet s = {0};
    h.connected = true; h.mutex = &h; s.bRequest = 0x45; s.wLength = 64;
    assert(usbh_control_transfer(&h, &s, NULL) == 1);
    assert(reports == 1 && got_wait == 200 && got_hold == 7 && submissions == 1);
    failure = -USB_ERR_STALL;
    assert(usbh_control_transfer(&h, &s, NULL) == -USB_ERR_STALL);
    assert(reports == 2 && got_wait == 200 && got_hold == 21 && submissions == 4);
    failure = -USB_ERR_TIMEOUT;
    assert(usbh_control_transfer(&h, &s, NULL) == -USB_ERR_TIMEOUT);
    assert(reports == 3 && got_hold == 7 && submissions == 5);
    /* Unsigned elapsed clock wrap must not appear as a multi-day wait. */
    clock_ms = UINT32_MAX - 100; failure = 0;
    assert(usbh_control_transfer(&h, &s, NULL) == 1);
    assert(got_wait == 200 && got_hold == 7 && !locked);
    puts("EP0 probe: shared-lock wait vs hold/retry, no changed return values, clock wrap passed");
    return 0;
}
