#!/usr/bin/env python3
"""Merge a job's coverage runs: tests/cov_merge.py RUNS DIR JOB STATUS.

mgba-shot (TORII_COVERAGE=RUNS) writes one file per power-on, CRC-XXXXXX
after the ROM run. This ORs them into DIR/CRC.cov, one per kernel, and writes
DIR/job (the job's name) and DIR/status (its exit status): the layout
tests/all.sh gives its jobs, which tools/testmap.py records.
"""

import pathlib
import sys


def merge(runs: pathlib.Path, out: pathlib.Path, job: str, status: str) -> None:
    out.mkdir(parents=True, exist_ok=True)
    merged, size = {}, 0
    for run in sorted(runs.iterdir()) if runs.is_dir() else []:
        crc, bits = run.name.split("-")[0], run.read_bytes()
        size = len(bits)
        merged[crc] = merged.get(crc, 0) | int.from_bytes(bits, "little")
    for crc, bits in merged.items():
        (out / f"{crc}.cov").write_bytes(bits.to_bytes(size, "little"))
    (out / "job").write_text(job + "\n")
    (out / "status").write_text(status + "\n")


if __name__ == "__main__":
    merge(
        pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3], sys.argv[4]
    )
