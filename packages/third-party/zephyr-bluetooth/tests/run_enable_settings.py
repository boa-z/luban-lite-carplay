#!/usr/bin/env python3
"""Isolate actual bt_enable() for its pre-HCI settings failure/retry path."""
import os
from pathlib import Path
import subprocess
import tempfile

package = Path(__file__).resolve().parents[1]
source = (package / "subsys/bluetooth/host/hci_core.c").read_text()
start = source.index("int bt_enable(bt_ready_cb_t cb)\n{")
end = source.index("\nint bt_disable(void)", start)
with tempfile.TemporaryDirectory(prefix="carplay-bt-enable-") as directory:
    root = Path(directory)
    isolated = root / "bt_enable.c"
    isolated.write_text(source[start:end])
    binary = root / "enable-settings"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined",
                    f'-DBT_ENABLE_SOURCE="{isolated}"',
                    str(package / "tests/test_enable_settings.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
