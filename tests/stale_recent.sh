#!/usr/bin/env bash
# A Recently Played entry whose file is gone: opening it must show an error,
# drop the entry and leave the rest of the card alone, not make a save and
# start an emulator with no ROM.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-stale-recent${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# name  keys-after-START
run_case() {
  local name=$1
  shift
  local cart=$carts/stale-$name shots=dist/shots/stale-$name log
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
cart=$carts/stale-after
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
cart=$carts/stale-delete
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
cart=$carts/stale-long
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

# A Recently Played NOR game that is no longer on the game NOR shows an
# error and comes off the list (v0.3.2: it stayed)
cart=$carts/stale-nor
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
printf 'NOR:Gone.gba\n/Other.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:START -k 104:- -k 130:A -k 134:- \
  -f 300 >"$cart/kernel.log" 2>&1
grep -q '^kernel: launch Gone.gba: not on the game NOR' "$cart/kernel.log" ||
  fail "the gone NOR game showed no error: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
[ "$recent" = "/Other.gba " ] || fail "after the gone NOR game, Recently Played has '$recent'"
echo "PASS: a Recently Played NOR game gone from the NOR shows an error and comes off the list"

# keys FRAME KEY...: press/release pairs, a key every 8 frames (_ a pause)
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    [ "$key" = _ ] || printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# Hello.gba written to NOR with WRITE TO NOR ADDON, RESET on and the other
# switches' slots erased, then started from the NOR list: its launch logs
# no save states and makes no state file (v0.3.2: "savestates 65535", and
# it made one)
cart=$carts/stale-nordel
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Other.gba
printf '/Other.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
head -c $((8 << 20)) /dev/zero | tr '\0' '\377' >"$cart/kernel-nor.bin"
printf '\001\000' | dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * 1)) conv=notrunc status=none
# The list: SAVER, Hello.gba, Other.gba; WRITE TO NOR ADDON is the menu's
# fourth row
# shellcheck disable=SC2046 # $(keys) is a list of options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(keys 100 DOWN A DOWN DOWN DOWN A) -f 600 \
  >"$cart/1-write.log" 2>&1
grep -q '^kernel: nor: writing Hello.gba at 00000000, patched 1' "$cart/1-write.log" ||
  fail "Hello.gba wasn't written to NOR with addons: $(grep '^kernel: ' "$cart/1-write.log" | tail -2)"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(keys 100 R A _ A) -f 500 >"$cart/2-boot.log" 2>&1
grep -q '^kernel: launch: loading Hello.gba from NOR at .*, patched 1, savestates 0$' "$cart/2-boot.log" ||
  fail "the NOR launch logged: $(grep '^kernel: launch: loading' "$cart/2-boot.log")"
if mdir -b -i "$cart/sd.img" ::/RTS 2>/dev/null | grep -q .; then
  fail "the NOR launch with SAVESTATE off made a state file"
fi
recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
[ "$recent" = "NOR:Hello.gba /Other.gba " ] || fail "after the NOR launch, Recently Played has '$recent'"
echo "PASS: a NOR game written with RESET alone starts with no save states"

# For FORMAT ALL below: a copy of the cart, with another NOR game listed
format=$carts/stale-norformat
rm -rf "$format" && cp -r "$cart" "$format"
printf 'NOR:Hello.gba\n/Other.gba\nNOR:Old.gba\n' >"$format/recent.txt"
mcopy -o -i "$format/sd.img" "$format/recent.txt" "::/SAVER/Recently play.txt"

# NOR DELETE: A copies Hello's save to SAVER, then on the NOR list A on
# Hello, DELETE (the second row), A yes. It comes off Recently Played
# (v0.3.2: it stayed).
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 104:- $(keys 300 R A DOWN A _ A) \
  -f 700 >"$cart/3-delete.log" 2>&1
grep -q 'dialog: Delete this game from NOR?: Hello.gba: A, yes' "$cart/3-delete.log" ||
  fail "Hello.gba wasn't deleted from NOR: $(grep '^kernel: ' "$cart/3-delete.log" | tail -2)"
recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
[ "$recent" = "/Other.gba " ] || fail "after NOR DELETE, Recently Played has '$recent'"
echo "PASS: a game deleted from NOR comes off Recently Played"

# FORMAT ALL (the third row), A yes: every NOR game comes off Recently
# Played (v0.3.2: they stayed)
# shellcheck disable=SC2046
OMEGA_DIR=$format emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 104:- $(keys 300 R A DOWN DOWN A _ A) \
  -w 'recent: forgot NOR:Old.gba' -h @60 >"$format/3-format.log" 2>&1 ||
  fail "FORMAT ALL: Old.gba never came off Recently Played: $(grep '^kernel: ' "$format/3-format.log" | tail -2)"
grep -q '^kernel: nor: erased the game NOR' "$format/3-format.log" ||
  fail "FORMAT ALL didn't run: $(grep '^kernel: ' "$format/3-format.log" | tail -2)"
recent=$(mtype -i "$format/sd.img" "::/SAVER/Recently play.txt" | tr '\n' ' ')
[ "$recent" = "/Other.gba " ] || fail "after FORMAT ALL, Recently Played has '$recent'"
echo "PASS: FORMAT ALL takes every NOR game off Recently Played"
