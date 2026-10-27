#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <stdio.h>
#include <stdlib.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>
#include <stdarg.h>
#include <gba_timers.h>

#include "actions.h"
#include "ez_define.h"
#include "ff.h"
#include "ui/dialog.h"
#include "ui/ui.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "saveMODE.h"
#include "RTC.h"
#include "tlog.h"
#include "waitcnt.h"
#include "savename.h"
#include "NORflash_OP.h"
#include "lang.h"
#include "GBApatch.h"
#include "ingame.h"
#include "cheat.h"
#include "cheatstore.h"
#include "settings.h"
#include "cartlog.h"
#include "cutpoint.h"
#include "diskstat.h"
#include "fathint.h"
#include "rtssink.h"
#include "saveorphan.h"
#include "settings_model.h"
#include "shotstore.h"
#include "slotstore.h"
#include "saveslot.h"
#include "charset.h"
#include "diag.h"
#include "engine/cover.h"
#include "library.h"
#include "engine/cycles.h"
#include "engine/psram.h"
#include "gamesave.h"
#include "screens/nav.h"
#include "progress.h"
#include "userdata.h"

#include "emulators.h"

FM_NOR_FS pNorFS[MAX_NOR] EWRAM_BSS;

u32 FAT_table_buffer[FAT_table_size / 4] EWRAM_BSS;
u8 pReadCache[MAX_pReadCache_size] EWRAM_BSS;  // a scratch buffer: its owners are in ez_define.h

void readcache_poison(void)
{
	// mgba-shot fills it when it reads this line, in no emulated time, so
	// the tests' frame timings stay as they are
	tlog_console("scratch free %08lX %lX", (unsigned long)pReadCache, (unsigned long)sizeof(pReadCache));
}

TCHAR currentpath[MAX_path_len];//
TCHAR current_filename[GAME_NAME_MAX + 1] EWRAM_BSS;

u32 game_total_NOR;

u32 gl_currentpage;
u32 gl_norOffset;
u32 gl_lang;
u16 gl_engine_sel;

u16 gl_game_clock;
u16 gl_game_rumble;

u8 __attribute__((aligned(4))) GAMECODE[4];

FATFS EZcardFs;
FILINFO fileinfo;
DIR dir;
FIL gfile;

u16 gl_reset_on;
u16 gl_menu_off;
u16 gl_rts_on;
u16 gl_sleep_on;
u16 gl_cheat_on;

u16 gl_auto_save_sel;

u16 gl_led_open_sel;
u16 gl_Breathing_R;
u16 gl_Breathing_G;
u16 gl_Breathing_B;

u16 gl_SD_R;
u16 gl_SD_G;
u16 gl_SD_B;

u16 SAV_info_buffer[0x200];//Torii: IWRAM, so EWRAM data fits in 256 KB
//******************************************************************************
void delay(u32 R0)
{
	int volatile i;

	for (i = R0; i; --i)
		;
	return;
}
//---------------------------------------------------------------------------------
// Torii: the boot screen's notice, two lines (ui/dialog.h). Not inlined, so
// its buffer stays off main's frame, which is on the deepest call chain.
static void __attribute__((noinline)) Show_boot_notice(const char *line1, const char *line2, u32 kind)
{
	char msg[160];
	snprintf(msg, sizeof(msg), "%s\n%s", line1, line2);
	dialog_notice(NULL, msg, kind);
}
//---------------------------------------------------------------------------------
// Torii: the boot's "Loading..." notice, only once the work after the card
// mounted has taken BOOT_NOTICE_FRAMES (a third of a second): a fast boot
// goes to the list with nothing before it (a notice only flashed), and a
// slow one (a big, slow card; making the log file) says it is busy. The
// boot calls it between its steps.
#define BOOT_NOTICE_FRAMES 20
#define BOOT_MOUNT_RETRIES 3
#define BOOT_MOUNT_PAUSE 30  // frames before each retry
static u32 boot_mounted;  // torii_frames once the card mounted
static u8 boot_noticed;
static void Boot_notice_check(void)
{
	if (boot_noticed || torii_frames - boot_mounted < BOOT_NOTICE_FRAMES)
		return;
	boot_noticed = 1;
	tlog_debug("boot: still busy %lu frames after mounting", (unsigned long)(torii_frames - boot_mounted));
	dialog_notice(NULL, gl_Loading, DIALOG_INFO);
}
//---------------------------------------------------------------------------------
//------------------------------------------------------------------
// Which of the cart's clock and rumble motor a game can use (GAME_USES_*).
// GBA: only Drill Dozer and WarioWare: Twisted! have rumble, and neither has a
// clock; any other game may read the clock. GB and GBC games run in Goomba,
// which passes the cart's clock to MBC3+TIMER games and its rumble motor to
// MBC5+RUMBLE games, going by the cart type at 0x147. PocketNES shows the clock.
u32 Game_hardware(u32 is_EMU, const u8 *gamecode, u8 gb_cart_type)
{
	switch (is_EMU) {
	case 0://gba
		if ((gamecode[0] == 'V' && gamecode[1] == '4' && gamecode[2] == '9') ||
		    (gamecode[0] == 'R' && gamecode[1] == 'Z' && gamecode[2] == 'W'))
			return GAME_USES_RUMBLE;
		return GAME_USES_CLOCK;
	case 1://gbc
	case 2://gb
		if (gb_cart_type == 0x0F || gb_cart_type == 0x10)
			return GAME_USES_CLOCK;
		if (gb_cart_type >= 0x1C && gb_cart_type <= 0x1E)
			return GAME_USES_RUMBLE;
		return 0;
	default://nes
		return GAME_USES_CLOCK;
	}
}
//---------------------------------------------------------------------------------
// The game NOR list's game named name, or game_total_NOR if none.
u32 NOR_game_named(const char *name)
{
	u32 i;
	for (i = 0; i < game_total_NOR; i++)
		if (strcmp(pNorFS[i].filename, name) == 0)
			break;
	return i;
}
//---------------------------------------------------------------------------------
void init_FAT_table(void)
{
	//memset(FAT_table_buffer,0,0x200);
	FAT_table_buffer[0] = 0x00000000;
	CpuFastSet(FAT_table_buffer, FAT_table_buffer, FILL | (FAT_table_size / 4));
	FAT_table_buffer[2] = 0xFFFFFFFF;
}
//---------------------------------------------------------------------------------
u32 Check_game_RTS_FAT(TCHAR *filename, u32 game_save_rts)
{
	u32 res;
	//u32 ret;
	FIL file;
	u32 *FAT_table_P;
	u32 *FAT_table_end;
	u32 getcluster;
	u32 getcluster_old;
	u32 cluster_num = 0;
	u32 lastest_cluster;

	res = f_open(&file, filename, FA_READ);

	if (res != FR_OK)
		return 0xffffffff;

#ifdef DEBUG
	//DEBUG_printf("first clust %x;  sec=%x ",(&file)->obj.sclust,	ClustToSect(&EZcardFs,(&file)->obj.sclust)	);
	//DEBUG_printf("fs->fs_type %x",(&EZcardFs)->fs_type);
#endif
	// Values from here up are bad clusters or the end of the chain
	if ((&EZcardFs)->fs_type == FS_FAT12) {
		lastest_cluster = 0xFF7;
	} else if ((&EZcardFs)->fs_type == FS_FAT16) {
		lastest_cluster = 0xFFF7;
	} else {
		lastest_cluster = 0xFFFFFF7;
	}
	getcluster = (&file)->obj.sclust;
	if (getcluster < 2) {  // an empty file has no clusters
		f_close(&file);
		return 0xffffffff;
	}

	if (game_save_rts == 1) {
		FAT_table_P = FAT_table_buffer;
	}
	/*else if	(game_save_rts == 2)
	{
		FAT_table_P = FAT_table_buffer+ FAT_table_SAV_offset/4;
	}*/
	else {
		FAT_table_P = FAT_table_buffer + FAT_table_RTS_offset / 4;
	}
	// The game's table ends at the launch parameters at 0x1F0, the RTS
	// file's at the end of the buffer: each run of clusters takes 8 bytes
	FAT_table_end = (game_save_rts == 1) ? FAT_table_buffer + 0x1F0 / 4 : FAT_table_buffer + FAT_table_size / 4;

	*FAT_table_P = 0x00000000;
	FAT_table_P++;
	*FAT_table_P = (ClustToSect(&EZcardFs, getcluster));
	FAT_table_P++;

	getcluster_old = getcluster;
	do {
		getcluster = Get_NextCluster(&(&file)->obj, getcluster);
		cluster_num++;
		// The card failed to read: FatFs masks every real FAT value below this
		if (getcluster == 0xFFFFFFFF) {
			f_close(&file);
			return 0xffffffff;
		}
		if (getcluster < 2) {  // FatFs couldn't follow the chain
			f_close(&file);
			return RTS_FAT_FRAGMENTED;
		}
		if (getcluster != (getcluster_old + 1)) {
			if (FAT_table_P + 2 > FAT_table_end) {
				tlog("%s has more runs of clusters than the cart's table holds", filename);
				f_close(&file);
				return RTS_FAT_FRAGMENTED;
			}
#ifdef DEBUG
			//DEBUG_printf("getcluster = %x",getcluster);
#endif
			*FAT_table_P = (cluster_num * (&EZcardFs)->csize);//sector_per_cluster
			FAT_table_P++;
			*FAT_table_P = (ClustToSect(&EZcardFs, getcluster));//getcluster;
			FAT_table_P++;
		}
		getcluster_old = getcluster;
	} while (getcluster < lastest_cluster);
	*--FAT_table_P = 0x0;
	*--FAT_table_P = 0xffffffff;

	f_close(&file);
	return 0;
}
//---------------------------------------------------------------------------------
u32 Loadsavefile(TCHAR *filename)
{
	UINT ret;
	UINT filesize;
	UINT left;
	FIL file;

	switch (f_open(&file, filename, FA_READ)) {
	case FR_OK: {
		filesize = f_size(&file);
		if (filesize > 128 * 1024)
			filesize = 128 * 1024;

		SetRampage(0x0);

		// Torii: the reads' results are logged, and what a failed or short
		// read leaves out is blank (0xFF), not an earlier owner's bytes
		FRESULT res, res2 = FR_OK;
		UINT got;
		memset(pReadCache, 0xFF, 64 * 1024);
		if (filesize > 64 * 1024) {
			res = f_read(&file, pReadCache, 64 * 1024, (UINT *)&ret);
			got = ret;
			WriteSram(SRAMSaver, pReadCache, 64 * 1024);
			SetRampage(0x10);
			left = filesize - 64 * 1024;
			memset(pReadCache, 0xFF, 64 * 1024);
			res2 = f_read(&file, pReadCache, left, (UINT *)&ret);
			got += ret;
			WriteSram(SRAMSaver, pReadCache, left);
		} else {
			res = f_read(&file, pReadCache, filesize, (UINT *)&ret);
			got = ret;
			WriteSram(SRAMSaver, pReadCache, filesize);
		}
		f_close(&file);
		SetRampage(0x0);
		if (res != FR_OK || res2 != FR_OK || got != filesize) {
			// Torii: no launch: its pending-save record would have the next
			// power-on copy this cart SRAM over the good file
			tlog_error("save: loading %s into cart SRAM: f_read gave %d and %d, %u of %u bytes", filename,
				   res, res2, got, filesize);
			return 0;
		}
		tlog("save: loaded %s into cart SRAM, %u bytes", filename, filesize);
		return 1;
	}
	default:
		tlog_error("save: can't open %s to load it", filename);
		return false;
	}
}
//---------------------------------------------------------------------------------
// Torii: the copy of cart SRAM to a .sav, made so a good save survives a
// power cut or a full card at any step (v0.5, docs/plans/roadmap.md):
//   1. Save_check: a blank chip, or one identical to the file, writes nothing;
//      one that doesn't look like the file asks first (pending_save_copy).
//   2. Save_savefile writes <save>.tmp, f_sync, f_close, reads it back from the
//      card and compares it with the chip. The save is untouched until then.
//   3. The old save is renamed to <save>.bak, the tmp file to <save>, and the
//      .bak renamed into the slot's history as its next version
//      (Save_bak_retire; removed with History off). Nothing is copied twice:
//      the old save's data never moves, so the swap costs the same at any size.
// f_rename refuses to replace a file and isn't atomic, so Save_recover, at boot
// and before each copy and launch, finishes or undoes whatever a cut left. The
// pending-save record stays until the copy checks out (the callers clear it).
// Reads go a chip chunk and a file chunk at a time (the two fill READCACHE_SAVE's
// first 8 KB, clear of READCACHE_SLOTS); the tmp file is written 8 KB at a
// time from the chip chunk's room alone: each disk_write call ends with the
// SD driver's settle delay (Write_SD_sectors, about 8.6 ms), so fewer, larger
// writes are what make the copy fast
#define SAVE_CHUNK 0x1000
#define SAVE_WRITE_CHUNK 0x2000
_Static_assert(READCACHE_SAVE + 2 * SAVE_CHUNK <= READCACHE_SLOTS,
	       "the save copy's chunks stay clear of the slot store's");
