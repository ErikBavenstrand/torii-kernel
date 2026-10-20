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

# Settings page (R twice), down to LANGUAGE, A to edit, A to save English,
# L to leave SETTINGS, which writes the NOR as it closes.
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  -k 100:R -k 106:- -k 120:R -k 126:- -k 170:DOWN -k 176:- -k 185:DOWN -k 191:- \
  -k 200:A -k 206:- -k 240:A -k 246:- -k 270:L -k 276:- -f 360 >/dev/null 2>&1
[ "$(slots 0 1)" = e1e1 ] || fail "language not saved: $(slots 0 1)"
# The hotkeys are saved as the defaults the page shows: L R SELECT, L R START.
[ "$(slots 5 6)" = "0009 0008 0002 0009 0008 0003" ] || fail "hotkeys: $(slots 5 6)"
[ "$(slots 14 11)" = "$(ffff 11)" ] || fail "slots 14-24 changed: $(slots 14 11)"
[ "$(slots 26 4)" = "$(ffff 4)" ] || fail "slots 26-29 changed: $(slots 26 4)"
[ "$(slots 30 4)" = "0000 0001 0004 0001" ] || fail "the save lost slots 30-33: $(slots 30 4)"
[ "$(slots 34 31)" = "$(ffff 31)" ] || fail "slots 34-64 changed: $(slots 34 31)"
[ "$(slots 65 1)" = 0000 ] || fail "slot 65: $(slots 65 1)"
booted "24-hour clock Off, game view List + art, start on SD card, show all files On"

echo "PASS: unwritten settings read 0xFFFF, the v0.4 ones as their defaults, and saving one keeps the slots the kernel doesn't know"
