# Builds the showcase card's stand-in game into $(OUT) (run inside the
# devkitARM container by `just media`).
PREFIX := /opt/devkitpro/devkitARM/bin/arm-none-eabi-
CFLAGS := -O2 -mthumb -mthumb-interwork -mcpu=arm7tdmi -Wall
OUT ?= ../../../dist/media

$(OUT)/scene.gba: $(OUT)/scene.elf
	$(PREFIX)objcopy -O binary $< $@
	/opt/devkitpro/tools/bin/gbafix -tTORIISCENE -cTRSC -mTR $@

$(OUT)/scene.elf: main.c scene.s $(OUT)/scene.bin
	$(PREFIX)gcc $(CFLAGS) -specs=gba.specs -o $@ main.c \
		-x assembler-with-cpp -DSCENE='"$(OUT)/scene.bin"' scene.s
