/* SPDX-License-Identifier: Apache-2.0 */
/* Read-only controller snapshot. No HCD mutation, cancellation or IRQ ack. */
#include <rtthread.h>
#include "usb_hc_ehci.h"
#ifdef RT_USING_FINSH
#include <finsh.h>
#include <stdlib.h>
#define DIAG_QTDS 8
extern struct ehci_qh_hw ehci_qh_pool[CONFIG_USBHOST_MAX_BUS][CONFIG_USB_EHCI_QH_NUM];
extern struct ehci_qtd_hw ehci_qtd_pool[CONFIG_USBHOST_MAX_BUS][CONFIG_USB_EHCI_QTD_NUM];
extern struct ehci_qh_hw g_async_qh_head[CONFIG_USBHOST_MAX_BUS];
volatile uint32_t aic_ehci_irqs[CONFIG_USBHOST_MAX_BUS];
struct qtd_snapshot { uint32_t token, next, alternate, requested; };
struct qh_snapshot {
    bool used, bad_chain, truncated;
    uint8_t address, endpoint;
    uint32_t requested, actual, link, current, token, next, alternate;
    int error;
    unsigned count;
    struct qtd_snapshot qtd[DIAG_QTDS];
};
struct bus_snapshot {
    uint32_t command, status, interrupt, frame, async, port, irqs, head;
    unsigned hcd;
    struct qh_snapshot qh[CONFIG_USB_EHCI_QH_NUM];
};
static bool qtd_in_pool(unsigned hcd, const struct ehci_qtd_hw *q)
{
    uintptr_t p = (uintptr_t)q, start = (uintptr_t)&ehci_qtd_pool[hcd][0];
    uintptr_t end = (uintptr_t)&ehci_qtd_pool[hcd][CONFIG_USB_EHCI_QTD_NUM];
    return p >= start && p < end && (p - start) % sizeof(*q) == 0;
}
static bool snapshot_bus(struct usbh_bus *bus, struct bus_snapshot *s)
{
    if (!bus->hcd.reg_base || bus->busid >= CONFIG_USBHOST_MAX_BUS ||
        bus->hcd.hcd_id >= CONFIG_USBHOST_MAX_BUS) return false;
    memset(s, 0, sizeof(*s));
    size_t flags = usb_osal_enter_critical_section();
    s->hcd = bus->hcd.hcd_id;
    s->command = EHCI_HCOR->usbcmd; s->status = EHCI_HCOR->usbsts;
    s->interrupt = EHCI_HCOR->usbintr; s->frame = EHCI_HCOR->frindex;
    s->async = EHCI_HCOR->asynclistaddr; s->port = EHCI_HCOR->portsc[0];
    s->irqs = aic_ehci_irqs[bus->busid];
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
    usb_dcache_invalidate((uintptr_t)&g_async_qh_head[s->hcd].hw, CONFIG_USB_EHCI_ALIGN_SIZE);
#endif
    s->head = g_async_qh_head[s->hcd].hw.hlp;
    for (unsigned i = 0; i < CONFIG_USB_EHCI_QH_NUM; i++) {
        struct ehci_qh_hw *qh = &ehci_qh_pool[s->hcd][i];
        struct qh_snapshot *out = &s->qh[i];
        if (!qh->inuse || !qh->urb) continue;
        struct usbh_urb *u = qh->urb;
        out->used = true;
        out->address = u->hport ? u->hport->dev_addr : 0;
        out->endpoint = u->ep ? u->ep->bEndpointAddress : 0;
        out->requested = u->transfer_buffer_length;
        out->actual = u->actual_length; out->error = u->errorcode;
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
        usb_dcache_invalidate((uintptr_t)&qh->hw, CONFIG_USB_EHCI_ALIGN_SIZE);
#endif
        out->link = qh->hw.hlp; out->current = qh->hw.curr_qtd;
        out->token = qh->hw.overlay.token; out->next = qh->hw.overlay.next_qtd;
        out->alternate = qh->hw.overlay.alt_next_qtd;
        struct ehci_qtd_hw *q = EHCI_ADDR2QTD(qh->first_qtd);
        while (q && out->count < DIAG_QTDS) {
            if (!qtd_in_pool(s->hcd, q)) { out->bad_chain = true; break; }
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
            usb_dcache_invalidate((uintptr_t)&q->hw, CONFIG_USB_EHCI_ALIGN_SIZE);
#endif
            struct qtd_snapshot *d = &out->qtd[out->count++];
            d->token = q->hw.token; d->next = q->hw.next_qtd;
            d->alternate = q->hw.alt_next_qtd; d->requested = q->length;
            q = EHCI_ADDR2QTD(q->hw.next_qtd);
        }
        out->truncated = q != NULL && !out->bad_chain;
    }
    usb_osal_leave_critical_section(flags);
    return true;
}
static void usb_ehci_status(int argc, char **argv)
{
    unsigned first = 0, end = CONFIG_USBHOST_MAX_BUS;
    if (argc > 2) goto usage;
    if (argc == 2) {
        char *tail; unsigned long index = strtoul(argv[1], &tail, 0);
        if (!argv[1][0] || *tail || index >= end) goto usage;
        first = (unsigned)index; end = first + 1;
    }
    struct bus_snapshot *s = rt_calloc(1, sizeof(*s));
    if (!s) { rt_kprintf("EHCI snapshot: no memory\n"); return; }
    for (unsigned b = first; b < end; b++) {
        if (!snapshot_bus(&g_usbhost_bus[b], s)) {
            rt_kprintf("EHCI bus %u: inactive\n", b); continue;
        }
        rt_kprintf("EHCI bus %u hcd %u IRQ=%u CMD=%08x STS=%08x INTR=%08x FRAME=%08x PORT=%08x\n",
            b, s->hcd, s->irqs, s->command, s->status, s->interrupt, s->frame, s->port);
        rt_kprintf("  ASYNC=%08x head_link=%08x (read-only snapshot)\n", s->async, s->head);
        for (unsigned i = 0; i < CONFIG_USB_EHCI_QH_NUM; i++) {
            const struct qh_snapshot *q = &s->qh[i];
            if (!q->used) continue;
            rt_kprintf("  QH%u addr=%u ep=%02x length=%u actual=%u err=%d link=%08x current=%08x\n",
                i, q->address, q->endpoint, q->requested, q->actual, q->error, q->link, q->current);
            rt_kprintf("    overlay token=%08x next=%08x alternate=%08x\n", q->token, q->next, q->alternate);
            for (unsigned j = 0; j < q->count; j++) {
                const struct qtd_snapshot *d = &q->qtd[j];
                rt_kprintf("    qTD%u token=%08x requested=%u remaining=%u next=%08x alternate=%08x\n",
                    j, d->token, d->requested,
                    (d->token & QTD_TOKEN_NBYTES_MASK) >> QTD_TOKEN_NBYTES_SHIFT,
                    d->next, d->alternate);
            }
            if (q->bad_chain) rt_kprintf("    invalid qTD address; walk stopped\n");
            if (q->truncated) rt_kprintf("    qTD walk truncated at %u descriptors\n", DIAG_QTDS);
        }
    }
    rt_free(s); return;
usage:
    rt_kprintf("usage: usb_ehci_status [bus]\n");
}
/* Low-priority startup diagnostics; no URB or hport pointer escapes its owner. */
void usbh_ehci_network_snapshot(void)
{
    extern const char *usbh_ehci_completion_policy(void) __attribute__((weak));
    if (usbh_ehci_completion_policy)
        rt_kprintf("[wired-test] EHCI completion=%s\n", usbh_ehci_completion_policy());
    char command[] = "usb_ehci_status";
    char *argv[] = {command};
    usb_ehci_status(1, argv);
}
/* Called from a waiting task before cancellation destroys the pending QH.
 * Uses the same bounded read-only collector as the shell command. */
void usbh_ehci_transfer_snapshot(struct usbh_urb *urb, int reason)
{
    if (!urb || !urb->hport || !urb->hport->bus || !urb->ep) return;
    unsigned id = urb->hport->bus->busid;
    if (id >= CONFIG_USBHOST_MAX_BUS) return;
    char index[4]; unsigned used = 0;
    if (id >= 100U) index[used++] = (char)('0' + id / 100U);
    if (id >= 10U) index[used++] = (char)('0' + (id / 10U) % 10U);
    index[used++] = (char)('0' + id % 10U); index[used] = 0;
    char command[] = "usb_ehci_status";
    char *argv[] = {command, index};
    rt_kprintf("EHCI before cancel bus=%u ep=%02x reason=%d\n",
               id, urb->ep->bEndpointAddress, reason);
    usb_ehci_status(2, argv);
}
MSH_CMD_EXPORT(usb_ehci_status, Read-only EHCI register and descriptor snapshot);
#endif
