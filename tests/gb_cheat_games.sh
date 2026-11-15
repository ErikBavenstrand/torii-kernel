#!/usr/bin/env bash
# libretro's GB and GBC cheats on the owner's games, through Torii and mGBA's GB
# core (tests/gb_cheat_games.py). Needs roms/ and the corpus
# (dist/cheat-emu-corpus, tools/cheat_emu_stats.py); without them it skips.
# Usage: gb_cheat_games.sh [GAME...]. Runs inside the torii-emu container after
# `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts dist/cart-gb-cheat-games
python3 tests/gb_cheat_games.py "$carts" dist/cheat-emu-corpus "$@"
echo "PASS: libretro's GB and GBC cheats on the owner's games: pokes hold in Torii where they hold in mGBA, Game Genie patches as a device's"
