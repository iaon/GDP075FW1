# Open reconstruction of GDP075FW1 firmware

This repository is an Arduino/PlatformIO replacement for the firmware in
`GDP075FW1-backup.bin`, a 4 MiB flash dump from a Good Display GDP075FW1
sign. It targets the ESP32-M075 board (ESP32-WROOM-32D) and the 800×480
four-colour GDEM075F52 panel (JD79665AA controller).

It is a source-level reconstruction, not a byte-identical rebuild. Targeted
Xtensa disassembly recovered the vendor's UART provisioning packet, static
Wi-Fi addressing, TCP image port, and packed four-colour frame format. The
project also provides an HTTP interface. Evidence and remaining differences
are listed in [BINARY-ANALYSIS.md](BINARY-ANALYSIS.md).

## What the replacement does

- loads and saves the vendor's Wi-Fi NVS keys, including `host_number`;
- uses factory defaults `Goodisplay` / `12345678` / host `208` when no valid
  saved configuration exists;
- learns the gateway via DHCP, then selects a static `/24` address;
- accepts the recovered 64-byte UART configuration packet at 115200 baud;
- reconnects after Wi-Fi loss and displays network status on the panel;
- reproduces the original ASCII status fonts, positions and native colours;
- drives the 800×480 four-colour panel;
- accepts a full packed native frame on TCP port `8000` or `POST /frame/native`;
- offers explicit experimental HTTP routes for comparing fast-full and window
  refreshes, with per-stage timings and `GET /display/modes` capability reporting;
- reports its address and signal at `GET /status`.

Uploaded frames are staged in the SPIFFS partition while they are sent to the
display, so the firmware fits on ESP32-WROOM-32D boards without PSRAM.

The vendor frame is exactly 96,000 bytes: row-major 800×480 pixels, four pixels
per byte, most significant bits first. Colour indices are black=`00`,
white=`01`, yellow=`10`, red=`11`. Byte `0x1b` is a black, white, yellow, red
sequence. Send the raw bytes to TCP port `8000`, then close the connection:

```sh
python3 scripts/device.py frame DEVICE_IP frame.native
```

Alternatively send the same packed frame using multipart field `frame`:

```sh
curl -F frame=@frame.native http://DEVICE_IP/frame/native
```

