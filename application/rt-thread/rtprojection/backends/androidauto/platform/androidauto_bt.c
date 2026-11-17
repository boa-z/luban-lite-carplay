/* SPDX-License-Identifier: GPL-3.0-only */
#include "androidauto_wireless.h"
#include <rtthread.h>
#ifdef RTANDROIDAUTO_WIRELESS
#include <rthw.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>

#define MTU 1011
NET_BUF_POOL_FIXED_DEFINE(aa_bt_tx, 4, BT_RFCOMM_BUF_SIZE(MTU), LPKG_BT_CONN_TX_USER_DATA_SIZE,
                          NULL);
static struct bt_rfcomm_dlc link;
static struct bt_conn *peer, *pair;
static struct bt_rfcomm_server server;
static uint8_t ring[4096];
static size_t head, tail;
static bool initialized, registered, enabled, allocated, connected, handed, canceled, disconnecting;
static int link_error;
static unsigned pair_token, sequence, passkey, displayed;
static uint32_t pair_started;
static bool compare;

/* Canonical AA UUID reversed for this Zephyr SDP/HCI encoder. */
static uint8_t uuid[16] = {0x66, 0x9a, 0x0c, 0x20, 0x00, 0x08, 0xf4, 0xbd,
                           0xe6, 0x11, 0xcb, 0x52, 0x00, 0x7a, 0xe1, 0x4d};
static uint16_t l2cap = BT_SDP_PROTO_L2CAP, rfcomm = BT_SDP_PROTO_RFCOMM;
static struct bt_sdp_data_elem service_class, protocols[2], l2[1], rf[2];
static struct bt_sdp_attribute attrs[] = {
    BT_SDP_NEW_SERVICE, {0}, {0}, BT_SDP_SERVICE_NAME("Android Auto")};
static struct bt_sdp_record record = BT_SDP_RECORD(attrs);
static void elem(struct bt_sdp_data_elem *e, uint8_t type, uint32_t n, uint32_t total,
                 const void *p)
{
    *e = (struct bt_sdp_data_elem){.type = type, .data_size = n, .total_size = total, .data = p};
}
static void pairing_clear(struct bt_conn *conn)
{
    rt_base_t flags = rt_hw_interrupt_disable();
    struct bt_conn *old = pair == conn ? pair : NULL;
    if (old)
    {
        pair = NULL;
        pair_token = 0;
    }
    rt_hw_interrupt_enable(flags);
    if (old)
        bt_conn_unref(old);
}
static void request(struct bt_conn *conn, bool numeric, unsigned key)
{
    struct bt_conn *held = bt_conn_ref(conn);
    rt_base_t flags = rt_hw_interrupt_disable();
    bool reject = !enabled || pair;
    if (!reject)
    {
        pair = held;
        compare = numeric;
        passkey = key;
        if (++sequence == 0)
            ++sequence;
        pair_token = sequence;
        pair_started = rt_tick_get_millisecond();
    }
    rt_hw_interrupt_enable(flags);
    if (reject)
    {
        bt_conn_auth_cancel(conn);
        bt_conn_unref(held);
    }
}
static void numeric_compare(struct bt_conn *c, unsigned key)
{
    request(c, true, key);
}
static void consent(struct bt_conn *c)
{
    request(c, false, 0);
}
static void display(struct bt_conn *c, unsigned key)
{
    request(c, false, key);
}
static void paired(struct bt_conn *c, bool bonded)
{
    (void)bonded;
    pairing_clear(c);
}
static void pair_failed(struct bt_conn *c, enum bt_security_err err)
{
    (void)err;
    pairing_clear(c);
}
static const struct bt_conn_auth_cb auth = {.passkey_display = display,
                                            .passkey_confirm = numeric_compare,
                                            .pairing_confirm = consent,
                                            .cancel = pairing_clear};
static struct bt_conn_auth_info_cb auth_info = {.pairing_complete = paired,
                                                .pairing_failed = pair_failed};
