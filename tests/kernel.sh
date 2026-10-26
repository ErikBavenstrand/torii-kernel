#!/usr/bin/env bash
# Runs the kernel unit tests: the test kernel runs tests/kernel at boot and
# logs PASS/FAIL lines through tlog. Runs inside the torii-emu container.
set -uo pipefail

out=$(emu/build-linux/mgba-shot ezkerneltest.gba -u 'TESTS DONE' -f 2400 2>&1)
grep -E '^kernel( error)?: ' <<<"$out" | sed 's/^kernel: //; s/^kernel error: /  /' | grep -v '^Torii kernel boot'
if ! grep -q 'TESTS DONE: [0-9]* passed, 0 failed' <<<"$out"; then
  grep -q 'TESTS DONE' <<<"$out" || echo "$out" | tail -20
  echo "FAIL: kernel unit tests"
  exit 1
fi
echo "PASS: kernel unit tests"
