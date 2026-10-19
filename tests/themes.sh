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

# Copies the entries of folder $2 (not dot files) into folder $3 of image $1,
# in sorted order, as copy_tree in tests/shots.py does: `mcopy -s` walks
# folders in the host's directory order, which differs between filesystems
# (APFS, ext4), and so would the card's directory entries.
copy_tree() {
  local img=$1 src=$2 dest=$3 entry
  local files=()
  while IFS= read -r -d '' entry; do
    if [ -d "$entry" ]; then
      if [ ${#files[@]} -gt 0 ]; then # one mcopy for a run of files, in order
        mcopy -Q -i "$img" "${files[@]}" "$dest/"
        files=()
      fi
      mmd -i "$img" "$dest/${entry##*/}"
      copy_tree "$img" "$entry" "$dest/${entry##*/}"
    else
      files+=("$entry")
    fi
  done < <(find "$src" -mindepth 1 -maxdepth 1 ! -name '.*' -print0 | LC_ALL=C sort -z)
  if [ ${#files[@]} -gt 0 ]; then
    mcopy -Q -i "$img" "${files[@]}" "$dest/"
  fi
}

# A fresh cart with THEME = 2 and theme.txt holding $1; more mgba-shot options follow
boot() {
  local name=$1
  shift
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n THEMES "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/torii ::/torii/themes ::/torii/themes/example
  copy_tree "$cart/sd.img" themes/example ::/torii/themes/example
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
# and nothing set, THEME (DOWN x5), A to edit, A for LIGHT, A for the
# example (loaded as it is picked), RIGHT and A on OK save it: theme.txt
# names it, slot 28 holds 2 once SETTINGS is left (L, L), and the next
# power-on loads it
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n THEMES "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/torii ::/torii/themes ::/torii/themes/example
copy_tree "$cart/sd.img" themes/example ::/torii/themes/example
keys="-k @8:R -k @12:-"
# shellcheck disable=SC2086 # $keys is a list of options
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: sd-list" $keys \
  -w "screen ready: nor-list" $keys -w "screen ready: settings1" $keys -w "screen ready: settings2" \
  -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:DOWN -k @48:- -k @56:DOWN -k @60:- -k @68:A -k @72:- -k @80:A -k @84:- \
  -k @92:A -k @96:- -k @104:RIGHT -k @108:- -k @116:A -k @120:- -w "settings: THEME" \
  -s @10:dist/cart-themes/settings.png -k @20:L -k @24:- -w "screen ready: settings1" -k @8:L -k @12:- \
  -w "screen ready: nor-list" 2>&1 || true)
grep -q "kernel: settings: THEME Example" <<<"$out" ||
  fail "SETTINGS didn't save the example theme: $(grep -E 'settings|theme' <<<"$out" | head -3)"
grep -q "kernel error:" <<<"$out" && fail "errors picking the example: $(grep 'kernel error' <<<"$out" | head -3)"
slot=$(od -An -tu2 -j $((0x7B0000 + 2 * 28)) -N 2 "$cart/kernel-nor.bin" | tr -d ' ')
[ "$slot" = 2 ] || fail "THEME slot holds $slot, not 2"
mtype -i "$cart/sd.img" ::/torii/theme.txt | grep -q '^example' || fail "theme.txt doesn't name the example"
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: sd-list" 2>&1 || true)
grep -q "kernel: theme: loaded example" <<<"$out" || fail "the saved SD theme didn't load at the next power-on"
echo "PASS: SETTINGS picks and saves the SD theme (theme.txt, THEME = 2), loaded at the next power-on"

# An SD theme picked after WRITE TO NOR (v0.3.2 on the real cart): the write
# leaves the game's last bytes in pReadCache, where the theme's SD reader
# lives, and the load must not call what they hold. Hello.gba padded to
# 1 MB with 0xFF, written CLEAN from the SD list's file menu, then THEME
# stepped to the example (A on DARK, LIGHT, the example).
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n THEMES "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/torii ::/torii/themes ::/torii/themes/example
copy_tree "$cart/sd.img" themes/example ::/torii/themes/example
python3 - tests/roms/hello/hello.gba "$cart/Big.gba" <<'EOF2'
import sys
rom = open(sys.argv[1], "rb").read()
open(sys.argv[2], "wb").write(rom + b"\xff" * (0x100000 - len(rom)))
EOF2
mcopy -i "$cart/sd.img" "$cart/Big.gba" ::/Big.gba
# shellcheck disable=SC2086 # $keys is a list of options
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: sd-list" \
  -k @8:DOWN -k @12:- -k @20:A -k @24:- -w "screen ready: file-menu" \
  -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:A -k @36:- -w "nor: list saved" \
  -w "screen ready: nor-list" $keys -w "screen ready: settings1" $keys -w "screen ready: settings2" \
  -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:DOWN -k @48:- -k @56:DOWN -k @60:- \
  -k @68:A -k @72:- -k @80:A -k @84:- -k @92:A -k @96:- -w "theme: loaded example" 2>&1 || true)
grep -q "^crash at frame" <<<"$out" && fail "the theme load after WRITE TO NOR crashed: $(grep -A2 '^crash at' <<<"$out")"
grep -q "kernel: nor: wrote Big.gba" <<<"$out" || fail "WRITE TO NOR didn't write Big.gba: $(grep 'nor:' <<<"$out" | tail -3)"
grep -q "kernel: theme: loaded example" <<<"$out" ||
  fail "THEME didn't load the example after WRITE TO NOR: $(grep -E 'theme|settings' <<<"$out" | tail -3)"
echo "PASS: an SD theme picked after WRITE TO NOR loads (the reader in pReadCache starts clean)"

# UP held on THEME (tests/shots/settings-theme-held.opts): 27 steps through
# DARK, LIGHT, the example and wall128 load no theme while the key is down;
# the example it stops on loads once, after it is let go
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n THEMES "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/torii ::/torii/themes ::/torii/themes/example ::/torii/themes/wall128
copy_tree "$cart/sd.img" themes/example ::/torii/themes/example
copy_tree "$cart/sd.img" tests/themes/wall128 ::/torii/themes/wall128
printf 'wall128\n' >"$cart/theme.txt"
mcopy -Q -i "$cart/sd.img" "$cart/theme.txt" ::/torii/theme.txt
python3 - "$cart/kernel-nor.bin" <<'EOF2'
import struct, sys
nor = bytearray(b"\xff" * (8 << 20))
nor[0x7B0000 + 2 * 28 : 0x7B0000 + 2 * 29] = struct.pack("<H", 2)
open(sys.argv[1], "wb").write(nor)
EOF2
# shellcheck disable=SC2086 # $keys is a list of options
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: sd-list" $keys \
  -w "screen ready: nor-list" $keys -w "screen ready: settings1" $keys -w "screen ready: settings2" \
  -k @8:DOWN -k @11:- -k @16:DOWN -k @19:- -k @24:DOWN -k @27:- -k @32:DOWN -k @35:- -k @40:DOWN -k @43:- \
  -k @48:A -k @51:- -k @70:UP -k @151:- -w "theme: loaded example" -k @20:RIGHT -k @24:- -k @32:A -k @36:- \
  -w "settings: THEME" 2>&1 || true)
loads=$(sed -n '/screen ready: settings2/,$p' <<<"$out" | grep -c "kernel: theme: loaded" || true)
[ "$loads" = 1 ] || fail "UP held on THEME loaded $loads themes, not 1: $(grep 'theme: loaded' <<<"$out" | tail -3)"
grep -q "kernel: settings: THEME Example" <<<"$out" ||
  fail "THEME didn't stop on the example: $(grep -E 'settings:' <<<"$out" | tail -3)"
echo "PASS: UP held on THEME steps without loading; the SD theme it stops on loads once"