static void acl_disconnected(struct bt_conn *c, uint8_t reason)
{
    (void)reason;
    pairing_clear(c);
}
static struct bt_conn_cb acl = {.disconnected = acl_disconnected};

static void dlc_connected(struct bt_rfcomm_dlc *dlc)
{
    (void)dlc;
    rt_base_t flags = rt_hw_interrupt_disable();
    /* Never send AP credentials on an unauthenticated (Just Works) link. */
    connected = enabled && peer && bt_conn_get_security(peer) >= BT_SECURITY_L3;
    if (!connected)
        link_error = RTAA_ESECURITY;
    rt_hw_interrupt_enable(flags);
}
static void dlc_disconnected(struct bt_rfcomm_dlc *dlc)
{
    (void)dlc;
    rt_base_t flags = rt_hw_interrupt_disable();
    struct bt_conn *old = peer;
    peer = NULL;
    connected = false;
    allocated = false;
    disconnecting = false;
    rt_hw_interrupt_enable(flags);
    if (old)
        bt_conn_unref(old);
}
static void dlc_recv(struct bt_rfcomm_dlc *dlc, struct net_buf *buf)
{
    (void)dlc;
    rt_base_t flags = rt_hw_interrupt_disable();
    if (connected && !canceled && !link_error)
    {
        size_t free = (tail + sizeof(ring) - head - 1) % sizeof(ring);
        if (buf->len > free)
            link_error = RTAA_EBOUNDS;
        else
            for (size_t i = 0; i < buf->len; ++i)
            {
                ring[head] = buf->data[i];
                head = (head + 1) % sizeof(ring);
            }
    }
    rt_hw_interrupt_enable(flags);
}
static void dlc_sent(struct bt_rfcomm_dlc *dlc, int err)
{
    (void)dlc;
    if (err)
    {
        rt_base_t f = rt_hw_interrupt_disable();
        link_error = RTAA_EIO;
        rt_hw_interrupt_enable(f);
    }
}
static struct bt_rfcomm_dlc_ops ops = {.connected = dlc_connected,
                                       .disconnected = dlc_disconnected,
                                       .recv = dlc_recv,
                                       .sent = dlc_sent};
