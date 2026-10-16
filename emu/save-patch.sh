#!/usr/bin/env bash
# Regenerate patches/0001-omega-de-cart-model.patch from changes in emu/mgba.
set -euo pipefail
cd "$(dirname "$0")"

git -C mgba add -A
git -C mgba diff --cached >patches/0001-omega-de-cart-model.patch
echo "updated patches/0001-omega-de-cart-model.patch"
