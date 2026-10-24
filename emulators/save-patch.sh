#!/usr/bin/env bash
# Regenerate patches/NAME.patch from the changes in emulators/NAME.
# Usage: save-patch.sh NAME
set -euo pipefail
cd "$(dirname "$0")"

name=$1
git -C "$name" add -A
git -C "$name" diff --cached --binary >"patches/$name.patch"
sha256sum <"patches/$name.patch" | cut -d' ' -f1 >".$name-patch" # see fetch.sh
echo "updated patches/$name.patch"
