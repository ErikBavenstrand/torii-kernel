"""Fail if a test script ends a pipeline with grep -q or head (just lint).

Under pipefail, grep -q and head exit as soon as they have what they need,
and the command still writing into them can die of SIGPIPE, which fails the
pipeline now and then (tests/search_more.sh midgame failed 1 run in 24 so).
Test scripts use drain_grep and drain_head from tests/pipes.sh instead.
"""

import re
import sys
from pathlib import Path

PIPED = re.compile(r"(?<!\|)\|(?!\|)\s*(grep -[A-Za-z]*q[A-Za-z]*|head)\s")


def main() -> int:
    bad = []
    for path in sorted(Path("tests").glob("*.sh")):
        for n, line in enumerate(path.read_text().splitlines(), 1):
            if not line.lstrip().startswith("#") and PIPED.search(line):
                bad.append(f"{path}:{n}: {line.strip()}")
    for line in bad:
        print(f"{line}\n  use drain_grep / drain_head (tests/pipes.sh)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
