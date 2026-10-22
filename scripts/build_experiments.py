#!/usr/bin/env python3
"""Build and package ONLY the application for the existing 0x10000 OTA slot."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from check_release import validate_image

PROJECT = Path(__file__).resolve().parents[1]


def source_fingerprint():
    inputs = sorted((PROJECT / "src").glob("*")) + [PROJECT / name for name in
        ("VERSION", "platformio.ini", "partitions.csv", "requirements-build.txt", "scripts/version_flags.py")]
    digest = hashlib.sha256()
    for path in inputs:
        if path.is_file():
            digest.update(str(path.relative_to(PROJECT)).encode() + b"\0" + path.read_bytes() + b"\0")
    return digest.hexdigest()


def main():
    source_sha = source_fingerprint()
    version = (PROJECT / "VERSION").read_text().strip() + "-pll." + source_sha[:12]
    revision = subprocess.check_output(["git", "rev-parse", "--short=12", "HEAD"],
                                       cwd=PROJECT, text=True).strip()
    environment = dict(os.environ, FIRMWARE_VERSION=version, FIRMWARE_GIT_SHA=revision)
    # Avoid an unrelated upgrade probe delaying an otherwise offline build.
    environment["PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL"] = "1000000"
    environment["PLATFORMIO_SETTING_ENABLE_TELEMETRY"] = "false"
    subprocess.run([str(PROJECT / "scripts/pio.sh"), "-e", "gdp075fw1"],
                   cwd=PROJECT, env=environment, check=True)
    if source_fingerprint() != source_sha:
        raise ValueError("firmware inputs changed during the build; rebuild before packaging")
    image = (PROJECT / ".pio/build/gdp075fw1/firmware.bin").read_bytes()
    app0 = [line.split(",") for line in (PROJECT / "partitions.csv").read_text().splitlines()
            if line.strip().startswith("app0,")]
    if len(app0) != 1 or int(app0[0][3].strip(), 0) != 0x10000 or int(app0[0][4].strip(), 0) != 0x140000:
        raise ValueError("unexpected application partition layout")
    if len(image) > 0x140000 or validate_image(image, 0, len(image)) != len(image):
        raise ValueError("invalid or oversized ESP32 application")
    if version.encode() not in image or revision.encode() not in image:
        raise ValueError("application identity mismatch")
    destination = PROJECT / "dist/lut-pll"
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(PROJECT / ".pio/build/gdp075fw1/firmware.bin", destination / "firmware.bin")
    metadata = {"version": version, "git_revision": revision, "source_sha256": source_sha,
                "image": "firmware.bin", "image_sha256": hashlib.sha256(image).hexdigest(),
                "bytes": len(image), "flash_offset": "0x10000", "slot_bytes": 0x140000,
                "environment": "gdp075fw1", "lut_available": False,
                "hardware_tested": False, "factory_image_created": False}
    (destination / "BUILD.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (destination / "SHA256SUMS").write_text("".join(
        hashlib.sha256((destination / filename).read_bytes()).hexdigest() + "  " + filename + "\n"
        for filename in ("firmware.bin", "BUILD.json")))
    print(json.dumps(metadata, indent=2))
    print(f"Application and SHA256SUMS: {destination}")


if __name__ == "__main__":
    main()
