#!/usr/bin/env bash
# Sound after the in-game menu, SAVE and LOAD (tests/m4a_sound.py): part of
# `just test`. tests/roms/m4a's sound DMAs, as Nintendo's MP2000 (m4a)
# engine runs them, are left off after each for the engine to re-arm, and
# then play where the engine has them; its copy under another ident, not the
# engine's to the stub, has them on again at once.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh

local_carts dist/cart-m4a-sound
export TRIP_CARTS=$carts
python3 tests/m4a_sound.py tests/roms/m4a/m4a.gba tests/roms/m4a/m4a-other.gba
