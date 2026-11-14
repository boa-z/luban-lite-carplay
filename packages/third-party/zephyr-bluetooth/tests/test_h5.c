/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static void mock_log(const char *format, ...) { (void)format; }
#define LOG_DBG(...) do { if (0) mock_log(__VA_ARGS__); } while (0)
#define LOG_WRN(...) LOG_DBG(__VA_ARGS__)
#define LOG_ERR(...) LOG_DBG(__VA_ARGS__)
#define ARG_UNUSED(x) (void)(x)
#define __ASSERT_NO_MSG(x) assert(x)
#define __ASSERT(x,...) assert(x)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define K_NO_WAIT 0
#define K_FOREVER (-1)
#define K_MSEC(n) (n)
#define BT_BUF_RESERVE 0
#define CONFIG_BT_HCI_ACL_FLOW_CONTROL 1
#define IS_ENABLED(x) 0 /* ISO_RX and TESTING are disabled in this fixture. */
#define BT_CONN_INDEX_INVALID 0xff
#define BT_CONN_CONNECTED 1
#define BT_CONN_DISCONNECTING 2
#define BT_HCI_OP_HOST_NUM_COMPLETED_PACKETS 0x0c35
#define BT_CMD_TEST(commands,a,b) (controller_flow_control)
#define sys_cpu_to_le16(x) (x) /* Little-endian host fixture. */
enum bt_buf_type { BT_BUF_EVT, BT_BUF_ACL_IN, BT_BUF_ISO_IN };
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef int k_timeout_t;
typedef unsigned char k_thread_stack_t;
struct k_thread { int unused; };
struct rt_device { int unused; };
struct device { void *data; const void *config; };
struct bt_conn_rx { uint8_t index; uint16_t handle; };
struct net_buf {
    uint8_t bytes[512], *data; unsigned len; struct net_buf *next;
    bool acl_in; struct bt_conn_rx meta;
};
struct bt_conn { uint16_t handle; unsigned state; };
static struct bt_conn connection={.handle=1,.state=BT_CONN_CONNECTED};
static int acl_in_pool, evt_pool;
static bool controller_flow_control=true;
static unsigned completed_commands;
void bt_hci_host_num_completed_packets(struct net_buf *buf);
typedef int (*bt_hci_recv_t)(const struct device *, struct net_buf *);
struct k_fifo { struct net_buf *head, *tail; };
struct k_sem { int count; };
struct k_mutex { pthread_mutex_t mutex; };
struct k_work { int unused; };
struct k_work_delayable { struct k_work work; int delay; unsigned schedules; };
#define NET_BUF_POOL_DEFINE(name,...) static int name
static unsigned allocations, releases, delivered;
static struct net_buf *net_buf_alloc(void *pool, int timeout) {
    (void)pool; (void)timeout; ++allocations;
    struct net_buf *b=calloc(1,sizeof(*b)); assert(b); b->data=b->bytes;
    b->acl_in=pool==&acl_in_pool; return b;
}
static void net_buf_destroy(struct net_buf *b) { assert(b); ++releases; free(b); }
static void net_buf_unref(struct net_buf *b) {
    assert(b);
    if(b->acl_in) bt_hci_host_num_completed_packets(b); else net_buf_destroy(b);
}
static void *net_buf_user_data(struct net_buf *b) { return &b->meta; }
static size_t net_buf_tailroom(struct net_buf *b) { return sizeof(b->bytes)-b->len; }
static void net_buf_add_mem(struct net_buf *b, const void *p, size_t n) {
    assert(n<=net_buf_tailroom(b)); memcpy(b->data+b->len,p,n); b->len+=(unsigned)n;
}
static struct net_buf *bt_buf_get_evt(uint8_t evt, bool discard, int timeout) {
    (void)evt; (void)discard; return net_buf_alloc(NULL,timeout);
}
static void *net_buf_add(struct net_buf *b, size_t size) {
    assert(size<=net_buf_tailroom(b)); void *p=b->data+b->len; b->len+=(unsigned)size; return p;
}
static void net_buf_add_u8(struct net_buf *b, uint8_t value) { net_buf_add_mem(b,&value,1); }
static uint8_t bt_buf_type_to_h4(enum bt_buf_type type) { return type==BT_BUF_EVT?4:2; }
static struct bt_conn *bt_conn_lookup_index(uint8_t index) { return index==0?&connection:NULL; }
static void bt_conn_unref(struct bt_conn *conn) { assert(conn==&connection); }
struct bt_hci_cp_host_num_completed_packets { uint8_t num_handles; };
struct bt_hci_handle_count { uint16_t handle,count; } __attribute__((packed));
#define acl(b) (&(b)->meta)
static struct net_buf *bt_hci_cmd_alloc(int timeout) { return net_buf_alloc(NULL,timeout); }
static int bt_hci_cmd_send(uint16_t opcode, struct net_buf *buf) {
    /* Check actual command builder: num_handles=1, handle=1 LE, count=1 LE. */
    uint8_t expected[]={1,1,0,1,0};
    assert(opcode==BT_HCI_OP_HOST_NUM_COMPLETED_PACKETS);
    assert(buf->len==sizeof(expected) && !memcmp(buf->data,expected,sizeof(expected)));
    ++completed_commands; net_buf_unref(buf); return 0;
}
static struct net_buf *bt_iso_get_rx(int timeout) { (void)timeout; abort(); }
static void bt_testing_trace_event_acl_pool_destroy(struct net_buf *b) { (void)b; }
/* PRODUCTION_ACL_ALLOC */
/* PRODUCTION_ACL_CREDITS */
static void k_fifo_put(struct k_fifo *q, struct net_buf *b) {
    b->next=NULL; if(q->tail) q->tail->next=b; else q->head=b; q->tail=b;
}
static struct net_buf *k_fifo_get(struct k_fifo *q, int timeout) {
    (void)timeout; struct net_buf *b=q->head;
    if(b) { q->head=b->next; if(!q->head) q->tail=NULL; b->next=NULL; } return b;
}
static void k_mutex_init(struct k_mutex *m) { assert(!pthread_mutex_init(&m->mutex,NULL)); }
static void k_mutex_lock(struct k_mutex *m, int timeout) {
    (void)timeout; assert(!pthread_mutex_lock(&m->mutex));
}
static void k_mutex_unlock(struct k_mutex *m) { assert(!pthread_mutex_unlock(&m->mutex)); }
static void k_sem_give(struct k_sem *s) { s->count=1; }
static jmp_buf worker_exit;
static unsigned worker_takes;
static void k_sem_take(struct k_sem *s, int timeout) {
    (void)s; (void)timeout; if(worker_takes++) longjmp(worker_exit,1);
}
static void k_yield(void) { sched_yield(); }
static int k_work_cancel_delayable(struct k_work_delayable *w) { w->delay=-1; return 0; }
static int k_work_reschedule(struct k_work_delayable *w, int delay) {
    w->delay=delay; ++w->schedules; return 0;
}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w,struct k_work_delayable,work);
}
static uint8_t wire[65536];
static size_t wire_len;
static uint8_t rx[65536];
static size_t rx_pos,rx_len;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool pause_wire, wire_paused, ack_started, resume_wire;
static int uart_poll_out(struct rt_device *uart, uint8_t byte) {
    pthread_mutex_lock(&gate);
    if(pause_wire) {
        wire_paused=true; pthread_cond_broadcast(&changed);
        while(!resume_wire) pthread_cond_wait(&changed,&gate);
        pause_wire=false;
    }
    pthread_mutex_unlock(&gate);
    (void)uart; assert(wire_len<sizeof(wire)); wire[wire_len++]=byte;
    sched_yield(); /* Make byte interleaving reproducible without serialization. */
    return 1;
}
static int uart_fifo_read(const struct rt_device *uart, uint8_t *byte, int n) {
    (void)uart; assert(n==1); if(rx_pos==rx_len) return 0; *byte=rx[rx_pos++]; return 1;
}
static int uart_irq_update(const struct rt_device *u) { (void)u; return 1; }
static int uart_irq_is_pending(const struct rt_device *u) { (void)u; return rx_pos<rx_len; }
static int uart_irq_rx_ready(const struct rt_device *u) { return uart_irq_is_pending(u); }
static int uart_irq_tx_ready(const struct rt_device *u) { (void)u; return 0; }
/* PRODUCTION_H5 */
static struct h5_data state;
static struct rt_device uart;
static struct h5_config cfg={.uart=&uart};
static struct device device={.data=&state,.config=&cfg};
static int receive(const struct device *d, struct net_buf *b) {
    assert(d==&device); /* Callback must run without the H5 state lock. */
    assert(!pthread_mutex_trylock(&state.lock.mutex));
    pthread_mutex_unlock(&state.lock.mutex);
    if(b->acl_in) { b->meta.index=0; b->meta.handle=connection.handle; }
    ++delivered; net_buf_unref(b); return 0;
}
static void pump_tx(void) {
    worker_takes=0;
    if(!setjmp(worker_exit)) tx_thread(&device,NULL,NULL);
}
static void enqueue(uint8_t type, uint8_t value) {
    struct net_buf *b=net_buf_alloc(NULL,0);
    uint8_t data[]={type,value,0xc0,0xdb}; net_buf_add_mem(b,data,sizeof(data));
    h5_queue(&device,b);
}
static void feed(const uint8_t *data, size_t len, bool fragmented) {
    assert(len<sizeof(rx)); memcpy(rx,data,len); rx_pos=0; rx_len=0;
    if(fragmented) { for(size_t i=0;i<len;++i) { rx_len=i+1; bt_uart_isr(&uart,&device); } }
    else { rx_len=len; bt_uart_isr(&uart,&device); }
    assert(rx_pos==rx_len);
}
static void make_frame(uint8_t type, uint8_t seq, uint8_t ack,
                       const uint8_t *payload, size_t len) {
    wire_len=0;
    uint8_t hdr[4]={0};
    H5_SET_TYPE(hdr,type); H5_SET_LEN(hdr,len); H5_SET_ACK(hdr,ack);
    if(reliable_packet(type)) { H5_SET_RELIABLE(hdr); H5_SET_SEQ(hdr,seq); }
    hdr[3]=(uint8_t)~(hdr[0]+hdr[1]+hdr[2]);
    uart_poll_out(&uart,0xc0);
    for(size_t i=0;i<4;++i) h5_slip_byte(&uart,hdr[i]);
    for(size_t i=0;i<len;++i) h5_slip_byte(&uart,payload[i]);
    uart_poll_out(&uart,0xc0);
}
static void *send_many(void *arg) {
    bool ack=(uintptr_t)arg;
    uint8_t payload[]={0xc0,0xdb,0x42,0x42};
    for(unsigned i=0;i<100;++i) {
        if(ack) ack_timeout(&state.ack_work.work);
        else h5_send(&device,payload,HCI_SCODATA_PKT,sizeof(payload));
    } return NULL;
}
static void *retry_worker(void *unused) {
    (void)unused; retx_timeout(&state.retx_work.work); return NULL;
}
static void *ack_worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&gate); ack_started=true;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    k_mutex_lock(&state.lock,K_FOREVER);
    state.rx_ack=state.tx_seq; process_unack(&state);
    k_mutex_unlock(&state.lock); return NULL;
}
static unsigned verify_frames(void) {
    size_t pos=0; unsigned frames=0;
    while(pos<wire_len) {
        assert(wire[pos++]==0xc0);
        uint8_t bytes[64]; unsigned n=0;
        while(pos<wire_len && wire[pos]!=0xc0) {
            uint8_t byte=wire[pos++];
            if(byte==0xdb) { assert(pos<wire_len); byte=wire[pos++];
                assert(byte==0xdc || byte==0xdd); byte=byte==0xdc?0xc0:0xdb; }
            assert(n<sizeof(bytes)); bytes[n++]=byte;
        }
        assert(pos<wire_len && wire[pos++]==0xc0 && n>=4);
        assert((uint8_t)(bytes[0]+bytes[1]+bytes[2]+bytes[3])==0xff);
        assert((unsigned)H5_HDR_LEN(bytes)==n-4); ++frames;
    } return frames;
}
int main(void) {
    k_mutex_init(&state.lock); state.dev=&device; state.recv=receive;
    state.link_state=ACTIVE; state.tx_win=4;
    /* A binary notification may coalesce: one wake must fill only the window. */
    for(unsigned i=0;i<6;++i) enqueue(HCI_COMMAND_PKT,(uint8_t)i);
    pump_tx(); assert(state.unack_queue_len==4 && state.tx_seq==4);
    assert(state.tx_queue.head && state.tx_queue.head->data[1]==4);
    /* In-place retries preserve seq, order, queue ownership, and timer. */
    wire_len=0; retx_timeout(&state.retx_work.work);
    assert(verify_frames()==4 && state.tx_seq==4 && state.unack_queue_len==4);
    uint8_t saved[65536]; size_t saved_len=wire_len; memcpy(saved,wire,saved_len);
    wire_len=0; retx_timeout(&state.retx_work.work);
    assert(wire_len==saved_len && !memcmp(wire,saved,saved_len));
    assert(state.retx_work.delay==H5_TX_ACK_TIMEOUT);
    /* Invalid ACK used to underflow the backwards search; must leave ownership intact. */
    state.rx_ack=7; process_unack(&state); assert(state.unack_queue_len==4);
    state.rx_ack=2; process_unack(&state); assert(state.unack_queue_len==2);
    assert(state.unack_queue.head->data[1]==2);
    pump_tx(); assert(state.unack_queue_len==4 && state.tx_seq==6 && !state.tx_queue.head);
    state.rx_ack=6; process_unack(&state); assert(!state.unack_queue_len);
    assert(state.retx_work.delay==-1);
    /* Window sequence wrap 6,7,0,1 -> ACK 0 frees exactly the first two. */
    for(unsigned i=0;i<4;++i) enqueue(HCI_COMMAND_PKT,(uint8_t)i);
    pump_tx(); assert(state.tx_seq==2 && state.unack_queue_len==4);
    state.rx_ack=0; process_unack(&state); assert(state.unack_queue_len==2);
    state.rx_ack=2; process_unack(&state); assert(!state.unack_queue_len);
    /* Force ACK processing to race with a retransmission already in UART.
     * ACK must wait until retry owns no in-flight buffers or temporary seq.
     */
    for(unsigned i=0;i<4;++i) enqueue(HCI_COMMAND_PKT,(uint8_t)i);
    pump_tx(); assert(state.tx_seq==6 && state.unack_queue_len==4);
    wire_len=0; pause_wire=true;
    pthread_t retry,ack;
    assert(!pthread_create(&retry,NULL,retry_worker,NULL));
    pthread_mutex_lock(&gate);
    while(!wire_paused) pthread_cond_wait(&changed,&gate);
    pthread_mutex_unlock(&gate);
    assert(!pthread_create(&ack,NULL,ack_worker,NULL));
    pthread_mutex_lock(&gate);
    while(!ack_started) pthread_cond_wait(&changed,&gate);
    resume_wire=true; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    assert(!pthread_join(retry,NULL) && !pthread_join(ack,NULL));
    assert(state.tx_seq==6 && !state.unack_queue_len && verify_frames()==4);
    state.tx_seq=2; /* Independent RX fixture below uses peer ACK=2. */
    /* SCO is unreliable: it must never enter the reliable retransmit queue. */
    enqueue(HCI_SCODATA_PKT,0x33); pump_tx(); assert(!state.unack_queue_len && state.tx_seq==2);
    /* Lost ACK produces duplicate RX; acknowledge it without duplicate HCI delivery. */
    uint8_t event[]={0xff,2,0xc0,0xdb};
    make_frame(HCI_EVENT_PKT,0,2,event,sizeof(event));
    feed(wire,wire_len,true); assert(delivered==1 && state.tx_ack==1);
    feed(wire,wire_len,true); assert(delivered==1 && state.tx_ack==1);
    assert(state.rx_sequence_retries==1 && state.ack_work.delay==0);
    make_frame(HCI_EVENT_PKT,1,2,event,sizeof(event));
    feed(wire,wire_len,true); assert(delivered==2 && state.tx_ack==2);
    /* Zero-size reliable events must not deliver NULL to the HCI receiver. */
    make_frame(HCI_EVENT_PKT,2,2,NULL,0);
    feed(wire,wire_len,true); assert(delivered==2 && !state.rx_buf);
    /* Invalid header, interrupted body, then valid recovery. */
    uint8_t corrupt[]={0xc0,0x80,0x14,0,0,0xff,0xc0};
    feed(corrupt,sizeof(corrupt),true); assert(delivered==2);
    make_frame(HCI_EVENT_PKT,2,2,event,sizeof(event));
    memcpy(saved,wire,wire_len); saved_len=wire_len;
    feed(saved,6,true); feed(saved,saved_len,true); assert(delivered==3);
    /* Production allocator and destructor: a dropped H5 ACL retransmission
     * must not report a phantom completion for zero-initialized conn/handle 0.
     */
    uint8_t acl_payload[]={1,0,1,0,0x42};
    make_frame(HCI_ACLDATA_PKT,3,2,acl_payload,sizeof(acl_payload));
    feed(wire,wire_len,true); assert(delivered==4 && completed_commands==1);
    feed(wire,wire_len,true); assert(delivered==4 && completed_commands==1);
    make_frame(HCI_ACLDATA_PKT,4,2,acl_payload,sizeof(acl_payload));
    feed(wire,6,true); /* Partial ACL allocated, then raw delimiter resets it. */
    uint8_t delimiter=0xc0; feed(&delimiter,1,true);
    assert(completed_commands==1 && !state.rx_buf);
    struct net_buf *stale=bt_buf_get_rx(BT_BUF_ACL_IN,0);
    stale->meta.index=0; stale->meta.handle=2; net_buf_unref(stale);
    assert(completed_commands==1); /* Index reused for another connection handle. */
    struct net_buf *fresh=bt_buf_get_rx(BT_BUF_ACL_IN,0);
    assert(fresh->meta.index==BT_CONN_INDEX_INVALID); net_buf_unref(fresh);
    assert(completed_commands==1);
    /* ACK worker and sender must never interleave bytes on the UART. */
    wire_len=0; pthread_t a,b;
    assert(!pthread_create(&a,NULL,send_many,(void *)(uintptr_t)0));
    assert(!pthread_create(&b,NULL,send_many,(void *)(uintptr_t)1));
    assert(!pthread_join(a,NULL) && !pthread_join(b,NULL));
    assert(verify_frames()==200);
    assert(allocations==releases);
    assert(!pthread_mutex_destroy(&state.lock.mutex));
    puts("Actual H5: bounded/coalesced TX, ACK validation, wrap, in-place retries, "
         "concurrent retry/ACK, SCO ownership, split escapes, duplicate suppression/re-ACK, malformed recovery, "
         "200 concurrent UART frames, actual ACL credit encoding/discard/reuse and buffer balance passed (ASan/UBSan)");
}
