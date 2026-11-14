#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
export UBSAN_OPTIONS=halt_on_error=1
export ASAN_OPTIONS=detect_leaks=1
overlay=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
upstream="$overlay/../cherryusb"
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT
python3 "$overlay/prepare_ehci.py" "$upstream/port/ehci/usb_hc_ehci.c" \
    "$overlay/patches/0001-ehci-complete-short-bulk-in.patch" "$test_build/patched.c"
for variant in original patched; do
    source_file="$test_build/patched.c"
    baseline=''
    if [ "$variant" = original ]; then
        source_file="$upstream/port/ehci/usb_hc_ehci.c"
        baseline='-DEXPECT_UPSTREAM_BUG'
    fi
    "${CC:-cc}" -std=c99 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -fno-pie -no-pie -Wno-pointer-to-int-cast \
        -I"$overlay/tests/config" -I"$upstream/core" -I"$upstream/common" \
        -I"$upstream/class/hub" -I"$upstream/port/ehci" \
        "-DCHERRYUSB_EHCI_SOURCE=\"$source_file\"" $baseline \
        "$overlay/tests/test_ehci_short.c" -Wl,--gc-sections -o "$test_build/$variant"
    "$test_build/$variant"
done
