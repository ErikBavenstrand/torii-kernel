#include "build_id.h"

#include "Ezcard_OP.h"

extern const u8 __rom_end__;  // the linker's: the end of the kernel's ROM image

u32 build_id_compute(void)
{
	static const u8 zero[4];
	const u8 *rom = (const u8 *)0x08000000;
	const u8 *id = (const u8 *)&torii_build_id;
	u32 crc = crc32_update(0, rom, id - rom);
	crc = crc32_update(crc, zero, sizeof(zero));
	return crc32_update(crc, id + 4, &__rom_end__ - (id + 4));
}
