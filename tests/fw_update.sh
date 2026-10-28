#!/usr/bin/env bash
# The FPGA firmware update (Check_FW_update, source/Ezcard_OP.c) on the
# emulated cart: OMEGA_FPGA_VERSION sets the FPGA's version word, and the
# cart keeps the FPGA's config flash in fpga-flash.bin. Only an allowlisted
# board (source/fpga_board.h: an LX16, high nibble A or B) on older firmware
# is offered the update. It shows the versions and a warning (A goes on),
# then asks for L+R+A held two seconds; then it writes the firmware, which
# must land at its flash address as the upgrade file's bytes at the address
# source/image_layout.h gives (whole 256-byte pages, as the official file
# wrote them, tests/image_test.py), and the kernel waits for the power-off.
# B on either screen, the hold let go early, or A alone cancels and boots
# on; firmware that fails its CRC32 is refused; an up-to-date cart is asked
# nothing. Runs inside the torii-emu container after `just build` and
# `just emu-headless`; KERNEL picks another upgrade file.
#
# With the argument sweep, it boots a sweep of version words instead: every
# high nibble, with low bytes around the LX16 (7) and LX9 (5) thresholds,
# and only the allowlisted, older LX16 words may get as far as the update's
# screens. Every other word, the LX9 ones too, shows nothing, writes nothing
# and logs the raw word.
set -uo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-fw-update${1:+-$1}"

kernel=${KERNEL:-dist/ezkernelnew.bin}
root=$carts/fw-update
rm -rf "$root"
mkdir -p "$root"
failed=0

bad() {
  echo "FAIL: fw_update $case: $*"
  failed=1
}

# new_cart CASE: an empty cart for CASE
new_cart() {
  case=$1
  cart=$root/$case
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n FW "$cart/sd.img" 65536 >/dev/null
}

# written: the FPGA flash holds anything but erased bytes
written() { [ -n "$(tr -d '\377' <"$cart/fpga-flash.bin" 2>/dev/null | head -c 1)" ]; }

