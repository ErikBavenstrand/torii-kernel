#!/usr/bin/env bash
# Copy the built kernel, and any games given, to a real SD card for the Omega
# DE, then eject it. Games already on the card are left alone, so their saves
# stay with them. Usage: sd_copy.sh <card mount point> [game.gba...]
set -euo pipefail

card=${1%/}
shift

# Refuse anything that isn't the root of a mounted volume, so a typo can't
# write into a normal folder.
mount_point=$(df -P "$card" 2>/dev/null | awk 'NR == 2 { print $NF }')
if [ ! -d "$card" ] || [ "$mount_point" != "$card" ] || [ "$card" = / ]; then
  echo "error: $card is not the mount point of an SD card (e.g. /Volumes/OMEGA)" >&2
  exit 1
fi

# On macOS, copy without extended attributes: they become ._ files on FAT
# cards, which the kernel would list as games.
copy=(cp)
[ "$(uname)" = Darwin ] && copy=(/bin/cp -X)

"${copy[@]}" dist/ezkernelnew.bin "$card/ezkernelnew.bin"
echo "copied: ezkernelnew.bin"
for game in "$@"; do
  name=$(basename "$game")
  if [ -e "$card/$name" ]; then
    echo "already on the card: $name"
  else
    "${copy[@]}" "$game" "$card/$name"
    echo "copied: $name"
  fi
done
[ "$(uname)" = Darwin ] && dot_clean -m "$card" 2>/dev/null
sync

if [ "$(uname)" = Darwin ]; then
  diskutil eject "$card" >/dev/null && echo "ejected $card"
elif command -v udisksctl >/dev/null; then
  udisksctl unmount -b "$(df -P "$card" | awk 'NR == 2 { print $1 }')" && echo "unmounted $card"
else
  echo "unmount $card before removing it"
fi
echo "To install the kernel: put the card in the cart and hold R while switching the GBA on."
