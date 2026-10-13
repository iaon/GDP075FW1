# GDP075FW1-backup.bin analysis

This report records a reproducible static analysis of the supplied file. It
does not extract or publish the NVS values from the dump: that partition can
contain device Wi-Fi credentials.

## File identity

```text
size:    4,194,304 bytes (0x400000, 4 MiB)
SHA-256: 7d9b45de0185dbe67184de7d15a28299ee4fbeeac187849079345b6a6ba75537
```

It is a complete ESP32 flash dump, not a standalone application image. The
first bytes are erased because this layout places the bootloader at `0x1000`.
The application header identifies the classic ESP32 target (chip ID 0), not
ESP32-S3.

## Partition table

The table at `0x8000` decodes as follows.

| Name | Type | Offset | Size |
|---|---|---:|---:|
| `nvs` | data/NVS | `0x009000` | `0x005000` |
| `otadata` | data/OTA | `0x00e000` | `0x002000` |
| `app0` | app/OTA_0 | `0x010000` | `0x140000` |
| `app1` | app/OTA_1 | `0x150000` | `0x140000` |
| `spiffs` | data/SPIFFS | `0x290000` | `0x170000` |

`app0` is a normal ESP image and is internally valid. It has six segments,
entry address `0x40082b20`, a correct XOR checksum, and a correct appended
SHA-256. Its compact end is `0x0bfec0`; its size from `0x10000` is `0xafec0`
(720,576 bytes).

`app1` does not begin with ESP image magic `0xe9`. A later full-dump scan
identified a valid seven-segment classic ESP32 image at `0x100000`, ending at
`0x24ff50`, with correct XOR and SHA-256. Its public app descriptor names the
project `esp-at`, version `v2.1.0.0-rc2_esp32-19-g0ad6331-`, IDF
`v4.0.1-193-ge7ac221`, dated July 28, 2020. The current `app1` offset
`0x150000` lies inside this image's executable segment, not at its header.
Thus the bytes previously described as opaque app1 data have an identified
structure. They are plausibly leftovers of an earlier flash layout, but the
reason this image remains in the dump is not established. It is not a valid
OTA image at the current app1 partition start. OTA metadata selects sequence
number 1, corresponding to the reconstructed app0 path.

## Recovered firmware behaviour

The following strings are in `app0` and establish the behaviour independently
of the product name:

- Wi-Fi setup, success/failure, and connection-status messages;
- a USB/UART provisioning flow for Wi-Fi name, password, and static host
  number;
- persisted configuration names `wifi_config`, `host_number`, and
  `restart_counter`;
- Arduino core build markers: Arduino-ESP32 2.0.5 and ESP-IDF 4.4.2.

The flash NVS region is populated. Its contents must be considered sensitive
and are intentionally omitted from this report.

### Correction after targeted Xtensa disassembly

The initial project was an open replacement based mainly on strings and image
headers, not a complete decompilation. A subsequent audit of executable
instructions recovered the following. These are ESP32 virtual addresses.
Factory credentials below are application literals, not private NVS values.

| Address | Evidence |
|---|---|
| `0x400d28b0` | Default SSID `Goodisplay`, password `12345678`, TCP server constructor |
| initialized data `0x3ffbdb78` | Default host number `208` (`0xd0`) |
| initialized data `0x3ffbdb68..0x3ffbdb74` | NVS names `host_number`, `password`, `ssid`, `wifi_config` |
| `0x400d290c` | 64-byte UART configuration packet, fixed field offsets and final CRLF |
| `0x400d2b30` | Station connection with a 30-second wait; handles UART while waiting |
| `0x400d2bc0` | Learns gateway through DHCP, then constructs a static `/24` address and uses gateway for both DNS addresses |
| `0x400d2ce4` | GPIO setup, 10 MHz SPI, 115200-baud UART and saved Wi-Fi NVS configuration |
| `0x400d5334`, `0x400d53b4` | Preferences setter/getter use signed I32 NVS type `0x14`, including host and restart count |
| `0x400d3030`, `0x400d30ac` | Approximately 30-second Wi-Fi checks/reconnection, status display and TCP image reception |
| `0x400d2718`, `0x400d27dc` | Success/failure screens with SSID, password and static host number |
| `0x400d2568` | Panel reset and controller initialization commands |
| `0x400d26d0`, `0x400d26f4` | SPI command/data writes: CS=27, DC=14; other GPIO calls confirm RESET=12, BUSY=13 |

The TCP constructor stores `0x00041f90`: low 16 bits are port `8000`, the
following byte is the maximum of four clients. The receive loop writes up to
`0x17700` (96,000) bytes directly to display command `0x10`, then refreshes
when the sender closes the connection. This is a raw TCP receiver.

### UART provisioning packet

The vendor collects exactly 64 bytes before examining the packet:

| Offset | Content |
|---|---|
| `0` | SSID byte length, maximum 29 |
| `1..29` | SSID bytes, remaining slot bytes padded |
| `30` | Password byte length, maximum 29 |
| `31..59` | Password bytes, remaining slot bytes padded |
| `60` | Host field length, normally 1 |
| `61` | Raw host octet: 208 is `0xd0`, not ASCII `"208"` |
| `62..63` | CRLF (`0x0d 0x0a`) |

