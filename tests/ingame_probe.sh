#!/usr/bin/env bash
# The in-game probe ROM (tests/roms/ingame, v0.8 P0) on the emulated cart:
# launched CLEAN BOOT, it tries cart SRAM, the page register, a flash bank,
# a PSRAM write and the clock in game mode, saves its report (cart RAM page
# 0xF0 and its own save) and shows it, and A on its results goes back to
# the kernel, whose boot logs what it saw. Its SD test is retired (on the
# real cart it wedged the card until a power cycle): the report says "not
# run", and the kernel no longer makes a scratch file for it.
# The emulated cart answers as it models the real one; the point is that
# the probe runs, puts everything back and reports. The emulator lets its
# PSRAM write through (OMEGA_GAME_PSRAM_WRITES), which it stops from
# Torii's own in-game code.
# Also: a probe run cut by a power-off at its results (page 0xF0 lost)
# reports from its save, and a card that doesn't answer at first mounts on
# the boot's retry (the real cart showed "microSD card error" after the
# probe, and the power-off that took lost page 0xF0).
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-ingame-probe${1:+-$1}"

fail() {
  echo "FAIL: in-game probe: $*" >&2
  exit 1
}

cart=$carts/ingame-probe shots=dist/shots/ingame-probe
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n PROBE "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/ingame/ingame.gba ::/Probe.gba

k() { printf -- '-k %d:%s -k %d:- ' "$1" "$2" $(($1 + 4)); }
run() {
  OMEGA_DIR=$cart OMEGA_GAME_PSRAM_WRITES=allow emu/build-linux/mgba-shot dist/ezkernelnew.bin \
    -c 1767225600 "$@" >"$cart/out.log" 2>"$cart/kernel.log" || true
  ! grep -q '^crash at frame' "$cart/kernel.log" || fail "the emulator caught a crash"
}
has() { grep -qF "in-game probe: $1" "$log" || fail "no line '$1': $(grep 'in-game probe' "$log" | head -12)"; }

# 1. CLEAN BOOT of Probe.gba: its cart tests run by themselves; A goes
# back to the kernel, and A at its "Save sav?" lets the boot go on to the
# report.
# shellcheck disable=SC2046
run $(nav_launch /Probe.gba at=180) -s 600:"$shots/1-cart.png" $(k 620 A) $(k 1000 A) \
  -s 1400:"$shots/3-menu.png" -f 1400
log=$cart/kernel.log
cp "$log" "$cart/1.log"
grep -q "launch: CLEAN BOOT" "$log" || fail "not launched: $(tail -3 "$log")"
! grep -q "scratch sector" "$log" || fail "the kernel still made a scratch sector"
# The emulated cart: plain RAM on every page, page register write-only,
# no flash save (the banks alike), PSRAM read-only in game mode, the SD
# clock as in kernel mode
has "the report from cart RAM page 0xF0 (the soft reset kept it)"
has "its copy in the save: byte writes (SRAM)"
has "cart SRAM pages 20-EF: 208 of 208 read back (first bad 00)"
has "SRAM wait states read back at 4 yes, 3 yes, 2 yes, 8 cycles yes"
has "flash bank: "
has "PSRAM 0x08010000 read 0000, 0000 after writing A55A, 0000 put back (PSRAM writes don't land)"
has "SD access: not run: SD access in game mode wedges the card (retired 2026-10-27)"
has "clock with 0x096A0000 at 1: 26-01-01"
has "page 0x20: 0 of 24576 bytes didn't read back (byte writes at 8 cycles); with the flash bank switched it read 3C in bank 1, C3 in bank 0 (one byte: the bank doesn't reach it)"
! mdir -i "$cart/sd.img" ::/torii/probe >/dev/null 2>&1 || fail "the kernel made /torii/probe"
# Both copies of the report are cleared once logged.
[ -z "$(od -A n -t x1 -v -j $((0xF1B00)) -N 16 "$cart/sram.bin" | tr -d ' \n0')" ] || fail "the report wasn't cleared"
[ -z "$(od -A n -t x1 -v -j $((0x1000)) -N 16 "$cart/sram.bin" | tr -d ' \n0')" ] || fail "the save's copy wasn't cleared"

# 2. The power goes off at the results instead of A: the next power-on
# (page 0xF0 blank again) reports from the save.
# shellcheck disable=SC2046
run $(nav_launch /Probe.gba at=180) -s 700:"$shots/4-results.png" -f 720
grep -q "launch: CLEAN BOOT" "$log" || fail "not launched again: $(tail -3 "$log")"
# shellcheck disable=SC2046
run $(k 300 A) -f 900
cp "$log" "$cart/2.log"
has "the report from its save: cart RAM page 0xF0 lost it, so the cart was off since; A wasn't pressed"
has "SD access: not run"
has "cart SRAM pages 20-EF: 208 of 208 read back (first bad 00)"
[ -z "$(od -A n -t x1 -v -j $((0x1000)) -N 16 "$cart/sram.bin" | tr -d ' \n0')" ] || fail "the save's copy wasn't cleared"

