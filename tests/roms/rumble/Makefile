# Builds rumble.gba, the rumble probe, with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall -Wextra -Werror

all: rumble.gba

rumble.gba: rumble.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tRUMBLEPROBE -cORMR -mOR $@ >/dev/null

rumble.elf: main.c ../../../source/rumbleprobe.h
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ main.c

clean:
	rm -f *.elf *.gba
