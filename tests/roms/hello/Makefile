# Builds hello.gba with devkitARM (run inside the omega-remix-build container).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall

hello.gba: hello.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tOMEGATEST -cOMRX -mOR $@

hello.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ $<

clean:
	rm -f hello.elf hello.gba
