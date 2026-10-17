#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Write compile_commands.json for clangd from `make -n` dry runs.

usage: compile_commands.py HOST_ROOT BUILD_DIR=DRY_RUN...

The kernel builds in the devkitARM container, so the dry runs have container
paths: /src is the repository and /opt/devkitpro the toolchain. Both are
mapped to the host, with each dry run's commands running in its BUILD_DIR
(relative to HOST_ROOT), and the toolchain's headers expected in
dist/devkitpro (copied there by `just compile-commands`). The commands keep
GCC's flags; .clangd sets the target and drops what clang doesn't know. A
file in several dry runs (the kernel and the test kernel) keeps its first.
"""

import json
import shlex
import sys
from pathlib import Path


def host_path(arg: str, root: str, devkitpro: str) -> str:
    """Map a container path, alone or after -I and the like, to the host."""
    for flag in ("", "-I", "-iquote", "-isystem"):
        path = arg[len(flag) :]
        if not arg.startswith(flag):
            continue
        for container, host in (("/opt/devkitpro", devkitpro), ("/src", root)):
            if path == container or path.startswith(container + "/"):
                return flag + host + path[len(container) :]
    return arg


def main() -> int:
    root = sys.argv[1]
    devkitpro = f"{root}/dist/devkitpro"
    entries = {}
    for pair in sys.argv[2:]:
        build, dry_run = pair.split("=", 1)
        for line in Path(dry_run).read_text().splitlines():
            args = shlex.split(line)
            if not args or not args[0].endswith("gcc") or "-c" not in args:
                continue
            out = []
            skip = False
            for arg in args:
                if skip:
                    skip = False
                    continue
                if arg in ("-MMD", "-MP"):
                    continue
                if arg == "-MF":
                    skip = True
                    continue
                out.append(host_path(arg, root, devkitpro))
            source = out[out.index("-c") + 1]
            entries.setdefault(
                source,
                {"directory": f"{root}/{build}", "file": source, "arguments": out},
            )
    Path("compile_commands.json").write_text(
        json.dumps(list(entries.values()), indent=1) + "\n"
    )
    print(f"compile_commands.json: {len(entries)} files")
    return 0 if entries else 1


if __name__ == "__main__":
    sys.exit(main())
