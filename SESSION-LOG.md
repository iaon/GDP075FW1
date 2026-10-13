# Session log — GDP075FW1 reconstruction

Date: 2026-10-14

## Input

- Source image: `GDP075FW1-backup.bin`
- Size: 4,194,304 bytes (`0x400000`)
- SHA-256: `7d9b45de0185dbe67184de7d15a28299ee4fbeeac187849079345b6a6ba75537`

## Static-analysis result

- The dump is a complete 4 MiB ESP32 flash image, not an ESP32-S3 image.
- The partition table is at `0x8000` and contains NVS, OTA metadata, `app0`,
  `app1`, and SPIFFS.
- `app0` starts at `0x10000`; it is a six-segment ESP image.
- `app0` XOR checksum and appended SHA-256 were verified successfully.
- The compact `app0` image ends at `0x0bfec0` and is 720,576 bytes long.
- `app1` does not have plain ESP image magic and was not treated as directly
  extractable firmware.
- The populated NVS partition was not decoded or recorded because it may hold
  Wi-Fi credentials.

## Created project files

- `platformio.ini`: ESP32 Arduino/PlatformIO configuration.
- `partitions.csv`: flash partition layout reproduced from the dump.
- `src/main.cpp`: open replacement firmware using GxEPD2 for GDEM075F52
  800×480 four-colour panels, Wi-Fi NVS provisioning, and HTTP frame upload.
- `scripts/analyze_image.py`: validates and lists application segments.
- `scripts/extract_app0.py`: extracts compact `app0` from a complete dump.
- `scripts/pio.sh`: starts PlatformIO with mutable state under the project.
- `README.md` and `BINARY-ANALYSIS.md`: build and analysis documentation.

## Verification

The following completed successfully:

```sh
python3 scripts/analyze_image.py GDP075FW1-backup.bin
python3 scripts/extract_app0.py GDP075FW1-backup.bin /tmp/app0.bin
python3 -m py_compile scripts/analyze_image.py scripts/extract_app0.py
```

## Initial build blocker (resolved)

During initial analysis PlatformIO could not be installed. Shell networking
was disabled by the active Codex
sandbox (`CODEX_SANDBOX_NETWORK_DISABLED=1`), so `pip` cannot resolve
`pypi.org`. Direct DNS and direct TCP socket attempts are also denied.

The project is now marked trusted in `~/.codex/config.toml`, but that only
applies to a newly created Codex session. Restart Codex in this project, then
run:

```sh
./.venv/bin/pip install "platformio==6.2.0"
scripts/pio.sh
```

## Initial reconstruction limit (subsequently investigated)

The vendor USB and LAN image-transfer protocol was not asserted to be
byte-compatible. Recovering it requires a live UART/TCP capture while using
the vendor application, or a deeper ESP32 LX6 decompilation. The replacement
instead documents an open HTTP interface.

## Follow-up reconstruction correction

A targeted Xtensa disassembly of app0 recovered the vendor's factory station
credentials, host 208, signed `wifi_config/host_number`, binary 64-byte UART
provisioning, DHCP-to-static `/24` setup and periodic reconnection. It also
recovered TCP port 8000, a single packed 96,000-byte 2-bpp image, and the panel's
reset/initialization/refresh/sleep commands. These are now implemented; exact
addresses and packet offsets are documented in `BINARY-ANALYSIS.md`.

The previous factory packaging incorrectly placed the ESP32 bootloader at
0x0 and zero-padded unused flash. It now puts the bootloader at 0x1000, retains
the partition table at 0x8000 and app at 0x10000, and fills unused flash with
0xff. A release check verifies the complete layout and erased NVS/OTA/SPIFFS.

Host-side tests cover binary/text provisioning, rejection of malformed data,
all four palette indices, the recovered controller command trace, native
pixel streaming, complete-frame refresh and sleep. PlatformIO and local
release packaging succeed. Physical hardware and ImageToWiFi have not been
tested; exact original fonts/layout and opaque app1 data remain outside the
reconstructed paths. The original dump was not modified.

## Further verification without hardware

Removed per-block CS transactions, extra reset/blank-frame writes, and silent
BUSY timeout success. The adapter now reproduces per-byte CS/DC sequencing
and propagates sticky reset/power-on/refresh/power-off failures. Wi-Fi restores
services after SDK auto-reconnect and does not accept a stale DHCP address as
proof of static-address setup. HTTP/TCP share a tested receive state machine;
stalled, truncated, oversized, aborted and failed-storage uploads are rejected.

The expanded host tests run actual firmware logic with mocked GPIO/SPI,
serial, NVS, clock and client streams. They run with address/undefined-behaviour
sanitizers locally and in CI. Image packaging now validates ESP checksums and
SHA-256 too; display dependency versions are pinned.

A complete aligned image scan found another checksum/hash-valid ESP32 image:
ESP-AT at 0x100000..0x24ff50. Current app1 begins inside that image, explaining
its missing ESP header; the historical reason for retaining it remains unknown.
No physical-board test or vendor-application capture has been performed.
