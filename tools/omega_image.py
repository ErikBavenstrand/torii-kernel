#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Combine a built kernel with image.bin's parts into the Omega DE upgrade file.

Replaces the Windows-only Link_kernel_image.exe. The shipped ezkernelnew.bin
is the kernel padded with zero bytes to 0x100000, then the parts of
EZ-Flash's image.bin the kernel still reads (the FPGA firmware and the LZ77
timing sample), each at the address source/image_layout.h gives it, with
zero bytes between them. image.bin's 16bpp menu pictures are left out.

Usage: omega_image.py KERNEL.gba image.bin OUTPUT.bin
"""

from __future__ import annotations

import argparse
import re
import sys
import zlib
from pathlib import Path

LAYOUT = Path(__file__).resolve().parent.parent / "source" / "image_layout.h"

ROM = 0x08000000
# The kernel's area: the parts start after it.
IMAGE_OFFSET = 0x100000
# EZ-Flash's loader (DE.BIN) installs at most this much of the file.
LOADER_MAX = 0x300000
# The firmware update sends whole pages, so each part goes with the bytes
# after it in image.bin up to a page end.
PAGE = 0x100

# Where each part sits in EZ-Flash's image.bin (1.06, 0x1719DC bytes),
# by the prefix of its names in image_layout.h.
OFFICIAL = {
    "LX9_newomega_top_bin": 0x95000,
    "LX16_newomega_top_bin": 0x100000,
    "LZ77_SAMPLE": 0x17890,
}
CRC = {
    "LX9_newomega_top_bin": "LX9_FW_crc32",
    "LX16_newomega_top_bin": "LX16_FW_crc32",
    "LZ77_SAMPLE": "LZ77_SAMPLE_crc32",
}


def read_layout(path: Path = LAYOUT) -> dict[str, int]:
    """image_layout.h's #defines with a number value, by name."""
    defines = {}
    for m in re.finditer(
        r"^#define\s+(\w+)\s+(?:\(void \*\))?(0x[0-9A-Fa-f]+)\b",
        path.read_text(),
        re.MULTILINE,
    ):
        defines[m.group(1)] = int(m.group(2), 16)
    return defines


def parts(layout: dict[str, int]) -> list[tuple[str, int, int, int]]:
    """(name, file offset, size, CRC32) of each part, in file order."""
    out = []
    for name in OFFICIAL:
        out.append(
            (
                name,
                layout[name + "_address"] - ROM,
                layout[name + "_size"],
                layout[CRC[name]],
            )
        )
    return sorted(out, key=lambda p: p[1])


def page_end(size: int) -> int:
    return (size + PAGE - 1) // PAGE * PAGE


def build_image(image: bytes, layout: dict[str, int]) -> bytes:
    """The upgrade file's part after the kernel area."""
    end = layout["IMAGE_END"] - ROM
    if layout["IMAGE_ADDRESS"] - ROM != IMAGE_OFFSET:
        raise ValueError(f"IMAGE_ADDRESS must be {ROM + IMAGE_OFFSET:#x}")
    if end > LOADER_MAX:
        raise ValueError(f"IMAGE_END is past the loader's {LOADER_MAX:#x} bytes")
    out = bytearray(end - IMAGE_OFFSET)
    at = IMAGE_OFFSET
    for name, offset, size, crc in parts(layout):
        src = OFFICIAL[name]
        if zlib.crc32(image[src : src + size]) != crc:
            raise ValueError(f"{name}: image.bin's bytes don't have CRC32 {crc:#010x}")
        if offset < at or offset % PAGE:
            raise ValueError(
                f"{name} at {offset:#x} overlaps the part before or isn't page aligned"
            )
        span = page_end(size)
        if offset + span > end:
            raise ValueError(f"{name} runs past IMAGE_END")
        out[offset - IMAGE_OFFSET : offset - IMAGE_OFFSET + span] = image[
            src : src + span
        ]
        at = offset + span
    return bytes(out)


def link_upgrade(
    kernel: bytes, image: bytes, layout: dict[str, int] | None = None
) -> bytes:
    if len(kernel) > IMAGE_OFFSET:
        raise ValueError(
            f"kernel is {len(kernel):#x} bytes; it must fit below {IMAGE_OFFSET:#x}"
        )
    return (
        kernel
        + bytes(IMAGE_OFFSET - len(kernel))
        + build_image(image, layout or read_layout())
    )


def read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("kernel", help="built kernel ROM (ezkernelnew.gba)")
    parser.add_argument("image", help="EZ-Flash's image.bin")
    parser.add_argument("output", help="upgrade file to write (ezkernelnew.bin)")
    args = parser.parse_args()

    kernel = read(args.kernel)
    try:
        out = link_upgrade(kernel, read(args.image))
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    with open(args.output, "wb") as f:
        f.write(out)
    print(
        f"{args.output}: kernel {len(kernel):#x} bytes "
        f"({len(kernel) * 100 // IMAGE_OFFSET}% of the kernel area), total {len(out):#x}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
