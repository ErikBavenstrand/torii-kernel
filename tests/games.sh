#!/usr/bin/env bash
# The game lists (source/screens/games.c): A on a row starts its game, with
# a hard reset while L is held (the row selected then); the game goes
# on Recent with the time it was started, and the Recent page's "played"
# text follows the clock.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-games${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cart=$carts/games
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Other.gba
boot() { # log [mgba-shot options]
  local log=$1
  shift
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$log" 2>&1 || true
  ! grep -qE 'assert failed|^crash at frame' "$log" || fail "a crash or failed assert: $(grep -E 'assert|crash' "$log" | head -2)"
}

# 2026-10-21 12:00 UTC: a set clock
t0=1792584000

# Power-on 1: on Other (the GBA page's second row), L held doesn't page (L
# and R do nothing in game lists), and A with L still held starts Other
# with a hard reset, by Settings' GBA boot mode (with addons, the default)
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" -C $t0 $(nav_game gba 1) -k @24:L -k @60:L+A -k @64:- -u "launch: loading" -f 1500
grep -q '^kernel: games: start /Other.gba (how 4, hard reset)' "$cart/1.log" ||
  fail "L+A didn't start Other.gba with a hard reset: $(grep '^kernel: games' "$cart/1.log" | tail -2)"
grep -q "^kernel: launch: boot mode with addons (Settings')" "$cart/1.log" ||
  fail "A didn't follow Settings' boot mode: $(grep '^kernel: launch' "$cart/1.log" | head -3)"
grep -q '^kernel: launch: loading Other.gba' "$cart/1.log" || fail "Other.gba didn't load: $(grep '^kernel: launch' "$cart/1.log" | tail -2)"
echo "PASS: L held doesn't page a game list, and L+A starts the selected row with a hard reset"

# Power-on 2, 30 s later on a running clock: A at "Save sav?", then
# Recent's first row is Hello, played "just now"; a minute on it says "1m
# ago" (the page refreshes the text as it changes)
# (the menu starts facing the item it was left on, GBA: LEFT to Recent)
boot "$cart/2.log" -c $((t0 + 30)) -k 100:A -k 106:- \
  -w "nav: home ready" -k @8:LEFT -k @12:- -k @24:A -k @28:- -w "nav: page recent ready" -s "@8:$cart/recent.png" \
  -u "games: row 0 played 1m ago" -f 6000
grep -q '^kernel: games: row 0 played just now' "$cart/2.log" ||
  fail "Recent's first row isn't Hello, just now: $(grep '^kernel: games' "$cart/2.log" | head -3)"
grep -q '^kernel: games: row 0 played 1m ago' "$cart/2.log" ||
  fail "the played text didn't follow the clock: $(grep '^kernel: games' "$cart/2.log" | tail -3)"
echo "PASS: a launched game is first on Recent, played just now, and the text follows the clock"

# A launch that fails (a name too long for its save record) brings the page
# back on the same row, listed at once: it loads the index again first,
# never showing an empty card while it loads
cart=$carts/games-fail
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
long=$(printf 'L%.0s' $(seq 249)).gba
for g in A.gba B.gba "$long"; do mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$g"; done
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_game gba 2) -k @30:A -k @34:- -w "screen ready: dialog" -k @20:B -k @24:- \
  -w "nav: page gba ready" -k @20:START -k @24:- -w "screen ready: game-popup" -f 2400
grep -q '^kernel: screen ready: game-popup LLL' "$cart/1.log" ||
  fail "after the failed launch the page isn't on the same row: $(grep '^kernel: screen ready: game-popup' "$cart/1.log")"
! grep -q '^kernel: games: a new index' "$cart/1.log" || fail "the page came back before the index, empty"
echo "PASS: a failed launch brings the page back on the same row, listed at once"

# Delete on a Recent entry whose file is already gone: it leaves Recent
cart=$carts/games-gone
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/A.gba
printf '/Gone.gba\n/A.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
NAV_SHOWN="recent gba"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_game_delete recent 0) -w "delete: /Gone.gba" -f 900
grep -q '^kernel: delete /Gone.gba: not on the card' "$cart/1.log" || fail "no delete of the gone entry: $(grep '^kernel: delete' "$cart/1.log")"
recent=$(mtype -i "$cart/sd.img" ::/torii/recent.txt | grep -v '^[#@]' | cut -f2 | tr '\n' ' ')
[ "$recent" = "/A.gba " ] || fail "after deleting the gone entry, Recent has '$recent'"
echo "PASS: Delete on an entry whose file is gone takes it off the list"

