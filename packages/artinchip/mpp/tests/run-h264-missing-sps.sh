#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mpp_dir=$(dirname "$test_dir")
sdk_dir=$(CDPATH= cd -- "$mpp_dir/../../.." && pwd)
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
# Optional original h264_nal.c path reproduces the pre-fix null dereference.
h264_source=${1:-"$mpp_dir/ve/decoder/h264/h264_nal.c"}
${CC:-cc} -std=gnu11 -g -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections \
    -I"$test_dir/support" -I"$mpp_dir/include" \
    -I"$sdk_dir/bsp/artinchip/include/uapi" \
    -I"$mpp_dir/base/include" -I"$mpp_dir/ve/include" \
    -I"$mpp_dir/ve/decoder/h264" \
    "$test_dir/test-h264-missing-sps.c" "$h264_source" \
    -Wl,--gc-sections -o "$test_build/test"
"$test_build/test"
