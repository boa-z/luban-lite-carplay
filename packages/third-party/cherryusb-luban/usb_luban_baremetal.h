/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_LUBAN_BAREMETAL_H
#define USB_LUBAN_BAREMETAL_H
#include <stdint.h>
#include <stdbool.h>
int usbh_init(void);
void usbh_hub_poll(void);
int usbh_hub_connect_check(void);
bool usbd_connect_check(uint32_t timeout);
void usb_osal_poll_timers(void);
#endif
