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
	int count = Find_irq_table_entry(copy, words, (const u8 *)code, offsets);
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
	CHECK_EQ(find(table, words, "bpre", &offsets), -1);
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
