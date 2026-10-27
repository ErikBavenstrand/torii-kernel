#!/usr/bin/env bash
# The screenshots' software renderer (source/ppu_render.c, v0.8 P4) against
# mGBA's own screen: each case runs a ROM in plain mgba-shot, dumps VRAM,
# the palettes, OAM and the display registers after a frame and saves the
# screen; tools/ppu_shot draws the dump, and every pixel must match at 5
# bits a channel (tests/ppu_compare.py). The dumps carry every register,
# write-only ones too, so this checks the drawing; what the in-game menu
# can't read back is checked once with hello (its dimming, BLDY, goes).
#
# Cases: the hello ROM (mode 3, a brightness fade), the kernel's own home
# and SD card pages (mode 0, 4 and 8 bits, sprites, a window), then games
# the user has in roms/ (skipped when missing): bitmap modes (Doom,
# Wolfenstein 3D's second frame), an affine BG (Mario Kart's title),
# windows, blending and sprite priorities. The frames don't change mid-frame
# (Mode 7 races, which set their BG matrix each line, would fail).
# Runs inside the torii-emu container after `just build` and `just test-roms`.
set -euo pipefail

dir=dist/ppu-render
mkdir -p "$dir"
python3 tools/hostcc.py "$dir/ppu_shot" tools/ppu_shot.c source/ppu_render.c >/dev/null

fail() {
  echo "FAIL: ppu_render: $*" >&2
  exit 1
}

# A or START every 60 frames from 200: through the games' titles
walk=()
for f in $(seq 200 60 3000); do
  key=A
  [ $((f % 120)) = 80 ] && key=START
  walk+=(-k "$f:$key" -k "$((f + 6)):-")
done
starts=(-k 300:START -k 306:- -k 700:START -k 706:- -k 1200:A -k 1206:-)

# shot NAME ROM FRAME [--readable] [mgba-shot options]: the dump and mGBA's
# screen after FRAME, and the renderer's BMP of the dump
shot() {
  local name=$1 rom=$2 frame=$3 readable=
  shift 3
  if [ "${1:-}" = --readable ]; then
    readable=--readable
    shift
  fi
  emu/build-linux/mgba-shot "$rom" "$@" -f "$frame" -m "$frame:0x06000000:0x18000" -m "$frame:0x05000000:0x400" \
    -m "$frame:0x07000000:0x400" -m "$frame:0x04000000:0x60" -s "$frame:$dir/$name.png" >"$dir/$name.txt" 2>/dev/null ||
    fail "$name: mgba-shot failed"
  python3 tests/ppu_compare.py dump "$dir/$name.dump" "$frame" $readable <"$dir/$name.txt"
  "$dir/ppu_shot" "$dir/$name.dump" "$dir/$name.bmp"
}

checked=0
same() {
  shot "$@"
  python3 tests/ppu_compare.py diff "$dir/$1.bmp" "$dir/$1.png" >"$dir/$1.diff" ||
    fail "$1: the drawing differs from mGBA's: $(head -3 "$dir/$1.diff" | tr '\n' ' ')"
  checked=$((checked + 1))
}

same hello tests/roms/hello/hello.gba 100

# What the menu reads back: hello's half dimming (BLDY) is write-only, so
# the screenshot shows its blue undimmed, RGB(2, 6, 20) at 8 bits
shot hello-readable tests/roms/hello/hello.gba 100 --readable
pixel=$(python3 -c '
import sys
d = open(sys.argv[1], "rb").read()
at = 54 + (80 * 240 + 120) * 3
print(*d[at:at + 3][::-1])' "$dir/hello-readable.bmp")
[ "$pixel" = "16 49 165" ] || fail "hello without BLDY: the blue reads $pixel, not 16 49 165"

cart=$dir/cart
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n PPU "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
cp dist/ezkernelnew.bin "$dir/kernel.gba"
OMEGA_DIR=$cart same kernel-home "$dir/kernel.gba" 200 -C 1793109780
OMEGA_DIR=$cart same kernel-sd "$dir/kernel.gba" 330 -C 1793109780 -k 240:RIGHT -k 244:- -k 300:A -k 304:-

games=0
game() {
  local name=$1 file=roms/$2.gba
  shift 2
  [ -f "$file" ] || return 0
  same "$name" "$file" "$@"
  games=$((games + 1))
}
game doom "Doom I" 900 "${starts[@]}"
game wolf3d "Wolfenstein 3D" 1500 "${starts[@]}"
game mk-title "Mario Kart - Super Circuit" 900 "${starts[@]}"
game mk-menu "Mario Kart - Super Circuit" 400 "${starts[@]}"
game advance-wars "Advance Wars" 1500 "${starts[@]}"
game kirby "Kirby & The Amazing Mirror" 400 "${starts[@]}"
game golden-sun "Golden Sun" 3000 "${walk[@]}"
game fusion "Metroid Fusion" 3000 "${walk[@]}"
game emerald "Pokemon - Emerald Version (Erik)" 3000 "${walk[@]}"
echo "PASS: ppu_render: $checked frames drawn as mGBA draws them ($games from games in roms/), BLDY left out as the menu reads it"
