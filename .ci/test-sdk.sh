#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

usb=packages/third-party/cherryusb-luban
mbedtls=packages/third-party/mbedtls
bt=packages/third-party/zephyr-bluetooth
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

bash "$usb/tests/run.sh"
bash "$usb/tests/run_baremetal.sh"
sh "$usb/tests/run_control_probe.sh"
bash packages/artinchip/mpp/tests/run-h264-missing-sps.sh
bash "$mbedtls/tests/run-aes-ctr.sh"
sh "$mbedtls/tests/run-pair-mpi.sh" "$scratch/mpi"
for test in br_keys enable_settings h5 hci_sync; do
    python3 "$bt/tests/run_$test.py"
done
make -C application/rt-thread/rt-carplay-demo BUILD="$scratch/demo" \
    check-udp-burst
cmake -S .ci/internet-share -B "$scratch/internet-share" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$scratch/internet-share" --parallel 2
ctest --test-dir "$scratch/internet-share" --output-on-failure
