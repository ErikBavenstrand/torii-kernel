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
