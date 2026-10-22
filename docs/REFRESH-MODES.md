# Refresh modes and manual A/B tests

This firmware exposes three experimental refresh routes for the GDEM075F52
800×480 four-colour panel. They let you upload your own images and compare
the controller's behaviour with the existing full refresh. The new modes
are covered by host tests. User-reported panel measurements on 2026-10-22:
full 22079–22082 ms, partial 400×240 22074 ms, fast-full 12246 ms,
fast-window 400×240 12234 ms and 40×20 12231 ms. The user reports no flicker
or image changes outside the 400×240 fast window, with mild artifacts in
fast-full. These are user observations, not automated or independently
reproduced measurements. For further PLL research and blocked register-LUT
capabilities, see [LUT/PLL experiments](LUT-PLL-EXPERIMENTS.md).

`POST /frame/native`, legacy `POST /frame`, TCP port `8000`, and the network
status picture retain the normal full-refresh behaviour. Selecting an
experimental route applies to that request; it does not change the default
mode for subsequent uploads or status drawing.

## 1. Check the available modes

Replace the address with the device's `IP Address:` value:

```sh
EPD_URL='http://192.168.1.208'
curl --fail-with-body --max-time 180 "$EPD_URL/display/modes"
```

`GET /display/modes` reports the routes, `default_mode=full`, and the
distinction between the upstream library's capabilities and this firmware's
ordinary paged interface:

| Reported field | Value | Meaning |
|---|---|---|
| `gxepd2.hasPartialUpdate` | `true` | The pinned GxEPD2 controller class contains window support |
| `gxepd2.hasFastPartialUpdate` | `false` | It does not advertise a fast partial waveform |
| `gxepd2.useFastFullUpdate` | `false` | Fast full refresh is disabled by default upstream |
| `vendor.hasPartialUpdate` | `false` | The existing vendor-compatible paged API keeps its full-frame behaviour |

The new routes use an explicit diagnostic path. They do not change
`vendor.hasPartialUpdate` to `true`, and `fast-window` does not make
`hasFastPartialUpdate` true. These are software capability/default values,
not results of automatically detecting or measuring the physical panel.

## 2. Image format and endpoint selection

Every `/frame/native...` request uses multipart field **`frame`**, containing
exactly **96,000 bytes**. It is always the entire 800×480 image, even when the
requested refresh window is small.

| Property | Value |
|---|---|
| Pixel order | Row-major; top-to-bottom rows, left-to-right pixels |
| Row size | 200 bytes |
| Packing | Four pixels per byte, most significant pair first |
| Native colour codes | Black `00`, white `01`, yellow `10`, red `11` |
| Example byte | `0x1b` means black, white, yellow, red |

| Route | Reported `mode` | Operation | `experimental` |
|---|---|---|---|
| `POST /frame/native` | `full` | Normal full-screen refresh | `false` |
| `POST /frame/native/fast-full` | `fast_full` | Fast full-screen waveform | `true` |
| `POST /frame/native/partial?x=...&y=...&w=...&h=...` | `partial` | Normal waveform with a refresh window | `true` |
| `POST /frame/native/fast-window?x=...&y=...&w=...&h=...` | `fast_window_experimental` | Fast full waveform combined with a refresh window | `true` |

The legacy `POST /frame` input remains two 48,000-byte bitplanes and uses
`mode=full`. Do not send that legacy format to the native routes.

