/* SPDX-License-Identifier: Apache-2.0 */
/* CherryUSB polling port for the single foreground context of Luban-Lite. */
#include "usb_osal.h"
#include "usb_errno.h"
#include <aic_osal.h>
#include <aic_time.h>
#include <limits.h>

struct semaphore { uint32_t count, limit; };
struct queue { uint32_t read, write, count, capacity; uintptr_t data[]; };
struct timer {
    struct usb_osal_timer api;
    struct timer *next;
    uint64_t deadline;
    bool active;
};
static struct timer *timers;

/* Threads are deliberately unsupported: the hub uses usbh_hub_poll(). */
usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack,
    uint32_t priority, usb_thread_entry_t entry, void *arg)
{ (void)name; (void)stack; (void)priority; (void)entry; (void)arg; return NULL; }
void usb_osal_thread_delete(usb_osal_thread_t thread) { (void)thread; }
void usb_osal_thread_schedule_other(void) {}
size_t usb_osal_enter_critical_section(void) { return aicos_enter_critical_section(); }
void usb_osal_leave_critical_section(size_t flags) { aicos_leave_critical_section(flags); }
void *usb_osal_malloc(size_t size) { return aicos_malloc(0, size); }
void usb_osal_free(void *ptr) { aicos_free(0, ptr); }
void usb_osal_msleep(uint32_t ms) { aicos_msleep(ms); }

static usb_osal_sem_t sem_create(uint32_t count, uint32_t limit)
{
    struct semaphore *s = usb_osal_malloc(sizeof(*s));
    if (s) { s->count = count; s->limit = limit; }
    return s;
}
usb_osal_sem_t usb_osal_sem_create(uint32_t count) { return sem_create(count, UINT32_MAX); }
usb_osal_sem_t usb_osal_sem_create_counting(uint32_t limit)
{ return limit ? sem_create(0, limit) : NULL; }
void usb_osal_sem_delete(usb_osal_sem_t s) { usb_osal_free(s); }
int usb_osal_sem_take(usb_osal_sem_t handle, uint32_t timeout)
{
    struct semaphore *s = handle;
    uint64_t start = aic_get_time_us();
    if (!s) return -USB_ERR_INVAL;
    do {
        size_t flags = usb_osal_enter_critical_section();
        if (s->count) {
            --s->count;
            usb_osal_leave_critical_section(flags);
            return 0;
        }
        usb_osal_leave_critical_section(flags);
        /* IRQs remain enabled while waiting for an EHCI completion. */
    } while (timeout == USB_OSAL_WAITING_FOREVER ||
             (aic_get_time_us() - start) / 1000 < timeout);
    return -USB_ERR_TIMEOUT;
}
int usb_osal_sem_give(usb_osal_sem_t handle)
{
    struct semaphore *s = handle;
    size_t flags;
    int ret = 0;
    if (!s) return -USB_ERR_INVAL;
    flags = usb_osal_enter_critical_section();
    if (s->count == s->limit) ret = -USB_ERR_NOMEM;
    else ++s->count;
    usb_osal_leave_critical_section(flags);
    return ret;
}
void usb_osal_sem_reset(usb_osal_sem_t handle)
{
    struct semaphore *s = handle;
    size_t flags = usb_osal_enter_critical_section();
    if (s) s->count = 0;
    usb_osal_leave_critical_section(flags);
}
usb_osal_mutex_t usb_osal_mutex_create(void) { return sem_create(1, 1); }
void usb_osal_mutex_delete(usb_osal_mutex_t m) { usb_osal_sem_delete(m); }
int usb_osal_mutex_take(usb_osal_mutex_t m) { return usb_osal_sem_take(m, USB_OSAL_WAITING_FOREVER); }
int usb_osal_mutex_give(usb_osal_mutex_t m) { return usb_osal_sem_give(m); }

