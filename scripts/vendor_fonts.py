#!/usr/bin/env python3
"""Reproduce recovered ASCII font header from app0; never inspect NVS.

Without --check, emit the header to stdout. The original dump is not needed
for building or for normal tests: its font tables are included in the source.
"""
import argparse
import hashlib
import struct
from pathlib import Path


def font_header(image: bytes) -> str:
    if len(image) < 0x10018 or image[0x10000] != 0xe9:
        raise ValueError("missing app0 ESP image")
    segments, position = [], 0x10018
    for _ in range(image[0x10001]):
        address, length = struct.unpack_from("<II", image, position)
        position += 8
        if position + length > len(image):
            raise ValueError("truncated app0 segment")
        segments.append((address, image[position:position + length]))
        position += length

    def read(address, length):
        for base, data in segments:
            if base <= address and address + length <= base + len(data):
                return data[address - base:address - base + length]
        raise ValueError(f"unmapped app0 address {address:#x}")

    lines = ["#pragma once", "", "#include <stdint.h>", "",
             "// ASCII 32..126 glyphs recovered from original app0 (not NVS).",
             "// Generated reproducibly by scripts/vendor_fonts.py.",
             "namespace vendor { namespace fonts {", ""]
    for descriptor, expected_address, width, height, label in (
            (0x3ffbdb7c, 0x3f400470, 14, 20, "kFont20"),
            (0x3ffbdb84, 0x3f401348, 17, 24, "kFont24")):
        address, actual_width, actual_height = struct.unpack("<IHH", read(descriptor, 8))
        if (address, actual_width, actual_height) != (expected_address, width, height):
            raise ValueError("unexpected original font descriptor")
        glyphs = read(address, 95 * height * ((width + 7) // 8))
        lines += [f"// Source {address:#010x}, {width}x{height}, SHA-256 {hashlib.sha256(glyphs).hexdigest()}",
                  f"static const uint8_t {label}[] ="]
        for offset in range(0, len(glyphs), 32):
            lines.append('    "' + ''.join(f"\\x{byte:02x}" for byte in glyphs[offset:offset + 32]) + '"')
        lines[-1] += ";"
        lines += [f'static_assert(sizeof({label}) == {len(glyphs) + 1}, "font table size");', ""]
    lines += ["} }  // namespace vendor::fonts", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--check", type=Path)
    args = parser.parse_args()
    header = font_header(args.image.read_bytes())
    if args.check:
        if args.check.read_text() != header:
            parser.exit(1, "Font header differs from original app0 glyphs\n")
        print("Both font tables and descriptors match original app0 exactly")
    else:
        print(header, end="")
