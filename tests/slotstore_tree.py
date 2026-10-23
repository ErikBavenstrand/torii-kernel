#!/usr/bin/env python3
"""Lists the files under a folder copied off a test card, for tests/slotstore.sh.

One line per file: its path relative to the folder, a tab, its size, a tab, and
the byte every one of its bytes is (as a character), or ? if they differ. The
test saves are runs of one character, so a line says which save a file is."""
import os
import sys

root = sys.argv[1]
for base, dirs, files in os.walk(root):
    dirs.sort()
    for name in sorted(files):
        path = os.path.join(base, name)
        with open(path, "rb") as f:
            data = f.read()
        kind = chr(data[0]) if data and data.count(data[:1]) == len(data) else "?"
        if data and data[0] > 126:
            kind = "_" if data.count(b"\xff") == len(data) else "?"  # 0xFF: a blank save
        print(f"{os.path.relpath(path, root)}\t{len(data)}\t{kind}")
