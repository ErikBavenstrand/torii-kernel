#!/usr/bin/env bash
# A Recently Played entry whose file is gone: opening it must show an error
# and leave the card alone, not make a save and start an emulator with no ROM.
# Runs inside the torii-emu container after `just build` and `just emu-headless`.
set -euo pipefail

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# name  keys-after-START
run_case() {
  local name=$1
  shift
  local cart=dist/cart-stale-$name shots=dist/shots/stale-$name log
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  log=$cart/kernel.log
  mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER
  printf '/Games/%s\n' "$name" >"$cart/recent.txt"
  mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"

  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:START -k 104:- "$@" \
    -s 300:"$shots/error.png" -f 300 >"$log" 2>&1 || fail "$name: the emulator failed: $(tail -3 "$log")"
  grep -qF "launch $name: can't open it" "$log" || fail "$name: no error logged: $(grep '^kernel: ' "$log" | tail -3)"
  if mdir -b -i "$cart/sd.img" ::/SAVER | grep -qv 'Recently play.txt'; then
    fail "$name: the kernel wrote to /SAVER: $(mdir -b -i "$cart/sd.img" ::/SAVER | tr '\n' ' ')"
  fi
  echo "PASS: a missing $name in Recently Played shows an error"
}

# A GB game starts as soon as it is picked; a GBA game after CLEAN BOOT
run_case Gone.gb -k 130:A -k 134:-
run_case Gone.gba -k 130:A -k 134:- -k 170:A -k 174:-
