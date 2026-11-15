#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reject a packaged GUI build with competing USB network implementations."""
from pathlib import Path
import subprocess
import sys


def check(config, elf, nm):
    values = set(Path(config).read_text().splitlines())
    required = {'CONFIG_RTCARPLAY_NATIVE_NCM=y', 'CONFIG_RTCARPLAY_WIRED_NETIF="n0"'}
    forbidden = {'CONFIG_' + name + '=y' for name in (
        'LPKG_USING_CHERRYUSB_IPHETH', 'LPKG_CHERRYUSB_HOST_NCM',
        'RTCARPLAY_NCM_TRACE', 'RTCARPLAY_WIRED_PROTOCOL_TRACE')}
    missing = required - values
    enabled = forbidden & values
    if missing or enabled:
        raise ValueError('Invalid wired configuration: missing={}, enabled={}'.format(
            sorted(missing), sorted(enabled)))
    output = subprocess.check_output([nm, '--defined-only', elf], text=True)
    symbols = {line.split()[-1] for line in output.splitlines() if line.split()}
    required = {'carplay_native_ncm_class_info', 'carplay_native_ncm_interface',
                'rtcp_lubanlite_wired_mac', 'usbh_ehci_rx_complete_probe'}
    forbidden = {'ipheth_class_info', 'usbh_ipheth_send', 'usbh_ipheth_host_ready',
                 'usbh_ipheth_session_link', 'cdc_ncm_class_info'}
    if required - symbols or forbidden & symbols:
        raise ValueError('Invalid wired ELF: missing={}, competing={}'.format(
            sorted(required - symbols), sorted(forbidden & symbols)))
    print('PASS: exclusive native NCM, n0 identity, short-IN fix; diagnostic tracing disabled')
    demo_enabled = 'CONFIG_RTCARPLAY_VEHICLE_PROTOCOL_DEMO=y' in values
    demo_symbol = 'carplay_vehicle_protocol_demo_create'
    if demo_enabled:
        required = {demo_symbol, 'carplay_vehicle_protocol_demo_tick',
                    'rtcp_vehicle_handle', 'rtcp_vehicle_notification'}
        if required - symbols:
            raise ValueError('Vehicle State bench missing from ELF: {}'.format(sorted(required - symbols)))
        print('PASS: SIMULATED engine Vehicle State bench compiled and linked')
    elif demo_symbol in symbols:
        raise ValueError('Classic artifact unexpectedly contains the Vehicle State bench')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit('Usage: check-wired-profile.py CONFIG ELF NM')
    check(*sys.argv[1:])
