#!/usr/bin/env bash
# The in-game menu's keys (v0.8 P5), end to end on the emulated cart with
# the hello ROM, Settings set as Settings, Hotkeys stores them:
#
#   every  In-game menu: Every game, Save states off, no cheats, the menu
#          keys L+R+B: L+R+B opens the menu (the hook goes in for it alone),
#          the session block hands the overlay those keys for its footer
#          ("L+R+B or B: resume"), and B gives the game back.
#   off    In-game menu: Off, Save states on and a cheat picked: L+R+START
#          opens nothing, and the cheat still runs every frame.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

mode=${1:?usage: menu_keys.sh every|off}
local_carts "dist/cart-menu-keys-$mode"

fail() {
  echo "FAIL: menu_keys $mode: $*" >&2
  exit 1
}

cart=$carts/keys
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n KEYS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

# Settings slots (source/ez_define.h): 1 In-game menu (0 Auto, 1 Every game,
# 2 Off), 2 Save states, 5-7 the sleep keys, 8-10 the menu keys (KEYINPUT
# bits: 9 L, 8 R, 3 START, 1 B); the save copied at power-on without asking
case $mode in
every)
  python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=0 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=1 11=1 12=0 13=1 14=1 25=1
  # shellcheck disable=SC2046 # option lists
  boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" \
    -k @300:L+R+B -k @306:- -w "screen ready: ingame" -s @8:"$cart/menu.png" -k @30:B -k @34:- -h @80 \
    >/dev/null 2>"$cart/log" || fail "the menu didn't open on L+R+B: $(grep -E 'launch: in-game|ingame' "$cart/log" | tail -3)"
  grep -q '^crash at frame' "$cart/log" && fail "crashed"
  [ -z "${MENU_KEYS_PNG:-}" ] || cp "$cart/menu.png" "$MENU_KEYS_PNG" # to look at the footer
  grep -q 'kernel: launch: in-game menu keys L+R+B$' "$cart/log" || fail "the launch didn't take L+R+B"
  # The keys the overlay's footer names: the session block's menu_key
  python3 - "$cart/sram.bin" <<'PY' || fail "the session block's menu keys aren't L+R+B"
import struct, sys
sram = open(sys.argv[1], "rb").read()
session = 0xB6000
assert struct.unpack_from("<I", sram, session)[0] == 0x31535354
menu_key = struct.unpack_from("<H", sram, session + 0x60)[0]
assert menu_key == (1 << 9) | (1 << 8) | (1 << 1), hex(menu_key)
PY
  echo "PASS: menu_keys every: In-game menu Every game opens on the keys Settings set (L+R+B) with no save states or cheats, and names them in its footer"
  ;;
off)
  python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=2 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1
  printf 'cheats = 1\n\ncheat0_desc = "Green"\ncheat0_code = "02001236:58"\ncheat0_enable = true\n' >"$cart/Hello.cht"
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  mcopy -i "$cart/sd.img" "$cart/Hello.cht" ::/torii/cheats/Hello.cht
  # shellcheck disable=SC2046 # option lists
  out=$(boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" \
    -k @300:L+R+START -k @330:- -m @400:0x02001236:1 -f 9000 2>"$cart/log") || fail "the run failed"
  grep -q 'kernel: launch: in-game menu keys L+R+START, off (In-game menu Off)$' "$cart/log" ||
    fail "the launch didn't turn the menu keys off: $(grep 'launch: in-game' "$cart/log")"
  grep -q 'screen ready: ingame' "$cart/log" && fail "the menu opened"
  [ "$(awk '$1 == "mem" { print $5 }' <<<"$out")" = 58 ] || fail "the cheat didn't run: $(grep '^mem' <<<"$out")"
  echo "PASS: menu_keys off: In-game menu Off opens nothing on the menu keys, and the game's cheat still runs"
  ;;
esac