The original check permits host length zero but still reads byte 61. The
replacement requires length one and host `1..254`, rejects empty SSIDs and
embedded NULs, and leaves configuration unchanged on a malformed packet.
The vendor saves all three fields, prints the received configuration, counts
down `--3--`, `--2--`, `--1--`, then restarts.

Static IP uses the first three gateway octets and `host_number`, with mask
`255.255.255.0`. The `restart_counter/count` key is signed I32: the inspected
code reads it on non-power-on resets and clears it on power-on and successful
connection. No increment was found; the replacement does not invent one.

### Packed pixels and controller commands

Native data is one packed 800×480 raster, with four pixels per byte, most
significant bits first: black=`00`, white=`01`, yellow=`10`, red=`11`.
A white frame contains `0x55` bytes. The initial project's two-bitplane
format was not the vendor's native four-colour format and lost yellow.

GPIO12 is held LOW for 10 ms, HIGH for 10 ms, then GPIO13 is polled until
HIGH. Controller initialization command/data pairs from `0x400d2568` are:

```text
4D:78  00:2F,29  E3:88  50:37  61:03,20,01,E0
65:00,00,00,00  E9:01  F0:5F  30:08  04
```

Frame data follows `10`; refresh is `12:00`, power-off is `02:00`, sleep is
`07:A5`. `src/vendor_display.h` implements these commands with per-byte
CS LOW/DC/transfer/CS HIGH, as in `0x400d26d0` and `0x400d26f4`. GxEPD2
provides paged drawing; the newer driver's partial-window command `83` and
an extra initialization reset/blank frame write are not used. BUSY waits have
a 50-second limit with a sticky error propagated to upload callers. Cleanup
does not turn a failed refresh into success.

The product documentation identifies the hardware as ESP32-M075 with
ESP32-WROOM-32D and a 800×480 four-colour GDEM075F52 e-paper panel. The
matching controller family is JD79665AA. This is consistent with the supplied
image's ESP32 target and with the vendor firmware's Wi-Fi-signage strings.

## What is and is not reconstructed

Confirmed from the dump:

- flash size and partition layout;
- ESP32 application format, segment layout, checksum, and hash;
- Arduino/ESP-IDF generation;
- Wi-Fi provisioning/status behaviour and NVS use.
- factory Wi-Fi defaults, static-host addressing and signed NVS key types;
- 64-byte UART packet and TCP port 8000 native-image receive loop;
- panel GPIO calls, SPI clock, reset and controller command sequence.

Confirmed from matching device documentation:

- ESP32-M075 / ESP32-WROOM-32D;
- GDEM075F52, 800×480, four-colour e-paper panel.

Implemented with deliberate differences from the vendor:

- optional text provisioning alongside the recovered UART binary packet;
- HTTP endpoints alongside the recovered TCP receiver;
- SPIFFS staging rather than direct TCP-to-display streaming;
- strict complete-frame checks and bounded receive/BUSY timeouts;
- paged GxEPD2 text drawing: exact vendor fonts and screen layout are not
  reproduced.

Still unverified on hardware: ImageToWiFi interoperability, physical panel
operation and board revision. The additional ESP-AT image is identified, but
the purpose of retaining it is unverified.
This is a targeted reconstruction of recovered paths, not a claim that every
function or asset in the original firmware has been decompiled.

## Commands

```sh
sha256sum GDP075FW1-backup.bin
xxd -g 1 -s 0x8000 -l 192 GDP075FW1-backup.bin
python3 scripts/analyze_image.py GDP075FW1-backup.bin
python3 scripts/scan_images.py GDP075FW1-backup.bin
python3 scripts/extract_app0.py GDP075FW1-backup.bin app0-original.bin
strings -a -t x -n 5 GDP075FW1-backup.bin | less
python3 scripts/inspect_vendor.py GDP075FW1-backup.bin 0x400d28b0 0x400d28ec
python3 scripts/inspect_vendor.py GDP075FW1-backup.bin 0x400d290c 0x400d2b30
python3 scripts/inspect_vendor.py GDP075FW1-backup.bin 0x400d2bc0 0x400d2c68
python3 scripts/inspect_vendor.py GDP075FW1-backup.bin 0x400d2568 0x400d2620
```

The image/header scripts use only the Python standard library. The
disassembly helper uses PlatformIO's local Xtensa objcopy/objdump, maps only
application segments, and annotates `l32r` with literal values and strings.
Its temporary ELF is an analysis container, not the original linker's ELF.
Start disassembly at a known function or branch target: decoding literal pools
or padding as instructions can lose alignment. All scripts leave the original
dump unchanged.

## Host verification boundary

`python3 scripts/run_tests.py --sanitize` executes the real display driver,
Wi-Fi manager and frame receiver against simulated hardware/storage/socket
interfaces, as well as protocol tests and Python image/sender tests. The GPIO
trace checks the order of CS/DC for every transmitted byte and the selected
10 MHz clock. Fault injection covers BUSY timeout in reset, power-on, refresh
and power-off, plus recovery, missing/stale Wi-Fi events, signed NVS types,
serial fragmentation, TCP EOF, receive timeout (including millis wrap), short
storage writes and ownership conflicts. Release checks validate both ESP
image hashes/checksums and the complete flash layout.

These checks prove logic under the simulated conditions. They cannot prove
ESP32 SDK timing, radio behaviour, physical NVS persistence, SPI electrical
timing, panel output, or ImageToWiFi compatibility. Those still require the
same scenarios on a board and comparison with an original-firmware capture.
