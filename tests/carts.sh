# shellcheck shell=bash
# Sourced by the test scripts: where their emulated carts live.
#
# The emulator memory-maps the cart's files and syncs them at the end of each
# power-on. On the macOS VM's virtiofs mount (dist/) that costs about 0.5 s
# for the 64 MB game NOR, against 0.03 s on the container's own disk, so carts
# go there.
#
# local_carts DIR: sets $carts to a new, empty folder on the container's disk
# standing in for DIR, and deletes DIR. When the script exits with a failure,
# the folder is copied to DIR, so a failed case can still be looked at there;
# paths in the failure messages then read from DIR.
local_carts() {
  _carts_keep=$1
  carts=${TMPDIR:-/tmp}/torii-carts/${1//\//-}
  rm -rf "$carts" "$_carts_keep"
  mkdir -p "$carts"
  trap _carts_exit EXIT
}

_carts_exit() {
  local status=$?
  if [ "$status" != 0 ]; then
    mkdir -p "$_carts_keep"
    cp -a "$carts/." "$_carts_keep/"
    echo "carts of the failed test kept in $_carts_keep (they ran in $carts)" >&2
  fi
  rm -rf "$carts"
  exit "$status"
}

# shard_init [N/M]: with N/M (a script's argument), the script runs only every
# Mth of its cases, from the Nth (N from 1), so tests/all.sh can run a long
# script as M jobs; without it, all of them. shard_case: whether the next
# case is this shard's, called once at the start of each case.
shard_init() {
  _shard_n=0 _shard_index=1 _shard_count=1
  if [ -n "${1:-}" ]; then
    _shard_index=${1%/*} _shard_count=${1#*/}
  fi
}

shard_case() {
  _shard_n=$((_shard_n + 1))
  [ $(((_shard_n - 1) % _shard_count + 1)) = "$_shard_index" ]
}

# sd_sectors IMAGE PATH: the first and last sector of PATH's first cluster
# run on a FAT image without partitions (mkfs.fat -C), as FIRST-LAST: what
# OMEGA_SD_READ_ERROR takes to fail reads of the file.
sd_sectors() {
  python3 - "$1" "$(mshowfat -i "$1" "::$2")" <<'PY'
import re, struct, sys
boot = open(sys.argv[1], "rb").read(512)
per_sector, per_cluster, reserved, fats = struct.unpack_from("<HBHB", boot, 11)
fat16, entries = struct.unpack_from("<H", boot, 22)[0], struct.unpack_from("<H", boot, 17)[0]
fat = fat16 or struct.unpack_from("<I", boot, 36)[0]
root = (entries * 32 + per_sector - 1) // per_sector
run = re.search(r"<(\d+)(?:-(\d+))?>", sys.argv[2])
first, last = int(run[1]), int(run[2] or run[1])
data = reserved + fats * fat + root
print(f"{data + (first - 2) * per_cluster}-{data + (last - 1) * per_cluster - 1}")
PY
}

# pending_flag NOR: the pending-save record's flag in a kernel-nor.bin, as
# the kernel reads it (Ezcard_OP.c's slots): the first of the 128 512-byte
# slots from 0x790000 whose flag isn't 0000; 0011 when a save waits, 0000
# for none (an erased slot, or every slot used)
pending_flag() {
  python3 - "$1" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
for k in range(128):
    at = 0x790000 + k * 512
    flag = struct.unpack_from("<H", data, at)[0] if at + 2 <= len(data) else 0xFFFF
    if flag != 0:
        print("%04x" % (flag if flag == 0x11 else 0))
        break
else:
    print("0000")
PY
}
