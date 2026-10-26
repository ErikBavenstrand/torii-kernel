#!/usr/bin/env python3
"""Compares Torii's decoding of a cheat corpus with mGBA's, for
tests/cheat_corpus.sh: every difference is explained by a rule of
tests/cheat_expected.toml (tools/cheat_diff.py) or is a mismatch.

Usage: cheat_corpus.py REPORT TORII.tsv MGBA.tsv FILE.cht...

Writes the full report to REPORT (totals per format, cheats only one
decoder reads, each rule's matches and the mismatches by code type, with
examples) and prints its summary. Exits 1 on a mismatch no rule explains.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import cheat_diff


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    out, torii, mgba, files = Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4:]
    expected = cheat_diff.load_expected()
    rules = expected["decode"]
    all_cheats = list(cheat_diff.cheats(files, torii, mgba))
    text, mismatches = cheat_diff.report(
        all_cheats, rules, f"Torii's cheat decoders against mGBA's: {len(files)} files"
    )
    # The device's reading of hand-made codes, where the sample has one
    expect = Path(files[0]).parent / "expect.tsv"
    failed = 0
    if expect.exists():
        lines = cheat_diff.check_expectations(expect, torii, expected["bugs"])
        text += "\nAs the device reads them (expect.tsv):\n" + "".join(
            f"  {t}: {line}\n" for t, line in lines
        )
        for t, line in lines:
            if t != "PASS":
                print(f"{'FAIL' if t == 'FAIL' else 'PASS'}: cheat_corpus: {t} {line}")
            failed += t == "FAIL"
    out.write_text(text, encoding="utf-8")
    summary = text.split("\nIntentional differences")[0]
    print(summary.rstrip())
    print(f"(full report: {out})")
    if failed:
        print(
            f"FAIL: cheat_corpus: {failed} codes decode unlike the device (expect.tsv)"
        )
        return 1
    if mismatches:
        print(
            f"FAIL: cheat_corpus: {mismatches} mismatches no rule of tests/cheat_expected.toml explains"
        )
        return 1
    print(
        f"PASS: cheat_corpus: {len(all_cheats)} cheats in {len(files)} files decode alike or as a rule explains"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