# Game info's save type on a card with no saves folder yet: the folders are
# made and the game's record (game.ini) kept
cart=$carts/games-mde
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/A.gba
NAV_SHOWN=gba
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_game_menu gba 0) -k @12:UP -k @16:- -k @24:UP -k @28:- -k @36:A -k @40:- \
  -w "screen ready: game-info" -k @20:RIGHT -k @24:- -k @36:B -k @40:- -w "save type of" -f 1500
grep -q '^kernel: save type of /A.gba: 1 (0)' "$cart/1.log" || fail "the save type wasn't kept: $(grep '^kernel: save type' "$cart/1.log")"
mdir -b -i "$cart/sd.img" ::/torii/saves/A.gba | grep -q 'game.ini' || fail "no /torii/saves/A.gba/game.ini"
echo "PASS: Game info's save type is kept in the game's record, its folder made when missing"

# The boot mode (Settings' GBA boot mode, a game's own in Game info): kept
# in the game's record, beside the save type
cart=$carts/games-boot
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/A.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/B.gba
record() { mtype -i "$cart/sd.img" "::/torii/saves/$1.gba/game.ini" | tr -d '\r' | tr '\n' ' '; }
NAV_SHOWN=gba
# Game info on A: Save type RIGHT (SRAM), DOWN to Boot mode, RIGHT twice
# (Clean), B: both are kept
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_game_menu gba 0) -k @12:UP -k @16:- -k @24:UP -k @28:- -k @36:A -k @40:- \
  -w "screen ready: game-info" -k @20:RIGHT -k @24:- -k @32:DOWN -k @36:- -k @44:RIGHT -k @48:- \
  -k @56:RIGHT -k @60:- -k @72:B -k @76:- -w "boot mode of" -f 1500
grep -q '^kernel: save type of /A.gba: 1 (0)' "$cart/1.log" || fail "the save type wasn't kept: $(grep '^kernel: save type' "$cart/1.log")"
grep -q '^kernel: boot mode of /A.gba: 2 (0)' "$cart/1.log" || fail "the boot mode wasn't kept: $(grep '^kernel: boot mode' "$cart/1.log")"
[ "$(record A)" = "save_type = 1 boot_mode = 2 slot = Main " ] || fail "A's record holds $(record A)"
echo "PASS: Game info keeps a game's boot mode beside its save type"

# Settings slots in the kernel NOR (made by the first power-on; tests/setnor.py)
setting() { python3 tests/setnor.py set "$cart/kernel-nor.bin" "$1=$2"; }
# Start on Home, so each power-on's route starts there; Auto save on (no
# question at power-on after a launch); the Reset hotkey on, an addon to
# start with
setting 32 2
setting 14 1
setting 1 1

# A on A (row 0) starts it clean, its own boot mode, while Settings says with addons
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/2.log" $(nav_game gba 0) -k @20:A -k @24:- -u "launch: loading" -f 1500
grep -q "^kernel: launch: boot mode clean (the game's)" "$cart/2.log" ||
  fail "A didn't follow the game's boot mode: $(grep '^kernel: launch' "$cart/2.log" | head -3)"
grep -q '^kernel: launch: loading A.gba, CLEAN BOOT' "$cart/2.log" || fail "A.gba didn't start clean: $(grep '^kernel: launch' "$cart/2.log" | tail -2)"
echo "PASS: A starts a game by its own boot mode"

# A on B (row 1, no boot mode of its own) starts it with the addons, as
# Settings' GBA boot mode says by default
NAV_SHOWN="recent gba"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/3.log" $(nav_game gba 1) -k @20:A -k @24:- -u "launch: loading" -f 1500
grep -q "^kernel: launch: boot mode with addons (Settings')$" "$cart/3.log" ||
  fail "A didn't follow Settings' boot mode: $(grep '^kernel: launch' "$cart/3.log" | head -3)"
grep -q '^kernel: launch: loading B.gba, BOOT WITH ADDON' "$cart/3.log" || fail "B.gba didn't start with addons: $(grep '^kernel: launch' "$cart/3.log" | tail -2)"
echo "PASS: A starts a game with its addons by default"

