#!/usr/bin/env bash
# libretro's GB and GBC cheats on your games, through Torii and mGBA's GB
# core (tests/gb_cheat_games.py). Needs roms/ and the corpus
# (dist/cheat-emu-corpus, tools/cheat_emu_stats.py); without them it skips.
# Usage: gb_cheat_games.sh [GAME...]. Runs inside the torii-emu container after
# `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts dist/cart-gb-cheat-games
python3 tests/gb_cheat_games.py "$carts" dist/cheat-emu-corpus "$@" 2>"$carts/games.log" || {
  cat "$carts/games.log" >&2
  exit 1
}
cat "$carts/games.log" >&2
if grep -qx "0 games run" "$carts/games.log"; then
  echo "SKIP: gb cheat games: no owned game with its libretro files (roms/, dist/cheat-emu-corpus)"
  exit 0
fi
echo "PASS: libretro's GB and GBC cheats on your games: pokes hold in Torii where they hold in mGBA, Game Genie patches as a device's"
