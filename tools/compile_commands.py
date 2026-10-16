#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Write compile_commands.json for clangd from a `make -n` dry run.

usage: compile_commands.py DRY_RUN HOST_ROOT

The kernel builds in the devkitARM container, so the dry run has container
paths: /src is the repository and /opt/devkitpro the toolchain. Both are
mapped to the host, with the toolchain's headers expected in
dist/devkitpro (copied there by `just compile-commands`).
"""

import json
import shlex
import sys
from pathlib import Path


def main() -> int:
    dry_run, root = Path(sys.argv[1]), sys.argv[2]
    devkitpro = f"{root}/dist/devkitpro"
    entries = []
    for line in dry_run.read_text().splitlines():
        args = shlex.split(line)
        if not args or not args[0].endswith("gcc") or "-c" not in args:
            continue
        out = []
        skip = False
        for arg in args[1:]:
            if skip:
                skip = False
                continue
            if arg in ("-MMD", "-MP"):
                continue
            if arg == "-MF":
                skip = True
                continue
            out.append(
                arg.replace("/opt/devkitpro", devkitpro).replace("/src", root, 1)
            )
        source = out[out.index("-c") + 1]
        command = [
            "clang",
            "--target=arm-none-eabi",
            "-isystem",
            f"{devkitpro}/devkitARM/arm-none-eabi/include",
            *[
                a
                for a in out
                if not a.startswith(("-mcpu", "-mtune", "-mthumb-interwork"))
            ],
        ]
        entries.append(
            {"directory": f"{root}/build", "file": source, "arguments": command}
        )
    Path("compile_commands.json").write_text(json.dumps(entries, indent=1) + "\n")
    print(f"compile_commands.json: {len(entries)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
