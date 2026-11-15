#!/usr/bin/env bash
# Runs Blargg's Game Boy test ROMs (and dmg-acid2 and cgb-acid2) through
# Jagoomba on the emulated cart and reports which pass (jagoombacolor#42).
# Each ROM runs as a game launched from the menu; the test prints its result
# as text on the screen, which is the tile map at $9800 of Jagoomba's copy of
# the Game Boy's VRAM (XGB_VRAM, one tile per character), so the run reads it
# from memory: "Passed" or "Failed...". The acid2 ROMs draw a picture: their
# screenshots go to dist/gb-tests/out/ for a look, not a verdict.
# The ROMs come from tools/gb_tests_fetch.sh (`just gb-accuracy` runs it); with
# none, this skips. It reports and doesn't fail on a ROM that fails: Jagoomba is
# not cycle-accurate. GB_ACCURACY_JOBS runs that many at once (default 4);
# GB_ACCURACY_FRAMES the frames each gets after the game starts (default 3000).
# The report is dist/gb-tests/report.txt. Runs inside the torii-emu container
# after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

shot=emu/build-linux/mgba-shot
elf=emulators/out/jagoombacolor.elf
dir=dist/gb-tests
frames=${GB_ACCURACY_FRAMES:-3000}

if [ "${1:-}" = --case ]; then
  # One ROM: --case ROM NAME CARTS VRAM
  rom=$2 name=$3 cart=$4/$3 vram=$5
  rm -rf "$cart" && mkdir -p "$cart"
  cp "$rom" "$cart/Game.${rom##*.}"
  mkfs.fat -C -F 32 -n GB "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$rom" "::/Game.${rom##*.}"
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart $shot dist/ezkernelnew.bin $(nav_launch "/Game.${rom##*.}" at=100) -w "launch: starting the game" \
    -m "@$frames:$vram:576" -s "@$frames:$dir/out/$name.png" >"$cart/run.txt" 2>"$cart/kernel.log" || true
  python3 - "$cart/run.txt" "$name" >"$dir/out/$name.txt" <<'PY'
import re, sys
data = b""
for line in open(sys.argv[1]):
    m = re.match(r"mem frame \d+ [0-9A-F]+: (.*)", line)
    if m:
        data += bytes.fromhex(m[1].replace(" ", ""))
rows = [data[i : i + 32].decode("latin-1").rstrip() for i in range(0, 576, 32)]
text = " ".join(r for r in rows if r.strip() and all(" " <= c <= "~" for c in r))
verdict = "FAIL" if "Failed" in text else "PASS" if "Passed" in text else "?"
print(f"{verdict}\t{sys.argv[2]}\t{text[:90]}")
PY
  rm -rf "$cart"
  exit 0
fi

if [ ! -f "$elf" ] || [ ! -d "$dir/retrio" ]; then
  echo "SKIP: gb accuracy: no $elf or no $dir/retrio (tools/gb_tests_fetch.sh)"
  exit 0
fi
local_carts dist/cart-gb-accuracy
vram=$(python3 tests/elf_sym.py "$elf" XGB_VRAM)
vram=$(printf '0x%08X' $((vram + 0x1800)))
rm -rf "$dir/out" && mkdir -p "$dir/out"

# One ROM per line: NAME<TAB>PATH. The whole cpu_instrs.gb is its 11 singles
# and dmg_sound.gb and mem_timing.gb the same, so only the singles run
{
  for f in "$dir"/retrio/cpu_instrs/individual/*.gb "$dir"/retrio/mem_timing/individual/*.gb \
    "$dir"/retrio/mem_timing-2/rom_singles/*.gb "$dir"/retrio/oam_bug/rom_singles/*.gb \
    "$dir"/retrio/dmg_sound/rom_singles/*.gb "$dir"/retrio/cgb_sound/rom_singles/*.gb \
    "$dir"/retrio/instr_timing/instr_timing.gb "$dir"/retrio/interrupt_time/interrupt_time.gb \
    "$dir"/retrio/halt_bug.gb "$dir"/dmg-acid2.gb "$dir"/cgb-acid2.gbc; do
    [ -f "$f" ] || continue
    rel=${f#"$dir"/retrio/}
    rel=${rel#"$dir"/}
    printf '%s\t%s\n' "$(echo "${rel%.*}" | tr '/ ,' '___' | sed 's/_rom_singles//;s/_individual//')" "$f"
  done
} >"$carts/list.txt"

start=$(date +%s)
jobs=${GB_ACCURACY_JOBS:-4}
while IFS=$'\t' read -r name rom; do
  while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do wait -n || true; done
  "$0" --case "$rom" "$name" "$carts" "$vram" &
done <"$carts/list.txt"
wait || true

cat "$dir"/out/*.txt >"$dir/report.txt"
cat "$dir/report.txt"
pass=$(grep -c '^PASS' "$dir/report.txt" || true)
fail=$(grep -c '^FAIL' "$dir/report.txt" || true)
echo "gb accuracy: $pass passed, $fail failed, $(($(wc -l <"$dir/report.txt") - pass - fail)) without a verdict, of $(wc -l <"$dir/report.txt") ROMs in $(($(date +%s) - start)) s"
