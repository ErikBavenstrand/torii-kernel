# Builds one save test ROM per save chip (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall
VARIANTS := flash128 flash64 eeprom8k eeprom512

all: $(VARIANTS:%=%.gba)

%.gba: %.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tSAVETEST -cOMRS -mOR $@ >/dev/null

flash128.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -DSAVE_FLASH128 -specs=gba.specs -o $@ $<
flash64.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -DSAVE_FLASH64 -specs=gba.specs -o $@ $<
eeprom8k.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -DSAVE_EEPROM8K -specs=gba.specs -o $@ $<
eeprom512.elf: main.c
	$(PREFIX)gcc $(CFLAGS) -DSAVE_EEPROM512 -specs=gba.specs -o $@ $<

clean:
	rm -f *.elf *.gba
