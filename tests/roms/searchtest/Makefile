# Builds searchtest.gba with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall

searchtest.gba: searchtest.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tOMEGASEARCH -cOMSE -mOR $@

searchtest.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ $<

clean:
	rm -f searchtest.elf searchtest.gba
