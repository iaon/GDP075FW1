import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from make_refresh_patterns import make_frames, packed_frame, ppm_bytes


def pixel(frame, x, y):
    value = frame[(y * 800 + x) // 4]
    return (value >> (2 * (3 - x % 4))) & 3


class RefreshPatternChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline, cls.candidate, cls.expected = make_frames()

    def test_native_packing_and_size(self):
        pattern = packed_frame(lambda x, y: x % 4)
        self.assertEqual(pattern, bytes((0x1b,)) * 96000)
        for frame in (self.baseline, self.candidate, self.expected):
            self.assertEqual(len(frame), 96000)

    def test_candidate_changes_inside_and_outside_and_exercises_all_transitions(self):
        inside, outside, transitions = 0, 0, set()
        for y in range(480):
            for x in range(800):
                before = pixel(self.baseline, x, y)
                after = pixel(self.candidate, x, y)
                transitions.add((before, after))
                if before != after:
                    if 200 <= x <= 599 and 120 <= y <= 359:
                        inside += 1
                    else:
                        outside += 1
        self.assertGreater(inside, 0)
        self.assertGreater(outside, 0)
        self.assertEqual(transitions, {(a, b) for a in range(4) for b in range(4)})

    def test_expected_preview_is_candidate_only_inside_exact_window(self):
        for y in range(480):
            for x in range(800):
                source = self.candidate if 200 <= x <= 599 and 120 <= y <= 359 else self.baseline
                self.assertEqual(pixel(self.expected, x, y), pixel(source, x, y))

    def test_preview_decodes_native_msb_first_and_rejects_wrong_length(self):
        preview = ppm_bytes(bytes((0x1b,)) * 96000)
        header = b"P6\n800 480\n255\n"
        self.assertTrue(preview.startswith(header))
        self.assertEqual(len(preview), len(header) + 800 * 480 * 3)
        self.assertEqual(preview[len(header):len(header) + 12],
                         bytes((0, 0, 0, 255, 255, 255, 255, 255, 0, 255, 0, 0)))
        with self.assertRaises(ValueError):
            ppm_bytes(self.baseline[:-1])


if __name__ == "__main__":
    unittest.main()
