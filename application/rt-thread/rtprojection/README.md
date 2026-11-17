# Mobility Cockpit / Android Auto development app

`rtprojection` is the independent application for the shared projection demo.
This checkpoint enables the wired Android Auto backend. CarPlay and CarLife
cards are disabled in this app; switching those protocols in the same firmware
is future work. The existing `rtcarplay` application remains available.
There is no separate `rtandroidauto` app or defconfig.

## Build

```sh
git submodule update --init packages/third-party/rt-androidauto
scons --apply-def=d21x_demo128-nand_rt-thread_rtprojection_defconfig
scons -j8
```

The profile uses D213, the existing `demo128-nand-carplay` board, USB1 phone
host, GT911, LVGL 9, MPP H.264, sound0 and the platform mbedTLS 2.28.1 owner.
The image is in `output/d21x_demo128-nand_rt-thread_rtprojection/images/`.
Its product filename retains the existing board pack's `carplay` name.
Build the corresponding baremetal bootloader first if its board pack is absent.

## Shared UI ownership

`../rtcarplay/ui/demo_dashboard.c` contains the existing launcher dashboard,
extracted with its layout, fonts, icons, colours and navigation geometry.
Both the original `lvgl_launcher.c` and the shared `demo_ui.c` call
`demo_dashboard_build()`. Changes to the protocol do not require another copy
of the home screen. Dashboard values are explicitly simulated.

`demo_ui.h` defines a protocol-neutral callback interface. `app/projection_demo.c`
adapts Android Auto to it; `backends/androidauto/` owns USB, session, video and
audio integration. The UI does not include either receiver SDK's headers.
Display, RTC, backlight and volume adapters are shared with the existing app.

The existing CarPlay app can opt into this same UI with
`CONFIG_RTCARPLAY_SHARED_DEMO_UI=y`; its default remains the existing launcher.
The opt-in adapter enables only CarPlay and Local. It does not add Android Auto
to the CarPlay image or modify the `rt-carplay` protocol implementation.

## Credentials and operation

Provision the deployment's TLS client identity and expected peer certificate
fingerprint under `/data/androidauto/` before starting a session:

| File | Contents |
| --- | --- |
| `client.pem` | Client certificate in PEM form |
| `client.key` | Matching private key in PEM form |
| `peer.sha256` | Exactly 32 raw SHA-256 fingerprint bytes, not hexadecimal text |

No default identity, private key or trust-all policy is provided. Missing or
invalid credentials stop the session. See the SDK's security documentation
before provisioning credentials; phone interoperability remains unverified.

Select Android Auto on Connectivity, or use the console:

```text
projection androidauto
projection status
projection fusion
projection fullscreen
projection local
```

Fullscreen hides the local UI layer; use `projection local` to leave it.
Fusion keeps a local instrument strip. Leaving the phone region releases the
last captured touch; local strip touches are not forwarded to the receiver.

The policy waits for the previous backend to acknowledge retirement before
granting ownership. Layout changes also retire the old session. A failed
video/audio stop retains resources for cleanup rather than freeing memory
still potentially owned by DMA. Session retries are bounded; security and
protocol errors require an explicit restart after correction.

## Navigation and phone status

`androidauto_info` displays the most recent navigation and phone status updates.
The app advertises separate semantic-navigation and phone-status services. Its
worker starts the navigation subscription once that channel opens, and starts
again after channel reopen. This diagnostic consumer retains real incoming
updates; it does not draw another dashboard or claim a graphical cluster UI.

The adapter deep-copies all strings from the SDK's borrowed views, preserving
optional fields, lane/road/destination data, required call fields and int64 time.
Console snapshots rebase their own pointers and remain valid if the session
closes while printing. Channel retirement clears the associated live cache.

The SDK exposes explicit phone cluster input, but the demo does not send call
actions automatically or claim working HFP call audio. Contact photos and legacy
navigation icons are skipped. Legacy navigation 0x8005 and energy forecast
0x8008 remain unsupported. See `docs/navigation-phone.md` in the SDK for exact
bounds, directions and reference evidence.

A host ownership test compiles the production adapter with explicit RT-Thread
stubs. From the project root with GCC:

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iapplication/rt-thread/rtprojection/tests/status_host \
  -Ipackages/third-party/rt-androidauto/include \
  application/rt-thread/rtprojection/tests/test_androidauto_status.c \
  -o test_androidauto_status
./test_androidauto_status
```

Linux additionally uses `-fsanitize=address,undefined -fno-omit-frame-pointer`.
The test checks maximum text capacity, borrowed-buffer expiry, snapshot
relocation and reset. IRQ timing, scheduling and hardware behavior are not
simulated by these stubs.

## Validation and remaining work

Software checkpoint: 2026-11-17. SDK gitlink:
`bc83e2c0762ddb2f96bf28a9ab2a98bb58f7d3b5`.
Its Windows and Linux CI passed:
https://github.com/boa-z/rt-androidauto/actions/runs/44081425601

This SDK adds navigation focus, voice/battery notifications, call availability
output, legacy service close/reopen, fragmented microphone TX, multiple TLS
records per AAP fragment, configurable 32-key/rotary input and sensor scheduler
callbacks. Its independent protobuf oracle checks 27 fixtures; Windows and Linux
ASan/UBSan tests pass 5/5. See the SDK's `docs/wired-extensions.md` for exact scope.
The demo grants projected navigation focus because it has no native navigator;
adding native navigation requires a real arbitration policy. The dashboard is
unchanged. The optional media-info channel now receives phone playback status
and text metadata. Use `androidauto_media` in the console to inspect the latest
individual status and metadata updates; missing fields remain absent, and
channel/session retirement clears both records. A notification without a field
does not fabricate a value. The console prints nothing for an absent field.
Each text field is bounded to 255 bytes. Album art, uncertain metadata fields
and instrument-cluster interaction are unsupported; see the SDK's
`docs/media-information.md`. This uses a separate dynamic service descriptor,
not the PCM audio channel. `RTANDROIDAUTO_PROTOCOL_MINOR` defaults to 1; a higher negotiation
ceiling does not imply full modern protocol support or phone compatibility.

- The Android Auto app cross-compiles and packages an ELF and NAND image.
- At the preceding UI integration checkpoint, the opt-in CarPlay shared UI
  profile packaged an image and the existing CarPlay Host `make check` passed.
- Real LVGL host checks cover shared UI rendering, backend selection,
  touch capture/release, layout changes and display failure recovery.
- Policy host checks cover startup cleanup, unsupported selections, contention,
  timeout without forced ownership, late retirement and tick rollover.
- The original launcher's dark English dashboard before/after extraction has
  zero changed pixels at 1024 x 600. The independent shared preview differs
  only by one intensity step in 23 antialiased settings-icon pixels.
- Logs and screenshots are retained locally in
  `output/androidauto-integration/`; generated firmware is not committed.
  `validation.json` records results and the Android Auto ELF/image hashes.

Android phone, D213 board, USB DMA/cache behavior, audio quality and Google DHU
acceptance are **NOT_RUN**. Wireless Android Auto, microphone capture, AEC,
same-image CarPlay/Android Auto switching and hardware acceptance remain open.
This is a development integration, not a declaration of phone compatibility.
