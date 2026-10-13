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
- drives the 800×480 four-colour panel;
- accepts a full packed native frame on TCP port `8000` or `POST /frame/native`;
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

For compatibility with the initial open replacement, `POST /frame` still
accepts two 48,000-byte bitplanes (black/white first, chromatic second). That
legacy format provides black, white and red; it is not the vendor's native
four-colour format and cannot represent yellow.

SPIFFS is automatically formatted only when its entire partition is erased
(`0xff`), so an application update does not silently erase vendor files.

Incomplete, oversized, stalled, or failed uploads are rejected without a
display refresh. The sender must close TCP after the frame; the receive idle
timeout is 30 seconds. A stuck BUSY signal is reported as a display failure,
including HTTP status 500; a new frame can retry after the fault clears.

## Build

Use a project-local PlatformIO Core 6 environment, as in `sd-app0-arduino`:

```sh
python3 -m venv .venv
./.venv/bin/pip install "platformio==6.2.0"
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

The configuration pins `espressif32@7.1.3`. It builds an ordinary ESP32 image
for the `wemos_d1_mini32` board definition, chosen for ESP32-WROOM-32D; it
does not target ESP32-S3.

The display dependencies are pinned to GxEPD2 1.6.9, Adafruit GFX 1.12.6 and
Adafruit BusIO 1.17.4 so a later library release does not silently change the
controller implementation.

## Checks without a board

```sh
python3 scripts/run_tests.py --sanitize
```

This compiles the actual protocol, display driver, Wi-Fi manager and frame
receiver on the host, using simulated GPIO/SPI, clock, serial, NVS and network
events. Tests cover timeout/error propagation and recovery, per-byte CS/DC,
DHCP-to-static transitions, SDK auto-reconnection, fragmented UART/TCP input,
storage failures and HTTP/TCP ownership. Python tests cover sender framing and
ESP image checksum/hash rejection. They do not emulate ESP32 radio, physical
flash or the panel. GitHub Actions runs the same checks before building.

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

Use the recovered binary provisioning protocol:

```sh
./.venv/bin/python scripts/device.py wifi --port /dev/ttyUSB0 \
  --ssid 'your-ssid' --password 'your-password' --host 208
```

Or open a 115200-baud serial terminal and send the project's text convenience
command (quotation marks support spaces):

```text
WIFI "your-ssid" "your-password" 208
```

The host argument is optional in the text command and preserves the current
host. Binary provisioning limits SSID and password to 29 bytes each; the text
command supports 32-byte SSIDs and 64-byte passwords. Use `""` for an open
network's empty password.

The firmware persists `ssid`, `password`, and signed 32-bit `host_number`
under the vendor's `wifi_config` NVS namespace and restarts after a countdown.
The original dump includes an NVS partition, so flashing only the application
preserves its existing configuration. Erasing or overwriting NVS removes it.

## Flashing

Back up the device before replacing its firmware. A full original dump may
contain Wi-Fi credentials, so keep it private. For a development application
upload, PlatformIO writes the generated application to `0x10000`:

```sh
scripts/pio.sh -t upload --upload-port /dev/ttyUSB0
```

The matching partition layout is in [partitions.csv](partitions.csv): `app0`
begins at `0x10000`, `app1` at `0x150000`, and SPIFFS at `0x290000`.

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
implemented in `src/vendor_display.h`, with GxEPD2 providing paged drawing and
ESP32 SPI setup. Physical panel operation
and ImageToWiFi interoperability still need hardware verification.
