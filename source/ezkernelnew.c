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

#include "ez_define.h"
#include "ff.h"
#include "draw.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "saveMODE.h"
#include "RTC.h"
#include "tlog.h"
#include "waitcnt.h"
#include "sort.h"
#include "savename.h"
#include "NORflash_OP.h"
#include "lang.h"
#include "GBApatch.h"
#include "showcht.h"
#include "helpwindow.h"
#include "cartlog.h"
#include "diag.h"
#include "engine/cover.h"
#include "screens/lists.h"

#include "jagoombacolor.h"
#include "pocketnes.h"

FM_NOR_FS pNorFS[MAX_NOR] EWRAM_BSS;

u32 FAT_table_buffer[FAT_table_size / 4] EWRAM_BSS;
u8 pReadCache[MAX_pReadCache_size] EWRAM_BSS;  // a scratch buffer: its owners are in ez_define.h
_Static_assert(READCACHE_SORT_WORK >= 0x1E0 && READCACHE_SORT_WORK % 4 == 0 &&
		       READCACHE_SORT_WORK + SORT_WORK_SIZE <= 0x2000,
	       "the sort's work buffer meets Clear()'s line or the cheat list");

char p_recently_play[10][RECENT_LEN] EWRAM_BSS ALIGN(4);
TCHAR currentpath[MAX_path_len];//
TCHAR currentpath_temp[MAX_path_len];
TCHAR current_filename[GAME_NAME_MAX + 1] EWRAM_BSS;

u32 game_total_SD;
u32 game_total_NOR;
u32 folder_total;

u32 gl_currentpage;
u32 gl_norOffset;
u32 gl_lang;
u16 gl_engine_sel;

u16 gl_show_Thumbnail;
u16 gl_game_clock;
u16 gl_game_rumble;

u8 __attribute__((aligned(4))) GAMECODE[4];

FATFS EZcardFs;
FILINFO fileinfo;
DIR dir;
FIL gfile;

u16 gl_reset_on;
u16 gl_rts_on;
u16 gl_sleep_on;
u16 gl_cheat_on;

u16 gl_auto_save_sel;
u16 gl_ModeB_init;

u16 gl_led_open_sel;
u16 gl_Breathing_R;
u16 gl_Breathing_G;
u16 gl_Breathing_B;

u16 gl_SD_R;
u16 gl_SD_G;
u16 gl_SD_B;

//----------------------------------------
/*u16 gl_color_selected 		= RGB(00,20,26);
u16 gl_color_text 				= RGB(31,31,31);
u16 gl_color_selectBG_sd 	= RGB(00,00,31);
u16 gl_color_selectBG_nor = RGB(10,10,10);
u16 gl_color_MENU_btn			= RGB(20,20,20);
u16 gl_color_cheat_count  = RGB(00,31,00);
u16 gl_color_cheat_black  = RGB(00,00,00);
u16 gl_color_NORFULL      = RGB(31,00,00);
u16 gl_color_btn_clean    = RGB(00,00,31);
*/
u16 SAV_info_buffer[0x200];//Torii: IWRAM, so EWRAM data fits in 256 KB
// The pending-save record in the kernel's NOR: 0x11, the size in 512-byte
// units, then the save's file name from halfword 2. Torii keeps the save
// type at halfword SAV_INFO_TYPE, past the longest name, for the log (0
// from older kernels).
#define SAV_INFO_TYPE 0x80
// The longest save file name the record holds, from byte 4 to SAV_INFO_TYPE
#define SAV_INFO_NAME_MAX (SAV_INFO_TYPE * 2 - 4 - 1)
//******************************************************************************
void delay(u32 R0)
{
	int volatile i;

	for (i = R0; i; --i)
		;
	return;
}
//---------------------------------------------------------------------------------
void wait_btn()
{
	while (1) {
		scanKeys();
		u16 keys = keysUp();
		if (keys & KEY_B) {
			break;
		}
	}
	//while(*(vu16*)0x04000130 == 0x3FF );
	//while(*(vu16*)0x04000130 != 0x3FF );
}
//---------------------------------------------------------------------------------

