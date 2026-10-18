#include <gba_interrupt.h>
#include <gba_input.h>
#include <stdio.h>
#include <stdlib.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>
#include <gba_systemcalls.h>

#include "ez_define.h"
#include "NORflash_OP.h"
#include "ezkernel.h"
#include "draw.h"
#include "lang.h"
#include "Ezcard_OP.h"
#include "GBApatch.h"
#include "cheat.h"
#include "norlist.h"
#include "tlog.h"
#include "waitcnt.h"
//#define DEBUG

extern FM_NOR_FS pNorFS[MAX_NOR] EWRAM_BSS;
extern u8 pReadCache[MAX_pReadCache_size] EWRAM_BSS;
extern u32 gl_currentpage;
extern u32 gl_norOffset;
extern FIL gfile;
extern u32 game_total_NOR;
extern u32 iTrimSize;
//---------------------------------------------------------------
void Chip_Reset(void)
{
	*((vu16 *)(FlashBase_S98)) = 0xF0;
}
//---------------------------------------------------------------
void IWRAM_CODE Block_Erase(u32 blockAdd) //0x20000 BYTE pre block
{
	vu16 page, v1, v2;
	u32 Address;
	u32 loop;
	u16 waitcnt = WAITCNT_SLOW();

	page = gl_currentpage;
	Address = blockAdd;
	while (Address >= 0x800000) {
		Address -= 0x800000;
		page += 0x1000;
	}
	//u8 ramdata = *((vu8 *)(0xE000000+1)) ;
	SetRompage(page);
	//u16 norid = Read_S98NOR_ID();
	Chip_Reset();
	v1 = 0;
	v2 = 1;
	if ((blockAdd == 0) || (blockAdd == 0x3FE0000)) {
		//Address=blockAdd;
		for (loop = 0; loop < 0x20000; loop += 0x8000) {
			*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
			*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
			*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0x80;
			*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
			*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
			*((vu16 *)(FlashBase_S98 + Address + loop)) = 0x30;
			do {
				v1 = *((vu16 *)(FlashBase_S98 + Address + loop));
				v2 = *((vu16 *)(FlashBase_S98 + Address + loop));
			} while ((v1 != v2) || ((v1 & 0x80) != 0x80));
		}
	} else {
		loop = Address;

		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
		*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0x80;
		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
		*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
		*((vu16 *)(FlashBase_S98 + loop)) = 0x30;
		do {
			v1 = *((vu16 *)(FlashBase_S98 + loop));
			v2 = *((vu16 *)(FlashBase_S98 + loop));
		} while ((v1 != v2) || ((v1 & 0x80) != 0x80));
	}
	SetRompage(gl_currentpage);
	WAITCNT_RESTORE(waitcnt);
}
//-----------------------------------------------------------
void Chip_Erase(void)
{
	char msg[128];
	u32 count = 0;
	vu16 v1, v2 = 0;
	u16 waitcnt = WAITCNT_SLOW();
	tlog("nor: erasing the whole game NOR");
	REG_IME = 0;
	*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
	*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
	*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0x80;
	*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
	*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
	*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0x10;
	do {
		VBlankIntrWait();
		VBlankIntrWait();
		ShowTime(NOR_list, 0);
		DrawPic((u16 *)(gImage_MENU + 78 * 128 * 2), 56, 90 + 13, 128, 13, 0, 0, 1);//show menu pic

		itoa(count, msg, 2);
		DrawHZText12(msg, 0, 60, 90 + 13, gl_color_text, 1);
		count++;

		VBlankIntrWait();
		v1 = *((vu16 *)(FlashBase_S98));
		v2 = *((vu16 *)(FlashBase_S98));
	} while (v1 != v2);
	REG_IME = 1;
	WAITCNT_RESTORE(waitcnt);
	tlog("nor: erased the game NOR");
}

