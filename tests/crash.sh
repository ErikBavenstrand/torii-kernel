#!/usr/bin/env bash
# Crash report test: the crash test ROM jumps to cart SRAM from a chain of
# calls; the headless runner must stop with status 3 and name the chain.
# Runs inside the torii-emu container after `just test-roms`.
set -uo pipefail

out=$(emu/build-linux/mgba-shot tests/roms/crash/crash.gba -f 300 2>&1)
status=$?

fail() {
  echo "FAIL: $*" >&2
  echo "$out" >&2
  exit 1
}

[ $status -eq 3 ] || fail "expected exit status 3, got $status"
grep -q '^crash at frame [0-9]*: Jumped to invalid address: 0E000000' <<<"$out" || fail "no crash line"
grep -q 'lr  .*(jump_to_sram+' <<<"$out" || fail "lr not resolved to jump_to_sram"
[ "$(grep -c '(crash_here+' <<<"$out")" -ge 2 ] || fail "call chain through crash_here missing"
grep -q '(main+' <<<"$out" || fail "main missing from the call chain"
echo "PASS: crash report names the call chain"