_Static_assert(READCACHE_SAVE + SAVE_WRITE_CHUNK <= READCACHE_SLOTS,
	       "the save copy's write chunk stays clear of the slot store's");

static FIL save_fil EWRAM_BSS;
static char save_tmp[SAV_INFO_NAME_MAX + 6] EWRAM_BSS;
static char save_bak[SAV_INFO_NAME_MAX + 6] EWRAM_BSS;
static char save_hist[SAV_INFO_NAME_MAX + 6] EWRAM_BSS;  // Save_bak_retire's history folder

// Sets save_tmp and save_bak for filename; 0 if the name is too long
static int save_names(const char *filename)
{
	if (strlen(filename) > SAV_INFO_NAME_MAX)
		return 0;
	snprintf(save_tmp, sizeof(save_tmp), "%s.tmp", filename);
	snprintf(save_bak, sizeof(save_bak), "%s.bak", filename);
	return 1;
}

// 1 if path is a file, 0 if there is none, -1 if the card didn't say
static int save_exists(const char *path)
{
	FRESULT res = f_stat(path, NULL);
	if (res == FR_OK)
		return 1;
	return res == FR_NO_FILE || res == FR_NO_PATH ? 0 : -1;
}

// Reads len bytes of the save chip from offset off (not across a 64 KB page)
static void sram_read(u8 *dst, u32 off, u32 len)
{
	SetRampage(off >= 0x10000 ? 0x10 : 0x0);
	ReadSram(SRAMSaver + (off & 0xFFFF), dst, len);
	SetRampage(0x0);
}

// The bytes of the chip that a save of savesize copies
static u32 save_span(u32 savesize)
{
	return savesize > 128 * 1024 ? 128 * 1024 : savesize;
}

// The versions a recovery keeps: the setting's, and one with History off, as
// the .bak it finds may be the old save a restore or import replaced
static u32 recover_keep(void)
{
	u32 k = slotstore_keep();
	return k ? k : 1;
}

int Save_bak_retire(const TCHAR *filename, u32 keep)
{
	if (!save_names(filename))
		return 0;
	if (keep && saveslot_backup_dir_of(save_hist, sizeof(save_hist), filename) >= 0) {
		int r = slotstore_move_file(save_bak, save_hist, keep);
		if (r == SLOTSTORE_OK || r == SLOTSTORE_NO_SLOT)
			return 1;
		if (r == SLOTSTORE_CARD) {
			tlog_error("save: %s stays: the history can't take it now", save_bak);
			return 0;
		}
		tlog_error("save: the history can't take %s (%d): it is removed", save_bak, r);
	}
	FRESULT u = f_unlink(save_bak);
	if (u != FR_OK && u != FR_NO_FILE) {
		tlog_error("save: removing %s: f_unlink gave %d", save_bak, u);
		return 0;
	}
	return 1;
}

void Save_install_finish(const TCHAR *filename)
{
	if (!save_names(filename))
		return;
	int f = save_exists(filename), t = save_exists(save_tmp), b = save_exists(save_bak);
	if (f == 1 && t == 1) {
		FRESULT r = f_unlink_alias(save_tmp, filename);
		if (r == FR_DENIED) {
			// The verified file waits beside the old one: the old one steps
			// aside (after any older .bak has gone), and Save_recover goes on
			if (b == 1 && !Save_bak_retire(filename, recover_keep()))
				return;
			r = f_rename(filename, save_bak);
			tlog("save: install of %s: the old file steps aside, f_rename gave %d", filename, r);
		}
		if (r != FR_OK)
			return;
	}
	Save_recover(filename);
}

// Finishes or undoes what a power cut left of a copy to filename. The states,
// by which of the save F, F.tmp (T) and F.bak (B) exist:
//   F            nothing to do
//   F T, F B, ...  F is whole (the rename of T onto F is the last step before
//                T goes, and F is only ever a renamed, verified T or the old
//                save): T goes, B goes into the history (Save_bak_retire,
//                keeping at least one version: it is the old save)
//   T B          the old save moved aside, T verified: T takes F's name, then
//                B goes into the history
//   B            the old save moved aside, nothing else: B takes F's name back
//   T            nothing existed, or T was cut mid-write: T goes (the chip
//                still holds the data, the record is still set)
// A cut inside f_rename leaves two names on one chain: the one that isn't
// final is dropped by its entry alone (f_unlink_alias), never f_unlink.
void Save_recover(const TCHAR *filename)
{
	if (!save_names(filename))
		return;
	int f = save_exists(filename), t = save_exists(save_tmp), b = save_exists(save_bak);
	if (f < 0 || t < 0 || b < 0) {
		tlog_error("save: recovery of %s: the card didn't answer", filename);
		return;
	}
	if (!t && !b)
		return;
	tlog("save: recovery of %s: save %d, tmp %d, bak %d", filename, f, t, b);
	int t_ok = 1, b_ok = 1;  // safe to f_unlink
	if (f && b) {
		FRESULT r = f_unlink_alias(save_bak, filename);
		if (r == FR_OK) {
			b = 0;
			tlog("save: recovery: the .bak was a second name of the save, dropped");
		} else if (r != FR_DENIED) {
			b_ok = 0;
		}
	}
	if (f && t) {
		FRESULT r = f_unlink_alias(save_tmp, filename);
		if (r == FR_OK) {
			t = 0;
			tlog("save: recovery: the .tmp was a second name of the save, dropped");
		} else if (r != FR_DENIED) {
			t_ok = 0;
		}
	}
	if (f) {
		if (t && t_ok)
			tlog("save: recovery: removed the .tmp, f_unlink gave %d", f_unlink(save_tmp));
		if (b && b_ok)
			Save_bak_retire(filename, recover_keep());
	} else if (t && b) {
		FRESULT r = f_rename(save_tmp, filename);
		tlog("save: recovery: the .tmp takes the save's name, f_rename gave %d", r);
		if (r == FR_OK)
			Save_bak_retire(filename, recover_keep());
	} else if (b) {
		tlog("save: recovery: the .bak takes the save's name back, f_rename gave %d",
		     f_rename(save_bak, filename));
	} else if (t) {
		tlog("save: recovery: removed the .tmp, f_unlink gave %d", f_unlink(save_tmp));
	}
}

// Whether the chip is worth copying over filename (the file as the game
// loaded it, or none): SAVE_SKIPPED for a blank chip (all FF or all 00,
// nothing was saved) or one identical to the file, SAVE_MISMATCH when it
// doesn't look like the file (see saveslot_match_ok: another game's save), SAVE_WRITE
// otherwise, SAVE_FAILED if the card can't be read, SAVE_NOTHING for
// savesize 0. Writes nothing, so it runs before the progress dialog.
u32 Save_check(TCHAR *filename, u32 savesize)
{
	u8 *chip = pReadCache + READCACHE_SAVE, *file = chip + SAVE_CHUNK;
	u32 span = save_span(savesize), off;
	if (span == 0)
		return SAVE_NOTHING;
	u32 blank = SAVESLOT_BLANK_START;
	for (off = 0; off < span && blank; off += SAVE_CHUNK) {
		u32 len = span - off < SAVE_CHUNK ? span - off : SAVE_CHUNK;
		sram_read(chip, off, len);
		blank = saveslot_blank_scan(blank, chip, len);
	}
	if (blank) {
		tlog("save: the chip is blank (all %s), %s stays as it is", blank & 1 ? "FF" : "00", filename);
		return SAVE_SKIPPED;
	}
	FRESULT res = f_open(&save_fil, filename, FA_READ);
	if (res == FR_NO_FILE || res == FR_NO_PATH) {
		tlog("save: %s doesn't exist yet", filename);
		return SAVE_WRITE;
	}
	if (res != FR_OK) {
		tlog_error("save: can't open %s to compare: f_open gave %d", filename, res);
		return SAVE_FAILED;
	}
	u32 fsize = f_size(&save_fil), common = fsize < span ? fsize : span;
	int same = fsize == span;
	struct saveslot_match match = {0, 0};
	for (off = 0; off < common; off += SAVE_CHUNK) {
		u32 len = common - off < SAVE_CHUNK ? common - off : SAVE_CHUNK;
		UINT got = 0;
		sram_read(chip, off, len);
		res = f_read(&save_fil, file, len, &got);
		if (res != FR_OK || got != len) {
			tlog_error("save: can't read %s to compare: f_read gave %d, %u of %lu bytes", filename, res,
				   got, (unsigned long)len);
			f_close(&save_fil);
			return SAVE_FAILED;
		}
		if (!saveslot_identical(chip, file, len))
			same = 0;
		saveslot_match_scan(&match, file, chip, len);
	}
	f_close(&save_fil);
	if (same) {
		tlog("save: the chip is identical to %s, nothing to copy", filename);
		return SAVE_SKIPPED;
	}
	if (!saveslot_match_ok(&match)) {
		tlog("save: the chip doesn't look like %s: %lu of its %lu data bytes are the same", filename,
		     (unsigned long)match.same, (unsigned long)match.data);
		return SAVE_MISMATCH;
	}
	return SAVE_WRITE;
}

// A step of the swap failed: undo or finish it, so a good save stays
static u32 save_swap_failed(const TCHAR *filename, const char *what, FRESULT res)
{
	tlog_error("save: %s: %d", what, res);
	Save_recover(filename);
	return SAVE_FAILED;
}

