#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
variant=${1:-gui}
case "$variant" in
  gui) config=d21x_demo128-nand_rt-thread_rtcarplay_defconfig ;;
  headless) config=d21x_demo128-nand_rt-thread_rtcarplay_headless_defconfig ;;
  *) echo 'Usage: build-firmware.sh [gui|headless]' >&2; exit 2 ;;
esac
jobs=${JOBS:-8}
pack=target/d21x/demo128-nand-carplay/pack
mkdir -p output/ci-logs
# Both stages build this tree, including its CherryUSB baremetal port.
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig 2>&1 | tee output/ci-logs/bootloader-config.log
scons -j"$jobs" 2>&1 | tee output/ci-logs/bootloader-build.log
test -s "$pack/bootloader.bin"
test -s "$pack/.image_cfg.json.tmp"
cmp "$pack/bootloader.bin" output/d21x_demo128-nand-carplay_baremetal_bootloader/images/d21x.bin
scons --apply-def="$config" 2>&1 | tee "output/ci-logs/$variant-config.log"
scons -j"$jobs" 2>&1 | tee "output/ci-logs/$variant-build.log"
if [[ "$variant" == headless ]]; then
  test -s "output/${config%_defconfig}/packages/third-party/rt-carplay/examples/rtthread_headless/headless.o"
fi
dest="output/ci/$variant"
mkdir -p "$dest"
mapfile -t images < <(find "output/${config%_defconfig}/images" -maxdepth 1 -type f -name '*.img')
if (( ${#images[@]} != 1 )); then
  echo "Expected one complete IMG, found ${#images[@]}" >&2; exit 1
fi
python3 .ci/verify-image.py "${images[0]}"
cp "${images[0]}" "$dest/d213-carplay-$variant.img"
cp "$pack/bootloader.bin" "$dest/bootloader.bin"
cp .config "$dest/firmware.config"
cp "$pack/.image_cfg.json.tmp" "$dest/image-layout.json"
{
  printf 'sdk_commit=%s\n' "$(git rev-parse HEAD)"
  printf 'variant=%s\ndefconfig=%s\n' "$variant" "$config"
  git submodule status --recursive
  toolchain/bin/riscv64-unknown-elf-gcc --version | head -1
  if [[ "$variant" == headless ]]; then
    printf 'generic_headless_example=COMPILED_NOT_STARTED\n'
  fi
  printf 'hardware_validation=NOT_RUN\n'
} > "$dest/source-manifest.txt"
(cd "$dest" && sha256sum *.img bootloader.bin firmware.config image-layout.json source-manifest.txt > SHA256SUMS)
