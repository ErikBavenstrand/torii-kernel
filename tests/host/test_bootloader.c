// The kernel NOR's writable range and the boot loader check (source/bootloader.c).
#include <string.h>

#include "../../source/bootloader.h"
#include "../../source/norlist.h"
#include "../../source/setrecord.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_kernel_nor_records(void)
{
	// The records the kernel writes: save info, NOR list copies, settings
	CHECK(kernel_nor_record_ok(0x790000, 0x200));
	CHECK(kernel_nor_record_ok(NOR_LIST_A, NOR_LIST_RECORD));
	CHECK(kernel_nor_record_ok(NOR_LIST_B, NOR_LIST_RECORD));
	CHECK(kernel_nor_record_ok(0x7B0000, 0x200));
	CHECK(kernel_nor_record_ok(KERNEL_NOR_DATA, 0x10000));
	// v0.2.1 and v0.2.2's copy B: the boot loader's alias
	CHECK(!kernel_nor_record_ok(0x7C0000, NOR_LIST_RECORD));
	for (u32 o = KERNEL_NOR_WINDOW; o < 0x800000; o += 0x10000)
		CHECK(!kernel_nor_record_ok(o, 2));
	// Wrapped or past the chip
	CHECK(!kernel_nor_record_ok(0x800000, 2));
	CHECK(!kernel_nor_record_ok(0xFFFF0000, 0x20000));
	// The kernel itself, and unaligned sectors
	CHECK(!kernel_nor_record_ok(0, 0x200));
	CHECK(!kernel_nor_record_ok(0x770000, 0x200));
	CHECK(!kernel_nor_record_ok(0x790002, 0x200));
	CHECK(!kernel_nor_record_ok(0x798000, 0x200));
	// Sizes: none, over a sector, over the small top sectors' 8 KB
	CHECK(!kernel_nor_record_ok(0x790000, 0));
	CHECK(!kernel_nor_record_ok(0x790000, 0x10002));
	CHECK(!kernel_nor_record_ok(0x7B0000, 0x2002));
	CHECK(!kernel_nor_record_ok(0x7B0000, 0xFFFFFFFF));
	CHECK(kernel_nor_record_ok(0x7B0000, 0x2000));
	// The settings' copy B, and the other 8 KB boot sectors, up to the window's end
	CHECK(kernel_nor_record_ok(SET_RECORD_B, SET_RECORD_BYTES));
	CHECK(kernel_nor_record_ok(0x7BE000, 0x2000));
	CHECK(!kernel_nor_record_ok(0x7BE000, 0x2002));
	CHECK(!kernel_nor_record_ok(0x7B1000, 0x200));
	CHECK(!kernel_nor_record_ok(0x7BF000, 0x200));
	CHECK(!kernel_nor_record_ok(0x7B2002, 0x200));
	CHECK(!kernel_nor_record_ok(0x7BFFFE, 2));
	// In place (no erase): the record's slots anywhere in the data, never
	// past the window (the loader) or below the data (the kernel)
	CHECK(kernel_nor_program_ok(0x790200, 0x200));
	CHECK(kernel_nor_program_ok(0x79FE00, 0x200));
	CHECK(kernel_nor_program_ok(KERNEL_NOR_WINDOW - 2, 2));
	CHECK(!kernel_nor_program_ok(KERNEL_NOR_WINDOW - 0x100, 0x200));
	CHECK(!kernel_nor_program_ok(KERNEL_NOR_WINDOW, 2));
	CHECK(!kernel_nor_program_ok(KERNEL_NOR_DATA - 2, 4));
	CHECK(!kernel_nor_program_ok(0x790001, 0x200));
	CHECK(!kernel_nor_program_ok(0x790200, 0));
	CHECK(!kernel_nor_program_ok(0x790000, 0xFFFFFFFFu));
}

static u8 image[LOADER_SIZE];

void test_loader_check(void)
{
	memset(image, 0xFF, sizeof(image));
	CHECK_EQ(loader_check(image), LOADER_BLANK);
	// v0.2.2's erase: the first 8 KB sector blank, the rest left
	memset(image + KERNEL_NOR_SMALL_SECTOR, 0x5A, 0x1000);
	CHECK_EQ(loader_check(image), LOADER_DAMAGED);
	memset(image, 0x5A, KERNEL_NOR_SMALL_SECTOR);
	CHECK_EQ(loader_check(image), LOADER_NO_HEADER);
	memcpy(image + 0xA0, "TORII LOADER", 12);
	image[0xB2] = 0x96;
	u8 sum = 0;
	for (u32 i = 0xA0; i < 0xBD; i++)
		sum += image[i];
	image[0xBD] = (u8) - (sum + 0x19);
	CHECK_EQ(loader_check(image), LOADER_OK);
	image[0xBD] ^= 1;
	CHECK_EQ(loader_check(image), LOADER_NO_HEADER);
	CHECK(strstr(loader_state_name(LOADER_BLANK), "BLANK") != NULL);
}
