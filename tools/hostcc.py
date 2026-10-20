#!/usr/bin/env python3
"""Builds a host program from kernel and test sources under ASan and UBSan,
for tests/host.sh, in the torii-emu container.

Each source compiles to its own object, in parallel, cached in dist/host/obj
under a hash of its preprocessed text and the flags, so an unchanged file is
never compiled twice, whichever program asks for it. The hash is of content,
not times: the colima VM's virtiofs mount can show an edit's new time late.

Usage: hostcc.py OUT SOURCE...
"""

import hashlib
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

OBJ = Path("dist/host/obj")
# -funsigned-char: char is unsigned on ARM, signed on x86 hosts
CFLAGS = [
    "-std=gnu11",
    "-g",
    "-O1",
    "-fno-omit-frame-pointer",
    "-funsigned-char",
    "-fsanitize=address,undefined",
    "-fno-sanitize-recover=all",
    "-Wall",
    "-Wextra",
    "-Wshadow",
    "-Wundef",
    "-Wformat=2",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Werror",
    "-I",
    "tests/host",
]


def run(cmd: list[str]) -> bytes:
    result = subprocess.run(cmd, capture_output=True, check=False)
    if result.returncode:
        sys.stderr.buffer.write(result.stderr)
        sys.exit(f"hostcc: {' '.join(cmd)} failed")
    return result.stdout


def compile_one(src: str) -> Path:
    """Returns src's object, compiling it unless the cache has it."""
    pre = run(["cc", *CFLAGS, "-E", src])
    key = hashlib.sha256("\0".join([*CFLAGS, src]).encode() + b"\0" + pre)
    obj = OBJ / f"{Path(src).stem}-{key.hexdigest()[:20]}.o"
    if not obj.exists():
        tmp = obj.with_suffix(f".{os.getpid()}.tmp")
        run(["cc", *CFLAGS, "-c", "-o", str(tmp), src])
        tmp.replace(obj)
    return obj


def build(out: Path, sources: list[str]) -> Path:
    OBJ.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        objs = list(pool.map(compile_one, sources))
    # Relink only when an object changed
    stamp = out.with_name(out.name + ".objs")
    want = "\n".join(str(o) for o in objs) + "\n"
    if not out.exists() or not stamp.exists() or stamp.read_text() != want:
        tmp = out.with_name(f"{out.name}.{os.getpid()}.tmp")
        run(["cc", *CFLAGS, "-o", str(tmp), *map(str, objs)])
        tmp.replace(out)
        stamp.write_text(want)
    return out


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    build(Path(sys.argv[1]), sys.argv[2:])
