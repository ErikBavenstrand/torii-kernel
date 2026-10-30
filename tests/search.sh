#!/usr/bin/env bash
# The in-game cheat search (v0.9, docs/design/v0.9/cheat-search.md) on the
# emulated cart with tests/roms/searchtest, whose lives (5) go down by one
# on each press of A, end to end:
#
#   Start an 8-bit search, Equals 5; back in the game A loses a life; in
#   the search again, Equals 4 leaves one candidate, the lives' byte. Show
#   them lists it; its page keeps it with the value 9, on now.
#
# live: the game has a cheat file (one picked cheat), so the launch puts
#       the interpreter in, with the live block: the lives read 9 from the
#       next frame and stay 9 however often A is pressed.
# set:  no cheat file, no interpreter: "set it now" writes 9 once, and the
#       next A takes the lives to 8.
#
# Both then go Back to Torii, whose next boot files the found cheat into
# the game's cheat file as "Found ADDRESS" with ADDRESS:09, picked. Each
# pass's time is logged ("search: pass in N frames").
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

mode=${1:?usage: search.sh live|set}
local_carts "dist/cart-search-$mode"

fail() {
  echo "FAIL: search $mode: $*" >&2
  exit 1
}

cart=$carts/se
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n SEARCH "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/searchtest/searchtest.gba ::/searchtest.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
if [ "$mode" = live ]; then
  cht=$carts/searchtest.cht
  printf '%s\n' 'cheats = 1' 'cheat0_desc = "Marker"' 'cheat0_code = "0203F000:01"' 'cheat0_enable = true' >"$cht"
  mcopy -i "$cart/sd.img" "$cht" ::/torii/cheats/searchtest.cht
fi
# Settings slots (source/ez_define.h), as tests/cheats_ingame.sh
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

# The options, in order
args=()
# keys FRAME KEY...: press/release pairs from @FRAME, one key every 8 frames
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    args+=(-k "@$frame:$key" -k "@$((frame + 4)):-")
    frame=$((frame + 8))
  done
}
wait_for() { args+=(-w "$1"); }

# The menu to the search page: with a cheat list its row is on the list's
# page (UP from the first cheat), else on the menu (past Save state, Load
# state greyed)
to_search() {
  wait_for "screen ready: ingame"
  keys 20 DOWN DOWN A
  if [ "$mode" = live ]; then
    wait_for "screen ready: ingame-cheats"
    keys 10 UP A
  fi
  wait_for "screen ready: ingame-search"
}

# From the search page back to the game: B to the cheat list, then the
# menu, then the game (no list: the menu, then the game)
back_out() {
  if [ "$mode" = live ]; then keys "$1" B B B; else keys "$1" B B; fi
}

# shellcheck disable=SC2207 # nav_launch prints options
args=($(nav_launch /searchtest.gba addon))
wait_for "launch: starting the game"
args+=(-k @300:L+R+START -k @306:-)
to_search
keys 10 A
wait_for "search: started"
wait_for "screen ready: ingame-search"
keys 10 DOWN DOWN DOWN DOWN A UP UP UP UP UP A
wait_for "search: pass in"
wait_for " left"
back_out 10
keys 80 A
args+=(-k @200:L+R+START -k @206:-)
to_search
keys 10 DOWN DOWN DOWN DOWN A DOWN A
wait_for "search: pass in"
wait_for " left"
keys 10 DOWN A
wait_for "screen ready: ingame-search-list"
keys 10 A
wait_for "screen ready: ingame-search-pick"
keys 10 UP A UP UP UP UP UP A DOWN DOWN A
wait_for "search: kept a cheat"
keys 10 B B B
back_out 34
keys 120 A
keys 160 A
args+=(-k @260:L+R+START -k @266:-)
wait_for "screen ready: ingame"
keys 20 DOWN DOWN DOWN DOWN DOWN DOWN A
wait_for "found in game"
file=$(mktemp)
boot "${args[@]}" -f 8000 >/dev/null 2>"$cart/log" || true
log=$(cat "$cart/log")
grep -q '^crash at frame' <<<"$log" && fail "crashed"
echo "$log" | grep -E '^kernel: search: pass in' | sed 's/^kernel: /  /'
grep -q 'search: started' <<<"$log" || fail "the search didn't start: $(grep -E 'search|screen ready' <<<"$log" | tail -5)"
# The candidates: after Equals 4, one
lefts=$(grep -E '^kernel: search: [0-9]+ left' <<<"$log" | awk '{ print $3 }' | tr '\n' ' ')
[ "$(awk '{ print $NF }' <<<"$lefts")" = 1 ] || fail "candidates left after each pass: $lefts"
grep -q 'search: kept a cheat, on' <<<"$log" || fail "nothing kept: $(grep -E 'search' <<<"$log" | tail -3)"
# The lives after the keep: 9, then with two presses of A, held at 9 (live)
# or 8 and 7 (set once)
after=$(sed -n '/search: kept a cheat/,/screen ready: ingame$/p' <<<"$log" | sed -n 's/^kernel: searchtest: lives //p' | uniq | tr '\n' ' ')
case $mode in
live) [ "$after" = "9 " ] || fail "the lives after keeping 9 on now: $after" ;;
set) [ "$after" = "9 8 7 " ] || fail "the lives after setting 9 once: $after" ;;
esac
# Back to Torii filed it
addr=$(sed -n 's/^kernel: cheats: .*found in game, \([0-9A-F]*\):09 on: added$/\1/p' <<<"$log")
[ -n "$addr" ] || fail "not filed: $(grep -E 'cheats:' <<<"$log" | tail -3)"
mtype -i "$cart/sd.img" ::/torii/cheats/searchtest.cht | tr -d '\r' >"$file"
grep -q "^cheat[0-9]*_desc = \"Found $addr\"" "$file" || fail "its name: $(cat "$file")"
n=$(sed -n "s/^cheat\([0-9]*\)_desc = \"Found $addr\"$/\1/p" "$file")
grep -q "^cheat${n}_code = \"$addr:09\"" "$file" || fail "its code: $(cat "$file")"
grep -q "^cheat${n}_enable = true" "$file" || fail "not picked: $(cat "$file")"
rm -f "$file"
echo "PASS: search $mode: lives at $addr found, kept as $addr:09 (lives then: $after)"
