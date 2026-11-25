/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: Xiong Hao <hao.xiong@artinchip.com>
 */

#include <aic_core.h>
#include <aic_utils.h>
#include <aicupg.h>
#include <usbd_core.h>
#include "usbd_aicupg.h"
#if defined(LPKG_CHERRYUSB_OTG) && !defined(AIC_BOOTLOADER)
#include "usb_otg.h"
extern struct usb_otg g_usb_otg;
#endif

#define USBD_VID           0x33C3
#define USBD_PID           0x6677

/*!< endpoint address */
#define BULK_OUT_EP  0x02
#define BULK_IN_EP   0x81

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + 9 + 7 + 7)

#ifdef CONFIG_USB_HS
#define BULK_MAX_MPS 512
#else
#define BULK_MAX_MPS 64
#endif

#define AICUPG_USB_DEVICE_DESCRIPTOR_INIT(bcdUSB, bDeviceClass,         \
                                   bDeviceSubClass, bDeviceProtocol,    \
                                   idVendor, idProduct, bcdDevice,      \
                                   bNumConfigurations)                  \
    0x12,                       /* bLength */                           \
    USB_DESCRIPTOR_TYPE_DEVICE, /* bDescriptorType */                   \
    WBVAL(bcdUSB),              /* bcdUSB */                            \
    bDeviceClass,               /* bDeviceClass */                      \
    bDeviceSubClass,            /* bDeviceSubClass */                   \
    bDeviceProtocol,            /* bDeviceProtocol */                   \
    0x40,                       /* bMaxPacketSize */                    \
    WBVAL(idVendor),            /* idVendor */                          \
    WBVAL(idProduct),           /* idProduct */                         \
    WBVAL(bcdDevice),           /* bcdDevice */                         \
    USB_STRING_MFC_INDEX,       /* iManufacturer */                     \
    USB_STRING_PRODUCT_INDEX,   /* iProduct */                          \
    0,    /* iSerial */                           \
    bNumConfigurations          /* bNumConfigurations */

/*!< global descriptor */
static const uint8_t usb_upg_descriptor[] = {
    AICUPG_USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, USBD_VID, USBD_PID, 0x101, 1),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 1, 1, USB_CONFIG_BUS_POWERED, 0xFA),
    USB_INTERFACE_DESCRIPTOR_INIT(0, 0, 2, 0xFF, 0xFF, 0xFF, 0),
    USB_ENDPOINT_DESCRIPTOR_INIT(BULK_IN_EP, USB_ENDPOINT_TYPE_BULK, BULK_MAX_MPS, 0),
    USB_ENDPOINT_DESCRIPTOR_INIT(BULK_OUT_EP, USB_ENDPOINT_TYPE_BULK, BULK_MAX_MPS, 0),
    ///////////////////////////////////////
    /// string0 descriptor
    ///////////////////////////////////////
    USB_LANGID_INIT(0x0409),
    ///////////////////////////////////////
    /// string1 descriptor
    ///////////////////////////////////////
    0x16,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'A', 0x00,                  /* wcChar0 */
    'r', 0x00,                  /* wcChar1 */
    't', 0x00,                  /* wcChar2 */
    'i', 0x00,                  /* wcChar3 */
    'n', 0x00,                  /* wcChar4 */
    'c', 0x00,                  /* wcChar5 */
    'h', 0x00,                  /* wcChar6 */
    'i', 0x00,                  /* wcChar7 */
    'p', 0x00,                  /* wcChar8 */
    0x00, 0x00,                 /* wcChar9 */
    ///////////////////////////////////////
    /// string2 descriptor
    ///////////////////////////////////////
    0x24,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'A', 0x00,                  /* wcChar0 */
    'r', 0x00,                  /* wcChar1 */
    't', 0x00,                  /* wcChar2 */
    'i', 0x00,                  /* wcChar3 */
    'n', 0x00,                  /* wcChar4 */
    'c', 0x00,                  /* wcChar5 */
    'h', 0x00,                  /* wcChar6 */
    'i', 0x00,                  /* wcChar7 */
    'p', 0x00,                  /* wcChar8 */
    ' ', 0x00,                  /* wcChar9 */
    'D', 0x00,                  /* wcChar10 */
    'e', 0x00,                  /* wcChar11 */
    'v', 0x00,                  /* wcChar12 */
    'i', 0x00,                  /* wcChar13 */
    'c', 0x00,                  /* wcChar14 */
    'e', 0x00,                  /* wcChar15 */
    0x00, 0x00,                 /* wcChar16 */
    ///////////////////////////////////////
    /// string3 descriptor
    ///////////////////////////////////////
    0x16,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '2', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '2', 0x00,                  /* wcChar2 */
    '2', 0x00,                  /* wcChar3 */
    '1', 0x00,                  /* wcChar4 */
    '2', 0x00,                  /* wcChar5 */
    '3', 0x00,                  /* wcChar6 */
    '4', 0x00,                  /* wcChar7 */
    '5', 0x00,                  /* wcChar8 */
    '6', 0x00,                  /* wcChar9 */
#ifdef CONFIG_USB_HS
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0xFF,
    0xFF,
    0xFF,
    0x40,
    0x01,
    0x00,
#endif
};

static struct usbd_interface intf0;

static const uint8_t *device_descriptor(uint8_t speed)
{ (void)speed; return usb_upg_descriptor; }
static const uint8_t *config_descriptor(uint8_t speed)
{ (void)speed; return usb_upg_descriptor + USB_SIZEOF_DEVICE_DESC; }
static const uint8_t *qualifier_descriptor(uint8_t speed)
{
    (void)speed;
#ifdef CONFIG_USB_HS
    return usb_upg_descriptor + sizeof(usb_upg_descriptor) - 10;
#else
    return NULL;
#endif
}
static const char *string_descriptor(uint8_t speed, uint8_t index)
{
    static const char langid[] = { 0x09, 0x04 };
    static const char *strings[] = { langid, "Artinchip", "Artinchip Device", "2022123456" };
    (void)speed;
    return index < sizeof(strings) / sizeof(strings[0]) ? strings[index] : NULL;
}
static const struct usb_descriptor descriptors = {
    .device_descriptor_callback = device_descriptor,
    .config_descriptor_callback = config_descriptor,
    .device_quality_descriptor_callback = qualifier_descriptor,
    .string_descriptor_callback = string_descriptor,
};
void usb_luban_device_event(uint8_t busid, uint8_t event)
{ (void)busid; (void)event; }

static bool registered;
static bool active;
int usb_init(void)
{
    if (active) return 0;
    if (!registered) {
        usbd_desc_register(0, &descriptors);
        usbd_add_interface(0, usbd_aicupg_init_intf(&intf0, BULK_OUT_EP, BULK_IN_EP));
        registered = true;
    }
#if defined(LPKG_CHERRYUSB_OTG) && !defined(AIC_BOOTLOADER)
    if (usb_otg_register_device(&intf0)) return 0;
#endif
    int ret = usbd_initialize(0, USB_DEV_BASE, usb_luban_device_event);
    active = ret == 0;
#if defined(LPKG_CHERRYUSB_OTG) && !defined(AIC_BOOTLOADER)
    if (!ret) g_usb_otg.device_on = 1;
#endif
    return ret;
}

int usb_deinit(void)
{
    if (active) usbd_deinitialize(0);
    active = false;
    return 0;
}
