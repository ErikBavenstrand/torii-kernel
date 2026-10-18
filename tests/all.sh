#!/usr/bin/env bash
# Run the test scripts in parallel, then print each one's output in order.
# Exits 1 if any failed. Runs inside the torii-emu container (see `just test`).
set -uo pipefail

jobs=(
  "tests/kernel.sh"
  "tests/smoke.sh"
  "tests/clean_boot.sh"
  "tests/hard_reset.sh"
  "tests/save_types.sh flash128"
  "tests/save_types.sh flash64"
  "tests/save_types.sh eeprom8k"
  "tests/save_types.sh eeprom512"
  "tests/save_types.sh eeprom8k-short"
  "tests/saves.sh copy"
  "tests/saves.sh full"
  "tests/eeprom_probe.sh 512"
  "tests/eeprom_probe.sh 8k"
  "tests/crash.sh"
  "tests/state.sh"
  "tests/settings.sh"
  "tests/menu.sh"
  "tests/names.sh fat32"
  "tests/names.sh exfat"
  "tests/stale_recent.sh"
  "tests/fragmented.sh"
  "tests/listing.sh"
  "tests/cheats.sh"
  "tests/nor.sh"
  "tests/nor_safety.sh"
  "tests/loader_safety.sh"
  "tests/logging.sh launch"
  "tests/logging.sh debug"
  "tests/logging.sh crash"
  "tests/logging.sh rotate"
  "tests/logging.sh junk"
  "tests/logging.sh full"
)

logs=$(mktemp -d)
pids=()
for i in "${!jobs[@]}"; do
  ${jobs[$i]} >"$logs/$i" 2>&1 &
  pids+=($!)
done

failed=0
for i in "${!jobs[@]}"; do
  wait "${pids[$i]}" || failed=1
  cat "$logs/$i"
done
rm -rf "$logs"
exit $failed
