#!/usr/bin/env bash
# Run the test scripts in parallel, then print each one's output in order.
# Exits 1 if any failed. Runs inside the torii-emu container (see `just test`).
# The longest scripts run as shards (N/M: every Mth case from the Nth), so
# none of them sets the wall time alone.
set -uo pipefail

jobs=(
  "tests/kernel.sh"
  "tests/assets.sh"
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
  "tests/saves.sh nearly-full"
  "tests/saves.sh delete"
  "tests/saves.sh rename"
  "tests/games.sh"
  "tests/boxart.sh"
  "tests/eeprom_probe.sh 512"
  "tests/eeprom_probe.sh 8k"
  "tests/rumble_probe.sh"
  "tests/crash.sh"
  "tests/state.sh"
  "tests/settings.sh"
  "tests/menu.sh"
  "tests/scan_engine.sh"
  "tests/nes.sh"
  "tests/names.sh fat32"
  "tests/names.sh exfat"
  "tests/names_db.sh"
  "tests/stale_recent.sh"
  "tests/rename.sh"
  "tests/userdata.sh"
  "tests/navigation.sh"
  "tests/fragmented.sh"
  "tests/listing.sh"
  "tests/library.sh 1/4"
  "tests/library.sh 2/4"
  "tests/library.sh 3/4"
  "tests/library.sh 4/4"
  "tests/cheats.sh"
  "tests/nor.sh"
  "tests/nor_safety.sh 1/3"
  "tests/nor_safety.sh 2/3"
  "tests/nor_safety.sh 3/3"
  "tests/loader_safety.sh 1/4"
  "tests/loader_safety.sh 2/4"
  "tests/loader_safety.sh 3/4"
  "tests/loader_safety.sh 4/4"
  "tests/fw_update.sh"
  "tests/logging.sh launch"
  "tests/logging.sh debug"
  "tests/logging.sh crash"
  "tests/logging.sh rotate"
  "tests/logging.sh junk"
  "tests/logging.sh full"
  "tests/logging.sh settings"
  "tests/logging.sh open"
  "tests/logging.sh flood"
  "tests/logging.sh hang"
  "python3 tests/shots.py"
  "python3 tests/perf.py self-test"
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
