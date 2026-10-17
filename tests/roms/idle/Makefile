# Builds idle.gba with devkitARM (run inside the devkitARM container, see the justfile).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-

idle.gba: idle.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tOMEGAIDLE -cOMID -mOR $@

idle.elf: idle.s
	$(PREFIX)gcc -nostdlib -Wl,-Ttext=0x08000000 -o $@ $<

clean:
	rm -f idle.elf idle.gba
