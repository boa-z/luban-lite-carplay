/* SPDX-License-Identifier: BSD-3-Clause */
#include "lwip/opt.h"
#if IP_NAPT
#include "lwip/napt_policy.h"
#include "lwip/lwip_napt.h"
#include "lwip/ip4_napt.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip.h"
#include "lwip/icmp.h"
#include "lwip/prot/ip4.h"
#include <string.h>

static struct netif *downstream, *upstream;
static u32_t lan_address, wan_address, lan_mask, wan_mask, gateway;
static struct napt_policy_counters counters;

struct netif *napt_policy_route(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    struct netif *entry;
    NETIF_FOREACH(entry) {
        if (netif_is_up(entry) && netif_is_link_up(entry) && src &&
            ip4_addr_cmp(src, netif_ip4_addr(entry))) return entry;
    }
    NETIF_FOREACH(entry) {
        if (netif_is_up(entry) && netif_is_link_up(entry) &&
            !ip4_addr_isany_val(*netif_ip4_addr(entry)) &&
            ip4_addr_netcmp(dest, netif_ip4_addr(entry), netif_ip4_netmask(entry))) return entry;
    }
    if (netif_default && netif_is_up(netif_default) && netif_is_link_up(netif_default) &&
        !ip4_addr_isany_val(*netif_ip4_addr(netif_default))) return netif_default;
    return NULL;
}

int napt_policy_set(struct netif *lan, struct netif *wan, int enabled)
{
    if (downstream && (!enabled || lan != downstream || wan != upstream ||
        netif_ip4_addr(lan)->addr != lan_address || netif_ip4_addr(wan)->addr != wan_address ||
        netif_ip4_netmask(lan)->addr != lan_mask || netif_ip4_netmask(wan)->addr != wan_mask ||
        netif_ip4_gw(wan)->addr != gateway)) {
        ip_napt_enable_netif(downstream, 0);
        downstream = upstream = NULL;
    }
    if (!enabled) return 0;
    if (!lan || !wan || lan == wan || !netif_is_up(lan) || !netif_is_link_up(lan) ||
        !netif_is_up(wan) || !netif_is_link_up(wan) ||
        ip4_addr_isany_val(*netif_ip4_addr(lan)) || ip4_addr_isany_val(*netif_ip4_addr(wan)) ||
        ip4_addr_netcmp(netif_ip4_addr(wan), netif_ip4_addr(lan), netif_ip4_netmask(lan)) ||
        ip4_addr_netcmp(netif_ip4_addr(lan), netif_ip4_addr(wan), netif_ip4_netmask(wan))) return 0;
    if (!ip_napt_enable_netif(lan, 1)) return 0;
    downstream = lan; upstream = wan;
    lan_address = netif_ip4_addr(lan)->addr; wan_address = netif_ip4_addr(wan)->addr;
    lan_mask = netif_ip4_netmask(lan)->addr; wan_mask = netif_ip4_netmask(wan)->addr; gateway = netif_ip4_gw(wan)->addr;
    return 1;
}

void napt_policy_detach(struct netif *netif)
{
    if (netif == downstream || netif == upstream) napt_policy_set(NULL, NULL, 0);
}

int napt_policy_active(void)
{
    return downstream && upstream && ip_napt_is_enabled(downstream) && netif_is_up(downstream) && netif_is_link_up(downstream) &&
        netif_is_up(upstream) && netif_is_link_up(upstream) &&
        netif_ip4_addr(upstream)->addr == wan_address && netif_ip4_addr(downstream)->addr == lan_address &&
        netif_ip4_netmask(downstream)->addr == lan_mask && netif_ip4_netmask(upstream)->addr == wan_mask && netif_ip4_gw(upstream)->addr == gateway;
}

static int transport_valid(struct pbuf *p)
{
    struct ip_hdr *ip = p->payload;
    unsigned h = IPH_HL_BYTES(ip), needed;
    /* NAPT does not support IP fragments or IPv4 options. Fail closed.
     * Contiguous transport headers are required by the reused translator. */
    if (h != IP_HLEN || (IPH_OFFSET(ip) & PP_HTONS(IP_OFFMASK | IP_MF))) return 0;
    switch (IPH_PROTO(ip)) {
    case IP_PROTO_TCP: needed = TCP_HLEN; break;
    case IP_PROTO_UDP: needed = UDP_HLEN; break;
    case IP_PROTO_ICMP: needed = sizeof(struct icmp_echo_hdr); break;
    default: return 0;
    }
    if (p->len < h + needed || lwip_ntohs(IPH_LEN(ip)) < h + needed) return 0;
    if (IPH_PROTO(ip) == IP_PROTO_TCP) {
        struct tcp_hdr *tcp = (void *)((u8_t *)p->payload + h);
        if (TCPH_HDRLEN_BYTES(tcp) < TCP_HLEN || h + TCPH_HDRLEN_BYTES(tcp) > p->len) return 0;
    }
    if (IPH_PROTO(ip) == IP_PROTO_UDP) {
        struct udp_hdr *udp = (void *)((u8_t *)p->payload + h);
        if (lwip_ntohs(udp->len) < UDP_HLEN || h + lwip_ntohs(udp->len) != p->tot_len) return 0;
    }
    return 1;
}

struct pbuf *napt_policy_input(struct pbuf *p, struct netif *input)
{
    struct ip_hdr *ip = p->payload;
    p->flags &= (u8_t)~PBUF_FLAG_NAPT_TRANSLATED;
    if (!napt_policy_active() || input != upstream || ip->dest.addr != wan_address) return p;
    p = pbuf_coalesce(p, PBUF_RAW);
    if (!transport_valid(p)) { counters.rejected++; pbuf_free(p); return NULL; }
    ip = p->payload;
    ip_napt_recv(p, ip);
    if (ip->dest.addr != wan_address) { counters.inbound++; p->flags |= PBUF_FLAG_NAPT_TRANSLATED; }
    return p;
}

int napt_policy_forward(struct pbuf *p, struct netif *input, struct netif *output)
{
    struct ip_hdr *ip = p->payload;
    if (!napt_policy_active() || !transport_valid(p)) goto reject;
    if (output->mtu && p->tot_len > output->mtu) {
#if LWIP_ICMP
        if (IPH_OFFSET(ip) & PP_HTONS(IP_DF)) icmp_dest_unreach(p, ICMP_DUR_FRAG);
#endif
        goto reject;
    }
    if (input == downstream && output == upstream &&
        ip4_addr_netcmp(&ip->src, netif_ip4_addr(downstream), netif_ip4_netmask(downstream)) &&
        !ip4_addr_ismulticast(&ip->dest) && !ip4_addr_isbroadcast(&ip->dest, downstream)) {
        if (ip_napt_forward(p, ip, input, output) != ERR_OK) goto reject;
        counters.outbound++;
        return 1;
    }
    /* Only reverse translations performed at WAN input may reach the LAN.
     * Plain WAN -> LAN routing is forbidden, including unsolicited packets. */
    if (input == upstream && output == downstream && (p->flags & PBUF_FLAG_NAPT_TRANSLATED)) return 1;
reject:
    counters.rejected++;
    return 0;
}

void napt_policy_stats(struct napt_policy_counters *out) { if (out) *out = counters; }
#endif
