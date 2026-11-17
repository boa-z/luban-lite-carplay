/* SPDX-License-Identifier: GPL-3.0-only */
#include "androidauto_wireless.h"
#include <rtthread.h>
#ifdef RTANDROIDAUTO_WIRELESS
#include "rtaa/wireless.h"
#include <wlan_mgnt.h>
#include <wifi_conf.h>
#include <lwip/sockets.h>
#include <lwip/netif.h>
#include <netif/ethernetif.h>
#include <dhcp_server.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int listener = -1, client = -1;
static bool ap_owned, starting, bootstrapping, credentials_sent, stopped;
static uint32_t started, generation;
static rtaa_wireless bootstrap;
static char ssid[33], secret[65], bssid[18], address[16];
static unsigned channel;
static void erase(void)
{
    volatile char *p = secret;
    for (size_t i = 0; i < sizeof(secret); ++i)
        p[i] = 0;
}
static int line(FILE *file, char *p, size_t n)
{
    if (!fgets(p, (int)n, file))
        return RTAA_EINVAL;
    size_t len = strlen(p);
    if (len && p[len - 1] == '\n')
        p[--len] = 0;
    else if (!feof(file))
        return RTAA_EBOUNDS;
    if (len && p[len - 1] == '\r')
        p[--len] = 0;
    return len ? 0 : RTAA_EINVAL;
}
static int provision(void)
{
    char path[192], s[36], key[68], c[16];
    snprintf(path, sizeof(path), "%s/wireless.conf", RTANDROIDAUTO_CREDENTIAL_DIR);
    FILE *f = fopen(path, "rb");
    if (!f)
        return RTAA_ESECURITY;
    int rc = line(f, s, sizeof(s));
    if (!rc)
        rc = line(f, key, sizeof(key));
    if (!rc)
        rc = line(f, c, sizeof(c));
    if (!rc && (fgetc(f) != EOF || ferror(f)))
        rc = RTAA_EINVAL;
    fclose(f);
    if (!rc)
    {
        char *end;
        unsigned long value = strtoul(c, &end, 10);
        if (*end || (value != 36 && value != 40 && value != 44 && value != 48) || strlen(s) > 32 ||
            strlen(key) < 8 || strlen(key) > 63)
            rc = RTAA_EINVAL;
        else
        {
            for (size_t i = 0; i < strlen(key); ++i)
                if ((unsigned char)key[i] < 32 || (unsigned char)key[i] > 126)
                    rc = RTAA_EINVAL;
            if (!rc)
            {
                strcpy(ssid, s);
                strcpy(secret, key);
                channel = (unsigned)value;
            }
        }
    }
    volatile char *p = key;
    for (size_t i = 0; i < sizeof(key); ++i)
        p[i] = 0;
    return rc;
}
static struct netif *ap_netif(void)
{
    rt_device_t ap = rt_device_find(RT_WLAN_DEVICE_AP_NAME);
    for (struct netif *n = netif_list; n; n = n->next)
    {
        struct eth_device *eth = n->state;
        if (n->name[0] == 'w' && ap && eth && eth->parent.user_data == ap)
            return n;
    }
    return NULL;
}
static int nonblocking(int fd)
{
    unsigned long on = 1;
    return lwip_ioctl(fd, FIONBIO, &on) ? RTAA_EIO : 0;
}
static int setup(void)
{
    int rc = provision();
    if (rc)
        return rc;
    if (rt_wlan_ap_is_active())
        return RTAA_ESTATE;
    rt_wlan_config_autoreconnect(RT_FALSE);
    if (rt_wlan_set_mode(RT_WLAN_DEVICE_AP_NAME, RT_WLAN_AP) != RT_EOK)
        return RTAA_EIO;
    struct rt_wlan_info info = {0};
    info.security = SECURITY_WPA2_AES_PSK;
    info.band = RT_802_11_BAND_5GHZ;
    info.channel = (rt_int16_t)channel;
    info.ssid.len = (rt_uint8_t)strlen(ssid);
    memcpy(info.ssid.val, ssid, info.ssid.len);
    /* Own the attempted AP until stop also on partial driver failure. */
    ap_owned = true;
    if (rt_wlan_start_ap_adv(&info, secret) != RT_EOK)
        return RTAA_EIO;
    started = rt_tick_get_millisecond();
    starting = true;
    stopped = false;
    return 0;
}
static int listen_ap(void)
{
    struct netif *n = ap_netif();
    if (!n)
        return RTAA_AGAIN;
    char name[3] = {n->name[0], n->name[1], 0};
    dhcpd_start(name);
    if (!netif_ip4_addr(n)->addr)
        return RTAA_AGAIN;
    if (!ip4addr_ntoa_r(netif_ip4_addr(n), address, sizeof(address)))
        return RTAA_EIO;
    /* The AP netif MAC is the BSSID. Never substitute the STA interface MAC. */
    if (n->hwaddr_len != 6)
        return RTAA_ESECURITY;
    snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", n->hwaddr[0], n->hwaddr[1],
             n->hwaddr[2], n->hwaddr[3], n->hwaddr[4], n->hwaddr[5]);
    listener = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0)
        return RTAA_EIO;
    int reuse = 1;
    if (lwip_setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)))
        return RTAA_EIO;
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(5288);
    local.sin_addr.s_addr = netif_ip4_addr(n)->addr;
    if (nonblocking(listener) || lwip_bind(listener, (struct sockaddr *)&local, sizeof(local)) ||
        lwip_listen(listener, 1))
        return RTAA_EIO;
    int rc = androidauto_bt_start();
    if (rc)
        return rc;
    rt_kprintf("AA wireless AP ready: channel=%u TCP=%s:5288; awaiting Bluetooth\n", channel,
               address);
    return 0;
}
static int wait_socket(bool write, uint32_t timeout)
{
    if (stopped)
        return RTAA_ECANCEL;
    if (client < 0)
        return RTAA_EIO;
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(client, &ready);
    struct timeval limit = {(long)(timeout / 1000), (long)(timeout % 1000) * 1000};
    int rc = lwip_select(client + 1, write ? NULL : &ready, write ? &ready : NULL, NULL, &limit);
    return rc < 0 ? RTAA_EIO : rc;
}
static int tcp_read(void *ctx, uint8_t *p, size_t n, uint32_t timeout)
{
    (void)ctx;
    int rc = wait_socket(false, timeout);
    if (rc <= 0)
        return rc;
    rc = lwip_recv(client, p, n, 0);
    if (rc > 0)
        return rc;
    return rc < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : RTAA_EIO;
}
static int tcp_write(void *ctx, const uint8_t *p, size_t n, uint32_t timeout)
{
    (void)ctx;
    int rc = wait_socket(true, timeout);
    if (rc <= 0)
        return rc;
    rc = lwip_send(client, p, n, 0);
    return rc < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : rc <= 0 ? RTAA_EIO : rc;
}
static uint64_t clock_ms(void *ctx)
{
    (void)ctx;
    static uint32_t previous;
    static uint64_t elapsed;
    uint32_t now = rt_tick_get_millisecond();
    elapsed += (uint32_t)(now - previous);
    previous = now;
    return elapsed;
}
static void tcp_cancel(void *ctx)
{
    (void)ctx;
    stopped = true;
    if (client >= 0)
        lwip_shutdown(client, SHUT_RDWR);
}
static void tcp_close(void *ctx)
{
    (void)ctx;
    androidauto_wireless_stop();
}
static void peer_status(void *ctx, uint16_t message, bool has, int32_t status, const char *text)
{
    (void)ctx;
    (void)text;
    rt_kprintf("AA wireless peer message=%u has_status=%u raw_status=%ld\n", message, has,
               (long)status);
}
int androidauto_wireless_open(rtaa_transport *out, uint32_t *out_generation)
{
    androidauto_bt_poll();
    if (client >= 0)
        return RTAA_ESTATE;
    int rc = 0;
    if (!starting && listener < 0)
    {
        if (!androidauto_bt_retired())
            return RTAA_AGAIN;
        rc = setup();
        if (rc)
            goto failed;
    }
    if ((uint32_t)(rt_tick_get_millisecond() - started) >= 60000)
    {
        rc = RTAA_ETIMEOUT;
        goto failed;
    }
    if (starting)
    {
        if (!rt_wlan_ap_is_active())
        {
            if ((uint32_t)(rt_tick_get_millisecond() - started) < 15000)
                return RTAA_AGAIN;
            rc = RTAA_ETIMEOUT;
            goto failed;
        }
        rc = listen_ap();
        if (rc == RTAA_AGAIN)
            return rc;
        if (rc)
            goto failed;
        starting = false;
    }
    if (!bootstrapping && !credentials_sent)
    {
        rtaa_transport bt;
        rc = androidauto_bt_open(&bt);
        if (rc == RTAA_AGAIN)
            return rc;
        if (rc)
            goto failed;
        rtaa_wireless_config c = {ssid, secret, bssid, address, 5288, true, 45000};
        rc = rtaa_wireless_begin(&bootstrap, &bt, &c, peer_status, NULL);
        if (rc)
        {
            bt.close(bt.ctx);
            goto failed;
        }
        bootstrapping = true;
        erase();
    }
    if (bootstrapping)
    {
        rc = rtaa_wireless_poll(&bootstrap, 5);
        if (bootstrap.state == RTAA_WIRELESS_WAIT_TCP)
            credentials_sent = true;
        /* Some peers retire RFCOMM after receiving credentials. Keep the AP
         * and TCP deadline in that case; rejection/protocol/security errors
         * still terminate. The TCP peer must pass the pinned AAP/TLS policy. */
        if (rc == RTAA_EIO && credentials_sent)
        {
            rtaa_wireless_close(&bootstrap);
            bootstrapping = false;
            androidauto_bt_stop();
        }
        else if (rc < 0)
            goto failed;
    }
    if (!credentials_sent)
        return RTAA_AGAIN;
    struct sockaddr_in remote;
    socklen_t len = sizeof(remote);
    client = lwip_accept(listener, (struct sockaddr *)&remote, &len);
    if (client < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return RTAA_AGAIN;
        rc = RTAA_EIO;
        goto failed;
    }
    if (nonblocking(client))
    {
        rc = RTAA_EIO;
        goto failed;
    }
    int on = 1;
    if (lwip_setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)))
    {
        rc = RTAA_EIO;
        goto failed;
    }
    /* Stop Bluetooth setup only after accepting a real TCP stream. Existing
     * receiver TLS pin verification still gates projection and all media. */
    if (bootstrapping)
        rtaa_wireless_close(&bootstrap);
    bootstrapping = false;
    androidauto_bt_stop();
    lwip_close(listener);
    listener = -1;
    if (++generation == 0)
        ++generation;
    *out_generation = generation;
    *out = (rtaa_transport){NULL, tcp_read, tcp_write, clock_ms, tcp_cancel, tcp_close};
    rt_kprintf("AA wireless TCP accepted; starting pinned AAP/TLS\n");
    return 0;
failed:
    androidauto_wireless_stop();
    return rc;
}
void androidauto_wireless_stop(void)
{
    if (bootstrapping)
    {
        rtaa_wireless_close(&bootstrap);
        bootstrapping = false;
    }
    androidauto_bt_stop();
    if (client >= 0)
    {
        lwip_close(client);
        client = -1;
    }
    if (listener >= 0)
    {
        lwip_close(listener);
        listener = -1;
    }
    if (ap_owned)
    {
        if (rt_wlan_ap_stop() == RT_EOK || !rt_wlan_ap_is_active())
            ap_owned = false;
    }
    starting = false;
    stopped = true;
    credentials_sent = false;
    erase();
}
bool androidauto_wireless_retired(void)
{
    return !ap_owned && !starting && !bootstrapping && client < 0 && listener < 0 &&
           androidauto_bt_retired();
}
#else
int androidauto_wireless_open(rtaa_transport *out, uint32_t *generation)
{
    (void)out;
    (void)generation;
    return RTAA_ENOTSUP;
}
void androidauto_wireless_stop(void) {}
bool androidauto_wireless_retired(void)
{
    return true;
}
#endif
