#!/usr/bin/env bash
# Save states across scenes on the test ROMs (tests/state_trips.py; `just
# compat` runs it on every game): part of `just test`.
#
#   hello    the round trips on the hello ROM, with a RAM cheat on (it adds 1
#            to a halfword each frame: a state loads with the cheats as
#            launched, and the frames after it must still be the save's)
#   refuse   a state from the card refused when it is another ROM's, another
#            path's or of a newer format, and one as v0.8.0 left it loaded
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh

mode=${1:?usage: state_trips.sh hello|refuse}
local_carts "dist/cart-trips-$mode"
export TRIP_CARTS=$carts

case $mode in
hello)
  cht=$carts/Hello.cht
  printf '%s\n' 'cheats = 1' 'cheat0_desc = "Count frames"' 'cheat0_code = "E203F000 0001"' 'cheat0_enable = true' >"$cht"
  python3 tests/state_trips.py --cheats "$cht" tests/roms/hello/hello.gba
  ;;
refuse)
  python3 tests/state_trips.py --refuse tests/roms/ingame/ingame.gba tests/roms/hello/hello.gba
  ;;
esac
