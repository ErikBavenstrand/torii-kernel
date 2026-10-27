# Builds the in-game overlay (docs/design/v0.8/ingame-menu.md) into
# $(OUT)/overlay.bin, which source/ingame_overlay.s embeds in the kernel. The
# kernel's Makefile runs it, inside the devkitARM container.
PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
SRC := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
TOP := $(abspath $(SRC)/../..)
OUT ?= $(SRC)/build

# As the kernel's own C: -Werror and the extra warnings; paths kept out
CFLAGS := -g -Os -mthumb -mthumb-interwork -mcpu=arm7tdmi -ffreestanding -fno-strict-aliasing \
	-ffunction-sections -fdata-sections -fstack-usage -MMD -MP \
	-Werror -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes \
	-ffile-prefix-map=$(TOP)/=
ASFLAGS := -g -mthumb-interwork -Wa,--debug-prefix-map=$(TOP)/=

OBJS := $(OUT)/ov_header.o $(OUT)/ov.o $(OUT)/shotpack.o

$(OUT)/overlay.bin: $(OUT)/overlay.elf
	$(PREFIX)objcopy -O binary $< $@

$(OUT)/overlay.elf: $(OBJS) $(SRC)/ov.ld
	$(PREFIX)gcc -mthumb -mthumb-interwork -nostdlib -T $(SRC)/ov.ld -Wl,--gc-sections -Wl,-Map,$(OUT)/overlay.map -o $@ $(OBJS) -lgcc

$(OUT)/%.o: $(SRC)/%.c | $(OUT)
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

# The screenshots' packing, which the kernel unpacks: ARM, run from IWRAM (ov.ld)
$(OUT)/shotpack.o: $(TOP)/source/shotpack.c $(SRC)/Makefile | $(OUT)
	$(PREFIX)gcc $(filter-out -mthumb,$(CFLAGS)) -marm -c $< -o $@

$(OUT)/%.o: $(SRC)/%.s | $(OUT)
	$(PREFIX)gcc $(ASFLAGS) -c $< -o $@

$(OUT):
	mkdir -p $@

-include $(OUT)/*.d
