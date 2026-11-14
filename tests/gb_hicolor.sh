#!/usr/bin/env bash
# Hi-colour GBC (jagoombacolor#20, #53): a game that rewrites the BG
# palettes during the frame gets each line's colours in Jagoomba, as on the
# GBC. tests/gb_hicolor_rom.py's ROM rewrites one palette in every line's
# HBlank (576 colours a frame); launched from the menu, its picture is
# compared with mGBA's own GB core running the same ROM (tests/gb_hicolor.py,
# which allows for Jagoomba's line timing), at gamma 0 and with the gamma
# setting on (mGBA's colours converted through Jagoomba's table), and
# Jagoomba's statistics say every frame's list fitted (HC_MAX, HC_FULL in
# its equates.h). Before the change every line had the palettes as they
# stood at line 75. Runs inside the torii-emu container after `just build`
# and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-gb-hicolor${1:+-$1}"

shot=emu/build-linux/mgba-shot
elf=emulators/out/jagoombacolor.elf
cart=$carts/gb-hicolor
fail() {
  echo "FAIL: gb hicolor: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}
if [ ! -f "$elf" ]; then
  echo "SKIP: gb hicolor: no $elf (just emulators)"
  exit 0
fi
rm -rf "$cart" && mkdir -p "$cart"
python3 tests/gb_hicolor_rom.py "$cart/Game.gbc"

# mGBA's picture: the ROM draws the same one every frame
$shot "$cart/Game.gbc" -s "300:$cart/mgba.png" >/dev/null 2>&1 || fail "mGBA's GB core failed"

{
  read -r vars
  read -r gamma_table
  read -r globals
  read -r gamma_value
} < <(python3 tests/elf_sym.py "$elf" HC_VARS HC_GAMMA GLOBAL_PTR_BASE _gammavalue)
gamma_word=$(printf '0x%08X' $(((globals + gamma_value) & ~3)))
gamma_shift=$((((globals + gamma_value) & 3) * 8))

# run NAME [GAMMA]: the game's picture at @300 and @400 after it starts (the
# gamma setting poked at @100), Jagoomba's HC_VARS and HC_GAMMA after them
run() {
  local name=$1 gamma=${2:-0} pokes=()
  [ "$gamma" = 0 ] || pokes=(-p "@100:$gamma_word:$(printf '0x%08X' $((gamma << gamma_shift)))")
  # a fresh card each time: a played game shows in Recent, and the route goes by the card
  rm -f "$cart/sd.img" "$cart"/*.bin
  mkfs.fat -C -F 32 -n GB "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/Game.gbc" ::/Game.gbc
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart $shot dist/ezkernelnew.bin $(nav_launch /Game.gbc at=100) -w "launch: starting the game" \
    -s "@300:$cart/$name-300.png" -s "@400:$cart/$name-400.png" \
    -m "@400:$vars:36" -m "@400:$gamma_table:64" "${pokes[@]}" >"$cart/$name.txt" 2>"$cart/kernel.log" ||
    fail "$name: the emulator failed"
  if grep -q '^crash at frame' "$cart/kernel.log"; then fail "$name: the emulator caught a crash"; fi
}

# mem NAME ADDR LEN: LEN bytes from ADDR of NAME's -m lines, as hex
mem() {
  python3 - "$cart/$1.txt" "$2" "$3" <<'PY'
import re, sys
start, n = int(sys.argv[2], 16), int(sys.argv[3])
data = {}
for line in open(sys.argv[1]):
    m = re.match(r"mem frame \d+ ([0-9A-F]+): (.*)", line)
    if m:
        for i, b in enumerate(bytes.fromhex(m[2].replace(" ", ""))):
            data[int(m[1], 16) + i] = b
print(bytes(data[start + i] for i in range(n)).hex())
PY
}

run gamma0
stats=$(mem gamma0 "$vars" 36)
max=$((16#${stats:62:2}${stats:60:2}${stats:58:2}${stats:56:2}))
full=$((16#${stats:70:2}${stats:68:2}${stats:66:2}${stats:64:2}))
[ "$max" -eq 576 ] || fail "the most colours a frame recorded is $max, not the ROM's 576"
[ "$full" -eq 0 ] || fail "$full frames ran out of entries"
for f in 300 400; do
  out=$(python3 tests/gb_hicolor.py "$cart/gamma0-$f.png" "$cart/mgba.png") || fail "gamma 0, @$f: $out (dist/cart-gb-hicolor*/gb-hicolor/)"
done
echo "PASS: gb hicolor: every line's palettes as mGBA's GB core shows them ($max colours a frame)"

run gamma2 2
table=$(mem gamma2 "$gamma_table" 64)
for f in 300 400; do
  out=$(python3 tests/gb_hicolor.py "$cart/gamma2-$f.png" "$cart/mgba.png" 0 "$table") || fail "gamma 2, @$f: $out"
done
echo "PASS: gb hicolor: with the gamma setting on, the lines' colours are converted as the frame's"
