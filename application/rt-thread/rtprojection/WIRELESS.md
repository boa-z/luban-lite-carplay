# Wireless Android Auto development profile

This profile connects the portable RFCOMM bootstrap to the SDK's existing
Zephyr Classic/RTL8733BS H5 host, Realtek WLAN framework and lwIP TCP stack.
The original shared `rtcarplay` dashboard is unchanged. No `rt-carplay` protocol
core is linked. It is a software integration checkpoint; no Android phone,
D213 radio, HFP, DHCP over the air or DHU acceptance has run.

## Build and provisioning

```sh
git submodule update --init packages/third-party/rt-androidauto
scons --apply-def=d21x_demo128-nand_rt-thread_rtprojection_wireless_defconfig
scons -j8
```

Output: `output/d21x_demo128-nand_rt-thread_rtprojection_wireless/images/`.
This separate profile selects wireless at boot, leaves the receiver unarmed,
and disables the AOA USB adapter. The original `rtprojection_defconfig` remains
the wired profile. The hardware route is the existing `demo128-nand-carplay`
board with RTL8733BS UART7 H5 and SDIO Wi-Fi. Build the matching bootloader if
the board pack is absent.

Provision the same `client.pem`, `client.key` and raw 32-byte `peer.sha256`
files described in README.md. Under that same directory, default
`/data/androidauto`, add `wireless.conf` containing exactly three nonempty lines:

1. Your AP SSID, 1–32 bytes.
2. Your independently generated WPA2 passphrase, 8–63 printable ASCII bytes.
3. A locally permitted 5 GHz channel: `36`, `40`, `44` or `48`.

LF and CRLF are accepted. There are no compiled-in credentials, automatic
secret generation, fallback BSSID, test TLS identity or trust-all mode. The
configuration file and private keys must not be committed. Ensure `/data` is
mounted before pairing; the shared DFS settings engine stores AA bonds at
`/data/androidauto/bt_settings.bin`, separate from CarPlay's bond file.
The storage engine can operate in RAM when storage fails, so inspect its logs
before relying on bond persistence across a reboot.

## Operation and pairing

Use the unchanged Android Auto card, or the serial console:

```text
androidauto_link status
androidauto start
androidauto_pair status
```

The console reports a request token and six-digit numeric-comparison key.
Compare it with the phone, then explicitly confirm that current request:

```text
androidauto_pair <token> yes
```

Use `no` to reject. Tokens expire in 30 seconds; stale tokens are rejected.
Only authenticated numeric comparison is accepted in this development profile;
Just Works consent is rejected. RFCOMM requires Bluetooth security level L3
and the connected callback checks that level before releasing AP credentials.
There is no change to the dashboard or its settings/pairing presentation.

The worker starts a WPA2 AP through the RT-Thread WLAN framework, obtains the
real AP netif IPv4/BSSID, starts DHCP, and binds port 5288 to that AP address.
An already active AP owned by another application is rejected and left intact.
Only then does it publish the AA SDP service and EIR UUID
`4de17a00-52cb-11e6-bdf4-0800200c9a66`. It advertises no iAP2/CarPlay UUID.

After the phone requests Wi-Fi information, the bootstrap sends SSID, key,
BSSID, WPA2 security and dynamic-AP type using the exact supported dialect.
Raw phone statuses are logged without assuming that status 0 or 1 means
success. A real accepted TCP stream is passed to the existing AAP receiver;
its provisioned TLS peer pin must still pass before projection is established.
An RFCOMM disconnect after credentials were written permits TCP to arrive
within the existing deadline; explicit rejection and security/protocol errors
still fail the attempt. The peer cannot redirect the listener via a response.

```text
androidauto status
androidauto stop
```

Stop also retires a pending bootstrap with no AAP receiver. The owner cancels
and closes RFCOMM/TCP, closes the listener, stops its AP and erases in-memory
AP credentials. Resources whose driver stop has not completed do not report
retired and cannot be reused prematurely. TCP I/O uses nonblocking sockets and
bounded select calls; RFCOMM PDU allocation is nonblocking. The application
worker owns all session and media lifecycle calls. Stack callbacks only copy
bounded input or publish status. AP/BT controller initialization itself uses
the existing SDK driver/host initialization APIs and still needs board validation.

AP readiness has a 15-second limit, bootstrap a 45-second total deadline,
and the outer AP/listener attempt a 60-second total deadline. Application retries
remain bounded to three attempts. Port reuse supports a new attempt following
a prior TCP connection. The bootstrap and transport clocks tolerate RTOS tick
rollover. Stopping fails visibly rather than forcing reuse of an unreleased DLC.

When a custom build enables both transports, use `androidauto_link usb` or
`androidauto_link wireless` only after `androidauto stop` reports retired.
The selector rejects an adapter absent from the image. The stock wireless
profile contains only wireless; the stock wired profile contains only USB.

## Validation and remaining work

The portable SDK implements the explicit Dongle start/info/credentials dialect,
raw status, ping and rejection. Its `docs/wireless.md` records the pinned
reference evidence and conflicting layouts/IDs that were not combined.
Modern version/setup-info messages (4/5/11) fail explicitly as unsupported.

**Genuine HFP hands-free integration is not implemented or advertised.** Some
phones require a real HFP connection to initiate wireless Android Auto; those
phones may never open this RFCOMM service. The SDK has HFP headers and source,
but its build integration, SLC handling and real call-audio routing must be
completed and validated. No dummy HFP advertisement or invented success
response is used. Automatic outbound bonded-peer paging and full modern
wireless negotiation are also pending. This profile does not establish phone
compatibility or complete consumer wireless behavior.

The production network adapter has a POSIX host test with explicit WLAN and
Bluetooth API mocks, linked to the real portable bootstrap. From a Linux
checkout with the sibling SDK sanitizer archive already built:

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iapplication/rt-thread/rtprojection/tests/wireless_host \
  -Ipackages/third-party/rt-androidauto/include \
  application/rt-thread/rtprojection/tests/test_androidauto_wireless.c \
  ../rt-androidauto/build-linux/librtaa.a -o /tmp/test-aa-wireless
/tmp/test-aa-wireless
```

It exercises real TCP listener/accept, 32 KiB send/receive, idle timeout,
cancel/close, immediate restart, RFCOMM close before TCP, pending-bootstrap stop,
total deadline, partial AP-start failure, AP-stop retry, foreign AP preservation,
tick rollover and invalid provisioning. Radio, pairing, bond persistence,
DHCP, real lwIP scheduling and full TCP→phone AAP/TLS are **not** simulated by
these host mocks. See `validation.json` for separate firmware and SDK evidence.
