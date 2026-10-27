// The launch table's check (fattable.h).
#include <stddef.h>

#include "crc32.h"
#include "fattable.h"

int fat_runs(const u32 *table, u32 from, u32 to)
{
	if (from + 3 <= to && table[from] == 0 && table[from + 1] == 0 && table[from + 2] == FAT_TABLE_END)
		return 0;  // no file
	int runs = 0;
	for (u32 i = from; i + 1 < to; i += 2) {
		if (table[i] == FAT_TABLE_END)
			return runs ? runs : -1;
		if (table[i + 1] == 0)
			return -1;  // the card's first sector: its partition table
		if (runs == 0 ? table[i] != 0 : table[i] <= table[i - 2])
			return -1;
		runs++;
	}
	return -1;
}

u32 fat_table_rts_crc(const u32 *table)
{
	return crc32(table + FAT_TABLE_RTS_WORD, FAT_TABLE_RTS_WORDS * 4);
}

int fat_table_safe(const u32 *table, const u32 *sink_crc, const char **why)
{
	const char *bad = NULL;
	if (fat_runs(table, 0, FAT_TABLE_PARAM_WORD) < 0)
		bad = "the game's runs have no end marker before the parameters";
	else if (!sink_crc)
		bad = "no state file's runs checked";
	else if (fat_runs(table, FAT_TABLE_RTS_WORD, FAT_TABLE_WORDS) < 1)
		bad = "no state file's runs from 0x300";
	else if (fat_table_rts_crc(table) != *sink_crc)
		bad = "the words from 0x300 aren't the state file's checked runs";
	if (why)
		*why = bad;
	return bad == NULL;
}
