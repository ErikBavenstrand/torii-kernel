#!/usr/bin/env bash
# Decodes every cheat in a folder of libretro GBA .cht files (dist/cheat-corpus
# unless given) with Torii's decoders and with mGBA's own, prints Torii's
# summary by status and lists where the two decodings differ (just
# cheat-corpus). The corpus is the user's download, never committed. Runs
# inside the torii-emu container, after `just emu-headless`.
set -euo pipefail

dir=${1:-dist/cheat-corpus}
out=dist/cheat-corpus-out
mkdir -p "$out"
python3 tools/hostcc.py dist/host/cheat_corpus tools/cheat_corpus.c source/cheatcode.c source/cheatlower.c
gcc -O1 -Wall -Werror -Iemu/mgba/include -Iemu/build-linux/include -o "$out/cheat_oracle" \
  tools/cheat_oracle.c emu/build-linux/libmgba.a -llua5.4 -lpng -lz -lm -lpthread

mapfile -d '' files < <(find "$dir" -name '*.cht' -print0 | sort -z)
if [ "${#files[@]}" = 0 ]; then
  echo "no .cht files in $dir"
  exit 1
fi
dist/host/cheat_corpus "${files[@]}" >"$out/torii.tsv"
"$out/cheat_oracle" "${files[@]}" >"$out/mgba.tsv"
python3 tests/cheat_corpus.py "$out/torii.tsv" "$out/mgba.tsv"
