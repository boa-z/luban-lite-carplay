/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AA_WIRELESS_HOST_H
#define AA_WIRELESS_HOST_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int rt_int16_t;
typedef uint8_t rt_uint8_t;
typedef void *rt_device_t;
#define RT_EOK 0
#define RT_FALSE 0
#define RT_WLAN_AP 1
#define RT_WLAN_DEVICE_AP_NAME "ap0"
#define SECURITY_WPA2_AES_PSK 8
#define RT_802_11_BAND_5GHZ 5
struct rt_wlan_info
{
    int security, band, channel;
    struct
    {
        uint8_t len;
        char val[33];
    } ssid;
};
struct eth_device
{
    struct
    {
        void *user_data;
    } parent;
};
typedef struct
{
    uint32_t addr;
} ip4_addr_t;
struct netif
{
    struct netif *next;
    char name[2];
    void *state;
    ip4_addr_t ip;
    uint8_t hwaddr_len, hwaddr[6];
};
extern struct netif *netif_list;
#define netif_ip4_addr(n) (&(n)->ip)
uint32_t rt_tick_get_millisecond(void);
int rt_kprintf(const char *, ...);
rt_device_t rt_device_find(const char *);
bool rt_wlan_ap_is_active(void);
void rt_wlan_config_autoreconnect(int);
int rt_wlan_set_mode(const char *, int);
int rt_wlan_start_ap_adv(const struct rt_wlan_info *, const char *);
int rt_wlan_ap_stop(void);
void dhcpd_start(const char *);
char *ip4addr_ntoa_r(const ip4_addr_t *, char *, int);
#endif
