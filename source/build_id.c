#include "build_id.h"

#include "Ezcard_OP.h"

extern const u8 __rom_end__;  // the linker's: the end of the kernel's ROM image

u32 build_id_compute(void)
{
	static const u8 zero[4];
	// Addresses as numbers: the ROM, the ID and the linker's end aren't one object.
	u32 rom = 0x08000000, id = (u32)&torii_build_id, end = (u32)&__rom_end__;
	u32 crc = crc32_update(0, (const u8 *)rom, id - rom);
	crc = crc32_update(crc, zero, sizeof(zero));
	return crc32_update(crc, (const u8 *)(id + 4), end - (id + 4));
}