# Settings' GBA boot mode Clean (slot 35): B starts clean
setting 35 1
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/4.log" $(nav_game gba 1) -k @20:A -k @24:- -u "launch: loading" -f 1500
grep -q "^kernel: launch: boot mode clean (Settings')" "$cart/4.log" ||
  fail "A didn't follow Settings' boot mode Clean: $(grep '^kernel: launch' "$cart/4.log" | head -3)"
grep -q '^kernel: launch: loading B.gba, CLEAN BOOT' "$cart/4.log" || fail "B.gba didn't start clean: $(grep '^kernel: launch' "$cart/4.log" | tail -2)"
echo "PASS: Settings' GBA boot mode Clean starts a game clean"

# A launch that changes the save type keeps the boot mode
# shellcheck disable=SC2046 # nav.sh's options
# (its save was SRAM's 32 KB: the type change keeps it in the history, and the notice takes A)
boot "$cart/5.log" $(nav_launch /A.gba addon save=3) -w "save: type change notice shown" -k @20:A -k @26:- -u "launch: loading" -f 1500
[ "$(record A)" = "save_type = 3 boot_mode = 2 slot = Main " ] || fail "after a launch with save type 3, A's record holds $(record A)"
echo "PASS: a launch's save type keeps the game's boot mode"

# The Favourites page's order is kept between visits and changed in place
# once a popup closes: B turned off and on again in its popup (no change),
# then off; the rows follow (the emulator checks the change against a whole
# sort), and only the first visit sorts. Favourites in three runs: GBA, GB
# and one not on the card.
cart=$carts/games-fav
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/torii
for g in A.gba B.gba C.gba D.gb; do mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$g"; done
printf '/A.gba\n/B.gba\n/C.gba\n/D.gb\n/Gone.gba\n' >"$cart/fav.txt"
mcopy -i "$cart/sd.img" "$cart/fav.txt" ::/torii/favorites.txt
NAV_SHOWN="fav gba gb"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_game_menu fav 1) -k @8:DOWN -k @12:- -k @16:DOWN -k @20:- -k @24:DOWN -k @28:- -k @36:A -k @40:- \
  -w "userdata: favourite /B.gba off" -k @20:A -k @24:- -w "userdata: favourite /B.gba on" \
  -k @20:B -k @24:- -w "popup: closed" -k @20:START -k @24:- -w "screen ready: game-popup B" \
  -k @8:DOWN -k @12:- -k @16:DOWN -k @20:- -k @24:DOWN -k @28:- -k @36:A -k @40:- -w "userdata: favourite /B.gba off" -k @20:B -k @24:- \
  -w "popup: closed" -k @20:START -k @24:- -w "screen ready: game-popup C" -f 3000
grep -q "^kernel: screen ready: game-popup C" "$cart/1.log" ||
  fail "after B went, row 1 isn't C: $(grep -E '^kernel: (screen ready|games|userdata)' "$cart/1.log" | tail -6)"
[ "$(grep -c '^kernel: games: favourite /B.gba out in place' "$cart/1.log")" = 1 ] ||
  fail "B's going wasn't made in place: $(grep '^kernel: games' "$cart/1.log")"
grep -q "^kernel: games: 4 favourites kept" "$cart/1.log" || fail "the page sorted again: $(grep '^kernel: games' "$cart/1.log")"
# A favourite added from the GBA page (B, row 1) goes into the kept order
# in place (the GB page's run and the one not on the card around it)
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/2.log" $(nav_game fav 0) -k @20:B -k @24:- -w "nav: home ready" -k @8:RIGHT -k @12:- -k @20:A -k @24:- \
  -w "nav: page gba ready" -k @8:DOWN -k @12:- -k @20:START -k @24:- -w "screen ready: game-popup B" \
  -k @12:DOWN -k @16:- -k @20:DOWN -k @24:- -k @28:DOWN -k @32:- -k @40:A -k @44:- -w "userdata: favourite /B.gba on" -f 3000
grep -q '^kernel: games: favourite /B.gba in in place' "$cart/2.log" ||
  fail "B's coming back wasn't made in place: $(grep '^kernel: games' "$cart/2.log")"
echo "PASS: Favourites keeps its order between visits and changes it in place"
