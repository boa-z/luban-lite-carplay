/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef LWIP_NAPT_POLICY_H
#define LWIP_NAPT_POLICY_H
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#if IP_NAPT
/* All calls run on the TCP/IP core. NULL/disabled closes forwarding and
 * destroys mappings. Only one downstream/upstream pair is supported. */
struct netif *napt_policy_route(const ip4_addr_t *src, const ip4_addr_t *dest);
void napt_policy_detach(struct netif *netif);
int napt_policy_active(void);
int napt_policy_set(struct netif *lan, struct netif *wan, int enabled);
struct pbuf *napt_policy_input(struct pbuf *p, struct netif *input);
int napt_policy_forward(struct pbuf *p, struct netif *input, struct netif *output);
struct napt_policy_counters { u32_t outbound, inbound, rejected; };
void napt_policy_stats(struct napt_policy_counters *out);
#endif
#endif
