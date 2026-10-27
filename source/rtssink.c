// The save-state buffer's write-back target (rtssink.h).
#include <string.h>

#include "rtssink.h"
#include "GBApatch.h"
#include "ez_define.h"
#include "fattable.h"
#include "ingame_abi.h"
#include "tlog.h"
#include "ff15/ff.h"

extern u32 FAT_table_buffer[FAT_table_size / 4];

static u32 sink_crc;
static int sink_ok;

_Static_assert(FAT_table_size == FAT_TABLE_WORDS * 4 && FAT_table_RTS_offset == FAT_TABLE_RTS_WORD * 4,
	       "fattable.h's layout is the launch table's");

// The file at its size, made when missing or another size: its clusters
// are allocated (an lseek past the end), their old bytes left
static int sink_file(void)
{
	FIL f;
	if (f_open(&f, RTS_SINK_FILE, FA_READ) == FR_OK) {
		FSIZE_t size = f_size(&f);
		f_close(&f);
		if (size == INGAME_STATE_BYTES)
			return 1;
		tlog("launch: %s is %lu bytes: made again", RTS_SINK_FILE, (unsigned long)size);
	}
	f_mkdir("/torii");
	FRESULT r = f_open(&f, RTS_SINK_FILE, FA_CREATE_ALWAYS | FA_WRITE);
	if (r != FR_OK) {
		tlog_error("launch: can't make %s (FatFs %d)", RTS_SINK_FILE, (int)r);
		return 0;
	}
	r = f_lseek(&f, INGAME_STATE_BYTES);
	int ok = r == FR_OK && f_tell(&f) == INGAME_STATE_BYTES;
	FRESULT c = f_close(&f);
	if (!ok || c != FR_OK) {
		tlog_error("launch: %s not made at %u bytes (FatFs %d, %d)", RTS_SINK_FILE, INGAME_STATE_BYTES, (int)r,
			   (int)c);
		f_unlink(RTS_SINK_FILE);
		return 0;
	}
	tlog("launch: made %s, %u bytes, for the cart's state write-back", RTS_SINK_FILE, INGAME_STATE_BYTES);
	return 1;
}

int rts_sink_prepare(void)
{
	sink_ok = 0;
	for (int tries = 0; tries < 2; tries++) {
		if (!sink_file())
			return 0;
		TCHAR path[] = RTS_SINK_FILE;
		u32 res = Check_game_RTS_FAT(path, 3);
		if (res == 0 && fat_runs(FAT_table_buffer, FAT_TABLE_RTS_WORD, FAT_TABLE_WORDS) >= 1) {
			sink_crc = fat_table_rts_crc(FAT_table_buffer);
			sink_ok = 1;
			return 1;
		}
		// Too many runs for the table, or a broken chain: once more, anew
		tlog_error("launch: %s's runs don't fit the cart's table (%08lX): made again", RTS_SINK_FILE,
			   (unsigned long)res);
		f_unlink(RTS_SINK_FILE);
	}
	memset(FAT_table_buffer + FAT_TABLE_RTS_WORD, 0, FAT_TABLE_RTS_WORDS * 4);
	return 0;
}

const u32 *rts_sink_crc(void)
{
	return sink_ok ? &sink_crc : NULL;
}
