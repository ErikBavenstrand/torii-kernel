#!/usr/bin/env python3
"""The per-frame cycle budget: "frame-cycles: NAME N" lines against tests/perf/budget.txt.

usage: tests/perf.py check LOG...    check the lines in the LOG files
       tests/perf.py update LOG...   record them in tests/perf/budget.txt
       tests/perf.py self-test       check this file's logic on fixed lines

The kernel logs a line per measured frame (the engine's cycle timer, read
from cascaded timers). For each NAME the largest N counts. A figure fails
over FRAME_CYCLES, a whole frame (228 lines of 1232 cycles), or over 10%
above its recorded value; a NAME measured but not recorded, or recorded but
not measured by a full run, fails too: `just perf-update` re-records.
tests/shots.py runs this check on the lines its scenarios log.
"""

import re
import sys
from pathlib import Path

BUDGET = Path("tests/perf/budget.txt")
FRAME_CYCLES = 280_896
DRIFT = 0.10
LINE = re.compile(r"frame-cycles: (\S+) (\d+)\s*$")

HEADER = """\
# Cycles per frame for tests/perf.py, written by `just perf-update`.
# NAME CYCLES: the most one frame of NAME took in the scenarios' runs.
# A run fails over 280896 (a whole frame) or 10% above the figure here.
"""


def lines(text: str) -> list[str]:
    """The frame-cycles lines in a log, as logged."""
    return [line for line in text.splitlines() if LINE.search(line)]


def peaks(found: list[str]) -> dict[str, int]:
    """NAME -> the largest N among the lines."""
    out: dict[str, int] = {}
    for line in found:
        m = LINE.search(line)
        if m:
            out[m[1]] = max(out.get(m[1], 0), int(m[2]))
    return out


def read_budget(path: Path) -> dict[str, int]:
    if not path.exists():
        return {}
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            name, cycles = line.split()
            out[name] = int(cycles)
    return out


def check(found: list[str], budget: dict[str, int], partial: bool = False) -> list[str]:
    """Problems with the measured figures; partial: only some scenarios ran."""
    problems = []
    for name, n in sorted(peaks(found).items()):
        if n > FRAME_CYCLES:
            problems.append(f"{name}: {n} cycles, over a frame ({FRAME_CYCLES})")
        elif name not in budget:
            problems.append(
                f"{name}: {n} cycles, not in {BUDGET} (run `just perf-update`)"
            )
        elif n > budget[name] * (1 + DRIFT):
            pct = 100 * (n - budget[name]) / budget[name]
            problems.append(
                f"{name}: {n} cycles, {pct:.0f}% over the recorded {budget[name]}"
            )
    if not partial:
        measured = peaks(found)
        problems += [
            f"{name}: in {BUDGET}, but no run measured it (run `just perf-update`)"
            for name in sorted(budget)
            if name not in measured
        ]
    return problems


def summary(found: list[str]) -> str:
    p = peaks(found)
    if not p:
        return "no frame-cycles lines logged"
    worst = max(p, key=p.get)
    return f"{len(p)} measured, the most {worst} with {p[worst]} of {FRAME_CYCLES}"


def update(found: list[str], path: Path, partial: bool = False) -> None:
    old = read_budget(path)
    new = dict(old) if partial else {}
    new.update(peaks(found))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        HEADER + "".join(f"{k} {v}\n" for k, v in sorted(new.items())), encoding="utf-8"
    )
    for name in sorted(set(old) | set(new)):
        if old.get(name) != new.get(name):
            print(f"perf: {name}: {old.get(name, '-')} -> {new.get(name, '-')}")
    print(f"perf: {len(new)} figures in {path}")


def self_test() -> int:
    log = """kernel: boot: WAITCNT 4317 for the menu
kernel: frame-cycles: sd-list 120000
kernel: frame-cycles: sd-list 150000
GBA Debug: frame-cycles: settings1 99000
kernel: frame-cycles: scroll 300000
kernel: frame-cycles: sd-list nonsense
"""
    found = lines(log)
    cases = [
        (peaks(found), {"sd-list": 150000, "settings1": 99000, "scroll": 300000}),
        (
            check(found, {"sd-list": 140000, "settings1": 80000, "gone": 5}),
            [
                "scroll: 300000 cycles, over a frame (280896)",
                "settings1: 99000 cycles, 24% over the recorded 80000",
                f"gone: in {BUDGET}, but no run measured it (run `just perf-update`)",
            ],
        ),
        # 150000 is just within 10% of 136364; partial: no complaint about "gone"
        (
            check(
                found, {"sd-list": 136364, "settings1": 99000, "gone": 5}, partial=True
            ),
            ["scroll: 300000 cycles, over a frame (280896)"],
        ),
        (
            check(lines("kernel: frame-cycles: new 10\n"), {}),
            [f"new: 10 cycles, not in {BUDGET} (run `just perf-update`)"],
        ),
        (check([], {}), []),
    ]
    failed = [f"got {got!r}, want {want!r}" for got, want in cases if got != want]
    for f in failed:
        print(f"FAIL: perf self-test: {f}")
    if not failed:
        print("PASS: cycle budget checker (self-test on fixed frame-cycles lines)")
    return 1 if failed else 0


def main() -> int:
    args = sys.argv[1:]
    if args == ["self-test"]:
        return self_test()
    if len(args) < 2 or args[0] not in ("check", "update"):
        sys.exit(__doc__)
    found = [
        line
        for log in args[1:]
        for line in lines(Path(log).read_text(encoding="utf-8"))
    ]
    if args[0] == "update":
        update(found, BUDGET)
        return 0
    problems = check(found, read_budget(BUDGET))
    for p in problems:
        print(f"FAIL: perf: {p}")
    if not problems:
        print(f"PASS: cycle budget: {summary(found)}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
