#include "irqpatch.h"

int Is_irq_literal(u32 word)
{
	return word == 0x03007FFC || word == 0x03FFFFFC;
}

static int is_code_char(u8 c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

int Find_irq_table_entry(const u32 *table, u32 words, const u8 code[4], const u32 **offsets)
{
	u32 want = code[0] | (code[1] << 8) | (code[2] << 16) | ((u32)code[3] << 24);
	u32 i = 0;

	for (int c = 0; c < 4; c++)
		if (!is_code_char(code[c]))
			return -1;
	while (i + 1 < words) {
		u32 count = table[i + 1];
		if (count > words - i - 2)
			return -1;  // a truncated entry ends the table
		if (table[i] == want) {
			*offsets = &table[i + 2];
			return (int)count;
		}
		i += count + 2;
	}
	return -1;
}

u16 Hotkey_value(const u16 slots[3], const u16 defaults[3])
{
	u16 held = 0;

	for (int k = 0; k < 3; k++)
		held |= 1 << (slots[k] <= 9 ? slots[k] : defaults[k]);
	return ~held & 0x3FF;
}
