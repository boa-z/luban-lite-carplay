/* Host regression for the SDK's actual BR key-pool implementation.
 * Only host/settings dependencies are stubbed; no controller is exercised.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_ENABLED(a) (a)
#define CONFIG_BT_SETTINGS 1
#define LOG_MODULE_REGISTER(a)
#define LOG_DBG(...)
#define LOG_ERR(...)
#define LOG_WRN(...)
#define __ASSERT_NO_MSG(a) assert(a)
typedef struct { uint8_t val[6]; } bt_addr_t;
typedef struct { uint8_t type; bt_addr_t a; } bt_addr_le_t;
#define BT_ADDR_LE_PUBLIC 0
static const bt_addr_t empty;
#define BT_ADDR_ANY (&empty)
static bool bt_addr_eq(const bt_addr_t *a, const bt_addr_t *b) { return !memcmp(a, b, sizeof(*a)); }
static void bt_addr_copy(bt_addr_t *a, const bt_addr_t *b) { memcpy(a, b, sizeof(*a)); }
struct bt_br_bond_info { bt_addr_t addr; };
struct bt_keys_link_key {
    bt_addr_t addr;
    uint8_t enc_key_size;
    uint8_t storage_start[0] __attribute__((aligned(sizeof(void *))));
    uint8_t flags;
    uint8_t val[16];
};
#define BT_KEYS_LINK_KEY_STORAGE_LEN (sizeof(struct bt_keys_link_key) - offsetof(struct bt_keys_link_key, storage_start))
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
static int bt_settings_delete_link_key(const bt_addr_le_t *a) { (void)a; return 0; }
static int bt_settings_store_link_key(const bt_addr_le_t *a, const void *p, size_t n)
{ (void)a; (void)p; (void)n; return 0; }
static int bt_settings_decode_key(const char *s, bt_addr_le_t *a)
{
    memset(a, 0, sizeof(*a)); a->a.val[0] = (uint8_t)(s[0] - '0'); return 0;
}
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include BR_KEYS_SOURCE
#pragma GCC diagnostic pop

static ssize_t read_key(void *arg, void *data, size_t len)
{ memset(data, *(uint8_t *)arg, len); return (ssize_t)len; }
int main(void)
{
    uint8_t value = 0x5a;
    for (unsigned i = 1; i <= CONFIG_BT_MAX_PAIRED; ++i) {
        char name[] = {(char)('0' + i), 0};
        assert(!link_key_set(name, BT_KEYS_LINK_KEY_STORAGE_LEN, read_key, &value));
    }
    char overflow[] = {(char)('1' + CONFIG_BT_MAX_PAIRED), 0};
    assert(link_key_set(overflow, BT_KEYS_LINK_KEY_STORAGE_LEN, read_key, &value) == -ENOMEM);
    bt_addr_t extra = {{(uint8_t)(CONFIG_BT_MAX_PAIRED + 1)}};
    assert(!bt_keys_get_link_key(&extra));
    bt_addr_t first = {{1}};
    struct bt_keys_link_key *key = bt_keys_find_link_key(&first);
    assert(key && key->val[0] == value);
    bt_keys_link_key_clear_addr(&first);
    assert(bt_keys_get_link_key(&extra));
    printf("Actual SDK BR keys: capacity=%d, full-pool restore rejects safely, explicit deletion frees slot\n", CONFIG_BT_MAX_PAIRED);
    return 0;
}
