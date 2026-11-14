/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_osal.h"
#include "usb_errno.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

void usb_osal_poll_timers(void);
static uint64_t now;
static unsigned int locked, allocations;
static bool fail_alloc;
static usb_osal_sem_t irq_sem;
void *aicos_malloc(unsigned int type, size_t size)
{ (void)type; if (fail_alloc) return NULL; void *p = malloc(size); if (p) ++allocations; return p; }
void aicos_free(unsigned int type, void *p)
{ (void)type; if (p) { assert(allocations); --allocations; free(p); } }
size_t aicos_enter_critical_section(void) { return locked++; }
void aicos_leave_critical_section(size_t previous) { assert(locked == previous + 1); locked = previous; }
uint64_t aic_get_time_us(void)
{
    assert(!locked);
    now += 100;
    if (irq_sem) { usb_osal_sem_t s = irq_sem; irq_sem = NULL; usb_osal_sem_give(s); }
    return now;
}
void aicos_msleep(uint32_t ms) { now += (uint64_t)ms * 1000; }
static unsigned int calls;
static struct usb_osal_timer *self_timer;
static void tick(void *arg) { assert(arg == &calls); ++calls; }
static void self_delete(void *arg) { tick(arg); usb_osal_timer_delete(self_timer); self_timer = NULL; }

int main(void)
{
    usb_osal_sem_t s = usb_osal_sem_create_counting(1);
    assert(s && usb_osal_sem_take(s, 0) == -USB_ERR_TIMEOUT);
    assert(usb_osal_sem_give(s) == 0);
    assert(usb_osal_sem_give(s) < 0);
    assert(usb_osal_sem_take(s, 0) == 0);
    uint64_t start = now;
    assert(usb_osal_sem_take(s, 3) == -USB_ERR_TIMEOUT && now - start >= 3000);
    irq_sem = s;
    assert(usb_osal_sem_take(s, 10) == 0);
    usb_osal_sem_give(s); usb_osal_sem_reset(s);
    assert(usb_osal_sem_take(s, 0) < 0);
    usb_osal_sem_delete(s);
    assert(usb_osal_sem_take(NULL, 0) == -USB_ERR_INVAL);
    assert(!usb_osal_sem_create_counting(0));
    usb_osal_mutex_t m = usb_osal_mutex_create();
    assert(m && !usb_osal_mutex_take(m) && !usb_osal_mutex_give(m));
    usb_osal_mutex_delete(m);

    usb_osal_mq_t q = usb_osal_mq_create(2);
    uintptr_t value;
    assert(q && usb_osal_mq_recv(q, &value, 0) == -USB_ERR_TIMEOUT);
    assert(!usb_osal_mq_send(q, 10) && !usb_osal_mq_send(q, 20));
    assert(usb_osal_mq_send(q, 30) < 0);
    assert(!usb_osal_mq_recv(q, &value, 0) && value == 10);
    assert(!usb_osal_mq_send(q, 30));
    assert(!usb_osal_mq_recv(q, &value, 0) && value == 20);
    assert(!usb_osal_mq_recv(q, &value, 0) && value == 30);
    assert(usb_osal_mq_recv(q, &value, 2) == -USB_ERR_TIMEOUT);
    usb_osal_mq_delete(q);
    assert(!usb_osal_mq_create(0));

    struct usb_osal_timer *t = usb_osal_timer_create("tick", 5, tick, &calls, true);
    assert(t); usb_osal_timer_start(t);
    usb_osal_poll_timers(); assert(calls == 0);
    aicos_msleep(5); usb_osal_poll_timers(); assert(calls == 1);
    aicos_msleep(5); usb_osal_poll_timers(); assert(calls == 2);
    usb_osal_timer_stop(t); aicos_msleep(20); usb_osal_poll_timers(); assert(calls == 2);
    usb_osal_timer_delete(t);
    self_timer = usb_osal_timer_create("self", 0, self_delete, &calls, false);
    usb_osal_timer_start(self_timer); aicos_msleep(2); usb_osal_poll_timers();
    assert(!self_timer && calls == 3);
    usb_osal_poll_timers(); assert(calls == 3);
    fail_alloc = true;
    assert(!usb_osal_sem_create(0) && !usb_osal_mq_create(1));
    assert(!usb_osal_timer_create("oom", 1, tick, NULL, false));
    assert(!usb_osal_thread_create("unsupported", 1, 1, NULL, NULL));
    assert(!locked && !allocations);
    puts("Baremetal OSAL: IRQ completion, deadlines, queue capacity/wrap, timers, deletion and allocation failure passed");
    return 0;
}
