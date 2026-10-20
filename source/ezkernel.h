#ifndef EZKERNEL_HEADER
#define EZKERNEL_HEADER

#include "ff.h"
#include "ez_define.h"
#include "savename.h"

typedef struct FM_NOR_FILE_SECT {////save to nor
	char filename[100];
	u16 rompage;
	u16 have_patch;
	u16 have_RTS;
	u16 is_64MBrom;//reserved;
	u32 filesize;
	u8 savemode;
	u8 reserved1;
	u16 reserved2;
	char gamename[0x10];
} FM_NOR_FS;

//----------------------------
extern DWORD Get_NextCluster(FFOBJID *obj, DWORD clst);
extern DWORD ClustToSect(FATFS *fs, DWORD clst);

extern FM_NOR_FS pNorFS[MAX_NOR] EWRAM_BSS;
extern u8 pReadCache[MAX_pReadCache_size] EWRAM_BSS;
// Says pReadCache holds nothing now (ez_define.h). In the emulator,
// mgba-shot then fills it with 0xFF, so code that reads what an earlier owner
// left there (a stale function pointer, length or count) fails in the tests
// instead of on a cart. On a cart it does nothing.
void readcache_poison(void);
extern u8 __attribute__((aligned(4))) GAMECODE[4];

extern u16 gl_reset_on;
extern u16 gl_rts_on;
extern u16 gl_sleep_on;
extern u16 gl_cheat_on;

void CheckLanguage(void);
void CheckSwitch(void);
u32 Game_hardware(u32 is_EMU, const u8 *gamecode, u8 gb_cart_type);
// The game NOR list's game named name, or game_total_NOR if none.
u32 NOR_game_named(const char *name);
// The text of error error_num (gl_error_*), as Show_error_num shows it.
const char *Error_message(u8 error_num);
u8 Check_saveMODE(u8 gamecode[]);
u8 Get_saveMODE(u8 Save_num, u32 gamefilesize);
u32 Check_file_type(TCHAR *pfilename);
u32 Get_savefilesize(BYTE saveMODE);
// Empties FAT_table_buffer, the cart's table of a file's cluster runs and parameters.
void init_FAT_table(void);
// IWRAM code must run from IWRAM while the ROM page changes, so it is never
// static: GCC would inline it into callers in ROM.
u32 Loadsavefile(TCHAR *filename);
// Save_savefile's results
#define SAVE_FAILED 0   // no file made, or the write fell short (a full card)
#define SAVE_WRITTEN 1  // all of it reached the card
#define SAVE_NOTHING 2  // savesize 0: nothing to write, no file touched
u32 Save_savefile(TCHAR *filename, u32 savesize);
u32 IWRAM_CODE LoadEMU2PSRAM(TCHAR *filename, u32 is_EMU);

void delay(u32 R0);
u32 LoadRTSfile(TCHAR *filename);

// The file name the launch path opens, in the folder currentpath
// (TCHAR[MAX_path_len], declared where it is used, as other files name
// locals so). actions.c sets both for a launch, and puts the folder back
// after: a copy in RAM, as the library's names are in PSRAM, which a launch
// fills.
extern TCHAR current_filename[GAME_NAME_MAX + 1];
extern u32 game_total_SD;
extern u32 folder_total;
extern u32 game_total_NOR;

// A game on its way to its launch (actions.c).
struct game_pick {
	TCHAR *name;      // its file name in currentpath (current_filename)
	u32 is_emu;       // Check_file_type: 0 GBA, 1 GBC, 2 GB, 3 NES
	u32 havecht;      // Check_cheat_file: 0 no cheat file
	u8 save_num;      // the SAVE TYPE line: 0 AUTO, 1-5 a type
	u8 old_save_num;  // what its .mde file says
};
// Fills g for g->name: 0 if it isn't a game. A GBA game reads its cheat and
// save type files; an emulated one starts only with Game_launch's line 0.
u32 Game_pick(struct game_pick *g);
// Launches g as line menu_line of the v0.3 game menu said (0 CLEAN BOOT, 1 BOOT WITH ADDON,
// 2 WRITE TO NOR CLEAN, 3 WRITE TO NOR ADDON), L held for a hard reset,
// with no engine session up (its dialogs run their own). Returns only if it
// didn't start a game: 0 nothing written, 1 the game NOR may have changed,
// 2 a game was written to the game NOR.
u8 Game_launch(struct game_pick *g, u32 menu_line, u32 key_L);
// Starts NOR list game index; returns only if it failed. No engine session
// up, as for Game_launch.
void NOR_launch(u32 index, u32 key_L);
#endif