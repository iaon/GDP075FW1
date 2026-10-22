#!/usr/bin/env python3
"""Generate previews or run ONE manual EPD experiment. Python stdlib only.

No POST retries or redirects. A timeout means unknown panel state. LUT and
differential codes are host research data, never transmitted as native pixels.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import http.client
import json
import math
import os
from pathlib import Path
import re
import socket
import time
from urllib.parse import urlencode, urlsplit

from make_refresh_patterns import (FRAME_BYTES, WIDTH, HEIGHT, WINDOW, make_frames,
                                   packed_frame, ppm_bytes)

BW_WINDOW = (200, 120, 80, 40)
INITIAL_PLL = {0x08, 0x09, 0x0a, 0x02, 0x03, 0x04}
RATES = (12.5, 25, 50, 65, 75, 85, 100, 120)
LUT_BLOCKED = "JD79665AA register-LUT format and validated panel profile unavailable"
TRANSITIONS = {(1, 1): 0, (1, 0): 1, (0, 1): 2, (0, 0): 3}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pixel(frame, x, y):
    return (frame[y * 200 + x // 4] >> (6 - 2 * (x % 4))) & 3


def validate_window(window, small=False):
    if window is None or len(window) != 4:
        raise ValueError("supply X Y W H window")
    x, y, w, h = window
    if (any(type(v) is not int for v in window) or x < 0 or y < 0 or w < 4 or h < 2
            or x % 4 or w % 4 or x + w > WIDTH or y + h > HEIGHT
            or (small and w * h > 4096)):
        raise ValueError("invalid window (small B/W shadow limited to 4096 pixels)")


def mask(frame, window):
    validate_window(window, small=True)
    if len(frame) != FRAME_BYTES:
        raise ValueError("expected a full native frame")
    x, y, w, h = window
    result = bytearray((w * h + 7) // 8)
    for row in range(h):
        for col in range(w):
            value = pixel(frame, x + col, y + row)
            if value not in (0, 1):
                raise ValueError("B/W encoder rejects red/yellow inside its window")
            i = row * w + col
            result[i // 8] |= value << (7 - i % 8)
    return bytes(result)


class BwSession:
    """HOST ONLY: expected 1-bpp state, never optical feedback or a device session.

    Establish only after a known successful baseline operation. Invalidations
    include reboot, sleep/reset uncertainty, error, external upload and status
    redraw. No current device endpoint uses this until a LUT format is known.
    """
    def __init__(self, window=BW_WINDOW):
        validate_window(window, small=True)
        self.window, self.base_id, self.shadow = window, 0, None
        self.pending = None

    def invalidate(self):
        self.base_id += 1
        self.shadow = self.pending = None

    def establish(self, frame, successful):
        self.invalidate()
        if successful:
            self.shadow = mask(frame, self.window)
        return self.base_id

    def encode(self, frame, base_id):
        if self.shadow is None or base_id != self.base_id or self.pending is not None:
            raise ValueError("stale/unknown base_id or operation pending")
        new = mask(frame, self.window)
        w, h = self.window[2:]
        rows = []
        for row in range(h):
            codes = bytearray(w // 4)
            for col in range(w):
                i = row * w + col
                before = (self.shadow[i // 8] >> (7 - i % 8)) & 1
                after = (new[i // 8] >> (7 - i % 8)) & 1
                # Logical transition IDs; NO claim of JD79665AA LUT mapping.
                codes[col // 4] |= TRANSITIONS[before, after] << (6 - 2 * (col % 4))
            rows.append(bytes(codes))
        self.pending = new
        return rows

    def complete(self, successful):
        if self.pending is None:
            raise ValueError("no pending operation")
        if not successful:
            self.invalidate()
        else:
            self.shadow, self.pending = self.pending, None
            self.base_id += 1
        return self.base_id


def validate_lut_profile(profile):
    """Validate provenance/integrity first, then fail closed on unknown format.

    This metadata schema intentionally does not define commands or electrical
    phases. No trusted register format/profile is available in this revision.
    """
    required = {"id", "controller", "source_url", "revision", "format_source",
                "payload_hex", "sha256", "constraints", "transitions"}
    if set(profile) != required or profile["controller"] != "JD79665AA":
        raise ValueError("incomplete/unknown profile metadata or wrong controller")
    if not all(profile[key] for key in required):
        raise ValueError("empty profile metadata")
    if not profile["source_url"].startswith("https://"):
        raise ValueError("profile needs a source URL")
    try:
        payload = bytes.fromhex(profile["payload_hex"])
    except (TypeError, ValueError) as exc:
        raise ValueError("invalid profile bytes") from exc
    if not payload or sha(payload) != profile["sha256"]:
        raise ValueError("profile payload/hash mismatch")
    raise ValueError(LUT_BLOCKED + "; no trusted format_source or upload implementation")


def compose(before, after, window):
    validate_window(window)
    x, y, w, h = window
    return packed_frame(lambda px, py: pixel(after if x <= px < x+w and y <= py < y+h
                                          else before, px, py))


def research_frames():
    a, b, expected = make_frames()
    x, y, w, h = BW_WINDOW

    def background(px, py):
        # Static colour controls outside the entire small B/W test window.
        return 2 if px < 80 else 3 if px >= 720 else 1

    def bw(which):
        return packed_frame(lambda px, py:
            (int(px < x+w//2) if which == 0 else int(py < y+h//2))
            if x <= px < x+w and y <= py < y+h else background(px, py))

    def square(offset):
        return packed_frame(lambda px, py: 0
                            if x+offset <= px < x+offset+24 and y+8 <= py < y+32
                            else background(px, py))

    bw_a, bw_b, sq_a, sq_b = bw(0), bw(1), square(12), square(24)
    return {"baseline": a, "candidate": b, "partial-expected": expected,
            "bw-before": bw_a, "bw-after": bw_b, "square-before": sq_a,
            "square-after": sq_b, "unchanged": bw_a,
            "bw-expected": compose(bw_a, bw_b, BW_WINDOW),
            "square-expected": compose(sq_a, sq_b, BW_WINDOW),
            "white": bytes([0x55]) * FRAME_BYTES}


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    frames = research_frames()
    manifest = {"encoding": "native_2bpp_BWYR", "window": WINDOW, "bw_window": BW_WINDOW,
                "transition_encoding": "host-only WW=0,WB=1,BW=2,BB=3; NOT native colours",
                "lut_available": False, "blocked_reason": LUT_BLOCKED, "files": {}}
    for name, frame in frames.items():
        for suffix, data in (("native", frame), ("ppm", ppm_bytes(frame))):
            filename = f"{name}.{suffix}"
            (directory / filename).write_bytes(data)
            manifest["files"][filename] = {"sha256": sha(data), "bytes": len(data)}
    session = BwSession()
    session.establish(frames["bw-before"], True)
    transitions = b"".join(session.encode(frames["bw-after"], session.base_id))
    (directory / "bw-transitions.host-only").write_bytes(transitions)
    manifest["files"]["bw-transitions.host-only"] = {"sha256": sha(transitions), "bytes": len(transitions)}
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Created native files and PPM previews in {directory}; no device contacted.")


def pll_byte(text):
    if not re.fullmatch(r"0[xX][0-9a-fA-F]{2}", text):
        raise argparse.ArgumentTypeError("PLL must be exactly 0xHH")
    value = int(text, 16)
    if value > 15:
        raise argparse.ArgumentTypeError("PLL reserved upper bits must be zero")
    return value


def parse_report(body):
    result = {}
    for line in body.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            if key in result:
                raise ValueError("duplicate response field: " + key)
            result[key] = value
    return result


def request_once(url, path, frame=None, experimental=False, timeout=300):
    parsed = urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.path not in ("", "/") \
            or parsed.query or parsed.fragment or parsed.username or parsed.password:
        raise ValueError("EPD_URL must be http(s)://host[:port] without path/query/credentials")
    connection_type = http.client.HTTPSConnection if parsed.scheme == "https" else http.client.HTTPConnection
    connection = connection_type(parsed.hostname, parsed.port, timeout=timeout)
    headers = {"Connection": "close"}
    body = None
    if experimental:
        headers["X-EPD-Experimental"] = "1"
    if frame is not None:
        # Boundary cannot occur in this payload; avoid ambiguity in multipart.
        boundary = "epd-" + sha(frame)[:32]
        while boundary.encode() in frame:
            boundary += "x"
        headers["Content-Type"] = "multipart/form-data; boundary=" + boundary
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="frame"; '
                'filename="frame.native"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
        body += frame + f"\r\n--{boundary}--\r\n".encode()
    try:
        connection.request("POST" if frame is not None else "GET", path, body=body, headers=headers)
        response = connection.getresponse()
        raw = response.read().decode("utf-8", errors="replace")
        return {"http_status": response.status, "raw_response": raw, "report": parse_report(raw)}
    finally:
        connection.close()


def save_result(directory, record):
    directory.mkdir(parents=True, exist_ok=True)
    stem = record["run_id"]
    (directory / f"{stem}.json").write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n")
    columns = ["run_id", "outcome", "step", "url", "path", "baseline_sha256", "candidate_sha256",
               "firmware_version", "firmware_git", "request_id", "pll_requested", "pll_written",
               "Dyna", "FR", "nominal_hz", "upload_ms", "init_ms", "lut_load_ms", "write_ms",
               "refresh_ms", "cleanup_ms", "total_ms", "busy_assert_seen", "cleanup_state",
               "temperature_c", "notes"]
    for result in record["responses"]:
        (directory / f'{stem}-{result["step"]}.txt').write_text(result["raw_response"])
    row = dict(record)
    if record["responses"]:
        row.update(record["responses"][-1]["report"])
    csv_path = directory / "results.csv"
    new = not csv_path.exists()
    with csv_path.open("a", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        if new:
            writer.writeheader()
        writer.writerow(row)


def measure(args, sender=request_once):
    if not math.isfinite(args.interval) or not math.isfinite(args.timeout) or \
            (args.temperature is not None and not math.isfinite(args.temperature)):
        raise ValueError("interval, timeout and manual temperature must be finite numbers")
    if args.pll is not None and args.pll not in INITIAL_PLL and not args.extended:
        raise ValueError("this PLL requires explicit --extended")
    if args.extended and args.pll is None:
        raise ValueError("--extended requires --pll")
    if args.count != 1 and not args.series:
        raise ValueError("--count>1 requires --series")
    if not 1 <= args.count <= 5 or args.interval < 0 or (args.series and args.interval < 1):
        raise ValueError("series limited to 1..5 experiments; explicit interval >=1 second required")
    if args.timeout < 300:
        raise ValueError("use timeout >=300 seconds for experimental PLL")
    if not args.url:
        raise ValueError("set EPD_URL or --url")
    baseline, candidate = args.baseline.read_bytes(), args.candidate.read_bytes()
    if len(baseline) != FRAME_BYTES or len(candidate) != FRAME_BYTES:
        raise ValueError("baseline and candidate must each contain exactly 96000 bytes")
    window_mode = args.base in ("partial", "fast-window")
    if window_mode:
        validate_window(args.window)
    elif args.window is not None:
        raise ValueError("window forbidden for full base")
    parameters = {}
    if args.pll is not None:
        parameters = {"base": args.base, "pll": f"0x{args.pll:02x}"}
        path = "/frame/native/pll"
        if args.extended:
            parameters["extended"] = "1"
    else:
        path = {"full": "/frame/native", "fast-full": "/frame/native/fast-full",
                "partial": "/frame/native/partial", "fast-window": "/frame/native/fast-window"}[args.base]
    if window_mode:
        parameters.update(zip(("x", "y", "w", "h"), args.window))
    if parameters:
        path += "?" + urlencode(parameters)
    plan = {"url": args.url, "steps": ["GET /display/experiments", "POST /frame/native (baseline)",
                                       "POST " + path], "count": args.count,
            "interval_seconds": args.interval, "baseline_sha256": sha(baseline),
            "candidate_sha256": sha(candidate), "restore_after_success": False,
            "baseline_file": str(args.baseline), "candidate_file": str(args.candidate),
            "retry": False, "nominal_hz": None if args.pll is None else RATES[args.pll & 7]}
    if args.dry_run:
        print(json.dumps(plan, indent=2))
        return 0
    for index in range(args.count):
        record = {**plan, "path": path, "parameters": parameters,
                  "run_id": datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"),
                  "temperature_c": args.temperature, "notes": args.notes,
                  "responses": [], "outcome": "unknown", "step": "capabilities"}
        try:
            for step, route, frame in (("capabilities", "/display/experiments", None),
                                       ("baseline", "/frame/native", baseline),
                                       ("experiment", path, candidate)):
                record["step"] = step
                response = sender(args.url, route, frame=frame,
                                  experimental=step == "experiment" and args.pll is not None,
                                  timeout=args.timeout)
                record["responses"].append({"step": step, **response})
                print(response["raw_response"], flush=True)
                if response["http_status"] != 200:
                    record["outcome"] = "http_error"
                    raise ValueError("HTTP failure; stopping without retry or cleanup refresh")
                if step != "capabilities" and (response["report"].get("result") != "ok" or
                                               response["report"].get("refresh_started") != "true"):
                    record["outcome"] = "invalid_response"
                    raise ValueError("missing/failed display result; stopping")
                if step != "capabilities" and response["report"].get("busy_assert_seen") != "true":
                    record["outcome"] = "busy_anomaly"
                    raise ValueError("display operation did not report BUSY assert; outcome unknown")
            record["outcome"] = "completed_expected_state_only"
        except (OSError, http.client.HTTPException, ValueError) as exc:
            record["error"] = str(exc)
            if isinstance(exc, (TimeoutError, socket.timeout, OSError, http.client.HTTPException)):
                record["outcome"] = "transport_error_outcome_unknown"
            save_result(args.output, record)
            print(f"Stopped: {exc}. No POST retry. Results: {args.output}")
            return 1
        save_result(args.output, record)
        if index + 1 < args.count:
            time.sleep(args.interval)
    print(f"Stopped for visual inspection; no cleanup refresh. Results: {args.output}")
    return 0


def parser():
    root = argparse.ArgumentParser(description=__doc__)
    sub = root.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate", help="generate files only; no network")
    gen.add_argument("directory", type=Path)
    run = sub.add_parser("measure", help="restore baseline, run one experiment and save results")
    run.add_argument("--url", default=os.environ.get("EPD_URL"))
    run.add_argument("--baseline", type=Path, required=True)
    run.add_argument("--candidate", type=Path, required=True)
    run.add_argument("--base", choices=("full", "fast-full", "partial", "fast-window"), default="fast-full")
    run.add_argument("--pll", type=pll_byte)
    run.add_argument("--extended", action="store_true")
    run.add_argument("--window", nargs=4, type=int, metavar=("X", "Y", "W", "H"))
    run.add_argument("--dry-run", action="store_true")
    run.add_argument("--output", type=Path, default=Path("/tmp/gdp075fw1-results"))
    run.add_argument("--temperature", type=float, help="manually measured ambient Celsius")
    run.add_argument("--notes", default="", help="manual ghosting/colour/flicker notes")
    run.add_argument("--timeout", type=float, default=300)
    run.add_argument("--series", action="store_true", help="explicit finite series opt-in; no safety promise")
    run.add_argument("--count", type=int, default=1)
    run.add_argument("--interval", type=float, default=0, help="explicit series interval in seconds")
    return root


def main():
    command_parser = parser()
    args = command_parser.parse_args()
    try:
        if args.command == "generate":
            generate(args.directory)
            return 0
        return measure(args)
    except (ValueError, OSError) as exc:
        command_parser.error(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
