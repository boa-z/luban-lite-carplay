#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
probe_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
probe_upstream="$probe_root/../cherryusb"
probe_build=$(mktemp -d)
trap 'rm -rf "$probe_build"' EXIT
python3 "$probe_root/prepare_ehci.py" "$probe_upstream/core/usbh_core.c" \
    "$probe_root/patches/0002-core-ep0-latency-probe.patch" "$probe_build/core.c"
"${CC:-cc}" -std=c99 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -I"$probe_root/tests/config" \
    -I"$probe_upstream/core" -I"$probe_upstream/common" -I"$probe_upstream/class/hub" \
    "$probe_build/core.c" "$probe_root/tests/test_control_probe.c" \
    -Wl,--gc-sections -o "$probe_build/control"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$probe_build/control"
