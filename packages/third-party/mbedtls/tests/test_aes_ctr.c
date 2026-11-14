/* SPDX-License-Identifier: Apache-2.0
 * Exercises the actual SDK AES_ALT with a deterministic ECB oracle and the
 * original CE native-CTR no-op. First four blocks are NIST SP 800-38A F.5.1;
 * additional counter blocks were generated independently with OpenSSL-backed
 * Python cryptography. This test does not emulate CE DMA or cache behavior.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtthread.h"
#include "aes_alt.h"

static const unsigned char key[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};
static const unsigned char initial[16] = {
    0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};
static const unsigned char plain[64] = {
    0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
    0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51,
    0x30,0xc8,0x1c,0x46,0xa3,0x5c,0xe4,0x11,0xe5,0xfb,0xc1,0x19,0x1a,0x0a,0x52,0xef,
    0xf6,0x9f,0x24,0x45,0xdf,0x4f,0x9b,0x17,0xad,0x2b,0x41,0x7b,0xe6,0x6c,0x37,0x10
};
static const unsigned char sealed[64] = {
    0x87,0x4d,0x61,0x91,0xb6,0x20,0xe3,0x26,0x1b,0xef,0x68,0x64,0x99,0x0d,0xb6,0xce,
    0x98,0x06,0xf6,0x6b,0x79,0x70,0xfd,0xff,0x86,0x17,0x18,0x7b,0xb9,0xff,0xfd,0xff,
    0x5a,0xe4,0xdf,0x3e,0xdb,0xd5,0xd3,0x5e,0x5b,0x4f,0x09,0x02,0x0d,0xb0,0x3e,0xab,
    0x1e,0x03,0x1d,0xda,0x2f,0xbe,0x03,0xd1,0x79,0x21,0x70,0xa0,0xf3,0x00,0x9c,0xee
};
#include "aes_ctr_ecb_vectors.h"
static int ecb_calls, native_calls, fail_ecb, fail_type;
void *rt_hwcrypto_dev_default(void) { return (void *)1; }
void *rt_hwcrypto_symmetric_create(void *dev, unsigned type)
{
    assert(dev); struct hwcrypto_symmetric *c = calloc(1, sizeof(*c));
    assert(c); c->type = type; return c;
}
void rt_hwcrypto_symmetric_destroy(void *ctx) { free(ctx); }
int rt_hwcrypto_symmetric_setkey(void *ctx, const unsigned char *k, unsigned bits)
{
    struct hwcrypto_symmetric *c = ctx; assert(c && bits == 128);
    memcpy(c->key, k, 16); c->flags |= SYMMTRIC_MODIFY_KEY; return 0;
}
int rt_hwcrypto_symmetric_set_type(void *ctx, unsigned type)
{
    if (fail_type) return -1;
    ((struct hwcrypto_symmetric *)ctx)->type = type; return 0;
}
int rt_hwcrypto_symmetric_setiv(void *ctx, const unsigned char *iv, size_t n)
{ assert(n == 16); memcpy(((struct hwcrypto_symmetric *)ctx)->iv, iv, n); return 0; }
int rt_hwcrypto_symmetric_getiv(void *ctx, unsigned char *iv, size_t n)
{ assert(n == 16); memcpy(iv, ((struct hwcrypto_symmetric *)ctx)->iv, n); return 0; }
int rt_hwcrypto_symmetric_set_ivoff(void *ctx, int n)
{ ((struct hwcrypto_symmetric *)ctx)->ivoff = n; return 0; }
int rt_hwcrypto_symmetric_get_ivoff(void *ctx, int *n)
{ *n = ((struct hwcrypto_symmetric *)ctx)->ivoff; return 0; }
int rt_hwcrypto_symmetric_crypt(void *ctx, int mode, size_t n,
                              const unsigned char *in, unsigned char *out)
{
    struct hwcrypto_symmetric *c = ctx;
    assert(!memcmp(c->key, key, 16));
    if (c->type == HWCRYPTO_TYPE_AES_CTR) {
        /* Faithful model of the old driver's alignment-buffer path:
         * return RT_EOK and copy unchanged input for native CTR. */
        native_calls++; memmove(out, in, n); c->flags = 0; return 0;
    }
    assert(c->type == HWCRYPTO_TYPE_AES_ECB && mode == HWCRYPTO_MODE_ENCRYPT && n == 16);
    if (fail_ecb) return -1;
    ecb_calls++;
    for (size_t i = 0; i < sizeof(counters) / sizeof(counters[0]); ++i) {
        if (!memcmp(in, counters[i], 16)) {
            memcpy(out, encrypted_counters[i], 16); c->flags = 0; return 0;
        }
    }
    assert(!"unexpected ECB counter"); return -1;
}
static void reset(mbedtls_aes_context *c, unsigned char counter[16],
                  unsigned char stream[16], size_t *offset)
{
    *offset = 0; memcpy(counter, initial, 16); memset(stream, 0, 16);
    assert(!mbedtls_aes_setkey_enc(c, key, 128));
}
int main(void)
{
    mbedtls_aes_context c;
    unsigned char counter[16], stream[16], out[129];
#ifndef EXPECT_NATIVE_CTR_BUG
    unsigned char expected[128];
#endif
    size_t offset;
    mbedtls_aes_init(&c); reset(&c, counter, stream, &offset);
    memset(out, 0xa5, sizeof(out));
    assert(!mbedtls_aes_crypt_ctr(&c, 64, &offset, counter, stream, plain, out));
#ifdef EXPECT_NATIVE_CTR_BUG
    assert(native_calls == 1 && ecb_calls == 0);
    assert(memcmp(out, sealed, 64) != 0 && !memcmp(out, plain, 64));
    puts("Original SDK AES_ALT reproduces silent native-CTR success without encryption");
#else
    assert(!memcmp(out, sealed, 64) && out[64] == 0xa5 && offset == 0);
    assert(ecb_calls == 4 && native_calls == 0);
    assert(counter[14] == 0xff && counter[15] == 3);
    /* Standard decrypt is the same CTR XOR with the ENCRYPT key/primitive. */
    reset(&c, counter, stream, &offset);
    assert(!mbedtls_aes_crypt_ctr(&c, 64, &offset, counter, stream, sealed, out));
    assert(!memcmp(out, plain, 64));
    /* Signature-sized buffer, fragmented calls and in-place input. */
    for (size_t i = 0; i < 128; ++i) {
        out[i] = (unsigned char)i;
        expected[i] = (unsigned char)i ^ encrypted_counters[i / 16][i % 16];
    }
    out[128] = 0xa5;
    reset(&c, counter, stream, &offset);
    const size_t parts[] = {1, 15, 7, 31, 2, 72};
    size_t pos = 0;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        assert(!mbedtls_aes_crypt_ctr(&c, parts[i], &offset, counter, stream, out + pos, out + pos));
        pos += parts[i]; assert(offset == pos % 16);
    }
    assert(pos == 128 && !memcmp(out, expected, 128) && out[128] == 0xa5);
    /* Complete 128-bit counter wrap and cached partial keystream. */
    reset(&c, counter, stream, &offset); memset(counter, 0xff, 16);
    memset(out, 0, 32);
    assert(!mbedtls_aes_crypt_ctr(&c, 32, &offset, counter, stream, out, out));
    assert(!memcmp(out, encrypted_counters[8], 16));
    assert(!memcmp(out + 16, encrypted_counters[9], 16));
    assert(!memcmp(counter, counters[9], 15) && counter[15] == 1);
    reset(&c, counter, stream, &offset);
    assert(!mbedtls_aes_crypt_ctr(&c, 0, &offset, counter, stream, NULL, NULL));
    assert(offset == 0 && !memcmp(counter, initial, 16));
    offset = 16;
    assert(mbedtls_aes_crypt_ctr(&c, 1, &offset, counter, stream, plain, out) == MBEDTLS_ERR_AES_BAD_INPUT_DATA);
    offset = 0; fail_type = 1;
    assert(mbedtls_aes_crypt_ctr(&c, 1, &offset, counter, stream, plain, out) == MBEDTLS_ERR_AES_HW_ACCEL_FAILED);
    fail_type = 0; fail_ecb = 1; out[0] = 0xa5;
    assert(mbedtls_aes_crypt_ctr(&c, 1, &offset, counter, stream, plain, out) != 0);
    assert(out[0] == 0xa5 && !memcmp(counter, initial, 16));
    fail_ecb = 0;
    assert(mbedtls_aes_crypt_ctr(NULL, 1, &offset, counter, stream, plain, out) == MBEDTLS_ERR_AES_BAD_INPUT_DATA);
    assert(native_calls == 0);
    puts("SDK AES_ALT NIST CTR, 128-byte signature, chunks, in-place, wrap and errors passed");
#endif
    mbedtls_aes_free(&c);
    return 0;
}
