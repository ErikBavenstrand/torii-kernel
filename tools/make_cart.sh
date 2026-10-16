#!/usr/bin/env bash
# Create an emulated Omega DE cart folder: a 64 MB FAT32 SD image filled from
# <folder>. The emulator adds kernel-nor.bin, game-nor.bin and sram.bin on
# first boot. Does nothing if the cart already has an SD image.
# Usage: make_cart.sh <folder> <cart-dir>
set -euo pipefail

src=$1
cart=$2

if [ -f "$cart/sd.img" ]; then
  exit 0
fi
mkdir -p "$cart"
mkfs.fat -C -F 32 -n OMEGA "$cart/sd.img" 65536 >/dev/null
if [ -n "$(ls -A "$src")" ]; then
  mcopy -s -Q -i "$cart/sd.img" "$src"/* ::/
fi
echo "created $cart/sd.img from $src"
