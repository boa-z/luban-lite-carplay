/* Actual bt_enable() with isolated dependency stubs; no HCI hardware.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#define CONFIG_ZTEST 0
#define CONFIG_BT_SETTINGS 1
#define CONFIG_BT_DEVICE_NAME_DYNAMIC 0
#define CONFIG_BT_WAIT_NOP 0
#define CONFIG_BT_DEVICE_NAME "test"
#define IS_ENABLED(x) (x)
#define BT_DEV_ENABLE 0
#define BT_DEV_DISABLE 1
#define BT_HCI_BUS 0
#define BT_HCI_NAME "test"
#define BT_ADDR_ANY NULL
#define BT_MONITOR_TYPE_PRIMARY 0
#define BT_MONITOR_OPEN_INDEX 0
#define LOG_ERR(...)
#define LOG_WRN(...)
struct device { int unused; };
static struct { const struct device *hci; unsigned flags[1]; int ncmd_sem, cmd_tx_queue, init; } bt_dev;
typedef void (*bt_ready_cb_t)(int);
static bt_ready_cb_t ready_cb;
static int settings_result = -EIO;
static unsigned opens, initializations;
static void *rt_device_find(const char *name) { (void)name; return &bt_dev; }
static int atomic_test_and_set_bit(unsigned *p, int bit)
{ int old = !!(*p & (1U << bit)); *p |= 1U << bit; return old; }
static void atomic_clear_bit(unsigned *p, int bit) { *p &= ~(1U << bit); }
static int bt_settings_init(void) { return settings_result; }
static int bt_set_name(const char *name) { (void)name; return 0; }
#define bt_monitor_new_index(...) ((void)0)
#define bt_monitor_send(...) ((void)0)
#define k_sem_init(...) ((void)0)
#define k_fifo_init(...) ((void)0)
#define k_work_submit(...) ((void)0)
static void bt_hci_recv(void) {}
static int bt_hci_open(const struct device *dev, void (*recv_cb)(void))
{ (void)dev; (void)recv_cb; ++opens; return 0; }
static int bt_init(void) { ++initializations; return 0; }
#include BT_ENABLE_SOURCE
int main(void)
{
    assert(bt_enable(NULL) == -EIO);
    assert(!opens && !initializations);
    assert(!(bt_dev.flags[0] & (1U << BT_DEV_ENABLE)));
    settings_result = 0;
    assert(!bt_enable(NULL));
    assert(opens == 1 && initializations == 1);
    assert(bt_enable(NULL) == -EALREADY);
    assert(opens == 1 && initializations == 1);
    puts("Actual SDK bt_enable: settings failure opens no HCI resources, retry initializes once, duplicate enable rejected");
}
