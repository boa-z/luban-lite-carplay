/* SPDX-License-Identifier: Apache-2.0 */
/* Execute the actual HCD builder/scanner against simulated DMA descriptors. */
#include "usb_hc_ehci.h"
/* The upstream (1 << 31) spelling is signed-shift UB on the host. Use the
 * equivalent unsigned mask here so UBSan can check the descriptor paths. */
#undef QTD_TOKEN_TOGGLE
#define QTD_TOKEN_TOGGLE (UINT32_C(1) << 31)
#include CHERRYUSB_EHCI_SOURCE
#include <assert.h>
#include <stdio.h>
#define RT_USING_FINSH
#include "../usbh_ehci_diag.c"
static struct ehci_hcor regs;
static USB_MEM_ALIGNX uint8_t data[65536];
static USB_MEM_ALIGNX struct usb_setup_packet setup;
static unsigned callbacks, cache_ops;
static int result;
static struct ehci_qh_hw *pending_at_callback;
size_t usb_osal_enter_critical_section(void) { return 0; }
void usb_osal_leave_critical_section(size_t f) { (void)f; }
/* CPU image and DMA memory deliberately diverge while the predecessor QH
 * remains active. Execute the real unlink helper against this cache model. */
static struct ehci_qh *cached_predecessor;
static struct ehci_qh dma_predecessor;
static struct ehci_hcor regs;
void usb_dcache_clean(uintptr_t p, size_t n)
{
    assert(p && n); cache_ops++;
#ifndef EXPECT_UPSTREAM_BUG
    if (p == (uintptr_t)cached_predecessor)
        assert(!(regs.usbcmd & EHCI_USBCMD_ASEN));
#endif
    if (p == (uintptr_t)cached_predecessor)
        memcpy(&dma_predecessor, (void *)p, n < sizeof(dma_predecessor) ? n : sizeof(dma_predecessor));
}
void usb_dcache_invalidate(uintptr_t p, size_t n)
{
    assert(p); cache_ops++;
    if (p == (uintptr_t)cached_predecessor)
        memcpy((void *)p, &dma_predecessor, n < sizeof(dma_predecessor) ? n : sizeof(dma_predecessor));
}
int usb_osal_sem_give(usb_osal_sem_t s) { (void)s; assert(!"async test cannot signal sync waiter"); return 0; }
void usbh_hub_thread_wakeup(struct usbh_hub *h) { (void)h; assert(!"unexpected port-change interrupt"); }
static void done(void *arg, int n)
{
    assert(arg == &callbacks);
    if (pending_at_callback) {
#ifdef EXPECT_IAA_BEFORE_SCAN
        assert(!pending_at_callback->remove_in_iaad);
#else
        assert(pending_at_callback->remove_in_iaad);
#endif
    }
    callbacks++; result = n;
}
static struct usbh_bus bus;
struct usbh_bus g_usbhost_bus[CONFIG_USBHOST_MAX_BUS];
static struct usbh_hub hub;
static struct usbh_hubport port;
static struct usb_endpoint_descriptor ep;
static struct usbh_urb urb;
static struct ehci_qh_hw *begin(bool input, unsigned length)
{
    memset(&bus, 0, sizeof(bus)); memset(&urb, 0, sizeof(urb));
    memset(&regs, 0, sizeof(regs)); memset(&ep, 0, sizeof(ep));
    memset(ehci_qh_pool, 0, sizeof(ehci_qh_pool));
    memset(ehci_qtd_pool, 0, sizeof(ehci_qtd_pool));
    assert((uintptr_t)ehci_qtd_pool < UINT32_MAX); /* -no-pie for EHCI 32-bit links */
    bus.hcd.reg_base = (uintptr_t)&regs;
    port.parent = &hub; port.speed = USB_SPEED_HIGH; port.bus = &bus;
    ep.bEndpointAddress = input ? 0x86 : 5; ep.bmAttributes = 2; ep.wMaxPacketSize = 512;
    urb.hport = &port; urb.ep = &ep; urb.transfer_buffer = data;
    urb.transfer_buffer_length = length; urb.complete = done; urb.arg = &callbacks;
    callbacks = 0; result = -999;
    struct ehci_qh_hw *head = &g_async_qh_head[0];
    memset(head, 0, sizeof(*head)); head->hw.hlp = QH_HLP_QH(head);
    return ehci_bulk_urb_init(&bus, &urb, data, length);
}
static struct ehci_qtd_hw *next(struct ehci_qtd_hw *q)
{ return EHCI_ADDR2QTD(q->hw.next_qtd); }
static void complete(struct ehci_qh_hw *qh)
{
    ehci_check_qh(&bus, &g_async_qh_head[0], qh);
    if (qh->remove_in_iaad) ehci_urb_waitup(&bus, &urb);
}
static void test_predecessor_cache_writeback(void)
{
    struct ehci_qh_hw *removed = begin(true, sizeof(data));
    struct ehci_qh_hw *pred = ehci_qh_alloc(&bus);
    assert(pred && removed);
    struct ehci_qh_hw *head = &g_async_qh_head[0];
    head->hw.hlp = QH_HLP_QH(pred);
    pred->hw.hlp = QH_HLP_QH(removed);
    pred->hw.overlay.token = QTD_TOKEN_STATUS_ACTIVE;
    pred->hw.curr_qtd = 0x1000;
    cached_predecessor = &pred->hw;
    dma_predecessor = pred->hw;
    /* DMA progresses after the CPU last read the predecessor (e.g. after
     * its visit earlier in the IRQ scan). Unlinking the next QH must not
     * roll this unrelated endpoint's overlay back to that cached image. */
    dma_predecessor.overlay.token = QTD_TOKEN_TOGGLE;
    dma_predecessor.curr_qtd = 0x2000;
#ifndef EXPECT_UPSTREAM_BUG
    regs.usbcmd = EHCI_USBCMD_ASEN | EHCI_USBCMD_PSEN;
    regs.usbsts = EHCI_USBSTS_ASS | EHCI_USBSTS_PSS;
    assert(!ehci_qh_remove(head, removed));
    assert(pred->hw.hlp == QH_HLP_QH(removed));
    assert(regs.usbcmd == (EHCI_USBCMD_ASEN | EHCI_USBCMD_PSEN));
    regs.usbsts = EHCI_USBSTS_PSS; /* periodic DMA stays running */
#endif
    ehci_qh_remove(head, removed);
    assert(dma_predecessor.hlp == removed->hw.hlp);
#ifdef EXPECT_UPSTREAM_BUG
    assert(dma_predecessor.overlay.token == QTD_TOKEN_STATUS_ACTIVE);
    assert(dma_predecessor.curr_qtd == 0x1000);
#else
    assert(dma_predecessor.overlay.token == QTD_TOKEN_TOGGLE);
    assert(dma_predecessor.curr_qtd == 0x2000);
    assert(regs.usbcmd == (EHCI_USBCMD_ASEN | EHCI_USBCMD_PSEN));
#endif
    cached_predecessor = NULL;
#ifdef EXPECT_UPSTREAM_BUG
    puts("cache hazard reproduced in upstream predecessor writeback");
#else
    puts("quiesced unlink preserves predecessor DMA toggle/current qTD");
#endif
}
static void test_snapshot(void)
{
    struct ehci_qh_hw *qh = begin(true, sizeof(data));
    struct bus_snapshot s;
    regs.frindex = 123; regs.usbcmd = 0x21; regs.portsc[0] = 0x1005;
    aic_ehci_irqs[0] = 17;
    struct ehci_hcor saved_regs; memcpy(&saved_regs, &regs, sizeof(regs));
    uint32_t first = qh->first_qtd, token = qh->hw.overlay.token;
    assert(snapshot_bus(&bus, &s));
    assert(s.frame == 123 && s.command == 0x21 && s.irqs == 17);
    assert(s.qh[0].used && s.qh[0].endpoint == 0x86 && s.qh[0].count == 4);
    assert(s.qh[0].qtd[0].requested == 16384 && !s.qh[0].bad_chain);
    assert(!memcmp(&saved_regs, &regs, sizeof(regs)));
    assert(qh->first_qtd == first && qh->hw.overlay.token == token);
    g_usbhost_bus[0] = bus;
    usbh_ehci_transfer_snapshot(&urb, -USB_ERR_TIMEOUT);
    assert(!memcmp(&saved_regs, &regs, sizeof(regs)));
    assert(qh->first_qtd == first && qh->hw.overlay.token == token);
    usbh_ehci_transfer_snapshot(NULL, -USB_ERR_TIMEOUT);
    qh->first_qtd = 0xdeadbe00;
    assert(snapshot_bus(&bus, &s) && s.qh[0].bad_chain && !s.qh[0].count);
    qh->first_qtd = first;
    struct ehci_qtd_hw *q = EHCI_ADDR2QTD(first);
    q->hw.next_qtd = first; /* diagnostic walk must be bounded on corruption */
    assert(snapshot_bus(&bus, &s) && s.qh[0].truncated && s.qh[0].count == DIAG_QTDS);
    bus.hcd.reg_base = 0; assert(!snapshot_bus(&bus, &s));
    bus.hcd.reg_base = (uintptr_t)&regs;
    bus.hcd.hcd_id = CONFIG_USBHOST_MAX_BUS; assert(!snapshot_bus(&bus, &s));
    puts("EHCI snapshot: read-only, bounded and rejects invalid descriptor pointers");
}
#ifndef EXPECT_UPSTREAM_BUG
static void test_old_iaa_with_new_completion(uint32_t completion_status)
{
    struct ehci_qh_hw *old = begin(true, 64);
    struct ehci_qtd_hw *q = EHCI_ADDR2QTD(old->first_qtd);
    q->hw.token = QTD_TOKEN_PID_IN;
    ehci_check_qh(&bus, &g_async_qh_head[0], old);
    assert(old->remove_in_iaad && !callbacks);

    /* The previous doorbell has completed when a different endpoint's
     * transfer finishes. Both interrupt bits are delivered together. */
    struct usbh_urb newer = urb;
    newer.hcpriv = NULL;
    newer.actual_length = 0;
    struct ehci_qh_hw *fresh = ehci_bulk_urb_init(&bus, &newer, data, 64);
    assert(fresh && fresh != old);
    q = EHCI_ADDR2QTD(fresh->first_qtd);
    q->hw.token = QTD_TOKEN_PID_IN;
    memset(&g_periodic_qh_head[0], 0, sizeof(g_periodic_qh_head[0]));
    g_usbhost_bus[0] = bus;
    regs.usbintr = completion_status | EHCI_USBSTS_IAA;
    regs.usbsts = regs.usbintr;
    pending_at_callback = fresh;
    USBH_IRQHandler(0);
    pending_at_callback = NULL;
    assert(callbacks == 1);
    assert(!old->inuse && !urb.hcpriv);
    assert(fresh->inuse && fresh->remove_in_iaad && newer.hcpriv == fresh);

    /* Only a subsequent advance acknowledgement can retire this QH. */
    regs.usbsts = EHCI_USBSTS_IAA;
    USBH_IRQHandler(0);
    assert(callbacks == 2 && !newer.hcpriv && !fresh->inuse);
    regs.usbsts = EHCI_USBSTS_IAA;
    USBH_IRQHandler(0);
    assert(callbacks == 2);
    puts("old IAA cannot retire a QH unlinked by the same interrupt");
}
static void test_iaa_without_old_removal(void)
{
    struct ehci_qh_hw *qh = begin(true, 64);
    EHCI_ADDR2QTD(qh->first_qtd)->hw.token = QTD_TOKEN_PID_IN;
    memset(&g_periodic_qh_head[0], 0, sizeof(g_periodic_qh_head[0]));
    g_usbhost_bus[0] = bus;
    regs.usbintr = regs.usbsts = EHCI_USBSTS_INT | EHCI_USBSTS_IAA;
    USBH_IRQHandler(0);
    assert(!callbacks && qh->inuse && qh->remove_in_iaad);
    regs.usbsts = EHCI_USBSTS_IAA;
    USBH_IRQHandler(0);
    assert(callbacks == 1 && !qh->inuse && !urb.hcpriv);
    puts("empty old IAA cohort cannot retire a newly completed transfer");
}
#endif
int main(void)
{
    test_predecessor_cache_writeback();
    test_snapshot();
#ifndef EXPECT_UPSTREAM_BUG
    test_old_iaa_with_new_completion(EHCI_USBSTS_INT);
    test_old_iaa_with_new_completion(EHCI_USBSTS_ERR);
    test_old_iaa_with_new_completion(EHCI_USBSTS_INT | EHCI_USBSTS_ERR);
    test_iaa_without_old_removal();
#endif
    /* Reproduce a 4-byte Apple control transfer terminating qTD 1/4. */
    struct ehci_qh_hw *qh = begin(true, sizeof(data)); assert(qh);
    struct ehci_qtd_hw *q = EHCI_ADDR2QTD(qh->first_qtd);
    assert(q && next(q) && next(next(q)) && next(next(next(q))));
#ifdef EXPECT_UPSTREAM_BUG
    assert(!(q->hw.token & QTD_TOKEN_IOC));
#else
    assert(q->hw.token & QTD_TOKEN_IOC);
#endif
    q->hw.token = QTD_TOKEN_PID_IN | ((q->length - 4) << QTD_TOKEN_NBYTES_SHIFT);
    qh->hw.overlay.token = QTD_TOKEN_TOGGLE;
    complete(qh);
#ifdef EXPECT_UPSTREAM_BUG
    assert(!callbacks && !urb.actual_length && next(q)->hw.token & QTD_TOKEN_STATUS_ACTIVE);
    puts("unpatched HCD reproduced: short first qTD leaves 64 KiB URB pending");
    return 0;
#else
    assert(callbacks == 1 && result == 4 && urb.actual_length == 4 && urb.data_toggle);
    assert(!urb.hcpriv);
    /* Empty IN on a middle qTD: full first qTD + zero-byte second. */
    qh = begin(true, sizeof(data)); q = EHCI_ADDR2QTD(qh->first_qtd);
    q->hw.token = QTD_TOKEN_PID_IN;
    q = next(q); q->hw.token = QTD_TOKEN_PID_IN | (q->length << QTD_TOKEN_NBYTES_SHIFT);
    complete(qh); assert(callbacks == 1 && result == 16384);
    /* An intermediate full qTD must not finish an active chain, for IN/OUT. */
    for (unsigned input = 0; input < 2; input++) {
        qh = begin(input, sizeof(data)); q = EHCI_ADDR2QTD(qh->first_qtd);
        uint32_t pid = input ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT;
        q->hw.token = pid;
        complete(qh); assert(!callbacks && !qh->remove_in_iaad);
        while ((q = next(q))) q->hw.token = pid;
        complete(qh); assert(callbacks == 1 && result == 65536);
    }
    /* A zero-byte first IN and a halted first qTD must not wait for the tail. */
    qh = begin(true, sizeof(data)); q = EHCI_ADDR2QTD(qh->first_qtd);
    q->hw.token = QTD_TOKEN_PID_IN | (q->length << QTD_TOKEN_NBYTES_SHIFT);
    complete(qh); assert(callbacks == 1 && result == 0);
    qh = begin(false, sizeof(data)); q = EHCI_ADDR2QTD(qh->first_qtd);
    q->hw.token = QTD_TOKEN_PID_OUT | QTD_TOKEN_STATUS_HALTED |
                  (q->length << QTD_TOKEN_NBYTES_SHIFT);
    complete(qh); assert(callbacks == 1 && result == -USB_ERR_STALL);
    /* Short control IN must wait for its mandatory status stage. */
    qh = begin(true, 64);
    ehci_qh_remove(&g_async_qh_head[0], qh); ehci_qh_free(&bus, qh);
    ep.bmAttributes = 0; ep.bEndpointAddress = 0;
    setup.bmRequestType = 0x80; setup.wLength = 64; urb.setup = &setup;
    qh = ehci_control_urb_init(&bus, &urb, &setup, data, 64);
    q = EHCI_ADDR2QTD(qh->first_qtd); assert(q->length == 8);
    q->hw.token = QTD_TOKEN_PID_SETUP;
    q = next(q); q->hw.token = QTD_TOKEN_PID_IN | (58 << QTD_TOKEN_NBYTES_SHIFT);
    complete(qh); assert(!callbacks);
    q = next(q); assert(q); q->hw.token = QTD_TOKEN_PID_OUT;
    complete(qh); assert(callbacks == 1 && result == 14);
    assert(cache_ops);
    puts("patched EHCI: short/zero/full/error chains and control status passed");
#endif
    return 0;
}
