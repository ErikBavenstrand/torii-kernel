// Find_irq_table_entry and Hotkey_value (source/irqpatch.c), with the kernel's
// real table. The tables tested here are heap copies, so AddressSanitizer
// catches a read past their end.
#include <stdlib.h>
#include <string.h>

#include "../../source/irqpatch.h"
#include "../../source/reset_table.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static int find(const u32 *table, u32 words, const char *code, const u32 **offsets)
{
	u32 *copy = malloc(words * 4 + 1);
	memcpy(copy, table, words * 4);
	u32 pos = 0;
	int count = Find_irq_table_entry(copy, words, (const u8 *)code, &pos, offsets);
	if (count > 0)
		*offsets = &table[*offsets - copy];  // point into the caller's table
	free(copy);
	return count;
}

void test_irq_table_lookup(void)
{
	const u32 *table = (const u32 *)reset_table;
	u32 words = sizeof(reset_table) / 4;
	const u32 *offsets = NULL;

	CHECK_EQ(find(table, words, "AFZJ", &offsets), 2);  // the first entry, F-Zero (JP)
	CHECK_EQ(offsets[0], 0x8D);
	CHECK_EQ(offsets[1], 0x123B6);
	CHECK_EQ(find(table, words, "BPRE", &offsets), 3);  // FireRed (US), rev 0's offsets
	CHECK_EQ(offsets[0], 0x90);
	CHECK_EQ(find(table, words, "A9MP", &offsets), 0);  // Moto Racer: listed, nothing to patch
	CHECK_EQ(find(table, words, "ZZZZ", &offsets), -1);
}

void test_irq_table_rejects_blank_codes(void)
{
	const u32 *table = (const u32 *)reset_table;
	u32 words = sizeof(reset_table) / 4;
	const u32 *offsets;

	// The table has an all-zero entry, which homebrew with no code matched.
	CHECK_EQ(find(table, words, "\0\0\0\0", &offsets), -1);
	CHECK_EQ(find(table, words, "    ", &offsets), -1);
	CHECK_EQ(find(table, words, "bpre", &offsets), -1);  // not listed
	CHECK_EQ(find(table, words, "tvap", &offsets), 0);   // TV Tuner PAL (CN), a lowercase code
}

void test_irq_table_next_entry(void)
{
	// A code listed twice, for two revisions: each call finds the next one.
	static const u32 table[] = {0x41414141, 1, 0x10, 0x42424242, 0, 0x41414141, 2, 0x20, 0x24};
	const u32 *offsets;
	u32 pos = 0;

	CHECK_EQ(Find_irq_table_entry(table, 9, (const u8 *)"AAAA", &pos, &offsets), 1);
	CHECK_EQ(offsets[0], 0x10);
	CHECK_EQ(Find_irq_table_entry(table, 9, (const u8 *)"AAAA", &pos, &offsets), 2);
	CHECK_EQ(offsets[1], 0x24);
	CHECK_EQ(Find_irq_table_entry(table, 9, (const u8 *)"AAAA", &pos, &offsets), -1);
}

void test_irq_table_bounds(void)
{
	static const u32 table[] = {0x41414141, 1, 0x10, 0x42424242, 5, 0x20};
	const u32 *offsets;

	CHECK_EQ(find(table, 6, "AAAA", &offsets), 1);
	CHECK_EQ(offsets[0], 0x10);
	// BBBB claims 5 offsets but the table ends after 1.
	CHECK_EQ(find(table, 6, "BBBB", &offsets), -1);
	CHECK_EQ(find(table, 5, "CCCC", &offsets), -1);
	CHECK_EQ(find(table, 0, "AAAA", &offsets), -1);
	CHECK_EQ(find(table, 1, "AAAA", &offsets), -1);
}

void test_irq_literals(void)
{
	CHECK(Is_irq_literal(0x03007FFC));
	CHECK(Is_irq_literal(0x03FFFFFC));
	CHECK(!Is_irq_literal(0x03007FF4));
	CHECK(!Is_irq_literal(0));
}

void test_hotkey_value(void)
{
	static const u16 sleep[3] = {9, 8, 2}, menu[3] = {9, 8, 3};

	// L+R+START held: KEYINPUT reads 0 for held keys.
	CHECK_EQ(Hotkey_value((const u16[]){9, 8, 3}, menu), 0x0F7);
	CHECK_EQ(Hotkey_value((const u16[]){9, 8, 2}, sleep), 0x0FB);
	CHECK_EQ(Hotkey_value((const u16[]){0, 1, 2}, sleep), 0x3F8);
	// Erased slots read 0xFFFF and fall back to the keys the settings page
	// shows; before, 1 << 0xFFFF made the key "no key held".
	CHECK_EQ(Hotkey_value((const u16[]){0xFFFF, 0xFFFF, 0xFFFF}, menu), 0x0F7);
	CHECK_EQ(Hotkey_value((const u16[]){10, 8, 0xFFFF}, sleep), 0x0FB);
}

void test_rom_patch_table(void)
{
	for (u32 i = 0; i < rom_patch_count; i++) {
		const ST_rom_patch *p = &rom_patches[i];
		CHECK_EQ(p->offset % 4, 0);
		CHECK(p->offset + 4 <= 0x800000);  // read through the first PSRAM window
		CHECK(p->offset >= 0xC0);          // not the header the match reads
		CHECK(p->expected != p->value);
		for (int c = 0; c < 4; c++)
			CHECK((p->code[c] >= '0' && p->code[c] <= '9') || (p->code[c] >= 'A' && p->code[c] <= 'Z'));
		for (u32 j = 0; j < i; j++)  // one entry per site
			CHECK(memcmp(p->code, rom_patches[j].code, 4) || p->offset != rom_patches[j].offset);
	}
}

void test_rom_patch_matches(void)
{
	const ST_rom_patch *p = &rom_patches[0];
	u8 header[0xC0] = {0};

	memcpy(&header[0xAC], p->code, 4);
	header[0xBC] = p->version;
	header[0xBD] = p->checksum;
	CHECK(Rom_patch_matches(p, header));
	header[0xBC] = p->version + 1;  // another revision
	CHECK(!Rom_patch_matches(p, header));
	header[0xBC] = p->version;
	header[0xBD] = p->checksum ^ 1;  // a hack with another title
	CHECK(!Rom_patch_matches(p, header));
	header[0xBD] = p->checksum;
	header[0xAF] = 'E';  // another region
	CHECK(!Rom_patch_matches(p, header));
}
