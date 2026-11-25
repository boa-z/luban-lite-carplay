# CarPlay firmware

[简体中文](carplay-firmware.zh-CN.md)

[SDK integration and dependency audit](carplay-sdk-integration.md)

| Hardware target | Radio path | Firmware features |
| --- | --- | --- |
| `d213-demo128-nand-carplay-rtl8733bs` | Realtek SDIO, H:5 BR/EDR | Wired and wireless CarPlay, Ethernet sharing, A/B OTA |
| `d213-demo128-nand-carplay-aic8800d40` | AIC8800 D80-family SDIO firmware and H:4 BR/EDR | Wired and wireless CarPlay, A/B OTA |

The operator confirmed wired and wireless CarPlay on D40 image SHA‑256 `098367850f4a45b821813bc9266b0f0b66b4b09fb65e76fe8342d411b0ee5dec` built from `c59d3cc3`. New images require separate board evidence. D40L has a separate WLAN selection and cannot select the D40 Classic host. D80 identifies the SDK's SDIO product and firmware family used by the board-tested D40 radio profile.

## Build and register hardware

Use Linux or WSL. Install the packages listed by the Firmware build workflow and the pinned Python requirements, then run `git lfs pull` and `git submodule update --init --recursive`.

```sh
python3 .ci/hardware_targets.py matrix
bash .ci/build-firmware.sh d213-demo128-nand-carplay-aic8800d40
```

The script accepts `build` and `verify` as separate phases. Results are in `output/ci/<target>/` and logs are in `output/ci-logs/<target>/`. A lock serializes builds sharing `.config` and the pack directory. Use separate checkouts for parallel local builds. GitHub Actions uses a fresh job per target and accepts an ID or `all`.

For a new target, add its BSP and boot/application defconfigs, then add an entry in `.ci/hardware-targets.json` with its platform, board, storage, radio, driver, capabilities, features, partition layout, toolchain prefix, and configuration checks. Use a distinct OTA compatibility ID and matching `RTCARPLAY_OTA_COMPAT` for each incompatible update domain. The existing CPIO writer covers the D213 OS/Rodata layout. A different layout or image format needs a corresponding implementation and verifier.

## Promote and validate

- **Development** retains verified build artifacts. `[ultra-dev]` builds never automatically publish.
- **Nightly** uses the rolling `nightly` release with both target ZIPs and target-named CPIOs. Existing RTL8733BS devices retain the `ota.cpio` URL.
- **Stable** uses a lightweight `firmware-v<version>` tag at the validated commit. Firmware version comes from the BSP image config. The manifest separately records the Luban-Lite, rt-carplay and Demo versions or identities.

Run **Promote verified firmware** with the successful all-target build run ID and evidence JSON keyed by target ID. Each entry supplies `status: PASS`, `img_sha256`, and an HTTPS `url` to the board report. An operator supplies evidence today. A future `artinchip-flash` HIL runner can use the same contract. Promotion checks hashes and target completeness and uses the original artifacts. Each target ZIP contains its IMG and matching `bootcfg.txt`.

The updater checks the CPIO compatibility ID before writing. RTL accepts the legacy `demo128-nand-carplay` ID; D40 accepts `d213-demo128-nand-carplay-aic8800d40`. The existing A/B updater rechecks the staged package, writes and reads back the inactive OS and Rodata, then requests the slot switch. Use the full IMG to install the first OTA-capable D40 build. Board acceptance covers first pairing, wired and wireless sessions, reconnect and AP restart, mutual OTA rejection, A→B and B→A updates, interrupted-write recovery, unconfirmed-boot rollback, and `/data` retention. CI records new images as `NOT_RUN` for hardware.