//-----------------------------------------------------------
void FormatNor(void)
{
	char msg[128];
	sprintf(msg, "%s", gl_formatnor_info);

	DrawHZText12(msg, 0, 60, 90 + 13, gl_color_text, 1);

	while (1) {
		delay(500);
		scanKeys();
		u16 keys = keysDown();
		if (keys & KEY_A) {
			Chip_Erase();
			memset(pNorFS, 00, sizeof(FM_NOR_FS) * MAX_NOR);
			game_total_NOR = 0;
			Save_NOR_list(0);  // Torii: the boot no longer saves an empty list
			return;
		} else if (keys & KEY_B) {
			return;
		}
	}
}
//---------------------------------------------------------------
void WriteFlash(u32 address, u8 *buffer, u32 size)
{
	vu16 page, v1, v2;
	register u32 loopwrite;
	vu16 *buf = (vu16 *)buffer;
	u16 waitcnt = WAITCNT_SLOW();

	page = gl_currentpage;
	while (address >= 0x800000) {
		address -= 0x800000;
		page += 0x1000;
	}
	SetRompage(page);

	Chip_Reset();
	v1 = 0;
	v2 = 1;
	for (loopwrite = 0; loopwrite < (size / 2); loopwrite++) {
		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
		*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xA0;
		*((vu16 *)(FlashBase_S98 + address + loopwrite * 2)) = buf[loopwrite];
		do {
			v1 = *((vu16 *)(FlashBase_S98 + address + loopwrite * 2));
			v2 = *((vu16 *)(FlashBase_S98 + address + loopwrite * 2));
		} while (v1 != v2);
	}
	SetRompage(gl_currentpage);
	WAITCNT_RESTORE(waitcnt);
}
//---------------------------------------------------------------
void IWRAM_CODE WriteFlash_with32word(u32 address, u8 *buffer, u32 size)
{
	vu16 page, v1, v2;
	register u32 loopwrite;
	vu16 *buf = (vu16 *)buffer;
	u32 i;
	u16 waitcnt = WAITCNT_SLOW();

	page = gl_currentpage;
	while (address >= 0x800000) {
		address -= 0x800000;
		page += 0x1000;
	}
	SetRompage(page);

	Chip_Reset();
	v1 = 0;
	v2 = 1;
	for (loopwrite = 0; loopwrite < (size / 32); loopwrite++) {
		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
		*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
		*((vu16 *)(FlashBase_S98 + address + loopwrite * 32)) = 0x25;
		*((vu16 *)(FlashBase_S98 + address + loopwrite * 32)) = 15;
		delay(10);
		for (i = 0; i <= 15; i++) {
			*((vu16 *)(FlashBase_S98 + address + loopwrite * 32 + 2 * i)) = buf[loopwrite * 16 + i];
		}
		*((vu16 *)(FlashBase_S98 + address + loopwrite * 32)) = 0x29;
		delay(40);
		do {
			v1 = *((vu16 *)(FlashBase_S98 + address + loopwrite * 32 + 0xF * 2));
			v2 = *((vu16 *)(FlashBase_S98 + address + loopwrite * 32 + 0xF * 2));
		} while (v1 != v2);
	}
	SetRompage(gl_currentpage);
	WAITCNT_RESTORE(waitcnt);
}
//-----------------------------------------------------------
// Torii: the list and completion marks (norlist.h).
_Static_assert(sizeof(FM_NOR_FS) == NOR_ENTRY_SIZE && MAX_NOR == NOR_LIST_ENTRIES, "norlist.h's sizes");
#define NOR_GAME_SIZE 0x4000000
#define NOR_BLOCK 0x20000
// The most a patch adds at iTrimSize (Patch_length in GBApatch.c)
#define NOR_PATCH_MAX CHEAT_PATCH_LENGTH
_Static_assert(CHEAT_PATCH_LENGTH >= RTS_PATCH_LENGTH, "NOR_PATCH_MAX");

static u32 nor_list_stored;  // games on the copy Read_NOR_info read
static u32 nor_list_from = NOR_LIST_A;  // the copy it read
static u32 nor_list_legacy;  // it read an older kernel's list, with no trailer

// Copies len bytes at offset in the game NOR, which shows 8 MB at a time.
// len stays within one 8 MB page.
static void nor_read(u32 offset, void *dst, u32 len)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	SetRompage(gl_currentpage + (offset >> 23) * 0x1000);
	memcpy(dst, (const void *)(FlashBase_S98 + (offset & 0x7FFFFF)), len);
	SetRompage(gl_currentpage);
	REG_IME = ime;
}

// Whether the game NOR reads 0xFF for len bytes (a multiple of 32) at offset
static u32 nor_erased(u32 offset, u32 len)
{
	u32 words[8];
	for (; len; offset += sizeof(words), len -= sizeof(words)) {
		nor_read(offset, words, sizeof(words));
		for (u32 i = 0; i < 8; i++)
			if (words[i] != 0xFFFFFFFF)
				return 0;
	}
	return 1;
}

