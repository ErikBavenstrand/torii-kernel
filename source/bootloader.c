#include "bootloader.h"

int kernel_nor_record_ok(u32 offset, u32 size)
{
	if (size == 0 || size > 0x10000 || offset < KERNEL_NOR_DATA || offset >= KERNEL_NOR_WINDOW ||
	    size > KERNEL_NOR_WINDOW - offset)
		return 0;
	if (offset < KERNEL_NOR_SMALL)
		return !(offset & 0xFFFF);
	// The boot sectors: one 8 KB sector each
	return !(offset & (KERNEL_NOR_SMALL_SECTOR - 1)) && size <= KERNEL_NOR_SMALL_SECTOR;
}

static int blank(const u8 *p, u32 size)
{
	for (u32 i = 0; i < size; i++)
		if (p[i] != 0xFF)
			return 0;
	return 1;
}

enum loader_state loader_check(const u8 *image)
{
	if (blank(image, LOADER_SIZE))
		return LOADER_BLANK;
	if (blank(image, KERNEL_NOR_SMALL_SECTOR))
		return LOADER_DAMAGED;
	u8 sum = 0;
	for (u32 i = 0xA0; i < 0xBD; i++)
		sum += image[i];
	if (image[0xB2] != 0x96 || (u8)(sum + image[0xBD] + 0x19) != 0)
		return LOADER_NO_HEADER;
	return LOADER_OK;
}

const char *loader_state_name(enum loader_state state)
{
	switch (state) {
	case LOADER_BLANK:
		return "BLANK";
	case LOADER_DAMAGED:
		return "DAMAGED, its first 8 KB erased";
	case LOADER_NO_HEADER:
		return "OVERWRITTEN, no GBA header";
	default:
		return "present, with a GBA header";
	}
}
