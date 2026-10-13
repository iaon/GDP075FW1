#!/usr/bin/env python3
"""Check ESP32 image placement and erased partitions in a release image."""

import argparse
import hashlib
import struct
from pathlib import Path


def validate_image(image: bytes, offset: int, limit: int):
    if offset + 24 > limit or image[offset] != 0xe9:
        raise ValueError(f"missing ESP image header at {offset:#x}")
    count = image[offset + 1]
    if not 1 <= count <= 16 or struct.unpack_from("<H", image, offset + 12)[0] != 0:
        raise ValueError("invalid segment count or non-ESP32 chip ID")
    pos, checksum = offset + 24, 0xef
    for _ in range(count):
        if pos + 8 > limit:
            raise ValueError("segment header outside image")
        _, length = struct.unpack_from("<II", image, pos)
        pos += 8
        if pos + length > limit:
            raise ValueError("segment data outside image")
        for value in image[pos:pos + length]:
            checksum ^= value
        pos += length
    checksum_end = (pos + 16) & ~15
    if checksum_end > limit or image[checksum_end - 1] != checksum:
        raise ValueError(f"invalid ESP checksum at {offset:#x}")
    if image[offset + 23]:
        digest_end = checksum_end + 32
        if digest_end > limit or image[checksum_end:digest_end] != hashlib.sha256(
                image[offset:checksum_end]).digest():
            raise ValueError(f"invalid ESP SHA-256 at {offset:#x}")
        return digest_end
    return checksum_end


def check(project: Path):
    build = project / ".pio/build/gdp075fw1"
    image = (project / "gdp075fw1-factory-rebuilt.bin").read_bytes()
    app = (project / "gdp075fw1-app-rebuilt.bin").read_bytes()
    bootloader = (build / "bootloader.bin").read_bytes()
    partitions = (build / "partitions.bin").read_bytes()
    if len(image) != 4 * 1024 * 1024:
        raise ValueError("factory image must be exactly 4 MiB")
    if image[:0x1000] != b"\xff" * 0x1000:
        raise ValueError("ESP32 bootloader must not be at offset 0x0")
    if image[0x1000:0x1000 + len(bootloader)] != bootloader:
        raise ValueError("bootloader mismatch at 0x1000")
    if image[0x8000:0x8000 + len(partitions)] != partitions:
        raise ValueError("partition table mismatch at 0x8000")
    if app != (build / "firmware.bin").read_bytes():
        raise ValueError("application artifact differs from build")
    if image[0x10000:0x10000 + len(app)] != app or app[:1] != b"\xe9":
        raise ValueError("application mismatch at 0x10000")
    if len(app) > 0x140000:
        raise ValueError("application exceeds its OTA partition")
    validate_image(image, 0x1000, 0x8000)
    validate_image(app, 0, len(app))
    validate_image(image, 0x10000, 0x150000)
    for start, end in ((0x9000, 0xe000), (0x150000, 0x400000)):
        if image[start:end] != b"\xff" * (end - start):
            raise ValueError(f"partition range {start:#x}..{end:#x} must be erased")
    print("Release layout verified: bootloader=0x1000, partitions=0x8000, "
          "app=0x10000, size=4 MiB, unused partitions=0xff, ESP checksums/SHA-256 valid")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1])
    check(parser.parse_args().project)
