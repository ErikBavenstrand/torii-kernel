// The game NOR list's copies and completion marks (source/norlist.c).
#include <stdlib.h>
#include <string.h>

#include "../../source/norlist.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static u8 *new_copy(u32 seq, u32 count)
{
	u8 *record = malloc(NOR_LIST_RECORD);
	memset(record, 0, NOR_LIST_RECORD);
	for (u32 i = 0; i < count; i++)
		record[i * NOR_ENTRY_SIZE] = 'a' + i;
	nor_list_seal(record, seq, count);
	return record;
}

void test_nor_list_copies(void)
{
	u8 *a = new_copy(7, 2), *b = new_copy(8, 3);
	u32 seq = 0;
	CHECK(nor_list_check(a, &seq));
	CHECK_EQ(seq, 7);
	CHECK_EQ(nor_list_pick(a, b), NOR_COPY_B);  // the newer
	CHECK_EQ(nor_list_pick(b, a), NOR_COPY_A);

	// A copy cut off before its trailer, or with a changed byte, isn't whole
	b[NOR_LIST_RECORD - 1] ^= 0xFF;
	CHECK(!nor_list_check(b, &seq));
	CHECK_EQ(nor_list_pick(a, b), NOR_COPY_A);
	b[NOR_LIST_RECORD - 1] ^= 0xFF;
	b[5] ^= 1;
	CHECK_EQ(nor_list_pick(a, b), NOR_COPY_A);
	memset(b, 0xFF, NOR_LIST_RECORD);  // erased
	CHECK_EQ(nor_list_pick(b, a), NOR_COPY_B);
	CHECK_EQ(nor_list_pick(b, b), NOR_COPY_NONE);

	// The same number in both after a save: either will do, A it is
	free(b);
	b = new_copy(7, 2);
	CHECK_EQ(nor_list_pick(a, b), NOR_COPY_A);
	// Numbers wrap
	free(a);
	free(b);
	a = new_copy(0xFFFFFFFF, 1);
	b = new_copy(0, 1);
	CHECK_EQ(nor_list_pick(a, b), NOR_COPY_B);
	// An older kernel's list: entries, no trailer
	free(b);
	b = new_copy(1, 1);
	memset(b + NOR_LIST_BYTES, 0xFF, NOR_LIST_RECORD - NOR_LIST_BYTES);
	CHECK_EQ(nor_list_pick(b, b), NOR_COPY_NONE);
	free(a);
	free(b);
}

void test_nor_mark(void)
{
	u8 header[0x20], entry[NOR_ENTRY_SIZE];
	struct nor_mark mark;
	memset(header, 'h', sizeof(header));
	memset(entry, 'e', sizeof(entry));
	nor_mark_make(&mark, 0x40000, 0x60000, header, entry);
	CHECK(nor_mark_check(&mark, 0x40000, 0xA0000, header));
	CHECK(memcmp(mark.entry, entry, sizeof(entry)) == 0);
	// Another game's place, size or header
	CHECK(!nor_mark_check(&mark, 0x20000, 0xA0000, header));
	CHECK(!nor_mark_check(&mark, 0x40000, 0x80000, header));
	header[0x1F] = 'x';
	CHECK(!nor_mark_check(&mark, 0x40000, 0xA0000, header));
	header[0x1F] = 'h';
	// Cut off while programmed: its last word still erased
	mark.crc = 0xFFFFFFFF;
	CHECK(!nor_mark_check(&mark, 0x40000, 0xA0000, header));
	// Not a whole number of blocks
	nor_mark_make(&mark, 0x40000, 0x10000, header, entry);
	CHECK(!nor_mark_check(&mark, 0x40000, 0x50000, header));
}
