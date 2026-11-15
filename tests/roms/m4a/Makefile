# Builds m4a.gba and m4a-other.gba with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall -Wextra -Werror

all: m4a.gba m4a-other.gba

%.gba: %.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tOMEGAM4A -cOMMA -mOR $@

m4a.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ $<

m4a-other.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -DOTHER_IDENT -specs=gba.specs -o $@ $<

clean:
	rm -f m4a.elf m4a.gba m4a-other.elf m4a-other.gba
