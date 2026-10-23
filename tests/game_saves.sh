#!/usr/bin/env bash
# The per-game save manager (source/screens/game_saves.c), through the keys,
# each case on a card of its own: Hello.gba in the GBA list, START for its
# popup, Saves (fifth: Play, Favourite, Clean boot, Boot with addon, Saves),
# then the manager's rows (the slots, New slot, Import .sav) and a slot's
# actions (Launch, Set as current, Rename, Duplicate, Delete, History, Export):
#   open        the manager lists the slots; B returns to the page
#   new         New slot (named q, blank)
#   rename      Rename Main to q (the record follows)
#   duplicate   Duplicate Main as q: the same bytes
#   delete      Delete q asks, then q goes to the trash
#   last        Delete on the only slot is refused; Main stays
#   history     History lists the versions, Restore puts the newest in the
#               slot and the old current in the history
#   export      Export writes /torii/export/Hello - Main.sav
#   import      Import .sav picks /Z.sav for Main; Main's old bytes go to history
#   import-size a file of the wrong size is refused; nothing changes
#   set         Set as current changes the record only
#   launch      Launch with this slot starts q's file, with no picker
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
# route: the file for a run to the manager: the GBA page's first game, its
# popup, Saves. k N KEY...: presses 8 frames apart from frame N (of the last
# wait); w TEXT: waits for a log line; s NAME: waits for a screen
route() {
  local to
  to=$(nav_game_menu gba 0)
  ROUTE=${to#-i }
  printf '%s\n' "$(_nav_keys 12 DOWN DOWN DOWN DOWN A)" '-w "screen ready: game-saves"' >>"$ROUTE"
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
w "game-saves: cancelled"
w "nav: page gba ready"
run -f 800
logged "screen ready: game-saves"
echo "PASS: game saves: Start > Saves opens the manager, B returns to the list"

# new: a blank slot q
card new Main
route
k 30 DOWN A
scr rename
k 10 A
k 24 START
scr slot-new-choice
k 30 A
scr game-saves
k 30 B
run -f 600
logged "slots: new q: 0"
has $S/q.sav || fail "new: no q.sav"
[ "$(letter $S/q.sav)" = $'\xff' ] || fail "new: q.sav isn't blank"
echo "PASS: game saves: New slot makes a blank slot"

# rename: Main to q
card rename Main
route
k 30 A
scr game-saves-slot
k 30 DOWN DOWN A
scr rename
k 10 B B B B A
k 64 START
scr game-saves
k 30 B
run -f 600
logged "slots: rename Main to q: 0"
{ has $S/q.sav && ! has $S/Main.sav; } || fail "rename: files wrong"
[ "$(slot_of)" = q ] || fail "rename: the record says $(slot_of)"
echo "PASS: game saves: Rename renames the slot and the record follows"

# duplicate: Main as q
card duplicate Main
route
k 30 A
scr game-saves-slot
k 30 DOWN DOWN DOWN A
scr rename
k 10 A
k 24 START
scr game-saves
k 30 B
run -f 600
logged "gsaves: duplicate Main as q: 0"
[ "$(sum $S/q.sav)" = "$(sum $S/Main.sav)" ] || fail "duplicate: q isn't Main's bytes"
echo "PASS: game saves: Duplicate copies the slot"

# delete: q, after the question
card delete Main q
route
k 30 DOWN A
scr game-saves-slot
k 30 DOWN DOWN DOWN DOWN A
scr dialog
k 30 A
scr game-saves
k 30 B
run -f 600
logged "gsaves: delete q: 0"
! has $S/q.sav || fail "delete: q.sav is still there"
has /torii/trash || fail "delete: no trash"
has $S/Main.sav || fail "delete: Main went"
echo "PASS: game saves: Delete asks, then the slot goes to the trash"

# delete, B: nothing happens
card delete-no Main q
route
k 30 DOWN A
scr game-saves-slot
k 30 DOWN DOWN DOWN DOWN A
scr dialog
k 30 B
scr game-saves-slot
k 30 B
scr game-saves
k 30 B
run -f 600
not_logged "gsaves: delete"
has $S/q.sav || fail "delete-no: q.sav went"
echo "PASS: game saves: B on the Delete question keeps the slot"

# last: the only slot can't go
card last Main
route
k 30 A
scr game-saves-slot
k 30 DOWN DOWN DOWN DOWN A
scr dialog
k 30 A
scr game-saves-slot
k 30 B
scr game-saves
k 30 B
run_err -f 600
grep -qF "kernel error: gsaves: -5" "$cart/run.log" || fail "last: no refusal"
has $S/Main.sav || fail "last: Main went"
not_logged "gsaves: delete"
echo "PASS: game saves: the only slot is not deleted"

# history: restore version 2
card history Main
mmd -i "$cart/sd.img" ::/torii/backups ::/torii/backups/Hello.gba ::/torii/backups/Hello.gba/Main
put torii/backups/Hello.gba/Main/0001-20260101-120000.sav B
put torii/backups/Hello.gba/Main/0002-00000000-000000.sav C
route
k 30 A
scr game-saves-slot
k 30 DOWN DOWN DOWN DOWN DOWN A
scr game-saves-history
k 30 A
scr dialog
k 30 A
scr game-saves-history
k 30 B B B
run -f 700
logged "gsaves: restore Main version 2: 0"
[ "$(letter $S/Main.sav)" = C ] || fail "history: Main is $(letter $S/Main.sav), not C"
h=$(hist Main)
echo "$h" | grep -q "^0003-" || fail "history: the old current isn't version 3: $h"
echo "$h" | grep -q "^0001-20260101-120000" || fail "history: version 1 is gone: $h"
! echo "$h" | grep -q "^0002-" || fail "history: version 2 stayed in the history: $h"
echo "PASS: game saves: History restores a version, the old current goes into the history"

# export, twice
card export Main q
route
k 30 A
scr game-saves-slot
k 30 DOWN DOWN DOWN DOWN DOWN DOWN A
scr dialog
k 30 A
scr game-saves-slot
k 30 DOWN DOWN DOWN DOWN DOWN DOWN A
scr dialog
k 30 A
scr game-saves-slot
k 30 B B B
run -f 800
logged "gsaves: export Main to /torii/export/Hello - Main.sav: 0"
[ "$(sum "/torii/export/Hello - Main.sav")" = "$(sum $S/Main.sav)" ] || fail "export: the copy differs"
has "/torii/export/Hello - Main (2).sav" || fail "export: the second export was not numbered"
echo "PASS: game saves: Export writes a copy to /torii/export, a second one is numbered"

# import: /Z.sav into q (two slots: the target is asked)
card import Main q
put Z.sav Z 65536
route
k 30 DOWN DOWN DOWN A
scr game-saves-files
k 30 DOWN A
scr game-saves-target
k 30 DOWN A
scr dialog
k 30 A
scr game-saves
k 30 B
run -f 700
logged "gsaves: import /Z.sav into q: 0"
[ "$(letter $S/q.sav)" = Z ] || fail "import: q is $(letter $S/q.sav), not Z"
[ "$(hist q | wc -l)" = 1 ] || fail "import: q's old bytes aren't in its history"
[ "$(letter $S/Main.sav)" = M ] || fail "import: Main changed"
echo "PASS: game saves: Import .sav replaces the chosen slot, the old file goes into history"

# import-size: a file of the wrong size
card import-size Main
put Z.sav Z 100
route
k 30 DOWN DOWN A
scr game-saves-files
k 30 DOWN A
scr dialog
k 30 A
scr dialog
k 30 A
scr game-saves
k 30 B
run_err -f 700
grep -qF "kernel error: gsaves: -7" "$cart/run.log" || fail "import-size: no refusal"
[ "$(letter $S/Main.sav)" = M ] || fail "import-size: Main changed"
echo "PASS: game saves: a file of the wrong size is refused"

# set: the record only
card set Main q
route
k 30 DOWN A
scr game-saves-slot
k 30 DOWN A
scr game-saves
k 30 B
run -f 600
[ "$(slot_of)" = q ] || fail "set: the record says $(slot_of)"
not_logged "launch: loading"
echo "PASS: game saves: Set as current changes the record"

# launch: q starts with its file, with no picker
card launch Main q
route
k 30 DOWN A
scr game-saves-slot
k 30 A
run -u "launch: loading" -f 2400
logged "gsaves: current q: 0"
logged "save: $S/q.sav exists"
not_logged "screen ready: slot-picker"
[ "$(slot_of)" = q ] || fail "launch: the record says $(slot_of)"
echo "PASS: game saves: Launch with this slot starts it without the picker"
