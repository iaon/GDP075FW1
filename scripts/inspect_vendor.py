#!/usr/bin/env python3
"""Disassemble a range of app0, annotating Xtensa literal loads.

Only application segments are mapped; private NVS data is never inspected.
The generated ELF is an analysis container, not the original linker's ELF.
"""

import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("start", type=lambda value: int(value, 0))
    parser.add_argument("end", type=lambda value: int(value, 0))
    parser.add_argument("--tool-prefix", default=str(
        Path(__file__).resolve().parents[1] / ".platformio/packages/"
        "toolchain-xtensa-esp32/bin/xtensa-esp32-elf-"))
    args = parser.parse_args()
    flash = args.image.read_bytes()
    offset = 0x10000
    if flash[offset] != 0xE9:
        parser.error("app0 is not an ESP image")
    segments = []
    pos = offset + 24
    for _ in range(flash[offset + 1]):
        address, size = struct.unpack_from("<II", flash, pos)
        pos += 8
        segments.append((address, pos, flash[pos:pos + size]))
        pos += size

    def read(address, size):
        for base, _, data in segments:
            if base <= address and address + size <= base + len(data):
                return data[address - base:address - base + size]
        return b""

    def describe(address):
        for base, _, data in segments:
            if base <= address < base + len(data):
                candidate = data[address - base:address - base + 120].split(b"\0", 1)[0]
                if candidate and all(32 <= value < 127 or value in (10, 13) for value in candidate):
                    return repr(candidate.decode("ascii"))
        return ""

    with tempfile.TemporaryDirectory(prefix="gdp075fw1-analysis-") as directory:
        raw = Path(directory) / "code.bin"
        intermediate = Path(directory) / "code.elf"
        mapped = Path(directory) / "mapped.elf"
        for base, _, data in segments:
            if base <= args.start < args.end <= base + len(data):
                raw.write_bytes(data)
                break
        else:
            parser.error("range must fit in one application segment")
        subprocess.run([args.tool_prefix + "objcopy", "-I", "binary", "-O",
                        "elf32-xtensa-le", "-B", "xtensa", "--rename-section",
                        ".data=.text,contents,alloc,load,readonly,code",
                        str(raw), str(intermediate)], check=True)
        subprocess.run([args.tool_prefix + "objcopy", "--change-section-vma",
                        f".text={base:#x}", str(intermediate), str(mapped)], check=True)
        output = subprocess.check_output([
            args.tool_prefix + "objdump", "-d", f"--start-address={args.start:#x}",
            f"--stop-address={args.end:#x}", str(mapped)], text=True)
        for line in output.splitlines():
            literal = re.search(r"l32r\s+a\d+,\s*([0-9a-f]+)", line)
            if literal:
                value = read(int(literal[1], 16), 4)
                if value:
                    address = struct.unpack("<I", value)[0]
                    line += f"  ; {address:#010x} {describe(address)}"
            print(line)


if __name__ == "__main__":
    main()
