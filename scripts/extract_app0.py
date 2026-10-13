#!/usr/bin/env python3
"""Extract the compact, SHA-checked app0 ESP image from a complete dump."""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
flash = args.image.read_bytes()
base = 0x10000
if flash[base] != 0xE9:
    raise SystemExit("app0 is not a plain ESP image")
segments = flash[base + 1]
pos = base + 24
for _ in range(segments):
    _, length = struct.unpack_from("<II", flash, pos)
    pos += 8 + length
end = ((pos + 16) & ~15) + 32  # app0 has a SHA-256 trailer
args.output.write_bytes(flash[base:end])
print(f"wrote {end - base} bytes to {args.output}")
