#!/usr/bin/env python3
"""Exercise actual H5 framing, RX, TX and retry functions with a mock UART."""
from pathlib import Path
import os
import subprocess
import tempfile

bt = Path(__file__).resolve().parents[1]
text = (bt / 'drivers/bluetooth/hci/h5.c').read_text()
# Compile the production state, framing, parser and worker functions unchanged.
start = text.index('#define HCI_3WIRE_ACK_PKT')
end = text.index('static void h5_init(')
def function(path, signature):
    content = path.read_text()
    a = content.index(signature)
    b = content.index('{', a) + 1
    depth = 1
    while depth:
        depth += (content[b] == '{') - (content[b] == '}')
        b += 1
    return content[a:b]

source = (bt / 'tests/test_h5.c').read_text().replace('/* PRODUCTION_H5 */', text[start:end])
source = source.replace('/* PRODUCTION_ACL_ALLOC */', function(
    bt / 'subsys/bluetooth/host/buf.c', 'struct net_buf *bt_buf_get_rx('))
source = source.replace('/* PRODUCTION_ACL_CREDITS */', function(
    bt / 'subsys/bluetooth/host/hci_core.c', 'void bt_hci_host_num_completed_packets('))
with tempfile.TemporaryDirectory(prefix='rtcp-h5-') as directory:
    c = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    c.write_text(source)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-function', '-pthread',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
