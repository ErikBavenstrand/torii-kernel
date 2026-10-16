#!/usr/bin/env bash
# Smoke test on the emulated cart: boot the kernel, launch the hello test ROM
# from SD, power-cycle so the kernel writes the save back to SD, and launch it
# again to check the save came back. Runs inside the torii-emu container
# after `just build` and `just emu-headless`.
set -euo pipefail

cart=dist/cart-smoke
shots=dist/shots/smoke
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n SMOKE "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null
}

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

save_bytes() {
  mtype -i "$cart/sd.img" ::/SAVER/Hello.sav 2>/dev/null | head -c 5 | od -A n -t x1 | tr -d ' \n'
}

# Power-on 1: Hello.gba is the only entry. Open it, then pick CLEAN BOOT.
boot -k 100:A -k 106:- -k 160:A -k 166:- -s 400:$shots/1-game.png
od -A n -t x1 -N 5 "$cart/sram.bin" | tr -d ' \n' | grep -q '4f4d525801' ||
  fail "game did not write its first boot to SRAM"

# Power-on 2: the kernel asks to copy SRAM to SAVER/Hello.sav; confirm with A.
boot -s 90:$shots/2-prompt.png -k 100:A -k 106:- -s 300:$shots/2-menu.png
[ "$(save_bytes)" = 4f4d525801 ] || fail "save not written back to SD (got $(save_bytes))"

# Power-on 3: SAVER sorts first, so move down to the game and launch it.
boot -k 100:DOWN -k 106:- -k 120:A -k 126:- -k 180:A -k 186:- -s 500:$shots/3-game.png
od -A n -t x1 -N 5 "$cart/sram.bin" | tr -d ' \n' | grep -q '4f4d525802' ||
  fail "save was not loaded back into SRAM on the next launch"

echo "PASS: boot from SD, SRAM save, write-back to SD and reload all work"
echo "screenshots in $shots"
