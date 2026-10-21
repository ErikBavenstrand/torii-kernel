"""The settings record in kernel-nor.bin (source/setrecord.h): two copies, A
at 0x7B0000 and B at 0x7B2000, each 256 16-bit slots ending in a trailer.

Tests read slots from the copy the kernel reads, and set slots as an older
kernel would: the slots as they read, with the changes, written to A with the
trailer's slots 0, which the kernel reads over any copy of its own.

    python3 tests/setnor.py get KERNEL_NOR SLOT [COUNT]   # decimal values
    python3 tests/setnor.py pick KERNEL_NOR               # A, B or none
    python3 tests/setnor.py set KERNEL_NOR SLOT=VALUE...  # makes the file if missing
"""

import struct
import sys
import zlib
from pathlib import Path

COPY_A = 0x7B0000
COPY_B = 0x7B2000
SLOTS = 256
SEQ, CRC, MAGIC_SLOT = 248, 252, 254
MAGIC = 0x31455354  # "TSE1"
KERNEL_NOR_SIZE = 0x800000


def _copy(nor: bytes, offset: int) -> list[int]:
    return list(struct.unpack_from(f"<{SLOTS}H", nor, offset))


def _whole(slots: list[int]) -> int | None:
    """The copy's sequence number if it is whole, else None"""
    word = lambda i: slots[i] | slots[i + 1] << 16
    raw = struct.pack(f"<{CRC}H", *slots[:CRC])
    if word(MAGIC_SLOT) != MAGIC or word(CRC) != zlib.crc32(raw):
        return None
    return word(SEQ)


def pick(nor: bytes) -> str:
    """The copy the kernel reads (set_record_pick): A, B, or none (A as it is)"""
    a, b = _copy(nor, COPY_A), _copy(nor, COPY_B)
    seq_a, seq_b = _whole(a), _whole(b)
    if seq_a is None and a[SLOTS - 1] == 0:
        return "none"
    newer_b = seq_b is not None and (
        seq_a is None or 0 < (seq_b - seq_a) & 0xFFFFFFFF < 0x80000000
    )
    if newer_b:
        return "B"
    return "A" if seq_a is not None else "none"


def read(nor: bytes) -> list[int]:
    """The slots the kernel reads"""
    return _copy(nor, COPY_B if pick(nor) == "B" else COPY_A)


def write(nor: bytearray, changes: dict[int, int]) -> None:
    """Sets slots as an older kernel would: all of them to A, trailer 0"""
    slots = read(nor)[:SEQ] + [0] * (SLOTS - SEQ)
    for slot, value in changes.items():
        slots[slot] = value & 0xFFFF
    struct.pack_into(f"<{SLOTS}H", nor, COPY_A, *slots)


def main() -> None:
    cmd, path = sys.argv[1], Path(sys.argv[2])
    if cmd == "get":
        first = int(sys.argv[3], 0)
        count = int(sys.argv[4], 0) if len(sys.argv) > 4 else 1
        print(" ".join(str(v) for v in read(path.read_bytes())[first : first + count]))
    elif cmd == "pick":
        print(pick(path.read_bytes()))
    elif cmd == "set":
        nor = bytearray(
            path.read_bytes() if path.exists() else b"\xff" * KERNEL_NOR_SIZE
        )
        write(
            nor, {int(s, 0): int(v, 0) for s, v in (a.split("=") for a in sys.argv[3:])}
        )
        path.write_bytes(nor)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
