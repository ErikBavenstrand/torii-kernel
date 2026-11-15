#!/usr/bin/env bash
# Fetches the Game Boy accuracy test ROMs into dist/gb-tests for
# `just gb-accuracy` (never committed: none of them carries a licence we
# could ship): Blargg's suite (https://github.com/retrio/gb-test-roms, a
# shallow clone pinned to a commit) and Matt Currie's dmg-acid2 and cgb-acid2
# (release files, checked against their SHA-256). A set already fetched is
# kept. Exits 0 even when offline, so the test can skip.
set -uo pipefail

out=dist/gb-tests
blargg_commit=c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15
mkdir -p "$out"

if [ ! -d "$out/retrio/cpu_instrs" ]; then
  rm -rf "$out/retrio"
  if git clone -q https://github.com/retrio/gb-test-roms.git "$out/retrio" 2>/dev/null &&
    git -C "$out/retrio" checkout -q "$blargg_commit"; then
    rm -rf "$out/retrio/.git"
  else
    echo "gb tests: couldn't fetch Blargg's ROMs" >&2
    rm -rf "$out/retrio"
  fi
fi

fetch() { # FILE URL SHA256
  [ -f "$out/$1" ] && return 0
  if curl -fsSL -o "$out/$1.part" "$2" && echo "$3  $out/$1.part" | shasum -a 256 -c - >/dev/null 2>&1; then
    mv "$out/$1.part" "$out/$1"
  else
    rm -f "$out/$1.part"
    echo "gb tests: couldn't fetch $1" >&2
  fi
}
fetch dmg-acid2.gb https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb 464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1
fetch cgb-acid2.gbc https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc 197fb0bcec544f0400527fc707e0a94f55435974986e6986b424ace5de81720e
exit 0