For PNG/JPEG input, use the complete Pillow conversion example in
[Uploading a picture from Linux](../README.md#uploading-a-picture-from-linux).
It produces `frame.native`; rename or copy that output when preparing several
pictures, because running the example again replaces it. The device does not
decode PNG/JPEG, and changing the endpoint does not change the pixel format.

`scripts/device.py frame DEVICE_IP frame.native` is a TCP sender, not an image
converter or mode selector. It always uses the existing full-refresh TCP path.
Use HTTP for the experiments and their completion/timing reports.

### Strict window rules

Both window routes require all four query parameters exactly once: `x`, `y`,
`w`, `h`. Coordinates refer to the final 800×480 native image, after any host
rotation or resize, with origin at its top-left corner.

- Use non-negative integer coordinates and dimensions.
- `x` and `w` must be multiples of four; `w` must be at least four pixels.
- `h` must be at least two pixels.
- `x + w <= 800` and `y + h <= 480`.
- Missing, duplicate, malformed, misaligned or out-of-bounds parameters are
  rejected. The firmware does not silently align, clip or fall back to full.

For example, `x=200&y=120&w=400&h=240` covers pixels **x=200..599 and
y=120..359**, inclusive. A full-size window `x=0&y=0&w=800&h=480` is also
accepted by the window endpoints and still requests `PMODE=1`. It is useful
for comparing the window command path with a normal full refresh.

Keep URLs containing `&` quoted, as in the examples below.

### What a window request actually does

The upload is staged in SPIFFS using the existing bounded-memory workflow.
All 96,000 native bytes are then streamed to the **display controller's RAM**.
Only the requested physical refresh region is restricted by the window
command. There is no cropped upload, partial RAM-write optimization, or
96,000-byte ESP32 framebuffer requirement.

The intended window result is the candidate image inside the rectangle and
the previously displayed image outside it. This must be checked on the
panel. Because the controller received the complete candidate, its RAM and
the visible image outside the window can differ after a window operation.
Each subsequent request rewrites the full controller image; it does not rely
on retained RAM after sleep.

## 3. Generate controlled test images

This optional generator needs only Python's standard library and never
connects to or flashes a device:

```sh
python3 scripts/make_refresh_patterns.py /tmp/gdp075fw1-refresh
```

It creates or replaces these five named files:

| File | Purpose |
|---|---|
| `baseline.native` | Image A: vertical four-colour bars and a fine black/white strip |
| `candidate.native` | Image B: horizontal four-colour bars and a fine yellow/red strip |
| `baseline.ppm` | Preview decoded from the exact packed image A bytes |
| `candidate.ppm` | Preview decoded from the exact packed image B bytes |
| `partial-expected.ppm` | Expected A-outside/B-inside result for x=200, y=120, w=400, h=240 |

Both native files contain exactly 96,000 bytes. The pair exercises all
sixteen source-to-target colour combinations across the image. It deliberately
changes pixels both inside and outside the test rectangle. If the candidate
were identical outside the rectangle, a full refresh could be mistaken for a
successful window refresh.

Open the PPM files in an image viewer for comparison. PPM previews are not
upload payloads. The `partial-expected.ppm` file is an expected visual result,
not a measurement of panel behaviour or a colour-calibrated panel simulation.

## 4. Compare the four modes using the same A-to-B transition

Set the address and image paths once:

```sh
EPD_URL='http://192.168.1.208'
PATTERNS='/tmp/gdp075fw1-refresh'
```

Wait for each HTTP response before the next upload. Each command uses a
180-second client timeout and prints HTTP error bodies. A timeout does not
prove the device stopped its display operation; wait for it to finish or
check UART before sending another frame.

### A. Reference: normal full refresh

First display A, then measure the A-to-B full refresh:

```sh
curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/baseline.native" "$EPD_URL/frame/native"

curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/candidate.native" "$EPD_URL/frame/native"
```

The final picture should match `candidate.ppm` over the entire screen.
Record the second request's timings and photograph the final image. A normal
full refresh took about 22 seconds in the earlier user-reported board test;
use this run's measurements as the comparison baseline.

### B. Fast full refresh

Restore A with the normal full endpoint before testing B:

```sh
curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/baseline.native" "$EPD_URL/frame/native"

curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/candidate.native" "$EPD_URL/frame/native/fast-full"
```

The intended result is again `candidate.ppm`. Compare time, residual vertical
bars, colour saturation and the fine strip with the normal-full reference
result. No specific speed increase is promised by the endpoint name.

### C. Window refresh with the normal waveform

Restore A again, then send the **complete** B image with the window query:

```sh
curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/baseline.native" "$EPD_URL/frame/native"

curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/candidate.native" \
  "$EPD_URL/frame/native/partial?x=200&y=120&w=400&h=240"
```

Compare with `partial-expected.ppm`. The centre should show B's horizontal
bars; the rest should retain A's vertical bars and black/white strip. Look at
the exact rectangle boundaries, not just its centre. Record any flicker,
ghosting or colour change outside the rectangle as well as the final result.

### D. Experimental fast waveform with a window

Restore A once more before the combined experiment:

```sh
curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/baseline.native" "$EPD_URL/frame/native"

curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/candidate.native" \
  "$EPD_URL/frame/native/fast-window?x=200&y=120&w=400&h=240"
```

The intended result is the same `partial-expected.ppm` as in C. This tests a
combination of the upstream fast-full setup and the window register. It is
not evidence that the library or panel supports a distinct fast partial
waveform. Compare its actual results with both B and C.

To return to a known full image after any experiment:

```sh
curl --fail-with-body --max-time 180 \
  -F "frame=@$PATTERNS/baseline.native" "$EPD_URL/frame/native"
```

For a reverse-colour comparison, repeat the procedure with A and B swapped.
For your own pictures, replace the two native paths and select an appropriate
window; the payload remains a full frame. Changes outside the chosen window
make accidental full-screen updates visible. Use the same images, window,
panel temperature and power conditions when comparing modes.

## 5. Read the response and record results

A successful HTTP frame response preserves the initial line
`display refreshed`, followed by named diagnostic fields. The fields include:

| Field | Interpretation |
|---|---|
| `result` | `ok` or `error` for the firmware operation |
| `mode` | The explicit mode from the endpoint table above |
| `experimental` | `true` for all three new diagnostic routes |
| `window_x`, `window_y`, `window_w`, `window_h` | Requested rectangle; full screen for full modes |
| `frame_bytes` | Expected full payload size, 96000 |
| `received_bytes` | Bytes received for the staged frame |
| `refresh_started` | Whether a physical refresh command was started |
| `upload_ms` | First multipart file-start callback through completed staging |
| `init_ms` | Display initialization and the selected mode's setup |
| `write_ms` | Streaming the full staged frame into controller RAM |
| `refresh_ms` | Refresh command and completion wait |
| `sleep_ms` | Power-off/sleep cleanup |
| `total_ms` | First multipart file-start callback through display cleanup and response preparation |
| `error` | Failure description; empty on success |

`total_ms` is measured by the firmware. It excludes connection establishment
and HTTP processing before the first multipart file-start callback, so it is
not identical to curl's whole-request duration. Compare `refresh_ms` to study
the waveform/window operation and `total_ms` to see the practical upload-plus-
display latency. Small differences from the sum of stages are handling
overhead.

HTTP success means the command/BUSY sequence completed without a detected
error; the device has no optical feedback. A non-success response must be
treated as a failed experiment even if part of the panel changed. In
particular, invalid windows do not silently become full refreshes, and
`refresh_started=false` distinguishes rejection before the refresh command.

Use one row per measured A-to-B operation, excluding the baseline restoration:

| Mode / window | Temperature | Upload ms | Init ms | Write ms | Refresh ms | Sleep ms | Total ms | Outside window unchanged? | Colours / ghosting / flicker |
|---|---|---|---|---|---|---|---|---|---|
| full | | | | | | | | n/a | |
| fast full | | | | | | | | n/a | |
| partial, 200/120/400/240 | | | | | | | | | |
| fast window, 200/120/400/240 | | | | | | | | | |

Keep UART output if a BUSY timeout or another display error occurs. Record
observations outside the window even when the final pixels appear correct:
the controller's datasheet says that gates scan both inside and outside the
partial window, so a small window alone does not establish faster operation.

## 6. Source of the experiment

The project pins **GxEPD2 1.6.9**. Its
[GDEM075F52 header](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/epd4c/GxEPD2_750c_GDEM075F52.h)
sets `hasPartialUpdate=true`, `hasFastPartialUpdate=false`, and
`useFastFullUpdate=false`. It lists the same 21,000 ms nominal timing constant
for full and partial refresh, and comments that disabling fast full update
helps avoid residues and supports an extended low-temperature range. Those
constants and comments are not measurements of this firmware on your board.

The corresponding
[controller implementation](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/epd4c/GxEPD2_750c_GDEM075F52.cpp)
provides the fast initialization sequence `E0/02`, `E6/5A`, `A5/00` and the
`0x83` partial-window command. The diagnostic path uses that sourced setup;
it does not invent or upload an arbitrary LUT.

The [JD79665AA datasheet, section 8.2.24, page 52](https://www.laskakit.cz/user/related_files/jd79665aa_datasheet_v1.pdf)
describes `0x83`, the inclusive horizontal/vertical end addresses, and
`PMODE=1` for partial mode. Its horizontal address bits impose four-pixel
boundaries; the firmware exposes a strict validated rectangle instead of
silently changing the user's coordinates. The datasheet is controller-level
evidence. Final image quality, unchanged pixels outside the window and useful
speed gains still require the manual panel tests above.
