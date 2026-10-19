#!/usr/bin/env bash
# SD themes at boot on the emulated cart (source/ui/sdtheme.h): with THEME
# set to the SD theme (settings slot 28 = 2), the kernel loads the theme
# /torii/theme.txt names, here themes/example; one that doesn't load is
# logged and shown on the boot screen, and the menu still comes up.
# The menu is still mode 3, so only the log shows the theme. Runs inside the
# torii-emu container.
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
