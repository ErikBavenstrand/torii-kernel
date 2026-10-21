#!/usr/bin/env bash
# Settings sector test on the emulated cart. A new cart's kernel NOR is
# erased flash, so settings the kernel never wrote read 0xFFFF, as on a real
# cart, and the kernel falls back to its defaults. Saving one setting
# rewrites the whole 512-byte sector: slots it doesn't know stay 0xFFFF,
# and slots from 65 (assress_max) on become 0 (the rest of an EWRAM buffer).
# The v0.4 slots (source/settings_model.h) read as their defaults until set.
# Runs inside the torii-emu container.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-settings${1:+-$1}"

cart=$carts/settings
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n SETTINGS "$cart/sd.img" 65536 >/dev/null

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Halfwords from settings slot $1, $2 of them, as hex.
slots() {
  od -A n -t x2 -v -j $((0x7B0000 + 2 * $1)) -N $((2 * $2)) "$cart/kernel-nor.bin" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'
}
ffff() {
  printf 'ffff%.0s ' $(seq "$1") | sed 's/ $//'
}

# write SLOT=VALUE...: settings slots written straight into the kernel NOR
write() {
  local slot value
  for slot in "$@"; do
    value=${slot#*=}
    printf %b "\\x$(printf %02x $((value & 0xFF)))\\x$(printf %02x $((value >> 8)))" |
      dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * ${slot%=*})) conv=notrunc status=none
  done
}
# booted TEXT: a power-on logs the v0.4 settings as TEXT (settings_log)
booted() {
  local out
  out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -u "settings: 24-hour" -f 200 2>&1) || true
  grep -qF "settings: $1" <<<"$out" || fail "the boot read $(grep -o 'settings: 24-hour.*' <<<"$out" || echo nothing), not: $1"
}

OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -f 200 >/dev/null 2>&1
[ "$(slots 0 256)" = "$(ffff 256)" ] || fail "a new cart's settings are not erased: $(slots 0 16)"

# The v0.4 slots (30-33): erased, and values they can't hold, read as the
# defaults; values they hold as they are
defaults="24-hour clock On, game view List, start on Last place, show all files Off"
booted "$defaults"
write 30=7 31=2 32=9 33=2
booted "$defaults"
write 30=0 31=1 32=4 33=1
booted "24-hour clock Off, game view List + art, start on SD card, show all files On"
[ "$(slots 30 4)" = "0000 0001 0004 0001" ] || fail "a boot changed slots 30-33: $(slots 30 4)"
# The visits below start from home (Start on SD card opens its page)
write 32=2

# visit ROW KEYS...: power on, go to the Settings page's ROW, press KEYS
# there, then B, which leaves the page (writing the NOR once, if a value
# changed); the run's log goes to $cart/visit.log
visit() {
  local row=$1
  shift
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_setting "$row") $(nav_press "$@" B) \
    -w "nav: home ready" >"$cart/visit.log" 2>&1 || fail "the visit to $row didn't get back home: $(tail -3 "$cart/visit.log")"
}
writes() { grep -c 'nor: wrote the settings' "$cart/visit.log" || true; }

# Saving one setting on the Settings page (Auto save, A RIGHT A) writes the
# whole sector once as the page closes, keeping the slots the kernel
# doesn't know, the hidden LANGUAGE and the hotkeys it didn't touch
visit auto-save A RIGHT A
[ "$(writes)" = 1 ] || fail "a save wrote the settings $(writes) times"
[ "$(slots 14 1)" = 0001 ] || fail "Auto save not saved: $(slots 14 1)"
[ "$(slots 0 14)" = "$(ffff 14)" ] || fail "slots 0-13 changed: $(slots 0 14)"
[ "$(slots 15 11)" = "$(ffff 11)" ] || fail "slots 15-25 changed: $(slots 15 11)"
[ "$(slots 26 4)" = "$(ffff 4)" ] || fail "slots 26-29 changed: $(slots 26 4)"
[ "$(slots 30 4)" = "0000 0001 0002 0001" ] || fail "the save lost slots 30-33: $(slots 30 4)"
[ "$(slots 34 31)" = "$(ffff 31)" ] || fail "slots 34-64 changed: $(slots 34 31)"
[ "$(slots 65 1)" = 0000 ] || fail "slot 65: $(slots 65 1)"
booted "24-hour clock Off, game view List + art, start on Home, show all files On"

# One write a visit: two values changed (Game view, Dim screen), then a
# visit that changes nothing, and one that changes a value and back
visit game-view A RIGHT A DOWN DOWN DOWN A RIGHT A
[ "$(writes)" = 1 ] || fail "two changes wrote the settings $(writes) times"
[ "$(slots 31 1)" = 0000 ] || fail "Game view not saved: $(slots 31 1)"
[ "$(slots 27 1)" = 0001 ] || fail "Dim screen not saved: $(slots 27 1)"
visit theme
[ "$(writes)" = 0 ] || fail "a visit that changed nothing wrote the settings"
visit auto-save A RIGHT A A LEFT A
[ "$(writes)" = 0 ] || fail "a change and back wrote the settings"
grep -q 'settings: nothing changed, not written' "$cart/visit.log" || fail "the change and back wasn't staged"
[ "$(slots 14 1)" = 0001 ] || fail "Auto save changed: $(slots 14 1)"
# B on an edit puts the value back: nothing to write
visit auto-save A RIGHT B
[ "$(writes)" = 0 ] || fail "an edit B cancelled wrote the settings"
# ... the value from before that A, not the one the page opened with: Dim
# screen kept at 2 min, then edited again and cancelled
visit dim A RIGHT A A RIGHT B
[ "$(slots 27 1)" = 0002 ] || fail "B put back the page's first value, not the edit's: $(slots 27 1)"

echo "PASS: unwritten settings read 0xFFFF, the v0.4 ones as their defaults, and the Settings page writes once a visit, only a change, keeping the slots it doesn't know"