//---------------------------------------------------------------------------------
static void Show_MENU_btn(void)
{
	char msg[30];
	Clear(60, 118 - 1, 55, 14, gl_color_MENU_btn, 1);
	Clear(125, 118 - 1, 55, 14, gl_color_MENU_btn, 1);
	sprintf(msg, "%s", gl_menu_btn);
	DrawHZText12(msg, 0, 60, 118, gl_color_text, 1);
}
//---------------------------------------------------------------------------------
// Reads Recently Played into p_recently_play; returns how many rows it holds.
u32 Recently_play_load(void)
{
	u32 res;
	u32 count = 0;
	char buf[RECENT_LEN + 1];  // the longest row and its newline
	res = f_open(&gfile, "/SAVER/Recently play.txt", FA_READ);
	if (res == FR_OK)//have a play file
	{
		f_lseek(&gfile, 0x0);
		memset(buf, 0x00, sizeof(buf));
		while (Read_line(buf, sizeof(buf), &gfile) != NULL) {
			//DrawHZText12(buf, 32, 1+16, showy, name_color,1);
			Trim(buf);
			if (Recently_play_nor(buf)) {  // a game started from NOR
				if (strlen(Recently_play_nor(buf)) > sizeof(pNorFS[0].filename) - 1)
					continue;
			} else {
				if (buf[0] != '/')
					break;
				// A folder or name longer than the kernel writes (an edited
				// file) would overrun currentpath or current_filename
				char *slash = strrchr(buf, '/');
				if ((u32)(slash - buf) >= MAX_path_len || strlen(slash + 1) > GAME_NAME_MAX)
					continue;
			}
			memset(p_recently_play[count], 0x00, RECENT_LEN);
			memcpy(p_recently_play[count], buf, RECENT_LEN - 1);
			memset(buf, 0x00, sizeof(buf));
			count++;
			if (count == 10)
				break;
		}
	}
	f_close(&gfile);
	return count;
}
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
// Moves entry to the top of the count rows in p_recently_play, adding it if
// it is new and dropping the oldest row when all 10 are taken. Returns the
// new count.
u32 Recently_play_add(const char *entry, u32 count)
{
	u32 i;
	u32 found = count;
	for (i = 0; i < count; i++) {
		if (strcmp(entry, p_recently_play[i]) == 0) {
			found = i;
			break;
		}
	}
	if (found == count) {//new: take a free row, or the oldest one
		if (count < 10)
			count++;
		found = count - 1;
	}
	for (i = found; i > 0; i--) {
		memcpy(p_recently_play[i], p_recently_play[i - 1], RECENT_LEN);
	}
	memset(p_recently_play[0], 0x00, RECENT_LEN);
	strncpy(p_recently_play[0], entry, RECENT_LEN - 1);
	return count;
}
//---------------------------------------------------------------------------------
// Puts entry, a path or a NOR game's entry, on top of Recently Played.
static void Recently_play_save(const char *entry)
{
	u32 res;
	u32 i;
	u32 count = Recently_play_load();

	count = Recently_play_add(entry, count);

	res = f_open(&gfile, "/SAVER/Recently play.txt", FA_WRITE | FA_CREATE_ALWAYS);//truncate: the list can shrink
	if (res == FR_OK) {
		for (i = 0; i < count; i++) {
			res = f_printf(&gfile, "%s\n", p_recently_play[i]);
		}
		f_close(&gfile);
	}
}
//---------------------------------------------------------------------------------
static void Make_recently_play_file(TCHAR *path, TCHAR *gamefilename)
{
	char buf[RECENT_LEN];

	memset(buf, 0x00, sizeof(buf));
	snprintf(buf, sizeof(buf), strcmp(path, "/") == 0 ? "%s%s" : "%s/%s", path, gamefilename);
	Recently_play_save(buf);
}
//---------------------------------------------------------------------------------
// Torii: a game started from NOR is on Recently Played as NOR:NAME, NAME its
// list entry's file name, and starts from NOR there. Returns NAME, or NULL
// for an SD path.
const char *Recently_play_nor(const char *entry)
{
	return strncmp(entry, RECENT_NOR, sizeof(RECENT_NOR) - 1) == 0 ? entry + sizeof(RECENT_NOR) - 1 : NULL;
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

		// Torii: the reads' results are logged
		FRESULT res, res2 = FR_OK;
		UINT got;
		if (filesize > 64 * 1024) {
			res = f_read(&file, pReadCache, 64 * 1024, (UINT *)&ret);
			got = ret;
			WriteSram(SRAMSaver, pReadCache, 64 * 1024);
			SetRampage(0x10);
			left = filesize - 64 * 1024;
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
		if (res != FR_OK || res2 != FR_OK || got != filesize)
			tlog_error("save: loading %s into cart SRAM: f_read gave %d and %d, %u of %u bytes", filename,
				   res, res2, got, filesize);
		else
			tlog("save: loaded %s into cart SRAM, %u bytes", filename, filesize);
		return 1;
	}
	default:
		tlog_error("save: can't open %s to load it", filename);
		return false;
	}
}
//---------------------------------------------------------------------------------
// Torii: f_write for Save_savefile, adding up what went to the card and
// keeping the first error.
static void save_write(FIL *file, const u8 *data, UINT len, u32 *total, FRESULT *first_error)
{
	UINT written = 0;
	FRESULT res = f_write(file, data, len, &written);
	*total += written;
	if (res != FR_OK && *first_error == FR_OK)
		*first_error = res;
}

