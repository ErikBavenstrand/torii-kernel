#!/usr/bin/env bash
# The launch's save slot picker and the New slot and Rename flows
# (source/screens/slot_picker.c, slot_flow.c), through the keys, each case on a
# card of its own (the games' saves persist in the cart's SRAM):
#   one-slot    a game with one slot starts straight in, no picker
#   select      SELECT held with A on a game shows the picker even with one slot
#   cancel      B in the picker starts nothing; the slots and the record stay
#   new         New slot (named q, blank) in a game with Main: the new slot is
#               chosen, A starts it with its own file, and Main's is untouched
#   copy        New slot as a copy of the current slot: the same bytes
#   rename      START on Main renames it (the record follows)
#   taken       a name another slot has is refused, the keyboard stays
#   choose      two slots: the picker shows on every launch, A on the other
#               one starts with its file and the record follows
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
boot $(nav_game gba 0) $select -w "screen ready: slot-picker" -k @30:B -k @34:- -w "slot-picker: cancelled" -f 1500
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

# new: a blank slot named q, then A starts it
card new Main
before=$(sum Main)
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $select -w "screen ready: slot-picker" \
  -k @30:DOWN -k @34:- -k @40:A -k @44:- -w "screen ready: rename" \
  -k @10:A -k @14:- -k @24:START -k @28:- -w "screen ready: slot-new-choice" \
  -k @30:A -k @34:- -w "screen ready: slot-picker" -k @30:A -k @34:- -u "launch: loading" -f 2000
logged "slots: new q: 0"
logged "slots: playing q"
logged "save: $S/q.sav exists"
has q || fail "new: no q.sav"
[ "$(slot_of)" = q ] || fail "new: the record says $(slot_of)"
[ "$(sum Main)" = "$before" ] || fail "new: Main.sav changed"
echo "PASS: slot picker: New slot makes a blank slot the launch plays, Main's file untouched"

# copy: a copy of Main as q
card copy Main
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $select -w "screen ready: slot-picker" \
  -k @30:DOWN -k @34:- -k @40:A -k @44:- -w "screen ready: rename" \
  -k @10:A -k @14:- -k @24:START -k @28:- -w "screen ready: slot-new-choice" \
  -k @30:DOWN -k @34:- -k @40:A -k @44:- -w "screen ready: slot-picker" -k @30:B -k @34:- -w "slot-picker: cancelled" -f 1500
logged "slots: new q copy of Main: 0"
[ "$(sum q)" = "$(sum Main)" ] || fail "copy: q.sav isn't Main's bytes"
not_logged "launch: loading"
echo "PASS: slot picker: New slot as a copy of the current slot has its bytes"

# rename: START on Main, the name deleted and q typed
card rename Main
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $select -w "screen ready: slot-picker" -k @30:START -k @34:- -w "screen ready: rename" \
  -k @10:B -k @14:- -k @20:B -k @24:- -k @30:B -k @34:- -k @40:B -k @44:- -k @50:A -k @54:- -k @64:START -k @68:- \
  -w "screen ready: slot-picker" -k @30:B -k @34:- -w "slot-picker: cancelled" -f 1500
logged "slots: rename Main to q: 0"
has q || fail "rename: no q.sav"
! has Main || fail "rename: Main.sav is still there"
[ "$(slot_of)" = q ] || fail "rename: the record says $(slot_of)"
echo "PASS: slot picker: START renames the slot and the record follows"

# taken: a second slot named q, New slot q is refused
card taken Main q
# shellcheck disable=SC2046,SC2086
boot $(nav_game gba 0) $go -w "screen ready: slot-picker" \
  -k @30:DOWN -k @34:- -k @40:DOWN -k @44:- -k @50:A -k @54:- -w "screen ready: rename" \
  -k @10:A -k @14:- -k @24:START -k @28:- -w "rename: That name is taken." -f 3000
logged "rename: That name is taken."
echo "PASS: slot picker: a name another slot has is refused and the keyboard stays"

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
