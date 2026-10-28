#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Run the advisory static analysis and compare it with its baseline.

usage: analyze.py files                 C files to analyze, one per line
       analyze.py tidy                  for each, its cache key and the file (clang-tidy's)
       analyze.py project OUT            compile_commands.json for those files
       analyze.py gcc DIR                shell script running GCC's -fanalyzer
       analyze.py check DIR BASELINE     fail on findings not in BASELINE
       analyze.py baseline DIR BASELINE  write DIR's findings to BASELINE

The analyzed files are the kernel's own C files in source/, which
compile_commands.json (`just compile-commands`) lists; the vendored FatFs
(source/ff15) is left out. DIR holds each tool's output: cppcheck.txt,
clang-tidy.txt and gcc.txt. A tool that reports nothing where the baseline
expects findings fails the check, as does a GCC run that didn't finish.

The GCC script runs in the toolchain container from the repository root. It
writes one script per source file to DIR and runs them nproc at a time, each
into its own output file, then prints the outputs in file order, so the
messages of parallel runs can't interleave. The end marker follows only when
every run succeeded. A finding is the tool, file, check and message,
without line numbers, so edits elsewhere in a file don't make old findings
look new. The baseline lists each finding as often as it occurs.

Each file's GCC and clang-tidy output is kept in dist/analyze/cache under a
hash of what it depends on: the tool's command, the file and every header
its dependency file (build/NAME.d, from `just build`) lists, and the
analysis's own configuration. A file none of that changed for reuses its
output instead of running the tool again.
"""

import hashlib
import json
import re
import shlex
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GCC = "/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc"
# The last line of gcc.txt, so an analysis that never ran can't pass
DONE = "gcc -fanalyzer: analyzed"
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


CACHE = ROOT / "dist/analyze/cache"
# What every file's analysis depends on besides its own sources
CONFIG = ["justfile", ".clang-tidy", "tools/analyze.py"]


def cache_key(tool: str, rel: str, entry) -> str:
    """A hash of what the tool's output for rel depends on, or "" (don't cache)
    without a dependency file."""
    deps = ROOT / "build" / (Path(rel).stem + ".d")
    if not deps.exists():
        return ""
    digest = hashlib.sha256(f"{tool}\0{json.dumps(entry['arguments'])}\0".encode())
    paths = {rel, *CONFIG}
    for word in deps.read_text().split():
        if word.endswith(":") or word == "\\":
            continue
        if word.startswith("/src/"):
            paths.add(word[len("/src/") :])  # the build container's path
    for path in sorted(paths):
        try:
            data = (ROOT / path).read_bytes()
        except OSError:
            return ""
        digest.update(f"{path}\0".encode() + hashlib.sha256(data).digest())
    return digest.hexdigest()


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
    elif command == "tidy":
        for rel, entry in commands():
            print(cache_key("clang-tidy", rel, entry) or "none")
            print(rel)
    elif command == "project":
        out = [entry for _rel, entry in commands()]
        Path(sys.argv[2]).write_text(json.dumps(out, indent=1) + "\n")
    elif command == "gcc":
        # One analyzer run per file, output to /dev/null; warnings go to stderr.
        # The scripts are written in the container: files written on the host
        # can reach it late (see tools/fresh.py).
        print("set -e")
        print(f'd="$PWD"/{shlex.quote(sys.argv[2])}')
        print('rm -rf "$d" && mkdir -p "$d"')
        print(f"mkdir -p {shlex.quote(str(CACHE / 'gcc'))}")
        count = 0
        for rel, entry in commands():
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
            key = cache_key("gcc", rel, entry)
            cached = shlex.quote(str(CACHE / "gcc" / f"{key}.txt")) if key else ""
            print(f"cat > \"$d\"/{count:03}.sh <<'EOF'")
            print('out="${0%.sh}.txt"')
            print('exec > "$out" 2>&1')
            if cached:
                print(f"[ -f {cached} ] && exec cat {cached}")
            print(f"cd {shlex.quote(entry['directory'])} && {shlex.join(args)} || exit")
            if cached:
                print(f'cp "$out" {cached}.$$ && mv {cached}.$$ {cached}')
            print("EOF")
            count += 1
        print("set +e")
        print("""printf '%s\\n' "$d"/*.sh | xargs -I {} -P "$(nproc)" sh {}""")
        print("status=$?")
        print('cat "$d"/*.txt')
        print(f"[ $status -eq 0 ] && echo '{DONE} {count} files'")
        print("exit $status")
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
        # A tool that finds nothing where the baseline expects findings more
        # likely failed to run than fixed them all.
        expected = Counter(f.split(" ", 1)[0] for f in known.elements())
        silent = [tool for tool in expected if not by_tool[tool]]
        if silent:
            print(
                f"{', '.join(silent)} found nothing, but the baseline expects findings: check dist/analyze/"
            )
            return 1
        if DONE not in (directory / "gcc.txt").read_text(errors="replace"):
            print("gcc -fanalyzer did not finish: check dist/analyze/gcc.txt")
            return 1
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
