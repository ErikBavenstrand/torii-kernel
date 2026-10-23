#!/usr/bin/env bash
# The launch's Play with (source/screens/slot_picker.c), through the keys,
# each case on a card of its own (the games' saves persist in the cart's SRAM):
#   one-slot    a game with one slot starts straight in, no Play with
#   select      SELECT held with A on a game asks even with one slot; B starts nothing
#   select-go   and A there starts the slot
#   choose      two slots: it asks on every launch, A on the other one starts
#               with its file and the record follows
# Making, copying and renaming slots are the Saves page's (tests/game_saves.sh).
# Runs inside the torii-emu container after `just build` and `just
# emu-headless`, and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-slotpicker"

fail() {
  echo "FAIL: slot picker: $*" >&2
  [ ! -f "$cart/run.log" ] || grep '^kernel' "$cart/run.log" | tail -15 >&2
  exit 1
}

S=/torii/saves/Hello.gba

# card [slot...]: a new card with Hello.gba and the named slots (8 KB of the
# slot's first letter, 'A' for Main...), the record naming the first as current
card() {
  cart=$carts/$1
  shift
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SLOTS "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
  [ $# -gt 0 ] || return 0
  mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::$S
  for s in "$@"; do
    python3 -c "import sys; sys.stdout.buffer.write(sys.argv[1][0].upper().encode() * 8192)" "$s" >"$cart/f.sav"
    mcopy -i "$cart/sd.img" "$cart/f.sav" "::$S/$s.sav"
  done
  printf 'save_type = 0\nboot_mode = 0\nslot = %s\n' "$1" >"$cart/game.ini"
  mcopy -i "$cart/sd.img" "$cart/game.ini" ::$S/game.ini
}

boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null 2>"$cart/run.log" || true
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
  ! grep -q '^kernel error: ' "$cart/run.log" || fail "an error: $(grep '^kernel error: ' "$cart/run.log" | head -1)"
}

logged() { grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"; }
not_logged() { ! grep -qF "kernel: $1" "$cart/run.log" || fail "log line there: $1"; }
sum() { mtype -i "$cart/sd.img" "::$S/$1.sav" | md5sum | cut -d' ' -f1; }
slot_of() { mtype -i "$cart/sd.img" ::$S/game.ini | tr -d '\r' | sed -n 's/^slot = //p'; }
has() { mdir -b -i "$cart/sd.img" "::$S/$1.sav" >/dev/null 2>&1; }

# Keys from the GBA page's first row (nav_game gba 0), A with SELECT held
# when asked: "go" is A, "select" is SELECT+A. Later @N count from the
# page's "nav: page gba ready" line.
go="-k @30:A -k @34:-"
select="-k @26:SELECT -k @30:SELECT+A -k @34:SELECT -k @38:-"

# one-slot: straight in
card one-slot Main
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $go -u "launch: loading" -f 2000
logged "launch: loading Hello.gba"
not_logged "slot-picker"
echo "PASS: slot picker: a game with one slot starts straight in"

# select: the picker with one slot; B starts nothing
card select Main
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $select -w "screen ready: slot-picker" -k @30:B -k @34:- -w "launch: no slot chosen" -f 1500
not_logged "launch: loading"
logged "launch: no slot chosen, nothing started"
[ "$(slot_of)" = Main ] || fail "select: the record says $(slot_of)"
echo "PASS: slot picker: SELECT with A shows it for a one-slot game, B starts nothing"

# select, then A: the same slot starts
card select-go Main
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $select -w "screen ready: slot-picker" -k @30:A -k @34:- -u "launch: loading" -f 2000
logged "slots: playing Main"
logged "save: $S/Main.sav exists"
echo "PASS: slot picker: A on its one slot starts it"

# choose: two slots, the picker on every launch; the other slot starts
card choose Main q
before=$(sum Main)
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $go -w "screen ready: slot-picker" -k @30:DOWN -k @34:- -k @40:A -k @44:- \
  -u "launch: loading" -f 2000
logged "slots: playing q"
logged "save: $S/q.sav exists"
[ "$(slot_of)" = q ] || fail "choose: the record says $(slot_of)"
[ "$(sum Main)" = "$before" ] || fail "choose: Main.sav changed"
echo "PASS: slot picker: two slots show it on every launch, A starts the chosen one"
