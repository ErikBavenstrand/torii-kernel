# Builds crash.gba with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall

all: crash.gba crash.nm

crash.gba: crash.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tOMEGACRASH -cOMRC -mOR $@

crash.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ $<

crash.nm: crash.elf
	$(PREFIX)nm -n $< > $@

clean:
	rm -f crash.elf crash.gba crash.nm crash.sym