// Whether the game at offset, with header, has a whole completion mark at the
// end of an area ending at end. Leaves the mark read in mark.
static u32 nor_mark_valid(u32 offset, u32 end, const u8 *header, struct nor_mark *mark)
{
	if (end <= offset || end > NOR_GAME_SIZE)
		return 0;
	nor_read(end - NOR_MARK_SPACE, mark, 2 * sizeof(u32));  // the magic and offset, to skip the rest fast
	if (mark->magic != NOR_MARK_MAGIC || mark->offset != offset)
		return 0;
	nor_read(end - NOR_MARK_SPACE, mark, sizeof(*mark));
	return nor_mark_check(mark, offset, end, header);
}

// Whether list entry e is the game at offset: its title and code are in the
// header there, and a game Torii wrote has its completion mark.
static u32 nor_entry_valid(const FM_NOR_FS *e, u32 offset, const u8 *header)
{
	struct nor_mark mark;
	if (!e->filesize || (e->filesize & (NOR_BLOCK - 1)) || e->filesize > NOR_GAME_SIZE - offset ||
	    e->rompage != offset >> 17 || memcmp(header, e->gamename, sizeof(e->gamename)) != 0)
		return 0;
	if (e->reserved1 != NOR_ENTRY_MARKED)  // an older kernel's, which wrote no marks
		return 1;
	return nor_mark_valid(offset, offset + e->filesize, header, &mark) && memcmp(mark.entry, e, sizeof(*e)) == 0;
}

// The end of the area of the game at offset, from its completion mark, or 0
static u32 nor_find_mark(u32 offset, const u8 *header, struct nor_mark *mark)
{
	for (u32 end = offset + NOR_BLOCK; end <= NOR_GAME_SIZE; end += NOR_BLOCK)
		if (nor_mark_valid(offset, end, header, mark))
			return end;
	return 0;
}

// Erases every completion mark, whole or not, that claims offset, so none
// outlives its game: a game written there later and cut off would be listed.
static void nor_clear_marks(u32 offset)
{
	struct nor_mark mark;
	for (u32 end = offset + NOR_BLOCK; end <= NOR_GAME_SIZE; end += NOR_BLOCK) {
		nor_read(end - NOR_MARK_SPACE, &mark, 2 * sizeof(u32));
		if (mark.magic == NOR_MARK_MAGIC && mark.offset == offset) {
			tlog("nor: erasing an old completion mark at %07lX", (unsigned long)(end - NOR_MARK_SPACE));
			Block_Erase(end - NOR_BLOCK);
		}
	}
}

// Programs the completion mark of the game at offset whose area ends at end,
// after all its data. 1 when it reads back whole.
static u32 nor_mark_write(u32 offset, u32 end, const FM_NOR_FS *entry)
{
	struct nor_mark mark;
	u8 header[0x20];
	nor_read(offset + 0xA0, header, sizeof(header));
	nor_mark_make(&mark, offset, end - offset, header, entry);
	WriteFlash_with32word(end - NOR_MARK_SPACE, (u8 *)&mark, sizeof(mark));
	return nor_mark_valid(offset, end, header, &mark);
}

// How many of pNorFS's games, from the first, are in the game NOR; sets
// *end to where they end.
static u32 nor_walk(u32 *end)
{
	u8 header[0x20];
	u32 offset = 0, count;
	Chip_Reset();  // in a command mode the chip reads no games
	for (count = 0; count < MAX_NOR && offset < NOR_GAME_SIZE; count++) {
		nor_read(offset + 0xA0, header, sizeof(header));
		if (!nor_entry_valid(&pNorFS[count], offset, header))
			break;
		offset += pNorFS[count].filesize;
	}
	*end = offset;
	return count;
}

// Loads the trailerless list in copy A, an older kernel's, into pNorFS.
// Its entries were never cleared, so a mark flag in them means nothing.
static void nor_load_legacy(const u8 *a)
{
	memcpy(pNorFS, a, NOR_LIST_BYTES);
	for (nor_list_stored = 0; nor_list_stored < MAX_NOR && pNorFS[nor_list_stored].filesize &&
				  pNorFS[nor_list_stored].filesize != 0xFFFFFFFF;
	     nor_list_stored++)
		;
	for (u32 i = 0; i < MAX_NOR; i++)
		pNorFS[i].reserved1 = 0;
}

