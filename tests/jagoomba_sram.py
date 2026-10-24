#!/usr/bin/env python3
"""Reads the GB saves Jagoomba Color keeps in a cart's 64 KB SRAM image.

usage: jagoomba_sram.py SAVE [TITLE]

Jagoomba keeps its records from offset 4 of the SRAM (after the 0x57A731D8
id): a 48-byte header (u16 size with the header, u16 type, u32 uncompressed
size, u32 frame count, u32 ROM checksum, 32-byte title), then the data,
LZO1X-compressed. Type 1 is a game's SRAM. This lists the records, or with
TITLE writes that game's SRAM, decompressed, to stdout (status 1 if there is
none). Written for Torii's tests (tests/gb_save.sh).
"""

import struct
import sys

STATEID = 0x57A731D8
SRAMSAVE = 1


def lzo1x_decompress(src: bytes, out_len: int) -> bytes:
    """LZO1X decompression, as minilzo's lzo1x_decompress does it."""
    out = bytearray()
    ip = 0

    def copy_match(dist: int, length: int) -> None:
        start = len(out) - dist
        if start < 0:
            raise ValueError("LZO: match before the start")
        for i in range(length):
            out.append(out[start + i])

    def literals(n: int) -> None:
        nonlocal ip
        out.extend(src[ip : ip + n])
        ip += n

    def long_length(t: int, base: int) -> int:
        nonlocal ip
        if t:
            return t
        while src[ip] == 0:
            t += 255
            ip += 1
        t += base + src[ip]
        ip += 1
        return t

    state = "loop"
    t = src[0]
    if t > 17:
        ip = 1
        t -= 17
        if t < 4:
            state = "match_next"
        else:
            literals(t)
            state = "first_literal_run"
    while True:
        if state == "loop":
            t = src[ip]
            ip += 1
            if t >= 16:
                state = "match"
                continue
            literals(long_length(t, 15) + 3)
            state = "first_literal_run"
        if state == "first_literal_run":
            t = src[ip]
            ip += 1
            if t >= 16:
                state = "match"
                continue
            dist = 1 + 0x0800 + (t >> 2) + (src[ip] << 2)
            ip += 1
            copy_match(dist, 3)
            state = "match_done"
        if state == "match":
            if t >= 64:
                dist = 1 + ((t >> 2) & 7) + (src[ip] << 3)
                ip += 1
                copy_match(dist, (t >> 5) + 1)
            elif t >= 32:
                t = long_length(t & 31, 31)
                dist = 1 + ((src[ip] | src[ip + 1] << 8) >> 2)
                ip += 2
                copy_match(dist, t + 2)
            elif t >= 16:
                far = (t & 8) << 11
                t = long_length(t & 7, 7)
                dist = far + ((src[ip] | src[ip + 1] << 8) >> 2)
                ip += 2
                if dist == 0:
                    break  # end of the stream
                copy_match(dist + 0x4000, t + 2)
            else:
                dist = 1 + (t >> 2) + (src[ip] << 2)
                ip += 1
                copy_match(dist, 2)
            state = "match_done"
        if state == "match_done":
            t = src[ip - 2] & 3
            if t == 0:
                state = "loop"
                continue
            state = "match_next"
        if state == "match_next":
            literals(t)
            t = src[ip]
            ip += 1
            state = "match"
    if len(out) != out_len:
        raise ValueError(f"LZO: {len(out)} bytes, expected {out_len}")
    return bytes(out)


def records(sram: bytes):
    """(type, title, checksum, data) for each record, data decompressed for SRAM saves."""
    if struct.unpack_from("<I", sram, 0)[0] != STATEID:
        return
    p = 4
    while p + 48 <= len(sram):
        size, kind, unpacked, _frames, checksum = struct.unpack_from("<HHIII", sram, p)
        if size in (0, 0xFFFF) or p + size > len(sram):
            break
        title = sram[p + 16 : p + 48].split(b"\0")[0].decode("latin-1")
        data = sram[p + 48 : p + size]
        if kind == SRAMSAVE:
            data = lzo1x_decompress(data, unpacked)
        yield kind, title, checksum, data
        p += size


def main() -> int:
    with open(sys.argv[1], "rb") as f:
        sram = f.read()
    want = sys.argv[2] if len(sys.argv) > 2 else None
    for kind, title, checksum, data in records(sram):
        if want is None:
            print(f"type {kind} {title!r} checksum {checksum:08X}, {len(data)} bytes")
        elif kind == SRAMSAVE and title == want:
            sys.stdout.buffer.write(data)
            return 0
    return 0 if want is None else 1


if __name__ == "__main__":
    sys.exit(main())
