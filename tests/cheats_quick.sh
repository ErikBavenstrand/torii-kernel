#!/usr/bin/env bash
# A launch's quick decode (source/cheatlist.h): a CodeBreaker key line
# first, 149 cheats of another format, then a picked code encrypted with
# that key. The screen's first visit keeps the list's cache, checkpoints
# and all; a launch then decodes from the checkpoint before the pick (23
# cheats, from entry 128, not 151), the key carried in it, and the pick
# runs. A checkpoint changed in the cache
# (its CRCs made to fit) decodes to another program: the launch finds it
# isn't as the cache says, decodes the file in full, and the pick runs all
# the same.
# Runs inside the torii-emu container after `just build`, `just
# emu-headless` and building tests/roms/hello.
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-cheats-quick${1:+-$1}"

fail() {
  echo "FAIL: cheats-quick: $*" >&2
  exit 1
}

cart=$carts/cheats-quick
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
python3 - "$cart/Hello.cht" <<'EOF2'
import sys
sys.path.insert(0, "tools")
from cheat_encode import CBKey
key = CBKey(0x9A3B5C7D, 0x1F2E)
op1, op2 = key.encrypt(0x3203F08C, 0x0055)  # an 8-bit write of 55 to 0203F08C
lines = ["cheats = 151", "", 'cheat0_desc = "Key"', 'cheat0_code = "9A3B5C7D 1F2E"', ""]
for k in range(1, 150):
    lines += [f'cheat{k}_desc = "Cheat {k}"', f'cheat{k}_code = "{0x02030000 + 2 * k:08X}:{k & 0xFF:02X}"', ""]
lines += ['cheat150_desc = "Encrypted"', f'cheat150_code = "{op1:08X} {op2:04X}"', "cheat150_enable = true", ""]
open(sys.argv[1], "w", newline="\n").write("\n".join(lines))
EOF2
mcopy -i "$cart/sd.img" "$cart/Hello.cht" ::/torii/cheats/Hello.cht

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 30000 >"$cart/screen.txt" 2>&1 ||
  fail "the screen didn't run: $(grep -a '^kernel' "$cart/screen.txt" | tail -3)"
grep -q "cheats: /torii/cheats/Hello.cdc: list kept (151 cheats" "$cart/screen.txt" ||
  fail "the list's cache: $(grep -a 'cheats:' "$cart/screen.txt" | tail -3)"
cp "$cart/kernel-nor.bin" "$cart/nor-before.bin"

# launch LOG: Hello with addons, 0203F08C read once the game runs
launch() {
  cp "$cart/nor-before.bin" "$cart/kernel-nor.bin"
  rm -f "$cart/sram.bin"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" -m "@240:0x0203F08C:4" -f 30000 >"$1" 2>&1 ||
    fail "the launch didn't run: $(grep -a '^kernel' "$1" | tail -3)"
  got=$(sed -n 's/^mem frame [0-9]* 0203F08C: //p' "$1" | tr -d ' \n' | tr A-F a-f)
  [ "${got:0:2}" = 55 ] || fail "the encrypted pick didn't run: 0203F08C holds ${got:0:2}, not 55"
}
launch "$cart/launch.txt"
grep -q "cheats: /torii/cheats/Hello.cht: launch decoded 23 cheats (from the cache's checkpoints)" "$cart/launch.txt" ||
  fail "the quick decode: $(grep -a 'launch decoded' "$cart/launch.txt")"
echo "PASS: cheats-quick: a launch decodes from the checkpoint before its pick, the CodeBreaker key carried in it"

# The checkpoint before entry 128 (the fifth) with another key, its CRCs made to fit
mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cdc "$cart/Hello.cdc"
python3 - "$cart/Hello.cdc" <<'EOF2'
import struct, sys, zlib
data = bytearray(open(sys.argv[1], "rb").read())
head = list(struct.unpack_from("<8I", data))
size, count = 4 + 96, head[3]
ckpts = (count + 31) // 32
at = 32 + 4 * size
assert struct.unpack_from("<I", data, at)[0] == 129, "the fifth checkpoint is before entry 128"
struct.pack_into("<I", data, at + 4 + 72, struct.unpack_from("<I", data, at + 4 + 72)[0] ^ 0x00FF00FF)  # cb_master
head[7] = zlib.crc32(bytes(data[32 : 32 + ckpts * size]))
head[6] = zlib.crc32(bytes(data[32:]))
struct.pack_into("<8I", data, 0, *head)
open(sys.argv[1], "wb").write(data)
EOF2
mcopy -o -i "$cart/sd.img" "$cart/Hello.cdc" ::/torii/cheats/Hello.cdc
launch "$cart/tampered.txt"
grep -q "cheat 150 isn't as its cache says: the launch decodes in full" "$cart/tampered.txt" ||
  fail "the changed checkpoint wasn't found: $(grep -a 'cheats:' "$cart/tampered.txt" | tail -4)"
grep -q "cheats: /torii/cheats/Hello.cht: launch decoded 151 cheats in" "$cart/tampered.txt" ||
  fail "the full decode after it: $(grep -a 'launch decoded' "$cart/tampered.txt")"
echo "PASS: cheats-quick: a checkpoint that isn't the file's is caught at the pick, and the launch decodes in full"
