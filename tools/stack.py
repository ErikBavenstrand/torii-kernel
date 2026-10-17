#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Report the kernel's largest stack frames and deepest call chains.

usage: stack.py BUILD_DIR [COUNT]

Reads the call graph files (.ci) GCC writes with -fcallgraph-info=su. A
chain's depth adds up the frames on its way down, so it is a static
estimate: calls through function pointers, assembly and libc/libgba
(no frame sizes) count as zero, and a recursive call is cut off where it
recurses. Frames GCC marks "dynamic" (alloca, variable-length arrays) are
flagged, since their size is a lower bound.
"""

import re
import sys
from pathlib import Path

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
FRAME = re.compile(r"(\d+) bytes \(([a-z,]+)\)")
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')


def load(build: Path):
    """Return {key: (name, location, bytes, kind)} and {key: set of callee keys}."""
    frames, raw_edges = {}, []
    for ci in sorted(build.glob("*.ci")):
        local = {}
        text = ci.read_text(errors="replace")
        for title, label in NODE.findall(text):
            parts = label.split("\\n")  # lines: name, location, frame
            frame = FRAME.fullmatch(parts[-1])
            if frame:
                key = f"{ci.stem}:{title}"
                local[title] = key
                where = parts[1].removeprefix("/src/")
                name = title.rsplit(":", 1)[
                    -1
                ]  # GCC titles static functions "file:name"
                frames[key] = (name, where, int(frame.group(1)), frame.group(2))
        raw_edges += [(ci.stem, src, dst, local) for src, dst in EDGE.findall(text)]
    # A call goes to the function of that name in the same file (static or
    # not), else to the one other file that defines it.
    defined = {}
    for key in frames:
        defined.setdefault(key.split(":", 1)[1], []).append(key)
    calls = {key: set() for key in frames}
    for _stem, src, dst, local in raw_edges:
        target = local.get(dst) or (
            defined[dst][0] if len(defined.get(dst, [])) == 1 else None
        )
        if src in local and target:
            calls[local[src]].add(target)
    return frames, calls


def deepest(frames, calls):
    """Return {key: (depth, chain)} for the worst path from each function."""
    best, active = {}, set()

    def walk(key):
        if key in best:
            return best[key]
        if key in active:  # recursion: stop here
            return 0, []
        active.add(key)
        below = max(
            (walk(callee) for callee in calls[key]), default=(0, []), key=lambda r: r[0]
        )
        active.discard(key)
        best[key] = (frames[key][2] + below[0], [key] + below[1])
        return best[key]

    sys.setrecursionlimit(10000)
    for key in frames:
        walk(key)
    return best


def main() -> int:
    build = Path(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    frames, calls = load(build)
    if not frames:
        print(
            f"no .ci files in {build}; build with -fcallgraph-info=su", file=sys.stderr
        )
        return 1
    called = {callee for callees in calls.values() for callee in callees}
    best = deepest(frames, calls)

    print("Largest frames (bytes, function, location):")
    for key in sorted(frames, key=lambda k: -frames[k][2])[:count]:
        name, where, size, kind = frames[key]
        note = "  (dynamic)" if kind != "static" else ""
        print(f"  {size:6d}  {name}  {where}{note}")

    print("\nDeepest call chains from functions nothing calls (bytes, chain):")
    roots = [k for k in frames if k not in called]
    for key in sorted(roots, key=lambda k: -best[k][0])[:count]:
        depth, chain = best[key]
        print(f"  {depth:6d}  " + " > ".join(frames[k][0] for k in chain))
    return 0


if __name__ == "__main__":
    sys.exit(main())
