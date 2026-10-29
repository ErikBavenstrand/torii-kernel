#!/usr/bin/env bash
# Rename (source/screens/rename.c) through the keys, on one card over
# several power-ons:
#   1. a GBA game's Rename: "q" typed after its name, START: /torii/names.txt
#      gets the name
#   2. the next power-on's keyboard starts from that name (the walk read it
#      back); B deletes the "q", START: the game has its own name again and
#      its line goes
#   3. Cancel after a change asks "Discard changes?": B answers Cancel (the
#      keyboard stays), Discard closes it, and nothing is written
#   4. the SD card page's Rename of a file to a name its folder has: "That
#      name is taken." and the keyboard stays; the name back as it was, START
#      closes it with nothing renamed
#   5. a names.txt over 116 KB: the walk logs it once, Rename says why it
#      can't keep the name
#   6. the SD card page's Rename of a game with a custom name: the name
#      follows, and the index shows it at once
#   7. the game popup's Original name drops that name again
# The save that follows an SD file's rename is tests/saves.sh rename's.
# Runs inside the torii-emu container after `just build`, `just
# emu-headless` and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-rename"

cart=$carts/rename shots=dist/shots/rename
rm -rf "$shots"
mkdir -p "$cart" "$shots"

fail() {
  echo "FAIL: rename: $*" >&2
  [ ! -f "$cart/run.log" ] || grep '^kernel' "$cart/run.log" | tail -15 >&2
  exit 1
}

boot() {
  # Each power-on starts on home (Last place would open the page it left)
  mdel -i "$cart/sd.img" ::/torii/recent.txt ::/torii/recent.txt.bak 2>/dev/null || true
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null 2>"$cart/run.log" || true
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
  ! grep -q '^kernel error: ' "$cart/run.log" || fail "an error: $(grep '^kernel error: ' "$cart/run.log" | head -1)"
}

logged() {
  grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"
}

names() {
  mtype -i "$cart/sd.img" ::/torii/names.txt 2>/dev/null | grep -v '^#' || true
}

mkfs.fat -C -F 32 -n RENAME "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Helloq.gba

# 1: a name of its own
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_rename gba 0) -k @10:A -k @14:- -k @24:START -k @28:- -w "rename: kept" \
  -s @40:"$shots/1-list.png" -f 2400
logged "rename: from Hello"
logged 'names: /Hello.gba is "Helloq"'
names | grep -q $'^/Hello.gba\tHelloq$' || fail "names.txt doesn't have the name: $(names)"

# 2: its own name again
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_rename gba 0) -k @10:B -k @14:- -k @24:START -k @28:- -w "rename: kept" -f 2400
logged "rename: from Helloq"
logged "names: /Hello.gba is its own name again"
! names | grep -q 'Hello' || fail "names.txt still has the name: $(names)"

# 3: Cancel after a change asks; Discard leaves everything as it was
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_rename gba 0) -k @10:A -k @14:- \
  -k @20:DOWN -k @24:- -k @28:DOWN -k @32:- -k @36:DOWN -k @40:- \
  -k @48:RIGHT -k @52:- -k @56:RIGHT -k @60:- -k @64:RIGHT -k @68:- -k @72:RIGHT -k @76:- \
  -k @84:A -k @88:- -w "screen ready: confirm" -s @12:"$shots/3-discard.png" -k @20:B -k @24:- \
  -k @50:A -k @54:- -w "screen ready: confirm" -k @12:RIGHT -k @16:- -k @24:A -k @28:- \
  -w "rename: cancelled" -f 2400
[ "$(grep -c 'screen ready: confirm' "$cart/run.log")" = 2 ] || fail "Cancel didn't ask twice"
! grep -qF 'names: /Hello.gba is' "$cart/run.log" || fail "a cancelled rename wrote names.txt"

# 4: a file's name its folder has (Rename: UP three times from Launch)
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_file /Hello.gba 0) -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- \
  -w "screen ready: rename" \
  -k @10:A -k @14:- -k @24:START -k @28:- -w "rename: That name is taken." -s @4:"$shots/4-taken.png" \
  -k @10:B -k @14:- -k @24:START -k @28:- -w "rename: cancelled" -f 2400
logged "rename: from Hello"
logged "rename /Hello.gba: /Helloq.gba is there already"
mtype -i "$cart/sd.img" ::/Hello.gba >/dev/null || fail "Hello.gba went"
echo "PASS: rename: a game's name in names.txt and back, Cancel asks after a change, a taken file name is refused"

# 5: a hand-edited names.txt over what the kernel reads (116 KB): the walk
# says so once, as an error, and Rename says why it can't keep the name
{
  printf '/Hello.gba\tBig\n'
  for _ in $(seq 2000); do printf '# %s\n' "$(printf '%060d' 0)"; done
} >"$cart/names.txt"
mcopy -o -i "$cart/sd.img" "$cart/names.txt" ::/torii/names.txt
mdel -i "$cart/sd.img" ::/torii/names.txt.bak 2>/dev/null || true
mdel -i "$cart/sd.img" ::/torii/recent.txt ::/torii/recent.txt.bak 2>/dev/null || true
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_game_rename gba 0) -k @10:A -k @14:- \
  -k @24:START -k @28:- -w "rename: names.txt is too large to change" -s @4:"$shots/5-full.png" -f 2400 \
  >/dev/null 2>"$cart/run.log" || fail "Rename didn't say names.txt is too large"
[ "$(grep -c '^kernel error: library: /torii/names.txt is [0-9]* bytes, over' "$cart/run.log")" = 1 ] ||
  fail "the walk didn't say once that names.txt is too large"
logged "rename: from Hello"
names | grep -q $'^/Hello.gba\tBig$' || fail "names.txt changed"
echo "PASS: rename: a names.txt too large to read is logged once and Rename says so"

# 6: the SD card page's Rename of a game with a custom name: the name
# follows the file, and its row shows it at once
printf '/Hello.gba\tCustom Hello\n' >"$cart/names.txt"
mcopy -o -i "$cart/sd.img" "$cart/names.txt" ::/torii/names.txt
mdel -i "$cart/sd.img" ::/torii/names.txt.bak ::/Helloq.gba 2>/dev/null || true
boot -u "library: wrote" -f 900 # the index takes the name
logged "library: 1 custom names parsed"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_file /Hello.gba 0) -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- \
  -w "screen ready: rename" -k @10:A -k @14:- -k @24:START -k @28:- -w "rename: kept" -f 2400
logged "rename: /Hello.gba to Helloq.gba"
logged "library: /Hello.gba is Helloq.gba now, shown as Custom Hello"
names | grep -q $'^/Helloq.gba\tCustom Hello$' || fail "the custom name didn't follow the file: $(names)"
echo "PASS: rename: a game's custom name follows its SD rename and shows at once"

# 7: the game popup's Original name (above Rename for a game with a name of
# its own): the name goes from names.txt, and the row shows the file's name
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @36:UP -k @40:- \
  -k @48:A -k @52:- -w "its own name dropped" -f 2400
logged "games: /Helloq.gba: its own name dropped"
! names | grep -q 'Helloq' || fail "names.txt still has the name: $(names)"
echo "PASS: rename: Original name drops a game's own name"
