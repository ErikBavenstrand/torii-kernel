#!/usr/bin/env bash
# Build one emulator into emulators/out/NAME.gba, in the devkitARM container
# with the repo at /src. It builds from a copy in a folder of the same name
# every time, so the checkout keeps no build files and the result doesn't
# depend on where the repo is. Usage: build.sh NAME [OUT_DIR]
# With SHUFFLE=1 (`just repro`, under a tmpfs at /work) the copy is made
# in random order, where directory listings follow creation order: a build
# that depends on the order make lists files in then differs.
set -euo pipefail

name=$1
out=${2:-/src/emulators/out}
if [ -n "${SHUFFLE:-}" ]; then work=/work/$name; else work=/tmp/torii-emulator/$name; fi
rm -rf "$work" && mkdir -p "$work"
# Not through git: fetch.sh rewrites the checkout's index right before, and
# the share can show a container the old one for a second
if [ -n "${SHUFFLE:-}" ]; then
  (cd "/src/emulators/$name" && find . -name .git -prune -o -type f -print | shuf) | while IFS= read -r file; do
    mkdir -p "$work/$(dirname "$file")"
    cp -p "/src/emulators/$name/$file" "$work/$file"
  done
else
  tar -C "/src/emulators/$name" --exclude=.git -cf - . | tar -C "$work" -xf -
fi
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
mkdir -p "$out"
cp "$work/$name.gba" "$out/$name.gba"
cp "$work/$name.elf" "$out/$name.elf" # symbols, for debugging
echo "built $out/$name.gba ($end bytes)"
