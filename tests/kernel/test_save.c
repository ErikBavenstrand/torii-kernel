// Save type lookup and save sizes (source/ezkernelnew.c).
#include <string.h>

#include <gba_base.h>

#include "torii_test.h"

typedef unsigned char BYTE;

u8 Check_saveMODE(u8 gamecode[]);
u8 Get_saveMODE(u8 Save_num, u32 gamefilesize);
u32 Get_savefilesize(BYTE saveMODE);
extern u8 GAMECODE[4];

// The table from source/saveMODE.h, ended by a "FFFF" entry.
typedef struct {
	char gamecode[4];
	u8 savemode;
} save_mode_entry;
extern const save_mode_entry saveMODE_table[];

static u8 lookup(const char *code)
{
	u8 gamecode[4];
	memcpy(gamecode, code, 4);
	return Check_saveMODE(gamecode);
}

void test_check_save_mode(void)
{
	CHECK_EQ(lookup("BPEE"), 0x31);  // Pokémon Emerald: Flash 128K
	CHECK_EQ(lookup("BPRE"), 0x31);  // Pokémon FireRed
	CHECK_EQ(lookup("AFZJ"), 0x11);  // F-Zero (JP): SRAM, the first entry
	CHECK_EQ(lookup("AMAJ"), 0x21);  // Super Mario Advance (JP): EEPROM 512
	CHECK_EQ(lookup("ZZZZ"), 0x10);  // unknown games get 0x10
	CHECK_EQ(lookup("ADME"), 0x21);  // Doom: EEPROM 512 (ez-flash/omega-de-kernel#3)
	CHECK_EQ(lookup("A9DP"), 0x21);  // Doom II
	CHECK_EQ(lookup("AYGE"), 0x21);  // Gauntlet - Dark Legacy
	CHECK_EQ(lookup("FSME"), 0x21);  // Classic NES Series - Super Mario Bros.
	CHECK_EQ(lookup("FZLE"), 0x22);  // Classic NES Series - The Legend of Zelda: EEPROM 8K
}

void test_nes_classics_use_eeprom(void)
{
	// Classic NES Series and Famicom Mini games (codes starting with F) all
	// save to EEPROM; the table used to give them SRAM.
	for (unsigned i = 0; i < 3000 && memcmp(saveMODE_table[i].gamecode, "FFFF", 4); i++) {
		if (saveMODE_table[i].gamecode[0] != 'F')
			continue;
		u8 mode = saveMODE_table[i].savemode;
		if (mode != 0x21 && mode != 0x22)
			tlog_error("%.4s has save mode %02X, not EEPROM", saveMODE_table[i].gamecode, mode);
		CHECK(mode == 0x21 || mode == 0x22);
	}
}

void test_get_save_mode(void)
{
	memcpy(GAMECODE, "BPEE", 4);
	CHECK_EQ(Get_saveMODE(0, 0x1000000), 0x31);  // auto: looked up by game code
	CHECK_EQ(Get_saveMODE(1, 0), 0x11);           // SRAM
	CHECK_EQ(Get_saveMODE(2, 0x1000000), 0x22);   // EEPROM 8K
	CHECK_EQ(Get_saveMODE(2, 0x1200001), 0x23);   // EEPROM 8K, game over 18 MB
	CHECK_EQ(Get_saveMODE(3, 0), 0x21);           // EEPROM 512
	CHECK_EQ(Get_saveMODE(4, 0), 0x32);           // Flash 64K
	CHECK_EQ(Get_saveMODE(5, 0), 0x31);           // Flash 128K
}

void test_save_file_size(void)
{
	CHECK_EQ(Get_savefilesize(0x11), 0x8000);
	CHECK_EQ(Get_savefilesize(0x21), 0x200);
	CHECK_EQ(Get_savefilesize(0x22), 0x2000);
	CHECK_EQ(Get_savefilesize(0x23), 0x2000);
	CHECK_EQ(Get_savefilesize(0x31), 0x20000);
	CHECK_EQ(Get_savefilesize(0x32), 0x10000);
	CHECK_EQ(Get_savefilesize(0x10), 0x10000);  // unknown: 64 KB
}

void test_save_mode_table(void)
{
	// Check_saveMODE gives up after 3000 entries, so the table must end
	// before that, and every entry must name a save type the kernel knows.
	unsigned i;
	for (i = 0; i < 3000 && memcmp(saveMODE_table[i].gamecode, "FFFF", 4); i++) {
		u8 mode = saveMODE_table[i].savemode;
		int known = mode == 0x00 || mode == 0x10 || mode == 0x11 || mode == 0x21 || mode == 0x22 ||
		            mode == 0x23 || mode == 0x31 || mode == 0x32 || mode == 0x33;
		if (!known)
			tlog_error("save table entry %u (%.4s) has unknown save mode %02X", i, saveMODE_table[i].gamecode, mode);
		CHECK(known);
	}
	CHECK(i < 3000);
	tlog("save table: %u entries", i);
}
