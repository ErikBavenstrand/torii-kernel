# Builds eeprom.gba, the EEPROM probe, with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall -Wextra -Werror

all: eeprom.gba

eeprom.gba: eeprom.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tEEPROMPROBE -cOMRE -mOR $@ >/dev/null

eeprom.elf: main.c ../../../source/eepromprobe.c ../../../source/eepromprobe.h
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ main.c ../../../source/eepromprobe.c

clean:
	rm -f *.elf *.gba
