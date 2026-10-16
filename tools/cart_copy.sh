#!/usr/bin/env bash
# Copy files to the root of an SD image: cart_copy.sh overwrite|keep SD_IMG FILE...
# keep leaves a file alone if one of that name is already on the card.
# Runs inside the torii-emu container.
set -euo pipefail

mode=$1 img=$2
shift 2
for f in "$@"; do
  name=$(basename "$f")
  if [ "$mode" = keep ] && mdir -i "$img" "::/$name" >/dev/null 2>&1; then
    echo "already on the card: $name"
    continue
  fi
  mcopy -o -i "$img" "$f" "::/$name"
  echo "copied: $name"
done
