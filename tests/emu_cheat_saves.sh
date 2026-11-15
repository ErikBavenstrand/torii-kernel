#!/usr/bin/env bash
# Cheats leave a GB or NES game's battery save as the game writes it: the save
# games of tests/gb_save.sh (gb_rom.py, 32 KB and 8 KB) and tests/nes_save.sh
# (nes_rom.py save) run twice on fresh carts, once without a cheat file and once
# with Game Genie patches (on bytes the game doesn't run) and RAM writes every
# frame (to WRAM, HRAM, the NES's RAM) picked. After the same frames, power-off
# and the next power-on's copy, the two /torii/saves/<game>/Main.sav files must
# be the same bytes. Runs inside the torii-emu container after `just build` and
# `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/pipes.sh
. tests/pipes.sh
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-emu-cheat-saves"

cart=$carts/game
fail() {
  echo "FAIL: emu cheat saves: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

# run GAME CHT OUT: GAME (the ROM file) with CHT as its cheat file (none if empty);
# its save copied to OUT
run() {
  local game=$1 cht=$2 out=$3 name
  name=Save.${game##*.}
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$game" "::/$name"
  if [ -n "$cht" ]; then
    mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
    printf '%s' "$cht" >"$cart/game.cht"
    mcopy -i "$cart/sd.img" "$cart/game.cht" ::/torii/cheats/Save.cht
  fi
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$name" at=100) \
    -w "launch: starting the game" -s "@600:$cart/end.png" >/dev/null 2>"$cart/kernel.log" || fail "$out: the emulator failed"
  ! grep -q '^crash at frame' "$cart/kernel.log" || fail "$out: the emulator caught a crash"
  ! grep -q '^kernel error:' "$cart/kernel.log" || fail "$out: errors: $(grep '^kernel error:' "$cart/kernel.log" | drain_head -3)"
  if [ -n "$cht" ]; then
    grep -qF "pokes written to the emulator's table" "$cart/kernel.log" || fail "$out: no pokes were written"
    grep -qE "lines made [1-9][0-9]* patches" "$cart/kernel.log" || fail "$out: no Game Genie patch was made"
  fi
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: dialog" \
    -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 2000 >/dev/null 2>"$cart/kernel.log" || true
  grep -qF "kernel: save: wrote /torii/saves/$name/Main.sav" "$cart/kernel.log" || fail "$out: the save wasn't copied to SD"
  mcopy -o -i "$cart/sd.img" "::/torii/saves/$name/Main.sav" "$carts/$out"
}

cheats() { # CODE...: a file with each code a picked cheat
  printf 'cheats = %s\n\n' "$#"
  local n=0
  for code in "$@"; do
    printf 'cheat%s_desc = "Cheat %s"\ncheat%s_code = "%s"\ncheat%s_enable = true\n\n' "$n" "$n" "$n" "$code" "$n"
    n=$((n + 1))
  done
}

same() { # NAME A B
  cmp -s "$carts/$2" "$carts/$3" || fail "$1: the save with cheats differs from the one without ($(cmp "$carts/$2" "$carts/$3" | drain_head -1))"
}

gb_cheats=$(cheats "$(python3 tests/emu_cheat_codes.py gb 2010 77)" "$(python3 tests/emu_cheat_codes.py gb 4010 66)" 015500C1+0177A0D1 019990FF)
for kb in 32 8; do
  python3 tests/gb_rom.py "$carts/save$kb.gb" "$kb"
  run "$carts/save$kb.gb" "" "gb$kb-plain.sav"
  run "$carts/save$kb.gb" "$gb_cheats" "gb$kb-cheats.sav"
  same "GB $kb KB" "gb$kb-plain.sav" "gb$kb-cheats.sav"
done

python3 tests/nes_rom.py "$carts/save.nes" save
nes_cheats=$(cheats "$(python3 tests/emu_cheat_codes.py nes 9F10 77)" 0300:55+0400:66 07F0:01)
run "$carts/save.nes" "" nes-plain.sav
run "$carts/save.nes" "$nes_cheats" nes-cheats.sav
same NES nes-plain.sav nes-cheats.sav
echo "PASS: GB (32 KB and 8 KB) and NES battery saves are the same bytes with Game Genie patches and RAM writes picked as without"
