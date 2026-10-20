#!/usr/bin/env python3
"""Reads the library index (source/library.h) and prints it, checking its CRCs.

The card keeps it in two files written in turn, /torii/library.idx and
/torii/library.bak; give one or both, and the newest valid one (by its
header's generation) is read, as the boot does.

    tests/library_idx.py FILE...          one line per game, in list order:
                                          console, path, size, "save" or "-"
    tests/library_idx.py FILE... --header the header's fields, one per line

A missing file is skipped. Exits 1 if no file is whole (a bad magic,
version, size or CRC).
"""

import os
import struct
import sys
import zlib

CONSOLES = ["GBA", "GBC", "GB", "NES"]
HEADER = struct.Struct("<IHHHHHH8HIIIIIIII")
VERSION = 2


def four(n):
    return (n + 3) & ~3


def cstr(pool, off):
    end = pool.index(b"\0", off)
    return pool[off:end].decode("utf-8", "replace")


def parse(path):
    """The file's fields as a dict, or the reason it is no index."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 64:
        return "shorter than its header"
    f = HEADER.unpack_from(data)
    magic, version, header_bytes, record_bytes, flags, records, folders = f[:7]
    counts = f[7:15]
    total_files, pool_bytes, walk_hash, serial, body_crc, games_seen = f[15:21]
    generation, header_crc = f[21:23]
    if (
        magic != 0x42494C54
        or version != VERSION
        or header_bytes != 64
        or record_bytes != 16
    ):
        return f"magic {magic:08X}, version {version}: not a library index this reads"
    if zlib.crc32(data[:60]) != header_crc:
        return "bad header CRC"
    order_len = sum(counts)
    sizes = [records * 16, four(order_len * 2), folders * 4, four(pool_bytes)]
    if len(data) != 64 + sum(sizes):
        return f"{len(data)} bytes, expected {64 + sum(sizes)}"
    if zlib.crc32(data[64:]) != body_crc:
        return "bad body CRC"
    at = 64
    recs = [struct.unpack_from("<HHIII", data, at + 16 * i) for i in range(records)]
    at += sizes[0]
    order = struct.unpack_from(f"<{order_len}H", data, at)
    at += sizes[1]
    folder_offs = struct.unpack_from(f"<{folders}I", data, at)
    at += sizes[2]
    return {
        "path": path,
        "flags": flags,
        "records": records,
        "folders": folders,
        "counts": counts,
        "total_files": total_files,
        "games_seen": games_seen,
        "pool_bytes": pool_bytes,
        "walk_hash": walk_hash,
        "serial": serial,
        "generation": generation,
        "recs": recs,
        "order": order,
        "folder_offs": folder_offs,
        "pool": data[at : at + pool_bytes],
    }


def main():
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    found = []
    for path in files:
        if not os.path.exists(path):
            continue
        idx = parse(path)
        if isinstance(idx, str):
            print(f"library_idx: {path}: {idx}", file=sys.stderr)
        else:
            found.append(idx)
    if not found:
        print("library_idx: no whole index", file=sys.stderr)
        sys.exit(1)
    idx = max(found, key=lambda i: i["generation"])
    if "--header" in sys.argv:
        print(f"file {os.path.basename(idx['path'])}")
        print(f"generation {idx['generation']}")
        print(f"flags {idx['flags']}")
        print(f"records {idx['records']}")
        print(f"folders {idx['folders']}")
        for name, n in zip(CONSOLES, idx["counts"][: len(CONSOLES)], strict=True):
            print(f"{name} {n}")
        print(f"total_files {idx['total_files']}")
        print(f"games_seen {idx['games_seen']}")
        print(f"pool_bytes {idx['pool_bytes']}")
        print(f"walk_hash {idx['walk_hash']:08X}")
        print(f"volume_serial {idx['serial']:08X}")
        return
    pool = idx["pool"]
    for rec in idx["order"]:
        folder, rflags, size, name, _ = idx["recs"][rec]
        path = (
            cstr(pool, idx["folder_offs"][folder]).rstrip("/")
            + "/"
            + cstr(pool, name & 0xFFFFFF)
        )
        print(
            f"{CONSOLES[rflags & 15]}\t{path}\t{size}\t{'save' if rflags & 16 else '-'}"
        )


if __name__ == "__main__":
    main()
