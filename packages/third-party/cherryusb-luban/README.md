# Luban-Lite integration for official CherryUSB

`../cherryusb` is an unmodified snapshot of the official repository:

- https://github.com/cherry-embedded/CherryUSB
- master `a0054795c44037ace8a2b77b6e610adbff2257ea`
- VERSION 1.6.1, checked against remote master on 2026-11-10
- All 933 upstream tracked files were compared byte for byte.

SDK Kconfig, build policy, board startup, cache/interrupt glue and retained
SDK-only device helpers live here, outside the upstream snapshot. Existing
LPKG symbols, including the hidden V1_5_0 compatibility symbol, are retained
so existing defconfigs load; they do not identify the source version anymore.
The upstream SConscript uses PKG symbols and does not build a second stack.

## Verified target

Branch `poc/carplay-oss`, configuration
`d21x_demo128-nand_rt-thread_rtcarplay_defconfig`, RT-Thread/lwIP, D213
USB host EHCI with OHCI companion source. Full firmware compilation and image
packaging passed. Both external USBMUX and IPHETH class records survive linking.
IPHETH's host ASan/UBSan tests include the real upstream EP0 control function.
This image has not yet been validated on hardware after the stack replacement.

The application now implements upstream's
`usbh_get_hport_active_config_index()` hook. Control transfer returns data bytes,
excluding the eight Setup bytes accounted for by the HCD. This corrects the old
SDK API mismatch that caused IPHETH initialization to reject successful requests.
No Apple-specific behavior is patched into the official core.

## Porting limits

The previous precompiled EHCI ISO/OHCI binaries have been removed: their ABI
must not be mixed with the new core. EHCI and OHCI use official source instead.
EHCI descriptor cache maintenance is enabled; AIC cache hooks and board clock,
reset and IRQ policy remain in this overlay.

Current verification covers the selected host target. Other board configs,
USB device helpers/templates, OTG role switching, isochronous audio/video and
optional class integrations require separate compilation/board validation.
Old SDK-specific helpers are retained as source references, not claimed ported.
Missing selected upstream sources fail the build explicitly rather than being
silently omitted; legacy UVC binary adapters are explicitly rejected. A host
ISO streaming port must be completed before enabling those features.

## Reproduce

From SDK root, with sibling `rt-carplay` and `cherryusb-ipheth` checkouts:

```sh
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons -j8
```

For upstream updates, export the pinned official tree into `../cherryusb`, keep
this overlay separate, verify APIs and controller/OSAL ABI, and rerun host and
complete SDK builds. Preserve upstream licensing and source notices.

## EHCI short-IN completion correction (v6)

The official source snapshot is still unchanged. The build applies
`patches/0001-ehci-complete-short-bulk-in.patch` to a generated copy under
`generated/` and compiles that copy. The source and patch are explicit SCons
dependencies; patch application uses zero fuzz and fails on mismatched context.
The delta is a general EHCI correction, separate from IPHETH and USBMUX.

Evidence: a 64 KiB bulk IN URB creates four 16 KiB qTDs. A short packet in an
early qTD follows its terminating alternate link, leaving later qTDs Active.
The original scanner waits for those unreachable qTDs, and only the final qTD
has IOC. This can leave the URB pending forever. Each bulk IN qTD now has IOC;
the scanner recognizes terminal short bulk IN/errors without waiting for the
tail, counts only completed descriptors, and refreshes the queue-head overlay
before reading the next data toggle. Short control data still waits for its
mandatory status stage. The 64 KiB receive size and NCM protocol stay intact.

Reproduce the original defect and run patched ASan/UBSan regressions:

```sh
packages/third-party/cherryusb-luban/tests/run.sh
```

Tests compile the actual original/patched HCD, build descriptor chains and
simulate DMA results/MMIO. They cover 4-byte early IN, zero-byte early/middle
IN, full IN/OUT, incomplete active chains, halted first qTD, data toggle,
cache hook execution and short control data/status. The host executable uses
non-PIE low addresses for EHCI's 32-bit link fields, and replaces the upstream
signed `(1 << 31)` test mask with its equivalent unsigned spelling for UBSan.
This is a software reproduction; v6 still requires board confirmation and USB
captures before attributing every missing RX completion to this defect.

