#!/usr/bin/env bash
# Report how much of the cart and GBA memory the kernel uses, and the largest
# symbols. Exits 1 if anything is over its limit. Runs in the devkitARM
# container. Usage: size.sh <elf> <gba> [count] (count 0: no symbols)
#
# Limits: the upgrade file holds the kernel in its first 1 MB; IWRAM is 32 KB
# with the stacks at the top (from 0x03007F00 down); EWRAM is 256 KB and
# mirrors every 256 KB, so data past its end overwrites its start.
set -euo pipefail

elf=$1 gba=$2 count=${3:-12}
bin=/opt/devkitpro/devkitARM/bin

rom=$(stat -c %s "$gba")
"$bin"/arm-none-eabi-size -A -d "$elf" | awk -v rom="$rom" '
  $2 > 0 && $3 >= 33554432 && $3 < 33816576 { if ($3 + $2 > ewram) ewram = $3 + $2 }
  $2 > 0 && $3 >= 50331648 && $3 < 50364416 { if ($3 + $2 > iwram) iwram = $3 + $2 }
  function line(name, used, limit, note) {
    printf "%-6s %9d / %9d bytes  %3d%%  ", name, used, limit, used * 100 / limit
    if (used > limit) { printf "OVER by %d%s\n", used - limit, note; over = 1 }
    else printf "%d free%s\n", limit - used, note
  }
  END {
    line("ROM", rom, 1048576, "")
    line("IWRAM", iwram - 50331648, 32512, " (below the stacks at 0x03007F00)")
    line("EWRAM", ewram - 33554432, 262144, ewram > 33816576 ? " (wraps onto the start of EWRAM)" : "")
    exit over
  }' || status=$?

if [ "$count" -gt 0 ]; then
  echo
  echo "Largest symbols (hex size, address, name):"
  "$bin"/arm-none-eabi-nm --size-sort -r -S "$elf" | awk -v n="$count" 'NR <= n { printf "  %8s  %s  %s\n", $2, $1, $4 }'
fi
exit "${status:-0}"
