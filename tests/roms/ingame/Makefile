# Builds ingame.gba, the in-game probe (source/ingameprobe.h), with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall -Wextra -Werror -isystem /opt/devkitpro/libgba/include

all: ingame.gba

ingame.gba: ingame.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tINGAMEPROBE -cTIGP -mOR $@ >/dev/null

ingame.elf: main.c ../../../source/ingameprobe.h ../../../source/cartlog.h
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ main.c

clean:
	rm -f *.elf *.gba
