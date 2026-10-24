"""Fail if a tracked text file is not UTF-8 with LF line endings (just lint).

Windows scripts (*.bat) keep their original encoding and CRLF line endings,
as .gitattributes says, and the emulator patches (emulators/patches/) carry
the CRLF lines of the upstream files they change.
"""

import subprocess
import sys
from pathlib import Path

SKIP_PREFIXES = ("emulators/patches/",)
SKIP_SUFFIXES = (".bat",)


def main() -> int:
    files = subprocess.run(
        ["git", "ls-files", "-z", "--eol"], capture_output=True, check=True
    ).stdout.split(b"\0")
    bad = []
    for line in files:
        if not line:
            continue
        info, _, path = line.decode().partition("\t")
        if (
            "-text" in info
            or path.startswith(SKIP_PREFIXES)
            or path.endswith(SKIP_SUFFIXES)
        ):
            continue
        data = Path(path).read_bytes()
        if b"\0" in data[:8000]:
            continue  # binary
        if data.startswith(b"\xef\xbb\xbf"):
            bad.append(f"{path}: starts with a byte order mark")
        try:
            data.decode("utf-8")
        except UnicodeDecodeError as e:
            bad.append(f"{path}: not UTF-8 at byte {e.start}")
        if b"\r" in data:
            bad.append(f"{path}: has CR line endings")
    for b in bad:
        print(b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
