#!/usr/bin/env python3
"""Patch the OTA image header (length, CRC-32) into a firmware .bin.

The header sits at offset 0xC0, directly after the vector table
(src/image_header.h): u32 magic "PIRS", u32 length, u32 crc. The CRC-32
(zlib) covers the whole image with the crc field taken as zero.
"""
import struct
import sys
import zlib

HEADER_OFFSET = 0xC0
MAGIC = 0x53524950
MAX_SIZE = 0x6FFC


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} image.bin")
    path = sys.argv[1]
    data = bytearray(open(path, "rb").read())

    if len(data) > MAX_SIZE:
        sys.exit(f"{path}: {len(data)} bytes exceeds the 0x{MAX_SIZE:X}-byte limit")
    magic, = struct.unpack_from("<I", data, HEADER_OFFSET)
    if magic != MAGIC:
        sys.exit(f"{path}: no image header at 0x{HEADER_OFFSET:X}")

    struct.pack_into("<II", data, HEADER_OFFSET + 4, len(data), 0)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    struct.pack_into("<I", data, HEADER_OFFSET + 8, crc)
    open(path, "wb").write(data)
    print(f"{path}: {len(data)} bytes, crc32 0x{crc:08X}")


if __name__ == "__main__":
    main()
