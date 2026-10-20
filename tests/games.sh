#!/usr/bin/env bash
# The game lists (source/screens/games.c): A on a row starts its game, with
# a hard reset while L is held (the game L paged away from); the game goes
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

# Power-on 1: L held, then A on Hello (the GBA page's first row): L pages
# (wrapping to the last page), and A starts the row it left, Hello, with a
# hard reset
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" -C $t0 $(nav_game gba 0) -k @20:L -k @30:L+A -k @34:- -u "launch: loading" -f 1500
grep -q '^kernel: games: start /Hello.gba (how 1, hard reset)' "$cart/1.log" ||
  fail "L+A didn't start Hello.gba with a hard reset: $(grep '^kernel: games' "$cart/1.log" | tail -2)"
grep -q '^kernel: launch: loading Hello.gba' "$cart/1.log" || fail "Hello.gba didn't load: $(grep '^kernel: launch' "$cart/1.log" | tail -2)"
echo "PASS: L held with A starts the row L paged away from, with a hard reset"

# Power-on 2, 30 s later on a running clock: A at "Save sav?", then
# Recent's first row is Hello, played "just now"; a minute on it says "1m
# ago" (the page refreshes the text as it changes)
# (the menu starts where it was left: the GBA page; B, then UP to Recent)
boot "$cart/2.log" -c $((t0 + 30)) -k 100:A -k 106:- -w "nav: page gba ready" -k @8:B -k @12:- \
  -w "nav: home ready" -k @8:UP -k @12:- -k @20:A -k @24:- -w "nav: page recent ready" -s "@8:$cart/recent.png" \
  -u "games: row 0 played 1m ago" -f 6000
grep -q '^kernel: games: row 0 played just now' "$cart/2.log" ||
  fail "Recent's first row isn't Hello, just now: $(grep '^kernel: games' "$cart/2.log" | head -3)"
grep -q '^kernel: games: row 0 played 1m ago' "$cart/2.log" ||
  fail "the played text didn't follow the clock: $(grep '^kernel: games' "$cart/2.log" | tail -3)"
echo "PASS: a launched game is first on Recent, played just now, and the text follows the clock"

# A launch that fails (a name too long for its save record) brings the page
# back on the same row, without "No games here" while the index loads again
cart=$carts/games-fail
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n GAMES "$cart/sd.img" 65536 >/dev/null
long=$(printf 'L%.0s' $(seq 249)).gba
for g in A.gba B.gba "$long"; do mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$g"; done
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_game gba 2) -k @30:A -k @34:- -w "screen ready: dialog" -k @20:B -k @24:- \
  -w "games: a new index" -k @20:START -k @24:- -w "screen ready: game-popup" -f 2400
grep -q '^kernel: screen ready: game-popup LLL' "$cart/1.log" ||
  fail "after the failed launch the page isn't on the same row: $(grep '^kernel: screen ready: game-popup' "$cart/1.log")"
echo "PASS: a failed launch brings the page back on the same row"

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