# 3. A card that doesn't answer at first (its boot sector's first two reads,
# one mount) mounts on the retry.
OMEGA_SD_READ_ERROR=0:2 run -f 600
grep -q "kernel error: boot: the card didn't mount (1); trying again (1 of 3)" "$log" ||
  fail "no retry: $(grep -m3 'boot:' "$log")"
grep -q "screen ready: home" "$log" || fail "the menu didn't come up after the retry: $(tail -3 "$log")"
# 3b. A card that mounts but then reads nothing (as the real cart's after the
# probe's SD test, 2026-10-27: "Loading..." for long, then an empty list)
# stops at the error that asks for a power cycle, not an empty list.
OMEGA_SD_READ_ERROR=64-65535 run -s 3000:"$shots/5-unreadable.png" -f 3000
cp "$log" "$cart/unreadable.log"
grep -q "kernel error: library: none loaded or scanned, and [0-9]* reads failed" "$log" ||
  fail "no unreadable card: $(grep -m3 'library:' "$log")"
! grep -q "screen ready: home" "$log" || fail "the menu came up on a card that doesn't read"
# 4. A state left in cart SRAM's state buffer with its flag (as a Pokémon
# Emerald launch leaves one) when the probe starts CLEAN BOOT: the probe's
# page tests write page 0xAF, and the FPGA writes the buffer back to the card
# through the table's state-file runs. On the real cart (2026-10-27) the
# launch's table had none, the walk wrapped into the ROM's own runs and the
# buffer overwrote the probe's ROM file; the kernel now clears the flag and
# sends the runs of /torii/rts.bin, so the ROM file and every file but that
# one stay as they were.
mcopy -i "$cart/sd.img" tests/roms/ingame/ingame.gba ::/Flag.gba
python3 - "$cart/sram.bin" <<'EOF'
import sys
p = sys.argv[1]
b = bytearray(open(p, "rb").read())
for i in range(0x40000, 0xAFFF0):  # a heap header's 0xA3A3, as Emerald's EWRAM starts
    b[i] = 0xA3
b[0xAFFF0:0xB0000] = b"EZ-OmegaRTCFILE."
open(p, "wb").write(b)
EOF
mcopy -n -i "$cart/sd.img" ::/Probe.gba "$cart/before.gba"
# OMEGA_SOFT_RESET: the buffer kept (the cart wasn't off, or kept enough)
# shellcheck disable=SC2046
OMEGA_SOFT_RESET=1 run $(nav_launch /Flag.gba at=180) $(k 620 A) $(k 1000 A) -f 1400
cp "$log" "$cart/flag.log"
grep -q "launch Flag.gba: code TIGP" "$log" || fail "not launched with a flagged state: $(tail -3 "$log")"
! grep -q "STATE BUFFER WRITTEN OVER THE GAME'S FILE" "$log" ||
  fail "the state buffer was written over the game's file: $(grep -m2 'omega: state buffer' "$log")"
mcopy -n -i "$cart/sd.img" ::/Flag.gba "$cart/flag.gba"
cmp -s "$cart/flag.gba" tests/roms/ingame/ingame.gba || fail "/Flag.gba changed on the card"
mcopy -n -i "$cart/sd.img" ::/Probe.gba "$cart/after.gba"
cmp -s "$cart/before.gba" "$cart/after.gba" || fail "/Probe.gba changed on the card"
has "cart SRAM pages 20-EF: 208 of 208 read back (first bad 00)"
# The flag went before the launch: no write-back at all
! grep -q "omega: state buffer written back" "$log" || fail "a write-back with the flag cleared: $(grep -m1 'written back' "$log")"

# 5. The probe at each other save type (the real cart showed a white screen
# at EEPROM 8K, FLASH 64K and FLASH 128K, from a ROM whose header read
# blank): it runs, A goes back and the report is logged.
# Each under a name of its own, so no save of another type asks first.
for save in 2 3 4 5; do
  mcopy -i "$cart/sd.img" tests/roms/ingame/ingame.gba ::/P$save.gba
  # shellcheck disable=SC2046
  run $(nav_launch /P$save.gba save=$save at=180) $(k 620 A) $(k 1000 A) -f 1400
  cp "$log" "$cart/save-$save.log"
  grep -q "launch P$save.gba: code TIGP, .* save mode" "$log" || fail "not launched at save type $save"
  has "cart SRAM pages 20-EF: 208 of 208 read back (first bad 00)"
  has "page 0x20: 0 of 24576 bytes didn't read back"
done

echo "PASS: in-game probe"