// Copies savesize bytes of cart SRAM to filename as the comment above says:
// SAVE_WRITTEN once the verified copy is the save, SAVE_NOTHING for savesize 0
// (no file touched), SAVE_FAILED if anything fell short (a full card, a bad
// readback); then the old save, if any, is as it was. ROM code, as
// Loadsavefile: both run only in the menu. The SRAM goes through pReadCache's
// upper half, which no frame touches (ez_define.h), so the progress dialog can
// draw between chunks.
u32 Save_savefile(TCHAR *filename, u32 savesize, const char *hist_dir)
{
	u8 *chip = pReadCache + READCACHE_SAVE, *file = chip + SAVE_CHUNK;
	u32 span = save_span(savesize), off;
	if (span == 0)
		return SAVE_NOTHING;
	if (!save_names(filename)) {
		tlog_error("save: %s is too long to copy", filename);
		return SAVE_FAILED;
	}
	// The caller (pending_save_copy) has run Save_recover: nothing since
	// has touched the save's files

	// 1. The tmp file (a game's first save: its folder under /torii/saves
	// may not be there yet)
	FRESULT res = f_open(&save_fil, save_tmp, FA_WRITE | FA_CREATE_ALWAYS);
	if (res == FR_NO_PATH && gamesave_make_parents(filename))
		res = f_open(&save_fil, save_tmp, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK) {
		tlog_error("save: can't create %s: f_open gave %d", save_tmp, res);
		return SAVE_FAILED;
	}
	u32 total = 0;
	FRESULT werr = FR_OK;
	for (off = 0; off < span && werr == FR_OK && total == off; off += SAVE_WRITE_CHUNK) {
		u32 len = span - off < SAVE_WRITE_CHUNK ? span - off : SAVE_WRITE_CHUNK;
		UINT written = 0;
		sram_read(chip, off, len);
		progress_part(off, span * 2);
		werr = f_write(&save_fil, chip, len, &written);
		total += written;
		if (off == 0)
			cutpoint("save: tmp first chunk written");
	}
	FRESULT serr = werr == FR_OK ? f_sync(&save_fil) : FR_OK;
	FRESULT cerr = f_close(&save_fil);
	if (werr != FR_OK || serr != FR_OK || cerr != FR_OK || total != span) {
		tlog_error("save: writing %s: %lu of %lu bytes written, f_write gave %d, f_sync %d, f_close %d",
			   save_tmp, (unsigned long)total, (unsigned long)span, werr, serr, cerr);
		tlog("save: removed the .tmp, f_unlink gave %d", f_unlink(save_tmp));
		return SAVE_FAILED;  // a short write (a full card) is a failure: the save is untouched
	}
	cutpoint("save: tmp written");

	// 2. Read it back from the card and compare it with the chip
	res = f_open(&save_fil, save_tmp, FA_READ);
	if (res != FR_OK || f_size(&save_fil) != span) {
		tlog_error("save: reading %s back: f_open gave %d, %lu bytes", save_tmp, res,
			   res == FR_OK ? (unsigned long)f_size(&save_fil) : 0ul);
		if (res == FR_OK)
			f_close(&save_fil);
		f_unlink(save_tmp);
		return SAVE_FAILED;
	}
	int good = 1;
	for (off = 0; off < span && good; off += SAVE_CHUNK) {
		u32 len = span - off < SAVE_CHUNK ? span - off : SAVE_CHUNK;
		UINT got = 0;
		progress_part(span + off, span * 2);
		sram_read(chip, off, len);
		res = f_read(&save_fil, file, len, &got);
		if (res != FR_OK || got != len || !saveslot_identical(chip, file, len)) {
			tlog_error("save: %s read back wrong at %lu: f_read gave %d, %u of %lu bytes", save_tmp,
				   (unsigned long)off, res, got, (unsigned long)len);
			good = 0;
		}
	}
	f_close(&save_fil);
	if (!good) {
		f_unlink(save_tmp);
		return SAVE_FAILED;
	}
	cutpoint("save: tmp verified");

	// 3. Swap it in
	int had = save_exists(filename);
	if (had < 0) {
		tlog_error("save: the card didn't answer for %s", filename);
		f_unlink(save_tmp);
		return SAVE_FAILED;
	}
	// The old save is the file, the .bak or (renamed, never copied) a version
	// in the slot's history at every step from here
	if (had) {
		res = f_rename(filename, save_bak);
		if (res != FR_OK)
			return save_swap_failed(filename, "moving the old save to .bak", res);
		cutpoint("save: old save moved to bak");
	}
	res = f_rename(save_tmp, filename);
	if (res != FR_OK)
		return save_swap_failed(filename, "moving the .tmp into place", res);
	cutpoint("save: tmp moved into place");
	if (had && Save_bak_retire(filename, hist_dir ? slotstore_keep() : 0))
		cutpoint("save: old save retired");  // else recovery retires it
	tlog("save: wrote %s, %lu bytes, verified", filename, (unsigned long)total);
	return SAVE_WRITTEN;
}
//---------------------------------------------------------------------------------
// Torii: fills the save chip with the blank chip's 0xFF, savesize bytes, for
// a game with no .sav (Process_savefile)
static void Blank_sram(u32 savesize)
{
	u32 span = save_span(savesize);
	memset(pReadCache, 0xFF, SAVE_CHUNK);
	for (u32 off = 0; off < span; off += SAVE_CHUNK) {
		u32 len = span - off < SAVE_CHUNK ? span - off : SAVE_CHUNK;
		SetRampage(off >= 0x10000 ? 0x10 : 0x0);
		WriteSram(SRAMSaver + (off & 0xFFFF), pReadCache, len);
	}
	SetRampage(0x0);
	tlog("save: blank chip loaded, %lu bytes", (unsigned long)span);
}
//---------------------------------------------------------------------------------
static u32 SavefileWrite(TCHAR *filename, u32 savesize)
{
	FIL file;
	if (savesize == 0)
		return SAVE_NOTHING;
	u32 ret = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
	switch (ret) {
	case FR_OK: {
		// Whole sectors, or whole 2 KB for a save of 2 KB or more (as
		// before), written 8 KB at a time
		u32 unit = savesize < 0x800 ? 0x200 : 0x800;
		u32 target = (savesize + unit - 1) / unit * unit, total = 0;
		memset(pReadCache, 0xFF, SAVE_WRITE_CHUNK);
		while (total < target) {
			UINT chunk = target - total < SAVE_WRITE_CHUNK ? target - total : SAVE_WRITE_CHUNK, written = 0;
			f_write(&file, pReadCache, chunk, &written);
			total += written;
			if (written != chunk)
				break;
		}

		FRESULT cerr = f_close(&file);
		if (total < savesize || cerr != FR_OK)  // Torii: logged, as before not an error
			tlog_error("save: making a blank %s: %lu of %lu bytes written, f_close gave %d", filename,
				   (unsigned long)total, (unsigned long)savesize, cerr);
		else
			tlog("save: made a blank %s, %lu bytes", filename, (unsigned long)total);

		return 1;
	} break;
	default:
		tlog_error("save: can't create %s: f_open gave %lu", filename, (unsigned long)ret);
		return SAVE_FAILED;
	}
}
//---------------------------------------------------------------
u8 Check_saveMODE(u8 gamecode[])
{
	u32 i;
	BYTE savemode = 0x10;
	dmaCopy((void *)saveMODE_table, (void *)pReadCache, sizeof(saveMODE_table));
	for (i = 0; i < 3000; i++) {
		if (memcmp(((SAVE_MODE_ *)pReadCache)[i].gamecode, "FFFF", 4) == 0) {
			break;
		} else if (memcmp(((SAVE_MODE_ *)pReadCache)[i].gamecode, gamecode, 4) == 0) {
			savemode = ((SAVE_MODE_ *)pReadCache)[i].savemode;
			break;
		}
	}
	return savemode;
}
//---------------------------------------------------------------
u8 Get_saveMODE(u8 Save_num, u32 gamefilesize)
{
	u8 saveMODE;
	if (Save_num == 0)//auto
	{
		saveMODE = Check_saveMODE(GAMECODE);
	} else //Manual selection
	{
		switch (Save_num) {
		case 0x1:
			saveMODE = 0x11;
			break;//SRAM
		case 0x2:
			if (gamefilesize > 0x1200000) {//some eeprom modify rom
				saveMODE = 0x23;//32M //EEPROM8K
			} else {
				saveMODE = 0x22;//EEPROM8K
			}
			break;
		case 0x3:
			saveMODE = 0x21;
			break;//EEPROM512
		case 0x4:
			saveMODE = 0x32;
			break;//FLASH64
		case 0x5:
			saveMODE = 0x31;
			break;//FLASH128
		case 0xf:
			saveMODE = 0x10;
			break;
		default:
			saveMODE = 0x00;
			break;
		}
	}
	return saveMODE;
}
//---------------------------------------------------------------
// Torii: whether a block read of a ROM at blocknum failed: an error, or
// short of the file's end. Logs it. Not inlined into the IWRAM loaders.
static __attribute__((noinline)) int rom_read_failed(const TCHAR *filename, FRESULT res, u32 blocknum, UINT got,
						     u32 filesize)
{
	if (res == FR_OK && (got == 0x20000 || blocknum + got >= filesize))
		return 0;
	tlog_error("launch: reading %s at %lu failed (FatFs %d, %u bytes)", filename, (unsigned long)blocknum, res,
		   got);
	return 1;
}
//---------------------------------------------------------------
u32 IWRAM_CODE Loadfile2PSRAM(TCHAR *filename)
{
	UINT ret;
	u32 filesize;
	u32 res;
	u32 blocknum;

	u32 Address;
	vu16 page = 0;
	SetPSRampage(page);

	res = f_open(&gfile, filename, FA_READ);
	if (res == FR_OK) {
		filesize = f_size(&gfile);

		f_lseek(&gfile, 0x0000);
		for (blocknum = 0x0000; blocknum < filesize; blocknum += 0x20000) {
			// Torii: the launch's step (progress.h), between blocks
			progress_part(blocknum, filesize);
			res = f_read(&gfile, pReadCache, 0x20000, &ret);//pReadCache max 0x20000 Byte
			if (rom_read_failed(filename, res, blocknum, ret, filesize)) {
				f_close(&gfile);
				SetPSRampage(0);
				return 1;
			}

			if ((gl_reset_on == 1) || (gl_rts_on == 1) || (gl_sleep_on == 1) || (gl_cheat_on == 1)) {
				PatchInternal((u32 *)pReadCache, 0x20000, blocknum);
			}

			Address = blocknum;
			while (Address >= 0x800000) {
				Address -= 0x800000;
				page += 0x1000;
			}

			SetPSRampage(page);
			dmaCopy((void *)pReadCache, PSRAMBase_S98 + Address, 0x20000);
			page = 0;
		}
		f_close(&gfile);
		SetPSRampage(0);
		progress_part(filesize, filesize);
		return 0;
	} else {
		return 1;
	}
}
//---------------------------------------------------------------------------------
void CheckLanguage(void)
{
	u16 id = Read_SET_info(assress_language);
	u32 i;

	// An unknown id, such as a dropped language's, gets the first language
	gl_lang = 0;
	for (i = 0; i < lang_pack_count; i++) {
		if (lang_packs[i].id == id)
			gl_lang = i;
	}
	lang_packs[gl_lang].load();
}
//---------------------------------------------------------------------------------
// Torii: the BOOT WITH ADDON switches as the launches read them: 1 on, else
// off. An erased slot reads 0xFFFF, which went into the logs ("savestate
// 65535"), the patch's switches and a NOR game's entry. Cheats are on when
// the game's picked cheats have byte writes (cheatstore_launch, at the
// launch's start): the cheat hook and its table.
static void Read_addon_switches(void)
{
	gl_reset_on = Read_SET_info(assress_v_reset) == SET_MENU_EVERY;
	gl_menu_off = Read_SET_info(assress_v_reset) == SET_MENU_OFF;
	gl_rts_on = Read_SET_info(assress_v_rts) == 1;
	gl_sleep_on = Read_SET_info(assress_v_sleep) == 1;
	gl_cheat_on = gl_cheat_count > 0;
}
//---------------------------------------------------------------------------------
void CheckSwitch(void)
{
	gl_reset_on = Read_SET_info(assress_v_reset);
	gl_rts_on = Read_SET_info(assress_v_rts);
	gl_sleep_on = Read_SET_info(assress_v_sleep);
	gl_cheat_on = 0;  // a launch's, with the game's picks (Read_addon_switches)
	gl_menu_off = gl_reset_on == SET_MENU_OFF;  // In-game menu Off (v0.8)
	if ((gl_reset_on != 0x0) && (gl_reset_on != 0x1)) {
		gl_reset_on = 0x0;
	}
	if ((gl_rts_on != 0x0) && (gl_rts_on != 0x1)) {
		gl_rts_on = 0x0;
	}
	if ((gl_sleep_on != 0x0) && (gl_sleep_on != 0x1)) {
		gl_sleep_on = 0x0;
	}

	gl_engine_sel = Read_SET_info(assress_engine_sel);
	if ((gl_engine_sel != 0x0) && (gl_engine_sel != 0x1)) {
		gl_engine_sel = 0x1;
	}

	gl_game_clock = Read_SET_info(assress_game_clock);
	if ((gl_game_clock != 0x0) && (gl_game_clock != 0x1)) {
		gl_game_clock = 0x1;
	}

	gl_game_rumble = Read_SET_info(assress_game_rumble);
	if ((gl_game_rumble != 0x0) && (gl_game_rumble != 0x1)) {
		gl_game_rumble = 0x1;
	}

	gl_auto_save_sel = Read_SET_info(assress_auto_save_sel);
	if ((gl_auto_save_sel != 0x0) && (gl_auto_save_sel != 0x1)) {
		gl_auto_save_sel = 0x0;
	}

	gl_led_open_sel = Read_SET_info(assress_led_open_sel);
	if ((gl_led_open_sel != 0x0) && (gl_led_open_sel != 0x1)) {
		gl_led_open_sel = 0x1;
	}
	gl_Breathing_R = Read_SET_info(assress_Breathing_R);
	if ((gl_Breathing_R != 0x0) && (gl_Breathing_R != 0x1)) {
		gl_Breathing_R = 0x1;
	}
	gl_Breathing_G = Read_SET_info(assress_Breathing_G);
	if ((gl_Breathing_G != 0x0) && (gl_Breathing_G != 0x1)) {
		gl_Breathing_G = 0x1;
	}
	gl_Breathing_B = Read_SET_info(assress_Breathing_B);
	if ((gl_Breathing_B != 0x0) && (gl_Breathing_B != 0x1)) {
		gl_Breathing_B = 0x1;
	}
	gl_SD_R = Read_SET_info(assress_SD_R);
	if ((gl_SD_R != 0x0) && (gl_SD_R != 0x1)) {
		gl_SD_R = 0x0;
	}
	gl_SD_G = Read_SET_info(assress_SD_G);
	if ((gl_SD_G != 0x0) && (gl_SD_G != 0x1)) {
		gl_SD_G = 0x0;
	}
	gl_SD_B = Read_SET_info(assress_SD_B);
	if ((gl_SD_B != 0x0) && (gl_SD_B != 0x1)) {
		gl_SD_B = 0x0;
	}

	u16 led_status = (gl_led_open_sel << 7) | (gl_Breathing_R << 5) | (gl_Breathing_G << 4) |
			 (gl_Breathing_B << 3) | (gl_SD_R << 2) | (gl_SD_G << 1) | (gl_SD_B);
	Set_LED_control(led_status);
}
//---------------------------------------------------------------
u32 IWRAM_CODE LoadEMU2PSRAM(TCHAR *filename, u32 is_EMU)
{
	UINT ret;
	u32 filesize;
	u32 res;
	u32 blocknum;

	u32 Address;
	vu16 page = 0;
	SetPSRampage(page);

	// Torii: the emulator (emulators.h), through pReadCache, as a DMA from
	// cart space to cart space loses its writes. The game goes right after
	// it; a NES game after PocketNES's header, on a 256-byte boundary, as
	// PocketNES times branches' page crossings by their ROM address
	// (Dwedit/PocketNES#5).
	const u8 *emu = (is_EMU == 3) ? pocketnes_gba : jagoombacolor_gba;
	u32 emu_size = (is_EMU == 3) ? pocketnes_gba_size : jagoombacolor_gba_size;
	for (u32 done = 0; done < emu_size; done += 0x20000) {
		u32 n = (emu_size - done < 0x20000) ? emu_size - done : 0x20000;
		dmaCopy((void *)(emu + done), pReadCache, n);
		dmaCopy((void *)pReadCache, PSRAMBase_S98 + done, n);
	}
	u32 rom_start_address = (is_EMU == 3) ? (emu_size + 0x30 + 0xFF) & ~0xFFu : emu_size;

	res = f_open(&gfile, filename, FA_READ);
	if (res == FR_OK) {
		filesize = f_size(&gfile);

		if (is_EMU == 3) {
			// Torii: PocketNES's romheader, whole, after zeros from
			// the emulator's end: the name, the size, and flags and
			// sprite following at 0 (the defaults). Its clock is on
			// in its build
			u32 gap = rom_start_address - emu_size;
			vu32 *words = (vu32 *)pReadCache;
			for (u32 i = 0; i < gap / 4; i++)
				words[i] = 0;
			vu32 *header = words + (gap - 0x30) / 4;
			header[0] = 0x45564153;  // "SAVE"
			header[8] = filesize;
			dmaCopy((void *)pReadCache, PSRAMBase_S98 + emu_size, gap);
		}

		f_lseek(&gfile, 0x0000);
		for (blocknum = 0x0000; blocknum < filesize; blocknum += 0x20000) {
			// Torii: the launch's step (progress.h), between blocks
			progress_part(blocknum, filesize);
			//f_lseek(&gfile, blocknum);
			res = f_read(&gfile, pReadCache, 0x20000, &ret);//pReadCache max 0x20000 Byte
			if (rom_read_failed(filename, res, blocknum, ret, filesize)) {
				f_close(&gfile);
				SetPSRampage(0);
				return 1;
			}

			Address = blocknum;
			while (Address >= 0x800000) {
				Address -= 0x800000;
				page += 0x1000;
			}
			SetPSRampage(page);
			dmaCopy((void *)pReadCache, PSRAMBase_S98 + rom_start_address + Address, 0x20000);

			page = 0;
		}
		f_close(&gfile);
		SetPSRampage(0);
		progress_part(filesize, filesize);
		return 0;
	} else {
		return 1;
	}

	return 0;
}
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// Torii: the library's extension table (library.h), the one list of them
u32 Check_file_type(TCHAR *pfilename)
{
	return library_console_of(pfilename);
}
//---------------------------------------------------------------------------------
const char *Error_message(u8 error_num)
{
	const char *const messages[] = {gl_error_0, gl_error_1, gl_error_2, gl_error_3, gl_error_4,
					gl_error_5, gl_error_6, gl_error_7, gl_error_8, gl_error_9};
	return error_num < sizeof(messages) / sizeof(messages[0]) ? messages[error_num] : "error?";
}
//---------------------------------------------------------------------------------
static void Show_error_num(u8 error_num)
{
	const char *msg = Error_message(error_num);

	tlog_error("error %u: %s", error_num, msg);
	tlog_flush();
	dialog_message(NULL, msg, DIALOG_ERROR);
}
//---------------------------------------------------------------------------------
u32 Get_savefilesize(BYTE saveMODE)
{
	u32 savefilesize;
	switch (saveMODE) {
	case 0x00:
		savefilesize = 0x0;
		break;//no save
	case 0x11:
		savefilesize = 0x8000;
		break;//SRAM_TYPE 32k
	case 0x21:
		savefilesize = 0x200;
		break;//EEPROM_TYPE 512
	case 0x22:
		savefilesize = 0x2000;
		break;//EEPROM_TYPE 8k
	case 0x23:
		savefilesize = 0x2000;
		break;//EEPROM_TYPE v125 v126 must use 8k
	case 0x32:
		savefilesize = 0x10000;
		break;//FLASH_TYPE 64k
	case 0x33:
		savefilesize = 0x10000;
		break;//FLASH512_TYPE 64k
	case 0x31:
		savefilesize = 0x20000;
		break;//FLASH1M_TYPE 128k
	case 0xee:
		savefilesize = 0x10000;
		break;//EMU 64k
	default:
		savefilesize = 0x10000;
		break;//UNKNOW,FF  for homebrew SRAM_TYPE	//2018-4-23 some emu homebrew need 64kByte
	}
	return savefilesize;
}
//---------------------------------------------------------------------------------
// Torii: pads a .sav smaller than its save type needs (a 512-byte EEPROM
// save for a game now set to EEPROM 8K) with 0xFF, the blank chip's bytes.
// Kept short, the blocks past its end were never loaded or written back,
// so they were lost at power-off. Returns the size the file has now.
static u32 Grow_savefile(TCHAR *filename, u32 size, u32 need)
{
	FIL file;
	u32 ret = f_open(&file, filename, FA_WRITE | FA_OPEN_EXISTING);
	if (ret != FR_OK) {
		tlog_error("save: can't grow %s: f_open gave %lu", filename, (unsigned long)ret);
		return size;
	}
	u32 total = size;
	if (f_lseek(&file, size) == FR_OK) {
		memset(pReadCache, 0xFF, SAVE_WRITE_CHUNK);
		while (total < need) {
			UINT chunk = need - total < SAVE_WRITE_CHUNK ? need - total : SAVE_WRITE_CHUNK, written = 0;
			if (f_write(&file, pReadCache, chunk, &written) != FR_OK || written != chunk) {
				total += written;
				break;
			}
			total += written;
		}
	}
	// Cut short (a full card), it goes back to its old size: a size the
	// save copy can't write whole would lose the rest without an error.
	if (total < need && total > size && f_lseek(&file, size) == FR_OK && f_truncate(&file) == FR_OK)
		total = size;
	FRESULT cerr = f_close(&file);
	if (total < need || cerr != FR_OK) {
		tlog_error("save: can't grow %s from %lu to %lu bytes (a full card?): kept at %lu, f_close gave %d",
			   filename, (unsigned long)size, (unsigned long)need, (unsigned long)total, cerr);
		return cerr == FR_OK ? total : size;
	}
	tlog("save: grew %s from %lu to %lu bytes, the save type's size, padding with FF", filename,
	     (unsigned long)size, (unsigned long)need);
	return total;
}
//---------------------------------------------------------------------------------
// Torii: the chip a save type is (its code's high nibble: 1 SRAM, 2 EEPROM, 3
// Flash), and the one a file's size says it was written by (EEPROM up to
// 8 KB, SRAM's 32 KB, Flash from 64 KB); 0 for neither (no save, the
// homebrew's and the emulators' 64 KB)
static u32 Save_chip(BYTE saveMODE)
{
	u32 chip = saveMODE >> 4;
	return chip >= 1 && chip <= 3 && saveMODE != 0x10 ? chip : 0;
}

