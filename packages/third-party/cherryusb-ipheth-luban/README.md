# cherryusb-ipheth — Luban-Lite integration

Luban-Lite glue for the bundled [`cherryusb-ipheth`](./../cherryusb-ipheth)
component, an independent Apple iPhone USB Ethernet (IPHETH) class driver with
an RT-Thread/lwIP network adapter.

The component source is a git submodule at
`packages/third-party/cherryusb-ipheth`.  It is not a directory of this SDK
package: this package only provides the Kconfig symbol and the SCons entry
point, so the component can be updated by moving the submodule pointer.

| Item | Value |
| --- | --- |
| Submodule path | `packages/third-party/cherryusb-ipheth` |
| Upstream | https://github.com/boa-z/cherryusb-ipheth.git |
| Pin | tracked by the SDK gitlink; see `git submodule status` |
| License | Apache-2.0, with BSD-2-Clause/BSD-3-Clause parts (see the submodule `LICENSE`/`LICENSES`) |

## Fetching

The source is not fetched by a plain clone.  Initialise it from the SDK root:

```sh
git submodule update --init packages/third-party/cherryusb-ipheth
```

`CONFIG_LPKG_CHERRYUSB_IPHETH_PATH` defaults to the bundled path (relative to
the SDK root).  Keep the default for the self-contained build, or point it at
an absolute path to build against another checkout.

## Configuration

`LPKG_USING_CHERRYUSB_IPHETH` builds the class/core and the RT-Thread network
adapter.  It does not choose a USB configuration or implement pairing;
USBMUX and the application own those policies.

`RTCARPLAY_WIRED_NETIF="i0"` makes the application query the public Ethernet
interface.  Use the USB1 host controller and enable `RT_USING_LWIP_IPV6` for
Link-Local testing.

Build the combined firmware with:

```sh
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons -j8
```

On board, run `carplay_usb start` before inserting the phone, then query
`ipheth_status`, `ifconfig`, and `carplay_usb net`.  The standalone driver also
works without the CarPlay application when the integrating host selects a
suitable USB configuration.  Protocol and host-test detail lives in the
submodule documentation.
