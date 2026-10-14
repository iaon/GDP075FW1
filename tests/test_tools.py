import hashlib
import io
import re
import struct
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from check_release import validate_image
from device import provisioning_packet
from scan_images import scan
from vendor_fonts import font_header


def esp_image():
    header = bytearray(24)
    header[0:4] = bytes((0xe9, 1, 2, 0x20))
    header[23] = 1
    descriptor = bytearray(256)
    struct.pack_into("<I", descriptor, 0, 0xabcd5432)
    descriptor[48:54] = b"esp-at"
    image = header + struct.pack("<II", 0x3f400020, len(descriptor)) + descriptor
    checksum = 0xef
    for value in descriptor:
        checksum ^= value
    image += bytes(15 - len(image) % 16) + bytes((checksum,))
    image += hashlib.sha256(image).digest()
    return image


class ImageChecks(unittest.TestCase):
    def test_valid_and_truncated(self):
        image = esp_image()
        self.assertEqual(validate_image(image, 0, len(image)), len(image))
        for length in (4, 28, 50, len(image) - 1):
            with self.assertRaises(ValueError):
                validate_image(image[:length], 0, length)

    def test_corrupted_payload_header_and_digest(self):
        for index in (0, 1, 12, 32, len(esp_image()) - 33, len(esp_image()) - 1):
            image = esp_image()
            image[index] ^= 1
            with self.assertRaises(ValueError):
                validate_image(image, 0, len(image))

    def test_nonpartition_image_scan(self):
        image = b"\xff" * 0x100000 + esp_image()
        with redirect_stdout(io.StringIO()) as output:
            scan(image)
        self.assertIn("image=0x100000", output.getvalue())
        self.assertIn("project=esp-at", output.getvalue())


class SenderChecks(unittest.TestCase):
    def test_exact_wire_layout_and_utf8_byte_lengths(self):
        packet = provisioning_packet("Goodisplay", "12345678", 208)
        self.assertEqual(len(packet), 64)
        self.assertEqual(packet[0], 10)
        self.assertEqual(packet[1:11], b"Goodisplay")
        self.assertEqual(packet[30], 8)
        self.assertEqual(packet[31:39], b"12345678")
        self.assertEqual(packet[60:], bytes((1, 208, 13, 10)))
        self.assertEqual(provisioning_packet("сеть", "", 10)[0], 8)

    def test_invalid_configuration_rejected_before_sending(self):
        for ssid, password, host in (("", "pwd", 208), ("s" * 30, "", 208),
                                      ("ssid", "p" * 30, 208), ("s\0sid", "", 208),
                                      ("ssid", "", 0), ("ssid", "", 255)):
            with self.assertRaises(ValueError):
                provisioning_packet(ssid, password, host)


class FontChecks(unittest.TestCase):
    def test_committed_fonts_have_original_hashes(self):
        header = (Path(__file__).resolve().parents[1] / "src/vendor_fonts.h").read_text()
        for name, size, expected in (
                ("kFont20", 3800, "fa3edf3b21f2dd9baaec010dc6401baf0d7f6c800bad9b795f2de4e25a1181a5"),
                ("kFont24", 6840, "e62d5ac70d4d5b233e2495db66cbdb28b4b7efbadeba172d293214b3b23402fb")):
            table = header.split(f"{name}[] =", 1)[1].split(";", 1)[0]
            glyphs = bytes(int(value, 16) for value in re.findall(r"\\x([0-9a-f]{2})", table))
            self.assertEqual(len(glyphs), size)
            self.assertEqual(hashlib.sha256(glyphs).hexdigest(), expected)

    def test_extractor_rejects_missing_or_truncated_app0(self):
        for image in (b"", b"\xff" * 0x20000):
            with self.assertRaises(ValueError):
                font_header(image)


if __name__ == "__main__":
    unittest.main()