static u32 Save_chip_of_size(u32 bytes)
{
	return bytes <= 0x2000 ? 2 : bytes == 0x8000 ? 1 : bytes >= 0x10000 ? 3 : 0;
}

// The game's name for the save notices: its display name in the index, set
// before the launch (Game_boot) puts the index away; else the file's name
static char launch_display[64] EWRAM_BSS;
// Cycles a launch's save waited for the user (the type change's notice), left
// out of its "launch's save" time
static u32 save_wait_cycles EWRAM_BSS;

static const char *Launch_display(const char *rom_path)
{
	if (launch_display[0])
		return launch_display;
	const char *slash = strrchr(rom_path, '/');
	return slash ? slash + 1 : rom_path;
}

// Torii: a slot file bigger than the game's save type now needs (EEPROM 8K
// changed to 512, Flash 128K to 64K) would be cut short or half used, and one
// of another chip (EEPROM 8K changed to Flash 64K) would be read as that
// chip's: it is never cut, padded or converted. The file goes into the slot's
// history untouched, the slot starts the new type from the blank chip (the
// caller makes it as for a new game), and a notice says so once, as the file
// is gone afterwards. A smaller file of the same chip (EEPROM 512 to 8K) is
// grown by Grow_savefile: its bytes stay, so nothing is lost. Returns 0, or
// the error number to show.
static u32 __attribute__((noinline)) Retire_oversized_save(const char *savfilename, const char *hist,
							   const char *rom_path, BYTE saveMODE)
{
	FILINFO fno;
	u32 need = Get_savefilesize(saveMODE);
	if (f_stat(savfilename, &fno) != FR_OK || !fno.fsize)
		return 0;
	u32 chip = Save_chip(saveMODE), was = Save_chip_of_size((u32)fno.fsize);
	int other_chip = fno.fsize < need && chip && was && chip != was;
	if (fno.fsize <= need && !other_chip)
		return 0;
	tlog("save: %s is %lu bytes, the save type %02X needs %lu: kept in the history, the slot starts blank",
	     savfilename, (unsigned long)fno.fsize, saveMODE, (unsigned long)need);
	int r = slotstore_retire_file(savfilename, hist);
	if (r != SLOTSTORE_OK) {
		tlog_error("save: %s can't go into the history (%d): the launch stops, the file stays", savfilename, r);
		return 3;
	}
	const char *name = Launch_display(rom_path);
	tlog("save: type change notice shown for %s", name);
	char msg[260];
	int cut = (int)charset_cut(name, 60);
	snprintf(msg, sizeof(msg), EN_SAVE_TYPE_CHANGED, cut, name, (unsigned long)fno.fsize);
	u32 asked = cycles_now();
	dialog_message(NULL, msg, DIALOG_INFO);
	save_wait_cycles += cycles_since(asked);
	return 0;
}
//---------------------------------------------------------------------------------
// The launch's save: the current slot's file of the game at rom_path (its
// path from the root; gamesave.h), made if the game has none. The
// path is built here, and kept in the pending-save record. Returns 0, or the
// error number Show_error_num shows.
static struct {
	char rom[GAMESAVE_PATH_BYTES];
	char save[GAMESAVE_PATH_BYTES];
	char hist[GAMESAVE_PATH_BYTES];  // the slot's history folder
	struct gamerec rec;
} launch_save EWRAM_BSS;