// Copies savesize bytes of cart SRAM to filename, over the file there, and
// logs what reached the card. ROM code, as Loadsavefile: both run only in
// the menu.
u32 Save_savefile(TCHAR *filename, u32 savesize)
{
	FIL file;
	if (savesize == 0)
		return 0xff;
	u32 ret = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
	switch (ret) {
	case FR_OK: {
		u32 i;
		u32 total = 0, expected;
		FRESULT werr = FR_OK;
		//memset(pReadCache,0xFF,0x200*4);
		SetRampage(0x0);

		if (savesize < 0x800) {
			ReadSram(SRAMSaver, pReadCache, savesize);
			expected = (savesize + 0x1FF) / 0x200 * 0x200;
			for (i = 0; i < (savesize + 0x1FF) / 0x200 && total == i * 0x200; i++)
				save_write(&file, pReadCache + 0x200 * i, 0x200, &total, &werr);
		} else {
			if (savesize > 64 * 1024) {
				expected = 128 * 1024;
				ReadSram(SRAMSaver, pReadCache, 64 * 1024);
				for (i = 0; i < 64 * 1024 / 0x800 && total == i * 0x800; i++)
					save_write(&file, pReadCache + 0x800 * i, 0x200 * 4, &total, &werr);
				SetRampage(0x10);
				ReadSram(SRAMSaver, pReadCache, 64 * 1024);
				for (i = 0; i < 64 * 1024 / 0x800 && total == 64 * 1024 + i * 0x800; i++)
					save_write(&file, pReadCache + 0x800 * i, 0x200 * 4, &total, &werr);
			} else {
				expected = savesize / 0x800 * 0x800;
				ReadSram(SRAMSaver, pReadCache, savesize);
				for (i = 0; i < savesize / 0x800 && total == i * 0x800; i++)
					save_write(&file, pReadCache + 0x800 * i, 0x200 * 4, &total, &werr);
			}
		}
		SetRampage(0x0);  // Torii: the page was left at 0x10

		FRESULT cerr = f_close(&file);
		if (werr != FR_OK || cerr != FR_OK || total != expected)
			tlog_error("save: writing %s: %lu of %lu bytes written, f_write gave %d, f_close %d", filename,
				   (unsigned long)total, (unsigned long)expected, werr, cerr);
		else
			tlog("save: wrote %s, %lu bytes, f_close gave %d", filename, (unsigned long)total, cerr);

		return 1;
	} break;
	default:
		tlog_error("save: can't create %s: f_open gave %lu", filename, (unsigned long)ret);
		return false;
	}
}
//---------------------------------------------------------------------------------
u32 IWRAM_CODE LoadRTSfile(TCHAR *filename)
{
	UINT ret;
	UINT filesize;
	FIL file;
	u32 page;

	switch (f_open(&file, filename, FA_READ)) {
	case FR_OK: {
		filesize = f_size(&file);
		if (filesize > 0x70000)
			filesize = 0x70000;

		for (page = 0x40; page < 0xB0; page += 0x10) {
			SetRampage(page);
			f_read(&file, pReadCache, 64 * 1024, (UINT *)&ret);
			WriteSram(SRAMSaver, pReadCache, 64 * 1024);
		}
		f_close(&file);
		SetRampage(0x0);
		return 1;
	}
	default:
		return false;
	}
}
//---------------------------------------------------------------------------------
static u32 SavefileWrite(TCHAR *filename, u32 savesize)
{
	FIL file;
	if (savesize == 0)
		return 0xff;
	u32 ret = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
	switch (ret) {
	case FR_OK: {
		int i;
		unsigned int written;
		memset(pReadCache, 0xFF, 0x200 * 4);

		u32 total = 0;
		if (savesize < 0x800) {
			for (i = 0; (u32)i < (savesize + 0x1FF) / 0x200; i++) {
				f_write(&file, pReadCache, 0x200, &written);
				total += written;
				if (written != 0x200)
					break;
			}
		} else {
			for (i = 0; (u32)i < (savesize + 0x1FF) / 0x800; i++) {
				f_write(&file, pReadCache, 0x200 * 4, &written);
				total += written;
				if (written != 0x200 * 4)
					break;
			}
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
		return false;
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
u32 IWRAM_CODE Loadfile2PSRAM(TCHAR *filename)
{
	UINT ret;
	u32 filesize;
	u32 res;
	u32 blocknum;
	char msg[20];

	u32 Address;
	vu16 page = 0;
	SetPSRampage(page);

	res = f_open(&gfile, filename, FA_READ);
	if (res == FR_OK) {
		filesize = f_size(&gfile);
		Clear(60, 160 - 15, 120, 15, gl_color_cheat_black, 1);
		DrawHZText12(gl_writing, 0, 70, 160 - 15, gl_color_text, 1);

		f_lseek(&gfile, 0x0000);
		for (blocknum = 0x0000; blocknum < filesize; blocknum += 0x20000) {
			sprintf(msg, "%luMb/%luMb", (blocknum) / 0x20000, filesize / 0x20000);
			Clear(70 + 54, 160 - 15, 110, 15, gl_color_cheat_black, 1);
			DrawHZText12(msg, 0, 70 + 54, 160 - 15, gl_color_text, 1);
			f_read(&gfile, pReadCache, 0x20000, &ret);//pReadCache max 0x20000 Byte

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
void CheckSwitch(void)
{
	gl_reset_on = Read_SET_info(assress_v_reset);
	gl_rts_on = Read_SET_info(assress_v_rts);
	gl_sleep_on = Read_SET_info(assress_v_sleep);
	gl_cheat_on = Read_SET_info(assress_v_cheat);
	if ((gl_reset_on != 0x0) && (gl_reset_on != 0x1)) {
		gl_reset_on = 0x0;
	}
	if ((gl_rts_on != 0x0) && (gl_rts_on != 0x1)) {
		gl_rts_on = 0x0;
	}
	if ((gl_sleep_on != 0x0) && (gl_sleep_on != 0x1)) {
		gl_sleep_on = 0x0;
	}
	if ((gl_cheat_on != 0x0) && (gl_cheat_on != 0x1)) {
		gl_cheat_on = 0x0;
	}

	gl_engine_sel = Read_SET_info(assress_engine_sel);
	if ((gl_engine_sel != 0x0) && (gl_engine_sel != 0x1)) {
		gl_engine_sel = 0x1;
	}

	gl_show_Thumbnail = Read_SET_info(assress_show_Thumbnail);
	if ((gl_show_Thumbnail != 0x0) && (gl_show_Thumbnail != 0x1)) {
		gl_show_Thumbnail = 0x0;
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

	gl_ModeB_init = Read_SET_info(assress_ModeB_INIT);
	if ((gl_ModeB_init != 0x0) && (gl_ModeB_init != 0x1) && (gl_ModeB_init != 0x2)) {
		gl_ModeB_init = 0x2;
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
//---------------------------------------------------------------------------------
void ShowTime(u32 page_num, u32 page_mode)
{
	u8 datetime[3];
	char msgtime[50];
	//get time
	rtc_enable();
	rtc_gettime(datetime);
	rtc_disenable();
	delay(5);

	if (page_mode == 0x1)
		ClearWithBG((u16 *)gImage_RECENTLY, 80, 3, 80, 13, 1);
	else if (page_num == SD_list)
		ClearWithBG((u16 *)gImage_SD, 120, 3, 50, 13, 1);
	else if (page_num == NOR_list)
		ClearWithBG((u16 *)gImage_NOR, 120, 3, 50, 13, 1);

	u8 HH = UNBCD(datetime[0] & 0x3F);
	u8 MM = UNBCD(datetime[1] & 0x7F);
	u8 SS = UNBCD(datetime[2] & 0x7F);
	if (HH > 23)
		HH = 0;
	if (MM > 59)
		MM = 0;
	if (SS > 59)
		SS = 0;

	sprintf(msgtime, "%02u:%02u:%02u", HH, MM, SS);
	DrawHZText12(msgtime, 0, 120, 3, gl_color_text, 1);
}
//---------------------------------------------------------------
u32 IWRAM_CODE LoadEMU2PSRAM(TCHAR *filename, u32 is_EMU)
{
	UINT ret;
	u32 filesize;
	u32 res;
	u32 blocknum;
	char msg[20];

	u32 Address;
	vu16 page = 0;
	SetPSRampage(page);

	u32 rom_start_address = 0;
	switch (is_EMU) {
	case 1://gbc
	case 2://gb
		dmaCopy((void *)jagoombacolor_gba, pReadCache, jagoombacolor_gba_size);
		dmaCopy((void *)pReadCache, PSRAMBase_S98, jagoombacolor_gba_size);
		rom_start_address = jagoombacolor_gba_size;
		break;
	case 3://nes
		dmaCopy((void *)pocketnes_gba, pReadCache, pocketnes_gba_size);
		dmaCopy((void *)pReadCache, PSRAMBase_S98, pocketnes_gba_size);
		rom_start_address = pocketnes_gba_size + 0x30;
		break;
	default:
		break;
	}

	res = f_open(&gfile, filename, FA_READ);
	if (res == FR_OK) {
		filesize = f_size(&gfile);

		if (is_EMU == 3) {//nes pocketnes_2013_07_01
			*(vu32 *)pReadCache = 0x45564153;
			*((vu32 *)pReadCache + 1) = 0x0;
			dmaCopy((void *)pReadCache, PSRAMBase_S98 + rom_start_address - 0x30, 0x8);
			*(vu32 *)pReadCache = filesize;
			dmaCopy((void *)pReadCache, PSRAMBase_S98 + rom_start_address - 0x10, 0x4);

			*(vu32 *)pReadCache = 0x709346c0;//usr rtc
			dmaCopy((void *)pReadCache, PSRAMBase_S98 + 0x1EA0, 0x4);

		} else {
			//use rtc, have been modify
		}

		Clear(60, 160 - 15, 120, 15, gl_color_cheat_black, 1);
		DrawHZText12(gl_writing, 0, 78, 160 - 15, gl_color_text, 1);

		f_lseek(&gfile, 0x0000);
		for (blocknum = 0x0000; blocknum < filesize; blocknum += 0x20000) {
			sprintf(msg, "%luMb", (blocknum) / 0x20000);
			Clear(78 + 54, 160 - 15, 110, 15, gl_color_cheat_black, 1);
			DrawHZText12(msg, 0, 78 + 54, 160 - 15, gl_color_text, 1);
			//f_lseek(&gfile, blocknum);
			f_read(&gfile, pReadCache, 0x20000, &ret);//pReadCache max 0x20000 Byte

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
		return 0;
	} else {
		return 1;
	}

	return 0;
}
//---------------------------------------------------------------------------------
extern u16 SET_info_buffer[0x200] EWRAM_BSS;
//---------------------------------------------------------------------------------
u32 Check_file_type(TCHAR *pfilename)
{
	u32 strlen8 = strlen(pfilename);
	//u32 is_EMU;
	if (strlen8 < 3) {  // too short for an extension; don't read before the name
		return 0xff;
	} else if (!strcasecmp(&(pfilename[strlen8 - 3]), "gba")) {
		return 0;
	} else if (!strcasecmp(&(pfilename[strlen8 - 3]), "gbc")) {
		return 1;
	} else if (!strcasecmp(&(pfilename[strlen8 - 2]), "gb")) {
		return 2;
	} else if (!strcasecmp(&(pfilename[strlen8 - 3]), "nes")) {
		return 3;
	} else {
		return 0xff;
	}
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

	ClearWithBG((u16 *)gImage_SD, 90, 2, 90, 13, 1);
	tlog_error("error %u: %s", error_num, msg);
	tlog_flush();
	DrawHZText12((char *)msg, 0, 90, 2, RGB(31, 00, 00), 1);
	wait_btn();
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
		memset(pReadCache, 0xFF, 0x800);
		while (total < need) {
			UINT chunk = need - total < 0x800 ? need - total : 0x800, written = 0;
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
static u8 Process_savefile(u32 is_EMU, TCHAR *pfilename, u32 gamefilesize, BYTE saveMODE)
{
	u32 res;
	u32 savefilesize = 0;
	TCHAR savfilename[SAVE_NAME_MAX];

	res = f_chdir(SAVER_FOLDER);
	if (res != FR_OK) {
		return 2;
	}

	save_name(savfilename, pfilename, is_EMU);
	// The pending-save record holds the name up to SAV_INFO_TYPE: a longer one
	// would be copied back under another name. Nothing is written yet.
	if (strlen(savfilename) > SAV_INFO_NAME_MAX) {
		tlog("save: %s is too long for the pending-save record", savfilename);
		return 8;
	}
	//#ifdef DEBUG
	//DEBUG_printf("sav %s",savfilename);
	//DEBUG_printf("saveMODE %x",saveMODE);
	//wait_btn();
	//#endif

	res = f_open(&gfile, savfilename, FA_OPEN_EXISTING);
	if (res == FR_OK)//have a old save file
	{
		savefilesize = f_size(&gfile);
		f_close(&gfile);
		tlog("save: %s exists, %lu bytes, save type %02X", savfilename, (unsigned long)savefilesize, saveMODE);
		u32 need = Get_savefilesize(saveMODE);
		if (!is_EMU && saveMODE != 0 && savefilesize < need)
			savefilesize = Grow_savefile(savfilename, savefilesize, need);
	} else //make a new one
	{
		ShowbootProgress(gl_make_sav);
		savefilesize = Get_savefilesize(saveMODE);
		res = SavefileWrite(savfilename, savefilesize);
		if (res == 0) {
			u8 error_num = 5;
			return error_num;
		}
	}

	if (savefilesize) {
		Bank_Switching(0);
		res = Loadsavefile(savfilename);

		/*Save_savefile("dump.bin",0x8000);	
		DEBUG_printf("dump finish");
		wait_btn();	*/
		memset(SAV_info_buffer, 0x00, sizeof(SAV_info_buffer));
		SAV_info_buffer[0] = 0x11;
		SAV_info_buffer[1] = savefilesize >> 9;
		//memcpy(&SAV_info_buffer[2],savfilename,100);
		strcpy((char *)&SAV_info_buffer[2], savfilename);
		SAV_info_buffer[SAV_INFO_TYPE] = saveMODE;
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
//---------------------------------------------------------------------------------
static void Check_save_flag(void)
{
	//check save
	u16 readd;
	u32 savefilesize;
	readd = Read_sav_info(0);
	savefilesize = Read_sav_info(1) << 9;
	if (readd != 0x11)
		tlog_debug("save: no save waiting to be copied");
	if (readd == 0x11) {
		//u32 res;
		register u32 loopwrite;
		DrawPic((u16 *)gImage_SD, 0, 0, 240, 160, 0, 0, 1);
		for (loopwrite = 0; loopwrite < 0x200 / 2; loopwrite++) {
			((u16 *)SAV_info_buffer)[loopwrite] = Read_sav_info(loopwrite + 2);
		}
		DrawPic((u16 *)gImage_MENU, 56, 25, 128, 110, 0, 0, 1);//show menu pic

		DrawHZText12(gl_save_sav, 0, 60, 28, gl_color_text, 1);//use sure?gl_LSTART_help
		DrawHZText12((TCHAR *)SAV_info_buffer, 20, 60, 40, 0x7fff, 1);//file name
		DrawHZText12((TCHAR *)SAV_info_buffer + 20, 20, 60, 52, 0x7fff, 1);//file name
		DrawHZText12((TCHAR *)SAV_info_buffer + 40, 20, 60, 64, 0x7fff, 1);//file name
			//DrawHZText12(gl_formatnor_info,5,60,90,gl_color_text,1);//use sure?

		// Torii: the answer is logged
		static const char saved_how[] = "save: \"Save sav?\" for %s/%s, %lu bytes, save type %02X: %s";
		u16 savetype = Read_sav_info(SAV_INFO_TYPE);
		if (gl_auto_save_sel) {
			tlog(saved_how, SAVER_FOLDER, (TCHAR *)SAV_info_buffer, (unsigned long)savefilesize, savetype,
			     "auto");
			DrawHZText12(gl_save_ing, 0, 60, 88, gl_color_text, 1);//use sure?gl_LSTART_help
			f_mkdir(SAVER_FOLDER);//"/SAVER"
			f_chdir(SAVER_FOLDER);
			Save_savefile((TCHAR *)SAV_info_buffer, savefilesize);
		} else {
			Show_MENU_btn();
			while (1) {
				VBlankIntrWait();
				scanKeys();
				u16 keysdown = keysDown();
				if (keysdown & KEY_A) {
					tlog(saved_how, SAVER_FOLDER, (TCHAR *)SAV_info_buffer,
					     (unsigned long)savefilesize, savetype, "A, save");
					DrawHZText12(gl_save_ing, 0, 60, 88, gl_color_text,
						     1);//use sure?gl_LSTART_help
					f_mkdir(SAVER_FOLDER);
					f_chdir(SAVER_FOLDER);
					Save_savefile((TCHAR *)SAV_info_buffer, savefilesize);
					/*TCHAR bmpfilename[100];	
						u32 bmpnum;
						for(bmpnum=0;bmpnum<1000;bmpnum++)
						{
							f_mkdir(SAVER_FOLDER);
							f_chdir(SAVER_FOLDER);
							sprintf(bmpfilename,"%4lu.sav",bmpnum);
							Save_savefile(bmpfilename,savefilesize);
						}*/
					break;
				} else if (keysdown & KEY_B) {
					tlog(saved_how, SAVER_FOLDER, (TCHAR *)SAV_info_buffer,
					     (unsigned long)savefilesize, savetype, "B, not saved");
					break;
				}
			}
		}
		memset(SAV_info_buffer, 0x00, sizeof(SAV_info_buffer));//clean flag
		Save_sav_info(SAV_info_buffer, 0x200);
	}
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
	Save_SET_info(SET_info_buffer, 0x200);
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

	SetMode(MODE_3 | BG2_ENABLE);

	SD_Disable();
	Set_RTC_status(1);

	//check FW
	Check_FW_update();
	REG_WAITCNT_CART = WAITCNT_MENU;
	tlog_debug("boot: WAITCNT %04X for the menu", REG_WAITCNT_CART);

	DrawPic((u16 *)gImage_splash, 0, 0, 240, 160, 0, 0, 1);
	CheckLanguage();
	CheckSwitch();

	res = f_mount(&EZcardFs, "", 1);
	if (res != FR_OK) {
		DrawHZText12(gl_init_error, 0, 2, 20, gl_color_cheat_black, 1);
		DrawHZText12(gl_power_off, 0, 2, 33, gl_color_cheat_black, 1);
		while (1)
			;
	} else {
		DrawHZText12(gl_init_ok, 0, 2, 20, gl_color_cheat_black, 1);
		DrawHZText12(gl_Loading, 0, 2, 33, gl_color_cheat_black, 1);
	}
	VBlankIntrWait();

	// Diagnostics also starts from these keys held at power-on, in case the
	// menu itself is broken. Read once the card is up, a second into the boot.
	u32 diag_keys = (~REG_KEYINPUT & 0x3FF) == DIAG_BOOT_KEYS;
	tlog_debug("boot: keys held %03X", ~REG_KEYINPUT & 0x3FF);
	Check_save_flag();
	tlog_sd_ready();
	cartlog_boot_report();
	tlog_flush();
	if (diag_keys)
		Diagnostics();

	f_chdir("/");
	//TCHAR currentpath[MAX_path_len];
	memset(currentpath, 00, MAX_path_len);
	memset(currentpath_temp, 0x00, MAX_path_len);

	res = f_getcwd(currentpath, sizeof currentpath / sizeof *currentpath);

	Read_NOR_info();
	gl_norOffset = 0x000000;
	// Torii: reading the list writes nothing. The official kernel erased the
	// list whenever it read no games (a chip left in a command mode, a read
	// glitch), and the next write then went over them.
	game_total_NOR = GetFileListFromNor();//initialize to prevent direct writes to NOR without page turning
	Migrate_NOR_list(game_total_NOR);

	cartlog_phase(PHASE_MENU, 0, NULL);
	lists_run();  // the menu: never returns
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
static void Boot_NOR_game(u32 show_offset, u32 file_select, u32 key_L)
{
	cover_cache_invalidate();  // before the save files are touched, as boot_game does
	//TCHAR savfilename[100];
	TCHAR *pfilename;
	u32 gamefilesize = 0;
	BYTE SAVEMODE;
	BYTE error_num;
	u32 res;

	Clear(0, 0, 240, 160, gl_color_cheat_black, 1);
	DrawHZText12(gl_Loading, 0, (240 - strlen(gl_Loading) * 6) / 2, 74, gl_color_text, 1);

	init_FAT_table();

	res = f_mkdir(SAVER_FOLDER);  //"/SAVER"
	if ((res != FR_OK) && (res != FR_EXIST)) {
		error_num = 2;
		Show_error_num(error_num);
		return;
	}

	//boot nor game
	pfilename = pNorFS[show_offset + file_select].filename;
	gamefilesize = pNorFS[show_offset + file_select].filesize;
	SAVEMODE = pNorFS[show_offset + file_select].savemode;
	memcpy(GAMECODE, &pNorFS[show_offset + file_select].gamename[0xC], 4);  //header 0xA0: title, then game code
	cartlog_phase(PHASE_LAUNCH, 0, pfilename);
	tlog("launch: loading %s from NOR at %08lX: code %.4s, %lu bytes, save type %02X, patched %u, savestates %u",
	     pfilename, (unsigned long)pNorFS[show_offset + file_select].rompage << 17, (const char *)GAMECODE,
	     (unsigned long)gamefilesize, SAVEMODE, pNorFS[show_offset + file_select].have_patch,
	     pNorFS[show_offset + file_select].have_RTS);
	tlog_flush();

	ShowbootProgress(gl_check_sav);
	//memcpy(savfilename,pfilename,100);
	error_num = Process_savefile(0, pfilename, gamefilesize, SAVEMODE);
	if (error_num != 0) {
		Show_error_num(error_num);
		return;
	}
	{
		char entry[sizeof(RECENT_NOR) + sizeof(pNorFS[0].filename)];
		snprintf(entry, sizeof(entry), RECENT_NOR "%.99s", pfilename);
		Recently_play_save(entry);
	}

	Set_64MROM_flag(pNorFS[show_offset + file_select].is_64MBrom);
	if (pNorFS[show_offset + file_select].have_patch && pNorFS[show_offset + file_select].have_RTS) {
		ShowbootProgress(gl_check_RTS);
		u32 size = Check_RTS(pfilename);
		if (size == 0) {
			error_num = 6;
			Show_error_num(error_num);
			return;
		}
	}

	FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
	Send_FATbuffer(FAT_table_buffer, 1);  //only RTS FAT and some parameter
	//wait_btn();
	gl_game_uses = Game_hardware(0, GAMECODE, 0);
	Launch_now(pNorFS[show_offset + file_select].rompage, key_L, "NOR game");
	while (1)
		;
}
//---------------------------------------------------------------

// A game picked on the SD list or Recently Played (ezkernel.h).
u32 Game_pick(struct game_pick *g)
{
	g->is_emu = Check_file_type(g->name);
	g->havecht = 0;
	g->save_num = g->old_save_num = 0;
	g->menu_max = 0;
	if (g->is_emu == 0xff)  //can not boot
		return 0;
	if (g->is_emu) {  //GB, GBC and NES games only have CLEAN BOOT: start them without the menu
		g->save_num = 0xF;
		return 1;
	}
	f_chdir(currentpath);  //can open  re list game
	g->havecht = Check_cheat_file(g->name);
	g->old_save_num = Check_mde_file(g->name);
	g->save_num = g->old_save_num;
	g->menu_max = 4 + ((gl_cheat_on == 1) ? ((g->havecht > 0) ? 1 : 0) : 0);
	return 1;
}
//---------------------------------------------------------------------------------
// The launch itself, from the game menu's line MENU_line (0 CLEAN BOOT, 1
// BOOT WITH ADDON, 2 WRITE TO NOR CLEAN, 3 WRITE TO NOR ADDON), in mode 3.
static u8 Game_boot(TCHAR *pfilename, u32 is_EMU, u32 MENU_line, u8 Save_num, u8 old_Save_num, u32 key_L)
{
	u32 res;
	u8 error_num;
	BYTE SAVEMODE;
	// The launch fills PSRAM with the game: the cover cache there is gone
	cover_cache_invalidate();
	Clear(0, 0, 240, 160, gl_color_cheat_black, 1);
	DrawHZText12(gl_Loading, 0, (240 - strlen(gl_Loading) * 6) / 2, 74, gl_color_text, 1);

	u32 gamefilesize = 0;
	//u32 savefilesize=0;
	u32 ret;
	u32 have_pat = 0;

	init_FAT_table();

	//Load to PSRAM or NOR

	f_chdir(currentpath);  //return to game folder
	res = f_open(&gfile, pfilename, FA_READ);
	if (res == FR_OK) {
		u8 gb_cart_type = 0;
		u8 header[0x20];
		f_lseek(&gfile, 0xA0);
		if (f_read(&gfile, header, sizeof(header), (UINT *)&ret) != FR_OK || ret != sizeof(header))
			memset(header, 0, sizeof(header));
		memcpy(GAMECODE, header + 0xC, 4);
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
		// A Recently Played entry for a file that has since moved: launching
		// would make its save and start an emulator with no ROM
		tlog("launch %s: can't open it", pfilename);
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
			ShowbootProgress(gl_file_overflow);
			wait_btn();
			return 0;
		}

		ShowbootProgress(gl_check_sav);
		res = f_mkdir(SAVER_FOLDER);  //"/SAVER"
		if ((res != FR_OK) && (res != FR_EXIST)) {
			error_num = 2;
			Show_error_num(error_num);
			return 0;
		}

		error_num = Process_savefile(is_EMU, pfilename, gamefilesize, SAVEMODE);
		if (error_num != 0) {
			Show_error_num(error_num);
			return 0;
		}
		Make_recently_play_file(currentpath, pfilename);  //make txt in /SAVER
	}

	if (is_EMU)  //boot emu game
	{
		ShowbootProgress(gl_loading_game);
		f_chdir(currentpath);  //return to game folder
		cartlog_phase(PHASE_LAUNCH, 0, pfilename);
		tlog("launch: loading %s with %s", pfilename, (is_EMU == 3) ? "PocketNES" : "Jagoomba Color");
		tlog_flush();

		FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
		Send_FATbuffer(FAT_table_buffer, 1);

		res = LoadEMU2PSRAM(pfilename, is_EMU);
		Launch_now(0x200, key_L, (is_EMU == 3) ? "plugin PocketNES" : "plugin Jagoomba Color");
		while (1)
			;
	} else {  //gba file
		if (old_Save_num != Save_num) {
			error_num = Make_mde_file(pfilename, Save_num);
			if (error_num == 2) {
				Show_error_num(error_num);
				return 0;
			}
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
			ShowbootProgress(gl_loading_game);
			cartlog_phase(PHASE_LAUNCH, 0, pfilename);
			tlog("launch: loading %s, CLEAN BOOT", pfilename);
			tlog_flush();

			Send_FATbuffer(FAT_table_buffer, 0);
			GBApatch_Cleanrom(PSRAMBase_S98, gamefilesize);
			//wait_btn();
			Launch_now(0x200, key_L, "CLEAN BOOT");
			break;
		case 1:  //PSRAM BOOT WITH ADDON
			ShowbootProgress(gl_loading_game);
			gl_reset_on = Read_SET_info(assress_v_reset);
			gl_rts_on = Read_SET_info(assress_v_rts);
			gl_sleep_on = Read_SET_info(assress_v_sleep);
			gl_cheat_on = Read_SET_info(assress_v_cheat);
			cartlog_phase(PHASE_LAUNCH, 0, pfilename);
			tlog("launch: loading %s, BOOT WITH ADDON: reset %u, savestate %u, sleep %u, cheats %u (%lu)",
			     pfilename, gl_reset_on, gl_rts_on, gl_sleep_on, gl_cheat_on,
			     (unsigned long)gl_cheat_count);
			tlog_flush();
			if ((gl_reset_on == 1) || (gl_rts_on == 1) || (gl_sleep_on == 1) || (gl_cheat_on == 1)) {
				if (gl_rts_on == 1) {
					ShowbootProgress(gl_check_RTS);
					u32 size = Check_RTS(pfilename);
					if (size == 0) {
						error_num = 6;
						Show_error_num(error_num);
						return 0;
					}
				}
				ShowbootProgress(gl_check_pat);
				have_pat = Check_pat(pfilename);
				if (have_pat == 1) {
					tlog("launch: IRQ hook from PATCH/ (cached scan)");
					Send_FATbuffer(FAT_table_buffer, 0);  //Loading rom
				} else  //(have_pat==0)
				{
					f_chdir(currentpath);  //return to game folder
					//get the location for the patch
					res = f_open(&gfile, pfilename, FA_READ);
					f_lseek(&gfile, (gamefilesize - 1) & 0xFFFE0000);
					f_read(&gfile, pReadCache, 0x20000, (UINT *)&ret);
					f_close(&gfile);
					SetTrimSize(pReadCache, gamefilesize, 0x20000, 0x0, SAVEMODE);
					if (gl_engine_sel == 0) {
						tlog("launch: IRQ hook from a scan of the ROM (scan engine)");
					get_find:
						FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
						Send_FATbuffer(FAT_table_buffer, 1);
						res = Loadfile2PSRAM(pfilename);
						ShowbootProgress(gl_make_pat);
						Make_pat_file(pfilename);
					} else {
						res = use_internal_engine(GAMECODE, pfilename);
						if (res == 1) {
							tlog("launch: IRQ hook from the built-in table");
							Send_FATbuffer(FAT_table_buffer, 0);  //Loading rom
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
				Apply_rom_patches();  //Torii: exact-ROM patches, SD launches only
				if (iCount2 > found)
					tlog("launch: exact-ROM patch for %.4s (irqpatch.c rom_patches)",
					     (char *)GAMECODE);
				GBApatch_PSRAM(PSRAMBase_S98, gamefilesize);
				Log_patches();
			} else {  //no select switch ,CLEAN
				tlog("launch: no addon on, so no patches");
				Send_FATbuffer(FAT_table_buffer, 0);  //Loading rom
			}
			//wait_btn();
			Launch_now(0x200, key_L, "BOOT WITH ADDON");
			break;
		case 2:  //WRITE TO NOR CLEAN
			f_chdir(currentpath);  //return to game folder
			res = Loadfile2NOR(pfilename, gl_norOffset, 0x0, SAVEMODE);
			if (res == 0)  //ok
			{
				if (gl_norOffset == 0)  //first game need set saveMODE
				{
					Set_saveMODE(SAVEMODE);
				}
				return 2;
			} else if (res == 2) {
				Clear(0, 160 - 15, 200, 15, gl_color_cheat_black, 1);
				DrawHZText12(gl_NOR_full, 0, 0, 160 - 15, gl_color_NORFULL, 1);  //"NOR FULL!"
				wait_btn();
				return 1;
			} else {  // Torii: the file or the NOR failed; the game isn't listed
				Show_error_num(1);
				return 1;
			}
			break;
		case 3:  //WRITE TO NOR ADDON
			gl_reset_on = Read_SET_info(assress_v_reset);
			gl_rts_on = Read_SET_info(assress_v_rts);
			gl_sleep_on = Read_SET_info(assress_v_sleep);
			gl_cheat_on = Read_SET_info(assress_v_cheat);

			f_chdir(currentpath);  //return to game folder
			u32 needpatch = 0;
			if ((gl_reset_on == 1) || (gl_rts_on == 1) || (gl_sleep_on == 1) || (gl_cheat_on == 1)) {
				Patch_SpecialROM_sleepmode();  //

				//get the location of the patch
				res = f_open(&gfile, pfilename, FA_READ);
				if (res == FR_OK) {
					f_lseek(&gfile, (gamefilesize - 1) & 0xFFFE0000);
					f_read(&gfile, pReadCache, 0x20000, (UINT *)&ret);
					f_close(&gfile);
					SetTrimSize(pReadCache, gamefilesize, 0x20000, 0x1, SAVEMODE);
				}
				needpatch = 1;
			}
			res = Loadfile2NOR(pfilename, gl_norOffset, needpatch, SAVEMODE);
			//wait_btn();
			if (res == 0) {
				if (gl_norOffset == 0)  //first game need set saveMODE
				{
					Set_saveMODE(SAVEMODE);
				}
				return 2;
			} else if (res == 2) {
				Clear(0, 160 - 15, 200, 15, gl_color_cheat_black, 1);
				DrawHZText12(gl_NOR_full, 0, 0, 160 - 15, gl_color_NORFULL, 1);  //"NOR FULL!"
				wait_btn();
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
}//---------------------------------------------------------------------------------
u8 Game_launch(struct game_pick *g, u32 menu_line, u32 key_L)
{
	// A launch loads and patches the game in PSRAM, writes never measured
	// faster than the BIOS's wait states (waitcnt.h)
	REG_WAITCNT_CART = WAITCNT_BIOS;
	u8 res = Game_boot(g->name, g->is_emu, menu_line, g->save_num, g->old_save_num, key_L);
	REG_WAITCNT_CART = WAITCNT_MENU;
	cartlog_phase(PHASE_MENU, 0, NULL);
	return res;
}
//---------------------------------------------------------------------------------
void NOR_launch(u32 index, u32 key_L)
{
	REG_WAITCNT_CART = WAITCNT_BIOS;  // as Game_launch
	Boot_NOR_game(0, index, key_L);  // returns only if it failed
	REG_WAITCNT_CART = WAITCNT_MENU;
	cartlog_phase(PHASE_MENU, 0, NULL);
}
//---------------------------------------------------------------
// The first NOR game's save (ezkernel.h): OP 0 looks for its file (0x10 if
// there, 0 if not), 1 loads it into the cart's SRAM (0, or 3 if it isn't
// there), 2 writes the SRAM to it (0). 2: the SAVER folder failed.
u8 FRAM_save_op(u8 OP)
{
	u32 res;
	TCHAR savfilename[SAVE_NAME_MAX];

	res = f_mkdir(SAVER_FOLDER);  //"/SAVER"
	if ((res != FR_OK) && (res != FR_EXIST)) {
		return 2;
	}
	res = f_chdir(SAVER_FOLDER);
	if (res != FR_OK) {
		return 2;
	}
	save_name(savfilename, pNorFS[0].filename, 0);
	res = f_open(&gfile, savfilename, FA_OPEN_EXISTING);
	if (res == FR_OK)
		f_close(&gfile);
	if (OP == 0)
		return res == FR_OK ? 0x10 : 0;
	if (OP == 1) {  //load to fram
		if (res != FR_OK)
			return 3;
		Bank_Switching(0);
		Loadsavefile(savfilename);
		return 0;
	}
	//bak fram save
	Save_savefile(savfilename, Get_savefilesize(pNorFS[0].savemode));
	return 0;
}
//---------------------------------------------------------------------------------
// The SETTINGS and HELP pages, in mode 3, from R on the NOR list: R goes on to
// the next page, L back; L on the first page returns to the NOR list.
void Settings_pages(void)
{
	PAGE_NUM page_num = SET_win;
	u32 res;
	for (;;) {
		if (page_num == SET_win) {
			DrawPic((u16 *)gImage_SET, 0, 0, 240, 160, 0, 0, 1);
			res = Setting_window();
			tlog_flush();
			if (res == 0)
				return;  // the NOR list
			page_num = SET2_win;
		} else if (page_num == SET2_win) {
			DrawPic((u16 *)gImage_SET2, 0, 0, 240, 160, 0, 0, 1);
			res = Setting_window2();
			tlog_flush();
			if (res == 2)  //Torii: DIAGNOSE, which restarts the kernel
				Diagnostics();
			else if (res == 0)
				page_num = SET_win;
			else
				page_num = HELP;
		} else {
			DrawPic((u16 *)gImage_HELP, 0, 0, 240, 160, 0, 0, 1);
			Show_help_window();
			page_num = SET2_win;
		}
	}
}
