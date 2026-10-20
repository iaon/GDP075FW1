#!/usr/bin/env python3
"""Compare a GDP075FW1 full readback to its exact flashed reference image.

Default: fail on changes outside NVS/OTA metadata/SPIFFS. --strict: fail on
any change. Only byte counts are printed; no NVS values or pixel data.
This tool reads files only and never connects to or resets the board.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from check_release import validate_image

FLASH_SIZE = 0x400000
LAYOUT = (
    ("nvs", 1, 2, 0x9000, 0x5000, True),
    ("otadata", 1, 0, 0xe000, 0x2000, True),
    ("app0", 0, 0x10, 0x10000, 0x140000, False),
    ("app1", 0, 0x11, 0x150000, 0x140000, False),
    ("spiffs", 1, 0x82, 0x290000, 0x170000, True),
)


def compare(reference: bytes, readback: bytes, strict=False):
    if len(reference) != FLASH_SIZE or len(readback) != FLASH_SIZE:
        raise ValueError("reference and readback must both be exactly 4 MiB")
    # Derive allowances from the trusted reference, never the readback. Only
    # the known layout is accepted, not an arbitrary table that hides code.
    for index, (name, typ, sub, offset, size, _) in enumerate(LAYOUT):
        fields = struct.unpack_from("<HBBII16sI", reference, 0x8000 + index * 32)
        expected = (0x50aa, typ, sub, offset, size, name.encode().ljust(16, b"\0"), 0)
        if fields != expected:
            raise ValueError("unsupported reference partition layout")
    entries = reference[0x8000:0x8000 + len(LAYOUT) * 32]
    md5 = reference[0x8000 + len(entries):0x8000 + len(entries) + 32]
    if md5 != b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest():
        raise ValueError("invalid reference partition-table MD5")
    validate_image(reference, 0x1000, 0x8000)
    validate_image(reference, 0x10000, 0x150000)
    regions = [("reserved", 0, 0x1000, False),
               ("bootloader", 0x1000, 0x8000, False),
               ("partitions", 0x8000, 0x9000, False)]
    regions += [(name, offset, offset + size, mutable)
                for name, _, _, offset, size, mutable in LAYOUT]
    results = []
    matches = True
    for name, start, end, mutable in sorted(regions, key=lambda item: item[1]):
        changed = sum(a != b for a, b in zip(reference[start:end], readback[start:end]))
        results.append((name, start, end, changed, mutable))
        if changed and (strict or not mutable):
            matches = False
    return matches, results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path, help="exact full image flashed to this board")
    parser.add_argument("readback", type=Path, help="4 MiB read-flash snapshot")
    parser.add_argument("--strict", action="store_true", help="include writable runtime data in comparison")
    args = parser.parse_args()
    try:
        matches, results = compare(args.reference.read_bytes(), args.readback.read_bytes(), args.strict)
    except (OSError, ValueError) as error:
        parser.exit(2, f"Cannot verify: {error}\n")
    for name, start, end, changed, mutable in results:
        kind = "RUNTIME (not validated)" if mutable and not args.strict else "PROTECTED"
        print(f"{name:10} {start:#08x}..{end - 1:#08x} changed={changed:7} {kind}")
    if matches:
        print("FULL FLASH MATCH" if args.strict else
              "CODE MATCH: bootloader, partition table, both app regions and reserved bytes unchanged")
    else:
        print("FULL FLASH DIFFERS" if args.strict else "CODE MISMATCH: unexpected change outside runtime data")
    if not args.strict:
        print("NVS, OTA metadata and SPIFFS changes are reported but not validated by this check.")
    return 0 if matches else 1


if __name__ == "__main__":
    raise SystemExit(main())
