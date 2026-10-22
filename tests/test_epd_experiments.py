import contextlib
import http.client
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import epd_experiments as epd
from make_refresh_patterns import PALETTE


class ExperimentTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frames = epd.research_frames()

    def test_patterns_and_previews(self):
        frames = self.frames
        for frame in frames.values():
            self.assertEqual(len(frame), 96000)
            self.assertEqual(epd.ppm_bytes(frame)[len(b"P6\n800 480\n255\n"):][:3],
                             bytes(PALETTE[epd.pixel(frame, 0, 0)]))
        x, y, w, h = epd.BW_WINDOW
        transitions = set()
        move = set()
        for py in range(480):
            for px in range(800):
                a = epd.pixel(frames["bw-before"], px, py)
                b = epd.pixel(frames["bw-after"], px, py)
                c = epd.pixel(frames["square-before"], px, py)
                d = epd.pixel(frames["square-after"], px, py)
                if x <= px < x+w and y <= py < y+h:
                    self.assertIn(a, (0, 1)); self.assertIn(b, (0, 1))
                    transitions.add((a, b)); move.add((c, d))
                else:
                    self.assertEqual(a, b)
                    self.assertEqual(c, d)
                source = "candidate" if 200 <= px < 600 and 120 <= py < 360 else "baseline"
                self.assertEqual(epd.pixel(frames["partial-expected"], px, py), epd.pixel(frames[source], px, py))
        self.assertEqual(transitions, set(epd.TRANSITIONS))
        self.assertEqual(move, set(epd.TRANSITIONS))
        self.assertEqual(frames["unchanged"], frames["bw-before"])
        self.assertEqual(epd.pixel(frames["bw-before"], 0, 0), 2)
        self.assertEqual(epd.pixel(frames["bw-before"], 799, 0), 3)

    def test_transition_encoder_and_stale_state(self):
        session = epd.BwSession()
        with self.assertRaisesRegex(ValueError, "stale"):
            session.encode(self.frames["bw-after"], 0)
        base = session.establish(self.frames["bw-before"], True)
        rows = session.encode(self.frames["bw-after"], base)
        self.assertEqual(len(session.shadow), 400)  # Small 1-bpp state only.
        self.assertEqual(len(rows), 40)
        self.assertEqual(len(rows[0]), 20)
        # WW/WB/BW/BB are distinct, not a conflated HOLD operation.
        self.assertEqual(rows[0][:10], bytes(10))
        self.assertEqual(rows[0][10:], bytes([0xaa])*10)
        self.assertEqual(rows[20][:10], bytes([0x55])*10)
        self.assertEqual(rows[20][10:], bytes([0xff])*10)
        with self.assertRaisesRegex(ValueError, "pending"):
            session.encode(self.frames["bw-after"], base)
        next_base = session.complete(True)
        with self.assertRaisesRegex(ValueError, "stale"):
            session.encode(self.frames["bw-before"], base)
        unchanged = b"".join(session.encode(self.frames["bw-after"], next_base))
        self.assertTrue(all(((v >> shift) & 3) in (0, 3) for v in unchanged for shift in (6, 4, 2, 0)))
        session.complete(False)
        self.assertIsNone(session.shadow)
        session.establish(self.frames["bw-before"], False)
        with self.assertRaisesRegex(ValueError, "stale"):
            session.encode(self.frames["bw-after"], session.base_id)
        for reason in ("reboot", "status redraw", "upload", "reset/hibernate uncertainty"):
            session.establish(self.frames["bw-before"], True)
            session.invalidate()
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, "stale"):
                session.encode(self.frames["bw-after"], session.base_id)
        with self.assertRaisesRegex(ValueError, "red/yellow"):
            epd.mask(bytes([0xaa])*96000, epd.BW_WINDOW)
        for window in (None, (1, 0, 4, 2), (0, 0, 800, 480), (796, 0, 8, 2), (0, 0, 4, 1)):
            with self.assertRaises(ValueError):
                epd.BwSession(window)

    def test_profile_validation_is_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "incomplete"):
            epd.validate_lut_profile({})
        profile = {"id": "test", "controller": "JD79665AA", "source_url": "https://example.invalid",
                   "revision": "test-only", "format_source": "unknown", "payload_hex": "00",
                   "sha256": epd.sha(b"\0"), "constraints": ["not usable"], "transitions": ["WW"]}
        with self.assertRaisesRegex(ValueError, "unavailable"):
            epd.validate_lut_profile(profile)
        profile["payload_hex"] = "01"
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            epd.validate_lut_profile(profile)
        profile["payload_hex"] = "bad-hex"
        with self.assertRaisesRegex(ValueError, "invalid profile bytes"):
            epd.validate_lut_profile(profile)
        manifest = json.loads((Path(__file__).resolve().parents[1] / "patterns/lut-profiles.json").read_text())
        self.assertEqual(manifest["profiles"], [])
        self.assertFalse(manifest["register_lut_available"])

    def test_generator_hashes_actual_bytes(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            directory = Path(temp)
            with mock.patch("socket.socket", side_effect=AssertionError("no network allowed")):
                epd.generate(directory)
            manifest = json.loads((directory / "manifest.json").read_text())
            for name, entry in manifest["files"].items():
                data = (directory / name).read_bytes()
                self.assertEqual(entry, {"sha256": epd.sha(data), "bytes": len(data)})
                if name.endswith(".ppm"):
                    self.assertEqual(data, epd.ppm_bytes((directory / name.replace(".ppm", ".native")).read_bytes()))

    def make_args(self, directory, *options):
        for name in ("baseline", "candidate"):
            (directory / f"{name}.native").write_bytes(self.frames[name])
        return epd.parser().parse_args(["measure", "--url", "http://192.0.2.1",
            "--baseline", str(directory / "baseline.native"), "--candidate", str(directory / "candidate.native"),
            "--output", str(directory / "results"), *options])

    @staticmethod
    def success(url, path, **kwargs):
        raw = ("firmware_version=test\nfirmware_git=abcdef\n" if kwargs["frame"] is None
               else "result=ok\nrefresh_started=true\nbusy_assert_seen=true\nrefresh_ms=123\n"
                    "firmware_version=test\nfirmware_git=abcdef\nrequest_id=1\n")
        return {"http_status": 200, "raw_response": raw, "report": epd.parse_report(raw)}

    def test_single_measurement_and_dry_run(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            args = self.make_args(Path(temp), "--pll", "0x09", "--dry-run")
            sender = mock.Mock(side_effect=AssertionError("dry-run called network"))
            with mock.patch("socket.socket", side_effect=AssertionError("dry-run opened socket")):
                self.assertEqual(epd.measure(args, sender), 0)
            sender.assert_not_called()
            self.assertFalse(args.output.exists())
            args.dry_run = False
            sender = mock.Mock(side_effect=self.success)
            self.assertEqual(epd.measure(args, sender), 0)
            self.assertEqual(sender.call_count, 3)
            calls = sender.call_args_list
            self.assertEqual(calls[1].args[1], "/frame/native")
            self.assertEqual(calls[2].args[1], "/frame/native/pll?base=fast-full&pll=0x09")
            self.assertTrue(calls[2].kwargs["experimental"])
            records = list(args.output.glob("*.json"))
            self.assertEqual(len(records), 1)
            record = json.loads(records[0].read_text())
            self.assertEqual(record["outcome"], "completed_expected_state_only")
            self.assertIsNone(record["temperature_c"])
            self.assertTrue((args.output / "results.csv").exists())
            self.assertEqual(len(list(args.output.glob("*.txt"))), 3)

    def test_timeout_http_error_stop_no_retry(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            args = self.make_args(Path(temp), "--pll", "0x09", "--series", "--count", "3", "--interval", "1")
            sender = mock.Mock(side_effect=[self.success(args.url, "", frame=None),
                                            self.success(args.url, "", frame=b"x"), TimeoutError("timeout")])
            self.assertEqual(epd.measure(args, sender), 1)
            self.assertEqual(sender.call_count, 3)
            record = json.loads(next(args.output.glob("*.json")).read_text())
            self.assertEqual(record["outcome"], "transport_error_outcome_unknown")
            self.assertEqual(record["step"], "experiment")
            sender = mock.Mock(return_value={"http_status": 501, "raw_response": "blocked_reason=test\n",
                                            "report": {"blocked_reason": "test"}})
            self.assertEqual(epd.measure(args, sender), 1)
            self.assertEqual(sender.call_count, 1)

    def test_client_validation_before_network(self):
        with tempfile.TemporaryDirectory() as temp:
            for options in (("--pll", "0x05"), ("--base", "partial"), ("--count", "2"),
                            ("--extended",), ("--series", "--count", "6", "--interval", "1"),
                            ("--window", "0", "0", "4", "2"), ("--timeout", "10"),
                            ("--timeout", "nan"), ("--temperature", "inf")):
                args = self.make_args(Path(temp), *options)
                with self.subTest(options=options), self.assertRaises(ValueError):
                    epd.measure(args, mock.Mock(side_effect=AssertionError("network")))
            for text in ("8", "0x8", "0x100", "0x18", "0x0aZ", " 0x09"):
                with self.assertRaises(Exception):
                    epd.pll_byte(text)
            self.assertEqual(epd.pll_byte("0X0A"), 10)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            epd.parse_report("result=ok\nresult=error\n")

    def test_http_transport_single_post_no_redirect_following(self):
        response = mock.Mock(status=302)
        response.read.return_value = b"redirect\n"
        connection = mock.Mock()
        connection.getresponse.return_value = response
        with mock.patch("http.client.HTTPConnection", return_value=connection):
            result = epd.request_once("http://192.0.2.1", "/frame/native/pll", self.frames["candidate"], True)
        self.assertEqual(result["http_status"], 302)
        self.assertEqual(connection.request.call_count, 1)
        args, kwargs = connection.request.call_args
        self.assertEqual(args[0], "POST")
        self.assertEqual(kwargs["headers"]["X-EPD-Experimental"], "1")
        self.assertIn(b'name="frame"', kwargs["body"])
        self.assertIn(self.frames["candidate"], kwargs["body"])
        connection.close.assert_called_once()

    def test_missing_busy_at_baseline_stops_before_experiment(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            args = self.make_args(Path(temp), "--pll", "0x09")
            baseline = self.success(args.url, "", frame=b"x")
            baseline["report"]["busy_assert_seen"] = "false"
            sender = mock.Mock(side_effect=[self.success(args.url, "", frame=None), baseline])
            self.assertEqual(epd.measure(args, sender), 1)
            self.assertEqual(sender.call_count, 2)
            result = json.loads(next(args.output.glob("*.json")).read_text())
            self.assertEqual(result["outcome"], "busy_anomaly")
            self.assertEqual(result["step"], "baseline")


if __name__ == "__main__":
    unittest.main()
