#!/usr/bin/env bash
# The global save manager (source/screens/saves.c, Settings > Library > Saves)
# through the keys, on one card over several power-ons:
#   1. a game's ROM is renamed outside the kernel (mren), so its saves are
#      orphaned: the screen counts the space the saves, history and trash use
#      (the scan's log line has the figures) and lists the orphan
#   2. Relink: the picker lists the library's games, the GBA ones for a .gba
#      orphan; the one chosen, asked, gets the save and history folders
#   3. the next power-on's screen lists no orphan
#   4. an orphan's Delete (asked) moves it to the trash; Empty trash now
#      (asked) empties it
#   5. Keep leaves an orphan where it is
# Runs inside the torii-emu container after `just build`, `just
# emu-headless` and building tests/roms.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-saves-manager"

cart=$carts/saves-manager shots=dist/shots/saves-manager
rm -rf "$shots"
mkdir -p "$cart" "$shots"

fail() {
  echo "FAIL: saves manager: $*" >&2
  [ ! -f "$cart/run.log" ] || grep '^kernel' "$cart/run.log" | tail -15 >&2
  exit 1
}

img=$cart/sd.img
boot() {
  mdel -i "$img" ::/torii/recent.txt ::/torii/recent.txt.bak 2>/dev/null || true
  # The clock stands at 2026-01-21, 20 days after the trash's folder
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -C 1769000000 "$@" >/dev/null 2>"$cart/run.log" || true
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
  ! grep -q '^kernel error: ' "$cart/run.log" || fail "an error: $(grep '^kernel error: ' "$cart/run.log" | head -1)"
}
logged() { grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"; }

card_dir() {
  local dir=${1#::} part='' IFS=/
  for p in $dir; do
    [ -n "$p" ] || continue
    part+=/$p
    mmd -i "$img" "::$part" 2>/dev/null || true
  done
}
card_save() { # CHAR SIZE ::/PATH
  local f=$carts/tmp.sav
  head -c "$2" /dev/zero | tr '\000' "$1" >"$f"
  card_dir "$(dirname "$3")"
  mcopy -i "$img" "$f" "$3"
}
has() { mdir -i "$img" "::$1" >/dev/null 2>&1; }
content() { mtype -i "$img" "::$1" 2>/dev/null | head -c 1; }

mkfs.fat -C -F 32 -n SAVES "$img" 65536 >/dev/null
card_dir ::/G
mcopy -i "$img" tests/roms/hello/hello.gba ::/G/Old.gba
mcopy -i "$img" tests/roms/idle/idle.gba ::/G/Other.gba
mcopy -i "$img" tests/roms/hello/hello.gba ::/G/Third.gba
card_save A 8192 ::/torii/saves/G/Old.gba/Main.sav
card_save B 8192 ::/torii/saves/G/Old.gba/Boss.sav
card_save H 8192 ::/torii/backups/G/Old.gba/Main/0001-20260101-000000.sav
card_save T 4096 ::/torii/trash/20260101-000000-1/G/Gone.gba/Main.sav
card_save C 8192 ::/torii/saves/G/Third.gba/Main.sav
# The ROM is renamed outside the kernel: its saves are orphaned
mren -i "$img" ::/G/Old.gba ::/G/Renamed.gba

# The Saves row (nav_setting), A on it, and the screen
to_screen=(-k @8:A -k @12:- -w "screen ready: saves")

# 1 and 2: the figures, then Relink to Renamed.gba (the picker's second GBA game)
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_setting saves) "${to_screen[@]}" -s @6:"$shots/1-list.png" \
  -k @10:DOWN -k @14:- -k @18:DOWN -k @22:- -k @26:DOWN -k @30:- -k @34:DOWN -k @38:- -k @42:DOWN -k @46:- \
  -k @56:A -k @60:- -w "saves: popup" -k @20:A -k @24:- -w "saves: picker" \
  -s @10:"$shots/2-picker.png" -k @20:DOWN -k @24:- -k @32:A -k @36:- \
  -k @60:RIGHT -k @64:- -k @72:A -k @76:- -w "orphan: relinked" -f 6000
logged "saves: 1 orphans"
# Its saves and history (A, B, H), the trash (T) and the live game's save (C)
logged "orphan: scan: 2 games, 1 orphans (0 more), saves $((8192 * 3)), history 8192, trash 4096 bytes"
logged "orphan: relinked /G/Old.gba to /G/Renamed.gba, 2 slots, 0 renamed"
has /torii/saves/G/Renamed.gba/Main.sav || fail "Main.sav didn't follow"
[ "$(content /torii/saves/G/Renamed.gba/Boss.sav)" = B ] || fail "Boss.sav isn't the orphan's"
[ "$(content /torii/backups/G/Renamed.gba/Main/0001-20260101-000000.sav)" = H ] || fail "the history didn't follow"
! has /torii/saves/G/Old.gba || fail "the old save folder is left"
! has /torii/backups/G/Old.gba || fail "the old history folder is left"
[ "$(content /torii/saves/G/Third.gba/Main.sav)" = C ] || fail "another game's save was touched"

# 3: no orphan now
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_setting saves) "${to_screen[@]}" -s @6:"$shots/3-none.png" -f 1200
logged "saves: 0 orphans"

