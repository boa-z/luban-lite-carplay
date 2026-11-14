#!/usr/bin/env python3
"""Run the actual SDK synchronous HCI function with two concurrent callers."""
from pathlib import Path
import os
import subprocess
import tempfile

bt = Path(__file__).resolve().parents[1]
text = (bt/'subsys/bluetooth/host/hci_core.c').read_text()
start = text.index('int bt_hci_cmd_send_sync(')
end = text.index('{', start)+1
depth = 1
while depth:
    depth += (text[end] == '{')-(text[end] == '}')
    end += 1
function = text[start:end]
source = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#define LOG_DBG(...) do { if(0) printf(__VA_ARGS__); } while(0)
#define LOG_WRN(...) LOG_DBG(__VA_ARGS__)
#define BT_ASSERT_MSG(expr, ...) assert(expr)
#define __ASSERT_NO_MSG(expr) assert(expr)
#define IS_ENABLED(x) (x)
#define __maybe_unused __attribute__((unused))
#define CONFIG_BT_TX_PROCESSOR_THREAD 1
#define K_FOREVER (-1)
#define HCI_CMD_TIMEOUT 1000
#define BT_HCI_ERR_CONN_LIMIT_EXCEEDED 9
#define BT_HCI_ERR_INSUFFICIENT_RESOURCES 7
#define BT_HCI_ERR_INVALID_PARAM 0x12
#define BT_HCI_ERR_CMD_DISALLOWED 0x0c
typedef int k_timeout_t;
struct k_sem { pthread_mutex_t mutex; pthread_cond_t cond; bool initialized; unsigned count; };
struct net_buf { int id, pool_id, len; atomic_int refs; };
struct cmd_data { uint8_t status; struct k_sem *sync; };
static struct cmd_data commands[2];
static struct k_sem cmd_sync_sems[2];
static struct net_buf buffers[2];
static atomic_uint allocations;
#define cmd(buf) (&commands[net_buf_id(buf)])
static int hci_cmd_pool;
static struct { int thread; } k_sys_work_q;
static struct { int cmd_tx_queue; } bt_dev;
static pthread_mutex_t control=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool queued[2], done[2];
static int results[2];
static unsigned net_buf_id(struct net_buf *buf) { return (unsigned)buf->id; }
static void *net_buf_pool_get(int id) { assert(id==0); return &hci_cmd_pool; }
static struct net_buf *net_buf_ref(struct net_buf *buf) { atomic_fetch_add(&buf->refs,1); return buf; }
static void net_buf_unref(struct net_buf *buf) { assert(atomic_fetch_sub(&buf->refs,1)>0); }
static struct net_buf *bt_hci_cmd_alloc(int timeout) {
    assert(timeout==K_FOREVER);
    unsigned id=atomic_fetch_add(&allocations,1)%2;
    struct net_buf *buf=&buffers[id];
    assert(!atomic_load(&buf->refs)); buf->id=(int)id; buf->pool_id=0; buf->len=1;
    atomic_store(&buf->refs,1); commands[id].status=0; return buf;
}
static int k_sem_init(struct k_sem *sem, unsigned initial, unsigned limit) {
    assert(!initial && limit==1);
    pthread_mutex_lock(&control);
    if (!sem->initialized) {
        assert(!pthread_mutex_init(&sem->mutex,NULL));
        assert(!pthread_cond_init(&sem->cond,NULL)); sem->initialized=true;
    }
    pthread_mutex_unlock(&control);
    return 0; /* SDK port also retains the allocated semaphore on reuse. */
}
static int k_sem_take(struct k_sem *sem, int timeout) {
    assert(timeout==HCI_CMD_TIMEOUT); pthread_mutex_lock(&sem->mutex);
    while (!sem->count) pthread_cond_wait(&sem->cond,&sem->mutex);
    --sem->count; pthread_mutex_unlock(&sem->mutex); return 0;
}
static void k_sem_give(struct k_sem *sem) {
    pthread_mutex_lock(&sem->mutex); ++sem->count;
    pthread_cond_signal(&sem->cond); pthread_mutex_unlock(&sem->mutex);
}
static int bt_hci_cmd_send(uint16_t opcode, struct net_buf *buf) {
    assert(opcode==0x0c19+buf->id);
    pthread_mutex_lock(&control); queued[buf->id]=true;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&control); return 0;
}
static void *k_current_get(void) { return NULL; }
static struct net_buf *k_fifo_peek_head(void *queue) { (void)queue; return NULL; }
static bool process_pending_cmd(int timeout) { (void)timeout; return false; }
static const char *bt_hci_err_to_str(unsigned status) { (void)status; return "mock"; }
'''
source += function
source += r'''
static void *caller(void *arg) {
    unsigned id=(unsigned)(uintptr_t)arg;
    struct net_buf *response=NULL;
    int result=bt_hci_cmd_send_sync((uint16_t)(0x0c19+id),NULL,&response);
    if (id==0) { assert(result==0 && response==&buffers[0]); net_buf_unref(response); }
    else assert(result==-EACCES && !response);
    pthread_mutex_lock(&control); results[id]=result; done[id]=true;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&control); return NULL;
}
static void wait_queued(unsigned id) {
    pthread_mutex_lock(&control);
    while(!queued[id]) pthread_cond_wait(&changed,&control);
    pthread_mutex_unlock(&control);
}
int main(void) {
    (void)cmd_sync_sems;
    for (unsigned round=0;round<3;++round) {
        queued[0]=queued[1]=done[0]=done[1]=false;
        pthread_t a,b; assert(!pthread_create(&a,NULL,caller,(void *)(uintptr_t)0));
        wait_queued(0);
        assert(!pthread_create(&b,NULL,caller,(void *)(uintptr_t)1)); wait_queued(1);
        assert(commands[0].sync!=commands[1].sync); /* old SDK shared one waiter */
        unsigned first=round%2, second=1-first;
        commands[1].status=BT_HCI_ERR_CMD_DISALLOWED;
        net_buf_unref(&buffers[first]); k_sem_give(commands[first].sync);
        pthread_mutex_lock(&control);
        while(!done[first]) pthread_cond_wait(&changed,&control);
        assert(!done[second]); /* One command must never wake the other caller. */
        pthread_mutex_unlock(&control);
        net_buf_unref(&buffers[second]); k_sem_give(commands[second].sync);
        assert(!pthread_join(a,NULL) && !pthread_join(b,NULL));
        assert(!atomic_load(&buffers[0].refs) && !atomic_load(&buffers[1].refs));
    }
    puts("Actual SDK HCI sync: two concurrent callers, ordered/reverse completions, isolated errors/responses, reference balance and 3 slot reuses passed");
}
'''
with tempfile.TemporaryDirectory(prefix='rtcp-hci-sync-') as directory:
    c = Path(directory)/'test.c'
    binary = Path(directory)/'test'
    c.write_text(source)
    subprocess.run([os.environ.get('CC','cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
                    '-pthread', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