usb_osal_mq_t usb_osal_mq_create(uint32_t capacity)
{
    struct queue *q;
    if (!capacity) return NULL;
#if SIZE_MAX <= UINT32_MAX
    if (capacity > (SIZE_MAX - sizeof(*q)) / sizeof(uintptr_t)) return NULL;
#endif
    q = usb_osal_malloc(sizeof(*q) + capacity * sizeof(uintptr_t));
    if (q) { memset(q, 0, sizeof(*q)); q->capacity = capacity; }
    return q;
}
void usb_osal_mq_delete(usb_osal_mq_t q) { usb_osal_free(q); }
int usb_osal_mq_send(usb_osal_mq_t handle, uintptr_t value)
{
    struct queue *q = handle;
    size_t flags;
    if (!q) return -USB_ERR_INVAL;
    flags = usb_osal_enter_critical_section();
    if (q->count == q->capacity) {
        usb_osal_leave_critical_section(flags);
        return -USB_ERR_NOMEM;
    }
    q->data[q->write] = value;
    q->write = (q->write + 1) % q->capacity;
    ++q->count;
    usb_osal_leave_critical_section(flags);
    return 0;
}
int usb_osal_mq_recv(usb_osal_mq_t handle, uintptr_t *value, uint32_t timeout)
{
    struct queue *q = handle;
    uint64_t start = aic_get_time_us();
    if (!q || !value) return -USB_ERR_INVAL;
    do {
        size_t flags = usb_osal_enter_critical_section();
        if (q->count) {
            *value = q->data[q->read];
            q->read = (q->read + 1) % q->capacity;
            --q->count;
            usb_osal_leave_critical_section(flags);
            return 0;
        }
        usb_osal_leave_critical_section(flags);
    } while (timeout == USB_OSAL_WAITING_FOREVER ||
             (aic_get_time_us() - start) / 1000 < timeout);
    return -USB_ERR_TIMEOUT;
}

struct usb_osal_timer *usb_osal_timer_create(const char *name, uint32_t ms,
    usb_timer_handler_t handler, void *arg, bool periodic)
{
    struct timer *t;
    (void)name;
    if (!handler) return NULL;
    t = usb_osal_malloc(sizeof(*t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->api.handler = handler; t->api.argument = arg;
    t->api.timeout_ms = ms ? ms : 1; t->api.is_period = periodic;
    t->next = timers; timers = t;
    return &t->api;
}
void usb_osal_timer_delete(struct usb_osal_timer *api)
{
    struct timer **p = &timers;
    while (*p && &(*p)->api != api) p = &(*p)->next;
    if (*p) { struct timer *t = *p; *p = t->next; usb_osal_free(t); }
}
void usb_osal_timer_start(struct usb_osal_timer *api)
{
    struct timer *t = (struct timer *)api;
    uint64_t now = aic_get_time_us() / 1000;
    size_t flags = usb_osal_enter_critical_section();
    if (t) { t->deadline = now + api->timeout_ms; t->active = true; }
    usb_osal_leave_critical_section(flags);
}
void usb_osal_timer_stop(struct usb_osal_timer *api)
{
    size_t flags = usb_osal_enter_critical_section();
    if (api) ((struct timer *)api)->active = false;
    usb_osal_leave_critical_section(flags);
}

/* Foreground only. Restart traversal after callbacks, which may delete timers. */
void usb_osal_poll_timers(void)
{
    struct timer *t;
    uint64_t now = aic_get_time_us() / 1000;
    for (;;) {
        usb_timer_handler_t handler;
        void *argument;
        size_t flags = usb_osal_enter_critical_section();
        for (t = timers; t; t = t->next)
            if (t->active && t->deadline <= now) break;
        if (!t) { usb_osal_leave_critical_section(flags); break; }
        t->active = t->api.is_period;
        t->deadline = now + t->api.timeout_ms;
        handler = t->api.handler;
        argument = t->api.argument;
        usb_osal_leave_critical_section(flags);
        handler(argument);
    }
}
