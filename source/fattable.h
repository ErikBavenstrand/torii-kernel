// The 1 KB table a launch sends the FPGA (Send_FATbuffer), checked before it
// goes. Words: the game's cluster runs from 0, as [sector offset, start
// sector] pairs ending with an offset of 0xFFFFFFFF; the launch's parameters
// at 0x1F0-0x1FF; the save-state file's runs from 0x300 to the end.
//
// The FPGA writes cart SRAM's state buffer (0x40000-0xAFFFF) back to the
// card through the runs at 0x300. With no end marker there, its walk runs
// past the table's end into the game's runs: on 2026-10-27 that wrote a
// Pokémon Emerald state over /ingame.gba on the real cart. So every table
// carries the runs of /torii/rts.bin (rtssink.h), a file the kernel owns,
// and nothing is sent that doesn't.
#ifndef TORII_FATTABLE_H
#define TORII_FATTABLE_H

#include <gba_base.h>

#define FAT_TABLE_WORDS (0x400 / 4)
#define FAT_TABLE_PARAM_WORD (0x1F0 / 4)  // the parameters: the game's runs end before
#define FAT_TABLE_RTS_WORD (0x300 / 4)  // the state file's runs, to the table's end
#define FAT_TABLE_RTS_WORDS (FAT_TABLE_WORDS - FAT_TABLE_RTS_WORD)
#define FAT_TABLE_END 0xFFFFFFFFu

// The runs in words [from, to) of table before its end marker: 0 for an
// empty list ([0, 0] then the marker, as a launch that copies no file sends),
// or -1 when there is no marker before to, the first run doesn't start at
// offset 0, the offsets don't rise, or a run starts at the card's sector 0.
int fat_runs(const u32 *table, u32 from, u32 to);

// The CRC32 of table's words from 0x300 (the state file's runs)
u32 fat_table_rts_crc(const u32 *table);

// 1 if table (FAT_TABLE_WORDS) may go to the FPGA: the game's runs end
// before the parameters, and the words from 0x300 hold at least one run
// ending in the marker, with the CRC32 *sink_crc (as checked against the
// state file's chain; NULL: none checked). 0 otherwise, the reason in why.
int fat_table_safe(const u32 *table, const u32 *sink_crc, const char **why);

#endif
