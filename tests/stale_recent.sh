#!/usr/bin/env bash
# A Recently Played entry whose file is gone: opening it must show an error
# and leave the card alone, not make a save and start an emulator with no ROM.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
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

# After the error the list must still open its own games: the entry's folder
# exists here, and the kernel used to keep looking in it
cart=dist/cart-stale-after
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER ::/Games
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
printf '/Games/Gone.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
# START, open the entry, CLEAN BOOT: File error; a key back to the list (Games,
# SAVER, Hello.gba), then down to Hello.gba, open it, CLEAN BOOT
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:START -k 104:- -k 130:A -k 134:- \
  -k 170:A -k 174:- -k 250:B -k 254:- -k 300:DOWN -k 304:- -k 316:DOWN -k 320:- -k 332:A -k 336:- \
  -k 380:A -k 384:- -f 600 >"$cart/kernel.log" 2>&1
grep -q '^kernel: launch Hello.gba: code' "$cart/kernel.log" ||
  fail "after the error, Hello.gba didn't launch: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | grep -qx '/Hello.gba' ||
  fail "Recently Played has $(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' '), expected /Hello.gba"
echo "PASS: after the error, the list's own games open"
