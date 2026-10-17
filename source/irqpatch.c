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

// Games that overwrite the in-game hook at 0x3007FFC in ways the general
// patching can't find without running them. All are the EU dumps tested in
// docs/research/hook-coverage.md; other regions and revisions don't match.
const ST_rom_patch rom_patches[] = {
	// Grand Theft Auto Advance: a DMA3 fill of all of IWRAM with 0xEF after the
	// hook is in (destination in a register). Stop it at 0x3007FF0.
	{{'B', 'G', 'T', 'P'}, 0x00, 0xAC, 0x584, 0x85002000, 0x85001FFC},
	// Super Monkey Ball Jr.: a DMA3 fill from 0x3007600 to the end of IWRAM.
	// Stop it at 0x3007FF0.
	{{'A', 'L', 'U', 'P'}, 0x00, 0x15, 0x36070, 0x85000280, 0x8500027C},
	// LEGO Star Wars II: crt0 clears IWRAM up to this end address, after the
	// hook is in. Stop it at 0x3007FF0.
	{{'B', 'L', '7', 'P'}, 0x00, 0xE0, 0x14C, 0x03008000, 0x03007FF0},
	// Lara Croft Tomb Raider: Legend: computes 0x3008000 - 4 for its IRQ
	// handler store. The Thumb subs r0,#4 at 0x14A2 becomes subs r0,#12, so the
	// handler goes to 0x3007FF4; r0 is reloaded before its next use.
	{{'B', 'L', '8', 'P'}, 0x00, 0xB8, 0x14A0, 0x38046008, 0x380C6008},
};
const u32 rom_patch_count = sizeof(rom_patches) / sizeof(*rom_patches);

int Rom_patch_matches(const ST_rom_patch *p, const u8 *header)
{
	for (int i = 0; i < 4; i++)
		if (header[0xAC + i] != (u8)p->code[i])
			return 0;
	return header[0xBC] == p->version && header[0xBD] == p->checksum;
}
