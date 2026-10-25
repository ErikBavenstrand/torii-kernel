#!/usr/bin/env bash
# A game's saves (source/screens/game_saves.c on screens/save_page.c), through
# the keys, each case on a card of its own: Hello.gba in the GBA list, START
# for its popup, Saves (second, after Play), then the slots' page (the slots,
# then + New slot), a slot's popup (START: Play, Make active when it isn't,
# Backups, Rename, Copy to new slot, Delete) and its backups
# (RIGHT):
#   open        the page lists the slots; B returns to the list
#   new         New slot, Empty, named q
#   taken       a name another slot has is refused, the keyboard stays
#   rename      Rename Main to q (the record follows)
#   copy        Copy to new slot: Main as q, the same bytes
#   delete      Delete q asks, then q goes to the trash
#   delete-no   Cancel on the question keeps it
#   last        Delete on the only slot is refused; Main stays
#   backups     RIGHT lists the backups, Restore puts one in the slot and the
#               old save in the backups as the newest
#   import      New slot, From a .sav file: /Z.sav as a new slot Z; nothing else changes
#   import-size a file of the wrong size is refused; nothing changes
#   active      Make active changes the record only
#   launch      A on a slot plays it, with no Play with
#   gb-new      a GB game (Save.gb, the GB page): New slot offers Empty and a
#               copy, no .sav file; Empty makes a blank 64 KB slot
#   gb-launch   A on a GB game's second slot plays it with that slot
# Runs inside the torii-emu container after `just build` and `just
# emu-headless`, and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-gamesaves"

fail() {
  echo "FAIL: game saves: $*" >&2
  [ ! -f "$cart/run.log" ] || grep '^kernel' "$cart/run.log" | tail -15 >&2
  exit 1
}

S=/torii/saves/Hello.gba

# card [slot...]: a new card with Hello.gba and the named slots (8 KB of the
# slot's first letter), the record naming the first as current
card() {
  cart=$carts/$1
  shift
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
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

# put NAME LETTER [BYTES]: a file of BYTES (8192) of LETTER on the card
put() {
  python3 -c "import sys; sys.stdout.buffer.write(sys.argv[2].encode() * int(sys.argv[3]))" "$1" "$2" "${3:-8192}" >"$cart/f.bin"
  mcopy -i "$cart/sd.img" "$cart/f.bin" "::$1"
}

boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null 2>"$cart/run.log" || true
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
  ! grep -q '^kernel error: ' "$cart/run.log" || fail "an error: $(grep '^kernel error: ' "$cart/run.log" | head -1)"
}
# boot_err: the same, an error is expected (a refused action logs one)
boot_err() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null 2>"$cart/run.log" || true
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
}

