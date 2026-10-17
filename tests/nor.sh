#!/usr/bin/env bash
# Game NOR test on the emulated cart: write two test ROMs to the game NOR
# through WRITE TO NOR, power-cycle, and boot each from the NOR list. The
# first game must keep its save across NOR boots. Runs inside the torii-emu
# container after `just build`, `just emu-headless` and the test ROMs.
set -euo pipefail

cart=dist/cart-nor
shots=dist/shots/nor
hello=tests/roms/hello/hello.gba
world=tests/roms/savetest/flash128.gba
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n NOR "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" "$hello" ::/Hello.gba
mcopy -i "$cart/sd.img" "$world" ::/World.gba

# Cart trace on stdout; errors still show, the kernel's tlog lines don't.
boot() {
  OMEGA_TRACE=1 OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" 2>&1 | grep -v '^kernel: ' || true
}

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# nor_holds OFFSET ROM: the game NOR holds ROM at OFFSET
nor_holds() {
  cmp -s -n "$(stat -c %s "$2")" <(tail -c +$(($1 + 1)) "$cart/game-nor.bin") "$2"
}

sram_record() {
  od -A n -t x1 -N 5 "$cart/sram.bin" | tr -d ' \n'
}

# Power-on 1: Hello.gba sorts first. Open it, WRITE TO NOR CLEAN (two down).
# shellcheck disable=SC2046 # $(keys) is a list of options
boot $(keys 100 A DOWN DOWN A) -s 400:$shots/1-nor-list.png -f 400 >/dev/null
nor_holds 0 "$hello" || fail "Hello.gba was not written to NOR at 0"

# Power-on 2: R to the NOR list, open Hello, boot it.
# shellcheck disable=SC2046
log=$(boot $(keys 100 R A A) -s 500:$shots/2-hello.png -f 500)
grep -q 'mode game from NOR offset 000000$' <<<"$log" || fail "Hello did not boot from NOR at 0"
[ "$(sram_record)" = 4f4d525801 ] || fail "Hello did not run from NOR (SRAM $(sram_record))"

# Power-on 3: confirm the save copy to SAVER/Hello.sav, then down past
# SAVER and Hello to World.gba and write it to NOR after Hello.
# shellcheck disable=SC2046
boot -k 100:A -k 104:- $(keys 200 DOWN DOWN A DOWN DOWN A) -s 600:$shots/3-nor-list.png -f 600 >/dev/null
[ "$(mtype -i "$cart/sd.img" ::/SAVER/Hello.sav | head -c 5 | od -A n -t x1 | tr -d ' \n')" = 4f4d525801 ] ||
  fail "Hello's NOR save was not written back to SD"
nor_holds 0x20000 "$world" || fail "World.gba was not written to NOR at 0x20000"
nor_holds 0 "$hello" || fail "writing World.gba changed Hello.gba"

# Power-on 4: second NOR game.
# shellcheck disable=SC2046
log=$(boot $(keys 100 R DOWN A A) -s 500:$shots/4-world.png -f 500)
grep -q 'mode game from NOR offset 020000$' <<<"$log" || fail "World did not boot from NOR at 0x20000"

# Power-on 5: Hello from NOR again gets its save back and adds a boot.
# shellcheck disable=SC2046
log=$(boot -k 100:A -k 104:- $(keys 200 R A A) -s 600:$shots/5-hello.png -f 600)
grep -q 'mode game from NOR offset 000000$' <<<"$log" || fail "Hello did not boot from NOR again"
[ "$(sram_record)" = 4f4d525802 ] || fail "Hello's save did not come back on a NOR boot (SRAM $(sram_record))"

echo "PASS: two games written to the game NOR boot from it after power cycles, and keep their saves"
