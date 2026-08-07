"""Unit tests for the iNES / NES 2.0 header parser.

Run with:  python -m unittest discover -s test-framework/tests
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from rnestest.romheader import (FMT_INES, FMT_NES2, FMT_UNKNOWN,  # noqa: E402
                                parse_rom_header)


def make_header(prg=1, chr=1, f6=0, f7=0, f8=0,
                f9=0, f10=0, f11=0, f12=0, f13=0, f14=0, f15=0) -> bytes:
    return b"NES\x1a" + bytes([prg, chr, f6, f7, f8,
                               f9, f10, f11, f12, f13, f14, f15])


def parse_bytes(data: bytes):
    with tempfile.NamedTemporaryFile(suffix=".nes", delete=False) as tf:
        tf.write(data)
        path = tf.name
    try:
        return parse_rom_header(path)
    finally:
        os.unlink(path)


class TestRomHeader(unittest.TestCase):
    def test_ines_mapper0(self):
        info = parse_bytes(make_header(f6=0x00, f7=0x00))
        self.assertTrue(info.valid)
        self.assertEqual(info.fmt, FMT_INES)
        self.assertEqual(info.mapper, 0)

    def test_ines_mapper1(self):
        # flag6 high nibble = 1
        info = parse_bytes(make_header(f6=0x10))
        self.assertEqual(info.mapper, 1)
        self.assertEqual(info.fmt, FMT_INES)

    def test_ines_mapper4(self):
        info = parse_bytes(make_header(f6=0x40))
        self.assertEqual(info.mapper, 4)

    def test_ines_mapper66(self):
        # mapper 66 = 0x42 -> low nibble 2 (flag6=0x20), high nibble 4 (flag7=0x40)
        info = parse_bytes(make_header(f6=0x20, f7=0x40))
        self.assertEqual(info.mapper, 66)

    def test_ines_requires_zero_tail(self):
        # Nonzero byte 12 with flag7 low bits 0 -> not plain iNES (unknown fmt)
        info = parse_bytes(make_header(f6=0x10, f7=0x00, f12=0x01))
        self.assertTrue(info.valid)
        self.assertEqual(info.fmt, FMT_UNKNOWN)

    def test_nes2_detection_and_mapper(self):
        # rNES treats (flag7 & 0x0C) == 0x0C as NES 2.0
        info = parse_bytes(make_header(f6=0x50, f7=0x0C))
        self.assertTrue(info.valid)
        self.assertEqual(info.fmt, FMT_NES2)
        self.assertEqual(info.mapper, 5)

    def test_nes2_high_mapper_256(self):
        info = parse_bytes(make_header(f6=0x00, f7=0x0C, f8=0x01))
        self.assertEqual(info.fmt, FMT_NES2)
        self.assertEqual(info.mapper, 256)

    def test_nes2_submapper(self):
        info = parse_bytes(make_header(f6=0x50, f7=0x0C, f8=0x30))
        self.assertEqual(info.submapper, 3)

    def test_standard_nes2_flag_is_unknown_but_mapper_ok(self):
        # A real NES 2.0 ROM uses (flag7 & 0x0C) == 0x08; rNES calls that
        # "unknown" but still decodes the mapper with the NES 2.0 formula.
        info = parse_bytes(make_header(f6=0x50, f7=0x08))
        self.assertEqual(info.fmt, FMT_UNKNOWN)
        self.assertEqual(info.mapper, 5)

    def test_invalid_magic(self):
        info = parse_bytes(b"XYZ\x00" + bytes(12))
        self.assertFalse(info.valid)

    def test_too_short(self):
        info = parse_bytes(b"NES\x1a\x01")
        self.assertFalse(info.valid)

    def test_missing_file(self):
        info = parse_rom_header("/no/such/rom/file.nes")
        self.assertFalse(info.valid)


if __name__ == "__main__":
    unittest.main()
