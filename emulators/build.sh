#!/usr/bin/env bash
# Build one emulator into emulators/out/NAME.gba, in the devkitARM container
# with the repo at /src. It builds from a copy in a folder of the same name
# every time, so the checkout keeps no build files and the result doesn't
# depend on where the repo is. Usage: build.sh NAME
set -euo pipefail

name=$1
work=/tmp/torii-emulator/$name
rm -rf "$work" && mkdir -p "$work"
git -C "/src/emulators/$name" ls-files -z | (cd "/src/emulators/$name" && xargs -0 cp --parents -t "$work")
# Neither uses __DATE__ in what it links, but a fixed date keeps it so
export SOURCE_DATE_EPOCH=0
# The Makefiles call make again for their sub-builds: one job keeps the
# jobserver out of it
make -C "$work" -j1 >"$work.log" 2>&1 || {
  cat "$work.log"
  exit 1
}
# The emulator finds the game at __rom_end__ (its textstart), and the kernel
# puts it right after the file: pad the file to there
rom_end=$(/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm "$work/$name.elf" | awk '$3 == "__rom_end__" { print $1 }')
end=$((0x$rom_end - 0x08000000))
size=$(stat -c %s "$work/$name.gba")
if [ "$size" -gt "$end" ] || [ $((end % 4)) -ne 0 ]; then
  echo "error: $name.gba is $size bytes, but __rom_end__ is at $end" >&2
  exit 1
fi
truncate -s "$end" "$work/$name.gba"
mkdir -p /src/emulators/out
cp "$work/$name.gba" "/src/emulators/out/$name.gba"
echo "built emulators/out/$name.gba ($end bytes)"
