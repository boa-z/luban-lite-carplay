#!/usr/bin/env python3
"""Compile the actual SDK keys_br.c against bounded host dependency stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

package = Path(__file__).resolve().parents[1]
source = package / "subsys/bluetooth/host/classic/keys_br.c"
headers = ["zephyr/kernel.h", "zephyr/sys/atomic.h", "zephyr/sys/util.h",
           "zephyr/bluetooth/bluetooth.h", "zephyr/bluetooth/conn.h",
           "zephyr/bluetooth/hci.h", "zephyr/settings/settings.h",
           "zephyr/logging/log.h", "common/bt_str.h", "host/hci_core.h",
           "host/settings.h", "host/keys.h"]
with tempfile.TemporaryDirectory(prefix="carplay-br-keys-") as directory:
    root = Path(directory)
    for header in headers:
        path = root / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("/* Dependency supplied by test_br_keys.c. */\n")
    for capacity in (1, 4):
        binary = root / f"keys-{capacity}"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", f"-I{root}",
                        f'-DBR_KEYS_SOURCE="{source}"', f"-DCONFIG_BT_MAX_PAIRED={capacity}",
                        str(package / "tests/test_br_keys.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
