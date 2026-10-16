#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Keep builds in containers from reusing stale results.

usage: fresh.py sync [--all] PATH...   wait until containers see recent edits
       fresh.py stamp DIR KEY FILE...  empty DIR if the build settings changed
       fresh.py track DIR OUTPUT... -- PATH...
                                       drop objects whose inputs changed

sync: colima shares the repo with containers over sshfs, which caches file
attributes for about a second. A build started right after an edit can see
the old timestamp, or even old content, and skip or botch a rebuild. For
files under PATH changed in the last few seconds, this waits until a
container sees the same size and timestamp as the host. Build outputs
(.o, .gba, ...) are skipped unless --all is given, e.g. for folders of games.

stamp: make only compares source timestamps, so a changed Makefile or
toolchain leaves old objects behind. This hashes KEY and the FILEs and, if
the result differs from the one recorded in DIR, deletes DIR's contents.

track: sshfs reports whole-second timestamps, so a file edited in the same
second a build finished looks no newer than its object, and make keeps the
old object. This hashes every file under PATH and compares with the hashes
recorded in DIR at the last build. For each changed file it deletes the
objects whose dependency files (.d, written by gcc -MMD) name it, and then
the OUTPUTs (linked files), so they are rebuilt whatever the timestamps say.
"""

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

RECENT = 5.0  # seconds; sshfs caches attributes for about one
TIMEOUT = 15.0
IMAGE = "torii-emu"
# Written by the builds in containers, so containers see them as written.
OUTPUTS = {".o", ".d", ".a", ".elf", ".gba", ".nm", ".sym", ".map", ".png"}


def recent_files(paths: list[str], everything: bool) -> dict[str, tuple[int, int]]:
    now = time.time()
    found = {}
    for root in paths:
        candidates = [Path(root)] if Path(root).is_file() else Path(root).rglob("*")
        for path in candidates:
            if not path.is_file() or (path.suffix in OUTPUTS and not everything):
                continue
            st = path.stat()
            if now - st.st_mtime < RECENT:
                found[str(path)] = (int(st.st_mtime), st.st_size)
    return found


def container_view(files: list[str]) -> dict[str, tuple[int, int]]:
    out = subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-v",
            f"{os.getcwd()}:/src",
            "-w",
            "/src",
            IMAGE,
            "stat",
            "-c",
            "%Y %s %n",
            *files,
        ],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    view = {}
    for line in out.splitlines():
        mtime, size, name = line.split(" ", 2)
        view[name] = (int(mtime), int(size))
    return view


def sync(paths: list[str]) -> int:
    everything = paths[0] == "--all"
    host = recent_files(paths[1:] if everything else paths, everything)
    if not host:
        return 0
    deadline = time.time() + TIMEOUT
    while True:
        seen = container_view(sorted(host))
        stale = [f for f, attrs in host.items() if seen.get(f) != attrs]
        if not stale:
            return 0
        if time.time() > deadline:
            print(
                f"fresh: containers still see old versions of {', '.join(stale)}",
                file=sys.stderr,
            )
            return 1
        time.sleep(0.3)


def stamp(directory: str, key: str, files: list[str]) -> int:
    digest = hashlib.sha256(key.encode())
    for name in files:
        digest.update(Path(name).read_bytes())
    wanted = digest.hexdigest()
    target = Path(directory)
    record = target / ".fresh-stamp"
    if record.exists() and record.read_text().strip() == wanted:
        return 0
    if target.exists():
        for entry in target.iterdir():
            if entry.is_dir():
                subprocess.run(["rm", "-rf", "--", str(entry)], check=True)
            else:
                entry.unlink()
        print(f"fresh: build settings changed, cleared {directory}")
    target.mkdir(parents=True, exist_ok=True)
    record.write_text(wanted + "\n")
    return 0


def file_hashes(paths: list[str]) -> dict[str, str]:
    hashes = {}
    for root in paths:
        candidates = [Path(root)] if Path(root).is_file() else Path(root).rglob("*")
        for path in candidates:
            if path.is_file() and path.suffix not in OUTPUTS:
                hashes[str(path)] = hashlib.sha1(path.read_bytes()).hexdigest()
    return hashes


def track(directory: str, outputs: list[str], paths: list[str]) -> int:
    target = Path(directory)
    record = target / ".fresh-hashes"
    hashes = file_hashes(paths)
    old = json.loads(record.read_text()) if record.exists() else None
    if old is not None:
        changed = [name for name, digest in hashes.items() if old.get(name) != digest]
        dropped = 0
        if changed:
            for dep in target.rglob("*.d"):
                text = dep.read_text(errors="replace")
                if any(f"/{name}" in text or text.startswith(name) for name in changed):
                    obj = dep.with_suffix("" if dep.name.endswith(".o.d") else ".o")
                    for stale in (obj, dep):
                        if stale.exists():
                            stale.unlink()
                            dropped += stale.suffix == ".o"
            for output in outputs:
                if Path(output).exists():
                    Path(output).unlink()
        if dropped:
            print(
                f"fresh: {len(changed)} changed file(s), rebuilding {dropped} object(s)"
            )
    target.mkdir(parents=True, exist_ok=True)
    record.write_text(json.dumps(hashes))
    return 0


def main() -> int:
    if len(sys.argv) >= 4 and sys.argv[1] == "track" and "--" in sys.argv:
        split = sys.argv.index("--")
        return track(sys.argv[2], sys.argv[3:split], sys.argv[split + 1 :])
    if len(sys.argv) >= 3 and sys.argv[1] == "sync":
        return sync(sys.argv[2:])
    if len(sys.argv) >= 4 and sys.argv[1] == "stamp":
        return stamp(sys.argv[2], sys.argv[3], sys.argv[4:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