For ordinary PNG/JPEG files, use the complete conversion/upload example in
[Uploading a picture from Linux](#uploading-a-picture-from-linux).

For compatibility with the initial open replacement, `POST /frame` still
accepts two 48,000-byte bitplanes (black/white first, chromatic second). That
legacy format provides black, white and red; it is not the vendor's native
four-colour format and cannot represent yellow.

SPIFFS is mounted lazily on the first image upload, never at boot or during
status drawing. It is automatically formatted only when its entire partition
is erased (`0xff`), so an application update does not silently erase vendor
files. Its first format writes four metadata bytes per 4 KiB sector, and
uploads write their staging file there. Boot/reconnect diagnostics no longer
create or reset `restart_counter` in NVS; its legacy value is read only.

Arduino/ESP-IDF still initializes NVS and Wi-Fi stores device-specific radio
calibration when needed. Consequently, comparing the entire flash against an
erased-data factory image after boot is not a code-integrity check. See
[Flash verification and personal backups](#flash-verification-and-personal-backups).

The status screen is refreshed for its first result and changes between
connection success/failure, not for every failed retry. Factory Font20 (14×20)
and Font24 (17×24) glyphs are rendered directly into packed native rows. The
original failure screen inverts the top 240 rows and fills the bottom half
yellow; the success screen leaves the bottom half white. Fonts are ASCII;
unsupported bytes are safely drawn as `?` instead of indexing outside a table.

Incomplete, oversized, stalled, or failed uploads are rejected without a
display refresh. The sender must close TCP after the frame; the receive idle
timeout is 30 seconds. A stuck BUSY signal is reported as a display failure,
including HTTP status 500; a new frame can retry after the fault clears.

## Build

Use the pinned PlatformIO Core version in `requirements-build.txt`, with a
project-local environment as in `sd-app0-arduino`:

```sh
python3 -m venv .venv
./.venv/bin/pip install -r requirements-build.txt
chmod +x scripts/pio.sh
scripts/pio.sh
```

## GitHub Actions releases

The GitHub Actions workflow builds both a flashable application image and a
complete 4 MiB factory image for every push and pull request. Its artifacts
include SHA-256 checksums. Pushing a tag matching `v` plus the version in
[`VERSION`](VERSION) also creates a GitHub Release.

To reproduce the release images locally:

```sh
PIO="$PWD/.venv/bin/pio" scripts/build-release.sh
```

This produces `gdp075fw1-app-rebuilt.bin` for an application update at
`0x10000`, and `gdp075fw1-factory-rebuilt.bin`, a 4 MiB image for a blank
flash chip.

### Release v0.1.0

The first release includes the corrected status refresh/retry behaviour,
recovered vendor fonts/colours, boot diagnostics, UART Wi-Fi provisioning,
native TCP/HTTP picture upload and the documented Linux conversion workflow.
The user confirmed stable operation, Wi-Fi setup and rotated-picture upload
on their board. Other board revisions, vendor ImageToWiFi interoperability
and long-duration/fault testing remain unverified.

The [v0.1.0 release](https://github.com/iaon/GDP075FW1/releases/tag/v0.1.0)
provides these assets:

| Asset | Use |
|---|---|
| `firmware-0.1.0.bin` | Application update at `0x10000`; preserves NVS/SPIFFS with the matching partition layout |
| `gdp075fw1-factory-0.1.0.bin` | Complete 4 MiB image at `0x0`; overwrites settings and stored files |
| `SHA256SUMS` | SHA-256 checksums of both firmware images |

Verify downloaded files with `sha256sum -c SHA256SUMS`. A complete flash
comparison after boot will differ in NVS/SPIFFS because those are writable
runtime data, not immutable application code.

The configuration pins `espressif32@7.1.3`. It builds an ordinary ESP32 image
for the `wemos_d1_mini32` board definition, chosen for ESP32-WROOM-32D; it
does not target ESP32-S3.

The display dependencies are pinned to GxEPD2 1.6.9, Adafruit GFX 1.12.6 and
Adafruit BusIO 1.17.4 so a later library release does not silently change the
controller implementation.

## Dependency updates

[Dependabot](.github/dependabot.yml) checks GitHub Actions and the Python
build-tool manifest `requirements-build.txt` weekly on Mondays at 09:00
Europe/Moscow. Action updates are grouped into one pull request; each
ecosystem is limited to five open update pull requests. Updates require
review and are not automatically merged.

The CI and local setup both install PlatformIO Core from the same manifest.
Changing it triggers the firmware tests/build in CI, including Dependabot
pull requests. No secrets are required to test/build pull requests; only
tagged push builds publish releases.

Dependabot does not currently support the PlatformIO package ecosystem, so
the platform and libraries pinned in `platformio.ini` (espressif32, GxEPD2,
Adafruit GFX/BusIO) still need manual updates and hardware verification.
Optional picture-conversion/UART tools installed into another environment
(Pillow/pyserial in the Linux examples) are not part of the build manifest.

## Checks without a board

These checks do not contact an IP address and do not change the physical panel.
For single on-device PLL measurements, result logs and recovery commands, see
[LUT/PLL experiments](docs/LUT-PLL-EXPERIMENTS.md). That tool uses `EPD_URL`.

```sh
python3 scripts/run_tests.py --sanitize
```

This compiles the actual protocol, display driver, Wi-Fi manager, frame
receiver, status renderer and boot diagnostics on the host, using simulated
GPIO/SPI, clock, serial, NVS and network events. Tests cover timeout/error
propagation and recovery, per-byte CS/DC,
DHCP-to-static transitions, SDK auto-reconnection, fragmented UART/TCP input,
repeated failures without refresh/restart, standalone terminal line endings,
native status frames against a separate full-frame Paint transcription,
heartbeat/reset causes, storage failures and HTTP/TCP ownership. The refresh
tests compile the real HTTP handlers in `main.cpp` and exercise multipart
callbacks, strict window validation, timing reports, failure cleanup and
mode isolation against the actual driver command trace. They simulate
WebServer events, not its HTTP wire parser. Python tests cover sender framing,
ESP image checksum/hash rejection and the native test images/expected window
preview. These tests do not emulate ESP32 radio, physical flash or the panel.
GitHub Actions runs the same checks before building.

With the original dump available, verify both recovered font tables directly:

```sh
python3 scripts/vendor_fonts.py GDP075FW1-backup.bin --check src/vendor_fonts.h
```

Release packaging validates the application and bootloader checksum/SHA-256,
placement, partition sizes and erased unused flash. To scan the original
dump for additional valid images beyond named partition starts:

```sh
python3 scripts/scan_images.py GDP075FW1-backup.bin
```

## First Wi-Fi provisioning

The firmware defaults to the station network `Goodisplay`, password
`12345678`, with static host number `208`; these are not access-point
credentials. If the router's gateway is `192.168.1.1`, the device selects
`192.168.1.208` with mask `255.255.255.0`. DNS is the gateway, as in the vendor
firmware. Hosts must be in `1..254` and must not equal the gateway's last octet.
The selected address must also be unused on the local network. Use a 2.4 GHz
Wi-Fi network; the classic ESP32 does not support 5 GHz.

Use the recovered binary provisioning protocol:

```sh
./.venv/bin/python scripts/device.py wifi --port /dev/ttyUSB0 \
  --ssid 'your-ssid' --password 'your-password' --host 208
```

Or open a 115200-baud serial terminal and send the project's text convenience
command (quotation marks support spaces). On Linux, using an existing esptool
Python environment:

```sh
~/esptool-env/bin/python -m serial.tools.miniterm \
  --echo --eol LF /dev/ttyUSB0 115200
```

If that environment does not have pyserial, install it with
`~/esptool-env/bin/python -m pip install pyserial`. Local echo is off by
default in miniterm; `--echo` makes typed text visible. In an already running
miniterm, press Ctrl+T followed by Ctrl+E to toggle it. Echo confirms local
typing, not receipt by the ESP32.

Prepare the following line, replace both credentials, paste the entire line,
and immediately press Enter:

```text
WIFI "your-ssid" "your-password" 208
```

The current UART parser discards an unfinished command after two seconds
without input, so pauses while typing can invalidate a command. Pasting a
complete line avoids this. The firmware does not echo individual characters.
On acceptance it prints `Received WiFi-Name`, counts down 3/2/1, saves NVS
and intentionally restarts once. Look for `WiFi Connected` and `IP Address:`.
`WIFI timeout ... status=1` means `WL_NO_SSID_AVAIL`: the configured network
was not found, not necessarily that its password is wrong.

Exit miniterm with Ctrl+] before using the binary sender or esptool on the
same UART. `scripts/device.py wifi` is an alternative to interactive typing;
it also works with `~/esptool-env/bin/python` when pyserial is installed there.
Wi-Fi provisioning reports credentials on UART and displays them on the
panel, matching the vendor screen; redact passwords before sharing logs.

The host argument is optional in the text command and preserves the current
host. Binary provisioning limits SSID and password to 29 bytes each; the text
command supports 32-byte SSIDs and 64-byte passwords. Use `""` for an open
network's empty password.

The firmware persists `ssid`, `password`, and signed 32-bit `host_number`
under the vendor's `wifi_config` NVS namespace and restarts after a countdown.
The original dump includes an NVS partition, so flashing only the application
preserves its existing configuration. Erasing or overwriting NVS removes it.

An isolated CR/LF in a terminal is ignored after the two-second input timeout.
CR/LF cannot be discarded immediately because byte values 10 and 13 also
represent valid SSID lengths in the binary packet. Other incomplete packets
report `UART timeout mode=... received=... expected=...`; malformed complete
packets report `UART rejected ... reason=...`, without dumping their contents.

## Uploading a picture from Linux

The firmware does not decode PNG/JPEG. First convert the picture to an
800×480 packed native four-colour frame, then upload that file over Wi-Fi.
Run the following commands from the project directory. They reuse the
existing `~/esptool-env` environment; if using a different environment,
replace that Python path consistently in installation and conversion.

Install Pillow in the same environment as the converter:

```sh
~/esptool-env/bin/python -m pip install Pillow
~/esptool-env/bin/python -c 'from PIL import Image; print("Pillow ready")'
```

`ModuleNotFoundError: No module named 'PIL'` means Pillow is not available to
the interpreter running the conversion. Installing into another Python
environment does not make it available here.

Replace `picture.jpg` below with your PNG/JPEG path (quote paths containing
spaces). This complete example rotates 90 degrees clockwise, respects EXIF
orientation, composites transparency onto white, preserves aspect ratio with
white padding, and dithers to black/white/yellow/red:

```sh
~/esptool-env/bin/python - picture.jpg <<'PY'
import sys
from pathlib import Path
from PIL import Image, ImageOps

# Load the picture and apply its EXIF orientation.
image = ImageOps.exif_transpose(
    Image.open(sys.argv[1])
).convert("RGBA")

# Composite transparent pixels onto white.
background = Image.new("RGBA", image.size, "white")
image = Image.alpha_composite(background, image).convert("RGB")

# -90: clockwise; 90: counterclockwise; 180: upside down; 0: no rotation.
image = image.rotate(-90, expand=True, fillcolor="white")

# Rotate before fitting the final 800x480 canvas.
image = ImageOps.pad(image, (800, 480), color="white")

# Native indices: black=0, white=1, yellow=2, red=3.
palette = Image.new("P", (1, 1))
palette.putpalette(
    [0, 0, 0, 255, 255, 255, 255, 255, 0, 255, 0, 0] * 64
)
pixels = image.quantize(
    palette=palette,
    dither=Image.Dither.FLOYDSTEINBERG,
).tobytes()

# Four pixels per byte, most significant bits first.
frame = bytes(
    ((pixels[i] & 3) << 6)
    | ((pixels[i + 1] & 3) << 4)
    | ((pixels[i + 2] & 3) << 2)
    | (pixels[i + 3] & 3)
    for i in range(0, len(pixels), 4)
)

assert len(frame) == 96000
Path("frame.native").write_bytes(frame)
print("Created frame.native:", len(frame), "bytes")
PY
```

The converter replaces `frame.native` in the current directory. It leaves
the input picture unchanged. Change `-90` to `0`, `90` or `180` as needed;
do not rotate the packed frame after conversion, because the receiver always
expects 800×480 rows in native order.

Only after successful conversion, upload the file. Replace the example IP
with the device's actual `IP Address:` value. Linux and the device must be on
a network that allows communication between them:

```sh
curl --fail-with-body --max-time 180 \
  -F frame=@frame.native \
  http://192.168.1.208/frame/native
```

Wait for `display refreshed`. A full panel update flashes the display and
typically took about 22 seconds in the reported board test; do not send
another frame while it is updating. HTTP success follows completion of the
display operation; errors such as storage unavailability or BUSY timeout
produce a non-success response. `curl http://192.168.1.208/status` checks the
device's address, uptime and Wi-Fi state if needed.

Alternatively, send the same file via the recovered TCP protocol:

```sh
python3 scripts/device.py frame 192.168.1.208 frame.native
```

The TCP sender closes its write side to signal frame completion. Its success
message means the bytes were sent, not that the panel acknowledged a refresh;
use the HTTP method above when an explicit result is desired.

## Comparing display refresh modes

The normal upload paths (`POST /frame/native`, legacy `POST /frame`, TCP port
`8000`) and the network status screen continue to use the normal full refresh.
Three additional HTTP routes expose explicit diagnostic modes:

| Endpoint | Requested operation |
|---|---|
| `POST /frame/native/fast-full` | Experimental fast full-screen waveform |
| `POST /frame/native/partial?x=200&y=120&w=400&h=240` | Experimental window refresh with the normal waveform |
| `POST /frame/native/fast-window?x=200&y=120&w=400&h=240` | Experimental combination of the fast full waveform and a refresh window |

All three take the same complete **96,000-byte** native frame in multipart
field `frame`; a window request does not take a cropped image. The full frame
is written to the panel controller's RAM before requesting the refresh window.
This experiment does not reduce upload or RAM-write traffic. It also does not
enable or claim GxEPD2 fast-partial-update support.

For a quick capability check:

```sh
curl --fail-with-body --max-time 180 http://192.168.1.208/display/modes
```

For ordinary pictures, use the conversion procedure above, then select the
desired endpoint. For a controlled comparison, the dependency-free generator
creates two deliberately different images and an expected window-result preview:

```sh
python3 scripts/make_refresh_patterns.py /tmp/gdp075fw1-refresh
```

Follow [Refresh modes and manual A/B tests](docs/REFRESH-MODES.md) for the
complete commands, strict window rules, response fields and visual checks.
Restore the same baseline using the normal full endpoint before each test.
Actual speed, colour quality, ghosting and unchanged pixels outside the window
must be verified on the board; HTTP success confirms the command/BUSY sequence,
not the optical result. The new modes have not yet been verified on hardware.

## Reported hardware verification

On 2026-10-14 the user reported that the corrected firmware was stable and
the status picture resembled the original. The supplied UART excerpt showed
increasing uptime and attempts without a new boot marker, a successful status
refresh lasting 22,175 ms, and no second display update on the next Wi-Fi
failure. The user subsequently confirmed Wi-Fi provisioning and the Linux
picture conversion/rotation/upload workflow worked.

These are user-reported board results, not an automated hardware test suite.
The vendor ImageToWiFi application, other board revisions, fault recovery and
long-duration operation have not yet been confirmed on hardware.

## Distinguishing retries from resets

Capture UART at 115200 baud from power-on. Each application start prints
`GDP075FW1 ...` and `BOOT reset_reason=...`, including `BROWNOUT`, `PANIC` or
watchdog reset causes. Every approximately 30 seconds, `ALIVE uptime_ms=...`
reports the running uptime and Wi-Fi attempt count. A display operation may
delay a report. This diagnostics counter is not written to NVS.

Increasing uptime/attempts without another `BOOT` indicates retries rather
than an application restart (uptime naturally wraps after about 49.7 days).
Repeated `BOOT`, or ROM `rst:` lines before reaching the application, confirms
resets. `DISPLAY status start/end` isolates panel update time and errors;
`STORAGE` distinguishes mounting from first-time formatting. `GET /status`
also includes uptime, reset reason and Wi-Fi attempts when the network works.

## Flashing

Back up the device before replacing its firmware. A full original dump may
contain Wi-Fi credentials, so keep it private. For a development application
upload, PlatformIO writes the generated application to `0x10000`:

```sh
scripts/pio.sh -t upload --upload-port /dev/ttyUSB0
```

The matching partition layout is in [partitions.csv](partitions.csv): `app0`
begins at `0x10000`, `app1` at `0x150000`, and SPIFFS at `0x290000`.

For a board already running this replacement with the matching layout, update
only the application to preserve NVS and SPIFFS:

```sh
./.venv/bin/python .platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32 --port /dev/ttyUSB0 write_flash \
  0x10000 gdp075fw1-app-rebuilt.bin
```

The full `gdp075fw1-factory-rebuilt.bin` is written at `0x0`, but overwrites
all 4 MiB, including configuration and files. It will restore the default
station credentials and require a first-time SPIFFS format again.

## Flash verification and personal backups

A factory image deliberately has erased NVS/SPIFFS; the vendor backup is a
snapshot of an already initialized device. After every complete factory
flash, Wi-Fi may recreate NVS radio-calibration data. This changes the full
flash digest without changing executable firmware. Restoring an initialized
personal dump to the same board generally reuses those calibration data.
This is not a guarantee of permanent full-flash immutability: provisioning,
uploads, SDK recovery and invalid/incompatible calibration can legitimately
write runtime data. Radio calibration is not display calibration.

The current firmware preserves calibration, Wi-Fi configuration and staging
data rather than disabling persistence just to make a full digest match.
It performs no application-level NVS writes on boot/reconnection and does
not mount/format SPIFFS until an explicit upload. The Arduino core performs
its own NVS initialization before `setup()`. The SDK's radio calibration
policy is documented in [Espressif's RF calibration guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32/api-guides/RF_calibration.html).

### Exact verification before first boot

For esptool 5.x, prevent the automatic reset that otherwise runs the firmware
between writing and verifying. Close miniterm first:

```sh
~/esptool-env/bin/esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  --after no-reset write-flash 0x0 gdp075fw1-factory-rebuilt.bin
~/esptool-env/bin/esptool --chip esp32 --port /dev/ttyUSB0 \
  --before no-reset --after no-reset verify-flash 0x0 gdp075fw1-factory-rebuilt.bin
```

Only reset/power-cycle the board after verification. Do not do a factory flash
merely to recheck an already configured device: it erases settings and
calibration again. The `Hash of data verified` message at write time already
checks the just-written bytes before the final reset.

### Code verification after boot

Read the complete flash and compare it with the exact full image flashed to
that board (not another build or an earlier release):

```sh
~/esptool-env/bin/esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  read-flash 0x0 0x400000 gdp075fw1-readback.bin
python3 scripts/verify_readback.py \
  gdp075fw1-factory-rebuilt.bin gdp075fw1-readback.bin
```

The checker requires the known partition layout and a checksum/hash-valid
reference. It compares the bootloader, partition table, reserved bytes and
both complete application regions, including padding/inactive OTA storage.
It reports NVS/OTA metadata/SPIFFS differences separately without printing
their contents. These writable regions are **not validated** by the code
check; a successful result does not prove that all runtime data are healthy.
Unexpected changes outside them cause a nonzero exit status.

Add `--strict` to require all 4 MiB to match, including runtime data.
The checker reads files only; it never accesses, flashes or resets a board.

### Check that initialization writes stop

After provisioning and first initialization, let the board finish any display
operation. With no new configuration commands or image uploads, capture two
snapshots separated by another complete startup:

```sh
~/esptool-env/bin/esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  read-flash 0x0 0x400000 gdp075fw1-snapshot-before.bin
# The read command resets the device afterward. Wait for startup/display
# completion before taking the second snapshot, without uploading a frame.
~/esptool-env/bin/esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  read-flash 0x0 0x400000 gdp075fw1-snapshot-after.bin
python3 scripts/verify_readback.py --strict \
  gdp075fw1-snapshot-before.bin gdp075fw1-snapshot-after.bin
```

`FULL FLASH MATCH` demonstrates stability over that tested restart. If it
differs, rerun without `--strict` to locate the changed region by byte count.
Capturing/restoring data does not itself disable future writes. A saved full
dump can be used as a personalized backup for the same ESP32/compatible PHY
library; do not distribute it as a generic factory image. It may contain
Wi-Fi passwords and uploaded pictures. Keep it private and outside Git.

## Reproduce the binary analysis

The supplied image is intentionally never modified. Validate its application
headers and checksums:

```sh
python3 scripts/analyze_image.py GDP075FW1-backup.bin
python3 scripts/extract_app0.py GDP075FW1-backup.bin app0-original.bin
```

`app0-original.bin` is an extraction artefact and should not be committed.

## Hardware pins

The driver uses CS=GPIO27, DC=GPIO14, RESET=GPIO12, BUSY=GPIO13,
SCK=GPIO18, MOSI=GPIO23. CS/DC/RESET/BUSY are confirmed by the vendor's GPIO
calls; SPI uses the classic ESP32 VSPI default pins at 10 MHz. The vendor's
initialization, per-byte CS/DC, BUSY handling and refresh commands are
implemented in `src/vendor_display.h`, with GxEPD2 providing the controller
base and ESP32 SPI setup. Status rendering uses the recovered native font
tables rather than Adafruit text drawing. Physical panel operation
and Linux image upload have been confirmed by the user on their board;
ImageToWiFi interoperability remains unverified.
