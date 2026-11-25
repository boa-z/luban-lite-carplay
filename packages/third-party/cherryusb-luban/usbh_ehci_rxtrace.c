/* SPDX-License-Identifier: Apache-2.0 */
/* Optional, bounded completion evidence. No printing or payload retention in IRQ. */
#include <rtthread.h>
#include "usb_hc_ehci.h"
#ifdef RT_USING_FINSH
#include <finsh.h>
#define RXTRACE_SAMPLES 12U
struct rxtrace_sample {
    uint32_t actual, prefix, token, packets;
    uint16_t sequence, block;
    uint8_t address, endpoint, initial, final, expected;
    bool header, delivery;
};
struct rxtrace_state {
    bool enabled, have_sequence;
    uint8_t address, endpoint, bus;
    uint16_t sequence;
    uint32_t completed, headers, bad_headers, pid_mismatch, sequence_gaps;
    uint32_t delivered, delivery_bad_headers, delivery_pid_mismatch;
    uint32_t active_unlink, active_delivery, halted_unlink, halted_delivery;
    uint32_t stored;
    struct rxtrace_sample last_good, bad[RXTRACE_SAMPLES];
};
static struct rxtrace_state rxtrace;
static uint16_t trace_le16(const uint8_t *p)
{ return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t trace_le32(const uint8_t *p)
{ return (uint32_t)trace_le16(p) | ((uint32_t)trace_le16(p + 2) << 16); }
/* Called after successful unlink and byte accounting, before replacing the
 * URB's starting PID. Only the 64-KiB NCM receive path is observed. The removed
 * QH and buffer remain owned until the normal IAA completion callback. */
static void record_probe(struct usbh_urb *u, uint32_t token, bool delivery)
{
    if (!rxtrace.enabled || !u || !u->ep || !u->hport || !u->hport->bus ||
        USB_GET_ENDPOINT_TYPE(u->ep->bmAttributes) != USB_ENDPOINT_TYPE_BULK ||
        !(u->ep->bEndpointAddress & 0x80) ||
        u->transfer_buffer_length != 65536U || !u->transfer_buffer ||
        u->actual_length > u->transfer_buffer_length) return;
    /* Both HCD hooks already restrict calls to successful real-data qTDs.
     * A halted stop overlay is intentional and must remain observable. */
    unsigned mps = USB_GET_MAXPACKETSIZE(u->ep->wMaxPacketSize);
    if (!mps) return;
    struct rxtrace_sample s = {0};
    s.delivery = delivery;
    s.actual = u->actual_length; s.token = token;
    s.initial = !!u->data_toggle;
    s.final = !!(token & QTD_TOKEN_TOGGLE);
    s.address = u->hport->dev_addr; s.endpoint = u->ep->bEndpointAddress;
    s.packets = s.actual / mps + (s.actual % mps != 0);
    /* A short transfer ending on a packet boundary includes a terminating ZLP. */
    if (s.actual < u->transfer_buffer_length && s.actual % mps == 0) ++s.packets;
    s.expected = s.initial ^ (s.packets & 1U);
    if (s.actual >= 4U) {
#ifdef CONFIG_USB_DCACHE_ENABLE
        usb_dcache_invalidate((uintptr_t)u->transfer_buffer, CONFIG_USB_ALIGN_SIZE);
#endif
        const uint8_t *p = u->transfer_buffer;
        s.prefix = trace_le32(p);
        if (s.actual >= 12U) {
            s.sequence = trace_le16(p + 6); s.block = trace_le16(p + 8);
            s.header = s.prefix == UINT32_C(0x484d434e) && trace_le16(p + 4) == 12 &&
                trace_le16(p + 10) == 12 && s.block == (uint16_t)s.actual;
        }
    }
    bool control = s.actual == 0 || (s.actual == 4 && (s.prefix & 0xffffU) == 0x100U);
    if (delivery) {
        ++rxtrace.delivered;
        if (token & QTD_TOKEN_STATUS_ACTIVE) ++rxtrace.active_delivery;
        if (token & QTD_TOKEN_STATUS_HALTED) ++rxtrace.halted_delivery;
        if (s.expected != s.final) ++rxtrace.delivery_pid_mismatch;
        if (!s.header && !control) ++rxtrace.delivery_bad_headers;
    } else {
        ++rxtrace.completed;
        if (token & QTD_TOKEN_STATUS_ACTIVE) ++rxtrace.active_unlink;
        if (token & QTD_TOKEN_STATUS_HALTED) ++rxtrace.halted_unlink;
        if (s.expected != s.final) ++rxtrace.pid_mismatch;
    }
    if (!delivery && s.header) {
        bool same = rxtrace.have_sequence && rxtrace.address == s.address &&
            rxtrace.endpoint == s.endpoint && rxtrace.bus == u->hport->bus->busid;
        if (same && (uint16_t)(rxtrace.sequence + 1U) != s.sequence)
            ++rxtrace.sequence_gaps;
        rxtrace.have_sequence = true; rxtrace.sequence = s.sequence;
        rxtrace.address = s.address; rxtrace.endpoint = s.endpoint;
        rxtrace.bus = u->hport->bus->busid;
        rxtrace.last_good = s; ++rxtrace.headers;
    } else if (!delivery && !s.header && !control) {
        /* Four-byte Apple control notifications and ZLPs are not NTBs. */
        ++rxtrace.bad_headers;
    }
    if ((!s.header && s.actual > 4) || s.expected != s.final)
        rxtrace.bad[rxtrace.stored++ % RXTRACE_SAMPLES] = s;
}
void usbh_ehci_rx_complete_probe(struct usbh_urb *u, uint32_t token)
{ record_probe(u, token, false); }
void usbh_ehci_rx_deliver_probe(struct usbh_urb *u, uint32_t token)
{ record_probe(u, token, true); }
static void print_sample(const char *label, const struct rxtrace_sample *s)
{
    /* Each call must fit the board's 128-byte RT_CONSOLEBUF_SIZE, including
     * newline and NUL; a truncated call can hide following serial records. */
    rt_kprintf("[ehci-rx] %s phase=%s addr=%u ep=%02x actual=%u prefix=%08x header=%u\n",
        label, s->delivery ? "deliver" : "unlink", s->address, s->endpoint,
        s->actual, s->prefix, s->header);
    rt_kprintf("[ehci-rx] seq=%u block=%u packets=%u PID=%u->%u expected=%u token=%08x\n",
        s->sequence, s->block, s->packets, s->initial, s->final, s->expected, s->token);
}
static void usb_ehci_rxtrace(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "on")) {
        size_t f = usb_osal_enter_critical_section();
        memset(&rxtrace, 0, sizeof(rxtrace)); rxtrace.enabled = true;
        usb_osal_leave_critical_section(f);
        rt_kprintf("EHCI NCM completion trace on (counters reset)\n"); return;
    }
    if (argc == 2 && !strcmp(argv[1], "off")) {
        size_t f = usb_osal_enter_critical_section(); rxtrace.enabled = false;
        usb_osal_leave_critical_section(f);
        rt_kprintf("EHCI NCM completion trace off (evidence retained)\n"); return;
    }
    if (argc != 1) { rt_kprintf("usage: usb_ehci_rxtrace [on|off]\n"); return; }
    struct rxtrace_state *s = rt_calloc(1, sizeof(*s));
    if (!s) { rt_kprintf("EHCI trace: no memory\n"); return; }
    size_t f = usb_osal_enter_critical_section(); *s = rxtrace;
    usb_osal_leave_critical_section(f);
    rt_kprintf("[ehci-rx] enabled=%u completed=%u headers=%u bad_headers=%u\n",
        s->enabled, s->completed, s->headers, s->bad_headers);
    rt_kprintf("[ehci-rx] pid_mismatch=%u sequence_gaps=%u delivered=%u\n",
        s->pid_mismatch, s->sequence_gaps, s->delivered);
    rt_kprintf("[ehci-rx] delivery_bad_headers=%u delivery_pid_mismatch=%u\n",
        s->delivery_bad_headers, s->delivery_pid_mismatch);
    rt_kprintf("[ehci-rx] active_unlink=%u active_delivery=%u halted_unlink=%u halted_delivery=%u\n",
        s->active_unlink, s->active_delivery, s->halted_unlink, s->halted_delivery);
    if (s->headers) print_sample("last_good", &s->last_good);
    unsigned n = s->stored < RXTRACE_SAMPLES ? s->stored : RXTRACE_SAMPLES;
    for (unsigned i = 0; i < n; ++i)
        print_sample("anomaly", &s->bad[(s->stored - n + i) % RXTRACE_SAMPLES]);
    rt_free(s);
}
MSH_CMD_EXPORT(usb_ehci_rxtrace, Bounded NCM completion PID and header evidence);
#endif
