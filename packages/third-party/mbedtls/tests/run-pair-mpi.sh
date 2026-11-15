#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Native: sh tests/run-pair-mpi.sh OUTPUT
# RV64: CC=riscv64-linux-gnu-gcc AR=riscv64-linux-gnu-ar
#       RUNNER="qemu-riscv64 -L /usr/riscv64-linux-gnu" sh tests/run-pair-mpi.sh OUTPUT
set -eu
package=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=$package/../rt-carplay
out=${1:?output directory required}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
# Use the upstream host configuration, not the board's hardware ALT backends.
# Both variants use the generic multiply path used on RISC-V; forced limb
# widths cannot be combined with architecture-specific assembly in mbedTLS.
sed '/^#define MBEDTLS_HAVE_ASM/d; /^#define MBEDTLS_AESNI_C/d; /^#define MBEDTLS_PADLOCK_C/d' \
    "$sdk/third_party/mbedtls/include/mbedtls/config.h" > "$out/mpi_test_config.h"
for bits in 32 64; do
    dest=$out/mpi$bits
    mkdir -p "$dest"
    flags="-std=c99 -O2 -g -I$package/mbedtls/include -I$package/mbedtls/library -I$out"
    if [ "$bits" = 32 ]; then flags="$flags -DMBEDTLS_HAVE_INT32"; fi
    for source in "$package"/mbedtls/library/*.c; do
        ${CC:-cc} $flags -DMBEDTLS_CONFIG_FILE='"mpi_test_config.h"' -c "$source" -o "$dest/$(basename "$source" .c).o"
    done
    ${AR:-ar} rcs "$dest/libcrypto.a" "$dest"/*.o
    ${CC:-cc} $flags -DMBEDTLS_CONFIG_FILE='"mpi_test_config.h"' -DEXPECT_LIMB_BITS=$bits \
        "$package/tests/test_mpi.c" "$dest/libcrypto.a" -o "$dest/test_mpi"
    ${CC:-cc} $flags -DMBEDTLS_CONFIG_FILE='"mpi_test_config.h"' \
        -I"$sdk/include" -I"$sdk/core/usb/include" \
        "$sdk/core/usb/lockdown_pair.c" "$sdk/tests/unit/test_lockdown_pair.c" \
        "$dest/libcrypto.a" -o "$dest/test_pair"
    ${RUNNER:-} "$dest/test_mpi"
    ${RUNNER:-} "$dest/test_pair"
done
