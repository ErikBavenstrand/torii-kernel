#!/usr/bin/env bash
# The proof of the GB and GBC cheats: tests/gb_cheat_proof.py runs each case's
# cheat file on its test ROM (tests/gb_cheat_rom.py) in mGBA's own Game Boy core
# with mGBA's cheat device and through Torii on the emulated cart (Jagoomba), and
# compares what the ROM read with a model of the device. Usage:
# gb_cheat_proof.sh [CASE...]. Runs inside the torii-emu container after
# `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts dist/cart-gb-cheat-proof
python3 tests/gb_cheat_proof.py "$carts" "$@"
echo "PASS: GB and GBC cheats run as a cheat device does: Game Genie on MBC1, MBC3 and MBC5 banks and bank 0, GameShark 01 and 90-97 (against mGBA's GB core and the model)"
