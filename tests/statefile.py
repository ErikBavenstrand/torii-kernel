#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Read and write the kernel's save state files (source/statestore.h).

usage: tests/statefile.py unpack STATE RAW
       tests/statefile.py pack STATE RAW OUT [OFFSET=WORD...]

unpack writes the state buffer (INGAME_STATE_BYTES, the cart SRAM's
0x40000-0xAFFFF) of the file STATE to RAW. pack writes OUT: STATE's head with
RAW as its data, each OFFSET (hex, in the buffer) set to the 32-bit WORD (hex)
first, packed as LZ77 of literals, its sizes and CRCs made right: a file the
kernel reads whole, whatever it holds (tests/state_trips.sh).
"""

import struct
import sys
import zlib

HEAD = 64
CHUNK = 0x8000


def lz77_unpack(src: bytes) -> bytes:
    if src[0] != 0x10:
        raise ValueError("not LZ77")
    n = src[1] | src[2] << 8 | src[3] << 16
    out = bytearray()
    i = 4
    while len(out) < n:
        flags = src[i]
        i += 1
        for bit in range(8):
            if len(out) >= n:
                break
            if flags & (0x80 >> bit):
                a, b = src[i], src[i + 1]
                i += 2
                back = ((a & 15) << 8 | b) + 1
                for _ in range((a >> 4) + 3):
                    out.append(out[-back])
            else:
                out.append(src[i])
                i += 1
    return bytes(out)


def lz77_literal(data: bytes) -> bytes:
    out = bytearray([0x10, len(data) & 0xFF, len(data) >> 8 & 0xFF, len(data) >> 16])
    for i in range(0, len(data), 8):
        out.append(0)
        out += data[i : i + 8]
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def unpack(path: str) -> bytes:
    data = open(path, "rb").read()
    raw = bytearray()
    pos = HEAD
    while pos < len(data):
        _, size = struct.unpack_from("<2I", data, pos)
        raw += lz77_unpack(data[pos + 8 : pos + 8 + size])
        pos += 8 + size
    return bytes(raw)


def pack(head_from: str, raw: bytes) -> bytes:
    head = bytearray(open(head_from, "rb").read()[:HEAD])
    data = bytearray()
    for at in range(0, len(raw), CHUNK):
        part = lz77_literal(raw[at : at + CHUNK])
        data += struct.pack("<2I", CHUNK, len(part)) + part
    struct.pack_into("<2I", head, 16, len(data), zlib.crc32(data))
    struct.pack_into("<I", head, HEAD - 4, zlib.crc32(bytes(head[: HEAD - 4])))
    return bytes(head) + bytes(data)


def main() -> int:
    if len(sys.argv) >= 4 and sys.argv[1] == "unpack":
        open(sys.argv[3], "wb").write(unpack(sys.argv[2]))
    elif len(sys.argv) >= 5 and sys.argv[1] == "pack":
        raw = bytearray(open(sys.argv[3], "rb").read())
        for patch in sys.argv[5:]:
            offset, word = patch.split("=")
            struct.pack_into("<I", raw, int(offset, 16), int(word, 16))
        open(sys.argv[4], "wb").write(pack(sys.argv[2], bytes(raw)))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