// Logs which list Read_NOR_info read, when it differs from the last time: the
// menu reads it on each visit.
static void nor_log_read(const char *from)
{
	static const char *last_from;
	static u32 last_count = 0xFFFFFFFF;
	if (from == last_from && nor_list_stored == last_count)
		return;
	last_from = from;
	last_count = nor_list_stored;
	tlog_debug("nor: list read from %s, %lu games", from, (unsigned long)nor_list_stored);
}

void Read_NOR_info(void)
{
	const u8 *a = (const u8 *)FlashBase_S71 + NOR_LIST_A;
	const u8 *b = (const u8 *)FlashBase_S71 + NOR_LIST_B;
	u32 copy = nor_list_pick(a, b), seq, end;
	nor_list_legacy = 0;
	if (copy == NOR_COPY_B && !nor_list_check(a, &seq)) {
		// A without its trailer: a Torii save cut off there, or an older
		// kernel saved its list after Torii (a downgrade and back). Take
		// the one more of whose games are in NOR; a cut-off A has no more.
		nor_load_legacy(a);
		u32 games_a = nor_walk(&end);
		memcpy(pNorFS, b, NOR_LIST_BYTES);
		if (games_a > nor_walk(&end)) {
			nor_load_legacy(a);
			nor_list_from = NOR_LIST_A;
			nor_list_legacy = 1;
			nor_log_read("copy A, an older kernel's with more games than copy B");
			return;
		}
	}
	if (copy == NOR_COPY_NONE) {  // an older kernel's list, or none
		nor_load_legacy(a);
		nor_list_from = NOR_LIST_A;
		nor_list_legacy = 1;
	} else {
		memcpy(pNorFS, copy == NOR_COPY_B ? b : a, NOR_LIST_BYTES);
		nor_list_from = copy == NOR_COPY_B ? NOR_LIST_B : NOR_LIST_A;
		nor_list_stored =
			((const struct nor_list_trailer *)((copy == NOR_COPY_B ? b : a) + NOR_LIST_BYTES))->count;
	}
	nor_log_read(copy == NOR_COPY_A ? "copy A" : copy == NOR_COPY_B ? "copy B" : "an older kernel's copy A");
}

void Migrate_NOR_list(u32 count)
{
	if (!nor_list_legacy)
		return;
	// Only a list the game NOR bears out in full: an older kernel's list
	// that a misread made look shorter must not be saved short.
	if (!count || count != nor_list_stored)
		return;
	tlog("nor: saving an older kernel's list of %lu games in Torii's format", (unsigned long)count);
	if (Save_NOR_list(count) == 0)
		nor_list_legacy = 0;
}

u32 Save_NOR_list(u32 count)
{
	const u8 *a = (const u8 *)FlashBase_S71 + NOR_LIST_A;
	const u8 *b = (const u8 *)FlashBase_S71 + NOR_LIST_B;
	u32 seq_a = 0, seq_b = 0, seq;
	u32 has_a = nor_list_check(a, &seq_a), has_b = nor_list_check(b, &seq_b);
	if (has_a && has_b)
		seq = ((s32)(seq_b - seq_a) > 0 ? seq_b : seq_a) + 1;
	else
		seq = (has_a ? seq_a : has_b ? seq_b : 0) + 1;
	// Write first the copy the list wasn't read from: an older kernel's list
	// is read from A, so B goes first
	u32 first = nor_list_from == NOR_LIST_B ? NOR_LIST_A : NOR_LIST_B;
	u32 second = first == NOR_LIST_A ? NOR_LIST_B : NOR_LIST_A;

	u8 *record = pReadCache;
	memset(record, 0, NOR_LIST_RECORD);
	memcpy(record, pNorFS, count * sizeof(FM_NOR_FS));
	nor_list_seal(record, seq, count);
	const u32 copies[2] = {first, second};
	for (u32 i = 0; i < 2; i++) {
		u32 got;
		u32 wrong = Save_info(copies[i], (u16 *)record, NOR_LIST_RECORD);
		if (wrong || !nor_list_check((const u8 *)FlashBase_S71 + copies[i], &got) || got != seq) {
			// Leave the other copy alone: it may be the only whole one, so the
			// next save must write this one first again
			if (i)
				nor_list_from = first;
			tlog_error("nor: writing the NOR game list at %06lX: %lu of %lu halfwords read back wrong",
				   (unsigned long)copies[i], (unsigned long)wrong, (unsigned long)NOR_LIST_RECORD / 2);
			return 1;
		}
	}
	nor_list_stored = count;
	nor_list_from = second;  // both whole: the next save goes to first
	tlog("nor: list saved, %lu games, number %lu", (unsigned long)count, (unsigned long)seq);
	return 0;
}

