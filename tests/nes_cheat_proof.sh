#!/usr/bin/env bash
# The proof of the NES cheats: tests/nes_cheat_proof.py runs each case's cheat
# file on its test ROM (tests/nes_cheat_rom.py: NROM, UxROM, MMC1, MMC3)
# through Torii on the emulated cart (PocketNES) and compares what the ROM read
# with a model of the device. Usage: nes_cheat_proof.sh [CASE...]. Runs inside
# the torii-emu container after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts dist/cart-nes-cheat-proof
python3 tests/nes_cheat_proof.py "$carts" "$@"
echo "PASS: NES cheats run as a cheat device does: Game Genie of 6 and 8 letters on NROM, UxROM, MMC1 and MMC3 banks, raw RAM and PRG-RAM pokes"
