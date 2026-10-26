# Builds cheattest.gba (and cheattest.sym, its symbols, for tests/cheat_engine.sh), and
# cheatirq.gba and cheatirq-vblank.gba (tests/cheat_safety.sh irq), with devkitARM (run
# inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall -Wextra -Werror

all: cheattest.gba cheattest.nm cheatirq.gba cheatirq-vblank.gba

cheattest.gba: cheattest.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tCHEATTEST -cOMCT -mOR $@ >/dev/null

cheattest.nm: cheattest.elf
	$(PREFIX)nm -n $< > $@

cheattest.elf: main.c hook.s
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ main.c hook.s

cheatirq.elf: irq.c
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ irq.c

cheatirq-vblank.elf: irq.c
	$(PREFIX)gcc $(CFLAGS) -DVBLANK_ONLY -specs=gba.specs -o $@ irq.c

cheatirq.gba cheatirq-vblank.gba: %.gba: %.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tCHEATIRQ -cOMCI -mOR $@ >/dev/null

clean:
	rm -f *.elf *.gba *.nm *.sym