logged() { grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"; }
not_logged() { ! grep -qF "kernel: $1" "$cart/run.log" || fail "log line there: $1"; }
sum() { mtype -i "$cart/sd.img" "::$1" | md5sum | cut -d' ' -f1; }
slot_of() { mtype -i "$cart/sd.img" ::$S/game.ini | tr -d '\r' | sed -n 's/^slot = //p'; }
has() { mdir -b -i "$cart/sd.img" "::$1" >/dev/null 2>&1; }
letter() { mtype -i "$cart/sd.img" "::$1" | head -c 1; }
hist() { mdir -b -i "$cart/sd.img" "::/torii/backups/Hello.gba/$1" 2>/dev/null | sed 's|.*/||' | sort; }

# The options of a run go in a file (-i), so the waits' texts keep their blanks.
# route: the file for a run to the slots' page: the GBA page's first game, its
# popup, Saves. k N KEY...: presses 8 frames apart from frame N (of the last
# wait); w TEXT: waits for a log line; s NAME: waits for a screen
route() {
  local to
  to=$(nav_game_menu gba 0)
  ROUTE=${to#-i }
  printf '%s\n' "$(_nav_keys 12 DOWN A)" '-w "screen ready: game-saves"' >>"$ROUTE"
}
k() { _nav_keys "$@" >>"$ROUTE"; }
w() { printf -- '-w "%s"\n' "$1" >>"$ROUTE"; }
scr() { w "screen ready: $1"; }
run() { boot -i "$ROUTE" "$@"; }
run_err() { boot_err -i "$ROUTE" "$@"; }

# open
card open Main
route
k 30 B
w "nav: page gba ready"
run -f 800
logged "screen ready: game-saves"
echo "PASS: game saves: Start > Saves opens the slots, B returns to the list"

# new: an empty slot q
card new Main
route
k 30 DOWN A
scr game-saves-new
k 20 A
scr rename
k 10 A
k 24 START
scr game-saves
k 30 B
run -f 700
logged "gsaves: new q (0): 0"
has $S/q.sav || fail "new: no q.sav"
[ "$(letter $S/q.sav)" = $'\xff' ] || fail "new: q.sav isn't blank"
[ "$(slot_of)" = Main ] || fail "new: the active slot changed"
echo "PASS: game saves: New slot, Empty makes a blank slot"

# taken: a second slot named q, New slot q is refused
card taken Main q
route
k 30 DOWN DOWN A
scr game-saves-new
k 20 A
scr rename
k 10 A
k 24 START
w "rename: That name is taken."
run -f 900
logged "rename: That name is taken."
! grep -qF "gsaves: new" "$cart/run.log" || fail "taken: a slot was made"
echo "PASS: game saves: a name another slot has is refused and the keyboard stays"

# rename: Main to q
card rename Main
route
k 30 START
scr game-saves-slot
k 20 DOWN DOWN A
scr rename
k 10 B B B B A
k 64 START
scr game-saves
k 30 B
run -f 700
logged "slots: rename Main to q: 0"
{ has $S/q.sav && ! has $S/Main.sav; } || fail "rename: files wrong"
[ "$(slot_of)" = q ] || fail "rename: the record says $(slot_of)"
echo "PASS: game saves: Rename renames the slot and the record follows"

# copy: Main as q
card copy Main
route
k 30 START
scr game-saves-slot
k 20 DOWN DOWN DOWN A
scr rename
k 10 A
k 24 START
scr game-saves
k 30 B
run -f 700
logged "gsaves: copy Main as q: 0"
[ "$(sum $S/q.sav)" = "$(sum $S/Main.sav)" ] || fail "copy: q isn't Main's bytes"
echo "PASS: game saves: Copy to new slot copies the slot"

# delete: q, after the question (Cancel is chosen first: RIGHT, then A)
card delete Main q
route
k 30 DOWN START
scr game-saves-slot
k 20 UP A
scr confirm
k 20 RIGHT A
w "gsaves: delete q"
k 30 B
run -f 700
logged "gsaves: delete q: 0"
! has $S/q.sav || fail "delete: q.sav is still there"
has /torii/trash || fail "delete: no trash"
has $S/Main.sav || fail "delete: Main went"
echo "PASS: game saves: Delete asks, then the slot goes to the trash"

# delete-no: Cancel keeps it
card delete-no Main q
route
k 30 DOWN START
scr game-saves-slot
k 20 UP A
scr confirm
k 20 A
k 50 B
k 70 B
run -f 700
not_logged "gsaves: delete"
has $S/q.sav || fail "delete-no: q.sav went"
echo "PASS: game saves: Cancel on the Delete question keeps the slot"

# last: the only slot can't go
card last Main
route
k 30 START
scr game-saves-slot
k 20 UP A
scr dialog
k 30 A
k 60 B
run_err -f 700
grep -qF "kernel error: gsaves: -5" "$cart/run.log" || fail "last: no refusal"
has $S/Main.sav || fail "last: Main went"
not_logged "gsaves: delete"
echo "PASS: game saves: the only slot is not deleted"

# backups: restore version 2 (the newest: first)
card backups Main
mmd -i "$cart/sd.img" ::/torii/backups ::/torii/backups/Hello.gba ::/torii/backups/Hello.gba/Main
put torii/backups/Hello.gba/Main/0001-20260101-120000.sav B
put torii/backups/Hello.gba/Main/0002-00000000-000000.sav C
route
k 30 RIGHT
scr game-saves-backups
k 30 A
scr confirm
k 20 RIGHT A
w "gsaves: restore Main"
k 40 B
scr game-saves
k 30 B
run -f 800
logged "gsaves: restore Main version 2: 0"
[ "$(letter $S/Main.sav)" = C ] || fail "backups: Main is $(letter $S/Main.sav), not C"
h=$(hist Main)
echo "$h" | grep -q "^0003-" || fail "backups: the old save isn't version 3: $h"
echo "$h" | grep -q "^0001-20260101-120000" || fail "backups: version 1 is gone: $h"
! echo "$h" | grep -q "^0002-" || fail "backups: version 2 stayed in the backups: $h"
echo "PASS: game saves: Backups restores one, the old save becomes the newest backup"

# import: /Z.sav as a new slot (the browser starts in the ROM's folder, the
# root: the torii folder, then Z.sav)
card import Main
put Z.sav Z 65536
route
k 30 DOWN A
scr game-saves-new
k 20 DOWN DOWN A
scr game-saves-files
k 30 DOWN A
scr rename
k 24 START
scr game-saves
k 30 B
run -f 800
logged "gsaves: new Z (2): 0"
[ "$(letter $S/Z.sav)" = Z ] || fail "import: Z is $(letter $S/Z.sav 2>/dev/null)"
[ "$(letter $S/Main.sav)" = M ] || fail "import: Main changed"
[ -z "$(hist Main)" ] || fail "import: Main got a backup"
echo "PASS: game saves: New slot from a .sav file makes a new slot and changes nothing else"

# import-size: a file of the wrong size
card import-size Main
put Z.sav Z 100
route
k 30 DOWN A
scr game-saves-new
k 20 DOWN DOWN A
scr game-saves-files
k 30 DOWN A
scr rename
k 24 START
scr dialog
k 30 A
scr game-saves
k 30 B
run_err -f 800
grep -qF "kernel error: gsaves: -7" "$cart/run.log" || fail "import-size: no refusal"
! has $S/Z.sav || fail "import-size: a slot was made"
[ "$(letter $S/Main.sav)" = M ] || fail "import-size: Main changed"
echo "PASS: game saves: a file of the wrong size is refused"

# active: the record only
card active Main q
route
k 30 DOWN START
scr game-saves-slot
k 20 DOWN A
w "gsaves: active q"
k 30 B
run -f 700
[ "$(slot_of)" = q ] || fail "active: the record says $(slot_of)"
not_logged "launch: loading"
echo "PASS: game saves: Make active changes the record"

# launch: A on q plays it, with no Play with
card launch Main q
route
k 30 DOWN A
run -u "launch: loading" -f 2400
logged "gsaves: active q: 0"
logged "save: $S/q.sav exists"
not_logged "screen ready: slot-picker"
[ "$(slot_of)" = q ] || fail "launch: the record says $(slot_of)"
echo "PASS: game saves: A on a slot plays it without Play with"

# A GB game's slots: its emulator's 64 KB of cart SRAM each
G=/torii/saves/Save.gb
gb_card() {
  cart=$carts/$1
  shift
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
  python3 tests/gb_rom.py "$cart/Save.gb"
  mcopy -i "$cart/sd.img" "$cart/Save.gb" ::/Save.gb
  mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::$G
  for s in "$@"; do
    python3 -c "import sys; sys.stdout.buffer.write(sys.argv[1][0].upper().encode() * 65536)" "$s" >"$cart/f.sav"
    mcopy -i "$cart/sd.img" "$cart/f.sav" "::$G/$s.sav"
  done
  printf 'save_type = 0\nboot_mode = 0\nslot = %s\n' "$1" >"$cart/game.ini"
  mcopy -i "$cart/sd.img" "$cart/game.ini" ::$G/game.ini
}
gb_route() {
  local to
  NAV_SHOWN=gb # the card has only Save.gb: home shows the GB page, not the GBA one
  to=$(nav_game_menu gb 0)
  ROUTE=${to#-i }
  printf '%s\n' "$(_nav_keys 12 DOWN A)" '-w "screen ready: game-saves"' >>"$ROUTE"
}
gb_slot_of() { mtype -i "$cart/sd.img" ::$G/game.ini | tr -d '\r' | sed -n 's/^slot = //p'; }

# gb-new: New slot has Empty and a copy of Main, and no .sav file: DOWN
# twice wraps to Empty (two items), so A makes an empty slot q
gb_card gb-new Main
gb_route
k 30 DOWN A
scr game-saves-new
k 20 DOWN DOWN A
scr rename
k 10 A
k 24 START
scr game-saves
k 30 B
run -f 900
logged "gsaves: new q (0): 0"
mdir -b -i "$cart/sd.img" "::$G/q.sav" >/dev/null 2>&1 || fail "gb-new: no q.sav"
[ "$(mtype -i "$cart/sd.img" "::$G/q.sav" | wc -c)" = 65536 ] || fail "gb-new: q.sav isn't 64 KB"
[ "$(mtype -i "$cart/sd.img" "::$G/q.sav" | head -c 1)" = $'\xff' ] || fail "gb-new: q.sav isn't blank"
[ "$(gb_slot_of)" = Main ] || fail "gb-new: the active slot changed"
echo "PASS: game saves: a GB game's New slot offers Empty and a copy, no .sav file, and makes a blank 64 KB slot"

# gb-launch: A on q plays it with q
gb_card gb-launch Main q
gb_route
k 30 DOWN A
run -u "launch: loading" -f 2400
logged "gsaves: active q: 0"
logged "save: $G/q.sav exists"
logged "launch: loading Save.gb with Jagoomba Color"
[ "$(gb_slot_of)" = q ] || fail "gb-launch: the record says $(gb_slot_of)"
echo "PASS: game saves: A on a GB game's second slot plays it with that slot"
