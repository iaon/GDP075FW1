#!/usr/bin/env python3
"""Validate and describe the ESP32 images in a GDP075FW1 flash dump."""
import argparse
import hashlib
import struct
from pathlib import Path


def image_info(flash: bytes, offset: int, label: str) -> None:
    magic, count, mode, size_freq, entry = struct.unpack_from("<BBBBI", flash, offset)
    print(f"[{label}] offset=0x{offset:x} magic=0x{magic:02x}")
    if magic != 0xE9:
        print("  not a plain ESP image (the OTA slot may contain encrypted data)")
        return
    print(f"  segments={count}, flash_mode={mode}, size_freq=0x{size_freq:02x}, entry=0x{entry:08x}")
    pos, checksum = offset + 24, 0xEF
    for number in range(count):
        load, length = struct.unpack_from("<II", flash, pos)
        pos += 8
        data = flash[pos:pos + length]
        for value in data:
            checksum ^= value
        print(f"  {number}: file=0x{pos:x}-0x{pos + length:x} load=0x{load:08x} size=0x{length:x}")
        pos += length
    digest = (pos + 16) & ~15
    print(f"  xor valid: {flash[digest - 1] == checksum}")
    if flash[offset + 23]:
        valid = flash[digest:digest + 32] == hashlib.sha256(flash[offset:digest]).digest()
        print(f"  SHA-256 valid: {valid}; compact image end=0x{digest + 32:x}")


parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
args = parser.parse_args()
flash = args.image.read_bytes()
print(f"size={len(flash)} sha256={hashlib.sha256(flash).hexdigest()}")
image_info(flash, 0x10000, "app0")
image_info(flash, 0x150000, "app1")
