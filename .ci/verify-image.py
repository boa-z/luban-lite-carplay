#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the USB image and match the SDK's CPIO payloads to it."""
import argparse
import pathlib
import struct
import zlib


def verify(path, archive_path=None, platform="d21x", bootcfg=None, compat=None):
    data = pathlib.Path(path).read_bytes()
    if len(data) < 2048 or data[:8] != b'AIC.FW\0\0':
        raise ValueError('invalid ArtInChip IMG header')
    if data[8:72].rstrip(b'\0') != platform.encode("ascii"):
        raise ValueError('unexpected firmware platform')
    meta_offset, meta_size, file_offset, file_size = struct.unpack_from('<4I', data, 332)
    if (meta_offset != 2048 or not meta_size or meta_size % 512 or
            file_offset != meta_offset + meta_size or
            file_offset + file_size != len(data)):
        raise ValueError('invalid IMG area bounds')
    components = {}
    locations = {}
    for position in range(meta_offset, file_offset, 512):
        meta = data[position:position + 512]
        if meta[:8] != b'META\0\0\0\0':
            if len(set(meta)) == 1 and meta[0] in (0, 255):
                continue
            raise ValueError('invalid component metadata')
        name = meta[8:72].rstrip(b'\0').decode('ascii')
        offset, size, crc = struct.unpack_from('<3I', meta, 136)
        if not size or offset + size > len(data):
            raise ValueError('component outside image: ' + name)
        payload = data[offset:offset + size]
        if zlib.crc32(payload) != crc:
            raise ValueError('component CRC mismatch: ' + name)
        if name in components:
            raise ValueError('duplicate component: ' + name)
        components[name] = payload
        locations[name] = (size, offset)
    required = {'image.updater.ddr', 'image.updater.spl', 'image.target.spl',
                'image.target.os', 'image.target.rodata', 'image.info'}
    if not required <= components.keys():
        raise ValueError('missing components: ' + ', '.join(sorted(required - components.keys())))
    print('{}: {} components, bounds and CRC32 passed'.format(path, len(components)))
    if bootcfg:
        values = dict(line.split("=", 1) for line in pathlib.Path(bootcfg).read_text().splitlines()
                      if line and not line.startswith("#"))
        if values.get("image") != pathlib.Path(path).name:
            raise ValueError("bootcfg points to a different image")
        for field, member in (("boot0", "spl"), ("boot1", "uboot"), ("env", "env")):
            location = locations.get("image.updater." + member)
            expected = "@".join(hex(value) for value in location) if location else None
            if values.get(field) != expected:
                raise ValueError("bootcfg does not match IMG metadata: " + field)
    if archive_path:
        version = data[136:200].split(b'\0', 1)[0]
        verify_payloads(archive_path, version, components, platform, compat)


def verify_payloads(path, version, components, platform="d21x", compat=None):
    archive = pathlib.Path(path).read_bytes()
    position = 0
    os_image = (platform + "_os.itb").encode("ascii")
    for name, component in ((b'ota_info.bin', None), (os_image, 'image.target.os'),
                            (b'rodata.fatfs', 'image.target.rodata')):
        header = archive[position:position + 110]
        if len(header) != 110 or header[:6] != b'070702':
            raise ValueError('invalid CPIO header for ' + repr(name))
        size = int(header[54:62], 16)
        name_size = int(header[94:102], 16)
        name_begin = position + 110
        name_end = name_begin + name_size
        payload_begin = (name_end + 3) & ~3
        payload_end = payload_begin + size
        if archive[name_begin:name_end] != name + b'\0' or payload_end > len(archive):
            raise ValueError('invalid CPIO member bounds or name: ' + repr(name))
        payload = archive[payload_begin:payload_end]
        if component is None:
            if b'\0version = "' + version + b'";\0' not in payload:
                raise ValueError('CPIO and IMG versions differ')
            if compat and b'\0board = "' + compat.encode("ascii") + b'";\0' not in payload:
                raise ValueError('CPIO hardware compatibility ID does not match target')
        elif payload != components[component]:
            raise ValueError('CPIO and IMG payloads differ: ' + component)
        position = (payload_end + 3) & ~3
    print('{}: version, OS and Rodata match the USB image'.format(path))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("legacy_archive", nargs="?")
    parser.add_argument("--archive")
    parser.add_argument("--platform", default="d21x")
    parser.add_argument("--bootcfg")
    parser.add_argument("--compat")
    args = parser.parse_args()
    verify(args.image, args.archive or args.legacy_archive, args.platform, args.bootcfg, args.compat)
