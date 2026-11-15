#!/usr/bin/env bash
# The GB cheats' cost: the hi-colour test ROM with 63 pokes picked runs at full
# speed (tests/emu_cheat_speed.py; --measure also prints the owned games' GB
# frames with VSync on and off). Runs inside the torii-emu container after
# `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts dist/cart-emu-cheat-speed
python3 tests/emu_cheat_speed.py "$carts" "$@"
echo "PASS: 63 GB pokes and 576 hi-colour colours a frame together keep Jagoomba at full speed"
