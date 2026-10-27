#include <stdio.h>
#include <stdlib.h>
#include <gba_base.h>
#include <string.h>
#include <gba_dma.h>

#include "ez_define.h"
#include "GBApatch.h"
#include "gba_nes_patch.h"
#include "ezkernel.h"
#include "reset_table.h"
#include "lang.h"
#include "cheat.h"
#include "cheatstore.h"
#include "Ezcard_OP.h"
#include "irqpatch.h"
#include "progress.h"
#include "savename.h"
#include "gamesave.h"

extern TCHAR currentpath[MAX_path_len];  // ezkernelnew.c's: the game's folder
#include "settings_model.h"
#include "tlog.h"

u32 windows_offset;
u32 is_NORpatch;
u32 g_Offset;
u32 is_Nes;
u32 Nes_index;
u32 Nes_index_17_patch;
u32 iTrimSize;
u32 EA_offset;

u32 w_reset_on;
u32 w_rts_on;
u32 w_sleep_on;
u32 w_cheat_on;
static u32 w_patch_length;  // Torii: the patch length a .pat file kept room for

SPatchInfo2 iPatchInfo2[EMax];
u32 iCount2;

#define sizeofa(array) (sizeof(array) / sizeof(array[0]))

u32 spend_address;
static const char *patch_kind;  //Torii: for Log_patches
static u32 patch_room;          //Torii: the bytes at iTrimSize the patch may use
static u32 rom_bytes;           //Torii: the game's size in PSRAM (GBApatch_PSRAM)

//Torii: the ROM ranges a launch's patches wrote, for the cheats' ROM patches
//to keep off (rom_guard_hits); past ROM_GUARDS ranges, everything is guarded
#define ROM_GUARDS 96
static struct {
	u32 start, end;
} rom_guards[ROM_GUARDS] EWRAM_BSS;
static u32 rom_guard_count;
static u8 rom_guard_full;

void rom_guard_reset(void)
{
	rom_guard_count = 0;
	rom_guard_full = 0;
}

static void rom_guard_add(u32 start, u32 bytes)
{
	if (!bytes)
		return;
	for (u32 i = 0; i < rom_guard_count; i++)
		if (start <= rom_guards[i].end && start + bytes >= rom_guards[i].start) {  // touching: one range
			if (start < rom_guards[i].start)
				rom_guards[i].start = start;
			if (start + bytes > rom_guards[i].end)
				rom_guards[i].end = start + bytes;
			return;
		}
	if (rom_guard_count >= ROM_GUARDS) {
		rom_guard_full = 1;
		return;
	}
	rom_guards[rom_guard_count].start = start;
	rom_guards[rom_guard_count].end = start + bytes;
	rom_guard_count++;
}

