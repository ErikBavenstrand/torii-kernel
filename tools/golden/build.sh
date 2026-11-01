#!/usr/bin/env bash
# Builds the golden sets' routines (tools/golden/*.bin) with the kernel's
# devkitARM: shop.c and field.c (C, Thumb). The .s files were assembled by hand.
# Run from the repo: tools/golden/build.sh
set -euo pipefail
image=$(sed -n 's/^kernel_image := "\(.*\)"/\1/p' justfile)
DOCKER_CONTEXT=${DOCKER_CONTEXT:-colima-torii} docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src/tools/golden "$image" bash -c '
set -e
bin=/opt/devkitpro/devkitARM/bin/arm-none-eabi
for c in shop field; do
  $bin-gcc -mthumb -mcpu=arm7tdmi -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -Wall -Wextra -Werror \
    -ffunction-sections -c $c.c -o /tmp/$c.o
  $bin-ld -T $c.ld /tmp/$c.o -o /tmp/$c.elf
  $bin-objcopy -O binary -j .text /tmp/$c.elf $c.bin
done
for a in pid; do
  $bin-as -mcpu=arm7tdmi $a.s -o /tmp/$a.o
  $bin-ld -T $a.ld /tmp/$a.o -o /tmp/$a.elf
  $bin-objcopy -O binary -j .stub /tmp/$a.elf $a.bin
done
'
for c in shop field pid; do
  echo "$c.bin: $(stat -c %s "tools/golden/$c.bin" 2>/dev/null || stat -f %z "tools/golden/$c.bin") bytes"
done
