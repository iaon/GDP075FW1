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

## Follow-up to first hardware report: status/retry diagnostics

The user reported different fonts/colours, repeated flashing of the panel,
Wi-Fi failures, UART length errors and changed runtime flash bytes. Compared
the supplied readback to the flashed factory artifact: bootloader, partition
table and code regions are identical and image checksums remain valid. The
2,250 NVS changes and SPIFFS metadata (1,471 changed bytes in 368 sectors)
do not demonstrate damaged executable code. Every SPIFFS sector matches the
SDK's magic-length formula and zero erase counter, with no allocated files.

Reproduced repeated full display-update requests on failed Wi-Fi retries with
zero mock ESP restarts. Fixed by publishing only the first connection result
and success/failure transitions. Added BOOT reset reason, ALIVE uptime/attempt
reports, display operation boundaries and storage mount/format diagnostics;
these diagnostics do not add NVS writes. UART timeout errors now include mode
and byte count without contents. Lone terminal line endings are ignored after
the idle timeout, while valid binary lengths 10/13 and buffered continuations
during long panel updates remain supported.

Recovered exact Font20/Font24 tables, text coordinates, Paint colour semantics
and failure inversion from original app0. Status streams native packed rows
with 200 bytes of scratch storage; it no longer uses Adafruit text. Font data
are reproducible/verified with scripts/vendor_fonts.py, and unsupported bytes
are safely substituted. The original and readback dumps are preserved and
ignored by Git.

Expanded verification includes six C++ suites with ASan/UBSan and seven Python
tests, including retry refresh suppression, reboot diagnostics without NVS
writes, UART lengths/timeout, font hashes, full-frame reference Paint output
and actual native-driver transfer. No attached board was flashed here; actual
reset cause and physical panel output still require the user's UART/capture.

## User-confirmed operation and Linux workflow

The user subsequently reported stable operation and a status picture similar
to the original. Their UART capture showed ALIVE uptimes increasing through
91,115 ms, with Wi-Fi attempt 2 and no additional boot marker. The first status
refresh ended with result=ok after 22,175 ms; the next failure did not redraw.
WL_NO_SSID_AVAIL (status=1) was explained as an unavailable station network.

The user successfully provisioned their own Wi-Fi. Documented miniterm local
echo, LF endings and the current two-second inter-byte timeout, including the
recommendation to paste a complete WIFI line and press Enter immediately.
The firmware deliberately saves credentials and restarts after a countdown.
No private credentials were added to documentation.

The user confirmed the Linux picture conversion/upload workflow works. README
now contains the complete Pillow example with EXIF orientation, white alpha
compositing, clockwise rotation before aspect-preserving 800x480 fitting,
four-colour dithering and native packing to 96,000 bytes. It includes the
same-interpreter Pillow installation fix, HTTP upload with explicit completion
and the TCP alternative. Personal picture.jpg and generated frame.native are
kept locally and excluded from Git, like flash dumps and build artifacts.

Hardware claims are explicitly user-reported and limited to the tested board
and workflow; ImageToWiFi interoperability and fault/long-duration scenarios
remain unverified. This follow-up documents the existing firmware behaviour
without changing the UART timeout or automatically accessing the board.

## First release preparation

Prepared v0.1.0 using the existing VERSION=0.1.0 and tag-driven release
workflow. Before preparation, origin/main matched 0f02803, there were no
remote tags or published releases, and its GitHub Actions build had succeeded.
README now identifies the versioned application/factory assets, their flash
addresses, checksum verification and hardware verification limits. The release
workflow includes flash/data-preservation instructions in its generated notes.
Flash dumps, locally generated images and personal pictures remain excluded
from Git and from release assets.
