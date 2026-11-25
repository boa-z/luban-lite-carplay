#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
target=${1:?Usage: build-firmware.sh HARDWARE_TARGET [build|verify|all]}
mode=${2:-all}
case "$mode" in build|verify|all) ;; *) exit 2 ;; esac
settings=$(python3 .ci/hardware_targets.py settings "$target")
mapfile -t fields <<< "$settings"
config=${fields[0]}
boot_config=${fields[1]}
platform=${fields[2]}
layout=${fields[3]}
toolchain=${fields[4]}
checks=${fields[5]}
compat=${fields[6]:-}
pack=$(dirname "$layout")
jobs=${JOBS:-8}
logs="output/ci-logs/$target"
dest="output/ci/$target"
build_images="output/${config%_defconfig}/images"
boot_images="output/${boot_config%_defconfig}/images"
mkdir -p "$logs" "$dest"
exec 9>output/.firmware-build.lock
flock 9
export RTCARPLAY_BUILD_COMMIT="${GITHUB_SHA:-$(git rev-parse HEAD)}"
usb_sources=(packages/third-party/cherryusb packages/third-party/cherryusb-luban)
source_pins() {
    git rev-parse HEAD
    git submodule status --recursive
}

if [[ $mode != verify ]]; then
    rm -f "$logs/source-pins"
    find "$dest" -maxdepth 1 -type f -delete
    rm -f "$build_images"/*.img "$build_images"/bootcfg.txt "$build_images"/ota.cpio
    # SCons packages the firmware as an ELF post-action. Relink when deleting its outputs.
    rm -f "$build_images/$platform.elf" "$boot_images/$platform.elf"
    rm -f "$boot_images/$platform.bin" "$pack/.image_cfg.json.tmp"
    scons --apply-def="$boot_config" 2>&1 | tee "$logs/bootloader-config.log"
    scons -c "${usb_sources[@]}" 2>&1 | tee "$logs/bootloader-clean.log"
    scons -j"$jobs" 2>&1 | tee "$logs/bootloader-build.log"
    test -s "$pack/.image_cfg.json.tmp"
    cmp "$pack/bootloader.bin" "$boot_images/$platform.bin"
    scons --apply-def="$config" 2>&1 | tee "$logs/firmware-config.log"
    python3 .ci/hardware_targets.py check "$target"
    cp .config "$logs/firmware.config"
    scons -c "${usb_sources[@]}" application/rt-thread/rt-carplay-demo/generated \
        2>&1 | tee "$logs/firmware-clean.log"
    scons -j"$jobs" 2>&1 | tee "$logs/firmware-build.log"
    source_pins > "$logs/source-pins"
fi

if [[ $mode != build ]]; then
    cmp "$logs/source-pins" <(source_pins)
    python3 .ci/hardware_targets.py check "$target" --config "$logs/firmware.config"
    if [[ ,$checks, == *,native-ncm,* ]]; then
        python3 .ci/check-wired-profile.py "$logs/firmware.config" "$build_images/$platform.elf" "${toolchain}nm"
    fi
    mapfile -t images < <(find "$build_images" -maxdepth 1 -type f -name '*.img')
    if (( ${#images[@]} != 1 )); then
        echo "Expected one complete IMG for $target, found ${#images[@]}" >&2
        exit 1
    fi
    args=("${images[0]}" --platform "$platform" --bootcfg "$build_images/bootcfg.txt")
    files=("$(basename "${images[0]}")" bootcfg.txt)
    if [[ -n $compat ]]; then
        args+=(--archive "$build_images/ota.cpio" --compat "$compat")
        files+=(ota.cpio)
    fi
    python3 .ci/verify-image.py "${args[@]}"
    if [[ -n $compat ]]; then
        python3 application/rt-thread/rt-carplay-demo/tests/run_ota_worker.py "$build_images/ota.cpio" "$compat"
    fi
    for file in "${files[@]}"; do
        cp "$build_images/$file" "$dest/"
    done
    python3 .ci/write-manifest.py "$target" "$dest" "$logs/firmware.config"
    (cd "$dest" && sha256sum "${files[@]}" source-manifest.json firmware.config > SHA256SUMS)
fi
