/* SPDX-License-Identifier: GPL-3.0-only */
/* Production AP/bootstrap/TCP lifecycle with POSIX sockets and explicit mocked
 * WLAN/BT APIs. Does not validate radio, pairing, DHCP or phone behavior. */
#define RTANDROIDAUTO_WIRELESS 1
#define RTANDROIDAUTO_CREDENTIAL_DIR "/tmp/rtaa-wireless-host"
#include "../backends/androidauto/platform/androidauto_wireless.c"
#include <assert.h>
#include <ifaddrs.h>
#include <sys/stat.h>
static uint32_t ticks;
static bool ap_active, bt_enabled, bt_claimed, ap_fail, ap_stop_fail, bt_lost;
static unsigned ap_starts, ap_stops, bt_closes;
static int device;
static struct eth_device ethernet;
static struct netif test_netif;
struct netif *netif_list = &test_netif;
uint32_t rt_tick_get_millisecond(void)
{
    return ticks;
}
int rt_kprintf(const char *p, ...)
{
    (void)p;
    return 0;
}
rt_device_t rt_device_find(const char *name)
{
    assert(!strcmp(name, "ap0"));
    return &device;
}
bool rt_wlan_ap_is_active(void)
{
    return ap_active;
}
void rt_wlan_config_autoreconnect(int on)
{
    assert(!on);
}
int rt_wlan_set_mode(const char *name, int mode)
{
    assert(!strcmp(name, "ap0") && mode == RT_WLAN_AP);
    return 0;
}
int rt_wlan_start_ap_adv(const struct rt_wlan_info *info, const char *key)
{
    assert(info->band == 5 && info->channel == 36 && info->security == 8);
    assert(info->ssid.len == 7 && !memcmp(info->ssid.val, "AA-test", 7));
    assert(!strcmp(key, "12345678"));
    ++ap_starts;
    ap_active = true;
    return ap_fail ? -1 : 0;
}
int rt_wlan_ap_stop(void)
{
    ++ap_stops;
    if (ap_stop_fail)
        return -1;
    ap_active = false;
    return 0;
}
void dhcpd_start(const char *name)
{
    assert(!strcmp(name, "wl"));
}
char *ip4addr_ntoa_r(const ip4_addr_t *ip, char *p, int n)
{
    return (char *)inet_ntop(AF_INET, &ip->addr, p, (socklen_t)n);
}
static uint8_t bt_output[2048];
static size_t bt_sent, bt_read_pos;
static int bt_read_host(void *ctx, uint8_t *p, size_t n, uint32_t timeout)
{
    (void)ctx;
    (void)timeout;
    if (bt_lost)
        return RTAA_EIO;
    static const uint8_t request[] = {0, 0, 0, 2};
    if (n > sizeof(request) - bt_read_pos)
        n = sizeof(request) - bt_read_pos;
    memcpy(p, request + bt_read_pos, n);
    bt_read_pos += n;
    return (int)n;
}
static int bt_write_host(void *ctx, const uint8_t *p, size_t n, uint32_t timeout)
{
    (void)ctx;
    (void)timeout;
    if (n > 3)
        n = 3;
    assert(bt_sent + n <= sizeof(bt_output));
    memcpy(bt_output + bt_sent, p, n);
    bt_sent += n;
    return (int)n;
}
static void noop(void *ctx)
{
    (void)ctx;
}
static void close_bt(void *ctx)
{
    (void)ctx;
    ++bt_closes;
    bt_claimed = false;
}
int androidauto_bt_start(void)
{
    bt_enabled = true;
    bt_sent = bt_read_pos = 0;
    return 0;
}
void androidauto_bt_stop(void)
{
    bt_enabled = false;
}
bool androidauto_bt_retired(void)
{
    return !bt_enabled && !bt_claimed;
}
void androidauto_bt_poll(void) {}
int androidauto_bt_open(rtaa_transport *out)
{
    if (!bt_enabled || bt_claimed)
        return RTAA_AGAIN;
    bt_claimed = true;
    *out = (rtaa_transport){NULL, bt_read_host, bt_write_host, clock_ms, noop, close_bt};
    return 0;
}
static void provision_file(const char *text)
{
    (void)mkdir(RTANDROIDAUTO_CREDENTIAL_DIR, 0700);
    FILE *f = fopen(RTANDROIDAUTO_CREDENTIAL_DIR "/wireless.conf", "wb");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(!fclose(f));
}
int main(void)
{
    struct ifaddrs *interfaces = NULL;
    assert(!getifaddrs(&interfaces));
    uint32_t host_ip = 0;
    for (struct ifaddrs *i = interfaces; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET)
        {
            uint32_t ip = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
            if ((ntohl(ip) >> 24) != 127 && ip)
            {
                host_ip = ip;
                break;
            }
        }
    freeifaddrs(interfaces);
    assert(host_ip);
    ethernet.parent.user_data = &device;
    test_netif = (struct netif){.name = {'w', 'l'},
                                .state = &ethernet,
                                .ip = {host_ip},
                                .hwaddr_len = 6,
                                .hwaddr = {2, 17, 34, 51, 68, 85}};
    provision_file("AA-test\n12345678\n36\n");
    assert(androidauto_wireless_retired());
    rtaa_transport t = {0};
    uint32_t g = 0;
    for (unsigned i = 0; i < 100 && bootstrap.state != RTAA_WIRELESS_WAIT_TCP; ++i)
        assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    assert(bootstrap.state == RTAA_WIRELESS_WAIT_TCP && listener >= 0);
    assert(bt_sent > 40 && !strstr(secret, "12345678"));
    int phone = socket(AF_INET, SOCK_STREAM, 0);
    assert(phone >= 0);
    struct sockaddr_in target = {
        .sin_family = AF_INET, .sin_port = htons(5288), .sin_addr = {host_ip}};
    assert(!connect(phone, (struct sockaddr *)&target, sizeof(target)));
    assert(!androidauto_wireless_open(&t, &g));
    assert(g && client >= 0 && !bt_enabled);
    assert(!androidauto_wireless_retired());
    uint8_t bytes[32768], got[32768];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (uint8_t)i;
    assert(send(phone, bytes, sizeof(bytes), 0) == sizeof(bytes));
    size_t used = 0;
    while (used < sizeof(got))
    {
        int n = t.read(t.ctx, got + used, sizeof(got) - used, 100);
        assert(n > 0);
        used += (size_t)n;
    }
    assert(!memcmp(bytes, got, sizeof(bytes)));
    assert(!rtaa_write_all(&t, bytes, sizeof(bytes), 100));
    used = 0;
    while (used < sizeof(got))
    {
        int n = (int)recv(phone, got + used, sizeof(got) - used, 0);
        assert(n > 0);
        used += (size_t)n;
    }
    assert(!memcmp(bytes, got, sizeof(bytes)));
    assert(t.read(t.ctx, got, 1, 1) == 0);
    t.cancel(t.ctx);
    assert(t.read(t.ctx, got, 1, 1) == RTAA_ECANCEL);
    t.close(t.ctx);
    close(phone);
    assert(androidauto_wireless_retired() && bt_closes == 1 && ap_starts == ap_stops);
    /* Stop during pending bootstrap must retire AP, listener and BT too. */
    assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    androidauto_wireless_stop();
    assert(androidauto_wireless_retired());
    /* Total deadline; no incoming TCP. */
    assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    ticks += 60000;
    assert(androidauto_wireless_open(&t, &g) == RTAA_ETIMEOUT);
    assert(androidauto_wireless_retired());
    /* A partially started AP is still owned and stopped on error. */
    ap_fail = true;
    assert(androidauto_wireless_open(&t, &g) == RTAA_EIO);
    assert(androidauto_wireless_retired());
    ap_fail = false;
    provision_file("AA-test\nshort\n36\n");
    assert(androidauto_wireless_open(&t, &g) == RTAA_EINVAL);
    assert(androidauto_wireless_retired());
    provision_file("AA-test\n12345678\n36\n");
    assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    ap_stop_fail = true;
    androidauto_wireless_stop();
    assert(!androidauto_wireless_retired());
    ap_stop_fail = false;
    androidauto_wireless_stop();
    assert(androidauto_wireless_retired());
    /* A peer may close RFCOMM before initiating TCP. Credentials already
     * delivered remain sufficient to wait for the separately pinned session. */
    for (unsigned i = 0; i < 100 && !credentials_sent; ++i)
        assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    assert(credentials_sent);
    bt_lost = true;
    assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    assert(!bootstrapping && credentials_sent && listener >= 0);
    phone = socket(AF_INET, SOCK_STREAM, 0);
    assert(phone >= 0);
    assert(!connect(phone, (struct sockaddr *)&target, sizeof(target)));
    assert(!androidauto_wireless_open(&t, &g));
    t.close(t.ctx);
    close(phone);
    bt_lost = false;
    assert(androidauto_wireless_retired());
    /* Never stop or replace an AP owned by another application. */
    unsigned stops = ap_stops;
    ap_active = true;
    assert(androidauto_wireless_open(&t, &g) == RTAA_ESTATE);
    assert(ap_stops == stops && ap_active && androidauto_wireless_retired());
    ap_active = false;
    /* Both bootstrap and outer deadlines tolerate the RTOS tick wrap. */
    ticks = UINT32_MAX - 100;
    for (unsigned i = 0; i < 100 && !credentials_sent; ++i)
        assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    ticks = 20;
    assert(androidauto_wireless_open(&t, &g) == RTAA_AGAIN);
    androidauto_wireless_stop();
    assert(androidauto_wireless_retired());
    unlink(RTANDROIDAUTO_CREDENTIAL_DIR "/wireless.conf");
    puts("production wireless AP/bootstrap/TCP lifecycle: PASS");
    return 0;
}
