# CarPlay SDK integration and dependencies

[简体中文](carplay-sdk-integration.zh-CN.md)

The ArtInChip Luban-Lite v1.3.3 reference is
`90a740fe9c1b663ea52ec3f7e1af35acde200aea`. The Receiver SDK, Demo/HMI, and
USB Ethernet driver are maintained in their own repositories. This checkout
owns configuration, ArtInChip adaptation, Bootloader and firmware delivery.
`application/rt-thread/rt-carplay-demo` builds the Receiver once from
`packages/third-party/rt-carplay`. The main D213 CarPlay profiles use Demo's
native NCM. The ipheth profile separately exercises the optional driver.

## Source and maintenance

| Component | Source and licensing | Maintenance decision |
| --- | --- | --- |
| CherryUSB | [boa-z/CherryUSB](https://github.com/boa-z/CherryUSB) submodule, Apache-2.0. The initial pin `a0054795c44037ace8a2b77b6e610adbff2257ea` matches the previously integrated upstream source tree `f36dac4d25d67f9553cdd68c27a74be70f33a9d7`. | The Gitlink fixes the source revision. The newer upstream head `fd76ba101d829fff80272a2038c5caa89de9b42d` was checked for the patch-specific behaviors below. |
| CherryUSB Luban port and legacy SDK stack | `cherryusb-luban` holds the SDK build, ArtInChip controller glue, the Baremetal OSAL, the SDK-only MSC sample, device helpers and legacy display assets. `cherryusb-1.0.0` is the original SDK snapshot. | Keep the legacy tree for existing SDK consumers. The main Kconfig enables the current overlay, and the legacy `LPKG_USING_CHERRYUSB_V1_0_0` entry is inactive. SDK-only assets and patches stay outside the submodule. |
| RT-Thread and lwIP | [RT-Thread](https://github.com/RT-Thread/rt-thread) 4.1.1, Apache-2.0, and [lwIP](https://github.com/lwip-tcpip/lwip) 2.1.2, BSD-3-Clause, from the SDK baseline. The translator derives from [Espressif lwIP](https://github.com/espressif/esp-lwip/tree/f5af4d8b1b4cd4585322f43833beff52b4a189ea) at `f5af4d8b1b4cd4585322f43833beff52b4a189ea`. | Keep the SDK ABI, with scoped fixes and RT-Thread port options. RT-Thread `v5.3.0` and lwIP `STABLE-2_2_1_RELEASE` would replace baseline interfaces and require a separate port and driver regression. |
| Mbed TLS and WebClient | [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 2.28.1 and [RT-Thread WebClient](https://github.com/RT-Thread-packages/webclient), Apache-2.0. Luban versions are fixed by the SDK baseline. WebClient carries a package version catalog and lacks an exact imported upstream revision. | Keep the receiver's Mbed TLS 2.x ABI and the ArtInChip crypto port. WebClient `2.2.0` was checked as the latest tag. The SDK's outward-interface binding, TLS trust setup, request behavior and log redaction need equivalent regressions before a switch. |
| Zephyr Bluetooth | [Zephyr](https://github.com/zephyrproject-rtos/zephyr) SDK snapshot reports 4.4.0, Apache-2.0 with retained third-party notices. | Keep the RT-Thread port, Realtek controller firmware and H:5 path, and D40 H:4 path. Zephyr `v4.5.0` is a separate port candidate. D40L has a separate WLAN configuration and does not select D40 Classic. |
| LVGL, MPP and ArtInChip BSP | [LVGL](https://github.com/lvgl/lvgl) 8.3.1 and 9.1.0, MIT, from the SDK baseline. BSP/MPP source and firmware retain the SDK's per-file notices. | Keep the ArtInChip display, touch, DMA, GE and H.264 integration. LVGL `v9.6.0` needs a separate ABI/display regression. |
| Receiver, Demo and ipheth | [rt-carplay](https://github.com/boa-z/rt-carplay) and [rt-carplay-demo](https://github.com/boa-z/rt-carplay-demo), GPL-3.0 family with their own notices. [cherryusb-ipheth](https://github.com/boa-z/cherryusb-ipheth), Apache-2.0 with BSD-licensed upstream portions. | `git submodule status` lists the pinned commits. Integration CI checks formatting of the pinned Receiver and Demo with clang-format 23.1.1. |

The SDK baseline pins the vendor source even where the vendor does not identify
an upstream commit. Its source must not be treated as a byte-for-byte snapshot
of the corresponding upstream tag.

## CherryUSB patches

All five patches apply to generated build sources against the pinned official
tree using exact patch context. The newer official head was checked for each
specific behavior.

| Patch | Requirement and upstream status |
| --- | --- |
| `0001-ehci-complete-short-bulk-in.patch` | Early short IN and errors must complete URBs, with descriptor reclamation and toggle capture ordered against DMA. The newer head still sets bulk IOC on the tail and returns from the scanner on an active qTD. Its unlink changes do not replace the integration's host regressions. |
| `0002-core-ep0-latency-probe.patch` | Propagate hub initialization failure and undo insertion of the failed bus. Upstream still discards the hub return value. The independent timing hooks now require `LPKG_CHERRYUSB_HOST_CONTROL_PROBE`; firmware CI rejects the option. |
| `0003-hub-baremetal-poll.patch` | Process hub work and timers from the Baremetal foreground loop. The newer upstream hub still creates an OS thread and lacks that polling contract. |
| `0004-aic-device-irq-contract.patch` | Adapt the upstream zero-argument device ISR to the SDK IRQ callback type. The newer upstream low-level AIC code still registers the conflicting declaration directly. |
| `0005-rtthread-null-mutex.patch` | Preserve the port's null-handle behavior for mutex take/give, which upstream still forwards directly to RT-Thread. Formerly an edit inside the vendor tree. |

## Retained SDK changes and validation

- **RT-Thread WLAN** retains work cancellation before freeing the lwIP
  protocol descriptor, guards against restarting detached timers, and keeps
  embedded workqueue objects intact. The generic object trace, registry reset,
  and stale-pointer timer diagnostics were removed. Test repeated AP/STA
  startup and teardown on each radio.
- **lwIP** retains IP input/forwarding, interface teardown and pbuf hooks for
  the opt-in NAPT path, plus DHCP options, interface routing, queued input,
  receive budgets, and SNTP response checks. `RTCARPLAY_INTERNET_SHARE`
  builds the translator and forwarding policy from the BSD-3-Clause
  `packages/third-party/lwip-napt` submodule. The RTL8733BS profile enables
  sharing with 128 entries and static port mapping disabled. The AIC8800D40
  profile leaves sharing disabled. The Demo owns WAN probing, DHCP/DNS offers
  and interface selection. The mDNS responder and extra timers remain opt-in.
- **Wireless and Bluetooth** retain the D40/D40L firmware selection, API
  adapters for AP/STA and vendor IEs, Realtek receive ownership, H:4/H:5
  transport handling, BR/EDR pairing persistence and settings registration.
  The Bluetooth host regressions cover these software contracts, while
  controller UART, pairing and persistence require hardware.
- **Crypto and HTTP** retain RV64 MPI limb selection, the invalid native CTR
  failure path, ECB-backed CTR and the TLS trust/interface policy. The host
  suite exercises CTR and 32-/64-bit MPI/pairing. Confirm TLS time and
  certificate handling against the board's actual network and update server.
- **Display, MPP and LVGL** retain the generic monotonic VSync event counter,
  H.264 missing-SPS handling, startup after display registration, asset
  decoders and single ownership of the touch controller. H.264 host
  regression and GUI/Headless builds cover software. Test A/V performance,
  Fusion, and touch on both boards.
- **Bootloader, targets and tooling** retain Baremetal USB Host/Device
  upgrade, per-target D213 BSP and image config, A/B slot recovery and OTA
  compatibility IDs. The firmware verifier checks layout, CRCs, bootcfg,
  CPIO and matching OS/Rodata. Check USB flash, interrupted-write recovery,
  rollback and persistent data on hardware.

Run `bash .ci/test-sdk.sh` after initializing the submodules and running
`sh packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh`. See the
[firmware guide](carplay-firmware.md) for complete build commands, target
registration and physical validation. The optional legacy USB Display and UVC
paths need their own compatibility assessment before using them with the
official snapshot. The D40L WLAN configuration is validated separately from
the D40 wireless CarPlay profile.

### Internet sharing

From an SDK checkout, run the Demo routing and supervisor regressions against
the pinned NAPT sources with:

```sh
git submodule update --init --recursive
cmake -S .ci/internet-share -B build/internet-share -DCMAKE_BUILD_TYPE=Debug
cmake --build build/internet-share --parallel
ctest --test-dir build/internet-share --output-on-failure
```

For board acceptance, record the image SHA256, board and iPhone/iOS version.
On RTL8733BS, check wired and wireless projection, audio/video and session
reconnect. With wireless projection active, check AP and STA connectivity,
DHCP, DNS, mDNS and local routes. Connect the configured WAN, run `ifconfig`,
`internet_share` and `carplay_connection status`, and verify a phone request
with cellular disabled. Confirm both NAPT counters and the WAN capture. Unplug
and reconnect WAN, then renew its address and check that sharing recovers
after a fresh probe. Repeat local connectivity and projection on AIC8800D40
with sharing disabled. See the [Demo validation procedure](../application/rt-thread/rt-carplay-demo/docs/internet-sharing.md#required-iphone-acceptance)
for capture details.
