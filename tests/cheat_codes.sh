#!/usr/bin/env bash
# Real cheat codes in real games (v0.7): each game of tests/cheat_codes.toml
# that roms/ holds gets a card with its ROM and a cheat file picking its codes
# (CodeBreaker, GameShark v1/v2 encrypted and raw, Action Replay v3, VBA raw,
# and ROM patches), starts with its addons, and runs for ten seconds; then
# every address a code writes must hold the code's value (RAM writes repeat
# in each interrupt, so the game can't keep another there), and a ROM patch
# must read back from 0x08xxxxxx. Games missing from roms/ are skipped.
# Runs inside the torii-emu container after `just build` and `just
# emu-headless` (`just compat` runs it).
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-cheat-codes"

failed=0 passed=0 skipped=0
# One line per game: file, then its codes as "desc|code|addr:width:value,..."
mapfile -t games < <(
  python3 - <<'EOF'
import tomllib
import os
codes = tomllib.load(open(os.environ.get("CHEAT_CODES", "tests/cheat_codes.toml"), "rb"))["code"]
games = {}
for c in codes:
    writes = ",".join(f"{w['address']:08X}:{w['width']}:{w['value']:X}" for w in c["decoded"])
    games.setdefault(c["game"], []).append(f"{c['format']} {c['desc']}|{c['code']}|{writes}")
for game, cs in games.items():
    print("\t".join([game, *cs]))
EOF
)

for line in "${games[@]}"; do
  IFS=$'\t' read -r -a f <<<"$line"
  game=${f[0]}
  if [ ! -f "roms/$game" ]; then
    echo "SKIP: cheat_codes: $game isn't in roms/"
    skipped=$((skipped + 1))
    continue
  fi
  cart=$carts/game
  rm -rf "$cart" && mkdir -p "$cart"
  kb=$((($(stat -c %s "roms/$game") / 1024 + 16384) / 1024 * 1024))
  [ "$kb" -ge 65536 ] || kb=65536
  mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" "$kb" >/dev/null
  mcopy -i "$cart/sd.img" "roms/$game" "::/Game.gba"
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  {
    printf 'cheats = %d\n\n' $((${#f[@]} - 1))
    for ((i = 1; i < ${#f[@]}; i++)); do
      IFS='|' read -r desc code _ <<<"${f[$i]}"
      printf 'cheat%d_desc = "%s"\ncheat%d_code = "%s"\ncheat%d_enable = true\n\n' $((i - 1)) "$desc" $((i - 1)) "$code" $((i - 1))
    done
  } >"$cart/Game.cht"
  mcopy -i "$cart/sd.img" "$cart/Game.cht" ::/torii/cheats/Game.cht
  dumps=()
  for ((i = 1; i < ${#f[@]}; i++)); do
    IFS='|' read -r _ _ writes <<<"${f[$i]}"
    IFS=, read -r -a ws <<<"$writes"
    for w in "${ws[@]}"; do
      IFS=: read -r addr width _ <<<"$w"
      dumps+=(-m "@600:0x$addr:$width")
    done
  done
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Game.gba addon) \
    -w "launch: starting the game" "${dumps[@]}" -f 6000 >"$cart/run.txt" 2>&1 || true
  if ! grep -q "launch: starting the game" "$cart/run.txt"; then
    echo "FAIL: cheat_codes: $game didn't start: $(grep -a '^kernel' "$cart/run.txt" | tail -2)"
    failed=$((failed + 1))
    continue
  fi
  for ((i = 1; i < ${#f[@]}; i++)); do
    IFS='|' read -r desc code writes <<<"${f[$i]}"
    IFS=, read -r -a ws <<<"$writes"
    bad=
    for w in "${ws[@]}"; do
      IFS=: read -r addr width value <<<"$w"
      want=$(python3 -c "import sys; print(int(sys.argv[1], 16).to_bytes(int(sys.argv[2]), 'little').hex())" "$value" "$width")
      got=$(sed -n "s/^mem frame [0-9]* $addr: //p" "$cart/run.txt" | head -1 | tr -d ' ' | tr A-F a-f)
      [ "$got" = "$want" ] || bad="$bad $addr=${got:-?} (want $want)"
    done
    if [ -z "$bad" ]; then
      echo "PASS: cheat_codes: $game: $desc ($code)"
      passed=$((passed + 1))
    else
      echo "FAIL: cheat_codes: $game: $desc ($code):$bad; $(grep -a 'cheats:' "$cart/run.txt" | tail -2 | tr '\n' ' ')"
      failed=$((failed + 1))
    fi
  done
done
echo "cheat_codes: $passed passed, $failed failed, $skipped games skipped"
[ "$failed" = 0 ]
