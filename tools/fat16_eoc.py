"""Rewrite every FAT16 end-of-chain mark 0xFFFF in an SD image as 0xFFF8.

Both are valid end marks, and other tools write 0xFFF8; tests/fragmented.sh
checks the kernel stops at either. Usage: fat16_eoc.py IMG
"""

import struct
import sys
from pathlib import Path


def main() -> None:
    img = Path(sys.argv[1])
    data = bytearray(img.read_bytes())
    # f_mkfs puts the volume in a partition; its start is in the MBR
    mbr = data[510:512] == b"\x55\xaa" and data[446 + 4] != 0
    start = struct.unpack_from("<I", data, 446 + 8)[0] * 512 if mbr else 0
    sector, per_cluster, reserved, fats, roots, total16 = struct.unpack_from(
        "<HBHBHH", data, start + 11
    )
    fat_sectors = struct.unpack_from("<H", data, start + 22)[0]
    total = total16 or struct.unpack_from("<I", data, start + 32)[0]
    first_data = reserved + fats * fat_sectors + roots * 32 // sector
    clusters = (total - first_data) // per_cluster
    assert fat_sectors and 4085 <= clusters < 65525, "not a FAT16 volume"
    changed = 0
    for n in range(fats):
        fat = start + (reserved + n * fat_sectors) * sector
        for entry in range(2, fat_sectors * sector // 2):
            if struct.unpack_from("<H", data, fat + entry * 2)[0] == 0xFFFF:
                struct.pack_into("<H", data, fat + entry * 2, 0xFFF8)
                changed += 1
    img.write_bytes(data)
    print(f"{img}: {changed} end marks now 0xFFF8")


if __name__ == "__main__":
    main()