int rom_guard_hits(u32 offset, u32 bytes)
{
	if (rom_guard_full)
		return 1;
	for (u32 i = 0; i < rom_guard_count; i++)
		if (offset < rom_guards[i].end && offset + bytes > rom_guards[i].start)
			return 1;
	return 0;
}
//------------------------------------------------------------------
static void Write(u32 romaddress, const u8 *buffer, u32 size)
{
	u32 x;
	if (is_NORpatch) {
		if ((romaddress >= windows_offset) && (romaddress < windows_offset + 0x20000)) {
			for (x = 0; x < size / 2; x++) {
				((vu16 *)(pReadCache + romaddress - windows_offset))[x] = ((vu16 *)buffer)[x];
			}
			//DEBUG_printf("NORaddress{%x}:%x %x", romaddress,size ,((vu32*)buffer)[0]);
		}
	} else {
		rom_guard_add(romaddress, size);
		u32 Address;
		vu16 page = 0;
		Address = romaddress;
		while (Address >= 0x800000) {
			Address -= 0x800000;
			page += 0x1000;
		}
		SetPSRampage(page);

		for (x = 0; x < size / 2; x++)
			((vu16 *)(PSRAMBase_S98 + Address))[x] = ((vu16 *)buffer)[x];//todo 还要处理psram page

		//DEBUG_printf("address{%x}:%x %x %x %x", romaddress,page,Address,size ,((vu32*)buffer)[0]);
		SetPSRampage(0);
	}
}
//------------------------------------------------------------------
bool IWRAM_CODE PatchDragonBallZ(u32 *Data)
{
	(void)Data;
	bool res = false;
	const u32 game_codes_DragonBallZ[] = {
		0x45464c42, //2 Games in 1 - Dragon Ball Z - The Legacy of Goku I & II (USA).gba
		0x50474c41, //Dragon Ball Z - The Legacy of Goku (Europe) (En,Fr,De,Es,It).gba
		0x45474c41, //Dragon Ball Z - The Legacy of Goku (USA).gba
		0x50464c41, //Dragon Ball Z - The Legacy of Goku II (Europe) (En,Fr,De,Es,It).gba
		0x45464c41, //Dragon Ball Z - The Legacy of Goku II (USA).gba
		0x4a464c41,  //Dragon Ball Z - The Legacy of Goku II International (Japan).gba

		0x45593241   //1928 - Top Gun - Combat Zones(US).gba

	};
	const u32 patch_1_offsets[] = {0x033C, 0x0340, 0x0356, 0x035A, 0x035E, 0x0384, 0x0388,
				       0x494C, 0x4950, 0x4978, 0x497C, 0x998E, 0x9992};
	const u32 patch_2_offsets[] = {0x356, 0x35e, 0x37e, 0x382};
	s32 index_DragonBallZ = -1;
	for (u32 ii = 0; ii < sizeofa(game_codes_DragonBallZ); ii++) {
		if (*(u32 *)GAMECODE == game_codes_DragonBallZ[ii]) {
			index_DragonBallZ = ii;
			break;
		}
	}
	if (index_DragonBallZ != -1) {
		u16 patch = 0;
		res = true;
		switch (index_DragonBallZ) {
		case 0: //2in1 us
			for (u32 ii = 0; ii < sizeofa(patch_2_offsets); ii++) {
				Write(patch_2_offsets[ii] + 0x40000, (u8 *)&patch, sizeof(patch));
			}
			patch = 0x1001;
			Write(0xbb9016, (u8 *)&patch, sizeof(patch));
			break;
		case 1: //I eu
			patch = 0x46c0;
			for (u32 ii = 0; ii < sizeofa(patch_1_offsets); ii++) {
				Write(patch_1_offsets[ii], (u8 *)&patch, sizeof(patch));
			}
			break;
		case 2: //I us
			for (u32 ii = 0; ii < sizeofa(patch_2_offsets); ii++) {
				Write(patch_2_offsets[ii], (u8 *)&patch, sizeof(patch));
			}
			break;
		case 3: //II eu
			patch = 0x1001;
			Write(0x6f42b2, (u8 *)&patch, sizeof(patch));
			break;
		case 4: //II us
			patch = 0x1001;
			Write(0x3b8e9e, (u8 *)&patch, sizeof(patch));
			break;
		case 5: //II jp
			patch = 0x1001;
			Write(0x3fc8f6, (u8 *)&patch, sizeof(patch));
			break;
		case 6: //1928 - Top Gun - Combat Zones(US).gba
			patch = 0x3401;
			Write(0x088816, (u8 *)&patch, sizeof(patch));
			patch = 0x46c0; //only need one
			Write(0x088814, (u8 *)&patch, sizeof(patch));
			Write(0x088932, (u8 *)&patch, sizeof(patch));
			Write(0x088938, (u8 *)&patch, sizeof(patch));
			Write(0x08893C, (u8 *)&patch, sizeof(patch));
			Write(0x08897C, (u8 *)&patch, sizeof(patch));
			Write(0x088982, (u8 *)&patch, sizeof(patch));
			Write(0x088986, (u8 *)&patch, sizeof(patch));
			Write(0x088988, (u8 *)&patch, sizeof(patch));
			break;
		default:
			res = false;
			break;
		}
	}
	return res;
}
//------------------------------------------------------------------
void IWRAM_CODE CheckNes(u32 *Data)
{
	u32 jump = Data[0];
	if ((jump & 0xff000000) == 0xea000000) {
		Nes_index = (jump & 0xffffff) + 2;
		if ((Data[Nes_index] & 0xffffff00) == 0xe28f5000 && Data[Nes_index + 1] == 0xe8b503d3 &&
		    Data[Nes_index + 2] == 0xe129f007 && Data[Nes_index + 3] == 0xe281deba &&
		    Data[Nes_index + 4] == 0xe129f008 && Data[Nes_index + 5] == 0xe281debe &&
		    Data[Nes_index + 6] == 0xe129f009 && Data[Nes_index + 7] == 0xe281dc0b &&
		    Data[Nes_index + 8] == 0xe92d0003 && Data[Nes_index + 9] == 0xef110000 &&
		    Data[Nes_index + 10] == 0xe8bd8001) {
			if (Data[Nes_index] == 0xe28f503c)
				is_Nes = 1;
			else if (Data[Nes_index] == 0xe28f5040)
				is_Nes = 2;
		} else
			is_Nes = 0;
	}
}
//------------------------------------------------------------------
static bool PatchNes(u32 *Data)
{
	bool res = false;
	if (is_Nes) {
		res = true;
		u32 patch = 0xea000000 | (0x3fdf5 - Nes_index);
		Write(36 + Nes_index * 4, (u8 *)&patch, sizeof(patch));
		Write(0xff800, gba_nes_patch_bin, 0x340);
		if (windows_offset == 0)
			Nes_index_17_patch = Data[Nes_index + 17 + is_Nes - 1];
		Write(0xff840, (u8 *)&Nes_index_17_patch, sizeof(patch));
		u32 index2 = (Nes_index_17_patch - 0x08000000) / 4;
		if (Data[index2 - 1 - windows_offset / 4] == 0x3032) {
			patch = 0x060000f8;
			Write(0xff86c, (u8 *)&patch, sizeof(patch));
		}
	}
	return res;
}
//---------------------------------------------------------------------------------
static void Add2(u32 anOffset, u32 aValue)
{
	if (iCount2 < (EMax)) {
		anOffset = anOffset + g_Offset;
		SPatchInfo2 info = {anOffset, aValue};
		iPatchInfo2[iCount2++] = info;
	}
}
//------------------------------------------------------------------
void IWRAM_CODE PatchInternal(u32 *Data, int iSize, u32 offset)
{
	u32 search_size = iSize / 4;
	g_Offset = offset / 4;
	if (offset == 0) {
		EA_offset = Data[0] & 0xFFFFFF;
	}

	for (u32 ii = 0; ii < search_size; ii++) {
		switch (Data[ii]) {
		case 0x3007FFC: // IRQ handler
		{
			Add2(ii, 0x3007FF4);//0x3007FFC的位置
		} break;
		case 0x3FFFFFC: // IRQ handler
		{
			Add2(ii, 0x3007FF4);
		} break;
		}
	}
}
//------------------------------------------------------------------
// Bytes SetTrimSize keeps free at the ROM's end for the in-game code.
static u32 Patch_length(void)
{
	if (gl_cheat_on == 1)
		return CHEAT_PATCH_LENGTH;
	if ((gl_rts_on == 1) /* || (gl_cheat_on==1)*/)
		return RTS_PATCH_LENGTH;
	return 0x300;
}
//------------------------------------------------------------------
//Torii: 1 if the ROM's halfword h at offset at is no data: 0x0000 or 0xFFFF
//padding, or the open-bus value an overdump past a game's end holds
static int blank_half(u32 at, u16 h)
{
	return h == 0 || h == 0xFFFF || h == (u16)(at >> 1);
}
//Torii: 1 if the ROM holds no data in [start, end), as buffer (its iSize
//bytes from base) shows it; past romsize counts as none, and outside the
//buffer as data
static int rom_bytes_blank(const u8 *buffer, u32 base, u32 iSize, u32 romsize, u32 start, u32 end)
{
	if (end > romsize)
		end = romsize;
	for (u32 at = start & ~1u; at < end; at += 2) {
		if (at < base || at + 2 > base + iSize)
			return 0;
		if (!blank_half(at, (u16)(buffer[at - base] | buffer[at - base + 1] << 8)))
			return 0;
	}
	return 1;
}
//------------------------------------------------------------------
//Torii: the bytes this launch's in-game code takes with the picked cheats:
//the menu's code, the interpreter and their table (DEBUG's code goes in only
//where it fits after them)
static u32 Patch_need(void)
{
	u32 need = (u32)RTS_ReplaceIRQ_end - (u32)RTS_ReplaceIRQ_start;
	if (gl_cheat_on == 1 && gl_cheat_bytes)
		need += (u32)Cheat_blob_end - (u32)Cheat_blob_start + sizeof(gl_cheat_off) + gl_cheat_bytes;
	return (need + 15) & ~15u;
}
//------------------------------------------------------------------
void SetTrimSize(u8 *buffer, u32 romsize, u32 iSize, u32 mode, BYTE saveMODE)
{
	u8 byte;
	u32 bottom;
	u32 top;
	u32 alignedSize;
	u32 data_end = 0;  //Torii: where the scan saw the ROM's data end

	u32 PATCH_LENGTH = Patch_length();

	if (0) {
		iTrimSize = 0x1fff000;
	} else {
		byte = buffer[(romsize - 1) % iSize - 1];
		bottom = (romsize - 1) % iSize - 1;
		if (bottom - 16 > PATCH_LENGTH) {
			top = bottom - PATCH_LENGTH - 16;
		} else {
			top = 0;
		}
		alignedSize = romsize + (16 - (romsize & 15));

		for (u32 ii = bottom; ii >= top; ii--) {
			if (buffer[ii] != byte || ii == top) {
				// (only 0x00 and 0xFF pad a ROM: a run of another byte is its data)
				data_end =
					byte == 0x00 || byte == 0xFF ? ((romsize - 1) & 0xFFFE0000) + ii + 1 : romsize;
				iTrimSize = ii + 4;
				iTrimSize = iTrimSize + ((romsize - 1) & 0xFFFE0000);
				iTrimSize = iTrimSize + (16 - (iTrimSize & 15));
				if (iTrimSize > alignedSize)
					iTrimSize = alignedSize;
				break;
			}
		}
	}
	//DEBUG_printf("iTrimSize %08X ", iTrimSize);
	if (mode == 1)//nor
	{
		if (((iTrimSize & 0x1FFFF) + PATCH_LENGTH) > 0x20000)//Greater than one flash sector
		{
			iTrimSize = ((romsize + 0x1FFFF) / 0x20000) * 0x20000;
		}
		if (romsize <= 0x1000000) {
			if ((iTrimSize + PATCH_LENGTH) > 16 * 1024 * 1024) {
				if ((saveMODE == 0x21) || (saveMODE == 0x22))//eeprom game
				{
					iTrimSize = 0x1000000 - PATCH_LENGTH;//can not greater 16MB
				} else {//sram and flash
					iTrimSize = 0x1000000; //page
				}
			}
		}
	} else {
		if (romsize <= 0x800000) {
			if ((iTrimSize + PATCH_LENGTH) > 8 * 1024 * 1024)
				iTrimSize = 0x800000; //page
		} else if (romsize <= 0x1000000) {
			if ((iTrimSize + PATCH_LENGTH) > 16 * 1024 * 1024) {
				if ((saveMODE == 0x21) || (saveMODE == 0x22))//eeprom game
				{
					iTrimSize = 0x1000000 - PATCH_LENGTH;//can not greater 16MB
				} else {//sram and flash
					iTrimSize = 0x1000000; //page
				}
			}
		} else {
			if ((iTrimSize + PATCH_LENGTH) > 32 * 1024 * 1024)
				iTrimSize = 0x2000000 - PATCH_LENGTH; //page
		}
	}
	patch_room = PATCH_LENGTH;
	u32 scanned = iTrimSize;
	Patch_SpecialROM_TrimSize();
	if (iTrimSize != scanned || gl_cheat_on != 1 || iTrimSize >= data_end)
		return;
	//Torii: a cap (32 MB, or 16 MB for EEPROM) put the patch area on what may
	//be the game's data. Where that is padding (an overdump's open-bus fill
	//too), it stays; else the cheats' room is cut to what they need, at the
	//cap's end, if that much is padding
	u32 limit = iTrimSize + PATCH_LENGTH, need = Patch_need();
	u32 base = (romsize - 1) & 0xFFFE0000;
	if (rom_bytes_blank(buffer, base, iSize, romsize, iTrimSize, limit))
		return;
	u32 at = (limit - need) & ~15u;
	if (!rom_bytes_blank(buffer, base, iSize, romsize, at, limit) && gl_cheat_count > gl_cheat_picks) {
		// Without the extras (the cheats off for the in-game list) the picks may fit
		cheatstore_drop_extras();
		need = Patch_need();
		at = (limit - need) & ~15u;
	}
	if (rom_bytes_blank(buffer, base, iSize, romsize, at, limit)) {
		tlog("launch: the patch area at %08lX is on the game's data: cut to %lX bytes at %08lX",
		     (unsigned long)iTrimSize, (unsigned long)limit - at, (unsigned long)at);
		iTrimSize = at;
		patch_room = limit - at;
		return;
	}
	tlog("launch: the patch area at %08lX is on the game's data, and the cheats' %lX bytes from %08lX would "
	     "be too: the cheats that run in game are left out",
	     (unsigned long)iTrimSize, (unsigned long)need, (unsigned long)at);
	gl_cheat_on = 0;
	gl_cheat_count = gl_cheat_bytes = 0;
	gl_cheat_picks = gl_cheat_pick_bytes = 0;
	SetTrimSize(buffer, romsize, iSize, mode, saveMODE);  // as without cheats
}
//------------------------------------------------------------------
static void Patch_B_address(void)
{
	if (!iCount2)
		return;
	u32 B_install_handler;
	B_install_handler = 0xEA000000 | ((iTrimSize - 8) / 4);
	Write(0, (u8 *)&B_install_handler, 4); //B

	for (u32 ii = 0; ii < iCount2; ii++) {
		Write(iPatchInfo2[ii].iOffset * 4, (u8 *)&(iPatchInfo2[ii].iValue), sizeof(iPatchInfo2[ii].iValue));
	}
}
//------------------------------------------------------------------
//Torii: the hotkeys as Settings shows them (settings_hotkeys): an erased
//slot, or a stored pair Settings wouldn't accept, falls back to L R START
//and L R SELECT.
static void Read_hotkeys(u16 *reset, u16 *sleep)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	settings_hotkeys(slots, reset, sleep);
}
static void Patch_Reset_Sleep(u32 *Data)
{
	(void)Data;
	Patch_B_address();
	u32 Return_address = 0x8000000 + EA_offset * 4 + 8;

	u8 *p_patch_start = (u8 *)Sleep_ReplaceIRQ_start;
	u8 *p_patch_end = (u8 *)Sleep_ReplaceIRQ_end;
	u8 *p_patch_Return_address_L = (u8 *)Return_address_L;

	u8 *patchbuffer = (u8 *)_UnusedVram;
	u32 Return_address_offset = p_patch_Return_address_L - p_patch_start;

	dmaCopy((void *)p_patch_start, patchbuffer, p_patch_end - p_patch_start);
	*(vu32 *)(patchbuffer + Return_address_offset) = Return_address;//修改gba_sleep_patch_bin里面的返回地址

	u16 sleep_key, reset_key;
	Read_hotkeys(&reset_key, &sleep_key);

	u32 Reset_key_offset = (u8 *)Reset_key - p_patch_start;
	u32 Sleep_key_offset = (u8 *)Sleep_key - p_patch_start;

	if (gl_reset_on != 1) {
		*(vu32 *)(patchbuffer + Reset_key_offset) = HOTKEY_OFF;
	} else {
		*(vu32 *)(patchbuffer + Reset_key_offset) = reset_key & 0x3FF;
	}

	if (gl_sleep_on != 1) {
		*(vu32 *)(patchbuffer + Sleep_key_offset) = HOTKEY_OFF;
	} else {
		*(vu32 *)(patchbuffer + Sleep_key_offset) = sleep_key & 0x3FF;
	}

	Write(iTrimSize, patchbuffer, p_patch_end - p_patch_start);
	rom_guard_add(iTrimSize, patch_room ? patch_room : Patch_length());
}
//------------------------------------------------------------------
//Torii: 1 if the game's PSRAM image holds no data in [start, end): past the
//file's end, or padding throughout (blank_half)
static int rom_blank(u32 start, u32 end)
{
	if (end > rom_bytes)
		end = rom_bytes;
	if (start >= end)
		return 1;
	int blank = 1;
	for (u32 at = start & ~1u; at < end && blank; at += 2) {
		SetPSRampage((u16)((at >> 23) * 0x1000));
		blank = blank_half(at, *(vu16 *)((u8 *)PSRAMBase_S98 + (at & 0x7FFFFF)));
	}
	SetPSRampage(0);
	return blank;
}
//------------------------------------------------------------------
static void Patch_RTS_Cheat(u32 *Data)
{
	(void)Data;
	Patch_B_address();
	u32 Return_address = 0x8000000 + EA_offset * 4 + 8;

	u8 *p_patch_start = (u8 *)RTS_ReplaceIRQ_start;
	u8 *p_patch_end = (u8 *)RTS_ReplaceIRQ_end;
	u8 *p_patch_Return_address_L = (u8 *)RTS_Return_address_L;

	u8 *patchbuffer = (u8 *)_UnusedVram;
	u32 Return_address_offset = p_patch_Return_address_L - p_patch_start;

	dmaCopy((void *)p_patch_start, patchbuffer, p_patch_end - p_patch_start);
	*(vu32 *)(patchbuffer + Return_address_offset) = Return_address;//modify gba_sleep_patch_bin return address

	if (spend_address != 0x0) {
		*(vu32 *)(patchbuffer + Return_address_offset + 4) = spend_address;
	}

	u16 RTS_sleep_key_val, RTS_reset_key_val;
	Read_hotkeys(&RTS_reset_key_val, &RTS_sleep_key_val);

	u32 RTS_Reset_key_offset = (u8 *)RTS_Reset_key - p_patch_start;
	u32 RTS_Sleep_key_offset = (u8 *)RTS_Sleep_key - p_patch_start;

	//Torii (v0.8): In-game menu Off: the menu keys open nothing, the cheats still run
	*(vu32 *)(patchbuffer + RTS_Reset_key_offset) = gl_menu_off ? HOTKEY_OFF : RTS_reset_key_val & 0x3FF;
	if (gl_sleep_on != 1) {
		*(vu32 *)(patchbuffer + RTS_Sleep_key_offset) = HOTKEY_OFF;
	} else {
		*(vu32 *)(patchbuffer + RTS_Sleep_key_offset) = RTS_sleep_key_val & 0x3FF;
	}

	//rts switch
	u32 rts_switch_offset = (u8 *)RTS_switch - p_patch_start;
	*(vu32 *)(patchbuffer + rts_switch_offset) = gl_rts_on;

	//Torii: the cheats (v0.7): the interpreter after the menu's code, its
	//off mask (v0.8) and table after it, and Cheat_switch the interpreter's
	//offset (0: none)
	u32 copysize = p_patch_end - p_patch_start;
	TLOG_ASSERT(copysize <= RTS_PATCH_LENGTH);
	u32 cheat_at = 0;
	u32 room = patch_room ? patch_room : Patch_length();
	if (iTrimSize + room > 0x2000000)
		room = 0x2000000 - iTrimSize;
	if (gl_cheat_on == 1 && gl_cheat_bytes) {
		u32 code = (u8 *)Cheat_blob_end - (u8 *)Cheat_blob_start;
		u32 end = copysize + code + sizeof(gl_cheat_off) + gl_cheat_bytes;
		TLOG_ASSERT(gl_cheat_bytes <= CHEAT_TABLE_MAX && end <= CHEAT_PATCH_LENGTH);
		//Torii: never a table cut short, or over the game's data; the
		//extras (v0.8's cheats off for the in-game list) go first
		if (gl_cheat_count > gl_cheat_picks &&
		    (end > room || (!is_NORpatch && !rom_blank(iTrimSize + copysize, iTrimSize + end)))) {
			cheatstore_drop_extras();
			end = copysize + code + sizeof(gl_cheat_off) + gl_cheat_bytes;
		}
		if (!gl_cheat_bytes) {
			tlog("launch: no cheats picked, and no room for the in-game list's");
		} else if (end > room) {
			tlog("launch: the cheats' %lX bytes don't fit the patch area's %lX: left out",
			     (unsigned long)end, (unsigned long)room);
		} else if (!is_NORpatch && !rom_blank(iTrimSize + copysize, iTrimSize + end)) {
			tlog("launch: the game's data is at %08lX, where the cheats would go: left out",
			     (unsigned long)iTrimSize + copysize);
		} else {
			dmaCopy((void *)Cheat_blob_start, patchbuffer + copysize, code);
			dmaCopy(gl_cheat_off, patchbuffer + copysize + code, sizeof(gl_cheat_off));
			dmaCopy(gl_cheat_table, patchbuffer + copysize + code + sizeof(gl_cheat_off), gl_cheat_bytes);
			cheat_at = copysize;
			copysize = end;
		}
	}
	*(vu32 *)(patchbuffer + ((u8 *)Cheat_switch - p_patch_start)) = cheat_at;

	//Torii: the menu's DEBUG entry: its code goes after the cheats, only with
	//LOGGING on and if it fits the space kept for the patch.
	u32 debug_size = (u8 *)Debug_blob_end - (u8 *)Debug_blob_start;
	u32 debug_at = 0;
	if (tlog_level != TLOG_OFF && !is_NORpatch) {  // a NOR copy would keep it with logging off
		if (copysize + debug_size <= room) {
			dmaCopy((void *)Debug_blob_start, patchbuffer + copysize, debug_size);
			debug_at = copysize;
			copysize += debug_size;
		} else {
			tlog("launch: no room for the in-game menu's DEBUG entry");
		}
	}
	*(vu32 *)(patchbuffer + ((u8 *)Debug_switch - p_patch_start)) = debug_at;

	if (iTrimSize + copysize > 0x2000000) {
		copysize = 0x2000000 - iTrimSize;
	}
	//DEBUG_printf("iTrimSize =%x %x", iTrimSize,copysize);

	Write(iTrimSize, patchbuffer, copysize);
	rom_guard_add(iTrimSize, room);
}
//------------------------------------------------------------------
void GBApatch_Cleanrom(u32 *address, int filesize)//Only once
{
	(void)filesize;
	windows_offset = 0;
	is_NORpatch = 0;
	CheckNes(address);
	PatchNes(address);
	PatchDragonBallZ(address);
	//Check_Fire_Emblem();
}
//------------------------------------------------------------------
static u32 Get_spend_address(u32 *Data)
{
	u32 ii;
	u32 offset;
	u32 updown;

	u32 search_size = 0x5000 / 4;
	for (ii = 0; ii < search_size; ii++) {
		u32 word1 = Data[ii] & 0xFFFF001F;
		u32 word2 = Data[ii + 1];
		if (word1 == 0xE3A0001F) {
			if ((word2 == 0xE129F000) || (word2 == 0xE121F000)) {
				offset = Data[ii + 2] & 0xFFF;
				updown = Data[ii + 2] & 0x00F00000;
				break;
			}
		}
	}
	if (ii == search_size) {
		return 0;//Find_spend_address_SpecialROM(Data);
	}

	u32 address;
	if (updown == 0x00900000)
		address = ii * 4 + offset + 16;
	else if (updown == 0x00100000)
		address = ii * 4 - offset + 16;
	else
		address = 0;

	if ((Data[address / 4] > 0x03007E80) /*|| (Data[address/4] == 0x03007E00)*/ ||
	    (Data[address / 4] == 0x0203FFFC)) {
		u32 top = Data[address / 4];
		Data[address / 4] = top - 0x80;
		//Torii: a later "ldr sp,[pc,#n]" of the same stack top would put the
		//stack back over the in-game menu's save area (Crash Nitro Kart reloads
		//it after a RAM reset), so lower those too.
		for (u32 k = ii + 3; k < ii + 3 + 16 && k < search_size; k++) {
			if ((Data[k] & 0xFFFFF000) == 0xE59FD000) {
				u32 lit = k * 4 + 8 + (Data[k] & 0xFFF);
				if (Data[lit / 4] == top)
					Data[lit / 4] = top - 0x80;
			}
		}
		return (Data[address / 4]);
	} else
		return 0;
}
//------------------------------------------------------------------
void GBApatch_PSRAM(u32 *address, int filesize)//Only once
{
	windows_offset = 0;
	is_NORpatch = 0;
	EA_offset = address[0] & 0xFFFFFF;
	rom_bytes = (u32)filesize;

	CheckNes(address);
	PatchNes(address);
	PatchDragonBallZ(address);
	Patch_somegame(address);

	//Torii (v0.8): In-game menu Every game (gl_reset_on) puts the menu on the
	//menu keys too, where Back to menu left the game for the kernel
	if ((gl_rts_on == 1) || (gl_reset_on == 1) || ((gl_cheat_on == 1) && (gl_cheat_count > 0))) {
		spend_address = Get_spend_address(address);
		//DEBUG_printf("spend_address =%x",spend_address);
		Patch_RTS_Cheat(address);
		patch_kind = "in-game menu (RTS_ReplaceIRQ)";
	} else {
		Patch_Reset_Sleep(address);
		patch_kind = "reset and sleep keys (Sleep_ReplaceIRQ)";
	}
}
//------------------------------------------------------------------
void Log_patches(void)
{
	if (!tlog_wants(TLOG_INFO))
		return;
	tlog("launch: in-game code: %s at ROM %08lX, game entry %08lX, stack literal %08lX",
	     patch_kind ? patch_kind : "none", (unsigned long)iTrimSize, (unsigned long)EA_offset * 4 + 8,
	     (unsigned long)spend_address);
	tlog("launch: %lu patched words (offset=value):", (unsigned long)iCount2);
	for (u32 i = 0; i < iCount2; i += 4) {
		char words[4][20];
		for (u32 j = 0; j < 4; j++) {
			words[j][0] = 0;
			if (i + j < iCount2)
				snprintf(words[j], sizeof(words[j]), " %06lX=%08lX",
					 (unsigned long)iPatchInfo2[i + j].iOffset * 4,
					 (unsigned long)iPatchInfo2[i + j].iValue);
		}
		tlog("launch: %s%s%s%s", words[0], words[1], words[2], words[3]);
	}
}
//------------------------------------------------------------------
void GBApatch_Cleanrom_NOR(u32 *address, u32 offset)
{
	windows_offset = offset;
	is_NORpatch = 1;
	if (offset == 0) {
		CheckNes(address);
	}
	PatchNes(address);
	PatchDragonBallZ(address);
	//Check_Fire_Emblem();
}
//------------------------------------------------------------------
void GBApatch_NOR(u32 *address, int filesize, u32 offset)
{
	(void)filesize;
	windows_offset = offset;
	is_NORpatch = 1;
	if (offset == 0) {
		CheckNes(address);
		EA_offset = address[0] & 0xFFFFFF;

		u32 B_install_handler;
		B_install_handler = 0xEA000000 | ((iTrimSize - 8) / 4);
		Write(0, (u8 *)&B_install_handler, 4); //B
		spend_address = Get_spend_address(address);

		Patch_somegame(address);
	}
	PatchNes(address);
	PatchDragonBallZ(address);
	//Check_Fire_Emblem();

	if ((gl_rts_on == 1) || (gl_reset_on == 1) || ((gl_cheat_on == 1) && (gl_cheat_count > 0))) {
		Patch_RTS_Cheat(address);
	} else {
		Patch_Reset_Sleep(address);
	}
}
//------------------------------------------------------------------
static void make_pat_name(TCHAR *patnamebuf, TCHAR *gamefilename)
{
	name_with_ext(patnamebuf, gamefilename, "pat");
}
//------------------------------------------------------------------
static void GBA_patch_init(void)
{
	is_NORpatch = 0;
	windows_offset = 0;
	is_Nes = 0;
	Nes_index = 0;
	g_Offset = 0;

	iCount2 = 0;
	iTrimSize = 0;
	EA_offset = 0;
	patch_room = 0;

	w_reset_on = 0;
	w_rts_on = 0;
	w_sleep_on = 0;
	w_cheat_on = 0;
	w_patch_length = 0;

	memset(iPatchInfo2, 0x00, sizeof(iPatchInfo2));
}
//------------------------------------------------------------------
static void GBA_patch_init_buffer(u32 *buffer)
{
	memcpy(iPatchInfo2, buffer, sizeof(iPatchInfo2));
	u32 start = sizeof(iPatchInfo2) / 4;

	is_NORpatch = buffer[start + 0];
	windows_offset = buffer[start + 1];
	is_Nes = buffer[start + 2];
	Nes_index = buffer[start + 3];
	g_Offset = buffer[start + 4];
	iCount2 = buffer[start + 5];
	iTrimSize = buffer[start + 6];
	EA_offset = buffer[start + 7];

	w_reset_on = buffer[start + 8];
	w_rts_on = buffer[start + 9];
	w_sleep_on = buffer[start + 10];
	w_cheat_on = buffer[start + 11];
	w_patch_length = buffer[start + 12];
	patch_room = w_patch_length;
}
//------------------------------------------------------------------
u32 Check_pat(TCHAR *gamefilename)
{
	UINT ret;
	u32 find_the_patfile;
	u32 patfilesize;
	u32 res;

	TCHAR patnamebuf[GAME_NAME_MAX + 1];
	make_pat_name(patnamebuf, gamefilename);
	res = f_chdir("/PATCH");
	if (res == FR_OK) {
		res = f_open(&gfile, patnamebuf, FA_READ);

		if (res == FR_OK)//have a old file
		{
			patfilesize = f_size(&gfile);
			//Torii: only a file of this kernel's layout (EMax slots, then 16 words);
			//another size is from an older kernel, and the ROM is scanned again.
			find_the_patfile = (patfilesize == sizeof(iPatchInfo2) + 16 * 4);
			//Torii: and only one read whole, or pReadCache holds an earlier owner's bytes
			if (find_the_patfile &&
			    (f_read(&gfile, pReadCache, patfilesize, &ret) != FR_OK || ret != patfilesize))
				find_the_patfile = 0;
			f_close(&gfile);
			if (find_the_patfile && ((u32 *)pReadCache)[sizeof(iPatchInfo2) / 4 + 5] > EMax)
				find_the_patfile = 0;//Torii: a slot count past EMax is corrupt
		} else {
			find_the_patfile = 0;
		}
		//res=f_chdir("/");
	} else//no PATCH folder
	{
		find_the_patfile = 0;
	}

	if (find_the_patfile) {
		//read patch information
		GBA_patch_init_buffer((u32 *)pReadCache);

		//Torii: and, with cheats, room for this launch's picks (before v0.7,
		//cheats kept 0x2000 bytes and no length was saved)
		if ((w_reset_on != gl_reset_on) || (w_rts_on != gl_rts_on) || (w_sleep_on != gl_sleep_on) ||
		    (w_cheat_on != gl_cheat_on) || (gl_cheat_on == 1 && w_patch_length < Patch_need())) {
			GBA_patch_init();
			find_the_patfile = 0;
		}
	} else {
		GBA_patch_init();
	}
	return find_the_patfile;
}
//------------------------------------------------------------------
void Make_pat_file(TCHAR *gamefilename)
{
	u32 res;
	u32 written;
	u32 w_buffer[16];

	res = f_mkdir("/PATCH");
	res = f_chdir("/PATCH");

	memset(w_buffer, 0x00, sizeof(w_buffer));

	if (res == FR_OK) {
		TCHAR patnamebuf[GAME_NAME_MAX + 1];
		make_pat_name(patnamebuf, gamefilename);

		res = f_open(&gfile, patnamebuf, FA_WRITE | FA_OPEN_ALWAYS);
		if (res == FR_OK) {
			f_lseek(&gfile, 0x0000);
			res = f_write(&gfile, (void *)iPatchInfo2, sizeof(iPatchInfo2), (UINT *)&written);
			w_buffer[0] = is_NORpatch;
			w_buffer[1] = windows_offset;
			w_buffer[2] = is_Nes;
			w_buffer[3] = Nes_index;
			w_buffer[4] = g_Offset;
			w_buffer[5] = iCount2;
			w_buffer[6] = iTrimSize;
			w_buffer[7] = EA_offset;

			w_buffer[8] = gl_reset_on;
			w_buffer[9] = gl_rts_on;
			w_buffer[10] = gl_sleep_on;
			w_buffer[11] = gl_cheat_on;
			w_buffer[12] = patch_room;  // Torii: the room iTrimSize kept

			res = f_write(&gfile, (void *)w_buffer, sizeof(w_buffer), (UINT *)&written);
			f_close(&gfile);
		}
	}
	res = f_chdir("/");
}
//------------------------------------------------------------------
// A GBA game's save type and boot mode are in its record (gamerec.h), in its
// folder of saves; gamefilename is in currentpath (not FatFs's folder, which
// the cheat check changes)
u8 Check_mde_file(TCHAR *gamefilename, u8 *boot_mode)
{
	char rom[GAMESAVE_PATH_BYTES];
	struct gamerec rec;

	*boot_mode = 0;
	if (gamesave_rom_path_in(rom, sizeof(rom), currentpath, gamefilename) < 0)
		return 0;
	gamesave_record_read(rom, &rec);
	*boot_mode = rec.boot_mode;
	return rec.save_type;
}
//------------------------------------------------------------------
// Sets the save type (what 0) or the boot mode (1) in a game's record, keeping
// the rest; 0 if written
static u8 mde_write(TCHAR *gamefilename, u32 at, u8 value)
{
	char rom[GAMESAVE_PATH_BYTES];
	struct gamerec rec;

	if (gamesave_rom_path_in(rom, sizeof(rom), currentpath, gamefilename) < 0)
		return 1;
	gamesave_record_read(rom, &rec);
	if (at == 0)
		rec.save_type = value;
	else
		rec.boot_mode = value;
	return gamesave_record_write(rom, &rec) ? 0 : 1;
}
//------------------------------------------------------------------
u8 Make_mde_file(TCHAR *gamefilename, u8 Save_num)
{
	return mde_write(gamefilename, 0, Save_num);
}
//------------------------------------------------------------------
u8 Make_mde_boot(TCHAR *gamefilename, u8 boot_mode)
{
	return mde_write(gamefilename, 1, boot_mode);
}
//------------------------------------------------------------------
u32 Check_RTS(TCHAR *gamefilename)
{
	//UINT  ret;
	//u32 find_the_patfile;
	u32 rtsfilesize;
	u32 res;

	TCHAR rtsnamebuf[GAME_NAME_MAX + 1];
	name_with_ext(rtsnamebuf, gamefilename, "rts");

	res = f_mkdir("/RTS");
	res = f_chdir("/RTS");
	if (res != FR_OK) {
		return 0;
	}

	res = f_open(&gfile, rtsnamebuf, FA_OPEN_EXISTING);

	if (res == FR_OK)//have a old rts file
	{
		rtsfilesize = f_size(&gfile);
		f_close(&gfile);
	} else //make a new one
	{
		tlog("launch: making the save state file /RTS/%s", rtsnamebuf);
		res = f_open(&gfile, rtsnamebuf, FA_WRITE | FA_OPEN_ALWAYS);
		if (res == FR_OK) {
			int i;
			unsigned int written;
			for (i = 0; i < (0x70000) / 0x800; i++) {
				// Filled each time: the frame progress_part may run
				// can borrow pReadCache (ez_define.h)
				memset(pReadCache, 0xFF, 0x200 * 4);
				f_write(&gfile, pReadCache, 0x200 * 4, (UINT *)&written);
				if (written != 0x200 * 4)
					break;
				progress_part((u32)i + 1, 0x70000 / 0x800);  // its step of the launch's bar
			}
			f_close(&gfile);

			rtsfilesize = 0x70000;
		} else
			rtsfilesize = 0;
	}
	LoadRTSfile(rtsnamebuf);
	if (rtsfilesize) {
		res = Check_game_RTS_FAT(rtsnamebuf, 3);//rts FAT
		if (res != 0) {
			rtsfilesize = 0;
		}
	}

	return rtsfilesize;
}
//------------------------------------------------------------------
//Torii: reads the listed words from the ROM file; true if all are IRQ
//handler literals. Another revision of a game keeps them elsewhere, and
//patching the listed words there breaks it.
static u32 Table_matches_rom(TCHAR *gamefilename, const u32 *offsets, int count)
{
	u32 word;
	UINT got;
	u32 ok;
	if (f_open(&gfile, gamefilename, FA_READ) != FR_OK)
		return 0;
	ok = 1;
	for (int ii = 0; ok && ii < count; ii++) {
		ok = (f_lseek(&gfile, (FSIZE_t)offsets[ii] * 4) == FR_OK) &&
		     (f_read(&gfile, &word, 4, &got) == FR_OK) && (got == 4) && Is_irq_literal(word);
	}
	f_close(&gfile);
	return ok;
}
//------------------------------------------------------------------
//Torii: the table lookup is bounds-checked and skips blank game codes. Of
//the entries for this code, the first that matches this ROM is used; if
//none does, the kernel scans the ROM.
u32 use_internal_engine(u8 gamecode[], TCHAR *gamefilename)
{
	const u32 *offsets;
	u32 pos = 0;
	int count;
	do {
		count = Find_irq_table_entry((const u32 *)reset_table, sizeof(reset_table) / 4, gamecode, &pos,
					     &offsets);
		if (count < 0) {
			if (pos)
				tlog("patch: the built-in table doesn't match this ROM, scanning it");
			return 0;
		}
	} while (!Table_matches_rom(gamefilename, offsets, count));
	g_Offset = 0;
	iCount2 = 0;
	for (int ii = 0; ii < count; ii++) {
		Add2(offsets[ii], 0x3007FF4);//0x3007FFC offset
	}
	return 1;
}
//------------------------------------------------------------------
//Torii: the code patches for exact ROMs in source/irqpatch.c. Call it only
//once the ROM is in PSRAM (an SD launch), as it checks the words there.
void Apply_rom_patches(void)
{
	g_Offset = 0;
	u8 header[0xC0];
	SetPSRampage(0);
	for (u32 i = 0; i < sizeof(header) / 2; i++)
		((u16 *)header)[i] = ((vu16 *)PSRAMBase_S98)[i];
	for (u32 i = 0; i < rom_patch_count; i++) {
		const ST_rom_patch *p = &rom_patches[i];
		if (Rom_patch_matches(p, header) && !memcmp(&header[0xAC], GAMECODE, 4) &&
		    ((vu32 *)PSRAMBase_S98)[p->offset / 4] == p->expected)
			Add2(p->offset / 4, p->value);
	}
}
//------------------------------------------------------------------
void Patch_SpecialROM_sleepmode(void)
{
	g_Offset = 0;
	//
	switch (*(u32 *)GAMECODE) {
	case 0x4A4E4941://0414 - Initial D - Another Stage(JP)
		Add2(0x11C / 4, 0xE241100C);//sub r1,r1,0xC
		break;
	case 0x504D3941://0643 - Motoracer Advance(EU) OK
		Add2(0x2608 / 4, 0xE5810FF4);
		break;
	case 0x455A5641://0826 - Super Bubble Pop(US)
		Add2(0xDC / 4, 0xE3A01A07);
		Add2(0x147D8 / 4, 0xE58030F4);
		break;
	case 0x45533841://0854 - Digimon - Battle Spirit(US) OK
		Add2(0x110 / 4, 0xE241100C);
		break;
	case 0x45523241://0911 Bratz(US)
		Add2(0x28610 / 4, 0x39284939);
		break;
	case 0x454E3941://0913 - Piglet's Big Game(US)
		Add2(0x97A8 / 4, 0xE5810FF4);
		break;
	case 0x50523241://1017 Bratz(EU)
		Add2(0x28614 / 4, 0x39284939);
		break;
	case 0x50464C41://1056 - Dragon Ball Z - The Legacy of Goku II(EU)
		Add2(0x211D8 / 4, 0x63484916);
		break;
	case 0x45464C41://1085 - Dragon Ball Z - The Legacy of Goku II(US)
		Add2(0x20D28 / 4, 0x63484916);
		break;
	case 0x50533841://1133 - Digimon - Battle Spirit(EU)
		Add2(0x110 / 4, 0xE241100C);
		break;
	case 0x584E3941://1178 - Piglet's Big Game(EU)
		Add2(0x100 / 4, 0xE1A00000);
		Add2(0x9834 / 4, 0xE5810FF4);
		break;
	case 0x45524E41://1260 - Cartoon Network - Speedway(US)
		Add2(0x79B8 / 4, 0x63504A31);
		break;
	case 0x505A5641://1361 - Super Bubble Pop(EU)
		Add2(0xDC / 4, 0xE3A01A07);
		Add2(0x14938 / 4, 0xE58030F4);
		break;
	case 0x454D5842://1534 - XS Moto(US)
		Add2(0x7A7C / 4, 0x63504A31);
		break;
	case 0x45474c41://0434 - Dragon Ball Z - The Legacy of Goku(US)
		Add2(0x9A14 / 4, 0x3007FBC);
		break;
	case 0x4A464C41://1591 - Dragon Ball Z - The Legacy of Goku II International(JP)
		Add2(0x22CA8 / 4, 0x63484916);
		break;
	case 0x45334742://1646 - Dragon Ball Z - Buu's Fury(US) OK
		Add2(0x3F3DC / 4, 0x63484916);
		break;
	case 0x45554642://1786 - Fear Factor - Unleashed(US)
		Add2(0x2E20C / 4, 0x4A576350);
		break;
	case 0x45574C42://1953 - LEGO Star Wars - The Video Game(UE)
	case 0x4A574C42://2052 - LEGO Star Wars - The Video Game(JP)
		Add2(0x14C / 4, 0x03007FF0);
		break;
	case 0x45345442://2079 - Dragon Ball GT - Transformation(US)
		Add2(0x34FF8 / 4, 0x63484916);
		break;
	case 0x45455742://2151 - Whac-A-Mole(US)
		Add2(0xBDAC / 4, 0x49296348);
		break;
	case 0x45444342://2152 - Cinderella - Magical Dreams(US)
		Add2(0xA7A8 / 4, 0x49266348);
		break;
	case 0x45363842://2242 - Hello Kitty - Happy Party Pals(US)
		Add2(0x292BC / 4, 0x63484916);
		break;
	case 0x50495742://2255 - Winx Club(EU)
		Add2(0xB22C / 4, 0x49296348);
		break;
	case 0x45495742://2301 - Winx Club(US)
		Add2(0xB22C / 4, 0x49296348);
		break;
	case 0x58363842://2305 - Hello Kitty - Happy Party Pals(EU)
		Add2(0x2245C / 4, 0x63484916);
		break;
	case 0x50444342://2430 - Cinderella - Magical Dreams(EU)
		Add2(0xA7A8 / 4, 0x49296348);
		break;
	case 0x45545A42://2466 - VeggieTales - LarryBoy and the Bad Apple(US)
		Add2(0xB448 / 4, 0x63514A31);
		break;
	case 0x45465542://2472 - 2 Games in 1 - Dragon Ball Z - Buu's Fury + Dragon Ball GT - Transformation(US)
		Add2(0x307C / 4, 0x63484916);
		Add2(0x6FAA8 / 4, 0x63484916);
		Add2(0x835244 / 4, 0x63484916);
		break;
	case 0x45364C42://2487 - My Little Pony - Crystal Princess - The Runaway Rainbow(US)
		Add2(0x228B0 / 4, 0x63484916);
		break;
	case 0x45375442://2597 - Tonka - On the Job(US)
		Add2(0x21548 / 4, 0x63484916);
		break;
	case 0x454D3941://2602 - Motoracer Advance(US)
		Add2(0x2654 / 4, 0xE5810FF4);
		break;
	case 0x50363842://2627 - Hello Kitty - Happy Party Pals(EU)
		Add2(0x292BC / 4, 0x63484916);
		break;

		//one 0x3007FFC
	case 0x45544641://0096 - F-14 Tomcat(UE)
		Add2(0xE0 / 4, 0xE3A01A07);
		break;
	case 0x454D5041://0241 - Planet Monsters(US)
		Add2(0x1421C / 4, 0x494D6345);
		break;
	case 0x504D5041://0314 - Planet Monsters(EU)
		Add2(0x1508C / 4, 0x494D6345);
		break;
	case 0x50534341://0338 - Casper(EU)
		Add2(0xAD64 / 4, 0x494D6345);
		break;
	case 0x50514B41://0500 - Kong - The Animated Series(EU)
		Add2(0x13A0 / 4, 0x494D6345);
		break;
	case 0x50505641://0549 - V.I.P.(EU)
		Add2(0x1374 / 4, 0x494D6345);
		break;
	case 0x45413541://0685 - Bionicle - Matoran Adventures(UE)
		Add2(0x14B7C8 / 4, 0xE58320F4);
		break;
	case 0x50524941://0843 - Inspector Gadget Racing(EU)
		Add2(0x158 / 4, 0xE3A00005);
		break;
	case 0x45514B41://1915 - Kong - The Animated Series(US)
		Add2(0x13A0 / 4, 0x494D6345);
		break;
	case 0x45534341://2117 - Casper(US)
		Add2(0xAD64 / 4, 0x494D6345);
		break;
	}
}
//------------------------------------------------------------------
void Patch_SpecialROM_TrimSize(void)
{
	switch (*(u32 *)GAMECODE) {
	case 0x4A4D4F41://1058 - Disney Sports - Motocross(JP)
	case 0x504D4F41://1068 - Disney Sports - Motocross(EU)
		iTrimSize = 0xFE3000;
		break;
	case 0x4A494442://1176 - Koinu to Issho - Aijou Monogatari(JP)
	case 0x4A324942://1741 - Koinu to Issho 2(JP)
	case 0x454A4142://1864 - Banjo Pilot(US)
	case 0x504A4142://1899 - Banjo Pilot(EU)
	case 0x4A324D42://2045 - Momotarou Densetsu G - Gold Deck wo Tsukure!(JP)
	case 0x4A464842://2071 - Twin Series 4 - Hamu Hamu Monster EX + F Puzzle Hamusuta(JP)
	case 0x50514442://2214 - Donkey Kong Country 3(EU)
	case 0x45514442://2220 - Donkey Kong Country 3(US)
	case 0x4A514442://2270 - Super Donkey Kong Country 3(JP)
	case 0x4A564642:  //2286 - Twin Series 1 - Mezase Debut! Fashion Designer Monogatari + Kawaii Pet Game Gallery 2(JP)
	case 0x4A575A42:  //2335 - Akagi(JP)
	case 0x50385442:  //2342 - 2 Games in 1 - Teenage Mutant Ninja Turtles + Teenage Mutant Ninja Turtles 2 - Battle Nexus(EU)
	case 0x50384E41:  //2351 - Tales of Phantasia(EU)
	case 0x4A534842:  //2457 - Hamster Monogatari 3EX + 4 Special(JP)
	case 0x45385442:  //2526 - 2 Games in 1 - Teenage Mutant Ninja Turtles + Teenage Mutant Ninja Turtles 2 - Battle Nexus(US)
		iTrimSize = 0xFE4000;
		break;
	case 0x45505342:  //1553 - Spider-Man 2(UE)
	case 0x58505342:  //1565 - Spider-Man 2(EU)
	case 0x49505342:  //1657 - Spider-Man 2(IT).zip
	case 0x45585142:  //2618 - Superman Returns - Fortress of Solitude(UE)
		iTrimSize = 0xFE1000;
		break;
	case 0x44504C42:  //1815 - 2 Games in 1 - Disneys Konig der Lowen + Disneys Prinzessinnen(DE)
		iTrimSize = 0x3A9FD0;
		break;
	case 0x46504C42:  //1827 - 2 Games in 1 - Roi Lion, Le + Disney Princesse(FR)
		iTrimSize = 0x5ABF00;
		break;
	case 0x58424C42:  //1867 - 2 Games in 1 - Brother Bear + Lion King, The(EU)
		iTrimSize = 0xFFB510;
		break;
	case 0x50413542:  //2053 - 2 Games in 1 - Spyro - Season of Ice + Crash Bandicoot 2 - N-Tranced(EU)
		iTrimSize = 0x5E3CC0;
		break;
	case 0x45533842:  //2394 - 2 Games in 1 - Spyro - Season of Ice + Spyro 2 - Season of Flame(US)
		iTrimSize = 0x7CFF00;
		break;
	case 0x46425742:  //2503 - 2 Games in 1 - Disney Princesse + Frere des Ours(FR) cant work
		iTrimSize = 0x7FF000;
		break;
	case 0x49425742:  //2703 - 2 Games in 1 - Disney Principesse + Koda, Fratello Orso(IT) cant work
		iTrimSize = 0x7FEC30;
		break;
	case 0x44425742:  //2009 - 2 Games in 1 - Disneys Prinzessinnen + Baren Bruder(DE)
	case 0x53425742:  //2012 - 2 Games in 1 - Disney Princesas + Hermano Oso(ES)
		iTrimSize = 0x731000;
		break;
	case 0x53434B42:  //2361 - Shin-chan - Aventuras en Cineland(ES)
		iTrimSize = 0xAFF000;
		break;
	case 0x50504C42:  //2745 - 2 Games in 1 - Lion King, The + Disney Princess(EU)
	case 0x50425742:  //2780 - 2 Games in 1 - Disney Princess + Brother Bear(EU)
		iTrimSize = 0x738000;
		break;
	}
}
//------------------------------------------------------------------
/*void Check_Fire_Emblem(void)
{
	u32 code1 = 0x47004800;
	u32 address1[5];
	u32 code2[5];
	u32 patchaddress;
	u32 Baseaddress=0x08000000;
	u8* patchbuffer = (u8*)_UnusedVram ;
	u8 * p_patch_start;
	u8 * p_patch_end;
	u8 * p_modify_address;
	u32 modify_val=0;
	 
	u8 have=0;
	
	memset(address1,0x00,sizeof(address1));
	switch(*(u32*)GAMECODE)
	{
		case 0x4A454641://0378 - Fire Emblem - Fuuin no Tsurugi(JP)
		{
			address1[0]=0x858B0;
			address1[1]=0x85048;
			address1[2]=0x84FF0;
			address1[3]=0x85194;
			address1[4]=0x850F8;
			patchaddress=0x7FF100;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0x17;
			code2[2]=Baseaddress+patchaddress+0x27;
			code2[3]=Baseaddress+patchaddress+0x35;
			code2[4]=Baseaddress+patchaddress+0x47;
		  p_patch_start = (u8*)Fire_Emblem_0378_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_0378_patch_end;
		  have = 1;
		}break;
		case 0x4A384542://1692 - Fire Emblem - Seima no Kouseki(JP)
		{
			address1[0]=0xA9844;
			address1[1]=0xA989C;
			address1[2]=0xA99F8;
			address1[3]=0xA9B14;
			address1[4]=0xAA5D0;
			patchaddress=0xF00000;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x2D;
			code2[4]=Baseaddress+patchaddress+0x3D;
		  p_patch_start = (u8*)Fire_Emblem_1692_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_1692_patch_end;
		  have = 1;				
		}break;
		case 0x4A374541://0979 - Fire Emblem - Rekka no Ken(JP)
		{
			address1[0]=0xA0FE0;
			address1[1]=0xA1038;
			address1[2]=0xA1178;
			address1[3]=0xA1264;
			address1[4]=0xA1BA8;
			patchaddress=0xFFF900;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x1D;
			code2[4]=Baseaddress+patchaddress+0x2D;
		  p_patch_start = (u8*)Fire_Emblem_A_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_A_patch_end;
		  p_modify_address = (u8*)Modify_address_A;
		  modify_val = 0x80B3DAF;
		  have = 1;
		}break;
		case 0x45374541://1235 - Fire Emblem(US)
		{
			address1[0]=0xA0654;
			address1[1]=0xA06AC;
			address1[2]=0xA07EC;
			address1[3]=0xA08D8;
			address1[4]=0xA1214;
			patchaddress=0xFFF900;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x1D;
			code2[4]=Baseaddress+patchaddress+0x2D;
		  p_patch_start = (u8*)Fire_Emblem_A_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_A_patch_end;
		  p_modify_address = (u8*)Modify_address_A;
		  modify_val = 0x80B2F8B;
		  have = 1;
		}break;
		case 0x58374541://1574 - Fire Emblem(EU)
		{
			address1[0]=0xA09C8;
			address1[1]=0xA0A20;
			address1[2]=0xA0B60;
			address1[3]=0xA0C4C;
			address1[4]=0xA1560;
			patchaddress=0xFFF900;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x1D;
			code2[4]=Baseaddress+patchaddress+0x2D;
		  p_patch_start = (u8*)Fire_Emblem_A_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_A_patch_end;
		  p_modify_address = (u8*)Modify_address_A;
		  modify_val = 0x80B3A57;
		  have = 1;
		}break;
		case 0x59374541://1575 - Fire Emblem(EU)
		{
			address1[0]=0xA09CC;
			address1[1]=0xA0A24;
			address1[2]=0xA0B64;
			address1[3]=0xA0C50;
			address1[4]=0xA1564;
			patchaddress=0xFFF900;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x1D;
			code2[4]=Baseaddress+patchaddress+0x2D;
		  p_patch_start = (u8*)Fire_Emblem_A_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_A_patch_end;
		  p_modify_address = (u8*)Modify_address_A;
		  modify_val = 0x80B3A3B;
		  have = 1;
		}break;
		case 0x45384542://1997 - Fire Emblem - The Sacred Stones(US)
		{
			address1[0]=0xA4E00;
			address1[1]=0xA4E58;
			address1[2]=0xA4FDC;
			address1[3]=0xA50FC;
			address1[4]=0xA5BB8;
			patchaddress=0xFFF900;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x1D;
			code2[4]=Baseaddress+patchaddress+0x2D;
		  p_patch_start = (u8*)Fire_Emblem_B_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_B_patch_end;
		  p_modify_address = (u8*)Modify_address_B;
		  modify_val = 0x80B5D6B;
		  have = 1;
		}break;
		case 0x50384542://2215 - Fire Emblem - The Sacred Stones(EU)
		{
			address1[0]=0xA5738;
			address1[1]=0xA5790;
			address1[2]=0xA5914;
			address1[3]=0xA5A34;
			address1[4]=0xA64F0;
			patchaddress=0x1FFDD00;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1D;
			code2[3]=Baseaddress+patchaddress+0x1D;
			code2[4]=Baseaddress+patchaddress+0x2D;
		  p_patch_start = (u8*)Fire_Emblem_B_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_B_patch_end;
		  p_modify_address = (u8*)Modify_address_B;
		  modify_val = 0x80B670F;
		  have = 1;
		}break;
		case 0x43454641://Fire_Emblem_(Prototype,_iQue)
		{
			address1[0]=0x84FF0;
			address1[1]=0x85048;
			address1[2]=0x850F8;
			address1[3]=0x85194;
			address1[4]=0x858B0;
			patchaddress=0xFFCC00;
			code2[0]=Baseaddress+patchaddress+0x1;
			code2[1]=Baseaddress+patchaddress+0xF;
			code2[2]=Baseaddress+patchaddress+0x1F;
			code2[3]=Baseaddress+patchaddress+0x2D;
			code2[4]=Baseaddress+patchaddress+0x3D;
		  p_patch_start = (u8*)Fire_Emblem_iQue_patch_start;
		  p_patch_end  	= (u8*)Fire_Emblem_iQue_patch_end;
		  //p_modify_address = (u8*)Modify_address_B;
		  //modify_val = 0x80B670F;
		  have = 1;
		}break;
	}
	
	if(have){
    for(u32 i=0;i<5;i++)
    {
    	if(address1[i] != 0){
    		Write(address1[i],(u8*)&code1 , 4); 
      	Write(address1[i]+4,(u8*)&code2[i],4);
      }
    }			

	  u32 copysize = p_patch_end - p_patch_start ;
	  dmaCopy((void*)p_patch_start,patchbuffer, copysize);	
	  if( modify_val)
	  {
	  	u32 p_modify_address_offset = p_modify_address-p_patch_start;
	  	*(vu32*)(patchbuffer+p_modify_address_offset) = modify_val;
	  } 		  
	  Write(patchaddress, patchbuffer,copysize);
	  Set_AUTO_save(0x00);
	}
	else{
		Set_AUTO_save(0x01);
	}	
}
*/
//------------------------------------------------------------------
void Patch_somegame(u32 *Data)
{
	u32 size = 0x7FF0;
	if (*(u32 *)GAMECODE == 0x50424732) {
		for (u32 ii = 0; ii < 0x100; ii++) {
			if (0x3000000 == Data[ii]) {
				if (0x8000 == Data[ii + 1]) {
					Write((ii + 1) * 4, (u8 *)&size, 4);
				}
			}
		}
	}
}