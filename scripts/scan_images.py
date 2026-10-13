#!/usr/bin/env python3
"""Find checksum-valid classic ESP32 images at 4-KiB boundaries in a dump.

Print only image headers and public application descriptors, never NVS values.
"""
import argparse
import struct
from pathlib import Path
from check_release import validate_image


def scan(data):
    for offset in range(0, len(data) - 24, 0x1000):
        if data[offset] != 0xe9 or data[offset + 23] not in (0, 1):
            continue
        try:
            end = validate_image(data, offset, len(data))
        except ValueError:
            continue
        load, _ = struct.unpack_from("<II", data, offset + 24)
        if not (0x3f000000 <= load < 0x40400000 or 0x50000000 <= load < 0x50010000):
            continue
        print(f"image={offset:#x} end={end:#x} size={end - offset:#x} "
              f"segments={data[offset + 1]} checksum=valid "
              f"sha256={'valid' if data[offset + 23] else 'not appended'}")
        descriptor = offset + 32
        if struct.unpack_from("<I", data, descriptor)[0] == 0xabcd5432:
            for name, start, size in (("version", 16, 32), ("project", 48, 32),
                                      ("idf", 112, 32)):
                value = data[descriptor + start:descriptor + start + size].split(b"\0", 1)[0]
                print(f"  {name}={value.decode('ascii', errors='replace')}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    scan(parser.parse_args().image.read_bytes())
