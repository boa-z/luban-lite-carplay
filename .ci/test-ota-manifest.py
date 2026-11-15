#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import runpy
import unittest
from pathlib import Path

validate = runpy.run_path(str(Path(__file__).with_name("ota-manifest.py")))["validate_package"]


def member(name, payload, checksum=None):
    fields = [1, 0o100644, 0, 0, 1, 0, len(payload), 0, 0, 0, 0, len(name) + 1,
              sum(payload) if checksum is None else checksum]
    header = b"070702" + "".join("%08x" % f for f in fields).encode()
    data = header + name.encode() + b"\0"
    data += b"\0" * (-len(data) % 4)
    data += payload
    return data + b"\0" * (-len(data) % 4)


def package(mapping=b"rodata.fatfs:rodata;", extra=b""):
    def metadata(size):
        text = b'[image]\0size = "%d";\0version = "1.0.0";\0[file]\0ota_info.bin:file;\0d21x_os.itb:os;\0' % size + mapping + b"\0"
        return b"\0" * 4 + text + b"\xff" * (508 - len(text))
    tail = member("d21x_os.itb", b"kernel") + member("rodata.fatfs", b"resources") + extra + member("TRAILER!!!", b"")
    size = len(member("ota_info.bin", metadata(0))) + len(tail)
    return member("ota_info.bin", metadata(size)) + tail


class ManifestTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(validate(package()), "1.0.0")

    def test_missing_trailer(self):
        with self.assertRaises(ValueError):
            validate(package()[:-124])

    def test_payload_corruption(self):
        with self.assertRaises(ValueError):
            validate(package().replace(b"kernel", b"damage"))

    def test_active_or_data_mapping(self):
        for mapping in (b"rodata.fatfs:data;", b"rodata.fatfs:env;", b"rodata.fatfs:rodata_r;"):
            with self.assertRaises(ValueError):
                validate(package(mapping=mapping))

    def test_duplicate_member(self):
        with self.assertRaises(ValueError):
            validate(package(extra=member("d21x_os.itb", b"duplicate")))

    def test_unexpected_data_member(self):
        with self.assertRaises(ValueError):
            validate(package(extra=member("data.fatfs", b"forbidden")))

    def test_trailing_garbage(self):
        with self.assertRaises(ValueError):
            validate(package() + b"unexpected")


if __name__ == "__main__":
    unittest.main()
