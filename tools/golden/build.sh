#!/usr/bin/env bash
# Builds the golden sets' routines (tools/golden/*.bin) with the kernel's
# devkitARM: shop.c (C, Thumb), and the hand-written .s files.
# Run from the repo: tools/golden/build.sh
set -euo pipefail
image=$(sed -n 's/^kernel_image := "\(.*\)"/\1/p' justfile)
DOCKER_CONTEXT=${DOCKER_CONTEXT:-colima-torii} docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src/tools/golden "$image" bash -c '
set -e
bin=/opt/devkitpro/devkitARM/bin/arm-none-eabi
$bin-gcc -mthumb -mcpu=arm7tdmi -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -Wall -Wextra -Werror \
  -ffunction-sections -c shop.c -o /tmp/shop.o
$bin-ld -T shop.ld /tmp/shop.o -o /tmp/shop.elf
$bin-objcopy -O binary -j .text /tmp/shop.elf shop.bin
$bin-objdump -d /tmp/shop.elf | grep -c "" >/dev/null
'
echo "shop.bin: $(stat -c %s tools/golden/shop.bin 2>/dev/null || stat -f %z tools/golden/shop.bin) bytes"
