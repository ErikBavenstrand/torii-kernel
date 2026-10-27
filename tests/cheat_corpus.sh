#!/usr/bin/env bash
# Decodes every cheat in a folder of libretro GBA .cht files with Torii's
# decoders (and lowering) and with mGBA's own, and checks every difference
# against tests/cheat_expected.toml (tests/cheat_corpus.py): the report goes
# to REPORT, and an unexplained mismatch fails. `just cheat-corpus` runs it
# on libretro's database (dist/cheat-corpus, fetched by
# tools/cheat_corpus_fetch.sh, never committed); tests/host.sh on the
# committed tests/cheats/corpus_sample.cht. Runs inside the torii-emu
# container; needs emu/mgba (emu/fetch.sh).
#
# Usage: cheat_corpus.sh DIR|FILE.cht REPORT
set -euo pipefail

src=${1:-dist/cheat-corpus}
report=${2:-dist/cheat-corpus/report.txt}
out=dist/cheat-corpus-out/$(basename "$src" .cht)
mkdir -p "$out" "$(dirname "$report")"
python3 tools/hostcc.py "$out/cheat_corpus" tools/cheat_corpus.c source/cheatcode.c source/cheatlower.c
tools/mgba_cheats.sh "$out/cheat_oracle" tools/cheat_oracle.c

if [ -d "$src" ]; then
  mapfile -d '' files < <(find "$src" -name '*.cht' -print0 | sort -z)
else
  files=("$src")
fi
if [ "${#files[@]}" = 0 ]; then
  echo "no .cht files in $src (just cheat-corpus fetches libretro's)"
  exit 1
fi
"$out/cheat_corpus" "${files[@]}" >"$out/torii.tsv" 2>"$out/torii-summary.txt"
"$out/cheat_oracle" "${files[@]}" >"$out/mgba.tsv"
python3 tests/cheat_corpus.py "$report" "$out/torii.tsv" "$out/mgba.tsv" "${files[@]}"