if [ "${1:-}" = sweep ]; then
  # sweep_word WORD: boot a cart with that version word up to the update's
  # first screen or past the firmware check; prints a FAIL line if any
  # shellcheck disable=SC2329 # run through xargs
  sweep_word() {
    local word=$1 case=sweep-$1 cart=$root/sweep-$1 offered=0
    mkdir -p "$cart"
    cp "$root/sd.img" "$cart/sd.img"
    OMEGA_DIR=$cart OMEGA_FPGA_VERSION=$word emu/build-linux/mgba-shot "$kernel" -f 1500 \
      -u "screen ready: notice" -u "boot: WAITCNT" >"$cart/log.txt" 2>&1
    [[ $word == [AB]* ]] && [ $((16#${word:2})) -lt 7 ] && offered=1
    if [ "$offered" = 1 ]; then
      grep -q "firmware: FPGA version word $word, LX16 on V0${word: -1}, the kernel has V07" "$cart/log.txt" ||
        echo "FAIL: fw_update $case: no LX16 line: $(grep -m 3 'firmware' "$cart/log.txt")"
      grep -q "dialog: Rev.B firmware update: Checking" "$cart/log.txt" ||
        echo "FAIL: fw_update $case: an older LX16 wasn't offered the update"
    else
      grep -q "firmware update" "$cart/log.txt" && echo "FAIL: fw_update $case: the update showed a screen"
      grep -q "boot: WAITCNT" "$cart/log.txt" || echo "FAIL: fw_update $case: the boot didn't go on"
      case $word in
      [AB]*) grep -q "firmware: FPGA version word $word, LX16" "$cart/log.txt" ||
        echo "FAIL: fw_update $case: no LX16 line" ;;
      *) grep -q "firmware: FPGA version word $word isn't a known board: no update" "$cart/log.txt" ||
        echo "FAIL: fw_update $case: the word wasn't logged" ;;
      esac
    fi
    if [ -n "$(tr -d '\377' <"$cart/fpga-flash.bin" 2>/dev/null | head -c 1)" ]; then
      echo "FAIL: fw_update $case: the FPGA flash was written"
    fi
  }
  export -f sweep_word
  export kernel root
  mkfs.fat -C -F 32 -n FW "$root/sd.img" 65536 >/dev/null
  words=()
  for nibble in 0 1 2 3 4 5 6 7 8 9 A B C D E F; do
    for low in 00 04 05 06 07 08 FF; do
      words+=("${nibble}0$low")
    done
  done
  words+=(A106 BF00 FFFF)
  out=$(printf '%s\n' "${words[@]}" | xargs -P 4 -I{} bash -c 'sweep_word {}')
  if [ -n "$out" ]; then
    echo "$out"
    exit 1
  fi
  echo "PASS: FPGA firmware update sweep: ${#words[@]} version words, only older LX16 (A/B) ones offered, the rest write nothing"
  exit 0
fi

# run CASE VERSION KERNEL KEYS...: boot a new cart with that FPGA version and,
# once the update's first screen (or the CRC error) is up, press KEYS (mgba-shot
# options with @N frames from it, more -w waits among them); stop once the
# update is done or the menu is up
run() {
  new_cart "$1"
  local version=$2 rom=$3
  shift 3
  OMEGA_DIR=$cart OMEGA_FPGA_VERSION=$version emu/build-linux/mgba-shot "$rom" -f 3000 \
    -w "screen ready: dialog" "$@" \
    -u "firmware: updated" -u "screen ready: home" >"$cart/log.txt" 2>&1
}

# check_flash PART ADDRESS: fpga-flash.bin holds the upgrade file's PART (in whole
# pages) at ADDRESS and nothing else; check_flash none: nothing was written
check_flash() {
  python3 - "$cart/fpga-flash.bin" "$kernel" "$@" <<'EOF'
import sys
sys.path.insert(0, "tools")
import omega_image

flash = open(sys.argv[1], "rb").read()
upgrade = open(sys.argv[2], "rb").read()
what = sys.argv[3]
if what == "none":
    sys.exit(0 if flash == b"\xff" * len(flash) else "the FPGA flash was written")
part, at = what, int(sys.argv[4], 0)
layout = omega_image.read_layout()
src = layout[part + "_address"] - omega_image.ROM
span = omega_image.page_end(layout[part + "_size"])
if flash[at : at + span] != upgrade[src : src + span]:
    sys.exit(f"{part} isn't at {at:#x} in the FPGA flash")
rest = flash[:at] + flash[at + span :]
if rest != b"\xff" * len(rest):
    sys.exit("the FPGA flash was written outside the firmware")
EOF
}

# cancelled: nothing written, the boot went on to the menu
cancelled() {
  grep -q "firmware: update cancelled, V06 kept" "$cart/log.txt" || bad "no cancel logged"
  grep -q "firmware: writing" "$cart/log.txt" && bad "the update started"
  grep -q "screen ready: home" "$cart/log.txt" || bad "the menu didn't come up"
  out=$(check_flash none 2>&1) || bad "$out"
}

warning="dialog: Rev.B firmware update: Installed: V06, new: V07 A power cut can leave the cart unbootable: use fresh batteries or a plugged-in console."
hold="dialog: Rev.B firmware update: To update, hold L+R+A for two seconds. Press \[B\] to cancel."
# A on the warning, then on the hold screen
to_hold=(-k @8:A -k @14:- -w "screen ready: hold")

# A Rev.B cart on firmware 6: A, then L+R+A held two seconds (130 frames), writes
run lx16 B006 "$kernel" "${to_hold[@]}" -k @8:L+R+A -k @138:-
grep -q "firmware: FPGA version word B006, LX16 on V06, the kernel has V07" "$cart/log.txt" || bad "no LX16 line"
grep -q "dialog: Rev.B firmware update: Checking the new firmware..." "$cart/log.txt" ||
  bad "no notice during the CRC check"
grep -q "$warning: A, yes" "$cart/log.txt" || bad "no warning answered A: $(grep -m 3 'dialog: Rev' "$cart/log.txt")"
grep -q "$hold: held" "$cart/log.txt" || bad "no hold: $(grep 'dialog: Rev' "$cart/log.txt" | tail -1)"
grep -q "screen ready: progress" "$cart/log.txt" || bad "no progress dialog"
grep -q "firmware: updated to V07, waiting for the power-off" "$cart/log.txt" || bad "the update didn't finish"
out=$(check_flash LX16_newomega_top_bin 0x100000 2>&1) || bad "$out"

# B on the warning cancels
run cancel-warning B006 "$kernel" -k @8:B -k @14:-
grep -q "$warning: B, no" "$cart/log.txt" || bad "no warning answered B"
grep -q "screen ready: hold" "$cart/log.txt" && bad "the hold screen came up"
cancelled

# B on the hold screen cancels
run cancel-hold B006 "$kernel" "${to_hold[@]}" -k @8:B -k @14:-
grep -q "$hold: B, no" "$cart/log.txt" || bad "no hold answered B"
cancelled

# B while L+R+A are held cancels
run cancel-holding B006 "$kernel" "${to_hold[@]}" -k @8:L+R+A -k @40:L+R+A+B -k @50:-
grep -q "$hold: B, no" "$cart/log.txt" || bad "no hold answered B"
cancelled

# B held from the warning (pressed while A was down) into the hold: no, on
# the warning already
run b-under-a B006 "$kernel" -k @8:A -k @12:A+B -k @14:B -k @40:L+R+A+B -k @200:-
grep -q "$warning: B, no" "$cart/log.txt" || bad "B pressed under A didn't cancel the warning"
cancelled

# A and B together on the warning: no
run a-and-b B006 "$kernel" -k @8:A+B -k @14:- -k @40:L+R+A -k @200:-
grep -q "$warning: B, no" "$cart/log.txt" || bad "A+B was taken as yes"
cancelled

# B held into the hold screen, then L+R+A with it: cancels
run b-held B006 "$kernel" -k @8:A -k @12:- -k @13:B -w "screen ready: hold" -k @8:L+R+A+B -k @200:-
grep -q "$hold: B, no" "$cart/log.txt" || bad "B held didn't cancel the hold"
cancelled

# L let go for one frame halfway cancels
run partial B006 "$kernel" "${to_hold[@]}" -k @8:L+R+A -k @68:R+A -k @69:L+R+A -k @200:-
grep -q "$hold: let go early" "$cart/log.txt" || bad "L let go didn't cancel"
cancelled

# L+R+A let go after one second cancels
run early B006 "$kernel" "${to_hold[@]}" -k @8:L+R+A -k @68:-
grep -q "$hold: let go early" "$cart/log.txt" || bad "the early release didn't cancel"
cancelled

# A alone, pressed and held three seconds, starts nothing; L+R without A
# neither; then B
run a-alone B006 "$kernel" "${to_hold[@]}" -k @8:A -k @14:- -k @22:A -k @202:- -k @210:L+R -k @390:- \
  -k @400:B -k @406:-
grep -q "$hold: B, no" "$cart/log.txt" || bad "the hold wasn't left to B: $(grep 'dialog: Rev' "$cart/log.txt" | tail -1)"
cancelled

# One byte of the LX16 firmware changed: the CRC32 check refuses it
python3 - "$kernel" "$root/bad-crc.bin" <<'EOF'
import sys
sys.path.insert(0, "tools")
import omega_image

data = bytearray(open(sys.argv[1], "rb").read())
layout = omega_image.read_layout()
data[layout["LX16_newomega_top_bin_address"] - omega_image.ROM + 0x1000] ^= 1
open(sys.argv[2], "wb").write(data)
EOF
run crc B006 "$root/bad-crc.bin" -k @8:A -k @14:-
grep -q "dialog: Rev.B firmware update: The firmware in the kernel is damaged (CRC32). Press \[B\] to go back" "$cart/log.txt" ||
  bad "no CRC error: $(grep -m 3 'kernel' "$cart/log.txt")"
grep -q "firmware: the kernel's V07 image fails its CRC32" "$cart/log.txt" || bad "no CRC failure logged"
grep -q "firmware: writing" "$cart/log.txt" && bad "the update started"
grep -q "screen ready: home" "$cart/log.txt" || bad "the menu didn't come up"
out=$(check_flash none 2>&1) || bad "$out"

# Up to date, an LX9 word as EZ-Flash's source took it, and an unknown board:
# no screen, nothing written
for spec in "current B007" "lx9 0004" "unknown 5006"; do
  read -r name version <<<"$spec"
  new_cart "$name"
  OMEGA_DIR=$cart OMEGA_FPGA_VERSION=$version emu/build-linux/mgba-shot "$kernel" -f 3000 \
    -u "screen ready: home" >"$cart/log.txt" 2>&1
  grep -q "firmware update" "$cart/log.txt" && bad "a firmware update screen came up"
  grep -q "screen ready: home" "$cart/log.txt" || bad "the menu didn't come up"
  written && bad "the FPGA flash was written"
done
grep -q "firmware: FPGA version word 0004 isn't a known board: no update" "$root/lx9/log.txt" ||
  { case=lx9 && bad "the word wasn't logged"; }

[ $failed = 0 ] &&
  echo "PASS: FPGA firmware update (LX16 written after the warning and the hold; B on both screens, with A or held into the hold, the hold let go early or partly, A alone; CRC error; up to date, LX9, unknown)"
exit $failed
