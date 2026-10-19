#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Tests for the upgrade file's layout, run by `just test-host`: the parts
source/image_layout.h places after the kernel (the FPGA firmware the update
writes, Diagnostics' LZ77 sample) must be where tools/omega_image.py puts
them, with the CRC32s the kernel checks, the official bytes up to each
part's last 256-byte page, and the whole file inside what EZ-Flash's loader
installs (3 MB, far below the kernel's data sectors and the boot loader)."""

import re
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import omega_image

IMAGE = (ROOT / "image.bin").read_bytes()
LAYOUT = omega_image.read_layout()
ROM = omega_image.ROM
# Window offset of Torii's first kernel NOR data sector (source/bootloader.h)
KERNEL_DATA = 0x780000


class Layout(unittest.TestCase):
    def test_official_image_bin(self):
        # The parts are cut from EZ-Flash's image.bin 1.06 by its offsets
        self.assertEqual(len(IMAGE), 0x1719DC)

    def test_parts_in_order_aligned_and_inside(self):
        at = LAYOUT["IMAGE_ADDRESS"] - ROM
        self.assertEqual(at, omega_image.IMAGE_OFFSET)
        end = LAYOUT["IMAGE_END"] - ROM
        for name, offset, size, _ in omega_image.parts(LAYOUT):
            with self.subTest(name):
                self.assertEqual(offset % omega_image.PAGE, 0)
                self.assertGreaterEqual(offset, at)
                at = offset + omega_image.page_end(size)
                self.assertLessEqual(at, end)
        self.assertLessEqual(end, omega_image.LOADER_MAX)
        self.assertLess(end, KERNEL_DATA)

    def test_every_part_has_its_defines(self):
        for name in omega_image.OFFICIAL:
            for suffix in ("_address", "_size"):
                self.assertIn(name + suffix, LAYOUT)
            self.assertIn(omega_image.CRC[name], LAYOUT)

    def test_addresses_defined_only_in_the_layout(self):
        # One source of truth: no other kernel file defines these names
        names = list(LAYOUT)
        for path in sorted((ROOT / "source").rglob("*.[ch]")):
            if path.name == "image_layout.h":
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for name in names:
                with self.subTest(path=path.name, name=name):
                    self.assertIsNone(
                        re.search(rf"^\s*#\s*define\s+{name}\b", text, re.MULTILINE)
                    )


class Built(unittest.TestCase):
    kernel = bytes(range(256)) * 0x100  # 64 KB stand-in

    @classmethod
    def setUpClass(cls):
        cls.out = omega_image.link_upgrade(cls.kernel, IMAGE)

    def test_size(self):
        self.assertEqual(len(self.out), LAYOUT["IMAGE_END"] - ROM)
        self.assertEqual(self.out[: len(self.kernel)], self.kernel)
        self.assertFalse(any(self.out[len(self.kernel) : omega_image.IMAGE_OFFSET]))

    def test_parts_have_their_crcs(self):
        for name, offset, size, crc in omega_image.parts(LAYOUT):
            with self.subTest(name):
                self.assertEqual(zlib.crc32(self.out[offset : offset + size]), crc)

    def test_whole_pages_match_the_official_layout(self):
        # The firmware update sends 256-byte pages: the last one's bytes past
        # the firmware must be what the official file had there
        for name, offset, size, _ in omega_image.parts(LAYOUT):
            with self.subTest(name):
                src = omega_image.OFFICIAL[name]
                span = omega_image.page_end(size)
                self.assertEqual(
                    self.out[offset : offset + span], IMAGE[src : src + span]
                )

    def test_firmware_crcs_are_the_official_ones(self):
        self.assertEqual(LAYOUT["LX9_FW_crc32"], 0x7E33C4FB)
        self.assertEqual(LAYOUT["LX16_FW_crc32"], 0x709EF09C)

    def test_reproducible(self):
        self.assertEqual(omega_image.link_upgrade(self.kernel, IMAGE), self.out)


class Refused(unittest.TestCase):
    def build(self, **changes):
        layout = dict(LAYOUT, **changes)
        return omega_image.build_image(IMAGE, layout)

    def test_overlap(self):
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.build(
                LX16_newomega_top_bin_address=LAYOUT["LX9_newomega_top_bin_address"]
                + 0x1000
            )

    def test_unaligned(self):
        with self.assertRaisesRegex(ValueError, "aligned"):
            self.build(LZ77_SAMPLE_address=LAYOUT["LZ77_SAMPLE_address"] + 2)

    def test_wrong_crc(self):
        with self.assertRaisesRegex(ValueError, "CRC32"):
            self.build(LX9_FW_crc32=LAYOUT["LX9_FW_crc32"] ^ 1)

    def test_past_the_end(self):
        with self.assertRaisesRegex(ValueError, "past IMAGE_END"):
            self.build(IMAGE_END=LAYOUT["IMAGE_END"] - 0x1000)

    def test_past_the_loader_limit(self):
        with self.assertRaisesRegex(ValueError, "loader"):
            self.build(IMAGE_END=ROM + omega_image.LOADER_MAX + 0x1000)

    def test_kernel_too_big(self):
        with self.assertRaisesRegex(ValueError, "fit below"):
            omega_image.link_upgrade(bytes(omega_image.IMAGE_OFFSET + 1), IMAGE)


if __name__ == "__main__":
    unittest.main(verbosity=0)
