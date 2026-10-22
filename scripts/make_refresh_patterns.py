#!/usr/bin/env python3
"""Create native frames and PPM previews for manual refresh-mode comparisons.

Uses only the Python standard library. It never connects to a device. The
candidate deliberately differs both inside and outside the test window.
"""

import argparse
from pathlib import Path


WIDTH = 800
HEIGHT = 480
ROW_BYTES = WIDTH // 4
FRAME_BYTES = ROW_BYTES * HEIGHT
WINDOW = (200, 120, 400, 240)
PALETTE = ((0, 0, 0), (255, 255, 255), (255, 255, 0), (255, 0, 0))


def baseline_color(x, y):
    """Vertical four-colour bars, with a fine black/white strip at the bottom."""
    if y >= 440:
        return ((x // 4) + (y // 4)) & 1
    return (x // 50) & 3


def candidate_color(x, y):
    """Horizontal bars, with a fine yellow/red strip at the bottom."""
    if y >= 440:
        return 2 + (((x // 4) + (y // 4)) & 1)
    return ((y // 30) + 1) & 3


def in_window(x, y):
    left, top, width, height = WINDOW
    return left <= x < left + width and top <= y < top + height


def packed_frame(pixel):
    frame = bytearray(FRAME_BYTES)
    for y in range(HEIGHT):
        for x in range(0, WIDTH, 4):
            value = 0
            for offset in range(4):
                colour = pixel(x + offset, y)
                if not 0 <= colour <= 3:
                    raise ValueError("native colour must be an index in 0..3")
                value = (value << 2) | colour
            frame[y * ROW_BYTES + x // 4] = value
    return bytes(frame)


def make_frames():
    baseline = packed_frame(baseline_color)
    candidate = packed_frame(candidate_color)
    expected = packed_frame(lambda x, y: candidate_color(x, y) if in_window(x, y)
                            else baseline_color(x, y))
    return baseline, candidate, expected


def ppm_bytes(frame):
    """P6 preview decoded from the packed bytes that are sent to the panel."""
    if len(frame) != FRAME_BYTES:
        raise ValueError("frame must contain exactly 96000 bytes")
    image = bytearray(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii"))
    for value in frame:
        for shift in (6, 4, 2, 0):
            image.extend(PALETTE[(value >> shift) & 3])
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path,
                        help="output directory; replaces only the five named pattern files")
    args = parser.parse_args()
    baseline, candidate, expected = make_frames()
    args.directory.mkdir(parents=True, exist_ok=True)
    for name, frame in (("baseline", baseline), ("candidate", candidate)):
        (args.directory / f"{name}.native").write_bytes(frame)
        (args.directory / f"{name}.ppm").write_bytes(ppm_bytes(frame))
    (args.directory / "partial-expected.ppm").write_bytes(ppm_bytes(expected))
    print(f"Created two {FRAME_BYTES}-byte native frames and three PPM previews in {args.directory}")
    print("Window: x=200&y=120&w=400&h=240 (x=200..599, y=120..359)")
    print("Full refresh should show candidate.ppm; a successful window refresh should show partial-expected.ppm.")
    print("No frame was sent. See docs/REFRESH-MODES.md for the baseline-before-each-test procedure.")


if __name__ == "__main__":
    main()
