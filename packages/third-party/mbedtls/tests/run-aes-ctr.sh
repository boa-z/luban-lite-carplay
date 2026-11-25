#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
export UBSAN_OPTIONS=halt_on_error=1
export ASAN_OPTIONS=detect_leaks=1
package=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT
source=${1:-$package/ports/src/aes_alt.c}
baseline=''
if [ "${2:-}" = original ]; then baseline='-DEXPECT_NATIVE_CTR_BUG'; fi
"${CC:-cc}" -std=c99 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -fno-pie -no-pie \
    -DMBEDTLS_CONFIG_FILE='"mbedtls_test_config.h"' $baseline \
    -I"$package/tests/config" -I"$package/ports/inc" \
    "$package/tests/test_aes_ctr.c" "$source" -o "$test_build/test"
"$test_build/test"
