#!/usr/bin/env bash
# The in-game menu's Screenshot (v0.8 P4) on the emulated cart, with the
# hello ROM:
#
#   shot  The menu's Screenshot row packs the screen into cart SRAM (the
#         screen white meanwhile) and says it is kept on the way back,
#         its row the count; five times, then a sixth says the menu is
#         full. Back to Torii, and the next boot writes the five as
#         /torii/screenshots/Game <date> <time>.bmp (-2 and on in one
#         second): 240x160 BMPs of hello's screen as the game showed it
#         under the menu, its dimming left out (BLDY is write-only). A
#         second boot writes no more files.
#   cut   The boot's writing cut at each cutpoint (tests/cutsweep.sh): the
#         cut loses the dump, and the next power-on leaves one whole BMP or
#         none (the journal's broken file deleted), never a broken one.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh

mode=${1:?usage: screenshot.sh shot|cut}
local_carts "dist/cart-screenshot-$mode"

fail() {
  echo "FAIL: screenshot $mode: $*" >&2
  exit 1
}

cart=$carts/shot
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n SHOTS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Game.gba
# Settings slots (source/ez_define.h), as tests/states.sh: the menu key
# L+R+START, Save states on; the save copied at power-on without asking
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1

export TZ=UTC
clock=1793109780 # 27 Oct 2026 14:03 UTC

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -c $clock "$@"; }

# keys FRAME KEY...: -k press/release pairs from @FRAME, one key every 8 frames
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k @%d:%s -k @%d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# The menu at @400 from the game's start: Resume, Save state, Load state
# (greyed: no state), Cheat search, Screenshot; then the rest of TAIL's keys
menu_shot() {
  # shellcheck disable=SC2046 # option lists
  boot $(nav_launch /Game.gba addon) -w "launch: starting the game" \
    -k @400:L+R+START -k @406:- $(keys 440 DOWN DOWN DOWN A) -w "ingame: screenshot dumped" "$@"
}

shots() { mdir -b -i "$cart/sd.img" ::/torii/screenshots 2>/dev/null | grep -c '\.bmp$' || true; }

# check_bmp: the one BMP's size, head and pixels: hello's blue undimmed,
# its white frame
check_bmp() {
  local file
  file=$(mdir -b -i "$cart/sd.img" ::/torii/screenshots | grep '\.bmp$' | head -1)
  mcopy -o -i "$cart/sd.img" "$file" "$cart/shot.bmp"
  python3 - "$cart/shot.bmp" <<'PY' || fail "the BMP isn't hello's screen"
import struct, sys
d = open(sys.argv[1], "rb").read()
assert len(d) == 54 + 240 * 160 * 3, len(d)
assert d[:2] == b"BM" and struct.unpack_from("<ii", d, 18) == (240, -160), d[:30]
px = lambda x, y: tuple(d[54 + (y * 240 + x) * 3 : 57 + (y * 240 + x) * 3][::-1])
assert px(120, 80) == (16, 49, 165), px(120, 80)
assert px(0, 0) == (255, 255, 255) and px(239, 159) == (255, 255, 255), (px(0, 0), px(239, 159))
PY
  echo "$file"
}

case $mode in
shot)
  # shellcheck disable=SC2046 # option lists
  # Four more A, 40 frames apart (a press while one is packed is lost), a
  # sixth that finds the menu full, then Back to Torii
  more=()
  for i in 1 2 3 4 5; do
    more+=(-k @$((i * 40)):A -k @$((i * 40 + 4)):-)
  done
  # shellcheck disable=SC2046 # option lists
  menu_shot -s @20:"$cart/menu.png" "${more[@]}" -s @$((5 * 40 + 20)):"$cart/full.png" \
    $(keys $((6 * 40)) DOWN DOWN DOWN A) -w "in-game: back from /Game.gba" \
    -f 12000 -u "home ready" >/dev/null 2>"$cart/log" || fail "the run failed: $(tail -3 "$cart/log")"
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed"
  [ "$(grep -c 'ingame: screenshot dumped' <<<"$log")" = 5 ] || fail "$(grep -c 'ingame: screenshot dumped' <<<"$log") screenshots taken, not 5"
  [ "$(grep -c 'kernel: screenshot: /torii/screenshots/Game 2026-10-27 14-0[0-9]-[0-9][0-9]\(-[2-5]\)\?.bmp written' <<<"$log")" = 5 ] ||
    fail "not 5 BMPs written: $(grep -E 'screenshot|in-game' <<<"$log" | tail -3)"
  grep -q 'kernel: screenshot: 5 filed' <<<"$log" || fail "the list wasn't cleared"
  # Each named by the clock as it was taken (the game has the clock), not
  # at filing: the first shot's second is before the filing's
  [ "$(grep -c 'kernel: screenshot: named by the clock in game' <<<"$log")" = 5 ] ||
    fail "not every screenshot named by the clock in game: $(grep -E 'screenshot|clock' <<<"$log" | head -5)"
  frames=$(sed -n 's/^kernel: screenshot: .* written in \([0-9]* frames.*\)$/\1/p' <<<"$log" | head -1)
  file=$(check_bmp)
  [ "$(shots)" = 5 ] || fail "$(shots) files, not 5"
  # Booted again: nothing more to write
  boot -f 600 >/dev/null 2>"$cart/log"
  grep -q 'screenshot:' "$cart/log" && fail "the next boot wrote them again"
  [ "$(shots)" = 5 ] || fail "$(shots) files after the next boot"
  echo "PASS: screenshot shot: ${file##*/}, drawn and written at boot in $frames"
  ;;
cut)
  # The game leaves the dump in cart SRAM; the boot after it (a soft reset,
  # as Back to Torii's, which keeps cart SRAM) files it, cut at each
  # cutpoint. A cut is a power-off: the dump goes with it, and the next
  # power-on must leave one whole BMP or none, and no journal
  # shellcheck disable=SC2046 # option lists
  menu_shot $(keys 20 B) -h @80 >/dev/null 2>"$cart/log" || true
  grep -q 'ingame: screenshot dumped' "$cart/log" || fail "no screenshot taken"
  scenario() { OMEGA_SOFT_RESET=1 cut_run -c $clock -f 600 "$@"; }
  check() {
    local n=$1 name=$2 count
    cut_run -c $clock -f 600
    grep -q '^crash at frame' "$cart/run.log" && fail "cut at $n ($name): the next boot crashed"
    count=$(shots)
    [ "$count" -le 1 ] || fail "cut at $n ($name): $count BMPs after the next boot"
    [ "$n" != 0 ] || [ "$count" = 1 ] || fail "uncut: no BMP"
    [ "$count" = 0 ] || check_bmp >/dev/null
    ! mdir -a -b -i "$cart/sd.img" ::/torii/screenshots 2>/dev/null | grep '\.writing' >/dev/null ||
      fail "cut at $n ($name): the journal is left"
    case $name in
    "screenshot: its head written and closed" | "screenshot: its journal deleted" | "screenshot: its record marked filed")
      [ "$count" = 1 ] || fail "cut at $n ($name): the whole BMP went"
      ;;
    esac
  }
  cut_sweep scenario check
  echo "PASS: screenshot cut: one whole BMP or none after a cut at each of the boot's $cut_count cutpoints"
  ;;
esac