void Delete_latest_NOR(void)
{
	if (!game_total_NOR)
		return;
	FM_NOR_FS *e = &pNorFS[game_total_NOR - 1];
	u32 start = (u32)e->rompage << 17, end = start + e->filesize;
	tlog("nor: deleting %s at %07lX", e->filename, (unsigned long)start);
	// The completion mark first, then the header: a power cut midway leaves
	// a game no list entry matches and no mark brings back. A game without
	// a mark goes by its header alone. Writes erase each block they use, so
	// the rest can stay.
	if (e->reserved1 == NOR_ENTRY_MARKED)
		Block_Erase(end - NOR_BLOCK);
	if (e->reserved1 != NOR_ENTRY_MARKED || end - NOR_BLOCK != start)
		Block_Erase(start);
	memset(e, 0, sizeof(*e));
	game_total_NOR--;
	Save_NOR_list(game_total_NOR);
}
//-----------------------------------------------------------
static u32 Loadfile2NOR_slow(TCHAR *filename, u32 NORaddress, u16 have_patch, u8 SAVEMODE)
{
	u32 res;
	u32 ret;
	u32 filesize;
	u32 fileneedsize;

	u32 blocknum;
	char msg[128];
	FM_NOR_FS tmpNorFS;
	char temp[50];
	char flag[4];
	u16 flag_64MBrom;

	//u16 readdata;
	u32 add_patch = 0;

	if (game_total_NOR >= MAX_NOR)  // the list holds MAX_NOR games, however small
		return 2;

	u16 norid = Read_S98NOR_ID();
	if (norid == 0x223D)//S98
	{
		// Torii: the list again, from the kernel NOR and the game NOR now,
		// so a list read wrong earlier can't put this game over another
		Read_NOR_info();
		game_total_NOR = GetFileListFromNor();
		if (gl_norOffset != NORaddress) {
			tlog_error("nor: the game NOR changed: writing at %07lX, not %07lX",
				   (unsigned long)gl_norOffset, (unsigned long)NORaddress);
			NORaddress = gl_norOffset;
		}
		if (game_total_NOR >= MAX_NOR)
			return 2;

		res = f_open(&gfile, filename, FA_READ);
		if (res != FR_OK) {  // Torii: was 0, which callers take for success
			tlog_error("nor: can't open %s: f_open gave %lu", filename, (unsigned long)res);
			return 3;
		}

		filesize = f_size(&gfile);
		f_lseek(&gfile, 0xa0);
		f_read(&gfile, temp, 0x10, (UINT *)&ret);//read game name
		f_lseek(&gfile, 0xB0);
		f_read(&gfile, flag, 0x4, (UINT *)&ret);//read flag

		if (memcmp(flag, "5G", 2) == 0) {
			flag_64MBrom = 1;
		} else {
			flag_64MBrom = 0;
		}

		memset(&tmpNorFS, 0, sizeof(tmpNorFS));
		memcpy(tmpNorFS.gamename, temp, 0x10);
		tmpNorFS.rompage = NORaddress >> 17;

		fileneedsize = ((((filesize + 0x1FFFF) / 0x20000) * 0x20000));
		u32 datasize = fileneedsize;  // the blocks the loop below writes
		if (have_patch) {
			if (iTrimSize >= fileneedsize) {
				fileneedsize = fileneedsize + 0x20000;
				add_patch = 1;
			}
		}
		// Torii: the completion mark takes the area's last NOR_MARK_SPACE
		// bytes, or a block of its own when the ROM or its patch reaches them
		u32 used = filesize;
		if (have_patch && iTrimSize + NOR_PATCH_MAX > used)
			used = iTrimSize + NOR_PATCH_MAX;
		u32 marked = 1;
		if (used > fileneedsize - NOR_MARK_SPACE) {
			if (fileneedsize + NOR_BLOCK <= NOR_GAME_SIZE - NORaddress)
				fileneedsize += NOR_BLOCK;
			else  // no room for its block: listed by its header, as older kernels' games are
				marked = 0;
		}

		if (fileneedsize > (0x4000000 - NORaddress)) {
			f_close(&gfile);  // Torii: was left open
			tlog_error("nor: %s needs %08lX bytes, %08lX are free", filename, (unsigned long)fileneedsize,
				   (unsigned long)(0x4000000 - NORaddress));
			return 2; //Not enough NOR space
		}

		////////////////// erase all BBP
		*((vu16 *)(FlashBase_S98)) = 0xF0;

		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
		*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
		*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xC0;
		*((vu16 *)(FlashBase_S98 + 0x000 * 2)) = 0x80;
		*((vu16 *)(FlashBase_S98 + 0x000 * 2)) = 0x30;
		{
			int polling_counter = 0x15000;
			do {
				(void)*((vu16 *)(FlashBase_S98 + 0x5C0000));
				polling_counter--;

			} while (polling_counter);
		}
		*((vu16 *)(FlashBase_S98 + 0x000 * 2)) = 0x90;
		*((vu16 *)(FlashBase_S98 + 0x000 * 2)) = 0x00;
		/////////////////

		nor_clear_marks(NORaddress);

		tmpNorFS.filesize = fileneedsize;
		tmpNorFS.have_patch = have_patch;
		tmpNorFS.have_RTS = gl_rts_on;
		tmpNorFS.is_64MBrom = flag_64MBrom;
		tmpNorFS.savemode = SAVEMODE;
		tmpNorFS.reserved1 = marked ? NOR_ENTRY_MARKED : 0;

		snprintf(tmpNorFS.filename, sizeof(tmpNorFS.filename), "%s", filename);
		tlog("nor: %s: %lu bytes in an area of %lu", filename, (unsigned long)filesize,
		     (unsigned long)fileneedsize);

		Clear(60, 160 - 15, 120, 15, gl_color_cheat_black, 1);
		DrawHZText12(gl_writing, 0, 70, 160 - 15, gl_color_text, 1);
		for (blocknum = 0; blocknum < filesize; blocknum += 0x20000) {
			sprintf(msg, "%luMb/%luMb", (blocknum) / 0x20000, filesize / 0x20000);
			Clear(70 + 54, 160 - 15, 100, 15, gl_color_cheat_black, 1);
			DrawHZText12(msg, 0, 70 + 54, 160 - 15, gl_color_text, 1);
			Block_Erase(blocknum + NORaddress);

			f_lseek(&gfile, blocknum);
			res = f_read(&gfile, pReadCache, 0x20000, (UINT *)&ret);//pReadCache max 0x20000 Byte
			if (res != FR_OK || (ret < 0x20000 && blocknum + ret < filesize)) {
				tlog_error("nor: reading %s at %lu failed (%lu)", filename, (unsigned long)blocknum,
					   (unsigned long)res);
				f_close(&gfile);
				return 3;  // no completion mark, so it isn't listed
			}
			if (ret < 0x20000)  // past the ROM's end: erased, not the last block's bytes
				memset(pReadCache + ret, 0xFF, 0x20000 - ret);
			if (have_patch) {
				if ((gl_reset_on == 1) || (gl_rts_on == 1) || (gl_sleep_on == 1) ||
				    (gl_cheat_on == 1)) {
					PatchInternal((u32 *)pReadCache, 0x20000, blocknum);
					GBApatch_NOR((u32 *)pReadCache, 0x20000, blocknum);//some nes need check
				}
			} else {
				GBApatch_Cleanrom_NOR((u32 *)pReadCache, blocknum);
			}

			WriteFlash_with32word(blocknum + NORaddress, pReadCache, 0x20000);
			//WriteFlash(blocknum+NORaddress,pReadCache,0x20000);
		}
		f_close(&gfile);

		if (have_patch) {
			if (add_patch) {
				Block_Erase(blocknum + NORaddress);
				memset(pReadCache, 0xFF, 0x20000);  // Torii: only the patch, not the last block again
				GBApatch_NOR((u32 *)pReadCache, 0x20000, blocknum);
				WriteFlash_with32word(blocknum + NORaddress, pReadCache, 0x20000);
				datasize += NOR_BLOCK;
			}
		}

		// Torii: the completion mark, last
		for (blocknum = datasize; blocknum < fileneedsize; blocknum += NOR_BLOCK)
			Block_Erase(blocknum + NORaddress);
		u32 end = NORaddress + fileneedsize;
		if (!marked) {
			tlog("nor: no room for %s's completion mark", filename);
		} else if (!nor_erased(end - NOR_MARK_SPACE, NOR_MARK_SPACE) ||
			   !nor_mark_write(NORaddress, end, &tmpNorFS)) {
			tlog_error("nor: the completion mark of %s at %07lX didn't write", filename,
				   (unsigned long)(end - NOR_MARK_SPACE));
			return 3;
		}

		dmaCopy(&tmpNorFS, &pNorFS[game_total_NOR], sizeof(FM_NOR_FS));
		Save_NOR_list(game_total_NOR + 1);  // if it fails, the mark lists the game
		return 0;
	} else {
#ifdef DEBUG
		//DEBUG_printf("Bad NOR ID");
#endif
		return 1;
	}
}
// Torii: writes a game to the game NOR at NORaddress, at the BIOS's wait
// states (waitcnt.h), and logs it. Returns 0 when written, 1 for an unknown
// NOR, 2 when it doesn't fit, 3 when the file doesn't open.
u32 Loadfile2NOR(TCHAR *filename, u32 NORaddress, u16 have_patch, u8 SAVEMODE)
{
	tlog("nor: writing %s at %08lX, patched %u, save type %02X", filename, (unsigned long)NORaddress, have_patch,
	     SAVEMODE);
	u16 waitcnt = WAITCNT_SLOW();
	u32 res = Loadfile2NOR_slow(filename, NORaddress, have_patch, SAVEMODE);
	WAITCNT_RESTORE(waitcnt);
	if (res == 0)
		tlog("nor: wrote %s at %08lX, %lu bytes", filename, (unsigned long)NORaddress,
		     (unsigned long)pNorFS[game_total_NOR].filesize);
	else if (res == 1)
		tlog_error("nor: not written: the game NOR's ID isn't the S98's");
	tlog_flush();
	return res;
}
//-----------------------------------------------------------
u32 GetFileListFromNor(void)
{
	u8 header[0x20];
	struct nor_mark mark;
	u32 offset, count = nor_walk(&offset);

	if (count < nor_list_stored) {
		count = nor_walk(&offset);  // a second look before dropping games
		if (count < nor_list_stored)
			tlog_error("nor: list entry %lu (%.99s) doesn't match the game NOR at %07lX; it and the %lu "
				   "after it are dropped",
				   (unsigned long)count, pNorFS[count].filename, (unsigned long)offset,
				   (unsigned long)(nor_list_stored - count - 1));
	}
	// Games written in full that the list lacks: a power cut before the list
	// was saved, or a list lost
	for (; count < MAX_NOR && offset < NOR_GAME_SIZE; count++) {
		nor_read(offset + 0xA0, header, sizeof(header));
		u32 end = nor_find_mark(offset, header, &mark);
		if (!end)
			break;
		memcpy(&pNorFS[count], mark.entry, sizeof(FM_NOR_FS));
		if (pNorFS[count].filesize != end - offset || pNorFS[count].rompage != offset >> 17 ||
		    pNorFS[count].reserved1 != NOR_ENTRY_MARKED)
			break;
		pNorFS[count].filename[sizeof(pNorFS[count].filename) - 1] = 0;
		tlog("nor: %s at %07lX has its completion mark but no list entry: listed", pNorFS[count].filename,
		     (unsigned long)offset);
		offset = end;
	}
	if (count < MAX_NOR)
		memset(&pNorFS[count], 0, (MAX_NOR - count) * sizeof(FM_NOR_FS));
	gl_norOffset = offset;
	static u32 last_count = 0xFFFFFFFF, last_offset;  // the menu checks on each visit: log changes
	if (count != last_count || offset != last_offset)
		tlog_debug("nor: %lu games, the next goes at %07lX", (unsigned long)count, (unsigned long)offset);
	last_count = count;
	last_offset = offset;
	return count;
}
//-----------------------------------------------------------
