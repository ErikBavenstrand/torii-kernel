#!/usr/bin/env bash
# Rebuild the kernel from a copy of the source in a tmpfs, where directory
# listings follow creation order, after creating the files in random order.
# A build that depends on the order make lists files in, or on the folder it
# runs in, then differs from the normal one. Runs in the devkitARM container
# with the repo at /src. Usage: repro.sh OUT_DIR FILE_LIST
set -euo pipefail

out=$1 list=$2
work=/work/a-different-folder
mkdir -p "$work"
shuf "$list" | while IFS= read -r file; do
  mkdir -p "$work/$(dirname "$file")"
  cp "/src/$file" "$work/$file"
done
echo "files in $work/source, in the order make sees them:"
find "$work/source" -maxdepth 1 -name '*.c' -printf '%f '
echo
make -C "$work" TARGET=ezkernelnew -j8 >/dev/null
cp "$work/ezkernelnew.gba" "$work/ezkernelnew.elf" "/src/$out/"