static u8 Process_savefile_steps(u32 is_EMU, const char *rom_path, u32 gamefilesize, BYTE saveMODE)
{
	u32 res;
	u32 savefilesize = 0;
	TCHAR *savfilename = launch_save.save;

	// A slot or orphan operation a card error left half done is finished
	// first, so the record names the slot as it ends up
	if (slotstore_recover() < 0 || saveorphan_recover() < 0) {
		tlog_error("save: a slot operation can't be finished: the launch stops");
		return 4;
	}
	gamesave_record_read(rom_path, &launch_save.rec);
	int len = gamesave_current_path(savfilename, sizeof(launch_save.save), rom_path, &launch_save.rec);
	// The pending-save record holds the path up to SAV_INFO_TYPE: a longer one
	// would be copied back under another name. Nothing is written yet.
	int hlen = saveslot_backup_dir(launch_save.hist, sizeof(launch_save.hist), rom_path, launch_save.rec.slot);
	if (len < 0 || len > SAV_INFO_NAME_MAX || hlen < 0 || hlen > SAV_INFO_HIST_MAX) {
		tlog_error("save: the save path is over %u bytes, the pending-save record's room (slot %s): %s",
			   (unsigned)SAV_INFO_NAME_MAX, launch_save.rec.slot, rom_path);
		return 8;
	}
	//#ifdef DEBUG
	//DEBUG_printf("sav %s",savfilename);
	//DEBUG_printf("saveMODE %x",saveMODE);
	//wait_btn();
	//#endif

	Save_recover(savfilename);  // a copy a power cut interrupted
	if (!is_EMU && saveMODE != 0) {
		res = Retire_oversized_save(savfilename, launch_save.hist, rom_path, saveMODE);
		if (res)
			return (u8)res;
	}
	int blank_chip = 0;
	res = f_open(&gfile, savfilename, FA_OPEN_EXISTING);
	if (res == FR_OK)//have a old save file
	{
		savefilesize = f_size(&gfile);
		f_close(&gfile);
		tlog("save: %s exists, %lu bytes, save type %02X", savfilename, (unsigned long)savefilesize, saveMODE);
		u32 need = Get_savefilesize(saveMODE);
		if (!is_EMU && saveMODE != 0 && savefilesize < need)
			savefilesize = Grow_savefile(savfilename, savefilesize, need);
	} else if (saveMODE == 0x10) {
		// Torii: a game missing from the save table gets no file at launch:
		// the chip starts blank, and a file is made once it differs
		savefilesize = Get_savefilesize(saveMODE);
		blank_chip = 1;
		tlog("save: no %s yet, %lu bytes; the file is made when the chip differs from blank", savfilename,
		     (unsigned long)savefilesize);
	} else //make a new one
	{
		savefilesize = Get_savefilesize(saveMODE);
		tlog("save: making %s, %lu bytes", savfilename, (unsigned long)savefilesize);
		if (savefilesize && !gamesave_make_parents(savfilename))
			return 2;
		res = SavefileWrite(savfilename, savefilesize);
		if (res == 0) {
			u8 error_num = 5;
			return error_num;
		}
	}

	if (savefilesize) {
		Bank_Switching(0);
		if (blank_chip) {
			Blank_sram(savefilesize);
		} else {
			res = Loadsavefile(savfilename);
			if (!res)
				return 4;  // "Can't read the save": not launched, no record
		}

		memset(SAV_info_buffer, 0x00, sizeof(SAV_info_buffer));
		SAV_info_buffer[0] = 0x11;
		SAV_info_buffer[1] = savefilesize >> 9;
		//memcpy(&SAV_info_buffer[2],savfilename,100);
		strcpy((char *)&SAV_info_buffer[2], savfilename);
		SAV_info_buffer[SAV_INFO_TYPE] = saveMODE;
		strcpy((char *)&SAV_info_buffer[SAV_INFO_HIST], launch_save.hist);
		Save_sav_info(SAV_info_buffer, 0x200);
	}

	FAT_table_buffer[0x1F0 / 4] = gamefilesize;//size
	FAT_table_buffer[0x1F4 / 4] = DMA_COPY_MODE;  //rom copy to psram
	FAT_table_buffer[0x1F8 / 4] = (&EZcardFs)->csize;//0x40;  //secort of cluster
	FAT_table_buffer[0x1FC / 4] = (saveMODE << 24) | savefilesize;  //save mode and save file size
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0],FAT_table_buffer[1]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[2],FAT_table_buffer[3]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[4],FAT_table_buffer[5]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x200/4],FAT_table_buffer[0x204/4]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x208/4],FAT_table_buffer[0x20C/4]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x1F0/4],FAT_table_buffer[0x1F4/4]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x1F8/4],FAT_table_buffer[0x1FC/4]);

	return 0;
}

static u8 Process_savefile(u32 is_EMU, const char *rom_path, u32 gamefilesize, BYTE saveMODE)
{
	struct diskstat mark;
	diskstat_start(&mark);
	save_wait_cycles = 0;
	u8 r = Process_savefile_steps(is_EMU, rom_path, gamefilesize, saveMODE);
	mark.cycles += save_wait_cycles;  // the time the card worked, not the notice's
	diskstat_log("launch's save", &mark);
	launch_display[0] = 0;
	return r;
}
//---------------------------------------------------------------------------------
// Torii: the pending-save record (Read_sav_info): the save waiting to be
// copied from cart SRAM to the card, its file name into SAV_info_buffer and
// its size in bytes into *size. 0 if none waits.
static int pending_save(u32 *size)
{
	if (Read_sav_info(0) != 0x11) {
		tlog_debug("save: no save waiting to be copied");
		return 0;
	}
	*size = Read_sav_info(1) << 9;
	for (u32 i = 0; i < 0x200 / 2 - 2; i++)  // the record's halfwords past the flag and size
		SAV_info_buffer[i] = Read_sav_info(i + 2);
	SAV_info_buffer[0x200 / 2 - 2] = SAV_info_buffer[0x200 / 2 - 1] = 0;
	return 1;
}

int pending_save_under(const char *prefix)
{
	if (Read_sav_info(0) != 0x11)
		return 0;
	// The record's path, a halfword at a time, against prefix's bytes
	for (u32 i = 0; i <= SAV_INFO_NAME_MAX; i += 2) {
		u16 h = Read_sav_info(2 + i / 2);
		u8 a = (u8)h, b = (u8)(h >> 8);
		if (!prefix[i])
			return 1;
		if (a != (u8)prefix[i])
			return 0;
		if (!prefix[i + 1])
			return 1;
		if (b != (u8)prefix[i + 1])
			return 0;
	}
	return 1;
}

static void pending_save_clear(void)
{
	memset(SAV_info_buffer, 0x00, sizeof(SAV_info_buffer));  //clean flag
	Save_sav_info(SAV_info_buffer, 0x200);
}

// The history folder in the pending record that pending_save() read into
// SAV_info_buffer, or NULL for none (a record from an older kernel)
static const char *pending_hist_dir(void)
{
	const char *h = (const char *)SAV_info_buffer + (SAV_INFO_HIST - 2) * 2;
	if (strncmp(h, SAVESLOT_BACKUPS_DIR "/", sizeof(SAVESLOT_BACKUPS_DIR)) != 0 ||
	    strnlen(h, SAV_INFO_HIST_MAX + 1) > SAV_INFO_HIST_MAX)
		return NULL;
	return h;
}

