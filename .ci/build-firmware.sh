#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
config=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
jobs=${JOBS:-8}
pack=target/d21x/demo128-nand-carplay/pack
mkdir -p output/ci-logs
# Both stages build this tree, including its CherryUSB baremetal port.
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig 2>&1 | tee output/ci-logs/bootloader-config.log
scons -j"$jobs" 2>&1 | tee output/ci-logs/bootloader-build.log
test -s "$pack/bootloader.bin"
test -s "$pack/.image_cfg.json.tmp"
cmp "$pack/bootloader.bin" output/d21x_demo128-nand-carplay_baremetal_bootloader/images/d21x.bin
scons --apply-def="$config" 2>&1 | tee output/ci-logs/firmware-config.log
scons -j"$jobs" 2>&1 | tee output/ci-logs/firmware-build.log
dest=output/ci
mkdir -p "$dest"
mapfile -t images < <(find "output/${config%_defconfig}/images" -maxdepth 1 -type f -name '*.img')
if (( ${#images[@]} != 1 )); then
  echo "Expected one complete IMG, found ${#images[@]}" >&2; exit 1
fi
python3 .ci/verify-image.py "${images[0]}"

# The release also publishes the A/B OTA package built beside the IMG.
ota="output/${config%_defconfig}/images/ota.cpio"
test -s "$ota"
# Windows hosts do not have cpio on PATH; the SDK ships the same tool.
cpio_bin=$(command -v cpio || true)
if [[ -z "$cpio_bin" ]]; then
  cpio_bin="$PWD/tools/scripts/cpio.exe"
  test -x "$cpio_bin" || { echo 'cpio is required to verify the OTA package' >&2; exit 1; }
fi
"$cpio_bin" -it < "$ota" | tr -d '\r' > /tmp/ota-list.txt
for member in ota_info.bin d21x_os.itb rodata.fatfs; do
  grep -qx "$member" /tmp/ota-list.txt
done
test "$(stat -c %s "$ota")" -lt $((8 * 1024 * 1024))

cp "${images[0]}" "$dest/d213-carplay.img"
cp "$ota" "$dest/ota.cpio"
cp "$pack/bootloader.bin" "$dest/bootloader.bin"
{
  printf 'sdk_commit=%s\n' "$(git rev-parse HEAD)"
  printf 'source_worktree_changes_begin\n'
  git status --porcelain --untracked-files=all -- . ':!target/d21x/demo128-nand-carplay/pack/bootloader.bin'
  printf 'source_worktree_changes_end\n'
  printf 'defconfig=%s\n' "$config"
  git submodule status --recursive
  toolchain/bin/riscv64-unknown-elf-gcc --version | head -1
  printf 'hardware_validation=NOT_RUN\n'
} > "$dest/source-manifest.txt"
(cd "$dest" && sha256sum *.img *.cpio bootloader.bin source-manifest.txt > SHA256SUMS)
