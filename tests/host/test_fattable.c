// The launch table's check (source/fattable.c): the table the real cart had
// on 2026-10-27 (nothing at 0x300, so the FPGA's walk wrapped into the
// game's runs) is refused, and one with the state file's runs passes.
#include <string.h>

#include "../../source/fattable.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static u32 table[FAT_TABLE_WORDS], sink[FAT_TABLE_RTS_WORDS];

// As init_FAT_table, Check_game_RTS_FAT and the launch's parameters leave it
static void game_table(void)
{
	memset(table, 0, sizeof(table));
	table[0] = 0, table[1] = 28352;  // /ingame.gba: one cluster
	table[2] = FAT_TABLE_END, table[3] = 0;
	table[FAT_TABLE_PARAM_WORD] = 7228;
	table[FAT_TABLE_PARAM_WORD + 1] = 1;
	table[FAT_TABLE_PARAM_WORD + 2] = 64;
	table[FAT_TABLE_PARAM_WORD + 3] = 0x11u << 24 | 0x8000;
}

void test_fattable(void)
{
	const char *why = NULL;
	memset(sink, 0, sizeof(sink));
	sink[0] = 0, sink[1] = 40000, sink[2] = 448, sink[3] = 50000, sink[4] = FAT_TABLE_END;
	CHECK_EQ(fat_runs(sink, 0, FAT_TABLE_RTS_WORDS), 2);

	// The real cart's table: nothing at 0x300
	game_table();
	CHECK_EQ(fat_runs(table, 0, FAT_TABLE_PARAM_WORD), 1);
	CHECK_EQ(fat_runs(table, FAT_TABLE_RTS_WORD, FAT_TABLE_WORDS), -1);
	CHECK(!fat_table_safe(table, sink, &why));
	CHECK(why != NULL);
	CHECK(!fat_table_safe(table, NULL, &why));

	// With the state file's runs
	memcpy(table + FAT_TABLE_RTS_WORD, sink, sizeof(sink));
	CHECK(fat_table_safe(table, sink, &why));
	CHECK(why == NULL);

	// A launch that copies no file (a NOR game, an emulator's): no runs
	memset(table, 0, FAT_TABLE_PARAM_WORD * 4);
	table[2] = FAT_TABLE_END;
	CHECK_EQ(fat_runs(table, 0, FAT_TABLE_PARAM_WORD), 0);
	CHECK(fat_table_safe(table, sink, NULL));

	// Other runs at 0x300 than the file's checked ones
	table[FAT_TABLE_RTS_WORD + 1]++;
	CHECK(!fat_table_safe(table, sink, NULL));
	table[FAT_TABLE_RTS_WORD + 1]--;

	// A game table without its end marker (the walk would go on into the
	// parameters)
	game_table();
	memcpy(table + FAT_TABLE_RTS_WORD, sink, sizeof(sink));
	for (u32 i = 0; i < FAT_TABLE_PARAM_WORD; i += 2)
		table[i] = i * 64, table[i + 1] = 1000 + i * 64;
	CHECK_EQ(fat_runs(table, 0, FAT_TABLE_PARAM_WORD), -1);
	CHECK(!fat_table_safe(table, sink, NULL));

	// Runs that don't start at 0, don't rise, or start at sector 0
	u32 t[8] = {64, 100, FAT_TABLE_END, 0};
	CHECK_EQ(fat_runs(t, 0, 8), -1);
	u32 u[8] = {0, 100, 64, 200, 64, 300, FAT_TABLE_END, 0};
	CHECK_EQ(fat_runs(u, 0, 8), -1);
	u32 v[8] = {0, 100, 64, 0, FAT_TABLE_END, 0};
	CHECK_EQ(fat_runs(v, 0, 8), -1);
	u32 w[8] = {FAT_TABLE_END, 0};
	CHECK_EQ(fat_runs(w, 0, 8), -1);
	CHECK(!fat_table_safe(table, w, NULL));
}