// Copies the pending save (SAV_info_buffer holds its path) to the card, safely
// (Save_savefile): first any copy a power cut interrupted is finished or
// undone, then a blank or unchanged chip is skipped and a chip that doesn't
// look like the file asks, then the progress dialog. SAVE_WRITTEN,
// SAVE_SKIPPED, SAVE_NOTHING (also when the question got B) or SAVE_FAILED
// (no "Done"). The dialog must be open.
static u32 pending_save_copy(u32 size)
{
	TCHAR *name = (TCHAR *)SAV_info_buffer;
	Save_recover(name);
	u32 check = Save_check(name, size);
	if (check == SAVE_MISMATCH) {
		char msg[200];
		int cut = (int)charset_cut(name, 80);
		snprintf(msg, sizeof(msg), EN_SAVE_MISMATCH, cut, name);
		if (!dialog_confirm(NULL, msg, DIALOG_ERROR)) {
			tlog("save: the chip doesn't look like %s: B, the file stays", name);
			return SAVE_NOTHING;
		}
		tlog("save: the chip doesn't look like %s: A, written over it", name);
	} else if (check != SAVE_WRITE) {
		return check;
	}
	dialog_progress_begin(gl_saving, name);
	progress_step(gl_save_ing, 0, PROGRESS_FULL);
	struct diskstat mark;
	diskstat_start(&mark);
	u32 saved = Save_savefile(name, size, pending_hist_dir());
	diskstat_log("save copy", &mark);
	if (saved == SAVE_WRITTEN)
		progress_done(gl_done);
	dialog_progress_end();
	return saved;
}

static void Check_save_flag(void)
{
	u32 savefilesize;
	if (!pending_save(&savefilesize))
		return;
	// Torii: the answer is logged. The question and the saving are
	// dialogs on the engine (source/ui/dialog.h)
	static const char saved_how[] = "save: \"Save sav?\" for %s, %lu bytes, save type %02X: %s";
	u16 savetype = Read_sav_info(SAV_INFO_TYPE);
	TCHAR *name = (TCHAR *)SAV_info_buffer;
	dialog_open();
	int save = gl_auto_save_sel || dialog_confirm(gl_save_sav, name, DIALOG_INFO);
	tlog(saved_how, name, (unsigned long)savefilesize, savetype,
	     gl_auto_save_sel ? "auto"
	     : save           ? "A, save"
			      : "B, not saved");
	if (save && pending_save_copy(savefilesize) == SAVE_FAILED) {
		// Torii: not "Done" over a save the card didn't take, and the
		// record stays: the next launch, or power-on, tries again
		Show_error_num(3);
		tlog("save: the pending-save record stays for the next try");
		dialog_close();
		return;
	}
	if (!save)
		Save_recover(name);  // a copy a cut left (pending_save_copy's own when saving)
	dialog_close();
	pending_save_clear();
	cutpoint("save: pending record cleared");
}

// Torii: before a launch hands cart SRAM to another game. A save still
// waiting to be copied (a power-on copy the card couldn't take) would be
// lost, and its record would name the wrong game: the next power-on would
// copy the other game's SRAM over its file. So the copy runs again (the
// card may have room now); if it fails again, the user picks: launch anyway
// (the record goes, the save is lost) or keep it. 1: go on, 0: don't (the
// record stays).
int pending_save_flush(void)
{
	u32 size;
	if (!pending_save(&size))
		return 1;
	tlog("save: %s waits to be copied: first that", (const char *)SAV_info_buffer);
	dialog_open();
	u32 r = pending_save_copy(size);
	dialog_close();
	if (r == SAVE_FAILED)
		return 0;
	pending_save_clear();
	return 1;
}

static int pending_save_settle(void)
{
	u32 size;
	if (!pending_save(&size))
		return 1;
	TCHAR *name = (TCHAR *)SAV_info_buffer;
	tlog("save: %s still waits to be copied; trying again", name);
	dialog_open();
	int go = 1;
	if (pending_save_copy(size) == SAVE_FAILED) {
		char msg[160];
		int cut = (int)charset_cut(name, 80);
		snprintf(msg, sizeof(msg), EN_SAVE_UNSAVED, cut, name);
		go = dialog_confirm(NULL, msg, DIALOG_ERROR);
		tlog("save: %s isn't on the card: %s", name, go ? "A, launch anyway, it is lost" : "B, kept");
	}
	dialog_close();
	if (go)
		pending_save_clear();
	return go;
}
//---------------------------------------------------------------------------------
static void Set_saveMODE(BYTE saveMODE)
{
	u32 address;
	for (address = 0; address < assress_max; address++) {
		SET_info_buffer[address] = Read_SET_info(address);
	}

	SET_info_buffer[assress_saveMODE] = saveMODE;

	//save to nor
	if (!Save_SET_info(SET_info_buffer))
		tlog_error("nor: the first NOR game's save type %02X wasn't saved", saveMODE);
}
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------
int main(void)
{
	// A soft reset or an in-game RESET keeps the last WAITCNT; the firmware
	// update below runs at the BIOS's, the menu after it faster (waitcnt.h).
	REG_WAITCNT_CART = WAITCNT_BIOS;
	tlog_init();
	tlog_marker("power-on or reset", 0);
	torii_run_tests();
	irqInit();
	cartlog_install();
	irqEnable(IRQ_VBLANK);
	cartlog_boot_start();

	REG_IME = 1;

	u32 res;

	gl_currentpage = 0x8002;//kernel mode

	dialog_blank();  // black until the boot's screens are up

	SD_Disable();
	Set_RTC_status(1);

	//check FW
	Check_FW_update();  // its screens, if any, in an engine session of their own
	REG_WAITCNT_CART = WAITCNT_MENU;
	tlog_debug("boot: WAITCNT %04X for the menu", REG_WAITCNT_CART);

	CheckLanguage();
	CheckSwitch();
	settings_log();

	// The boot's screens (a card error, the save question)
	// share one engine session. A card that mounts shows nothing unless the
	// rest of the boot is slow (Boot_notice_check): it takes a few frames,
	// and a notice ("microSD card ready / Loading...") only flashed before
	// the list.
	dialog_open();
	res = f_mount(&EZcardFs, "", 1);
	// Torii: a card that doesn't answer gets a few more tries, SD mode off
	// and a pause between, before the error that asks for a power-off: one
	// left busy by a game's own SD access (the in-game probe's) may answer
	// again, and a power-off loses cart RAM page 0xF0 (cartlog.h)
	for (u32 retry = 1; res != FR_OK && retry <= BOOT_MOUNT_RETRIES; retry++) {
		tlog_error("boot: the card didn't mount (%u); trying again (%lu of %u)", (unsigned)res,
			   (unsigned long)retry, BOOT_MOUNT_RETRIES);
		SD_Disable();
		for (u32 f = 0; f < BOOT_MOUNT_PAUSE; f++)
			VBlankIntrWait();
		res = f_mount(&EZcardFs, "", 1);
	}
	if (res != FR_OK) {
		cartlog_mount_failed();
		Show_boot_notice(gl_init_error, gl_power_off, DIALOG_ERROR);
		while (1)
			ui_frame();
	}
	boot_mounted = torii_frames;
	fathint_start();  // the FAT's first free cluster, while the boot's screens show
	slotstore_recover();  // a slot rename or delete the power cut short, before any save is touched
	saveorphan_recover();  // a relink or delete of an orphaned save the same
	shotstore_recover();  // a screenshot a power cut left half written
	slotstore_trash_purge(NULL);  // deleted slots past their 30 days

	// Diagnostics also starts from these keys held at power-on, in case the
	// menu itself is broken. Read once the card is up, a second into the boot.
	u32 diag_keys = (~REG_KEYINPUT & 0x3FF) == DIAG_BOOT_KEYS;
	tlog_debug("boot: keys held %03X", ~REG_KEYINPUT & 0x3FF);
	Check_save_flag();
	ingame_after_game();  // what the in-game menu left for the kernel
	Boot_notice_check();
	tlog_sd_ready(Boot_notice_check);
	cartlog_boot_report();
	tlog_flush();
	Boot_notice_check();
	if (diag_keys) {
		dialog_notice_end();
		dialog_close();  // Diagnostics runs its own session
		Diagnostics();   // back only if B didn't start it: the menu starts as usual
	}

	f_chdir("/");
	//TCHAR currentpath[MAX_path_len];
	memset(currentpath, 00, MAX_path_len);

	res = f_getcwd(currentpath, sizeof currentpath / sizeof *currentpath);

	Read_NOR_info();
	gl_norOffset = 0x000000;
	// Torii: reading the list writes nothing. The official kernel erased the
	// list whenever it read no games (a chip left in a command mode, a read
	// glitch), and the next write then went over them.
	game_total_NOR = GetFileListFromNor();//initialize to prevent direct writes to NOR without page turning
	Migrate_NOR_list(game_total_NOR);
	action_rename_recover();  // a rename the power cut short, before anything reads the card's games
	library_boot();  // the index, or the first scan
	Boot_notice_check();
	tlog_debug("boot: handing over %lu frames after mounting", (unsigned long)(torii_frames - boot_mounted));
	dialog_hand_over();  // a notice fades out; the menu runs in this session

	cartlog_phase(PHASE_MENU, 0, NULL);
	nav_run();  // the menu: never returns
}
//---------------------------------------------------------------
// Torii: the last step of every launch. Logs the rest of the launch record,
// notes in cart RAM that the game started, writes the log to the card, then
// starts the game.
static void Launch_now(u16 page, u32 key_L, const char *how)
{
	// Every launch path ends here: no cover job keeps a file open past this
	// point, and the cache in PSRAM is forgotten (the SD paths did so before
	// loading; a second call does nothing)
	cover_cache_invalidate();
	userdata_invalidate();
	if (tlog_wants(TLOG_INFO)) {
		tlog("launch: %s, %s reset", how, key_L ? "hard (L held)" : "soft");
		static const char *const uses[] = {"neither", "the clock", "rumble", "the clock and rumble"};
		tlog("launch: CART clock %s, rumble %s; the game uses %s; GPIO port %s", gl_game_clock ? "on" : "off",
		     gl_game_rumble ? "on" : "off", uses[gl_game_uses & 3], Game_GPIO_status() ? "on" : "off");
	}
	tlog("launch: starting the game");
	cartlog_phase(PHASE_GAME, 0, NULL);
	tlog_flush();
	SetRompageWithHardReset(page, key_L);
}
//---------------------------------------------------------------------------------
// A launch's steps, as where each ends in thousandths of the bar (progress.h):
// the status names only "Loading the game" and "Patching the game", and the
// quick checks in between move the bar and are logged
enum {
	LAUNCH_SAVE = 50,       // the save checked (or made) and loaded into cart SRAM
	LAUNCH_STATES = 100,    // BOOT WITH ADDON: the save state file, the IRQ hook looked up
	LAUNCH_SCAN = 800,      // the scan engine's load: the kernel reads the ROM itself
	LAUNCH_PATCH = 900,     // the game loaded, before the patches
	LAUNCH_COPIED = 950,    // the cart's copy with nothing to patch, then the ROM's fixes
	LAUNCH_NOR_SAVE = 500,  // a NOR game: its save, then its save states
};
static void Boot_NOR_game(u32 show_offset, u32 file_select, u32 key_L)
{
	library_before_launch();
	cover_cache_invalidate();  // before the save files are touched, as boot_game does
	//TCHAR savfilename[100];
	TCHAR *pfilename;
	u32 gamefilesize = 0;
	BYTE SAVEMODE;
	BYTE error_num;

	// The launch's progress, until the game starts (NOR_launch ends it
	// otherwise), set up at the menu's wait states (as Game_boot's)
	REG_WAITCNT_CART = WAITCNT_MENU;
	dialog_progress_begin(pNorFS[show_offset + file_select].filename, NULL);
	REG_WAITCNT_CART = WAITCNT_BIOS;
	progress_step(gl_loading_game, 0, LAUNCH_NOR_SAVE);

	init_FAT_table();
	// Torii: the state buffer's write-back goes to /torii/rts.bin (fattable.h)
	if (!rts_sink_prepare()) {
		Show_error_num(1);
		return;
	}

	//boot nor game
	pfilename = pNorFS[show_offset + file_select].filename;
	gamefilesize = pNorFS[show_offset + file_select].filesize;
	SAVEMODE = pNorFS[show_offset + file_select].savemode;
	memcpy(GAMECODE, &pNorFS[show_offset + file_select].gamename[0xC], 4);  //header 0xA0: title, then game code
	cartlog_phase(PHASE_LAUNCH, 0, pfilename);
	// savestates: 1 if the launch makes the state file. Older kernels wrote
	// an erased SAVESTATE slot's 0xFFFF into the entry, which counts as on
	tlog("launch: loading %s from NOR at %08lX: code %.4s, %lu bytes, save type %02X, patched %u, savestates %u",
	     pfilename, (unsigned long)pNorFS[show_offset + file_select].rompage << 17, (const char *)GAMECODE,
	     (unsigned long)gamefilesize, SAVEMODE, pNorFS[show_offset + file_select].have_patch != 0,
	     pNorFS[show_offset + file_select].have_RTS != 0);
	tlog_flush();

	//memcpy(savfilename,pfilename,100);
	if (gamesave_nor_path(launch_save.rom, sizeof(launch_save.rom), pfilename) < 0)
		error_num = 8;
	else
		error_num = Process_savefile(0, launch_save.rom, gamefilesize, SAVEMODE);
	if (error_num != 0) {
		Show_error_num(error_num);
		return;
	}
	{
		char entry[sizeof(USERDATA_NOR) + sizeof(pNorFS[0].filename)];
		snprintf(entry, sizeof(entry), USERDATA_NOR "%.99s", pfilename);
		userdata_recent_add(entry);
	}

	Set_64MROM_flag(pNorFS[show_offset + file_select].is_64MBrom);
	// Torii (v0.8): the save states' slots (statestore.h), the newest in
	// cart SRAM for the in-game menu's Load state; no FAT run table for the
	// FPGA (the old /RTS file's write-through stays off until the cart is probed)
	ingame_states_none();
	if (pNorFS[show_offset + file_select].have_patch && pNorFS[show_offset + file_select].have_RTS) {
		progress_step(NULL, LAUNCH_NOR_SAVE, PROGRESS_FULL);
		ingame_states_launch(launch_save.rom, launch_save.rec.slot, pfilename, 0);
	}

	FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
	if (Send_FATbuffer(FAT_table_buffer, 1)) {  //only RTS FAT and some parameter
		Show_error_num(1);
		return;
	}
	//wait_btn();
	gl_game_uses = Game_hardware(0, GAMECODE, 0);
	ingame_cheats_clear();  // a NOR copy's cheats are as it was written: no list
	if (pNorFS[show_offset + file_select].have_patch)
		ingame_prepare(pfilename, gamefilesize, launch_save.rom);  // the in-game menu, in cart SRAM
	progress_done(NULL);
	Launch_now(pNorFS[show_offset + file_select].rompage, key_L, "NOR game");
	while (1)
		;
}
//---------------------------------------------------------------

