#!/usr/bin/env bash
# Builds the hardware-free kernel code and tests/host for the host with
# AddressSanitizer and UndefinedBehaviorSanitizer, then runs the tests; any
# sanitizer report fails them. Runs inside the torii-emu container.
set -euo pipefail

mkdir -p dist/host
# -funsigned-char: char is unsigned on ARM, signed on x86 hosts
cc -std=gnu11 -g -O1 -fno-omit-frame-pointer -funsigned-char \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -I tests/host -o dist/host/tests \
  source/sort.c source/cheat.c source/charset.c source/irqpatch.c source/savename.c source/logbuf.c source/lz77.c source/logcheck.c source/eepromprobe.c source/norlist.c source/bootloader.c source/crc32.c \
  source/engine/text_layout.c source/engine/font_body.c source/engine/font_heading.c tests/host/*.c
# A hung test (an endless loop in a sort, say) fails after a minute, and
# output is capped: ASan can loop printing "AddressSanitizer:DEADLYSIGNAL"
# when the kernel randomises mmap with more bits than it supports (CI's
# x86 runners use 32; the workflows lower vm.mmap_rnd_bits to 28).
set +e
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 timeout 60 dist/host/tests 2>&1 | head -c 65536
status=${PIPESTATUS[0]}
set -e
if [ "$status" = 0 ]; then
  echo "PASS: host unit tests"
else
  echo "FAIL: host unit tests (exit $status)"
  exit 1
fi
