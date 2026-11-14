#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the SDK's ArtInChip IMG component bounds, identities and CRC32."""
import pathlib
import struct
import sys
import zlib


def verify(path):
    data = pathlib.Path(path).read_bytes()
    if len(data) < 2048 or data[:8] != b'AIC.FW\0\0':
        raise ValueError('invalid ArtInChip IMG header')
    if data[8:72].rstrip(b'\0') != b'd21x':
        raise ValueError('unexpected firmware platform')
    meta_offset, meta_size, file_offset, file_size = struct.unpack_from('<4I', data, 332)
    if (meta_offset != 2048 or not meta_size or meta_size % 512 or
            file_offset != meta_offset + meta_size or
            file_offset + file_size != len(data)):
        raise ValueError('invalid IMG area bounds')
    names = set()
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
        if (zlib.crc32(data[offset:offset + size]) & 0xffffffff) != crc:
            raise ValueError('component CRC mismatch: ' + name)
        if name in names:
            raise ValueError('duplicate component: ' + name)
        names.add(name)
    required = {'image.updater.ddr', 'image.updater.spl', 'image.target.spl',
                'image.target.os', 'image.info'}
    if not required <= names:
        raise ValueError('missing components: ' + ', '.join(sorted(required - names)))
    print('{}: {} components, bounds and CRC32 passed'.format(path, len(names)))


if __name__ == '__main__':
    verify(sys.argv[1])