// A game on its way to its launch (ezkernel.h).
u32 Game_pick(struct game_pick *g)
{
	g->is_emu = Check_file_type(g->name);
	g->save_num = g->old_save_num = 0;
	g->boot_mode = 0;
	if (g->is_emu == 0xff)  //can not boot
		return 0;
	if (g->is_emu) {  //GB, GBC and NES games only have CLEAN BOOT: start them without the menu
		g->save_num = 0xF;
		return 1;
	}
	f_chdir(currentpath);  //can open  re list game
	g->old_save_num = Check_mde_file(g->name, &g->boot_mode);
	g->save_num = g->old_save_num;
	return 1;
}
//---------------------------------------------------------------------------------
// Whether a ROM header's bytes are all one blank value (00 or FF)
static int rom_header_blank(const u8 *h, u32 n)
{
	for (u32 i = 1; i < n; i++)
		if (h[i] != h[0])
			return 0;
	return h[0] == 0x00 || h[0] == 0xFF;
}
//---------------------------------------------------------------------------------
// The launch itself, as the v0.3 game menu's line MENU_line did (0 CLEAN BOOT, 1
// BOOT WITH ADDON, 2 WRITE TO NOR CLEAN, 3 WRITE TO NOR ADDON), with no
// engine session up.
static u8 Game_boot(TCHAR *pfilename, u32 is_EMU, u32 MENU_line, u8 Save_num, u8 old_Save_num, u32 key_L)
{
	u32 res;
	u8 error_num;
	BYTE SAVEMODE;
	// The game's display name for the save's notices, while the index is up
	launch_display[0] = 0;
	if (gamesave_rom_path_in(launch_save.rom, sizeof(launch_save.rom), currentpath, pfilename) >= 0) {
		u32 rec = library_find(launch_save.rom);
		if (rec != LIBRARY_NONE) {
			psram_begin();
			snprintf(launch_display, sizeof(launch_display), "%s", library_display(rec));
		}
	}
	// The launch fills PSRAM with the game: the library index and the cover
	// cache there are gone
	library_before_launch();
	cover_cache_invalidate();
	// The launch's progress, until the game starts (Game_launch ends it
	// otherwise): the game's name, and the bar over every step below. It
	// sets up at the menu's wait states, as the menu draws: at the BIOS's
	// the black screen before it lasted 3 frames longer
	REG_WAITCNT_CART = WAITCNT_MENU;
	dialog_progress_begin(pfilename, NULL);
	REG_WAITCNT_CART = WAITCNT_BIOS;

	u32 gamefilesize = 0;
	//u32 savefilesize=0;
	u32 ret;
	u32 have_pat = 0;

	init_FAT_table();
	// Torii: the state buffer's write-back goes to /torii/rts.bin, a file
	// of the kernel's, at every launch (fattable.h): with nothing there the
	// FPGA's walk ran on into the game's runs and wrote a state over the
	// game's file (the real cart, 2026-10-27)
	if (!rts_sink_prepare()) {
		Show_error_num(1);
		return 0;
	}

	//Load to PSRAM or NOR

	f_chdir(currentpath);  //return to game folder
	res = f_open(&gfile, pfilename, FA_READ);
	if (res == FR_OK) {
		u8 gb_cart_type = 0;
		u8 header[0x20];
		f_lseek(&gfile, 0xA0);
		FRESULT hres = f_read(&gfile, header, sizeof(header), (UINT *)&ret);
		if (hres != FR_OK || ret != sizeof(header))
			memset(header, 0, sizeof(header));
		memcpy(GAMECODE, header + 0xC, 4);
		// Torii: a GBA ROM whose header reads blank (all 00 or all FF) is
		// a damaged file or a card that doesn't read: the cart would copy
		// the same and start nothing, a white screen (the in-game probe on
		// the real cart, 2026-10-27, at three save types after one read)
		if (!is_EMU && rom_header_blank(header, sizeof(header))) {
			tlog_error("launch %s: its header at 0xA0 reads blank (FatFs %u, %lu bytes read): not started",
				   pfilename, (unsigned)hres, (unsigned long)ret);
			f_close(&gfile);
			Show_error_num(1);
			return 0;
		}
		if (!is_EMU)
			tlog("launch: header: title %.12s, code %.4s, maker %.2s, version %u, check %02X",
			     (char *)header, (char *)header + 0xC, (char *)header + 0x10, header[0x1C], header[0x1D]);
		if (is_EMU == 1 || is_EMU == 2) {
			f_lseek(&gfile, 0x147);
			f_read(&gfile, &gb_cart_type, 1, (UINT *)&ret);
		}
		gamefilesize = f_size(&gfile);
		f_close(&gfile);
		gl_game_uses = Game_hardware(is_EMU, GAMECODE, gb_cart_type);
	} else {
		// A Recent entry for a file that has since moved: launching
		// would make its save and start an emulator with no ROM. A file
		// that is gone comes off Recent (not on a card error,
		// which may pass)
		tlog("launch %s: can't open it (FatFs %lu)", pfilename, (unsigned long)res);
		if (res == FR_NO_FILE || res == FR_NO_PATH)
			userdata_forget_file(currentpath, pfilename);
		Show_error_num(1);
		return 0;
	}

	//check

	SAVEMODE = Get_saveMODE(Save_num, gamefilesize);
	tlog("launch %s: code %.4s, %lu bytes, save mode %02X, uses%s%s", pfilename, is_EMU ? "-" : (char *)GAMECODE,
	     (unsigned long)gamefilesize, SAVEMODE, (gl_game_uses & GAME_USES_CLOCK) ? " clock" : "",
	     (gl_game_uses & GAME_USES_RUMBLE) ? " rumble" : (gl_game_uses ? "" : " nothing"));
	// A GBA game the cart can't load fails here, before its save or Recently
	// Played entry is written; the table is filled again below
	if (!is_EMU) {
		res = Check_game_RTS_FAT(pfilename, 1);
		if (res != 0) {
			Show_error_num((res == RTS_FAT_FRAGMENTED) ? 7 : 1);
			return 0;
		}
	}
	if (MENU_line < 2) {  //work for psram
		if (gamefilesize > 0x2000000) {
			dialog_message(NULL, gl_file_overflow, DIALOG_ERROR);
			return 0;
		}

		progress_step(gl_loading_game, 0, LAUNCH_SAVE);
		if (gamesave_rom_path_in(launch_save.rom, sizeof(launch_save.rom), currentpath, pfilename) < 0)
			error_num = 8;
		else
			error_num = Process_savefile(is_EMU, launch_save.rom, gamefilesize, SAVEMODE);
		if (error_num != 0) {
			Show_error_num(error_num);
			return 0;
		}
		userdata_recent_add_file(currentpath, pfilename);
	}
	// From here the launch may fill PSRAM, where the user's lists are
	userdata_invalidate();

	if (is_EMU)  //boot emu game
	{
		progress_step(NULL, LAUNCH_SAVE, PROGRESS_FULL);
		f_chdir(currentpath);  //return to game folder
		cartlog_phase(PHASE_LAUNCH, 0, pfilename);
		tlog("launch: loading %s with %s", pfilename, (is_EMU == 3) ? "PocketNES" : "Jagoomba Color");
		tlog_flush();

		FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
		ingame_states_none();  // no state of another game's for the cart to write back
		if (Send_FATbuffer(FAT_table_buffer, 1)) {
			Show_error_num(1);
			return 0;
		}

		res = LoadEMU2PSRAM(pfilename, is_EMU);
		if (res) {  // Torii: not started from a ROM read in part
			Show_error_num(1);
			return 0;
		}
		progress_done(NULL);
		Launch_now(0x200, key_L, (is_EMU == 3) ? "plugin PocketNES" : "plugin Jagoomba Color");
		while (1)
			;
	} else {  //gba file
		if (old_Save_num != Save_num) {
			error_num = Make_mde_file(pfilename, Save_num);
			if (error_num)
				tlog_error("save type: %s's record wasn't written", pfilename);
		}

		f_chdir(currentpath);  //return to game folder
		res = Check_game_RTS_FAT(pfilename, 1);  //game FAT
		if (res != 0) {
			error_num = (res == RTS_FAT_FRAGMENTED) ? 7 : 1;
			Show_error_num(error_num);
			return 0;
		}

		switch (MENU_line) {
		case 0:  //DirectPSRAM CLEAN BOOT
			cartlog_phase(PHASE_LAUNCH, 0, pfilename);
			tlog("launch: loading %s, CLEAN BOOT", pfilename);
			ingame_probe_prepare(GAMECODE);  // the in-game probe ROM's scratch file
			// A state another launch left (its flag set) goes: the cart would
			// write it back (fattable.h)
			ingame_states_none();
			tlog_flush();

			if (progress_cart_copy(FAT_table_buffer, gamefilesize, LAUNCH_COPIED)) {
				Show_error_num(1);
				return 0;
			}
			GBApatch_Cleanrom(PSRAMBase_S98, gamefilesize);
			progress_done(NULL);
			//wait_btn();
			Launch_now(0x200, key_L, "CLEAN BOOT");
			break;
		case 1:  //PSRAM BOOT WITH ADDON
			Read_addon_switches();
			ingame_states_none();
			rom_guard_reset();  // Torii: the patches below mark what the cheats' keep off
			cartlog_phase(PHASE_LAUNCH, 0, pfilename);
			tlog("launch: loading %s, BOOT WITH ADDON: reset %u, savestate %u, sleep %u, cheats %u (%lu), "
			     "%lu more off for the in-game list",
			     pfilename, gl_reset_on, gl_rts_on, gl_sleep_on, gl_cheat_on, (unsigned long)gl_cheat_picks,
			     (unsigned long)(gl_cheat_count - gl_cheat_picks));
			ingame_probe_prepare(GAMECODE);  // the in-game probe ROM's scratch file
			tlog_flush();
			if ((gl_reset_on == 1) || (gl_rts_on == 1) || (gl_sleep_on == 1) || (gl_cheat_on == 1)) {
				// Torii (v0.8): the save states, as Boot_NOR_game's, and
				// the slot to start in when one was asked for
				if (gl_rts_on == 1) {
					progress_step(NULL, LAUNCH_SAVE, LAUNCH_STATES);
					// A slot the game popup's Load state picked that doesn't
					// read: the player says whether the game starts anyway
					if (!ingame_states_launch(launch_save.rom, launch_save.rec.slot, pfilename,
								  action_state_slot()) &&
					    !dialog_confirm(gl_states->unreadable, gl_states->start_anyway,
							    DIALOG_ERROR)) {
						tlog("launch: %s not started: its state slot %lu doesn't read",
						     pfilename, (unsigned long)action_state_slot());
						return 0;
					}
				}
				progress_step(NULL, LAUNCH_STATES, LAUNCH_STATES);
				have_pat = Check_pat(pfilename);
				if (have_pat == 1) {
					tlog("launch: IRQ hook from PATCH/ (cached scan)");
					if (progress_cart_copy(FAT_table_buffer, gamefilesize, LAUNCH_PATCH)) {
						Show_error_num(1);
						return 0;
					}
				} else  //(have_pat==0)
				{
					f_chdir(currentpath);  //return to game folder
					//get the location for the patch
					res = f_open(&gfile, pfilename, FA_READ);
					u32 last = (gamefilesize - 1) & 0xFFFE0000;
					if (res == FR_OK && f_lseek(&gfile, last) == FR_OK)
						res = f_read(&gfile, pReadCache, 0x20000, (UINT *)&ret);
					f_close(&gfile);
					if (rom_read_failed(pfilename, res, last, ret, gamefilesize)) {
						Show_error_num(1);  // Torii: no hook placed from a failed read
						return 0;
					}
					SetTrimSize(pReadCache, gamefilesize, 0x20000, 0x0, SAVEMODE);
					if (gl_engine_sel == 0) {
						tlog("launch: IRQ hook from a scan of the ROM (scan engine)");
					get_find:
						FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
						if (Send_FATbuffer(FAT_table_buffer, 1)) {
							Show_error_num(1);
							return 0;
						}
						progress_step(NULL, LAUNCH_STATES, LAUNCH_SCAN);
						res = Loadfile2PSRAM(pfilename);
						if (res) {  // Torii: not started from a ROM read in part
							Show_error_num(1);
							return 0;
						}
						progress_step(NULL, LAUNCH_SCAN, LAUNCH_PATCH);
						tlog("launch: saving the hook's place in PATCH/");
						Make_pat_file(pfilename);
					} else {
						res = use_internal_engine(GAMECODE, pfilename);
						if (res == 1) {
							tlog("launch: IRQ hook from the built-in table");
							if (progress_cart_copy(FAT_table_buffer, gamefilesize,
									       LAUNCH_PATCH)) {
								Show_error_num(1);
								return 0;
							}
						} else {
							tlog("launch: IRQ hook from a scan of the ROM (not in the "
							     "table)");
							goto get_find;
						}
					}
				}
				u32 found = iCount2;
				Patch_SpecialROM_sleepmode();  //
				if (iCount2 > found)
					tlog("launch: %lu code patches for this game's IRQ setup",
					     (unsigned long)(iCount2 - found));
				found = iCount2;
				progress_step(gl_patching, LAUNCH_PATCH, PROGRESS_FULL);
				Apply_rom_patches();  //Torii: exact-ROM patches, SD launches only
				if (iCount2 > found)
					tlog("launch: exact-ROM patch for %.4s (irqpatch.c rom_patches)",
					     (char *)GAMECODE);
				GBApatch_PSRAM(PSRAMBase_S98, gamefilesize);
				Log_patches();
			} else {  //no select switch ,CLEAN
				tlog("launch: no addon on, so no patches");
				if (progress_cart_copy(FAT_table_buffer, gamefilesize, LAUNCH_COPIED)) {
					Show_error_num(1);
					return 0;
				}
			}
			// Torii: the picked cheats' ROM patches, into the game in PSRAM
			if (gl_cheat_patches)
				cheatstore_launch(launch_save.rom, gamefilesize);
			ingame_prepare(pfilename, gamefilesize, launch_save.rom);  // the in-game menu, in cart SRAM
			progress_done(NULL);
			//wait_btn();
			Launch_now(0x200, key_L, "BOOT WITH ADDON");
			break;
		case 2:  //WRITE TO NOR CLEAN
			f_chdir(currentpath);  //return to game folder
			progress_step(gl_writing, 0, PROGRESS_FULL);  // the whole bar
			res = Loadfile2NOR(pfilename, gl_norOffset, 0x0, SAVEMODE);
			if (res == 0)  //ok
			{
				if (gl_norOffset == 0)  //first game need set saveMODE
				{
					Set_saveMODE(SAVEMODE);
				}
				return 2;
			} else if (res == 2) {
				dialog_message(NULL, gl_NOR_full, DIALOG_ERROR);  //"NOR FULL!"
				return 1;
			} else {  // Torii: the file or the NOR failed; the game isn't listed
				Show_error_num(1);
				return 1;
			}
			break;
		case 3:  //WRITE TO NOR ADDON
			Read_addon_switches();
			if (gl_cheat_patches)
				tlog("launch: %lu ROM patches of the picked cheats left out: a NOR game has no PSRAM "
				     "image to patch",
				     (unsigned long)gl_cheat_patches);

			f_chdir(currentpath);  //return to game folder
			u32 needpatch = 0;
			if ((gl_reset_on == 1) || (gl_rts_on == 1) || (gl_sleep_on == 1) || (gl_cheat_on == 1)) {
				Patch_SpecialROM_sleepmode();  //

				//get the location of the patch
				res = f_open(&gfile, pfilename, FA_READ);
				if (res == FR_OK) {
					u32 last = (gamefilesize - 1) & 0xFFFE0000;
					res = f_lseek(&gfile, last);
					if (res == FR_OK)
						res = f_read(&gfile, pReadCache, 0x20000, (UINT *)&ret);
					f_close(&gfile);
					if (rom_read_failed(pfilename, res, last, ret, gamefilesize)) {
						Show_error_num(1);  // Torii: nothing burned from a failed read
						return 1;
					}
					SetTrimSize(pReadCache, gamefilesize, 0x20000, 0x1, SAVEMODE);
				}
				needpatch = 1;
			}
			progress_step(gl_writing, 0, PROGRESS_FULL);  // the whole bar
			res = Loadfile2NOR(pfilename, gl_norOffset, needpatch, SAVEMODE);
			//wait_btn();
			if (res == 0) {
				if (gl_norOffset == 0)  //first game need set saveMODE
				{
					Set_saveMODE(SAVEMODE);
				}
				return 2;
			} else if (res == 2) {
				dialog_message(NULL, gl_NOR_full, DIALOG_ERROR);  //"NOR FULL!"
				return 0;
			} else {  // Torii: the file or the NOR failed; the game isn't listed
				Show_error_num(1);
				return 1;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}  //---------------------------------------------------------------------------------
u32 Game_save_bytes(const struct game_pick *g)
{
	u8 header[0x20];
	memset(header, 0, sizeof(header));
	f_chdir(currentpath);
	if (f_open(&gfile, g->name, FA_READ) != FR_OK)
		return 0;
	UINT got = 0;
	f_lseek(&gfile, 0xA0);
	if (f_read(&gfile, header, sizeof(header), &got) != FR_OK || got != sizeof(header))
		memset(header, 0, sizeof(header));
	u32 size = f_size(&gfile);
	f_close(&gfile);
	memcpy(GAMECODE, header + 0xC, 4);  // as Game_boot reads it
	return Get_savefilesize(Get_saveMODE(g->save_num, size));
}
//---------------------------------------------------------------------------------
u8 Game_launch(struct game_pick *g, u32 menu_line, u32 key_L)
{
	// CLEAN BOOT and BOOT WITH ADDON load the game's save into cart SRAM
	// (WRITE TO NOR doesn't touch it)
	if (menu_line < 2 && !pending_save_settle())
		return 0;
	// A launch loads and patches the game in PSRAM, writes never measured
	// faster than the BIOS's wait states (waitcnt.h)
	REG_WAITCNT_CART = WAITCNT_BIOS;
	u8 res = Game_boot(g->name, g->is_emu, menu_line, g->save_num, g->old_save_num, key_L);
	dialog_progress_end();  // the launch's, if it failed or wrote to NOR: a black screen
	REG_WAITCNT_CART = WAITCNT_MENU;
	cartlog_phase(PHASE_MENU, 0, NULL);
	return res;
}
//---------------------------------------------------------------------------------
void NOR_launch(u32 index, u32 key_L)
{
	if (!pending_save_settle())
		return;
	REG_WAITCNT_CART = WAITCNT_BIOS;  // as Game_launch
	Boot_NOR_game(0, index, key_L);  // returns only if it failed
	dialog_progress_end();  // a black screen
	REG_WAITCNT_CART = WAITCNT_MENU;
	cartlog_phase(PHASE_MENU, 0, NULL);
}
