#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate the board's SDK CPIO package before authorizing it for OTA."""
import hashlib
import json
import re
import sys
from pathlib import Path


def validate_package(data):
    if not 512 <= len(data) <= 8 * 1024 * 1024:
        raise ValueError("OTA package size is outside the supported range")
    expected = [("ota_info.bin", 512), ("d21x_os.itb", 8 * 1024 * 1024),
                ("rodata.fatfs", 12 * 1024 * 1024), ("TRAILER!!!", 0)]
    offset = 0
    payloads = {}
    for name, maximum in expected:
        header = data[offset:offset + 110]
        if len(header) != 110 or header[:6] != b"070702":
            raise ValueError("Expected complete CRC CPIO header")
        fields = [int(header[i:i + 8], 16) for i in range(6, 110, 8)]
        size, namesize, checksum = fields[6], fields[11], fields[12]
        if namesize != len(name) + 1 or size > maximum or (maximum and not size):
            raise ValueError("Unexpected CPIO member size")
        start = offset + 110
        if data[start:start + namesize] != name.encode() + b"\0":
            raise ValueError("Unexpected, duplicate or reordered CPIO member")
        start = (start + namesize + 3) & ~3
        end = start + size
        if end > len(data):
            raise ValueError("Truncated CPIO payload")
        payload = data[start:end]
        # The SDK patches size in ota_info.bin after cpio generation, without
        # updating its old CPIO checksum. Validate its exact mapping below.
        if name not in ("ota_info.bin", "TRAILER!!!") and sum(payload) & 0xffffffff != checksum:
            raise ValueError("CPIO payload checksum mismatch")
        if name != "TRAILER!!!" and fields[1] & 0xf000 != 0x8000:
            raise ValueError("Only regular firmware files are supported")
        payloads[name] = payload
        offset = (end + 3) & ~3
    if any(data[offset:]):
        raise ValueError("Unexpected bytes after CPIO trailer")
    info = payloads["ota_info.bin"]
    if len(info) != 512:
        raise ValueError("Invalid OTA metadata size")
    lines = info[4:].split(b"\0")
    if len(lines) < 8 or lines[0] != b"[image]" or lines[3:7] != [
            b"[file]", b"ota_info.bin:file;", b"d21x_os.itb:os;", b"rodata.fatfs:rodata;"]:
        raise ValueError("OTA metadata must target only OS and rodata")
    match = re.fullmatch(rb'size = "([0-9]+) *";', lines[1])
    if not match or int(match[1]) != len(data):
        raise ValueError("OTA metadata length mismatch")
    if not re.fullmatch(rb'version = "[0-9]+(?:\.[0-9]+){1,3}";', lines[2]):
        raise ValueError("Invalid OTA metadata version")
    if any(byte not in (0, 255) for line in lines[7:] for byte in line):
        raise ValueError("Unexpected OTA metadata directives")
    return lines[2].split(b'"')[1].decode("ascii")


def manifest(config, commit, package):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected full source commit")
    data = Path(package).read_bytes()
    version = validate_package(data)
    text = "".join(line.split("//")[0].strip() for line in Path(config).read_text().splitlines())
    cfg = json.loads(text.replace(",}", "}").replace(",]", "]"))
    if cfg["image"]["info"]["version"] != version:
        raise ValueError("Package and image versions differ")
    return "\n".join(("version=" + version, "commit=" + commit,
                      "board=d21x_demo128-nand-carplay", "size=" + str(len(data)),
                      "sha256=" + hashlib.sha256(data).hexdigest())) + "\n"


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: ota-manifest.py image_cfg.json commit ota.cpio")
    print(manifest(*sys.argv[1:]), end="")
