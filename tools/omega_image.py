#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Combine a built kernel with image.bin into the Omega DE upgrade file.

Replaces the Windows-only Link_kernel_image.exe: the shipped ezkernelnew.bin is
the kernel padded with zero bytes to 0x100000, followed by image.bin unchanged.

Usage: omega_image.py KERNEL.gba image.bin OUTPUT.bin
"""

import argparse
import sys

# The kernel reads graphics and FPGA bitstreams from fixed ROM addresses in
# image.bin (ez_define.h), so image.bin must start here.
IMAGE_OFFSET = 0x100000


def link_upgrade(kernel: bytes, image: bytes) -> bytes:
    if len(kernel) > IMAGE_OFFSET:
        raise ValueError(
            f"kernel is {len(kernel):#x} bytes; it must fit below {IMAGE_OFFSET:#x}"
        )
    return kernel + bytes(IMAGE_OFFSET - len(kernel)) + image


def read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("kernel", help="built kernel ROM (ezkernelnew.gba)")
    parser.add_argument("image", help="image.bin")
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
