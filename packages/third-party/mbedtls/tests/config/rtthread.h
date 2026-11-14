/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int32_t rt_int32_t;
#define RT_EOK 0
#define SYMMTRIC_MODIFY_KEY 1
struct hwcrypto_symmetric {
    unsigned flags, type;
    unsigned char key[16], iv[16];
    int ivoff;
};
#define HWCRYPTO_TYPE_AES 1
#define HWCRYPTO_TYPE_AES_ECB 2
#define HWCRYPTO_TYPE_AES_CTR 3
#define HWCRYPTO_MODE_ENCRYPT 1
#define HWCRYPTO_MODE_DECRYPT 0
void *rt_hwcrypto_dev_default(void);
void *rt_hwcrypto_symmetric_create(void *, unsigned);
void rt_hwcrypto_symmetric_destroy(void *);
int rt_hwcrypto_symmetric_setkey(void *, const unsigned char *, unsigned);
int rt_hwcrypto_symmetric_set_type(void *, unsigned);
int rt_hwcrypto_symmetric_setiv(void *, const unsigned char *, size_t);
int rt_hwcrypto_symmetric_getiv(void *, unsigned char *, size_t);
int rt_hwcrypto_symmetric_set_ivoff(void *, int);
int rt_hwcrypto_symmetric_get_ivoff(void *, int *);
int rt_hwcrypto_symmetric_crypt(void *, int, size_t, const unsigned char *, unsigned char *);
