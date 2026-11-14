# D213 AES-CTR backend correction

The v14 D213 configuration enables RT_HWCRYPTO_USING_AES and therefore
MBEDTLS_AES_ALT. AES-ECB/CBC are configured; native AES-CTR is not configured.
The mbedTLS ALT `mbedtls_aes_crypt_ctr()` nevertheless switches its context to
HWCRYPTO_TYPE_AES_CTR and submits a hardware operation. The generic RT-Thread
set-type operation does not reject that subtype based on the Kconfig feature.

In `bsp/artinchip/drv/ce/drv_ce.c`, the native CTR switch case was empty while
ret remained RT_EOK. Depending on the alignment-buffer path, output was left
untouched or replaced with unchanged input. The caller received success.

This affects the MFiSAP encrypted signature: local RSA verification is before
CTR encryption, so verified=1 does not establish that the transmitted signature
is correctly encrypted. The supplied v14 phone still closes roughly 25 ms
after the auth response. The backend defect supplies a concrete software
explanation, but hardware acceptance after this correction remains to be tested.
The earlier application interop test used the software mbedTLS backend and did
not exercise this SDK ALT implementation.

The correction implements standard CTR counter/keystream bookkeeping using the
working AES-ECB encryption primitive. The mechanism follows Apache-2.0 mbedTLS
2.28 `mbedtls/library/aes.c`, with its copyright/license retained in aes_alt.c.
Both directions use AES encryption; no unsupported CTR/decrypt operation is
submitted. Caller-owned nonce_counter, stream_block and nc_off survive partial
calls. Invalid arguments and hardware errors propagate. Native CE CTR requests
now fail explicitly instead of reporting successful work.

No application authentication wire format, signing input, hash, control-channel
frame size or IPHETH logic changes are required by this fix. The application
also gains a NIST known-answer check on its actual runtime AES-CTR backend,
which rejects silent success before constructing an authentication response.

## Host reproduction

```sh
packages/third-party/mbedtls/tests/run-aes-ctr.sh
# Optional reproduction against an unmodified pre-fix adapter:
git show a6c3a34a4:packages/third-party/mbedtls/ports/src/aes_alt.c > /tmp/aes_alt_original.c
packages/third-party/mbedtls/tests/run-aes-ctr.sh /tmp/aes_alt_original.c original
```

The test compiles the actual ALT C file with minimal RT-Thread hardware seams.
Its deterministic ECB oracle uses NIST SP 800-38A F.5.1 counter blocks and
additional independently generated AES-ECB vectors. The native CTR seam
models the original no-op-with-success behavior. This is an adapter test, not
an emulation of CE hardware/cache/DMA.

Original reproduction confirms success with unchanged plaintext. The corrected
adapter passes ASan/UBSan with the NIST 64-byte encrypt/decrypt vector,
128-byte signature-sized data, arbitrary fragmented/in-place calls, cached
keystream offsets, full 128-bit counter wrap, bounds canaries, invalid offsets,
zero-length operations and hardware-error propagation. No native CTR operation
is issued by the corrected adapter. A D213 full firmware build also passed.
The runtime known-answer result will verify the actual ECB-backed CTR path
on the board; phone acceptance remains pending.
