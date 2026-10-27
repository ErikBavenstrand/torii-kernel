// The save-state buffer's write-back target (rtssink.h).
#include <string.h>

#include "rtssink.h"
#include "Ezcard_OP.h"
#include "GBApatch.h"
#include "ez_define.h"
#include "fattable.h"
#include "ingame_abi.h"
#include "tlog.h"
#include "ff15/ff.h"

extern u32 FAT_table_buffer[FAT_table_size / 4];
extern FATFS EZcardFs;

static u32 sink_crc;
static int sink_ok;

_Static_assert(FAT_table_size == FAT_TABLE_WORDS * 4 && FAT_table_RTS_offset == FAT_TABLE_RTS_WORD * 4,
	       "fattable.h's layout is the launch table's");

#define SINK_SECTORS (INGAME_STATE_BYTES / 512)
// The room a made file leaves for a save: the largest chip's. A launch's
// .sav is made at the boot after the game (the pending save's copy), so the
// file mustn't take its room now.
#define SINK_SAVE_ROOM 0x20000

// The file at its size (anew: made again whatever is there), made when
// missing or another size: its clusters allocated in one piece where the
// card has one (f_expand), else as FatFs finds them; their old bytes left.
// 0, or the error to show (RTS_SINK_NO_ROOM, RTS_SINK_CARD).
static u8 sink_file(int anew)
{
	FIL f;
	if (!anew && f_open(&f, RTS_SINK_FILE, FA_READ) == FR_OK) {
		FSIZE_t size = f_size(&f);
		f_close(&f);
		if (size == INGAME_STATE_BYTES)
			return 0;
		tlog("launch: %s is %lu bytes: made again", RTS_SINK_FILE, (unsigned long)size);
	}
	f_mkdir("/torii");
	FRESULT r = f_open(&f, RTS_SINK_FILE, FA_CREATE_ALWAYS | FA_WRITE);
	if (r != FR_OK) {
		tlog_error("launch: can't make %s (FatFs %d)", RTS_SINK_FILE, (int)r);
		return r == FR_DENIED ? RTS_SINK_NO_ROOM : RTS_SINK_CARD;  // FR_DENIED: the folder is full
	}
	r = f_expand(&f, INGAME_STATE_BYTES, 1);
	int full = 0;
	if (r == FR_DENIED) {  // no free piece that big: cluster by cluster
		r = f_lseek(&f, INGAME_STATE_BYTES);
		full = r == FR_OK && f_tell(&f) != INGAME_STATE_BYTES;  // the card filled up first
	}
	int ok = r == FR_OK && !full && f_size(&f) == INGAME_STATE_BYTES;
	FRESULT c = f_close(&f);
	if (ok && c == FR_OK) {
		DWORD clusters = 0;
		FATFS *fs = NULL;
		FRESULT g = f_getfree("", &clusters, &fs);
		u32 csize = g == FR_OK ? fs->csize : 1;
		full = g == FR_OK && clusters < (SINK_SAVE_ROOM / 512 + csize - 1) / csize;
		ok = !full;
	}
	if (!ok || c != FR_OK) {
		tlog_error("launch: %s not made at %u bytes (FatFs %d, %d%s)", RTS_SINK_FILE, INGAME_STATE_BYTES,
			   (int)r, (int)c, full ? ", the card is full or left no room for a save" : "");
		f_unlink(RTS_SINK_FILE);
		return full ? RTS_SINK_NO_ROOM : RTS_SINK_CARD;
	}
	tlog("launch: made %s, %u bytes, for the cart's state write-back", RTS_SINK_FILE, INGAME_STATE_BYTES);
	return 0;
}

// The table with no game's runs ([0, 0, end]) and the sink's, as parameters
// only: the FPGA holds it from here, so a write to the state buffer before
// the launch's own table can only reach the sink. The table's other words
// are put back.
static u8 sink_send(void)
{
	u32 *t = FAT_table_buffer;
	u32 game[3] = {t[0], t[1], t[2]};
	u32 mode = t[0x1F4 / 4];
	t[0] = 0;
	t[1] = 0;
	t[2] = FAT_TABLE_END;
	t[0x1F4 / 4] = SET_PARAMETER_MODE;
	u32 refused = Send_FATbuffer(t, 1);
	memcpy(t, game, sizeof(game));
	t[0x1F4 / 4] = mode;
	return refused ? RTS_SINK_CARD : 0;
}

u8 rts_sink_prepare(void)
{
	sink_ok = 0;
	u8 err = RTS_SINK_CARD;
	for (int tries = 0; tries < 2; tries++) {
		err = sink_file(tries);
		if (err)
			break;
		TCHAR path[] = RTS_SINK_FILE;
		u32 sectors = 0;
		u32 res = Check_game_RTS_FAT(path, 3, &sectors);
		// The whole clusters the file needs, no fewer (a chain cut short:
		// the FPGA would write on past its end) and no more (a chain that
		// runs on into another file's clusters)
		u32 csize = EZcardFs.csize;
		u32 need = (SINK_SECTORS + csize - 1) / csize * csize;
		if (res == 0 && sectors == need &&
		    fat_runs(FAT_table_buffer, FAT_TABLE_RTS_WORD, FAT_TABLE_WORDS) >= 1) {
			sink_crc = fat_table_rts_crc(FAT_table_buffer);
			sink_ok = 1;
			return sink_send();
		}
		if (res == 0)
			tlog_error("launch: %s's cluster chain covers %lu sectors, not %lu: made again", RTS_SINK_FILE,
				   (unsigned long)sectors, (unsigned long)need);
		else  // too many runs for the table, or a broken chain
			tlog_error("launch: %s's runs don't fit the cart's table (%08lX): made again", RTS_SINK_FILE,
				   (unsigned long)res);
		err = res == RTS_FAT_FRAGMENTED ? RTS_SINK_NO_ROOM : RTS_SINK_CARD;
		f_unlink(RTS_SINK_FILE);
	}
	memset(FAT_table_buffer + FAT_TABLE_RTS_WORD, 0, FAT_TABLE_RTS_WORDS * 4);
	return err;
}

const u32 *rts_sink_crc(void)
{
	return sink_ok ? &sink_crc : NULL;
}
