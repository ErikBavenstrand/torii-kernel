#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Run the advisory static analysis and compare it with its baseline.

usage: analyze.py files                 C files to analyze, one per line
       analyze.py project OUT            compile_commands.json for those files
       analyze.py gcc                    shell script running GCC's -fanalyzer
       analyze.py check DIR BASELINE     fail on findings not in BASELINE
       analyze.py baseline DIR BASELINE  write DIR's findings to BASELINE

The analyzed files are the kernel's own C files in source/, which
compile_commands.json (`just compile-commands`) lists; the vendored FatFs
(source/ff15) is left out. DIR holds each tool's output: cppcheck.txt,
clang-tidy.txt and gcc.txt. A finding is the tool, file, check and message,
without line numbers, so edits elsewhere in a file don't make old findings
look new. The baseline lists each finding as often as it occurs.
"""

import json
import re
import shlex
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GCC = "/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc"
# GCC-only build flags that would stop or slow the analysis
DROPPED = {"-Werror", "-fstack-usage", "-fcallgraph-info=su", "-MMD", "-MP"}

# file:line:col: warning: message [check]  (clang-tidy, GCC)
COMPILER = re.compile(r"^(\S+?):\d+:\d+: (warning|error): (.*) \[([^\]]+)\]$")
# file:line: severity: id: message  (cppcheck, see the justfile's --template)
CPPCHECK = re.compile(r"^(\S+?):\d+: (\w+): (\w+): (.*)$")


def commands():
    entries = json.loads((ROOT / "compile_commands.json").read_text())
    for entry in entries:
        path = Path(entry["file"]).resolve()
        rel = path.relative_to(ROOT).as_posix()
        if (
            rel.startswith("source/")
            and not rel.startswith("source/ff15/")
            and rel.endswith(".c")
        ):
            yield rel, entry


def relative(path: str) -> str:
    try:
        return Path(path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path


def findings(directory: Path) -> Counter:
    found = Counter()
    for tool in ("cppcheck", "clang-tidy", "gcc"):
        output = directory / f"{tool}.txt"
        if not output.exists():
            sys.exit(f"{output} is missing; run just analyze")
        for line in output.read_text(errors="replace").splitlines():
            if tool == "cppcheck":
                match = CPPCHECK.match(line)
                if match:
                    path, severity, check, message = match.groups()
                    if severity != "information":
                        found[f"{tool} {relative(path)} {check}: {message}"] += 1
            else:
                match = COMPILER.match(line)
                if match:
                    path, _kind, message, check = match.groups()
                    found[f"{tool} {relative(path)} {check}: {message}"] += 1
    return found


def main() -> int:
    command = sys.argv[1]
    if command == "files":
        for rel, _entry in commands():
            print(rel)
    elif command == "project":
        out = [entry for _rel, entry in commands()]
        Path(sys.argv[2]).write_text(json.dumps(out, indent=1) + "\n")
    elif command == "gcc":
        # One analyzer run per file, output to /dev/null; warnings go to stderr.
        print("set -e")
        for _rel, entry in commands():
            args = [a for a in entry["arguments"] if a not in DROPPED]
            source = args[args.index("-c") + 1]
            args = [
                GCC,
                *args[1 : args.index("-c")],
                "-fanalyzer",
                "-S",
                source,
                "-o",
                "/dev/null",
            ]
            print(f"(cd {shlex.quote(entry['directory'])} && {shlex.join(args)})")
    elif command in ("check", "baseline"):
        directory, baseline = Path(sys.argv[2]), Path(sys.argv[3])
        current = findings(directory)
        if command == "baseline":
            lines = sorted(current.elements())
            baseline.write_text("".join(f"{line}\n" for line in lines))
            print(f"{baseline}: {len(lines)} findings")
            return 0
        known = (
            Counter(baseline.read_text().splitlines())
            if baseline.exists()
            else Counter()
        )
        new, gone = current - known, known - current
        by_tool = Counter(f.split(" ", 1)[0] for f in current.elements())
        print(
            "findings: "
            + ", ".join(f"{tool} {n}" for tool, n in sorted(by_tool.items()))
        )
        if gone:
            print(
                f"{sum(gone.values())} baseline findings are gone; run just analyze-baseline to drop them"
            )
        if new:
            print(
                f"{sum(new.values())} new findings (fix them, or run just analyze-baseline if they are false alarms):"
            )
            for finding in sorted(new.elements()):
                print(f"  {finding}")
            return 1
        print("no new findings")
    else:
        sys.exit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main())
