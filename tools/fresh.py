#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Keep builds in containers from reusing stale results.

usage: fresh.py sync [--all] PATH...   wait until containers see recent edits
       fresh.py stamp DIR KEY FILE...  empty DIR if the build settings changed
       fresh.py track DIR OUTPUT... -- PATH...
                                       drop objects whose inputs changed
       fresh.py check NAME OUTPUT... -- PATH...
                                       exit 0 if NAME can be skipped
       fresh.py record NAME -- PATH...  note that NAME was built from PATH

sync: the torii colima VM shares the repo over virtiofs, which caches file
attributes for about a second. Right after an edit a container can see the
old size and timestamp, and a file git replaced can read as its old content.
For files under PATH changed in the last two seconds, this waits (at most
about a second) until a container sees the same size and timestamp. Build outputs
(.o, .gba, ...) are skipped unless --all is given, e.g. for folders of games.

stamp: make only compares source timestamps, so a changed Makefile or
toolchain leaves old objects behind. This hashes KEY and the FILEs and, if
the result differs from the one recorded in DIR, deletes DIR's contents.

track: make trusts timestamps, and a timestamp seen through the share can be
a second old, so make could keep an object built from the old file. This hashes every file under PATH and compares with the hashes
recorded in DIR at the last build. For each changed file it deletes the
objects whose dependency files (.d, written by gcc -MMD) name it, and then
the OUTPUTs (linked files), so they are rebuilt whatever the timestamps say.

check/record: skip a whole step when nothing it reads has changed. record
saves one hash over every file under PATH (and its name) after the step
succeeds; check exits 0 when the hash still matches and every OUTPUT exists,
so `fresh.py check ... || { build; fresh.py record ...; }` builds only when
needed. A path that is not a file or folder counts as a literal key, such as
a toolchain image. Records live in dist/.fresh.
"""

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

RECENT = 2.0  # seconds; virtiofs caches attributes for about one
TIMEOUT = 5.0
IMAGE = "torii-emu"
# Written by the builds in containers, so containers see them as written.
OUTPUTS = {".o", ".d", ".a", ".elf", ".gba", ".nm", ".sym", ".map", ".png"}


def recent_files(paths: list[str], everything: bool) -> dict[str, tuple[str, int]]:
    now = time.time()
    found = {}
    for root in paths:
        candidates = [Path(root)] if Path(root).is_file() else Path(root).rglob("*")
        for path in candidates:
            if not path.is_file() or (path.suffix in OUTPUTS and not everything):
                continue
            st = path.stat()
            if now - st.st_mtime < RECENT:
                ns = st.st_mtime_ns
                found[str(path)] = (f"{ns // 10**9}.{ns % 10**9:09d}", st.st_size)
    return found


def container_view(files: list[str]) -> dict[str, tuple[str, int]]:
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
            "%.9Y %s %n",
            *files,
        ],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    view = {}
    for line in out.splitlines():
        mtime, size, name = line.split(" ", 2)
        view[name] = (mtime, int(size))
    return view


def sync(paths: list[str]) -> int:
    if sys.platform != "darwin":
        return 0  # Linux bind mounts show edits at once; the lag is colima's
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


RECORDS = Path("dist/.fresh")


def inputs_hash(paths: list[str]) -> str:
    digest = hashlib.sha256()
    for root in paths:
        if not Path(root).exists():
            digest.update(f"key {root}\n".encode())
            continue
        for name, file_digest in sorted(file_hashes([root]).items()):
            digest.update(f"{name} {file_digest}\n".encode())
    return digest.hexdigest()


def check(name: str, outputs: list[str], paths: list[str]) -> int:
    current = inputs_hash(paths)
    record = RECORDS / name
    outputs_exist = all(Path(o).exists() for o in outputs)
    if record.exists() and outputs_exist and record.read_text().strip() == current:
        return 0
    # Remember what this build starts from: an edit made while it runs must
    # not be recorded as built.
    RECORDS.mkdir(parents=True, exist_ok=True)
    (RECORDS / f"{name}.pending").write_text(current + "\n")
    return 1


def record(name: str, paths: list[str]) -> int:
    RECORDS.mkdir(parents=True, exist_ok=True)
    pending = RECORDS / f"{name}.pending"
    if pending.exists():
        pending.replace(RECORDS / name)
    else:
        (RECORDS / name).write_text(inputs_hash(paths) + "\n")
    return 0


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
    if len(sys.argv) >= 4 and sys.argv[1] in ("check", "record") and "--" in sys.argv:
        split = sys.argv.index("--")
        if sys.argv[1] == "record":
            return record(sys.argv[2], sys.argv[split + 1 :])
        return check(sys.argv[2], sys.argv[3:split], sys.argv[split + 1 :])
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