This unified patch and its reproduction are prepared for upstream review;
no upstream PR has been opened. No pairing or application protocol changes
are part of the controller fix.

## Read-only EHCI snapshot (v7)

The v6 board still reports one pending RX and no completion. The short-IN
software reproduction remains valid, but has not been shown to explain that
board's stall. Do not treat LINK_DOWN as a trust diagnosis.

With FINSH enabled, `usb_ehci_status [bus]` snapshots registers, the frame index,
IRQ count, active queue heads and up to eight qTDs per URB. Source pointers are
checked against the descriptor pool before walking, and walks are bounded.
State is copied with interrupts disabled, then printed after restoring them;
DMA may still update hardware state during the snapshot. The command never
writes MMIO or descriptors, acknowledges IRQs, cancels URBs, or sends requests.
Cache invalidation is needed to read DMA-owned descriptors. IRQ counters only
count ISR entries, not individual transactions or received bytes. Active
unchanged descriptors alone cannot prove a NAK response; USB captures are
needed for handshake-level evidence.

Capture this sequence after iPhone enumeration:

```text
ipheth_status
usb_ehci_status 1
carplay_usb desc
usb_ehci_status 1
ipheth_status
```

`carplay_usb desc` performs read-only descriptor control requests through the
application; omit the optional request argument. This provides known control
traffic between snapshots without touching IPHETH's RX URB. Compare controller
RUN/async enable/status, frame progress and IRQ count, then endpoint 86's queue
head overlay and qTD remaining lengths. No new Ethernet or pairing behavior
is enabled by these diagnostics.

The host test includes the actual snapshot collector, checks that MMIO and
hardware tokens are unchanged, and tests invalid pointer rejection, cyclic
chain truncation and inactive/invalid bus handling under ASan/UBSan.

## Snapshot before cancellation (v13)

With FINSH enabled, the overlay also exports
`usbh_ehci_transfer_snapshot(struct usbh_urb *, int reason)` for an optional
application diagnostic hook. It invokes the same bounded read-only collector
before a timed-out URB is cancelled and its QH reclaimed. It changes no USB
request, endpoint alternate, scheduling bit or HCD error policy. The caller
must be in task context; logging and allocation are not ISR-safe. The snapshot
is observational and the controller may complete the URB during UART output.
The caller must account for that race before cancellation and cleanup.

The host regression calls the hook with a pending URB and verifies unchanged
registers and tokens, along with NULL-input handling. The upstream and patched
HCD test variants both pass ASan/UBSan. This adds diagnostic visibility, not a
claim that the remaining iPhone partial-reply failure has been fixed.

## v25 optional EP0 probe and predecessor cache hazard reproduction

`0002-core-ep0-latency-probe.patch` builds a generated `usbh_core.c` with two
optional weak hooks around the actual control-transfer mutex. Without hooks it
preserves transfer behavior. The CarPlay port supplies the wrapping ms clock
and records Carrier GET wait/hold durations; UART reporting runs elsewhere.
Timeouts, retries, locking, DMA and cancellation are unchanged. Run:

```sh
sh packages/third-party/cherryusb-luban/tests/run_control_probe.sh
```

The existing HCD host test now also uses separate CPU/DMA QH images. Executing
`ehci_qh_remove` reproduces stale predecessor overlay/token/current-qTD writeback
when its horizontal link cache line is cleaned while DMA has progressed. Both
original and generated short-IN variants retain this hazard. This demonstrates
a cache-consistency defect under the modeled interleaving, not its occurrence
on this board. V25 deliberately does not change HCD behavior during the Carrier
A/B experiment. A complete fix needs coherent/uncached descriptor access or
verified schedule quiescence; invalidate-before-clean alone leaves a DMA race.
