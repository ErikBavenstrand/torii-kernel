#!/usr/bin/env bash
# A Recently Played entry whose file is gone: opening it must show an error,
# drop the entry and leave the rest of the card alone, not make a save and
# start an emulator with no ROM.
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
  printf '/Games/%s\n/Other.gba\n' "$name" >"$cart/recent.txt"
  mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"

  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:START -k 104:- "$@" \
    -s 300:"$shots/error.png" -f 300 >"$log" 2>&1 || fail "$name: the emulator failed: $(tail -3 "$log")"
  grep -qF "launch $name: can't open it" "$log" || fail "$name: no error logged: $(grep '^kernel: ' "$log" | tail -3)"
  if mdir -b -i "$cart/sd.img" ::/SAVER | grep -qv 'Recently play.txt'; then
    fail "$name: the kernel wrote to /SAVER: $(mdir -b -i "$cart/sd.img" ::/SAVER | tr '\n' ' ')"
  fi
  # The entry comes off the list; the others stay (v0.3.2: it stayed)
  recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
  [ "$recent" = "/Other.gba " ] || fail "$name: after the error, Recently Played has '$recent'"
  echo "PASS: a missing $name in Recently Played shows an error and comes off the list"
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
# START, open the entry, CLEAN BOOT: File error; B back to Recently Played
# (it went to the SD list before v0.3.2), B to the SD list (Games, SAVER,
# Hello.gba), then down to Hello.gba, open it, CLEAN BOOT
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:START -k 104:- -k 130:A -k 134:- \
  -k 170:A -k 174:- -k 250:B -k 254:- -k 300:B -k 304:- -k 340:DOWN -k 344:- -k 356:DOWN -k 360:- \
  -k 372:A -k 376:- -k 420:A -k 424:- -f 650 >"$cart/kernel.log" 2>&1
back=$(sed -n '/launch Gone.gba: can.t open it/,$p' "$cart/kernel.log" | grep -o 'screen ready: [a-z-]*' | grep -m1 -v dialog || true)
[ "$back" = "screen ready: recent" ] || fail "after the error, B brought '$back', not Recently Played"
grep -q '^kernel: launch Hello.gba: code' "$cart/kernel.log" ||
  fail "after the error, Hello.gba didn't launch: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
[ "$recent" = "/Hello.gba " ] || fail "Recently Played has '$recent', expected /Hello.gba alone"
echo "PASS: after the error, B goes back to Recently Played, and the list's own games open"

# A file deleted from the SD list (L+START, A) comes off Recently Played;
# the other entries stay
cart=dist/cart-stale-delete
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Other.gba
printf '/Hello.gba\n/Other.gba\nNOR:Hello.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
# The list: SAVER, Hello.gba, Other.gba
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:DOWN -k 104:- -k 116:L -k 120:L+START \
  -k 124:- -k 170:A -k 174:- -f 300 >"$cart/kernel.log" 2>&1
grep -q 'dialog: Delete this file?: Hello.gba: A, yes' "$cart/kernel.log" ||
  fail "Hello.gba wasn't deleted: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
[ "$recent" = "/Other.gba NOR:Hello.gba " ] || fail "after the delete, Recently Played has '$recent'"
echo "PASS: a file deleted from the SD list comes off Recently Played"

# A Recently Played line longer than the kernel writes (an edited file) is
# skipped: its folder would overrun the path buffer
cart=dist/cart-stale-long
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
{
  printf '/%s/x.gba\n' "$(printf 'a%.0s' $(seq 300))"
  printf '/Hello.gba\n'
} >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:START -k 104:- -k 130:A -k 134:- \
  -k 170:A -k 174:- -f 400 >"$cart/kernel.log" 2>&1
grep -q '^kernel: launch Hello.gba: code' "$cart/kernel.log" ||
  fail "the entry after an over-long line didn't open: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
echo "PASS: an over-long Recently Played line is skipped"
