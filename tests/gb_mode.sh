#!/usr/bin/env bash
# Which mode Jagoomba runs a game in (jagoombacolor#43): header byte $143
# $80 (GBC-compatible, "dual") or $C0 (GBC only) runs as a Game Boy Color
# game, and $146 = 3 with $14B = $33 as a Super Game Boy game, where the
# "Game Boy" setting (default: prefer GBC over SGB) picks for a game with
# both. A dual-mode game starts as SGB for a moment, to load its border
# (autoborder, jagoombacolor's cart.s), and then resets as GBC, so the check
# comes after that. A game with no mode flag gets the palette its title has in
# the Game Boy Color's table, or grey (jagoombacolor#34). Test ROMs from tests/gb_rom.py, and Link's Awakening DX
# when roms/ has it. Runs inside the torii-emu container after `just build`
# and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/pipes.sh
. tests/pipes.sh
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-gb-mode${1:+-$1}"

cart=$carts/gb-mode
fail() {
  echo "FAIL: gb mode: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

elf=emulators/out/jagoombacolor.elf
if [ ! -f "$elf" ]; then
  echo "SKIP: gb mode: no $elf (just emulators)"
  exit 0
fi
# gbc_mode is followed by sgb_mode, one byte each
{
  read -r gbc_mode
  read -r border_state
  read -r palette_bank
} < <(python3 tests/elf_sym.py "$elf" gbc_mode _autoborderstate palettebank)

# check NAME ROM GBC SGB [BANK]: ROM launched, and Jagoomba ends in the mode
# (1 or 0 for GBC and SGB) and, with BANK, with that palette bank
check() {
  local name=$1 rom=$2 want_gbc=$3 want_sgb=$4 want_bank=${5:-}
  rm -rf "$cart" && mkdir -p "$cart"
  cp "$rom" "$cart/Game.gb"
  mkfs.fat -C -F 32 -n GB "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/Game.gb" ::/Game.gb
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Game.gb at=100) \
    -w "launch: starting the game" -m "@900:$gbc_mode:2" -m "@900:$border_state:1" -m "@900:$palette_bank:1" -f 3000 \
    >"$cart/run.txt" 2>"$cart/kernel.log" || fail "$name: the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "$name: the emulator caught a crash"
  local got
  got=$(grep -E "^mem frame [0-9]+ ${gbc_mode#0x}" "$cart/run.txt" | awk '{print $5 $6}' | tr 'A-F' 'a-f')
  [ "$got" = "0${want_gbc}0${want_sgb}" ] || fail "$name: GBC/SGB flags are '$got', not 0${want_gbc}0${want_sgb}"
  if [ -n "$want_bank" ]; then
    local bank
    bank=$(grep -E "^mem frame [0-9]+ ${palette_bank#0x}" "$cart/run.txt" | awk '{print $5}' | tr 'A-F' 'a-f')
    [ "$bank" = "$(printf %02x "$want_bank")" ] || fail "$name: palette bank is '$bank', not $want_bank"
  fi
  local kind=DMG
  [ "$want_sgb" = 1 ] && kind=SGB
  [ "$want_gbc" = 1 ] && kind=GBC
  echo "PASS: gb mode: $name runs as $kind"
}

for mode in dual gbc sgb none; do
  python3 tests/gb_rom.py "$cart.$mode.gb" 8 "$mode" TORIITEST
done
# Titles the GBC's palette table (gbcgamedetect.c) knows and doesn't:
# jagoombacolor#34, a game with none gets the grey palette, not the yellow-green
python3 tests/gb_rom.py "$cart.zelda.gb" 8 none ZELDA
python3 tests/gb_rom.py "$cart.hello.gb" 8 none HELLO
check "dual-mode ROM" "$cart.dual.gb" 1 0
check "GBC-only ROM" "$cart.gbc.gb" 1 0
check "SGB ROM" "$cart.sgb.gb" 0 1
check "plain ROM" "$cart.none.gb" 0 0 1
check "plain ROM titled ZELDA (the table's palette 89)" "$cart.zelda.gb" 0 0 89
check "plain ROM titled HELLO (not in the table: grey)" "$cart.hello.gb" 0 0 1
rm -f "$cart".*.gb

link=$(find roms -maxdepth 1 -name "*Link's Awakening DX.gbc" 2>/dev/null | drain_head -1)
if [ -n "$link" ]; then
  check "Link's Awakening DX" "$link" 1 0
else
  echo "SKIP: gb mode: no Link's Awakening DX in roms/"
fi
