#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build isolated, downloadable GUI/headless development artifacts. No publish.
set -euo pipefail
cd "$(dirname "$0")/.."
variant=${1:-gui}
case "$variant" in gui|headless) ;; *) echo 'Usage: build-ultra-demo.sh gui|headless' >&2; exit 2 ;; esac
config="d21x_demo128-nand_rt-thread_rtcarplay_ultra_${variant}_defconfig"
images="output/${config%_defconfig}/images"
boot_images=output/d21x_demo128-nand-carplay_baremetal_bootloader/images
jobs=${JOBS:-8}
dest="output/ultra-ci/$variant"
logs="$dest/logs"
pack=target/d21x/demo128-nand-carplay/pack
mkdir -p "$logs"
export RTCARPLAY_BUILD_COMMIT="${GITHUB_SHA:-$(git rev-parse HEAD)}"
# Preserve shared generated configuration and bootloader pack inputs for local
# invocations as well as CI. Restore even if a build fails. No source cleanup.
backup=$(mktemp -d)
files=(.config .config.old .defconfig .Kconfig.prj cconfig.h rtconfig.h partition_table.h
       "$pack/bootloader.bin" "$pack/bootloader_upgrade.bin" "$pack/.image_cfg.json.tmp")
for i in "${!files[@]}"; do
  if [[ -f "${files[$i]}" ]]; then cp -p -- "${files[$i]}" "$backup/$i"; fi
done
restore() {
  for i in "${!files[@]}"; do
    if [[ -f "$backup/$i" ]]; then cp -p -- "$backup/$i" "${files[$i]}"; rm -f -- "$backup/$i"
    else rm -f -- "${files[$i]}"; fi
  done
  rmdir -- "$backup"
}
trap restore EXIT
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig 2>&1 | tee "$logs/bootloader-config.log"
# Some third-party groups keep objects beside shared sources. Switching RTOS
# must not reuse RT-Thread USB objects in the baremetal bootloader (or vice versa).
scons -c packages/third-party/cherryusb packages/third-party/cherryusb-luban 2>&1 | tee "$logs/bootloader-clean.log"
# Restored pack inputs are intentionally outside SCons' dependency graph.
# Relink so POST_ACTION always refreshes the matching pack binary/metadata.
rm -f -- "$boot_images/d21x.elf"
scons -j"$jobs" 2>&1 | tee "$logs/bootloader-build.log"
cmp "$pack/bootloader.bin" "$boot_images/d21x.bin"
scons --apply-def="$config" 2>&1 | tee "$logs/config.log"
scons -c packages/third-party/cherryusb packages/third-party/cherryusb-luban 2>&1 | tee "$logs/clean.log"
rm -f -- "$images/d21x.elf"
scons -j"$jobs" 2>&1 | tee "$logs/build.log"
nm=toolchain/bin/riscv64-unknown-elf-nm
if [[ ! -f "$nm" && -f "$nm.exe" ]]; then nm="$nm.exe"; fi
python3 .ci/check-wired-profile.py .config "$images/d21x.elf" "$nm"
mapfile -t image_files < <(find "$images" -maxdepth 1 -type f -name '*.img')
if (( ${#image_files[@]} != 1 )); then echo 'Expected one complete IMG' >&2; exit 1; fi
python3 .ci/verify-image.py "${image_files[0]}"
cp "${image_files[0]}" "$dest/d213-carplay-ultra-$variant.img"
cp "$images/d21x.elf" "$images/d21x.map" "$dest/"
cp "$pack/bootloader.bin" "$dest/bootloader.bin"
cp .config "$dest/build.config"
{
  printf 'profile=carplay-ultra-development\nvariant=%s\ndefconfig=%s\n' "$variant" "$config"
  printf 'integration_commit=%s\n' "$(git rev-parse HEAD)"
  printf 'sdk_commit=%s\n' "$(git -C packages/third-party/rt-carplay rev-parse HEAD)"
  printf 'L0=PASS_SOURCE_RESEARCH\nL1=PARTIAL_HOST_FOUNDATION\nL2=NOT_RUN\nL3=NOT_RUN\nL4=NOT_RUN\nL5=NOT_RUN\n'
  printf 'official_ultra_acceptance=NOT_RUN\n'
  printf 'integration_worktree_begin\n'
  git status --porcelain --untracked-files=all -- . ':!target/d21x/demo128-nand-carplay/pack/bootloader.bin' ':!target/d21x/demo128-nand-carplay/pack/bootloader_upgrade.bin'
  printf 'integration_worktree_end\nsdk_worktree_begin\n'
  git -C packages/third-party/rt-carplay status --porcelain --untracked-files=all
  printf 'sdk_worktree_end\n'
  git submodule status --recursive
} > "$dest/acceptance.txt"
(cd "$dest" && sha256sum *.img *.elf *.map bootloader.bin build.config acceptance.txt > SHA256SUMS)
