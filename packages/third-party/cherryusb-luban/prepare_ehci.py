# SPDX-License-Identifier: Apache-2.0
"""Apply the reviewed EHCI delta to a build copy, keeping upstream pristine."""
from pathlib import Path
import subprocess
import sys
import tempfile

def prepare(original, delta, output):
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    # SDK snapshots can contain CRLF blobs even on Linux. Patch exact content
    # with a stable LF representation, independent of Git autocrlf settings.
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / 'source.c'
        patch = Path(temp) / 'delta.patch'
        source.write_bytes(Path(original).read_bytes().replace(b'\r\n', b'\n'))
        patch.write_bytes(Path(delta).read_bytes().replace(b'\r\n', b'\n'))
        subprocess.run(['patch', '--batch', '--fuzz=0', '--output', str(output),
                        str(source), str(patch)], check=True)

if __name__ == '__main__':
    prepare(*sys.argv[1:])
