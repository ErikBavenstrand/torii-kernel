#!/usr/bin/env bash
# Differential cheat fuzzing (just cheat-fuzz): COUNT random cheat programs
# from SEED (tests/cheat_fuzz.py) run in mGBA's engine and through Torii as
# tests/cheat_engine.sh runs its cases; each failing one is minimized and
# saved to tests/cheat_fuzz_cases.json, which `just test` then runs with the
# other cheat cases. Exits 1 when a program failed. Runs inside the torii-emu
# container after `just build`, `just emu-headless` and `just test-roms`.
#
# Usage: cheat_fuzz.sh SEED COUNT
#        cheat_fuzz.sh --decode SEED COUNT: COUNT random cheats of each format
#        decoded by Torii's decoders and mGBA's only (tests/cheat_corpus.sh,
#        its rules), addressed all over the GBA's memory; the report goes to
#        dist/cheat-fuzz/decode-SEED/report.txt
set -euo pipefail

if [ "${1:-}" = --decode ]; then
  dir=dist/cheat-fuzz/decode-$2
  rm -rf "$dir"
  python3 tests/cheat_fuzz.py decode-gen "$dir" "$2" "$3"
  exec tests/cheat_corpus.sh "$dir" "$dir/report.txt"
fi

seed=$1 count=$2
mkdir -p dist/cheat-fuzz
report=dist/cheat-fuzz/$seed-report.txt
set +e
tests/cheat_engine.sh --fuzz "$seed" "$count" | tee "$report"
status=${PIPESTATUS[0]}
set -e
cp "$report" "dist/cheat-fuzz/$seed/report.txt"
if [ "$status" != 0 ]; then
  mapfile -t failing < <(python3 tests/cheat_fuzz.py failing "dist/cheat-fuzz/$seed")
  if [ "${#failing[@]}" != 0 ]; then
    # The first CHEAT_FUZZ_MINIMIZE (default 5): each takes a few dozen runs
    failing=("${failing[@]:0:${CHEAT_FUZZ_MINIMIZE:-5}}")
    echo "cheat_fuzz: minimizing ${#failing[@]} failing programs"
    python3 tests/cheat_fuzz.py minimize "dist/cheat-fuzz/$seed" "${failing[@]}"
  fi
fi
exit "$status"
