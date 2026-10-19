#!/usr/bin/env bash
# The FPGA firmware update (Check_FW_update, source/Ezcard_OP.c) on the
# emulated cart: OMEGA_FPGA_VERSION sets the FPGA's version word, and the
# cart keeps the FPGA's config flash in fpga-flash.bin. An older Rev.B
# (LX16) or Rev.A (LX9) cart is offered the update; A writes the firmware,
# which must land at its flash address as the upgrade file's bytes at the
# address source/image_layout.h gives (whole 256-byte pages, as the official
# file wrote them, tests/image_test.py), and the kernel then waits for the
# power-off. B cancels and boots on; firmware that fails its CRC32 is
# refused and nothing is written; an up-to-date cart is asked nothing. Runs
# inside the torii-emu container after `just build` and `just emu-headless`;
# KERNEL picks another upgrade file.
set -uo pipefail

kernel=${KERNEL:-dist/ezkernelnew.bin}
root=dist/cart-fw-update
rm -rf "$root"
mkdir -p "$root"
failed=0

bad() {
  echo "FAIL: fw_update $case: $*"
  failed=1
}

# run CASE VERSION KERNEL KEY: boot a new cart with that FPGA version, press
# KEY once the update question or CRC error is up, and stop once the update
# is done or the menu is up
run() {
  case=$1
  cart=$root/$case
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n FW "$cart/sd.img" 65536 >/dev/null
  OMEGA_DIR=$cart OMEGA_FPGA_VERSION=$2 emu/build-linux/mgba-shot "$3" -f 3000 \
    -w "screen ready: dialog" -k "@8:$4" -k @14:- \
    -u "firmware: updated" -u "screen ready: sd-list" >"$cart/log.txt" 2>&1
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

# A Rev.B cart on firmware 6 and a Rev.A cart on firmware 4 take the update
for spec in "lx16 B006 LX16_newomega_top_bin 0x100000 B V07" "lx9 0004 LX9_newomega_top_bin 0x80000 A V05"; do
  read -r name version part at rev to <<<"$spec"
  run "$name" "$version" "$kernel" A
  grep -q "dialog: Rev.$rev firmware update: Installed: V0${version: -1} New: $to Press \[A\] to update Press \[B\] to cancel: A, yes" "$cart/log.txt" ||
    bad "no update question answered A: $(grep -m 3 'kernel' "$cart/log.txt")"
  grep -q "screen ready: progress" "$cart/log.txt" || bad "no progress dialog"
  grep -q "firmware: updated to $to, waiting for the power-off" "$cart/log.txt" || bad "the update didn't finish"
  out=$(check_flash "$part" "$at" 2>&1) || bad "$out"
done

# B cancels: nothing written, the menu comes up
run cancel B006 "$kernel" B
grep -q "firmware update: .*: B, no" "$cart/log.txt" || bad "no update question answered B"
grep -q "screen ready: sd-list" "$cart/log.txt" || bad "the menu didn't come up"
out=$(check_flash none 2>&1) || bad "$out"

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
run crc B006 "$root/bad-crc.bin" B
grep -q "dialog: Rev.B firmware update: The firmware in the kernel is damaged (CRC32). Press \[B\] to go back" "$cart/log.txt" ||
  bad "no CRC error: $(grep -m 3 'kernel' "$cart/log.txt")"
grep -q "screen ready: sd-list" "$cart/log.txt" || bad "the menu didn't come up"
out=$(check_flash none 2>&1) || bad "$out"

# Up to date: no question
run current B007 "$kernel" A
grep -q "firmware update" "$cart/log.txt" && bad "asked to update firmware 7"
grep -q "screen ready: sd-list" "$cart/log.txt" || bad "the menu didn't come up"
out=$(check_flash none 2>&1) || bad "$out"

[ $failed = 0 ] && echo "PASS: FPGA firmware update (LX16, LX9, cancel, CRC error, up to date)"
exit $failed