# 4: an orphan again: Delete (the popup's last item, asked) then Empty trash now (asked)
mren -i "$img" ::/G/Renamed.gba ::/G/Moved.gba
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_setting saves) "${to_screen[@]}" \
  -k @10:DOWN -k @14:- -k @18:DOWN -k @22:- -k @26:DOWN -k @30:- -k @34:DOWN -k @38:- -k @42:DOWN -k @46:- \
  -k @56:A -k @60:- -w "saves: popup" -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:A -k @48:- \
  -k @70:RIGHT -k @74:- -k @82:A -k @86:- -w "orphan: deleted /G/Renamed.gba" -f 6000
! has /torii/saves/G/Renamed.gba || fail "the orphan is still in the saves"
! has /torii/backups/G/Renamed.gba || fail "the orphan's history is still there"
trash=$(mdir -b -i "$img" ::/torii/trash | grep -v 20260101 | head -1 | sed 's|/$||; s|.*/||')
[ -n "$trash" ] || fail "nothing new in the trash"
[ "$(content "/torii/trash/$trash/G/Renamed.gba/Main.sav")" = A ] || fail "the orphan's save isn't in the trash"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_setting saves) "${to_screen[@]}" \
  -k @10:DOWN -k @14:- -k @18:DOWN -k @22:- -k @26:DOWN -k @30:- -k @38:A -k @42:- \
  -k @70:RIGHT -k @74:- -k @82:A -k @86:- -w "orphan: trash /torii/trash/" -f 6000
logged "orphan: scan: 1 games, 0 orphans"
[ -z "$(mdir -b -i "$img" ::/torii/trash 2>/dev/null)" ] || fail "the trash isn't empty: $(mdir -b -i "$img" ::/torii/trash)"

# 5: Keep leaves it
card_save K 8192 ::/torii/saves/G/Lost.gba/Main.sav
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_setting saves) "${to_screen[@]}" \
  -k @10:DOWN -k @14:- -k @18:DOWN -k @22:- -k @26:DOWN -k @30:- -k @34:DOWN -k @38:- -k @42:DOWN -k @46:- \
  -k @56:A -k @60:- -w "saves: popup" -k @20:DOWN -k @24:- -k @32:A -k @36:- \
  -s @30:"$shots/5-kept.png" -f 800
[ "$(content /torii/saves/G/Lost.gba/Main.sav)" = K ] || fail "Keep changed the save"
echo "PASS: saves manager: the figures, Relink, Delete, Empty trash and Keep through the keys"