static int accept_link(struct bt_conn *conn, struct bt_rfcomm_server *s, struct bt_rfcomm_dlc **out)
{
    (void)s;
    rt_base_t flags = rt_hw_interrupt_disable();
    if (!enabled || allocated || handed)
    {
        rt_hw_interrupt_enable(flags);
        return -EBUSY;
    }
    /* The host raises security before connected(). Keep the DLC allocated
     * until its disconnected callback, including security-failure paths. */
    memset(&link, 0, sizeof(link));
    link.ops = &ops;
    link.mtu = MTU;
    link.required_sec_level = BT_SECURITY_L3;
    peer = bt_conn_ref(conn);
    allocated = true;
    canceled = false;
    disconnecting = false;
    head = tail = 0;
    link_error = 0;
    *out = &link;
    rt_hw_interrupt_enable(flags);
    return 0;
}
static int bt_read(void *ctx, uint8_t *p, size_t n, uint32_t timeout)
{
    (void)ctx;
    (void)timeout;
    rt_base_t flags = rt_hw_interrupt_disable();
    int err = canceled ? RTAA_ECANCEL : link_error ? link_error : !connected ? RTAA_EIO : 0;
    size_t used = 0;
    if (!err)
        while (used < n && tail != head)
        {
            p[used++] = ring[tail];
            tail = (tail + 1) % sizeof(ring);
        }
    rt_hw_interrupt_enable(flags);
    return err ? err : (int)used;
}
static int bt_write(void *ctx, const uint8_t *p, size_t n, uint32_t timeout)
{
    (void)ctx;
    (void)timeout;
    rt_base_t flags = rt_hw_interrupt_disable();
    int err = canceled ? RTAA_ECANCEL : link_error ? link_error : !connected ? RTAA_EIO : 0;
    rt_hw_interrupt_enable(flags);
    if (err)
        return err;
    if (n > link.mtu)
        n = link.mtu;
    /* bt_rfcomm_create_pdu() allocates with K_FOREVER in this SDK. Reserve
     * the same public header space using a nonblocking pool allocation. */
    struct net_buf *buf = net_buf_alloc(&aa_bt_tx, K_NO_WAIT);
    if (!buf)
        return 0;
    net_buf_reserve(buf, BT_RFCOMM_BUF_SIZE(0) - BT_RFCOMM_FCS_SIZE);
    net_buf_add_mem(buf, p, n);
    err = bt_rfcomm_dlc_send(&link, buf);
    if (err >= 0)
        return (int)n;
    net_buf_unref(buf);
    return err == -ENOMEM || err == -EAGAIN ? 0 : RTAA_EIO;
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
static void bt_cancel(void *ctx)
{
    (void)ctx;
    rt_base_t f = rt_hw_interrupt_disable();
    canceled = true;
    rt_hw_interrupt_enable(f);
}
static void bt_close(void *ctx)
{
    bt_cancel(ctx);
    rt_base_t f = rt_hw_interrupt_disable();
    handed = false;
    bool active = allocated && !disconnecting;
    if (active)
        disconnecting = true;
    rt_hw_interrupt_enable(f);
    if (active)
        (void)bt_rfcomm_dlc_disconnect(&link);
}
int androidauto_bt_open(rtaa_transport *out)
{
    rt_base_t f = rt_hw_interrupt_disable();
    int rc = link_error ? link_error : !connected || handed ? RTAA_AGAIN : 0;
    if (!rc)
    {
        handed = true;
        *out = (rtaa_transport){NULL, bt_read, bt_write, clock_ms, bt_cancel, bt_close};
    }
    rt_hw_interrupt_enable(f);
    return rc;
}
int androidauto_bt_start(void)
{
    int rc;
    if (!initialized)
    {
        if ((rc = bt_conn_auth_cb_register(&auth)))
            return RTAA_EIO;
        if ((rc = bt_conn_auth_info_cb_register(&auth_info)))
            return RTAA_EIO;
        bt_conn_cb_register(&acl);
        initialized = true;
    }
    if ((rc = bt_enable(NULL)) && rc != -EALREADY)
        return RTAA_EIO;
    if (settings_subsys_init() || settings_load())
        return RTAA_EIO;
    if (!registered)
    {
        server.accept = accept_link;
        if (bt_rfcomm_server_register(&server))
            return RTAA_EIO;
        elem(&service_class, BT_SDP_UUID128, 16, 17, uuid);
        attrs[4].id = BT_SDP_ATTR_SVCLASS_ID_LIST;
        elem(&attrs[4].val, BT_SDP_SEQ8, 17, 19, &service_class);
        elem(&l2[0], BT_SDP_UUID16, 2, 3, &l2cap);
        elem(&rf[0], BT_SDP_UUID16, 2, 3, &rfcomm);
        elem(&rf[1], BT_SDP_UINT8, 1, 2, &server.channel);
        elem(&protocols[0], BT_SDP_SEQ8, 3, 5, l2);
        elem(&protocols[1], BT_SDP_SEQ8, 5, 7, rf);
        attrs[5].id = BT_SDP_ATTR_PROTO_DESC_LIST;
        elem(&attrs[5].val, BT_SDP_SEQ8, 12, 14, protocols);
        if (bt_sdp_register_service(&record))
        {
            bt_rfcomm_server_unregister(&server);
            return RTAA_EIO;
        }
        registered = true;
    }
    if (bt_set_name("Luban Android Auto"))
        return RTAA_EIO;
    /* Advertise only the AA UUID, without iAP2/CarPlay or an unimplemented HFP. */
    struct net_buf *cmd = bt_hci_cmd_alloc(K_MSEC(100));
    if (!cmd)
        return RTAA_EIO;
    uint8_t *eir = net_buf_add(cmd, 241);
    memset(eir, 0, 241);
    eir[1] = 17;
    eir[2] = 0x07;
    memcpy(eir + 3, uuid, 16);
    static const char name[] = "Luban Android Auto";
    eir[19] = sizeof(name);
    eir[20] = 0x09;
    memcpy(eir + 21, name, sizeof(name) - 1);
    if (bt_hci_cmd_send_sync(BT_OP(BT_OGF_BASEBAND, 0x0052), cmd, NULL))
        return RTAA_EIO;
    rt_base_t f = rt_hw_interrupt_disable();
    enabled = true;
    rt_hw_interrupt_enable(f);
    rc = bt_br_set_connectable(true, NULL);
    if (rc && rc != -EALREADY)
    {
        androidauto_bt_stop();
        return RTAA_EIO;
    }
    rc = bt_br_set_discoverable(true, false);
    if (rc && rc != -EALREADY)
    {
        androidauto_bt_stop();
        return RTAA_EIO;
    }
    rt_kprintf("AA Bluetooth RFCOMM channel %u; pairing: androidauto_pair status\n",
               server.channel);
    return 0;
}
void androidauto_bt_stop(void)
{
    rt_base_t f = rt_hw_interrupt_disable();
    enabled = false;
    struct bt_conn *pending = pair;
    pair = NULL;
    pair_token = 0;
    rt_hw_interrupt_enable(f);
    if (pending)
    {
        bt_conn_auth_cancel(pending);
        bt_conn_unref(pending);
    }
    bt_close(NULL);
    if (initialized)
    {
        bt_br_set_discoverable(false, false);
        bt_br_set_connectable(false, NULL);
    }
}
bool androidauto_bt_retired(void)
{
    rt_base_t f = rt_hw_interrupt_disable();
    bool done = !enabled && !allocated && !pair;
    rt_hw_interrupt_enable(f);
    return done;
}
void androidauto_bt_poll(void)
{
    rt_base_t f = rt_hw_interrupt_disable();
    struct bt_conn *expired = NULL;
    unsigned token = pair_token, key = passkey;
    bool numeric = compare;
    if (pair && (uint32_t)(rt_tick_get_millisecond() - pair_started) >= 30000)
    {
        expired = pair;
        pair = NULL;
        pair_token = 0;
        token = 0;
    }
    rt_hw_interrupt_enable(f);
    if (expired)
    {
        bt_conn_auth_cancel(expired);
        bt_conn_unref(expired);
    }
    if (token && token != displayed)
    {
        displayed = token;
        rt_kprintf("AA pairing token=%u key=%06u numeric_compare=%u; confirm matching key: "
                   "androidauto_pair %u yes\n",
                   token, key, numeric, token);
    }
}
static int pair_command(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "status"))
    {
        rt_base_t f = rt_hw_interrupt_disable();
        unsigned token = pair_token, key = passkey;
        bool numeric = compare;
        rt_hw_interrupt_enable(f);
        rt_kprintf("AA pairing token=%u key=%06u numeric_compare=%u\n", token, key, numeric);
        return 0;
    }
    if (argc != 3 || (strcmp(argv[2], "yes") && strcmp(argv[2], "no")))
        return -1;
    char *end;
    unsigned long requested = strtoul(argv[1], &end, 10);
    if (*end || !requested)
        return -1;
    rt_base_t f = rt_hw_interrupt_disable();
    struct bt_conn *c = pair;
    bool allow = compare && !strcmp(argv[2], "yes");
    if (!c || requested != pair_token ||
        (uint32_t)(rt_tick_get_millisecond() - pair_started) >= 30000)
    {
        rt_hw_interrupt_enable(f);
        return -1;
    }
    pair = NULL;
    pair_token = 0;
    rt_hw_interrupt_enable(f);
    int rc = allow ? bt_conn_auth_passkey_confirm(c) : bt_conn_auth_cancel(c);
    bt_conn_unref(c);
    return rc;
}
MSH_CMD_EXPORT_ALIAS(pair_command, androidauto_pair, Android Auto numeric pairing confirmation);
#endif
