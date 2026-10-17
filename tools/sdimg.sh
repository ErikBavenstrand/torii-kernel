#!/usr/bin/env bash
# Runs tools/sdimg.c (SD images through the kernel's own FatFs, with exFAT and
# UTF-8 names), building it first if needed: sdimg.sh IMG COMMAND ARGS...
# Runs inside the torii-emu container.
set -euo pipefail

out=dist/sdimg
bin=$out/sdimg
if [ ! -x "$bin" ] || [ -n "$(find tools/sdimg.c tools/sdimg.sh source/ff15 -newer "$bin" 2>/dev/null)" ]; then
  mkdir -p "$out"
  # Tests run in parallel and may all build at once: each copies the sources
  # into its own folder, builds there and renames the binary into place, so
  # none sees another's half-written header or runs a half-written binary.
  tmp=$(mktemp -d "$out/build.XXXXXX")
  trap 'rm -rf "$tmp"' EXIT
  cp source/ff15/ff.c source/ff15/ff.h source/ff15/ffunicode.c source/ff15/diskio.h "$tmp/"
  # The kernel's FatFs settings, plus f_mkfs, labels, the UTF-8 API and no clock
  sed -e 's/^#define FF_USE_MKFS\t\t0/#define FF_USE_MKFS\t\t1/' \
    -e 's/^#define FF_LFN_UNICODE\t[0-9]/#define FF_LFN_UNICODE\t2/' \
    -e 's/^#define FF_FS_NORTC\t\t0/#define FF_FS_NORTC\t\t1/' \
    -e 's/^#define FF_USE_LABEL\t0/#define FF_USE_LABEL\t1/' \
    source/ff15/ffconf.h >"$tmp/ffconf.h"
  cc -std=gnu11 -O1 -g -Wall -I "$tmp" -o "$tmp/sdimg" tools/sdimg.c "$tmp/ff.c" "$tmp/ffunicode.c"
  mv -f "$tmp/sdimg" "$bin"
  rm -rf "$tmp"
  trap - EXIT
fi
exec "$bin" "$@"
