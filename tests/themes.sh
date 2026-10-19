#!/usr/bin/env bash
# SD themes at boot on the emulated cart (source/ui/sdtheme.h): with THEME
# set to the SD theme (settings slot 28 = 2), the kernel loads the theme
# /torii/theme.txt names, here themes/example; one that doesn't load is
# logged and shown on the boot screen, and the menu still comes up.
# The log shows the theme; boot.png keeps the boot screen's error notice.
# Runs inside the torii-emu container.
set -euo pipefail

cart=dist/cart-themes
fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A fresh cart with THEME = 2 and theme.txt holding $1; more mgba-shot options follow
boot() {
  local name=$1
  shift
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n THEMES "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/torii ::/torii/themes
  mcopy -s -Q -i "$cart/sd.img" themes/example ::/torii/themes/
  printf '%s\n' "$name" >"$cart/theme.txt"
  mcopy -Q -i "$cart/sd.img" "$cart/theme.txt" ::/torii/theme.txt
  python3 - "$cart/kernel-nor.bin" <<'EOF'
import struct, sys
nor = bytearray(b"\xff" * (8 << 20))
nor[0x7B0000 + 2 * 28 : 0x7B0000 + 2 * 29] = struct.pack("<H", 2)
open(sys.argv[1], "wb").write(nor)
EOF
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" -w "screen ready: sd-list" 2>&1 || true
}

out=$(boot example)
grep -q "kernel: theme: loaded example (Example, 0 warnings, a wallpaper)" <<<"$out" ||
  fail "example theme not loaded: $(grep theme <<<"$out" | head -3)"
grep -q "kernel error:" <<<"$out" && fail "errors loading the example: $(grep 'kernel error' <<<"$out" | head -3)"
grep -q "screen ready: sd-list" <<<"$out" || fail "no menu after loading the example"

# The boot screen saying why is kept in dist/cart-themes/boot.png
out=$(boot missing -w "has no theme.ini" -s @20:dist/cart-themes/boot.png)
grep -q "kernel error: theme: missing has no theme.ini" <<<"$out" ||
  fail "a missing theme isn't reported: $(grep theme <<<"$out" | head -3)"
grep -q "screen ready: sd-list" <<<"$out" || fail "no menu after a theme that didn't load"

echo "PASS: SD theme loaded at boot; one that doesn't load is reported and the menu still starts"

# SETTINGS page 2's THEME picks the SD theme: on a card with themes/example
# and nothing set, THEME (DOWN x3), A to edit, A for LIGHT, A for the
# example (loaded as it is picked), RIGHT and A on OK save it: theme.txt
# names it, slot 28 holds 2, and the next power-on loads it
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n THEMES "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/torii ::/torii/themes
mcopy -s -Q -i "$cart/sd.img" themes/example ::/torii/themes/
keys="-k @8:R -k @12:-"
# shellcheck disable=SC2086 # $keys is a list of options
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: sd-list" $keys \
  -w "screen ready: nor-list" $keys -w "screen ready: settings1" $keys -w "screen ready: settings2" \
  -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:A -k @48:- -k @56:A -k @60:- \
  -k @68:A -k @72:- -k @80:RIGHT -k @84:- -k @92:A -k @96:- -w "settings: THEME" \
  -s @10:dist/cart-themes/settings.png 2>&1 || true)
grep -q "kernel: settings: THEME Example" <<<"$out" ||
  fail "SETTINGS didn't save the example theme: $(grep -E 'settings|theme' <<<"$out" | head -3)"
grep -q "kernel error:" <<<"$out" && fail "errors picking the example: $(grep 'kernel error' <<<"$out" | head -3)"
slot=$(od -An -tu2 -j $((0x7B0000 + 2 * 28)) -N 2 "$cart/kernel-nor.bin" | tr -d ' ')
[ "$slot" = 2 ] || fail "THEME slot holds $slot, not 2"
mtype -i "$cart/sd.img" ::/torii/theme.txt | grep -q '^example' || fail "theme.txt doesn't name the example"
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: sd-list" 2>&1 || true)
grep -q "kernel: theme: loaded example" <<<"$out" || fail "the saved SD theme didn't load at the next power-on"
echo "PASS: SETTINGS picks and saves the SD theme (theme.txt, THEME = 2), loaded at the next power-on"
